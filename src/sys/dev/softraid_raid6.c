/* $OpenBSD: softraid_raid6.c,v 1.39 2013/03/29 13:05:47 jsing Exp $ */
/*
 * Copyright (c) 2009 Marco Peereboom <marco@peereboom.us>
 * Copyright (c) 2009 Jordan Hargrave <jordan@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bio.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/disk.h>
#include <sys/rwlock.h>
#include <sys/queue.h>
#include <sys/fcntl.h>
#include <sys/disklabel.h>
#include <sys/mount.h>
#include <sys/sensors.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/uio.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_disk.h>

#include <dev/softraidvar.h>
#include <dev/rndvar.h>

uint8_t *gf_map[256];
uint8_t	gf_pow[768];
int	gf_log[256];

/* RAID 6 functions. */
int	sr_raid6_create(struct sr_discipline *, struct bioc_createraid *,
	    int, int64_t);
int	sr_raid6_assemble(struct sr_discipline *, struct bioc_createraid *,
	    int, void *);
int	sr_raid6_alloc_resources(struct sr_discipline *);
int	sr_raid6_free_resources(struct sr_discipline *);
int	sr_raid6_rw(struct sr_workunit *);
int	sr_raid6_openings(struct sr_discipline *);
void	sr_raid6_intr(struct buf *);
void	sr_raid6_set_chunk_state(struct sr_discipline *, int, int);
void	sr_raid6_set_vol_state(struct sr_discipline *);

void	sr_raid6_xorp(void *, void *, int);
void	sr_raid6_xorq(void *, void *, int, int);
int	sr_raid6_addio(struct sr_workunit *wu, int, daddr64_t, daddr64_t,
	    void *, int, int, void *, void *, int);
void	sr_dump(void *, int);
void	sr_raid6_scrub(struct sr_discipline *);
int	sr_failio(struct sr_workunit *);

void	*sr_get_block(struct sr_discipline *, int);
void	sr_put_block(struct sr_discipline *, void *, int);

void	gf_init(void);
uint8_t gf_inv(uint8_t);
int	gf_premul(uint8_t);
uint8_t gf_mul(uint8_t, uint8_t);

#define SR_NOFAIL		0x00
#define SR_FAILX		(1L << 0)
#define SR_FAILY		(1L << 1)
#define SR_FAILP		(1L << 2)
#define SR_FAILQ		(1L << 3)

struct sr_raid6_opaque {
	int      gn;
	void	*pbuf;
	void	*qbuf;
};

/* discipline initialisation. */
void
sr_raid6_discipline_init(struct sr_discipline *sd)
{
	/* Initialize GF256 tables. */
	gf_init();

	/* Fill out discipline members. */
	sd->sd_type = SR_MD_RAID6;
	strlcpy(sd->sd_name, "RAID 6", sizeof(sd->sd_name));
	sd->sd_capabilities = SR_CAP_SYSTEM_DISK | SR_CAP_AUTO_ASSEMBLE |
	    SR_CAP_REDUNDANT;
	sd->sd_max_wu = SR_RAID6_NOWU;

	/* Setup discipline specific function pointers. */
	sd->sd_alloc_resources = sr_raid6_alloc_resources;
	sd->sd_assemble = sr_raid6_assemble;
	sd->sd_create = sr_raid6_create;
	sd->sd_free_resources = sr_raid6_free_resources;
	sd->sd_openings = sr_raid6_openings;
	sd->sd_scsi_rw = sr_raid6_rw;
	sd->sd_scsi_intr = sr_raid6_intr;
	sd->sd_set_chunk_state = sr_raid6_set_chunk_state;
	sd->sd_set_vol_state = sr_raid6_set_vol_state;
}

int
sr_raid6_create(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, int64_t coerced_size)
{

	if (no_chunk < 4)
		return EINVAL;

	/*
	 * XXX add variable strip size later even though MAXPHYS is really
	 * the clever value, users like * to tinker with that type of stuff.
	 */
        sd->sd_meta->ssdi.ssd_strip_size = MAXPHYS;
        sd->sd_meta->ssdi.ssd_size = (coerced_size &
	    ~((sd->sd_meta->ssdi.ssd_strip_size >> DEV_BSHIFT) - 1)) *
	    (no_chunk - 2);

	/* only if stripsize <= MAXPHYS */
	sd->sd_max_ccb_per_wu = max(6, 2 * no_chunk);

	return 0;
}

