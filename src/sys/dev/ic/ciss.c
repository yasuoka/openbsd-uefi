/*	$OpenBSD: ciss.c,v 1.28 2007/09/18 00:46:41 krw Exp $	*/

/*
 * Copyright (c) 2005,2006 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bio.h"

/* #define CISS_DEBUG */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/kthread.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/cissreg.h>
#include <dev/ic/cissvar.h>

#if NBIO > 0
#include <dev/biovar.h>
#endif
#include <sys/sensors.h>

#ifdef CISS_DEBUG
#define	CISS_DPRINTF(m,a)	if (ciss_debug & (m)) printf a
#define	CISS_D_CMD	0x0001
#define	CISS_D_INTR	0x0002
#define	CISS_D_MISC	0x0004
#define	CISS_D_DMA	0x0008
#define	CISS_D_IOCTL	0x0010
#define	CISS_D_ERR	0x0020
int ciss_debug = 0
/*	| CISS_D_CMD */
/*	| CISS_D_INTR */
/*	| CISS_D_MISC */
/*	| CISS_D_DMA */
/*	| CISS_D_IOCTL */
/*	| CISS_D_ERR */
	;
#else
#define	CISS_DPRINTF(m,a)	/* m, a */
#endif

struct cfdriver ciss_cd = {
	NULL, "ciss", DV_DULL
};

int	ciss_scsi_cmd(struct scsi_xfer *xs);
int	ciss_scsi_ioctl(struct scsi_link *link, u_long cmd,
    caddr_t addr, int flag, struct proc *p);
void	cissminphys(struct buf *bp);

struct scsi_adapter ciss_switch = {
	ciss_scsi_cmd, cissminphys, NULL, NULL, ciss_scsi_ioctl
};

struct scsi_device ciss_dev = {
	NULL, NULL, NULL, NULL
};

int	ciss_scsi_raw_cmd(struct scsi_xfer *xs);

struct scsi_adapter ciss_raw_switch = {
	ciss_scsi_raw_cmd, cissminphys, NULL, NULL,
};

struct scsi_device ciss_raw_dev = {
	NULL, NULL, NULL, NULL
};

#if NBIO > 0
int	ciss_ioctl(struct device *, u_long, caddr_t);
#endif
int	ciss_sync(struct ciss_softc *sc);
void	ciss_heartbeat(void *v);
void	ciss_shutdown(void *v);
void	ciss_kthread(void *v);
#ifndef SMALL_KERNEL
void	ciss_sensors(void *);
#endif

struct ciss_ccb *ciss_get_ccb(struct ciss_softc *sc);
void	ciss_put_ccb(struct ciss_ccb *ccb);
int	ciss_cmd(struct ciss_ccb *ccb, int flags, int wait);
int	ciss_done(struct ciss_ccb *ccb);
int	ciss_error(struct ciss_ccb *ccb);

struct ciss_ld *ciss_pdscan(struct ciss_softc *sc, int ld);
int	ciss_inq(struct ciss_softc *sc, struct ciss_inquiry *inq);
int	ciss_ldmap(struct ciss_softc *sc);
int	ciss_ldid(struct ciss_softc *, int, struct ciss_ldid *);
int	ciss_ldstat(struct ciss_softc *, int, struct ciss_ldstat *);
int	ciss_pdid(struct ciss_softc *, u_int8_t, struct ciss_pdid *, int);
int	ciss_blink(struct ciss_softc *, int, int, int, struct ciss_blink *);

struct ciss_ccb *
ciss_get_ccb(struct ciss_softc *sc)
{
	struct ciss_ccb *ccb;

	if ((ccb = TAILQ_LAST(&sc->sc_free_ccb, ciss_queue_head))) {
		TAILQ_REMOVE(&sc->sc_free_ccb, ccb, ccb_link);
		ccb->ccb_state = CISS_CCB_READY;
	}
	return ccb;
}

void
ciss_put_ccb(struct ciss_ccb *ccb)
{
	struct ciss_softc *sc = ccb->ccb_sc;

	ccb->ccb_state = CISS_CCB_FREE;
	TAILQ_INSERT_TAIL(&sc->sc_free_ccb, ccb, ccb_link);
}

