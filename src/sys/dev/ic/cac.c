/*	$OpenBSD: cac.c,v 1.14 2003/04/27 11:22:52 ho Exp $	*/
/*	$NetBSD: cac.c,v 1.15 2000/11/08 19:20:35 ad Exp $	*/

/*
 * Copyright (c) 2001 Michael Shalayeff
 * All rights reserved.
 *
 * The SCSI emulation layer is derived from gdt(4) driver,
 * Copyright (c) 1999, 2000 Niklas Hallqvist. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for Compaq array controllers.
 */

/* #define	CAC_DEBUG */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/pool.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/cacreg.h>
#include <dev/ic/cacvar.h>

struct cfdriver cac_cd = {
	NULL, "cac", DV_DULL
};

int     cac_scsi_cmd(struct scsi_xfer *);
void	cacminphys(struct buf *bp);

struct scsi_adapter cac_switch = {
	cac_scsi_cmd, cacminphys, 0, 0,
};

struct scsi_device cac_dev = {
	NULL, NULL, NULL, NULL
};

struct	cac_ccb *cac_ccb_alloc(struct cac_softc *, int);
void	cac_ccb_done(struct cac_softc *, struct cac_ccb *);
void	cac_ccb_free(struct cac_softc *, struct cac_ccb *);
int	cac_ccb_poll(struct cac_softc *, struct cac_ccb *, int);
int	cac_ccb_start(struct cac_softc *, struct cac_ccb *);
int	cac_cmd(struct cac_softc *sc, int command, void *data, int datasize,
	int drive, int blkno, int flags, struct scsi_xfer *xs);
int	cac_get_dinfo(struct cac_softc *sc, int target);
int	cac_flush(struct cac_softc *sc);
void	cac_shutdown(void *);
void	cac_copy_internal_data(struct scsi_xfer *xs, void *v, size_t size);

struct	cac_ccb *cac_l0_completed(struct cac_softc *);
int	cac_l0_fifo_full(struct cac_softc *);
void	cac_l0_intr_enable(struct cac_softc *, int);
int	cac_l0_intr_pending(struct cac_softc *);
void	cac_l0_submit(struct cac_softc *, struct cac_ccb *);

void	*cac_sdh;	/* shutdown hook */

const
struct cac_linkage cac_l0 = {
	cac_l0_completed,
	cac_l0_fifo_full,
	cac_l0_intr_enable,
	cac_l0_intr_pending,
	cac_l0_submit
};

/*
 * Initialise our interface to the controller.
 */
