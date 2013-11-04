/* $OpenBSD: softraid_raidp.c,v 1.52 2013/11/04 21:02:57 deraadt Exp $ */
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
#include <sys/pool.h>
#include <sys/conf.h>
#include <sys/uio.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_disk.h>

#include <dev/softraidvar.h>
#include <dev/rndvar.h>

/* RAID P functions. */
int	sr_raidp_create(struct sr_discipline *, struct bioc_createraid *,
	    int, int64_t);
int	sr_raidp_assemble(struct sr_discipline *, struct bioc_createraid *,
	    int, void *);
int	sr_raidp_init(struct sr_discipline *);
int	sr_raidp_rw(struct sr_workunit *);
int	sr_raidp_openings(struct sr_discipline *);
void	sr_raidp_intr(struct buf *);
int	sr_raidp_wu_done(struct sr_workunit *);
void	sr_raidp_set_chunk_state(struct sr_discipline *, int, int);
void	sr_raidp_set_vol_state(struct sr_discipline *);

void	sr_raidp_xor(void *, void *, int);
int	sr_raidp_addio(struct sr_workunit *wu, int, daddr_t, daddr_t,
	    void *, int, int, void *);
void	sr_dump(void *, int);
void	sr_raidp_scrub(struct sr_discipline *);

void	*sr_get_block(struct sr_discipline *, int);
void	sr_put_block(struct sr_discipline *, void *, int);

/* discipline initialisation. */
void
sr_raidp_discipline_init(struct sr_discipline *sd, u_int8_t type)
{
	/* Fill out discipline members. */
	sd->sd_type = type;
	if (sd->sd_type == SR_MD_RAID4)
		strlcpy(sd->sd_name, "RAID 4", sizeof(sd->sd_name));
	else
		strlcpy(sd->sd_name, "RAID 5", sizeof(sd->sd_name));
	sd->sd_capabilities = SR_CAP_SYSTEM_DISK | SR_CAP_AUTO_ASSEMBLE |
	    SR_CAP_REDUNDANT;
	sd->sd_max_ccb_per_wu = 4; /* only if stripsize <= MAXPHYS */
	sd->sd_max_wu = SR_RAIDP_NOWU;

	/* Setup discipline specific function pointers. */
	sd->sd_assemble = sr_raidp_assemble;
	sd->sd_create = sr_raidp_create;
	sd->sd_openings = sr_raidp_openings;
	sd->sd_scsi_rw = sr_raidp_rw;
	sd->sd_scsi_intr = sr_raidp_intr;
	sd->sd_scsi_wu_done = sr_raidp_wu_done;
	sd->sd_set_chunk_state = sr_raidp_set_chunk_state;
	sd->sd_set_vol_state = sr_raidp_set_vol_state;
}

int
sr_raidp_create(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, int64_t coerced_size)
{
	if (no_chunk < 3) {
		sr_error(sd->sd_sc, "%s requires three or more chunks",
		    sd->sd_name);
		return EINVAL;
	}

	/*
	 * XXX add variable strip size later even though MAXPHYS is really
	 * the clever value, users like to tinker with that type of stuff.
	 */
	sd->sd_meta->ssdi.ssd_strip_size = MAXPHYS;
	sd->sd_meta->ssdi.ssd_size = (coerced_size &
	    ~((sd->sd_meta->ssdi.ssd_strip_size >> DEV_BSHIFT) - 1)) *
	    (no_chunk - 1);

	return sr_raidp_init(sd);
}

int
sr_raidp_assemble(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, void *data)
{
	return sr_raidp_init(sd);
}

int
sr_raidp_init(struct sr_discipline *sd)
{
	/* Initialise runtime values. */
	sd->mds.mdd_raidp.srp_strip_bits =
	    sr_validate_stripsize(sd->sd_meta->ssdi.ssd_strip_size);
	if (sd->mds.mdd_raidp.srp_strip_bits == -1) {
		sr_error(sd->sd_sc, "invalid strip size");
		return EINVAL;
	}

	return 0;
}

int
sr_raidp_openings(struct sr_discipline *sd)
{
	return (sd->sd_max_wu >> 1); /* 2 wu's per IO */
}

