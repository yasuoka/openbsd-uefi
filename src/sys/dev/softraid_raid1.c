/* $OpenBSD: softraid_raid1.c,v 1.6 2008/07/19 22:41:58 marco Exp $ */
/*
 * Copyright (c) 2007 Marco Peereboom <marco@peereboom.us>
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

/* RAID 1 functions */
int
sr_raid1_alloc_resources(struct sr_discipline *sd)
{
	int			rv = EINVAL;

	if (!sd)
		return (rv);

	DNPRINTF(SR_D_DIS, "%s: sr_raid1_alloc_resources\n",
	    DEVNAME(sd->sd_sc));

	if (sr_wu_alloc(sd))
		goto bad;
	if (sr_ccb_alloc(sd))
		goto bad;

	rv = 0;
bad:
	return (rv);
}

int
sr_raid1_free_resources(struct sr_discipline *sd)
{
	int			rv = EINVAL;

	if (!sd)
		return (rv);

	DNPRINTF(SR_D_DIS, "%s: sr_raid1_free_resources\n",
	    DEVNAME(sd->sd_sc));

	sr_wu_free(sd);
	sr_ccb_free(sd);

	rv = 0;
	return (rv);
}

void
sr_raid1_set_chunk_state(struct sr_discipline *sd, int c, int new_state)
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
			break;
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
		if (new_state == BIOC_SDONLINE) {
			;
		} else
			goto die;
		break;

	case BIOC_SDREBUILD:
		if (new_state == BIOC_SDONLINE) {
			;
		} else
			goto die;
		break;

	case BIOC_SDHOTSPARE:
		if (new_state == BIOC_SDREBUILD) {
			;
		} else
			goto die;
		break;

	default:
die:
		splx(s); /* XXX */
		panic("%s: %s: %s: invalid chunk state transition "
		    "%d -> %d\n", DEVNAME(sd->sd_sc),
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
sr_raid1_set_vol_state(struct sr_discipline *sd)
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
		if (s > SR_MAX_STATES)
			panic("%s: %s: %s: invalid chunk state",
			    DEVNAME(sd->sd_sc),
			    sd->sd_meta->ssd_devname,
			    sd->sd_vol.sv_chunks[i]->src_meta.scmi.scm_devname);
		states[s]++;
	}

	if (states[BIOC_SDONLINE] == nd)
		new_state = BIOC_SVONLINE;
	else if (states[BIOC_SDONLINE] == 0)
		new_state = BIOC_SVOFFLINE;
	else if (states[BIOC_SDSCRUB] != 0)
		new_state = BIOC_SVSCRUB;
	else if (states[BIOC_SDREBUILD] != 0)
		new_state = BIOC_SVREBUILD;
	else if (states[BIOC_SDOFFLINE] != 0)
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
		case BIOC_SVOFFLINE:
		case BIOC_SVDEGRADED:
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
		panic("%s: %s: invalid volume state transition "
		    "%d -> %d\n", DEVNAME(sd->sd_sc),
		    sd->sd_meta->ssd_devname,
		    old_state, new_state);
		/* NOTREACHED */
	}

	sd->sd_vol_status = new_state;
}