int
sr_raid6_assemble(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, void *data)
{

	/* only if stripsize <= MAXPHYS */
	sd->sd_max_ccb_per_wu = max(6, 2 * sd->sd_meta->ssdi.ssd_chunk_no);

	return 0;
}

int
sr_raid6_openings(struct sr_discipline *sd)
{
	return (sd->sd_max_wu >> 1); /* 2 wu's per IO */
}

int
sr_raid6_alloc_resources(struct sr_discipline *sd)
{
	int			rv = EINVAL;

	DNPRINTF(SR_D_DIS, "%s: sr_raid6_alloc_resources\n",
	    DEVNAME(sd->sd_sc));

	if (sr_wu_alloc(sd))
		goto bad;
	if (sr_ccb_alloc(sd))
		goto bad;

	/* setup runtime values */
	sd->mds.mdd_raid6.sr6_strip_bits =
	    sr_validate_stripsize(sd->sd_meta->ssdi.ssd_strip_size);
	if (sd->mds.mdd_raid6.sr6_strip_bits == -1)
		goto bad;

	rv = 0;
bad:
	return (rv);
}

int
sr_raid6_free_resources(struct sr_discipline *sd)
{
	int			rv = EINVAL;

	DNPRINTF(SR_D_DIS, "%s: sr_raid6_free_resources\n",
	    DEVNAME(sd->sd_sc));

	sr_wu_free(sd);
	sr_ccb_free(sd);

	rv = 0;
	return (rv);
}