int
ciss_attach(struct ciss_softc *sc)
{
	struct scsibus_attach_args saa;
	struct scsibus_softc *scsibus;
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	struct ciss_inquiry *inq;
	bus_dma_segment_t seg[1];
	int error, i, total, rseg, maxfer;
	ciss_lock_t lock;
	paddr_t pa;

	bus_space_read_region_4(sc->iot, sc->cfg_ioh, sc->cfgoff,
	    (u_int32_t *)&sc->cfg, sizeof(sc->cfg) / 4);

	if (sc->cfg.signature != CISS_SIGNATURE) {
		printf(": bad sign 0x%08x\n", sc->cfg.signature);
		return -1;
	}

	if (!(sc->cfg.methods & CISS_METH_SIMPL)) {
		printf(": not simple 0x%08x\n", sc->cfg.methods);
		return -1;
	}

	sc->cfg.rmethod = CISS_METH_SIMPL;
	sc->cfg.paddr_lim = 0;			/* 32bit addrs */
	sc->cfg.int_delay = 0;			/* disable coalescing */
	sc->cfg.int_count = 0;
	strlcpy(sc->cfg.hostname, "HUMPPA", sizeof(sc->cfg.hostname));
	sc->cfg.driverf |= CISS_DRV_PRF;	/* enable prefetch */
	if (!sc->cfg.maxsg)
		sc->cfg.maxsg = MAXPHYS / PAGE_SIZE;

	bus_space_write_region_4(sc->iot, sc->cfg_ioh, sc->cfgoff,
	    (u_int32_t *)&sc->cfg, sizeof(sc->cfg) / 4);
	bus_space_barrier(sc->iot, sc->cfg_ioh, sc->cfgoff, sizeof(sc->cfg),
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);

	bus_space_write_4(sc->iot, sc->ioh, CISS_IDB, CISS_IDB_CFG);
	bus_space_barrier(sc->iot, sc->ioh, CISS_IDB, 4,
	    BUS_SPACE_BARRIER_WRITE);
	for (i = 1000; i--; DELAY(1000)) {
		/* XXX maybe IDB is really 64bit? - hp dl380 needs this */
		(void)bus_space_read_4(sc->iot, sc->ioh, CISS_IDB + 4);
		if (!(bus_space_read_4(sc->iot, sc->ioh, CISS_IDB) & CISS_IDB_CFG))
			break;
		bus_space_barrier(sc->iot, sc->ioh, CISS_IDB, 4,
		    BUS_SPACE_BARRIER_READ);
	}

	if (bus_space_read_4(sc->iot, sc->ioh, CISS_IDB) & CISS_IDB_CFG) {
		printf(": cannot set config\n");
		return -1;
	}

	bus_space_read_region_4(sc->iot, sc->cfg_ioh, sc->cfgoff,
	    (u_int32_t *)&sc->cfg, sizeof(sc->cfg) / 4);

	if (!(sc->cfg.amethod & CISS_METH_SIMPL)) {
		printf(": cannot simplify 0x%08x\n", sc->cfg.amethod);
		return -1;
	}

	/* i'm ready for you and i hope you're ready for me */
	for (i = 30000; i--; DELAY(1000)) {
		if (bus_space_read_4(sc->iot, sc->cfg_ioh, sc->cfgoff +
		    offsetof(struct ciss_config, amethod)) & CISS_METH_READY)
			break;
		bus_space_barrier(sc->iot, sc->cfg_ioh, sc->cfgoff +
		    offsetof(struct ciss_config, amethod), 4,
		    BUS_SPACE_BARRIER_READ);
	}

	if (!(bus_space_read_4(sc->iot, sc->cfg_ioh, sc->cfgoff +
	    offsetof(struct ciss_config, amethod)) & CISS_METH_READY)) {
		printf(": she never came ready for me 0x%08x\n",
		    sc->cfg.amethod);
		return -1;
	}

	sc->maxcmd = sc->cfg.maxcmd;
	sc->maxsg = sc->cfg.maxsg;
	if (sc->maxsg > MAXPHYS / PAGE_SIZE)
		sc->maxsg = MAXPHYS / PAGE_SIZE;
	i = sizeof(struct ciss_ccb) +
	    sizeof(ccb->ccb_cmd.sgl[0]) * (sc->maxsg - 1);
	for (sc->ccblen = 0x10; sc->ccblen < i; sc->ccblen <<= 1);

	total = sc->ccblen * sc->maxcmd;
	if ((error = bus_dmamem_alloc(sc->dmat, total, PAGE_SIZE, 0,
	    sc->cmdseg, 1, &rseg, BUS_DMA_NOWAIT))) {
		printf(": cannot allocate CCBs (%d)\n", error);
		return -1;
	}

	if ((error = bus_dmamem_map(sc->dmat, sc->cmdseg, rseg, total,
	    (caddr_t *)&sc->ccbs, BUS_DMA_NOWAIT))) {
		printf(": cannot map CCBs (%d)\n", error);
		return -1;
	}
	bzero(sc->ccbs, total);

	if ((error = bus_dmamap_create(sc->dmat, total, 1,
	    total, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &sc->cmdmap))) {
		printf(": cannot create CCBs dmamap (%d)\n", error);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		return -1;
	}

	if ((error = bus_dmamap_load(sc->dmat, sc->cmdmap, sc->ccbs, total,
	    NULL, BUS_DMA_NOWAIT))) {
		printf(": cannot load CCBs dmamap (%d)\n", error);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

	TAILQ_INIT(&sc->sc_ccbq);
	TAILQ_INIT(&sc->sc_ccbdone);
	TAILQ_INIT(&sc->sc_free_ccb);

	maxfer = sc->maxsg * PAGE_SIZE;
	for (i = 0; total; i++, total -= sc->ccblen) {
		ccb = sc->ccbs + i * sc->ccblen;
		cmd = &ccb->ccb_cmd;
		pa = sc->cmdseg[0].ds_addr + i * sc->ccblen;

		ccb->ccb_sc = sc;
		ccb->ccb_cmdpa = pa + offsetof(struct ciss_ccb, ccb_cmd);
		ccb->ccb_state = CISS_CCB_FREE;

		cmd->id = htole32(i << 2);
		cmd->id_hi = htole32(0);
		cmd->sgin = sc->maxsg;
		cmd->sglen = htole16((u_int16_t)cmd->sgin);
		cmd->err_len = htole32(sizeof(ccb->ccb_err));
		pa += offsetof(struct ciss_ccb, ccb_err);
		cmd->err_pa = htole64((u_int64_t)pa);

		if ((error = bus_dmamap_create(sc->dmat, maxfer, sc->maxsg,
		    maxfer, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &ccb->ccb_dmamap)))
			break;

		TAILQ_INSERT_TAIL(&sc->sc_free_ccb, ccb, ccb_link);
	}

	if (i < sc->maxcmd) {
		printf(": cannot create ccb#%d dmamap (%d)\n", i, error);
		if (i == 0) {
			/* TODO leaking cmd's dmamaps and shitz */
			bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
			bus_dmamap_destroy(sc->dmat, sc->cmdmap);
			return -1;
		}
	}

	if ((error = bus_dmamem_alloc(sc->dmat, PAGE_SIZE, PAGE_SIZE, 0,
	    seg, 1, &rseg, BUS_DMA_NOWAIT))) {
		printf(": cannot allocate scratch buffer (%d)\n", error);
		return -1;
	}

	if ((error = bus_dmamem_map(sc->dmat, seg, rseg, PAGE_SIZE,
	    (caddr_t *)&sc->scratch, BUS_DMA_NOWAIT))) {
		printf(": cannot map scratch buffer (%d)\n", error);
		return -1;
	}
	bzero(sc->scratch, PAGE_SIZE);

	lock = CISS_LOCK_SCRATCH(sc);
	inq = sc->scratch;
	if (ciss_inq(sc, inq)) {
		printf(": adapter inquiry failed\n");
		CISS_UNLOCK_SCRATCH(sc, lock);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

	if (!(inq->flags & CISS_INQ_BIGMAP)) {
		printf(": big map is not supported, flags=%b\n",
		    inq->flags, CISS_INQ_BITS);
		CISS_UNLOCK_SCRATCH(sc, lock);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

	sc->maxunits = inq->numld;
	sc->nbus = inq->nscsi_bus;
	sc->ndrives = inq->buswidth;
	printf(": %d LD%s, HW rev %d, FW %4.4s/%4.4s\n",
	    inq->numld, inq->numld == 1? "" : "s",
	    inq->hw_rev, inq->fw_running, inq->fw_stored);

	CISS_UNLOCK_SCRATCH(sc, lock);

	timeout_set(&sc->sc_hb, ciss_heartbeat, sc);
	timeout_add(&sc->sc_hb, hz * 3);

	/* map LDs */
	if (ciss_ldmap(sc)) {
		printf("%s: adapter LD map failed\n", sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

	if (!(sc->sc_lds = malloc(sc->maxunits * sizeof(*sc->sc_lds),
	    M_DEVBUF, M_NOWAIT | M_ZERO))) {
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

	sc->sc_flush = CISS_FLUSH_ENABLE;
	if (!(sc->sc_sh = shutdownhook_establish(ciss_shutdown, sc))) {
		printf(": unable to establish shutdown hook\n");
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

#if 0
	if (kthread_create(ciss_kthread, sc, NULL, "%s", sc->sc_dev.dv_xname)) {
		printf(": unable to create kernel thread\n");
		shutdownhook_disestablish(sc->sc_sh);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}
#endif

	sc->sc_link.device = &ciss_dev;
	sc->sc_link.adapter_softc = sc;
	sc->sc_link.openings = sc->maxcmd / (sc->maxunits? sc->maxunits : 1);
#if NBIO > 0
	/* XXX Reserve some ccb's for sensor and bioctl. */
	if (sc->maxunits < 2 && sc->sc_link.openings > 2)
		sc->sc_link.openings -= 2;
#endif
	sc->sc_link.adapter = &ciss_switch;
	sc->sc_link.adapter_target = sc->maxunits;
	sc->sc_link.adapter_buswidth = sc->maxunits;
	bzero(&saa, sizeof(saa));
	saa.saa_sc_link = &sc->sc_link;
	scsibus = (struct scsibus_softc *)config_found_sm(&sc->sc_dev,
	    &saa, scsiprint, NULL);

#if 0
	sc->sc_link_raw.device = &ciss_raw_dev;
	sc->sc_link_raw.adapter_softc = sc;
	sc->sc_link.openings = sc->maxcmd / (sc->maxunits? sc->maxunits : 1);
	sc->sc_link_raw.adapter = &ciss_raw_switch;
	sc->sc_link_raw.adapter_target = sc->ndrives;
	sc->sc_link_raw.adapter_buswidth = sc->ndrives;
	bzero(&saa, sizeof(saa));
	saa.saa_sc_link = &sc->sc_link_raw;
	rawbus = (struct scsibus_softc *)config_found_sm(&sc->sc_dev,
	    &saa, scsiprint, NULL);
#endif

#if NBIO > 0
	/* XXX for now we can only deal w/ one volume and need reserved ccbs. */
	if (!scsibus || sc->maxunits > 1 || sc->sc_link.openings == sc->maxcmd)
		return 0;

	/* now map all the physdevs into their lds */
	/* XXX currently we assign all pf 'em into ld#0 */
	for (i = 0; i < sc->maxunits; i++)
		if (!(sc->sc_lds[i] = ciss_pdscan(sc, i)))
			return 0;

	if (bio_register(&sc->sc_dev, ciss_ioctl) != 0)
		printf("%s: controller registration failed",
		    sc->sc_dev.dv_xname);

	sc->sc_flags |= CISS_BIO;
#ifndef SMALL_KERNEL
	sc->sensors = malloc(sizeof(struct ksensor) * sc->maxunits,
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sensors) {
		strlcpy(sc->sensordev.xname, sc->sc_dev.dv_xname,
		    sizeof(sc->sensordev.xname));
		for (i = 0; i < sc->maxunits;
		    sensor_attach(&sc->sensordev, &sc->sensors[i++])) {
			sc->sensors[i].type = SENSOR_DRIVE;
			sc->sensors[i].status = SENSOR_S_UNKNOWN;
			strlcpy(sc->sensors[i].desc, ((struct device *)
			    scsibus->sc_link[i][0]->device_softc)->dv_xname,
			    sizeof(sc->sensors[i].desc));
			strlcpy(sc->sc_lds[i]->xname, ((struct device *)
			    scsibus->sc_link[i][0]->device_softc)->dv_xname,
			    sizeof(sc->sc_lds[i]->xname));
		}
		if (sensor_task_register(sc, ciss_sensors, 10) == NULL)
			free(sc->sensors, M_DEVBUF);
		else
			sensordev_install(&sc->sensordev);
	}
#endif /* SMALL_KERNEL */
#endif /* BIO > 0 */

	return 0;
}

void
ciss_shutdown(void *v)
{
	struct ciss_softc *sc = v;

	sc->sc_flush = CISS_FLUSH_DISABLE;
	timeout_del(&sc->sc_hb);
	ciss_sync(sc);
}

void
cissminphys(struct buf *bp)
{
#if 0	/* TODO */
#define	CISS_MAXFER	(PAGE_SIZE * (sc->maxsg + 1))
	if (bp->b_bcount > CISS_MAXFER)
		bp->b_bcount = CISS_MAXFER;
#endif
	minphys(bp);
}               

/*
 * submit a command and optionally wait for completition.
 * wait arg abuses SCSI_POLL|SCSI_NOSLEEP flags to request
 * to wait (SCSI_POLL) and to allow tsleep() (!SCSI_NOSLEEP)
 * instead of busy loop waiting
 */
int
ciss_cmd(struct ciss_ccb *ccb, int flags, int wait)
{
	struct ciss_softc *sc = ccb->ccb_sc;
	struct ciss_cmd *cmd = &ccb->ccb_cmd;
	struct ciss_ccb *ccb1;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	u_int32_t id;
	int i, tohz, error = 0;

	if (ccb->ccb_state != CISS_CCB_READY) {
		printf("%s: ccb %d not ready state=%b\n", sc->sc_dev.dv_xname,
		    cmd->id, ccb->ccb_state, CISS_CCB_BITS);
		return (EINVAL);
	}

	if (ccb->ccb_data) {
		bus_dma_segment_t *sgd;

		if ((error = bus_dmamap_load(sc->dmat, dmap, ccb->ccb_data,
		    ccb->ccb_len, NULL, flags))) {
			if (error == EFBIG)
				printf("more than %d dma segs\n", sc->maxsg);
			else
				printf("error %d loading dma map\n", error);
			ciss_put_ccb(ccb);
			return (error);
		}
		cmd->sgin = dmap->dm_nsegs;

		sgd = dmap->dm_segs;
		CISS_DPRINTF(CISS_D_DMA, ("data=%p/%u<0x%lx/%u",
		    ccb->ccb_data, ccb->ccb_len, sgd->ds_addr, sgd->ds_len));

		for (i = 0; i < dmap->dm_nsegs; sgd++, i++) {
			cmd->sgl[i].addr_lo = htole32(sgd->ds_addr);
			cmd->sgl[i].addr_hi =
			    htole32((u_int64_t)sgd->ds_addr >> 32);
			cmd->sgl[i].len = htole32(sgd->ds_len);
			cmd->sgl[i].flags = htole32(0);
			if (i)
				CISS_DPRINTF(CISS_D_DMA,
				    (",0x%lx/%u", sgd->ds_addr, sgd->ds_len));
		}

		CISS_DPRINTF(CISS_D_DMA, ("> "));

		bus_dmamap_sync(sc->dmat, dmap, 0, dmap->dm_mapsize,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	} else
		cmd->sgin = 0;
	cmd->sglen = htole16((u_int16_t)cmd->sgin);
	bzero(&ccb->ccb_err, sizeof(ccb->ccb_err));

	bus_dmamap_sync(sc->dmat, sc->cmdmap, 0, sc->cmdmap->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	if ((wait & (SCSI_POLL|SCSI_NOSLEEP)) == (SCSI_POLL|SCSI_NOSLEEP))
		bus_space_write_4(sc->iot, sc->ioh, CISS_IMR,
		    bus_space_read_4(sc->iot, sc->ioh, CISS_IMR) | sc->iem);

	TAILQ_INSERT_TAIL(&sc->sc_ccbq, ccb, ccb_link);
	ccb->ccb_state = CISS_CCB_ONQ;
	CISS_DPRINTF(CISS_D_CMD, ("submit=0x%x ", cmd->id));
	bus_space_write_4(sc->iot, sc->ioh, CISS_INQ, ccb->ccb_cmdpa);

	if (wait & SCSI_POLL) {
		struct timeval tv;
		int etick;
		CISS_DPRINTF(CISS_D_CMD, ("waiting "));

		i = ccb->ccb_xs? ccb->ccb_xs->timeout : 60000;
		tv.tv_sec = i / 1000;
		tv.tv_usec = (i % 1000) * 1000;
		tohz = tvtohz(&tv);
		if (tohz == 0)
			tohz = 1;
		for (i *= 100, etick = tick + tohz; i--; ) {
			if (!(wait & SCSI_NOSLEEP)) {
				ccb->ccb_state = CISS_CCB_POLL;
				CISS_DPRINTF(CISS_D_CMD, ("tsleep(%d) ", tohz));
				if (tsleep(ccb, PRIBIO + 1, "ciss_cmd",
				    tohz) == EWOULDBLOCK) {
					break;
				}
				if (ccb->ccb_state != CISS_CCB_ONQ) {
					tohz = etick - tick;
					if (tohz <= 0)
						break;
					CISS_DPRINTF(CISS_D_CMD, ("T"));
					continue;
				}
				ccb1 = ccb;
			} else {
				DELAY(10);

				if (!(bus_space_read_4(sc->iot, sc->ioh,
				    CISS_ISR) & sc->iem)) {
					CISS_DPRINTF(CISS_D_CMD, ("N"));
					continue;
				}

				if ((id = bus_space_read_4(sc->iot, sc->ioh,
				    CISS_OUTQ)) == 0xffffffff) {
					CISS_DPRINTF(CISS_D_CMD, ("Q"));
					continue;
				}

				CISS_DPRINTF(CISS_D_CMD, ("got=0x%x ", id));
				ccb1 = sc->ccbs + (id >> 2) * sc->ccblen;
				ccb1->ccb_cmd.id = htole32(id);
			}

			error = ciss_done(ccb1);
			if (ccb1 == ccb)
				break;
		}

		/* if never got a chance to be done above... */
		if (ccb->ccb_state != CISS_CCB_FREE) {
			ccb->ccb_err.cmd_stat = CISS_ERR_TMO;
			error = ciss_done(ccb);
		}

		CISS_DPRINTF(CISS_D_CMD, ("done %d:%d",
		    ccb->ccb_err.cmd_stat, ccb->ccb_err.scsi_stat));
	}

	if ((wait & (SCSI_POLL|SCSI_NOSLEEP)) == (SCSI_POLL|SCSI_NOSLEEP))
		bus_space_write_4(sc->iot, sc->ioh, CISS_IMR,
		    bus_space_read_4(sc->iot, sc->ioh, CISS_IMR) & ~sc->iem);

	return (error);
}

int
ciss_done(struct ciss_ccb *ccb)
{
	struct ciss_softc *sc = ccb->ccb_sc;
	struct scsi_xfer *xs = ccb->ccb_xs;
	ciss_lock_t lock;
	int error = 0;

	CISS_DPRINTF(CISS_D_CMD, ("ciss_done(%p) ", ccb));

	if (ccb->ccb_state != CISS_CCB_ONQ) {
		printf("%s: unqueued ccb %p ready, state=%b\n",
		    sc->sc_dev.dv_xname, ccb, ccb->ccb_state, CISS_CCB_BITS);
		return 1;
	}

	lock = CISS_LOCK(sc);
	ccb->ccb_state = CISS_CCB_READY;
	TAILQ_REMOVE(&sc->sc_ccbq, ccb, ccb_link);

	if (ccb->ccb_cmd.id & CISS_CMD_ERR)
		error = ciss_error(ccb);

	if (ccb->ccb_data) {
		bus_dmamap_sync(sc->dmat, ccb->ccb_dmamap, 0,
		    ccb->ccb_dmamap->dm_mapsize, (xs->flags & SCSI_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->dmat, ccb->ccb_dmamap);
		ccb->ccb_xs = NULL;
		ccb->ccb_data = NULL;
	}

	ciss_put_ccb(ccb);

	if (xs) {
		xs->resid = 0;
		xs->flags |= ITSDONE;
		CISS_DPRINTF(CISS_D_CMD, ("scsi_done(%p) ", xs));
		scsi_done(xs);
	}
	CISS_UNLOCK(sc, lock);

	return error;
}

int
ciss_error(struct ciss_ccb *ccb)
{
	struct ciss_softc *sc = ccb->ccb_sc;
	struct ciss_error *err = &ccb->ccb_err;
	struct scsi_xfer *xs = ccb->ccb_xs;
	int rv;

	switch ((rv = letoh16(err->cmd_stat))) {
	case CISS_ERR_OK:
		rv = 0;
		break;

	case CISS_ERR_INVCMD:
		printf("%s: invalid cmd 0x%x: 0x%x is not valid @ 0x%x[%d]\n",
		    sc->sc_dev.dv_xname, ccb->ccb_cmd.id,
		    err->err_info, err->err_type[3], err->err_type[2]);
		if (xs) {
			bzero(&xs->sense, sizeof(xs->sense));
			xs->sense.error_code = SSD_ERRCODE_VALID | 0x70;
			xs->sense.flags = SKEY_ILLEGAL_REQUEST;
			xs->sense.add_sense_code = 0x24; /* ill field */
			xs->error = XS_SENSE;
		}
		rv = EIO;
		break;

	case CISS_ERR_TMO:
		xs->error = XS_TIMEOUT;
		rv = ETIMEDOUT;
		break;

	default:
		if (xs) {
			switch (err->scsi_stat) {
			case SCSI_CHECK:
				xs->error = XS_SENSE;
				bcopy(&err->sense[0], &xs->sense,
				    sizeof(xs->sense));
				rv = EIO;
				break;

			case SCSI_BUSY:
				xs->error = XS_BUSY;
				rv = EBUSY;
				break;

			default:
				CISS_DPRINTF(CISS_D_ERR, ("%s: "
				    "cmd_stat %x scsi_stat 0x%x\n",
				    sc->sc_dev.dv_xname, rv, err->scsi_stat));
				xs->error = XS_DRIVER_STUFFUP;
				rv = EIO;
				break;
			}
			xs->resid = letoh32(err->resid);
		} else
			rv = EIO;
	}
	ccb->ccb_cmd.id &= htole32(~3);

	return rv;
}

int
ciss_inq(struct ciss_softc *sc, struct ciss_inquiry *inq)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;

	ccb = ciss_get_ccb(sc);
	ccb->ccb_len = sizeof(*inq);
	ccb->ccb_data = inq;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = htole32(CISS_CMD_MODE_PERIPH);
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_IN;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_GET;
	cmd->cdb[6] = CISS_CMS_CTRL_CTRL;
	cmd->cdb[7] = sizeof(*inq) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*inq) & 0xff;

	return ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL|SCSI_NOSLEEP);
}

int
ciss_ldmap(struct ciss_softc *sc)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	struct ciss_ldmap *lmap;
	ciss_lock_t lock;
	int total, rv;

	lock = CISS_LOCK_SCRATCH(sc);
	lmap = sc->scratch;
	lmap->size = htobe32(sc->maxunits * sizeof(lmap->map));
	total = sizeof(*lmap) + (sc->maxunits - 1) * sizeof(lmap->map);

	ccb = ciss_get_ccb(sc);
	ccb->ccb_len = total;
	ccb->ccb_data = lmap;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = CISS_CMD_MODE_PERIPH;
	cmd->tgt2 = 0;
	cmd->cdblen = 12;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_IN;
	cmd->tmo = htole16(30);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_LDMAP;
	cmd->cdb[8] = total >> 8;	/* biiiig endian */
	cmd->cdb[9] = total & 0xff;

	rv = ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL|SCSI_NOSLEEP);
	CISS_UNLOCK_SCRATCH(sc, lock);

	if (rv)
		return rv;

	CISS_DPRINTF(CISS_D_MISC, ("lmap %x:%x\n",
	    lmap->map[0].tgt, lmap->map[0].tgt2));

	return 0;
}

int
ciss_sync(struct ciss_softc *sc)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	struct ciss_flush *flush;
	ciss_lock_t lock;
	int rv;

	lock = CISS_LOCK_SCRATCH(sc);
	flush = sc->scratch;
	bzero(flush, sizeof(*flush));
	flush->flush = sc->sc_flush;

	ccb = ciss_get_ccb(sc);
	ccb->ccb_len = sizeof(*flush);
	ccb->ccb_data = flush;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = CISS_CMD_MODE_PERIPH;
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_OUT;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_SET;
	cmd->cdb[6] = CISS_CMS_CTRL_FLUSH;
	cmd->cdb[7] = sizeof(*flush) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*flush) & 0xff;

	rv = ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL|SCSI_NOSLEEP);
	CISS_UNLOCK_SCRATCH(sc, lock);

	return rv;
}

int
ciss_scsi_raw_cmd(struct scsi_xfer *xs)	/* TODO */
{
	struct scsi_link *link = xs->sc_link;
	struct ciss_rawsoftc *rsc = link->adapter_softc;
	struct ciss_softc *sc = rsc->sc_softc;
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	ciss_lock_t lock;
	int error;

	CISS_DPRINTF(CISS_D_CMD, ("ciss_scsi_raw_cmd "));

	if (xs->cmdlen > CISS_MAX_CDB) {
		CISS_DPRINTF(CISS_D_CMD, ("CDB too big %p ", xs));
		bzero(&xs->sense, sizeof(xs->sense));
		xs->sense.error_code = SSD_ERRCODE_VALID | 0x70;
		xs->sense.flags = SKEY_ILLEGAL_REQUEST;
		xs->sense.add_sense_code = 0x20; /* illcmd, 0x24 illfield */
		xs->error = XS_SENSE;
		scsi_done(xs);
		return (COMPLETE);
	}

	lock = CISS_LOCK(sc);
	error = 0;
	xs->error = XS_NOERROR;

	/* TODO check this target has not yet employed w/ any volume */

	ccb = ciss_get_ccb(sc);
	cmd = &ccb->ccb_cmd;
	ccb->ccb_len = xs->datalen;
	ccb->ccb_data = xs->data;
	ccb->ccb_xs = xs;



	cmd->cdblen = xs->cmdlen;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL;
	if (xs->flags & SCSI_DATA_IN)
		cmd->flags |= CISS_CDB_IN;
	else if (xs->flags & SCSI_DATA_OUT)
		cmd->flags |= CISS_CDB_OUT;
	cmd->tmo = htole16(xs->timeout < 1000? 1 : xs->timeout / 1000);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	bcopy(xs->cmd, &cmd->cdb[0], CISS_MAX_CDB);

	if (ciss_cmd(ccb, BUS_DMA_WAITOK,
	    xs->flags & (SCSI_POLL|SCSI_NOSLEEP))) {
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		CISS_UNLOCK(sc, lock);
		return (COMPLETE);
	}

	CISS_UNLOCK(sc, lock);
	return xs->flags & SCSI_POLL? COMPLETE : SUCCESSFULLY_QUEUED;
}

int
ciss_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct ciss_softc *sc = link->adapter_softc;
	u_int8_t target = link->target;
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	int error;
	ciss_lock_t lock;

	CISS_DPRINTF(CISS_D_CMD, ("ciss_scsi_cmd "));

	if (xs->cmdlen > CISS_MAX_CDB) {
		CISS_DPRINTF(CISS_D_CMD, ("CDB too big %p ", xs));
		bzero(&xs->sense, sizeof(xs->sense));
		xs->sense.error_code = SSD_ERRCODE_VALID | 0x70;
		xs->sense.flags = SKEY_ILLEGAL_REQUEST;
		xs->sense.add_sense_code = 0x20; /* illcmd, 0x24 illfield */
		xs->error = XS_SENSE;
		scsi_done(xs);
		return (COMPLETE);
	}

	lock = CISS_LOCK(sc);
	error = 0;
	xs->error = XS_NOERROR;

	/* XXX emulate SYNCHRONIZE_CACHE ??? */

	ccb = ciss_get_ccb(sc);
	cmd = &ccb->ccb_cmd;
	ccb->ccb_len = xs->datalen;
	ccb->ccb_data = xs->data;
	ccb->ccb_xs = xs;
	cmd->tgt = CISS_CMD_MODE_LD | target;
	cmd->tgt2 = 0;
	cmd->cdblen = xs->cmdlen;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL;
	if (xs->flags & SCSI_DATA_IN)
		cmd->flags |= CISS_CDB_IN;
	else if (xs->flags & SCSI_DATA_OUT)
		cmd->flags |= CISS_CDB_OUT;
	cmd->tmo = htole16(xs->timeout < 1000? 1 : xs->timeout / 1000);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	bcopy(xs->cmd, &cmd->cdb[0], CISS_MAX_CDB);

	if (ciss_cmd(ccb, BUS_DMA_WAITOK,
	    xs->flags & (SCSI_POLL|SCSI_NOSLEEP))) {
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		CISS_UNLOCK(sc, lock);
		return (COMPLETE);
	}

	CISS_UNLOCK(sc, lock);
	return xs->flags & SCSI_POLL? COMPLETE : SUCCESSFULLY_QUEUED;
}

int
ciss_intr(void *v)
{
	struct ciss_softc *sc = v;
	struct ciss_ccb *ccb;
	ciss_lock_t lock;
	u_int32_t id;
	int hit = 0;

	CISS_DPRINTF(CISS_D_INTR, ("intr "));

	if (!(bus_space_read_4(sc->iot, sc->ioh, CISS_ISR) & sc->iem))
		return 0;

	lock = CISS_LOCK(sc);
	while ((id = bus_space_read_4(sc->iot, sc->ioh, CISS_OUTQ)) !=
	    0xffffffff) {

		ccb = sc->ccbs + (id >> 2) * sc->ccblen;
		ccb->ccb_cmd.id = htole32(id);
		if (ccb->ccb_state == CISS_CCB_POLL) {
			ccb->ccb_state = CISS_CCB_ONQ;
			wakeup(ccb);
		} else
			ciss_done(ccb);

		hit = 1;
	}
	CISS_UNLOCK(sc, lock);

	CISS_DPRINTF(CISS_D_INTR, ("exit "));
	return hit;
}

void
ciss_heartbeat(void *v)
{
	struct ciss_softc *sc = v;
	u_int32_t hb;

	hb = bus_space_read_4(sc->iot, sc->cfg_ioh,
	    sc->cfgoff + offsetof(struct ciss_config, heartbeat));
	if (hb == sc->heartbeat)
		panic("%s: dead", sc->sc_dev.dv_xname);	/* XXX reset! */
	else
		sc->heartbeat = hb;

	timeout_add(&sc->sc_hb, hz * 3);
}

void
ciss_kthread(void *v)
{
	struct ciss_softc *sc = v;
	ciss_lock_t lock;

	for (;;) {
		tsleep(sc, PRIBIO, sc->sc_dev.dv_xname, 0);

		lock = CISS_LOCK(sc);



		CISS_UNLOCK(sc, lock);
	}
}

int
ciss_scsi_ioctl(struct scsi_link *link, u_long cmd,
    caddr_t addr, int flag, struct proc *p)
{
#if NBIO > 0
	return ciss_ioctl(link->adapter_softc, cmd, addr);
#else
	return ENOTTY;
#endif
}

#if NBIO > 0
const int ciss_level[] = { 0, 4, 1, 5, 51, 7 };
const int ciss_stat[] = { BIOC_SVONLINE, BIOC_SVOFFLINE, BIOC_SVOFFLINE,
    BIOC_SVDEGRADED, BIOC_SVREBUILD, BIOC_SVREBUILD, BIOC_SVDEGRADED,
    BIOC_SVDEGRADED, BIOC_SVINVALID, BIOC_SVINVALID, BIOC_SVBUILDING,
    BIOC_SVOFFLINE, BIOC_SVBUILDING };

int
ciss_ioctl(struct device *dev, u_long cmd, caddr_t addr)
{
	struct ciss_softc *sc = (struct ciss_softc *)dev;
	struct bioc_inq *bi;
	struct bioc_vol *bv;
	struct bioc_disk *bd;
	struct bioc_blink *bb;
	/* struct bioc_alarm *ba; */
	/* struct bioc_setstate *bss; */
	struct ciss_ldid *ldid;
	struct ciss_ldstat *ldstat;
	struct ciss_pdid *pdid;
	struct ciss_blink *blink;
	struct ciss_ld *ldp;
	ciss_lock_t lock;
	int ld, pd, error = 0;
	u_int blks;

	if (!(sc->sc_flags & CISS_BIO))
		return ENOTTY;

	lock = CISS_LOCK(sc);
	switch (cmd) {
	case BIOCINQ:
		bi = (struct bioc_inq *)addr;
		strlcpy(bi->bi_dev, sc->sc_dev.dv_xname, sizeof(bi->bi_dev));
		bi->bi_novol = sc->maxunits;
		bi->bi_nodisk = sc->ndrives;
		break;

	case BIOCVOL:
		bv = (struct bioc_vol *)addr;
		if (bv->bv_volid > sc->maxunits) {
			error = EINVAL;
			break;
		}
		ldp = sc->sc_lds[bv->bv_volid];
		if (!ldp)
			return EINVAL;
		ldid = sc->scratch;
		if ((error = ciss_ldid(sc, bv->bv_volid, ldid)))
			break;
		/* params 30:88:ff:00:00:00:00:00:00:00:00:00:00:00:20:00 */
		bv->bv_status = BIOC_SVINVALID;
		blks = (u_int)letoh16(ldid->nblocks[1]) << 16 |
		    letoh16(ldid->nblocks[0]);
		bv->bv_size = blks * (u_quad_t)letoh16(ldid->blksize);
		bv->bv_level = ciss_level[ldid->type];
		bv->bv_nodisk = ldp->ndrives;
		strlcpy(bv->bv_dev, ldp->xname, sizeof(bv->bv_dev));
		strlcpy(bv->bv_vendor, "CISS", sizeof(bv->bv_vendor));
		ldstat = sc->scratch;
		bzero(ldstat, sizeof(*ldstat));
		if ((error = ciss_ldstat(sc, bv->bv_volid, ldstat)))
			break;
		bv->bv_percent = -1;
		bv->bv_seconds = 0;
		if (ldstat->stat < sizeof(ciss_stat)/sizeof(ciss_stat[0]))
			bv->bv_status = ciss_stat[ldstat->stat];
		if (bv->bv_status == BIOC_SVREBUILD ||
		    bv->bv_status == BIOC_SVBUILDING)
			bv->bv_percent = (blks -
			    (((u_int)ldstat->prog[3] << 24) |
			    ((u_int)ldstat->prog[2] << 16) |
			    ((u_int)ldstat->prog[1] << 8) |
			    (u_int)ldstat->prog[0])) * 100ULL / blks;
		break;

	case BIOCDISK:
		bd = (struct bioc_disk *)addr;
		if (bd->bd_volid > sc->maxunits) {
			error = EINVAL;
			break;
		}
		ldp = sc->sc_lds[bd->bd_volid];
		if (!ldp || (pd = bd->bd_diskid) > ldp->ndrives) {
			error = EINVAL;
			break;
		}
		ldstat = sc->scratch;
		if ((error = ciss_ldstat(sc, bd->bd_volid, ldstat)))
			break;
		bd->bd_status = -1;
		if (ldstat->bigrebuild == ldp->tgts[pd])
			bd->bd_status = BIOC_SDREBUILD;
		if (ciss_bitset(ldp->tgts[pd] & (~CISS_BIGBIT),
		    ldstat->bigfailed)) {
			bd->bd_status = BIOC_SDFAILED;
			bd->bd_size = 0;
			bd->bd_channel = (ldp->tgts[pd] & (~CISS_BIGBIT)) /
			    sc->ndrives;
			bd->bd_target = ldp->tgts[pd] % sc->ndrives;
			bd->bd_lun = 0;
			bd->bd_vendor[0] = '\0';
			bd->bd_serial[0] = '\0';
			bd->bd_procdev[0] = '\0';
		} else {
			pdid = sc->scratch;
			if ((error = ciss_pdid(sc, ldp->tgts[pd], pdid,
			    SCSI_POLL)))
				break;
			if (bd->bd_status < 0) {
				if (pdid->config & CISS_PD_SPARE)
					bd->bd_status = BIOC_SDHOTSPARE;
				else if (pdid->present & CISS_PD_PRESENT)
					bd->bd_status = BIOC_SDONLINE;
				else
					bd->bd_status = BIOC_SDINVALID;
			}
			bd->bd_size = (u_int64_t)letoh32(pdid->nblocks) *
			    letoh16(pdid->blksz);
			bd->bd_channel = pdid->bus;  
			bd->bd_target = pdid->target;
			bd->bd_lun = 0;
			strlcpy(bd->bd_vendor, pdid->model,
			    sizeof(bd->bd_vendor));
			strlcpy(bd->bd_serial, pdid->serial,
			    sizeof(bd->bd_serial));
			bd->bd_procdev[0] = '\0';
		}
		break;

	case BIOCBLINK:
		bb = (struct bioc_blink *)addr;
		blink = sc->scratch;
		error = EINVAL;
		/* XXX workaround completely dumb scsi addressing */
		for (ld = 0; ld < sc->maxunits; ld++) {
			ldp = sc->sc_lds[ld];
			if (!ldp)
				continue;
			for (pd = 0; pd < ldp->ndrives; pd++)
				if (ldp->tgts[pd] == (CISS_BIGBIT +
				    bb->bb_channel * sc->ndrives +
				    bb->bb_target))
					error = ciss_blink(sc, ld, pd,
					    bb->bb_status, blink);
		}
		break;

	case BIOCALARM:
	case BIOCSETSTATE:
	default:
		CISS_DPRINTF(CISS_D_IOCTL, ("%s: invalid ioctl\n",
		    sc->sc_dev.dv_xname));
		error = ENOTTY;
	}
	CISS_UNLOCK(sc, lock);

	return error;
}

#ifndef SMALL_KERNEL
void
ciss_sensors(void *v)
{
	struct ciss_softc *sc = v;
	struct ciss_ldstat *ldstat;
	int i, error;

	for (i = 0; i < sc->maxunits; i++) {
		ldstat = sc->scratch;
		if ((error = ciss_ldstat(sc, i, ldstat))) {
			sc->sensors[i].value = 0;
			sc->sensors[i].status = SENSOR_S_UNKNOWN;
			continue;
		}

		switch (ldstat->stat) {
		case CISS_LD_OK:
			sc->sensors[i].value = SENSOR_DRIVE_ONLINE;
			sc->sensors[i].status = SENSOR_S_OK;
			break;

		case CISS_LD_DEGRAD:
			sc->sensors[i].value = SENSOR_DRIVE_PFAIL;
			sc->sensors[i].status = SENSOR_S_WARN;
			break;

		case CISS_LD_EXPND:
		case CISS_LD_QEXPND:
		case CISS_LD_RBLDRD:
		case CISS_LD_REBLD:
			sc->sensors[i].value = SENSOR_DRIVE_REBUILD;
			sc->sensors[i].status = SENSOR_S_WARN;
			break;

		case CISS_LD_NORDY:
		case CISS_LD_PDINV:
		case CISS_LD_PDUNC:
		case CISS_LD_FAILED:
		case CISS_LD_UNCONF:
			sc->sensors[i].value = SENSOR_DRIVE_FAIL;
			sc->sensors[i].status = SENSOR_S_CRIT;
			break;

		default:
			sc->sensors[i].value = 0;
			sc->sensors[i].status = SENSOR_S_UNKNOWN;
		}
	}
}
#endif /* SMALL_KERNEL */

int
ciss_ldid(struct ciss_softc *sc, int target, struct ciss_ldid *id)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;

	ccb = ciss_get_ccb(sc);
	if (ccb == NULL)
		return ENOMEM;
	ccb->ccb_len = sizeof(*id);
	ccb->ccb_data = id;
	ccb->ccb_xs = NULL;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = htole32(CISS_CMD_MODE_PERIPH);
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_IN;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_GET;
	cmd->cdb[5] = target;
	cmd->cdb[6] = CISS_CMS_CTRL_LDIDEXT;
	cmd->cdb[7] = sizeof(*id) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*id) & 0xff;

	return ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL);
}