void
sr_raidp_set_chunk_state(struct sr_discipline *sd, int c, int new_state)
{
	int			old_state, s;

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
sr_raidp_set_vol_state(struct sr_discipline *sd)
{
	int			states[SR_MAX_STATES];
	int			new_state, i, s, nd;
	int			old_state = sd->sd_vol_status;

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
	else if (states[BIOC_SDONLINE] < nd - 1)
		new_state = BIOC_SVOFFLINE;
	else if (states[BIOC_SDSCRUB] != 0)
		new_state = BIOC_SVSCRUB;
	else if (states[BIOC_SDREBUILD] != 0)
		new_state = BIOC_SVREBUILD;
	else if (states[BIOC_SDONLINE] == nd - 1)
		new_state = BIOC_SVDEGRADED;
	else {
#ifdef SR_DEBUG
		DNPRINTF(SR_D_STATE, "%s: invalid volume state, old state "
		    "was %d\n", DEVNAME(sd->sd_sc), old_state);
		for (i = 0; i < nd; i++)
			DNPRINTF(SR_D_STATE, "%s: chunk %d status = %d\n",
			    DEVNAME(sd->sd_sc), i,
			    sd->sd_vol.sv_chunks[i]->src_meta.scm_status);
#endif
		panic("invalid volume state");
	}

	DNPRINTF(SR_D_STATE, "%s: %s: sr_raidp_set_vol_state %d -> %d\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    old_state, new_state);

	switch (old_state) {
	case BIOC_SVONLINE:
		switch (new_state) {
		case BIOC_SVONLINE: /* can go to same state */
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

int
sr_raidp_rw(struct sr_workunit *wu)
{
	struct sr_workunit	*wu_r = NULL;
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_chunk		*scp;
	int			s, i;
	daddr_t			blk, lbaoffs, strip_no, chunk, row_size;
	daddr_t			strip_size, no_chunk, lba, chunk_offs, phys_offs;
	daddr_t			strip_bits, length, parity, strip_offs, datalen;
	void			*xorbuf, *data;

	/* blk and scsi error will be handled by sr_validate_io */
	if (sr_validate_io(wu, &blk, "sr_raidp_rw"))
		goto bad;

	strip_size = sd->sd_meta->ssdi.ssd_strip_size;
	strip_bits = sd->mds.mdd_raidp.srp_strip_bits;
	no_chunk = sd->sd_meta->ssdi.ssd_chunk_no - 1;
	row_size = (no_chunk << strip_bits) >> DEV_BSHIFT;

	data = xs->data;
	datalen = xs->datalen;
	lbaoffs	= blk << DEV_BSHIFT;

	if (xs->flags & SCSI_DATA_OUT) {
		if ((wu_r = sr_scsi_wu_get(sd, SCSI_NOSLEEP)) == NULL){
			printf("%s: can't get wu_r", DEVNAME(sd->sd_sc));
			goto bad;
		}
		wu_r->swu_state = SR_WU_INPROGRESS;
		wu_r->swu_flags |= SR_WUF_DISCIPLINE;
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
		if (sd->sd_type == SR_MD_RAID4)
			parity = no_chunk; /* RAID4: Parity is always drive N */
		else {
			/* RAID5: left asymmetric algorithm */
			parity = no_chunk - ((strip_no / no_chunk) %
			    (no_chunk + 1));
			if (chunk >= parity)
				chunk++;
		}

		lba = phys_offs >> DEV_BSHIFT;

		/* XXX big hammer.. exclude I/O from entire stripe */
		if (wu->swu_blk_start == 0)
			wu->swu_blk_start = (strip_no / no_chunk) * row_size;
		wu->swu_blk_end = (strip_no / no_chunk) * row_size + (row_size - 1);

		scp = sd->sd_vol.sv_chunks[chunk];
		if (xs->flags & SCSI_DATA_IN) {
			switch (scp->src_meta.scm_status) {
			case BIOC_SDONLINE:
			case BIOC_SDSCRUB:
				/* drive is good. issue single read request */
				if (sr_raidp_addio(wu, chunk, lba, length,
				    data, xs->flags, 0, NULL))
					goto bad;
				break;
			case BIOC_SDOFFLINE:
			case BIOC_SDREBUILD:
			case BIOC_SDHOTSPARE:
				/*
				 * XXX only works if this LBA has already
				 * been scrubbed
				 */
				printf("Disk %llx offline, "
				    "regenerating buffer\n", chunk);
				memset(data, 0, length);
				for (i = 0; i <= no_chunk; i++) {
					/*
					 * read all other drives: xor result
					 * into databuffer.
					 */
					if (i != chunk) {
						if (sr_raidp_addio(wu, i, lba,
						    length, NULL, SCSI_DATA_IN,
						    0, data))
							goto bad;
					}
				}
				break;
			default:
				printf("%s: is offline, can't read\n",
				    DEVNAME(sd->sd_sc));
				goto bad;
			}
		} else {
			/* XXX handle writes to failed/offline disk? */
			if (scp->src_meta.scm_status == BIOC_SDOFFLINE)
				goto bad;

			/*
			 * initialize XORBUF with contents of new data to be
			 * written. This will be XORed with old data and old
			 * parity in the intr routine. The result in xorbuf
			 * is the new parity data.
			 */
			xorbuf = sr_get_block(sd, length);
			if (xorbuf == NULL)
				goto bad;
			memcpy(xorbuf, data, length);

			/* xor old data */
			if (sr_raidp_addio(wu_r, chunk, lba, length, NULL,
			    SCSI_DATA_IN, 0, xorbuf))
				goto bad;

			/* xor old parity */
			if (sr_raidp_addio(wu_r, parity, lba, length, NULL,
			    SCSI_DATA_IN, 0, xorbuf))
				goto bad;

			/* write new data */
			if (sr_raidp_addio(wu, chunk, lba, length, data,
			    xs->flags, 0, NULL))
				goto bad;

			/* write new parity */
			if (sr_raidp_addio(wu, parity, lba, length, xorbuf,
			    xs->flags, SR_CCBF_FREEBUF, NULL))
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
	splx(s);

	sr_schedule_wu(wu);

	return (0);

bad:
	/* XXX - can leak xorbuf on error. */
	/* wu is unwound by sr_wu_put */
	if (wu_r)
		sr_scsi_wu_put(sd, wu_r);
	return (1);
}

void
sr_raidp_intr(struct buf *bp)
{
	struct sr_ccb		*ccb = (struct sr_ccb *)bp;
	struct sr_workunit	*wu = ccb->ccb_wu;
	struct sr_discipline	*sd = wu->swu_dis;
	int			s;

	DNPRINTF(SR_D_INTR, "%s: sr_raidp_intr bp %p xs %p\n",
	    DEVNAME(sd->sd_sc), bp, wu->swu_xs);

	s = splbio();
	sr_ccb_done(ccb);

	/* XXX - Should this be done via the workq? */

	/* XOR data to result. */
	if (ccb->ccb_state == SR_CCB_OK && ccb->ccb_opaque)
		sr_raidp_xor(ccb->ccb_opaque, ccb->ccb_buf.b_data,
		    ccb->ccb_buf.b_bcount);

	/* Free allocated data buffer. */
	if (ccb->ccb_flags & SR_CCBF_FREEBUF) {
		sr_put_block(sd, ccb->ccb_buf.b_data, ccb->ccb_buf.b_bcount);
		ccb->ccb_buf.b_data = NULL;
	}

	sr_wu_done(wu);
	splx(s);
}

int
sr_raidp_wu_done(struct sr_workunit *wu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;

	/* XXX - we have no way of propagating errors... */
	if (wu->swu_flags & SR_WUF_DISCIPLINE)
		return SR_WU_OK;

	/* XXX - This is insufficient for RAID 4/5. */
	if (wu->swu_ios_succeeded > 0) {
		xs->error = XS_NOERROR;
		return SR_WU_OK;
	}

	if (xs->flags & SCSI_DATA_IN) {
		printf("%s: retrying read on block %lld\n",
		    sd->sd_meta->ssd_devname, (long long)wu->swu_blk_start);
		sr_wu_release_ccbs(wu);
		wu->swu_state = SR_WU_RESTART;
		if (sd->sd_scsi_rw(wu) == 0)
			return SR_WU_RESTART;
	} else {
		printf("%s: permanently fail write on block %lld\n",
		    sd->sd_meta->ssd_devname, (long long)wu->swu_blk_start);
	}

	wu->swu_state = SR_WU_FAILED;
	xs->error = XS_DRIVER_STUFFUP;

	return SR_WU_FAILED;
}

int
sr_raidp_addio(struct sr_workunit *wu, int chunk, daddr_t blkno,
    daddr_t len, void *data, int xsflags, int ccbflags, void *xorbuf)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct sr_ccb		*ccb;

	DNPRINTF(SR_D_DIS, "sr_raidp_addio: %s %d.%llx %llx %s\n",
	    (xsflags & SCSI_DATA_IN) ? "read" : "write", chunk, blkno, len,
	    xorbuf ? "X0R" : "-");

	/* Allocate temporary buffer. */
	if (data == NULL) {
		data = sr_get_block(sd, len);
		if (data == NULL)
			return (-1);
		ccbflags |= SR_CCBF_FREEBUF;
	}

	ccb = sr_ccb_rw(sd, chunk, blkno, len, data, xsflags, ccbflags);
	if (ccb == NULL) {
		if (ccbflags & SR_CCBF_FREEBUF)
			sr_put_block(sd, data, len);
		return (-1);
	}
	ccb->ccb_opaque = xorbuf;
	sr_wu_enqueue_ccb(wu, ccb);

	return (0);
}

void
sr_dump(void *blk, int len)
{
	uint8_t			*b = blk;
	int			i, j, c;

	for (i = 0; i < len; i += 16) {
		for (j = 0; j < 16; j++)
			printf("%.2x ", b[i + j]);
		printf("  ");
		for (j = 0; j < 16; j++) {
			c = b[i + j];
			if (c < ' ' || c > 'z' || i + j > len)
				c = '.';
			printf("%c", c);
		}
		printf("\n");
	}
}

void
sr_raidp_xor(void *a, void *b, int len)
{
	uint32_t		*xa = a, *xb = b;

	len >>= 2;
	while (len--)
		*xa++ ^= *xb++;
}

#if 0
void
sr_raidp_scrub(struct sr_discipline *sd)
{
	daddr_t strip_no, strip_size, no_chunk, parity, max_strip, strip_bits;
	daddr_t i;
	struct sr_workunit *wu_r, *wu_w;
	int s, slept;
	void *xorbuf;

	wu_w = sr_scsi_wu_get(sd, 0);
	wu_r = sr_scsi_wu_get(sd, 0);

	no_chunk = sd->sd_meta->ssdi.ssd_chunk_no - 1;
	strip_size = sd->sd_meta->ssdi.ssd_strip_size;
	strip_bits = sd->mds.mdd_raidp.srp_strip_bits;
	max_strip = sd->sd_meta->ssdi.ssd_size >> strip_bits;

	for (strip_no = 0; strip_no < max_strip; strip_no++) {
		if (sd->sd_type == SR_MD_RAID4)
			parity = no_chunk;
		else
			parity = no_chunk - ((strip_no / no_chunk) %
			    (no_chunk + 1));

		xorbuf = sr_get_block(sd, strip_size);
		for (i = 0; i <= no_chunk; i++) {
			if (i != parity)
				sr_raidp_addio(wu_r, i, 0xBADCAFE, strip_size,
				    NULL, SCSI_DATA_IN, 0, xorbuf);
		}
		sr_raidp_addio(wu_w, parity, 0xBADCAFE, strip_size, xorbuf,
		    SCSI_DATA_OUT, SR_CCBF_FREEBUF, NULL);

		wu_r->swu_flags |= SR_WUF_REBUILD;

		/* Collide wu_w with wu_r */
		wu_w->swu_state = SR_WU_DEFERRED;
		wu_w->swu_flags |= SR_WUF_REBUILD | SR_WUF_WAKEUP;
		wu_r->swu_collider = wu_w;

		s = splbio();
		TAILQ_INSERT_TAIL(&sd->sd_wu_defq, wu_w, swu_link);
		splx(s);

		wu_r->swu_state = SR_WU_INPROGRESS;
		sr_schedule_wu(wu_r);

		slept = 0;
		while ((wu_w->swu_flags & SR_WUF_REBUILDIOCOMP) == 0) {
			tsleep(wu_w, PRIBIO, "sr_scrub", 0);
			slept = 1;
		}
		if (!slept)
			tsleep(sd->sd_sc, PWAIT, "sr_yield", 1);
	}
done:
	return;
}
#endif

void *
sr_get_block(struct sr_discipline *sd, int length)
{
	return dma_alloc(length, PR_NOWAIT | PR_ZERO);
}

void
sr_put_block(struct sr_discipline *sd, void *ptr, int length)
{
	dma_free(ptr, length);
}