int
sr_raid1_rw(struct sr_workunit *wu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_ccb		*ccb;
	struct sr_chunk		*scp;
	int			ios, x, i, s, rt;
	daddr64_t		blk;

	/* blk and scsi error will be handled by sr_validate_io */
	if (sr_validate_io(wu, &blk, "sr_raid1_rw"))
		goto bad;

	/* calculate physical block */
	blk += SR_META_SIZE + SR_META_OFFSET;

	if (xs->flags & SCSI_DATA_IN)
		ios = 1;
	else
		ios = sd->sd_meta->ssdi.ssd_chunk_no;
	wu->swu_io_count = ios;

	for (i = 0; i < ios; i++) {
		ccb = sr_ccb_get(sd);
		if (!ccb) {
			/* should never happen but handle more gracefully */
			printf("%s: %s: too many ccbs queued\n",
			    DEVNAME(sd->sd_sc),
			    sd->sd_meta->ssd_devname);
			goto bad;
		}

		if (xs->flags & SCSI_POLL) {
			ccb->ccb_buf.b_flags = 0;
			ccb->ccb_buf.b_iodone = NULL;
		} else {
			ccb->ccb_buf.b_flags = B_CALL;
			ccb->ccb_buf.b_iodone = sr_raid1_intr;
		}

		ccb->ccb_buf.b_blkno = blk;
		ccb->ccb_buf.b_bcount = xs->datalen;
		ccb->ccb_buf.b_bufsize = xs->datalen;
		ccb->ccb_buf.b_resid = xs->datalen;
		ccb->ccb_buf.b_data = xs->data;
		ccb->ccb_buf.b_error = 0;
		ccb->ccb_buf.b_proc = curproc;
		ccb->ccb_wu = wu;

		if (xs->flags & SCSI_DATA_IN) {
			rt = 0;
ragain:
			/* interleave reads */
			x = sd->mds.mdd_raid1.sr1_counter++ %
			    sd->sd_meta->ssdi.ssd_chunk_no;
			scp = sd->sd_vol.sv_chunks[x];
			switch (scp->src_meta.scm_status) {
			case BIOC_SDONLINE:
			case BIOC_SDSCRUB:
				ccb->ccb_buf.b_flags |= B_READ;
				break;

			case BIOC_SDOFFLINE:
			case BIOC_SDREBUILD:
			case BIOC_SDHOTSPARE:
				if (rt++ < sd->sd_meta->ssdi.ssd_chunk_no)
					goto ragain;

				/* FALLTHROUGH */
			default:
				/* volume offline */
				printf("%s: is offline, can't read\n",
				    DEVNAME(sd->sd_sc));
				sr_ccb_put(ccb);
				goto bad;
			}
		} else {
			/* writes go on all working disks */
			x = i;
			scp = sd->sd_vol.sv_chunks[x];
			switch (scp->src_meta.scm_status) {
			case BIOC_SDONLINE:
			case BIOC_SDSCRUB:
			case BIOC_SDREBUILD:
				ccb->ccb_buf.b_flags |= B_WRITE;
				break;

			case BIOC_SDHOTSPARE: /* should never happen */
			case BIOC_SDOFFLINE:
				wu->swu_io_count--;
				sr_ccb_put(ccb);
				continue;

			default:
				goto bad;
			}

		}
		ccb->ccb_target = x;
		ccb->ccb_buf.b_dev = sd->sd_vol.sv_chunks[x]->src_dev_mm;
		ccb->ccb_buf.b_vp = NULL;

		LIST_INIT(&ccb->ccb_buf.b_dep);

		TAILQ_INSERT_TAIL(&wu->swu_ccb, ccb, ccb_link);

		DNPRINTF(SR_D_DIS, "%s: %s: sr_raid1: b_bcount: %d "
		    "b_blkno: %x b_flags 0x%0x b_data %p\n",
		    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
		    ccb->ccb_buf.b_bcount, ccb->ccb_buf.b_blkno,
		    ccb->ccb_buf.b_flags, ccb->ccb_buf.b_data);
	}

	s = splbio();

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
	return (1);
}