void
sr_raid6_set_chunk_state(struct sr_discipline *sd, int c, int new_state)
{
	int			old_state, s;

	/* XXX this is for RAID 0 */
	DNPRINTF(SR_D_STATE, "%s: %s: %s: sr_raid_set_chunk_state %d -> %d\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    sd->sd_vol.sv_chunks[c]->src_meta.scmi.scm_devname, c, new_state);

	/* ok to go to splbio since this only happens in error path */
	s = splbio();
	old_state = sd->sd_vol.sv_chunks[c]->src_meta.scm_status;

	/* multiple IOs to the same chunk that fail will come through here */
	if (old_state == new_state)
		goto done;

	switch (old_state) {
	case BIOC_SDONLINE:
		switch (new_state) {
		case BIOC_SDOFFLINE:
		case BIOC_SDSCRUB:
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SDOFFLINE:
		if (new_state == BIOC_SDREBUILD) {
			;
		} else
			goto die;
		break;

	case BIOC_SDSCRUB:
		switch (new_state) {
		case BIOC_SDONLINE:
		case BIOC_SDOFFLINE:
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SDREBUILD:
		switch (new_state) {
		case BIOC_SDONLINE:
		case BIOC_SDOFFLINE:
			break;
		default:
			goto die;
		}
		break;

	default:
die:
		splx(s); /* XXX */
		panic("%s: %s: %s: invalid chunk state transition "
		    "%d -> %d", DEVNAME(sd->sd_sc),
		    sd->sd_meta->ssd_devname,
		    sd->sd_vol.sv_chunks[c]->src_meta.scmi.scm_devname,
		    old_state, new_state);
		/* NOTREACHED */
	}

	sd->sd_vol.sv_chunks[c]->src_meta.scm_status = new_state;
	sd->sd_set_vol_state(sd);

	sd->sd_must_flush = 1;
	workq_add_task(NULL, 0, sr_meta_save_callback, sd, NULL);
done:
	splx(s);
}

void
sr_raid6_set_vol_state(struct sr_discipline *sd)
{
	int			states[SR_MAX_STATES];
	int			new_state, i, s, nd;
	int			old_state = sd->sd_vol_status;

	/* XXX this is for RAID 0 */

	DNPRINTF(SR_D_STATE, "%s: %s: sr_raid_set_vol_state\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname);

	nd = sd->sd_meta->ssdi.ssd_chunk_no;

	for (i = 0; i < SR_MAX_STATES; i++)
		states[i] = 0;

	for (i = 0; i < nd; i++) {
		s = sd->sd_vol.sv_chunks[i]->src_meta.scm_status;
		if (s >= SR_MAX_STATES)
			panic("%s: %s: %s: invalid chunk state",
			    DEVNAME(sd->sd_sc),
			    sd->sd_meta->ssd_devname,
			    sd->sd_vol.sv_chunks[i]->src_meta.scmi.scm_devname);
		states[s]++;
	}

	if (states[BIOC_SDONLINE] == nd)
		new_state = BIOC_SVONLINE;
	else if (states[BIOC_SDONLINE] < nd - 2)
		new_state = BIOC_SVOFFLINE;
	else if (states[BIOC_SDSCRUB] != 0)
		new_state = BIOC_SVSCRUB;
	else if (states[BIOC_SDREBUILD] != 0)
		new_state = BIOC_SVREBUILD;
	else if (states[BIOC_SDONLINE] < nd)
		new_state = BIOC_SVDEGRADED;
	else {
		printf("old_state = %d, ", old_state);
		for (i = 0; i < nd; i++)
			printf("%d = %d, ", i,
			    sd->sd_vol.sv_chunks[i]->src_meta.scm_status);
		panic("invalid new_state");
	}

	DNPRINTF(SR_D_STATE, "%s: %s: sr_raid_set_vol_state %d -> %d\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    old_state, new_state);

	switch (old_state) {
	case BIOC_SVONLINE:
		switch (new_state) {
		case BIOC_SVONLINE: /* can go to same state */
		case BIOC_SVOFFLINE:
		case BIOC_SVDEGRADED:
		case BIOC_SVREBUILD: /* happens on boot */
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SVOFFLINE:
		/* XXX this might be a little too much */
		goto die;

	case BIOC_SVSCRUB:
		switch (new_state) {
		case BIOC_SVONLINE:
		case BIOC_SVOFFLINE:
		case BIOC_SVDEGRADED:
		case BIOC_SVSCRUB: /* can go to same state */
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SVBUILDING:
		switch (new_state) {
		case BIOC_SVONLINE:
		case BIOC_SVOFFLINE:
		case BIOC_SVBUILDING: /* can go to the same state */
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SVREBUILD:
		switch (new_state) {
		case BIOC_SVONLINE:
		case BIOC_SVOFFLINE:
		case BIOC_SVDEGRADED:
		case BIOC_SVREBUILD: /* can go to the same state */
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SVDEGRADED:
		switch (new_state) {
		case BIOC_SVOFFLINE:
		case BIOC_SVREBUILD:
		case BIOC_SVDEGRADED: /* can go to the same state */
			break;
		default:
			goto die;
		}
		break;

	default:
die:
		panic("%s: %s: invalid volume state transition %d -> %d",
		    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
		    old_state, new_state);
		/* NOTREACHED */
	}

	sd->sd_vol_status = new_state;
}

/*  modes:
 *   readq: sr_raid6_addio(i, lba, length, NULL, SCSI_DATA_IN,
 *	        SR_CCBF_FREEBUF, qbuf, NULL, 0);
 *   readp: sr_raid6_addio(i, lba, length, NULL, SCSI_DATA_IN,
 *		SR_CCBF_FREEBUF, pbuf, NULL, 0);
 *   readx: sr_raid6_addio(i, lba, length, NULL, SCSI_DATA_IN,
 *		SR_CCBF_FREEBUF, pbuf, qbuf, gf_pow[i]);
 */

int
sr_raid6_rw(struct sr_workunit *wu)
{
	struct sr_workunit	*wu_r = NULL;
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_chunk		*scp;
	int			s, fail, i, gxinv, pxinv;
	daddr64_t		blk, lbaoffs, strip_no, chunk, qchunk, pchunk, fchunk;
	daddr64_t		strip_size, no_chunk, lba, chunk_offs, phys_offs;
	daddr64_t		strip_bits, length, strip_offs, datalen, row_size;
	void		        *pbuf, *data, *qbuf;

	/* blk and scsi error will be handled by sr_validate_io */
	if (sr_validate_io(wu, &blk, "sr_raid6_rw"))
		goto bad;

	strip_size = sd->sd_meta->ssdi.ssd_strip_size;
	strip_bits = sd->mds.mdd_raid6.sr6_strip_bits;
	no_chunk = sd->sd_meta->ssdi.ssd_chunk_no - 2;
	row_size = (no_chunk << strip_bits) >> DEV_BSHIFT;

	data = xs->data;
	datalen = xs->datalen;
	lbaoffs	= blk << DEV_BSHIFT;

	if (xs->flags & SCSI_DATA_OUT)
		/* create write workunit */
		if ((wu_r = scsi_io_get(&sd->sd_iopool, SCSI_NOSLEEP)) == NULL){
			printf("%s: can't get wu_r", DEVNAME(sd->sd_sc));
			goto bad;
		}

	wu->swu_blk_start = 0;
	while (datalen != 0) {
		strip_no = lbaoffs >> strip_bits;
		strip_offs = lbaoffs & (strip_size - 1);
		chunk_offs = (strip_no / no_chunk) << strip_bits;
		phys_offs = chunk_offs + strip_offs +
		    (sd->sd_meta->ssd_data_offset << DEV_BSHIFT);

		/* get size remaining in this stripe */
		length = MIN(strip_size - strip_offs, datalen);

		/* map disk offset to parity/data drive */
		chunk = strip_no % no_chunk;

		qchunk = (no_chunk + 1) - ((strip_no / no_chunk) % (no_chunk+2));
		if (qchunk == 0)
			pchunk = no_chunk + 1;
		else
			pchunk = qchunk - 1;
		if (chunk >= pchunk)
			chunk++;
		if (chunk >= qchunk)
			chunk++;

		lba = phys_offs >> DEV_BSHIFT;

		/* XXX big hammer.. exclude I/O from entire stripe */
		if (wu->swu_blk_start == 0)
			wu->swu_blk_start = (strip_no / no_chunk) * row_size;
		wu->swu_blk_end = (strip_no / no_chunk) * row_size + (row_size - 1);

		fail = 0;
		fchunk = -1;

		/* Get disk-fail flags */
		for (i=0; i< no_chunk+2; i++) {
			scp = sd->sd_vol.sv_chunks[i];
			switch (scp->src_meta.scm_status) {
			case BIOC_SDOFFLINE:
			case BIOC_SDREBUILD:
			case BIOC_SDHOTSPARE:
				if (i == qchunk)
					fail |= SR_FAILQ;
				else if (i == pchunk)
					fail |= SR_FAILP;
				else if (i == chunk)
					fail |= SR_FAILX;
				else {
					/* dual data-disk failure */
					fail |= SR_FAILY;
					fchunk = i;
				}
				break;
			}
		}
		if (xs->flags & SCSI_DATA_IN) {
			if (!(fail & SR_FAILX)) {
				/* drive is good. issue single read request */
				if (sr_raid6_addio(wu, chunk, lba, length,
				    data, xs->flags, 0, NULL, NULL, 0))
					goto bad;
			} else if (fail & SR_FAILP) {
				/* Dx, P failed */
				printf("Disk %llx offline, "
				    "regenerating Dx+P\n", chunk);

				gxinv = gf_inv(gf_pow[chunk]);

				/* Calculate: Dx = (Q^Dz*gz)*inv(gx) */
				memset(data, 0, length);
				if (sr_raid6_addio(wu, qchunk, lba, length, NULL,
				    SCSI_DATA_IN, SR_CCBF_FREEBUF, NULL, data,
				    gxinv))
					goto bad;

				/* Read Dz * gz * inv(gx) */
				for (i = 0; i < no_chunk+2; i++) {
					if  (i == qchunk || i == pchunk || i == chunk)
						continue;

					if (sr_raid6_addio(wu, i, lba,
					   length, NULL, SCSI_DATA_IN,
					   SR_CCBF_FREEBUF, NULL,
					   data, gf_mul(gf_pow[i], gxinv)))
						goto bad;
				}

				/* data will contain correct value on completion */
			} else if (fail & SR_FAILY) {
				/* Dx, Dy failed */
				printf("Disk %llx & %llx offline, "
				    "regenerating Dx+Dy\n", chunk, fchunk);

				gxinv = gf_inv(gf_pow[chunk] ^ gf_pow[fchunk]);
				pxinv = gf_mul(gf_pow[fchunk], gxinv);

				/* read Q * inv(gx + gy) */
				memset(data, 0, length);
				if (sr_raid6_addio(wu, qchunk, lba,
				    length,  NULL, SCSI_DATA_IN,
				    SR_CCBF_FREEBUF, NULL,
				    data, gxinv))
					goto bad;

				/* read P * gy * inv(gx + gy) */
				if (sr_raid6_addio(wu, pchunk, lba,
				    length,  NULL, SCSI_DATA_IN,
				    SR_CCBF_FREEBUF, NULL,
				    data, pxinv))
					goto bad;

				/* Calculate: Dx*gx^Dy*gy = Q^(Dz*gz) ; Dx^Dy = P^Dz
				 *   Q:  sr_raid6_xorp(qbuf, --, length);
				 *   P:  sr_raid6_xorp(pbuf, --, length);
				 *   Dz: sr_raid6_xorp(pbuf, --, length);
				 *	 sr_raid6_xorq(qbuf, --, length, gf_pow[i]);
				 */
				for (i = 0; i < no_chunk+2; i++) {
					if (i == qchunk || i == pchunk ||
					    i == chunk || i == fchunk)
						continue;

					/* read Dz * (gz + gy) * inv(gx + gy) */
					if (sr_raid6_addio(wu, i, lba,
					    length, NULL, SCSI_DATA_IN,
					    SR_CCBF_FREEBUF, NULL, data,
					    pxinv ^ gf_mul(gf_pow[i], gxinv)))
						goto bad;
				}
			} else {
				/* Two cases: single disk (Dx) or (Dx+Q)
				 *   Dx = Dz ^ P (same as RAID5)
				 */
				printf("Disk %llx offline, "
				    "regenerating Dx%s\n", chunk,
				    fail & SR_FAILQ ? "+Q" : " single");

				/* Calculate: Dx = P^Dz
				 *   P:  sr_raid6_xorp(data, ---, length);
				 *   Dz: sr_raid6_xorp(data, ---, length);
				 */
				memset(data, 0, length);
				for (i = 0; i < no_chunk+2; i++) {
					if (i != chunk && i != qchunk) {
						/* Read Dz */
						if (sr_raid6_addio(wu, i, lba,
						    length, NULL, SCSI_DATA_IN,
						    SR_CCBF_FREEBUF, data,
						    NULL, 0))
							goto bad;
					}
				}

				/* data will contain correct value on completion */
			}
		} else {
			/* XXX handle writes to failed/offline disk? */
			if (fail & (SR_FAILX|SR_FAILQ|SR_FAILP))
				goto bad;

			/*
			 * initialize pbuf with contents of new data to be
			 * written. This will be XORed with old data and old
			 * parity in the intr routine. The result in pbuf
			 * is the new parity data.
			 */
			qbuf = sr_get_block(sd, length);
			if (qbuf == NULL)
				goto bad;

			pbuf = sr_get_block(sd, length);
			if (pbuf == NULL)
				goto bad;

			/* Calculate P = Dn; Q = gn * Dn */
			if (gf_premul(gf_pow[chunk]))
				goto bad;
			sr_raid6_xorp(pbuf, data, length);
			sr_raid6_xorq(qbuf, data, length, gf_pow[chunk]);

			/* Read old data: P ^= Dn' ; Q ^= (gn * Dn') */
			if (sr_raid6_addio(wu_r, chunk, lba, length, NULL,
				SCSI_DATA_IN, SR_CCBF_FREEBUF, pbuf, qbuf,
				gf_pow[chunk]))
				goto bad;

			/* Read old xor-parity: P ^= P' */
			if (sr_raid6_addio(wu_r, pchunk, lba, length, NULL,
				SCSI_DATA_IN, SR_CCBF_FREEBUF, pbuf, NULL, 0))
				goto bad;

			/* Read old q-parity: Q ^= Q' */
			if (sr_raid6_addio(wu_r, qchunk, lba, length, NULL,
				SCSI_DATA_IN, SR_CCBF_FREEBUF, qbuf, NULL, 0))
				goto bad;

			/* write new data */
			if (sr_raid6_addio(wu, chunk, lba, length, data,
			    xs->flags, 0, NULL, NULL, 0))
				goto bad;

			/* write new xor-parity */
			if (sr_raid6_addio(wu, pchunk, lba, length, pbuf,
			    xs->flags, SR_CCBF_FREEBUF, NULL, NULL, 0))
				goto bad;

			/* write new q-parity */
			if (sr_raid6_addio(wu, qchunk, lba, length, qbuf,
			    xs->flags, SR_CCBF_FREEBUF, NULL, NULL, 0))
				goto bad;
		}

		/* advance to next block */
		lbaoffs += length;
		datalen -= length;
		data += length;
	}

	s = splbio();
	if (wu_r) {
		/* collide write request with reads */
		wu_r->swu_blk_start = wu->swu_blk_start;
		wu_r->swu_blk_end = wu->swu_blk_end;

		wu->swu_state = SR_WU_DEFERRED;
		wu_r->swu_collider = wu;
		TAILQ_INSERT_TAIL(&sd->sd_wu_defq, wu, swu_link);

		wu = wu_r;
	}

	/* rebuild io, let rebuild routine deal with it */
	if (wu->swu_flags & SR_WUF_REBUILD)
		goto queued;

	/* current io failed, restart */
	if (wu->swu_state == SR_WU_RESTART)
		goto start;

	/* deferred io failed, don't restart */
	if (wu->swu_state == SR_WU_REQUEUE)
		goto queued;

	if (sr_check_io_collision(wu))
		goto queued;

start:
	sr_raid_startwu(wu);
queued:
	splx(s);
	return (0);
bad:
	/* wu is unwound by sr_wu_put */
	if (wu_r)
		scsi_io_put(&sd->sd_iopool, wu_r);
	return (1);
}

/* Handle failure I/O completion */
int
sr_failio(struct sr_workunit *wu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct sr_ccb		*ccb;

	if (!(wu->swu_flags & SR_WUF_FAIL))
		return (0);

	/* Wu is a 'fake'.. don't do real I/O just intr */
	TAILQ_INSERT_TAIL(&sd->sd_wu_pendq, wu, swu_link);
	TAILQ_FOREACH(ccb, &wu->swu_ccb, ccb_link)
		sr_raid6_intr(&ccb->ccb_buf);
	return (1);
}

void
sr_raid6_intr(struct buf *bp)
{
	struct sr_ccb		*ccb = (struct sr_ccb *)bp;
	struct sr_workunit	*wu = ccb->ccb_wu, *wup;
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_softc		*sc = sd->sd_sc;
	struct sr_raid6_opaque  *pq = ccb->ccb_opaque;
	int			s;

	DNPRINTF(SR_D_INTR, "%s: sr_raid6_intr bp %p xs %p\n",
	    DEVNAME(sc), bp, xs);

	s = splbio();

	sr_ccb_done(ccb);

	/* XOR data to result. */
	if (ccb->ccb_state == SR_CCB_OK && pq) {
		if (pq->pbuf)
			/* Calculate xor-parity */
			sr_raid6_xorp(pq->pbuf, ccb->ccb_buf.b_data,
			    ccb->ccb_buf.b_bcount);
		if (pq->qbuf)
			/* Calculate q-parity */
			sr_raid6_xorq(pq->qbuf, ccb->ccb_buf.b_data,
			    ccb->ccb_buf.b_bcount, pq->gn);
		free(pq, M_DEVBUF);
		ccb->ccb_opaque = NULL;
	}

	/* Free allocated data buffer. */
	if (ccb->ccb_flag & SR_CCBF_FREEBUF) {
		sr_put_block(sd, ccb->ccb_buf.b_data, ccb->ccb_buf.b_bcount);
		ccb->ccb_buf.b_data = NULL;
	}

	DNPRINTF(SR_D_INTR, "%s: sr_intr: comp: %d count: %d failed: %d\n",
	    DEVNAME(sc), wu->swu_ios_complete, wu->swu_io_count,
	    wu->swu_ios_failed);

	if (wu->swu_ios_complete < wu->swu_io_count)
		goto done;

	if (xs != NULL)
		xs->error = XS_NOERROR;

	/* if all ios failed, retry reads and give up on writes */
	if (wu->swu_ios_failed == wu->swu_ios_complete) {
		/* XXX xs could be NULL. */
		if (xs->flags & SCSI_DATA_IN) {
			printf("%s: retrying read on block %lld\n",
			    DEVNAME(sc), ccb->ccb_buf.b_blkno);
			sr_ccb_put(ccb);
			TAILQ_INIT(&wu->swu_ccb);
			wu->swu_state = SR_WU_RESTART;
			if (sd->sd_scsi_rw(wu) == 0)
				goto done;
			xs->error = XS_DRIVER_STUFFUP;
		} else {
			printf("%s: permanently fail write on block %lld\n",
			    DEVNAME(sc), ccb->ccb_buf.b_blkno);
			xs->error = XS_DRIVER_STUFFUP;
		}
	}

	TAILQ_FOREACH(wup, &sd->sd_wu_pendq, swu_link)
		if (wu == wup)
			break;

	if (wup == NULL)
		panic("%s: wu %p not on pending queue",
		    DEVNAME(sd->sd_sc), wu);

	TAILQ_REMOVE(&sd->sd_wu_pendq, wu, swu_link);

	if (wu->swu_collider) {
		if (wu->swu_ios_failed)
			sr_raid_recreate_wu(wu->swu_collider);

		/* XXX Should the collider be failed if this xs failed? */
		/* restart deferred wu */
		wu->swu_collider->swu_state = SR_WU_INPROGRESS;
		TAILQ_REMOVE(&sd->sd_wu_defq, wu->swu_collider, swu_link);
		if (sr_failio(wu->swu_collider) == 0)
			sr_raid_startwu(wu->swu_collider);
	}

	if (wu->swu_flags & SR_WUF_REBUILD)
		wu->swu_flags |= SR_WUF_REBUILDIOCOMP;
	if (wu->swu_flags & SR_WUF_WAKEUP)
		wakeup(wu);
	if (!(wu->swu_flags & SR_WUF_REBUILD)) {
		if (xs == NULL) {
			scsi_io_put(&sd->sd_iopool, wu);
			if (sd->sd_sync && sd->sd_wu_pending == 0)
				wakeup(sd);
		} else {
			sr_scsi_done(sd, xs);
		}
	}

done:
	splx(s);
}

int
sr_raid6_addio(struct sr_workunit *wu, int dsk, daddr64_t blk, daddr64_t len,
    void *data, int flag, int ccbflag, void *pbuf, void *qbuf, int gn)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct sr_ccb		*ccb;
	struct sr_raid6_opaque  *pqbuf;

	ccb = sr_ccb_get(sd);
	if (!ccb)
		return (-1);

	/* allocate temporary buffer */
	if (data == NULL) {
		data = sr_get_block(sd, len);
		if (data == NULL)
			return (-1);
	}

	DNPRINTF(0, "%sio: %d.%llx %llx %p:%p\n",
	    flag & SCSI_DATA_IN ? "read" : "write",
	    dsk, blk, len, pbuf, qbuf);

	ccb->ccb_flag = ccbflag;
	if (flag & SCSI_POLL) {
		ccb->ccb_buf.b_flags = 0;
		ccb->ccb_buf.b_iodone = NULL;
	} else {
		ccb->ccb_buf.b_flags = B_CALL;
		ccb->ccb_buf.b_iodone = sr_raid6_intr;
	}
	if (flag & SCSI_DATA_IN)
		ccb->ccb_buf.b_flags |= B_READ;
	else
		ccb->ccb_buf.b_flags |= B_WRITE;

	/* add offset for metadata */
	ccb->ccb_buf.b_flags |= B_PHYS;
	ccb->ccb_buf.b_blkno = blk;
	ccb->ccb_buf.b_bcount = len;
	ccb->ccb_buf.b_bufsize = len;
	ccb->ccb_buf.b_resid = len;
	ccb->ccb_buf.b_data = data;
	ccb->ccb_buf.b_error = 0;
	ccb->ccb_buf.b_proc = curproc;
	ccb->ccb_buf.b_dev = sd->sd_vol.sv_chunks[dsk]->src_dev_mm;
	ccb->ccb_buf.b_vp = sd->sd_vol.sv_chunks[dsk]->src_vn;
	ccb->ccb_buf.b_bq = NULL;
	if ((ccb->ccb_buf.b_flags & B_READ) == 0)
		ccb->ccb_buf.b_vp->v_numoutput++;

	ccb->ccb_wu = wu;
	ccb->ccb_target = dsk;
	if (pbuf || qbuf) {
		if (qbuf && gf_premul(gn))
			return (-1);

		pqbuf = malloc(sizeof(struct sr_raid6_opaque), M_DEVBUF, M_ZERO | M_NOWAIT);
		if (pqbuf == NULL) {
			sr_ccb_put(ccb);
			return (-1);
		}
		pqbuf->pbuf = pbuf;
		pqbuf->qbuf = qbuf;
		pqbuf->gn = gn;
		ccb->ccb_opaque = pqbuf;
	}

	LIST_INIT(&ccb->ccb_buf.b_dep);
	TAILQ_INSERT_TAIL(&wu->swu_ccb, ccb, ccb_link);

	DNPRINTF(SR_D_DIS, "%s: %s: sr_raid6: b_bcount: %d "
	    "b_blkno: %x b_flags 0x%0x b_data %p\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    ccb->ccb_buf.b_bcount, ccb->ccb_buf.b_blkno,
	    ccb->ccb_buf.b_flags, ccb->ccb_buf.b_data);

	wu->swu_io_count++;

	return (0);
}

/* Perform RAID6 parity calculation.
 *   P=xor parity, Q=GF256 parity, D=data, gn=disk# */
void
sr_raid6_xorp(void *p, void *d, int len)
{
	uint32_t *pbuf = p, *data = d;

	len >>= 2;
	while (len--)
		*pbuf++ ^= *data++;
}

void
sr_raid6_xorq(void *q, void *d, int len, int gn)
{
	uint32_t 	*qbuf = q, *data = d, x;
	uint8_t	 	*gn_map = gf_map[gn];

	len >>= 2;
	while (len--) {
		x = *data++;
		*qbuf++ ^= (((uint32_t)gn_map[x & 0xff]) |
		  	    ((uint32_t)gn_map[(x >> 8) & 0xff] << 8) |
			    ((uint32_t)gn_map[(x >> 16) & 0xff] << 16) |
			    ((uint32_t)gn_map[(x >> 24) & 0xff] << 24));
	}
}

/* Create GF256 log/pow tables: polynomial = 0x11D */
void
gf_init(void)
{
	int i;
	uint8_t p = 1;

	/* use 2N pow table to avoid using % in multiply */
	for (i=0; i<256; i++) {
		gf_log[p] = i;
		gf_pow[i] = gf_pow[i+255] = p;
		p = ((p << 1) ^ ((p & 0x80) ? 0x1D : 0x00));
	}
	gf_log[0] = 512;
}

uint8_t
gf_inv(uint8_t a)
{
	return gf_pow[255 - gf_log[a]];
}

uint8_t
gf_mul(uint8_t a, uint8_t b)
{
	return gf_pow[gf_log[a] + gf_log[b]];
}

/* Precalculate multiplication tables for drive gn */
int
gf_premul(uint8_t gn)
{
	int i;

	if (gf_map[gn] != NULL)
		return (0);

	if ((gf_map[gn] = malloc(256, M_DEVBUF, M_ZERO | M_NOWAIT)) == NULL)
		return (-1);

	for (i=0; i<256; i++)
		gf_map[gn][i] = gf_pow[gf_log[i] + gf_log[gn]];
	return (0);
}