int
ciss_ldstat(struct ciss_softc *sc, int target, struct ciss_ldstat *stat)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;

	ccb = ciss_get_ccb(sc);
	if (ccb == NULL)
		return ENOMEM;
	ccb->ccb_len = sizeof(*stat);
	ccb->ccb_data = stat;
	ccb->ccb_xs = NULL;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = htole32(CISS_CMD_MODE_PERIPH);
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_IN;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_GET;
	cmd->cdb[5] = target;
	cmd->cdb[6] = CISS_CMS_CTRL_LDSTAT;
	cmd->cdb[7] = sizeof(*stat) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*stat) & 0xff;

	return ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL);
}

int
ciss_pdid(struct ciss_softc *sc, u_int8_t drv, struct ciss_pdid *id, int wait)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;

	ccb = ciss_get_ccb(sc);
	if (ccb == NULL)
		return ENOMEM;
	ccb->ccb_len = sizeof(*id);
	ccb->ccb_data = id;
	ccb->ccb_xs = NULL;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = htole32(CISS_CMD_MODE_PERIPH);
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_IN;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_GET;
	cmd->cdb[2] = drv;
	cmd->cdb[6] = CISS_CMS_CTRL_PDID;
	cmd->cdb[7] = sizeof(*id) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*id) & 0xff;

	return ciss_cmd(ccb, BUS_DMA_NOWAIT, wait);
}