int
cac_init(struct cac_softc *sc, int startfw)
{
	struct cac_controller_info cinfo;
	int error, rseg, size, i;
	bus_dma_segment_t seg[1];
	struct cac_ccb *ccb;

	SIMPLEQ_INIT(&sc->sc_ccb_free);
	SIMPLEQ_INIT(&sc->sc_ccb_queue);

        size = sizeof(struct cac_ccb) * CAC_MAX_CCBS;

	if ((error = bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, seg, 1,
	    &rseg, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to allocate CCBs, error = %d\n",
		    sc->sc_dv.dv_xname, error);
		return (-1);
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, seg, rseg, size,
	    &sc->sc_ccbs, BUS_DMA_NOWAIT | BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map CCBs, error = %d\n",
		    sc->sc_dv.dv_xname, error);
		return (-1);
	}

	if ((error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &sc->sc_dmamap)) != 0) {
		printf("%s: unable to create CCB DMA map, error = %d\n",
		    sc->sc_dv.dv_xname, error);
		return (-1);
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_dmamap, sc->sc_ccbs,
	    size, NULL, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to load CCB DMA map, error = %d\n",
		    sc->sc_dv.dv_xname, error);
		return (-1);
	}

	sc->sc_ccbs_paddr = sc->sc_dmamap->dm_segs[0].ds_addr;
	memset(sc->sc_ccbs, 0, size);
	ccb = (struct cac_ccb *)sc->sc_ccbs;

	for (i = 0; i < CAC_MAX_CCBS; i++, ccb++) {
		/* Create the DMA map for this CCB's data */
		error = bus_dmamap_create(sc->sc_dmat, CAC_MAX_XFER,
		    CAC_SG_SIZE, CAC_MAX_XFER, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &ccb->ccb_dmamap_xfer);

		if (error) {
			printf("%s: can't create ccb dmamap (%d)\n",
			    sc->sc_dv.dv_xname, error);
			break;
		}

		ccb->ccb_paddr = sc->sc_ccbs_paddr + i * sizeof(struct cac_ccb);
		SIMPLEQ_INSERT_TAIL(&sc->sc_ccb_free, ccb, ccb_chain);
	}

	/* Start firmware background tasks, if needed. */
	if (startfw) {
		if (cac_cmd(sc, CAC_CMD_START_FIRMWARE, &cinfo, sizeof(cinfo),
		    0, 0, CAC_CCB_DATA_IN, NULL)) {
			printf("%s: CAC_CMD_START_FIRMWARE failed\n",
			    sc->sc_dv.dv_xname);
			return (-1);
		}
	}

	if (cac_cmd(sc, CAC_CMD_GET_CTRL_INFO, &cinfo, sizeof(cinfo), 0, 0,
	    CAC_CCB_DATA_IN, NULL)) {
		printf("%s: CAC_CMD_GET_CTRL_INFO failed\n",
		    sc->sc_dv.dv_xname);
		return (-1);
	}

	if (!cinfo.num_drvs) {
		printf("%s: no volumes defined\n", sc->sc_dv.dv_xname);
		return (-1);
	}

	sc->sc_nunits = cinfo.num_drvs;
	sc->sc_dinfos = malloc(cinfo.num_drvs * sizeof(struct cac_drive_info),
	    M_DEVBUF, M_NOWAIT);
	if (sc->sc_dinfos == NULL) {
		printf("%s: cannot allocate memory for drive_info\n",
		    sc->sc_dv.dv_xname);
		return (-1);
	}
	bzero(sc->sc_dinfos, cinfo.num_drvs * sizeof(struct cac_drive_info));

	sc->sc_link.adapter_softc = sc;
	sc->sc_link.adapter = &cac_switch;
	sc->sc_link.adapter_target = cinfo.num_drvs;
	sc->sc_link.adapter_buswidth = cinfo.num_drvs;
	sc->sc_link.device = &cac_dev;
	sc->sc_link.openings = CAC_MAX_CCBS / sc->sc_nunits;
	if (sc->sc_link.openings < 4 )
		sc->sc_link.openings = 4;

	config_found(&sc->sc_dv, &sc->sc_link, scsiprint);

	/* Set our `shutdownhook' before we start any device activity. */
	if (cac_sdh == NULL)
		cac_sdh = shutdownhook_establish(cac_shutdown, NULL);

	return (0);
}

int
cac_flush(sc)
	struct cac_softc *sc;
{
	u_int8_t buf[512];

	memset(buf, 0, sizeof(buf));
	buf[0] = 1;
	return cac_cmd(sc, CAC_CMD_FLUSH_CACHE, buf, sizeof(buf), 0, 0,
	    CAC_CCB_DATA_OUT, NULL);
}

/*
 * Shut down all `cac' controllers.
 */
void
cac_shutdown(void *cookie)
{
	extern struct cfdriver cac_cd;
	struct cac_softc *sc;
	int i;

	for (i = 0; i < cac_cd.cd_ndevs; i++) {
		if ((sc = (struct cac_softc *)device_lookup(&cac_cd, i)) == NULL)
			continue;
		cac_flush(sc);
	}
}

/*
 * Handle an interrupt from the controller: process finished CCBs and
 * dequeue any waiting CCBs.
 */
int
cac_intr(v)
	void *v;
{
	struct cac_softc *sc = v;
	struct cac_ccb *ccb;
	int istat, ret = 0;

	if (!(istat = (sc->sc_cl->cl_intr_pending)(sc)))
		return 0;

	(*sc->sc_cl->cl_intr_enable)(sc, 0);
	if (istat & CAC_INTR_FIFO_NEMPTY)
		while ((ccb = (*sc->sc_cl->cl_completed)(sc)) != NULL) {
			ret = 1;
			cac_ccb_done(sc, ccb);
		}
	cac_ccb_start(sc, NULL);

	return (ret);
}

/*
 * Execute a [polled] command.
 */