void
sr_raid1_intr(struct buf *bp)
{
	struct sr_ccb		*ccb = (struct sr_ccb *)bp;
	struct sr_workunit	*wu = ccb->ccb_wu, *wup;
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_softc		*sc = sd->sd_sc;
	int			s, pend;

	DNPRINTF(SR_D_INTR, "%s: sr_intr bp %x xs %x\n",
	    DEVNAME(sc), bp, xs);

	DNPRINTF(SR_D_INTR, "%s: sr_intr: b_bcount: %d b_resid: %d"
	    " b_flags: 0x%0x block: %lld target: %d\n", DEVNAME(sc),
	    ccb->ccb_buf.b_bcount, ccb->ccb_buf.b_resid, ccb->ccb_buf.b_flags,
	    ccb->ccb_buf.b_blkno, ccb->ccb_target);

	s = splbio();

	if (ccb->ccb_buf.b_flags & B_ERROR) {
		DNPRINTF(SR_D_INTR, "%s: i/o error on block %lld target: %d\n",
		    DEVNAME(sc), ccb->ccb_buf.b_blkno, ccb->ccb_target);
		wu->swu_ios_failed++;
		ccb->ccb_state = SR_CCB_FAILED;
		if (ccb->ccb_target != -1)
			sd->sd_set_chunk_state(sd, ccb->ccb_target,
			    BIOC_SDOFFLINE);
		else
			panic("%s: invalid target on wu: %p", DEVNAME(sc), wu);
	} else {
		ccb->ccb_state = SR_CCB_OK;
		wu->swu_ios_succeeded++;
	}
	wu->swu_ios_complete++;

	DNPRINTF(SR_D_INTR, "%s: sr_intr: comp: %d count: %d failed: %d\n",
	    DEVNAME(sc), wu->swu_ios_complete, wu->swu_io_count,
	    wu->swu_ios_failed);

	if (wu->swu_ios_complete >= wu->swu_io_count) {
		/* if all ios failed, retry reads and give up on writes */
		if (wu->swu_ios_failed == wu->swu_ios_complete) {
			if (xs->flags & SCSI_DATA_IN) {
				printf("%s: retrying read on block %lld\n",
				    DEVNAME(sc), ccb->ccb_buf.b_blkno);
				sr_ccb_put(ccb);
				TAILQ_INIT(&wu->swu_ccb);
				wu->swu_state = SR_WU_RESTART;
				if (sd->sd_scsi_rw(wu))
					goto bad;
				else
					goto retry;
			} else {
				printf("%s: permanently fail write on block "
				    "%lld\n", DEVNAME(sc),
				    ccb->ccb_buf.b_blkno);
				xs->error = XS_DRIVER_STUFFUP;
				goto bad;
			}
		}

		xs->error = XS_NOERROR;
		xs->resid = 0;
		xs->flags |= ITSDONE;

		pend = 0;
		TAILQ_FOREACH(wup, &sd->sd_wu_pendq, swu_link) {
			if (wu == wup) {
				/* wu on pendq, remove */
				TAILQ_REMOVE(&sd->sd_wu_pendq, wu, swu_link);
				pend = 1;

				if (wu->swu_collider) {
					if (wu->swu_ios_failed)
						/* toss all ccbs and recreate */
						sr_raid1_recreate_wu(wu->swu_collider);

					/* restart deferred wu */
					wu->swu_collider->swu_state =
					    SR_WU_INPROGRESS;
					TAILQ_REMOVE(&sd->sd_wu_defq,
					    wu->swu_collider, swu_link);
					sr_raid_startwu(wu->swu_collider);
				}
				break;
			}
		}

		if (!pend)
			printf("%s: wu: %p not on pending queue\n",
			    DEVNAME(sc), wu);

		/* do not change the order of these 2 functions */
		sr_wu_put(wu);
		scsi_done(xs);

		if (sd->sd_sync && sd->sd_wu_pending == 0)
			wakeup(sd);
	}

retry:
	splx(s);
	return;
bad:
	xs->error = XS_DRIVER_STUFFUP;
	xs->flags |= ITSDONE;
	sr_wu_put(wu);
	scsi_done(xs);
	splx(s);
}

void
sr_raid1_recreate_wu(struct sr_workunit *wu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct sr_workunit	*wup = wu;
	struct sr_ccb		*ccb;

	do {
		DNPRINTF(SR_D_INTR, "%s: sr_raid1_recreate_wu: %p\n", wup);

		/* toss all ccbs */
		while ((ccb = TAILQ_FIRST(&wup->swu_ccb)) != NULL) {
			TAILQ_REMOVE(&wup->swu_ccb, ccb, ccb_link);
			sr_ccb_put(ccb);
		}
		TAILQ_INIT(&wup->swu_ccb);

		/* recreate ccbs */
		wup->swu_state = SR_WU_REQUEUE;
		if (sd->sd_scsi_rw(wup))
			panic("could not requeue io");

		wup = wup->swu_collider;
	} while (wup);
}