struct ciss_ld *
ciss_pdscan(struct ciss_softc *sc, int ld)
{
	struct ciss_pdid *pdid;
	struct ciss_ld *ldp;
	u_int8_t drv, buf[128];
	int i, j, k = 0;

	pdid = sc->scratch;
	for (i = 0; i < sc->nbus; i++)
		for (j = 0; j < sc->ndrives; j++) {
			drv = CISS_BIGBIT + i * sc->ndrives + j;
			if (!ciss_pdid(sc, drv, pdid, SCSI_NOSLEEP|SCSI_POLL))
				buf[k++] = drv;
		}

	if (!k)
		return NULL;

	ldp = malloc(sizeof(*ldp) + (k-1), M_DEVBUF, M_NOWAIT);
	if (!ldp)
		return NULL;

	bzero(&ldp->bling, sizeof(ldp->bling));
	ldp->ndrives = k;
	bcopy(buf, ldp->tgts, k);
	return ldp;
}

int
ciss_blink(struct ciss_softc *sc, int ld, int pd, int stat,
    struct ciss_blink *blink)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	struct ciss_ld *ldp;

	if (ld > sc->maxunits)
		return EINVAL;

	ldp = sc->sc_lds[ld];
	if (!ldp || pd > ldp->ndrives)
		return EINVAL;

	ldp->bling.pdtab[ldp->tgts[pd]] = stat == BIOC_SBUNBLINK? 0 :
	    CISS_BLINK_ALL;
	bcopy(&ldp->bling, blink, sizeof(*blink));

	ccb = ciss_get_ccb(sc);
	if (ccb == NULL)
		return ENOMEM;
	ccb->ccb_len = sizeof(*blink);
	ccb->ccb_data = blink;
	ccb->ccb_xs = NULL;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = htole32(CISS_CMD_MODE_PERIPH);
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_OUT;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_SET;
	cmd->cdb[6] = CISS_CMS_CTRL_PDBLINK;
	cmd->cdb[7] = sizeof(*blink) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*blink) & 0xff;

	return ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL);
}
#endif