int
cac_cmd(struct cac_softc *sc, int command, void *data, int datasize,
	int drive, int blkno, int flags, struct scsi_xfer *xs)
{
	struct cac_ccb *ccb;
	struct cac_sgb *sgb;
	int i, rv, size, nsegs;

#ifdef CAC_DEBUG
	printf("cac_cmd op=%x drv=%d blk=%d data=%p[%x] fl=%x xs=%p ",
	    command, drive, blkno, data, datasize, flags, xs);
#endif

	if ((ccb = cac_ccb_alloc(sc, 0)) == NULL) {
		printf("%s: unable to alloc CCB\n", sc->sc_dv.dv_xname);
		return (ENOMEM);
	}

	if ((flags & (CAC_CCB_DATA_IN | CAC_CCB_DATA_OUT)) != 0) {
		bus_dmamap_load(sc->sc_dmat, ccb->ccb_dmamap_xfer,
		    (void *)data, datasize, NULL, BUS_DMA_NOWAIT);

		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap_xfer, 0,
		    ccb->ccb_dmamap_xfer->dm_mapsize,
		    (flags & CAC_CCB_DATA_IN) != 0 ? BUS_DMASYNC_PREREAD :
		    BUS_DMASYNC_PREWRITE);

		sgb = ccb->ccb_seg;
		nsegs = ccb->ccb_dmamap_xfer->dm_nsegs;
		if (nsegs > CAC_SG_SIZE)
			panic("cac_cmd: nsegs botch");

		size = 0;
		for (i = 0; i < nsegs; i++, sgb++) {
			size += ccb->ccb_dmamap_xfer->dm_segs[i].ds_len;
			sgb->length =
			    htole32(ccb->ccb_dmamap_xfer->dm_segs[i].ds_len);
			sgb->addr =
			    htole32(ccb->ccb_dmamap_xfer->dm_segs[i].ds_addr);
		}
	} else {
		size = datasize;
		nsegs = 0;
	}

	ccb->ccb_hdr.drive = drive;
	ccb->ccb_hdr.priority = 0;
	ccb->ccb_hdr.size = htole16((sizeof(struct cac_req) +
	    sizeof(struct cac_sgb) * CAC_SG_SIZE) >> 2);

	ccb->ccb_req.next = 0;
	ccb->ccb_req.command = command;
	ccb->ccb_req.error = 0;
	ccb->ccb_req.blkno = htole32(blkno);
	ccb->ccb_req.bcount = htole16(howmany(size, DEV_BSIZE));
	ccb->ccb_req.sgcount = nsegs;
	ccb->ccb_req.reserved = 0;

	ccb->ccb_flags = flags;
	ccb->ccb_datasize = size;
	ccb->ccb_xs = xs;

	(*sc->sc_cl->cl_intr_enable)(sc, 0);
	if (!xs || xs->flags & SCSI_POLL) {

		/* Synchronous commands musn't wait. */
		if ((*sc->sc_cl->cl_fifo_full)(sc)) {
			cac_ccb_free(sc, ccb);
			rv = -1;
		} else {
			ccb->ccb_flags |= CAC_CCB_ACTIVE;
			(*sc->sc_cl->cl_submit)(sc, ccb);
			rv = cac_ccb_poll(sc, ccb, 2000);
		}
		(*sc->sc_cl->cl_intr_enable)(sc, 1);
	} else
		rv = cac_ccb_start(sc, ccb);

	return (rv);
}

/*
 * Wait for the specified CCB to complete.  Must be called at splbio.
 */
int
cac_ccb_poll(struct cac_softc *sc, struct cac_ccb *wantccb, int timo)
{
	struct cac_ccb *ccb;
	int t = timo * 10;

	do {
		for (; t--; DELAY(100))
			if ((ccb = (*sc->sc_cl->cl_completed)(sc)) != NULL)
				break;
		if (t < 0) {
			printf("%s: timeout\n", sc->sc_dv.dv_xname);
			return (EBUSY);
		}
		cac_ccb_done(sc, ccb);
	} while (ccb != wantccb);

	return (0);
}

/*
 * Enqueue the specified command (if any) and attempt to start all enqueued
 * commands.  Must be called at splbio.
 */
int
cac_ccb_start(struct cac_softc *sc, struct cac_ccb *ccb)
{
	if (ccb != NULL)
		SIMPLEQ_INSERT_TAIL(&sc->sc_ccb_queue, ccb, ccb_chain);

	while ((ccb = SIMPLEQ_FIRST(&sc->sc_ccb_queue)) != NULL &&
	    !(*sc->sc_cl->cl_fifo_full)(sc)) {
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_queue, ccb, ccb_chain);
		ccb->ccb_flags |= CAC_CCB_ACTIVE;
		(*sc->sc_cl->cl_submit)(sc, ccb);
	}

	(*sc->sc_cl->cl_intr_enable)(sc, 1);

	return (0);
}

/*
 * Process a finished CCB.
 */
void
cac_ccb_done(struct cac_softc *sc, struct cac_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_xs;
	int error = 0;

	if ((ccb->ccb_flags & CAC_CCB_ACTIVE) == 0) {
		printf("%s: CCB not active, xs=%p\n", sc->sc_dv.dv_xname, xs);
		if (xs) {
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
		}
		return;
	}

	if ((ccb->ccb_flags & (CAC_CCB_DATA_IN | CAC_CCB_DATA_OUT)) != 0) {
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap_xfer, 0,
		    ccb->ccb_dmamap_xfer->dm_mapsize,
		    ccb->ccb_flags & CAC_CCB_DATA_IN ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap_xfer);
	}

	if ((ccb->ccb_req.error & CAC_RET_SOFT_ERROR) != 0)
		printf("%s: soft error; corrected\n", sc->sc_dv.dv_xname);
	if ((ccb->ccb_req.error & CAC_RET_HARD_ERROR) != 0) {
		error = 1;
		printf("%s: hard error\n", sc->sc_dv.dv_xname);
	}
	if ((ccb->ccb_req.error & CAC_RET_CMD_REJECTED) != 0) {
		error = 1;
		printf("%s: invalid request\n", sc->sc_dv.dv_xname);
	}

	cac_ccb_free(sc, ccb);
	if (xs) {
		if (error)
			xs->error = XS_DRIVER_STUFFUP;
		else
			xs->resid = 0;

		xs->flags |= ITSDONE;
		scsi_done(xs);
	}
}

/*
 * Allocate a CCB.
 */
struct cac_ccb *
cac_ccb_alloc(struct cac_softc *sc, int nosleep)
{
	struct cac_ccb *ccb;

	if ((ccb = SIMPLEQ_FIRST(&sc->sc_ccb_free)) != NULL)
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_free, ccb, ccb_chain);
	else
		ccb = NULL;
	return (ccb);
}

/*
 * Put a CCB onto the freelist.
 */
void
cac_ccb_free(struct cac_softc *sc, struct cac_ccb *ccb)
{

	ccb->ccb_flags = 0;
	SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_free, ccb, ccb_chain);
}

int
cac_get_dinfo(sc, target)
	struct cac_softc *sc;
	int target;
{
	if (sc->sc_dinfos[target].ncylinders)
		return (0);

	if (cac_cmd(sc, CAC_CMD_GET_LOG_DRV_INFO, &sc->sc_dinfos[target],
	    sizeof(*sc->sc_dinfos), target, 0, CAC_CCB_DATA_IN, NULL)) {
		printf("%s: CMD_GET_LOG_DRV_INFO failed\n",
		    sc->sc_dv.dv_xname);
		return (-1);
	}

	return (0);
}

void
cacminphys(bp)
	struct buf *bp;
{
	if (bp->b_bcount > CAC_MAX_XFER)
		bp->b_bcount = CAC_MAX_XFER;
	minphys(bp);
}

void
cac_copy_internal_data(xs, v, size)
	struct scsi_xfer *xs;
	void *v;
	size_t size;
{
	size_t copy_cnt;

	if (!xs->datalen)
		printf("uio move is not yet supported\n");
	else {
		copy_cnt = MIN(size, xs->datalen);
		bcopy(v, xs->data, copy_cnt);
	}
}

int
cac_scsi_cmd(xs)
	struct scsi_xfer *xs;
{
	struct scsi_link *link = xs->sc_link;
	struct cac_softc *sc = link->adapter_softc;
	struct cac_drive_info *dinfo;
	struct scsi_inquiry_data inq;
	struct scsi_sense_data sd;
	struct {
		struct scsi_mode_header hd;
		struct scsi_blk_desc bd;
		union scsi_disk_pages dp;
	} mpd;
	struct scsi_read_cap_data rcd;
	u_int8_t target = link->target;
	u_int32_t blockno, blockcnt, size;
	struct scsi_rw *rw;
	struct scsi_rw_big *rwb;
	int op, flags, s, error, poll;
	const char *p;

	if (target >= sc->sc_nunits || link->lun != 0) {
		xs->error = XS_DRIVER_STUFFUP;
		return (COMPLETE);
	}

	s = splbio();
	xs->error = XS_NOERROR;
	xs->free_list.le_next = NULL;
	dinfo = &sc->sc_dinfos[target];

	switch (xs->cmd->opcode) {
	case TEST_UNIT_READY:
	case START_STOP:
#if 0
	case VERIFY:
#endif
		break;

	case REQUEST_SENSE:
		bzero(&sd, sizeof sd);
		sd.error_code = 0x70;
		sd.segment = 0;
		sd.flags = SKEY_NO_SENSE;
		*(u_int32_t*)sd.info = htole32(0);
		sd.extra_len = 0;
		cac_copy_internal_data(xs, &sd, sizeof sd);
		break;

	case INQUIRY:
		if (cac_get_dinfo(sc, target)) {
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		bzero(&inq, sizeof inq);
		inq.device = T_DIRECT;
		inq.dev_qual2 = 0;
		inq.version = 2;
		inq.response_format = 2;
		inq.additional_length = 32;
		strlcpy(inq.vendor, "Compaq  ", sizeof inq.vendor);
		switch (CAC_GET1(dinfo->mirror)) {
		case 0: p = "RAID0";	break;
		case 1: p = "RAID4";	break;
		case 2: p = "RAID1";	break;
		case 3: p = "RAID5";	break;
		default:p = "<UNK>";	break;
		}
		snprintf(inq.product, sizeof inq.product, "%s volume  #%02d",
		    p, target);
		strlcpy(inq.revision, "   ", sizeof inq.revision);
		cac_copy_internal_data(xs, &inq, sizeof inq);
		break;

	case MODE_SENSE:
		if (cac_get_dinfo(sc, target)) {
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		bzero(&mpd, sizeof mpd);
		switch (((struct scsi_mode_sense *)xs->cmd)->page) {
		case 4:
			/* scsi_disk.h says this should be 0x16 */
			mpd.dp.rigid_geometry.pg_length = 0x16;
			mpd.hd.data_length = sizeof mpd.hd + sizeof mpd.bd +
			    mpd.dp.rigid_geometry.pg_length;
			mpd.hd.blk_desc_len = sizeof mpd.bd;

			/* XXX */
			mpd.hd.dev_spec = 0;
			_lto3b(CAC_SECTOR_SIZE, mpd.bd.blklen);
			mpd.dp.rigid_geometry.pg_code = 4;
			_lto3b(CAC_GET2(dinfo->ncylinders),
			    mpd.dp.rigid_geometry.ncyl);
			mpd.dp.rigid_geometry.nheads =
			    CAC_GET1(dinfo->nheads);
			cac_copy_internal_data(xs, (u_int8_t *)&mpd,
			    sizeof mpd);
			break;

		default:
			printf("%s: mode sense page %d not simulated\n",
			    sc->sc_dv.dv_xname,
			    ((struct scsi_mode_sense *)xs->cmd)->page);
			xs->error = XS_DRIVER_STUFFUP;
			splx(s);
			return (TRY_AGAIN_LATER);
		}
		break;

	case READ_CAPACITY:
		if (cac_get_dinfo(sc, target)) {
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		bzero(&rcd, sizeof rcd);
		_lto4b( CAC_GET2(dinfo->ncylinders) * CAC_GET1(dinfo->nheads) *
		    CAC_GET1(dinfo->nsectors) - 1, rcd.addr);
		_lto4b(CAC_SECTOR_SIZE, rcd.length);
		cac_copy_internal_data(xs, &rcd, sizeof rcd);
		break;

	case PREVENT_ALLOW:
		break;

	case SYNCHRONIZE_CACHE:
		if (cac_flush(sc))
			xs->error = XS_DRIVER_STUFFUP;
		break;

	case READ_COMMAND:
	case READ_BIG:
	case WRITE_COMMAND:
	case WRITE_BIG:

		flags = 0;
		/* A read or write operation. */
		if (xs->cmdlen == 6) {
			rw = (struct scsi_rw *)xs->cmd;
			blockno = _3btol(rw->addr) &
			    (SRW_TOPADDR << 16 | 0xffff);
			blockcnt = rw->length ? rw->length : 0x100;
		} else {
			rwb = (struct scsi_rw_big *)xs->cmd;
			blockno = _4btol(rwb->addr);
			blockcnt = _2btol(rwb->length);
		}
		size = CAC_GET2(dinfo->ncylinders) *
		    CAC_GET1(dinfo->nheads) * CAC_GET1(dinfo->nsectors);
		if (blockno >= size || blockno + blockcnt > size) {
			printf("%s: out of bounds %u-%u >= %u\n",
			    sc->sc_dv.dv_xname, blockno, blockcnt, size);
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			break;
		}

		switch (xs->cmd->opcode) {
		case READ_COMMAND:
		case READ_BIG:
			op = CAC_CMD_READ;
			flags = CAC_CCB_DATA_IN;
			break;
		case WRITE_COMMAND:
		case WRITE_BIG:
			op = CAC_CMD_WRITE;
			flags = CAC_CCB_DATA_OUT;
			break;
		}

		poll = xs->flags & SCSI_POLL;
		if ((error = cac_cmd(sc, op, xs->data, blockcnt * DEV_BSIZE,
		    target, blockno, flags, xs))) {

			if (error == ENOMEM) {
				xs->error = XS_BUSY;
				splx(s);
				return (TRY_AGAIN_LATER);
			} else if (poll) {
				xs->error = XS_TIMEOUT;
				splx(s);
				return (TRY_AGAIN_LATER);
			} else {
				xs->error = XS_DRIVER_STUFFUP;
				scsi_done(xs);
				break;
			}
		}

		splx(s);

		if (poll)
			return (COMPLETE);
		else
			return (SUCCESSFULLY_QUEUED);

	default:
		xs->error = XS_DRIVER_STUFFUP;
	}
	splx(s);

	return (COMPLETE);
}

/*
 * Board specific linkage shared between multiple bus types.
 */

int
cac_l0_fifo_full(struct cac_softc *sc)
{

	return (cac_inl(sc, CAC_REG_CMD_FIFO) == 0);
}

void
cac_l0_submit(struct cac_softc *sc, struct cac_ccb *ccb)
{
#ifdef CAC_DEBUG
	printf("submit-%x ", ccb->ccb_paddr);
#endif
	bus_dmamap_sync(sc->sc_dmat, sc->sc_dmamap, 0,
	    sc->sc_dmamap->dm_mapsize,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	cac_outl(sc, CAC_REG_CMD_FIFO, ccb->ccb_paddr);
}

struct cac_ccb *
cac_l0_completed(sc)
	struct cac_softc *sc;
{
	struct cac_ccb *ccb;
	paddr_t off;

	if (!(off = cac_inl(sc, CAC_REG_DONE_FIFO)))
		return NULL;
#ifdef CAC_DEBUG
	printf("compl-%x ", off);
#endif
	if (off & 3 && ccb->ccb_req.error == 0)
		ccb->ccb_req.error = CAC_RET_CMD_INVALID;

	off = (off & ~3) - sc->sc_ccbs_paddr;
	ccb = (struct cac_ccb *)(sc->sc_ccbs + off);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dmamap, 0,
	    sc->sc_dmamap->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);

	return (ccb);
}

int
cac_l0_intr_pending(struct cac_softc *sc)
{

	return (cac_inl(sc, CAC_REG_INTR_PENDING));
}

void
cac_l0_intr_enable(struct cac_softc *sc, int state)
{

	cac_outl(sc, CAC_REG_INTR_MASK,
	    state ? CAC_INTR_ENABLE : CAC_INTR_DISABLE);
}
