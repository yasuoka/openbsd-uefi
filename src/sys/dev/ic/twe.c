/*	$OpenBSD: twe.c,v 1.5 2000/11/08 20:44:56 mickey Exp $	*/

/*
 * Copyright (c) 2000 Michael Shalayeff.  All rights reserved.
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

#undef	TWE_DEBUG

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <uvm/uvm_extern.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/twereg.h>
#include <dev/ic/twevar.h>

#ifdef TWE_DEBUG
#define	TWE_DPRINTF(m,a)	if (twe_debug & (m)) printf a
#define	TWE_D_CMD	0x0001
#define	TWE_D_INTR	0x0002
#define	TWE_D_MISC	0x0004
#define	TWE_D_DMA	0x0008
#define	TWE_D_AEN	0x0010
int twe_debug = 0xffff;
#else
#define	TWE_DPRINTF(m,a)	/* m, a */
#endif

struct cfdriver twe_cd = {
	NULL, "twe", DV_DULL
};

int	twe_scsi_cmd __P((struct scsi_xfer *));

struct scsi_adapter twe_switch = {
	twe_scsi_cmd, tweminphys, 0, 0,
};

struct scsi_device twe_dev = {
	NULL, NULL, NULL, NULL
};

static __inline struct twe_ccb *twe_get_ccb __P((struct twe_softc *sc));
static __inline void twe_put_ccb __P((struct twe_ccb *ccb));
void twe_dispose __P((struct twe_softc *sc));
int  twe_cmd __P((struct twe_ccb *ccb, int flags, int wait));
int  twe_start __P((struct twe_ccb *ccb, int wait));
void twe_exec_cmd __P((void *v));
int  twe_complete __P((struct twe_ccb *ccb));
int  twe_done __P((struct twe_softc *sc, int idx));
void twe_copy_internal_data __P((struct scsi_xfer *xs, void *v, size_t size));


static __inline struct twe_ccb *
twe_get_ccb(sc)
	struct twe_softc *sc;
{
	struct twe_ccb *ccb;

	ccb = TAILQ_LAST(&sc->sc_free_ccb, twe_queue_head);
	if (ccb)
		TAILQ_REMOVE(&sc->sc_free_ccb, ccb, ccb_link);
	return ccb;
}

static __inline void
twe_put_ccb(ccb)
	struct twe_ccb *ccb;
{
	struct twe_softc *sc = ccb->ccb_sc;

	ccb->ccb_state = TWE_CCB_FREE;
	TAILQ_INSERT_TAIL(&sc->sc_free_ccb, ccb, ccb_link);
}

void
twe_dispose(sc)
	struct twe_softc *sc;
{
	register struct twe_ccb *ccb;
	if (sc->sc_cmdmap != NULL)
		bus_dmamap_destroy(sc->dmat, sc->sc_cmdmap);
	/* TODO: traverse the ccbs and destroy the maps */
	for (ccb = &sc->sc_ccbs[TWE_MAXCMDS - 1]; ccb >= sc->sc_ccbs; ccb--)
		if (ccb->ccb_dmamap)
			bus_dmamap_destroy(sc->dmat, ccb->ccb_dmamap);
	uvm_km_free(kmem_map, (vaddr_t)sc->sc_cmds,
	    sizeof(struct twe_cmd) * TWE_MAXCMDS);
}

int
twe_attach(sc)
	struct twe_softc *sc;
{
	/* this includes a buffer for drive config req, and a capacity req */
	u_int8_t	param_buf[2 * TWE_SECTOR_SIZE + TWE_ALIGN - 1];
	struct twe_param *pb = (void *)
	    (((u_long)param_buf + TWE_ALIGN - 1) & ~(TWE_ALIGN - 1));
	struct twe_param *cap = (void *)((u_int8_t *)pb + TWE_SECTOR_SIZE);
	struct twe_ccb	*ccb;
	struct twe_cmd	*cmd;
	u_int32_t	status;
	int		error, i, retry, nunits;
	const char	*errstr;

	TAILQ_INIT(&sc->sc_ccb2q);
	TAILQ_INIT(&sc->sc_ccbq);
	TAILQ_INIT(&sc->sc_free_ccb);
	sc->sc_cmds = (void *)uvm_km_kmemalloc(kmem_map, uvmexp.kmem_object,
	    sizeof(struct twe_cmd) * TWE_MAXCMDS, UVM_KMF_NOWAIT);
	if (sc->sc_cmds == NULL) {
		printf(": cannot allocate commands\n");
		return (1);
	}
	error = bus_dmamap_create(sc->dmat,
	    sizeof(struct twe_cmd) * TWE_MAXCMDS, TWE_MAXCMDS,
	    sizeof(struct twe_cmd) * TWE_MAXCMDS, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &sc->sc_cmdmap);
	if (error) {
		printf(": cannot create ccb cmd dmamap (%d)\n", error);
		twe_dispose(sc);
		return (1);
	}
	error = bus_dmamap_load(sc->dmat, sc->sc_cmdmap, sc->sc_cmds,
	    sizeof(struct twe_cmd) * TWE_MAXCMDS, NULL, BUS_DMA_NOWAIT);
	if (error) {
		printf(": cannot load command dma map (%d)\n", error);
		twe_dispose(sc);
		return (1);
	}
	for (cmd = sc->sc_cmds + sizeof(struct twe_cmd) * (TWE_MAXCMDS - 1);
	     cmd >= (struct twe_cmd *)sc->sc_cmds; cmd--) {

		cmd->cmd_index = cmd - (struct twe_cmd *)sc->sc_cmds;
		ccb = &sc->sc_ccbs[cmd->cmd_index];
		error = bus_dmamap_create(sc->dmat,
		    TWE_MAXFER, TWE_MAXOFFSETS, TWE_MAXFER, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &ccb->ccb_dmamap);
		if (error) {
			printf(": cannot create ccb dmamap (%d)\n", error);
			twe_dispose(sc);
			return (1);
		}
		ccb->ccb_sc = sc;
		ccb->ccb_cmd = cmd;
		ccb->ccb_state = TWE_CCB_FREE;
		ccb->ccb_cmdpa = kvtop((caddr_t)cmd);
		TAILQ_INSERT_TAIL(&sc->sc_free_ccb, ccb, ccb_link);
	}

	for (errstr = NULL, retry = 3; retry--; ) {
		int		veseen_srst;
		u_int16_t	aen;

		if (errstr)
			TWE_DPRINTF(TWE_D_MISC, ("%s ", errstr));

		for (i = 60000; i--; DELAY(100)) {
			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
			if (status & TWE_STAT_CPURDY)
				break;
		}

		if (!(status & TWE_STAT_CPURDY)) {
			errstr = ": card CPU is not ready\n";
			continue;
		}

		/* soft reset, disable ints */
		bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
		    TWE_CTRL_SRST |
		    TWE_CTRL_CHOSTI | TWE_CTRL_CATTNI | TWE_CTRL_CERR |
		    TWE_CTRL_MCMDI | TWE_CTRL_MRDYI |
		    TWE_CTRL_MINT);

		for (i = 45000; i--; DELAY(100)) {
			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
			if (status & TWE_STAT_ATTNI)
				break;
		}

		if (!(status & TWE_STAT_ATTNI)) {
			errstr = ": cannot get card's attention\n";
			continue;
		}

		/* drain aen queue */
		for (veseen_srst = 0, aen = -1; aen != TWE_AEN_QEMPTY; ) {

			if ((ccb = twe_get_ccb(sc)) == NULL) {
				errstr = ": out of ccbs\n";
				continue;
			}

			ccb->ccb_xs = NULL;
			ccb->ccb_data = pb;
			ccb->ccb_length = TWE_SECTOR_SIZE;
			ccb->ccb_state = TWE_CCB_READY;
			cmd = ccb->ccb_cmd;
			cmd->cmd_unit_host = TWE_UNITHOST(0, 0);
			cmd->cmd_op = TWE_CMD_GPARAM;
			cmd->cmd_count = 1;

			pb->table_id = TWE_PARAM_AEN;
			pb->param_id = 2;
			pb->param_size = 2;

			if (twe_cmd(ccb, BUS_DMA_NOWAIT, 1)) {
				errstr = ": error draining attention queue\n";
				break;
			}
			aen = *(u_int16_t *)pb->data;
			TWE_DPRINTF(TWE_D_AEN, ("aen=%x ", aen));
			if (aen == TWE_AEN_SRST)
				veseen_srst++;
		}

		if (!veseen_srst) {
			errstr = ": we don't get it\n";
			continue;
		}

		if (status & TWE_STAT_CPUERR) {
			errstr = ": card CPU error detected\n";
			continue;
		}

		if (status & TWE_STAT_PCIPAR) {
			errstr = ": PCI parity error detected\n";
			continue;
		}

		if (status & TWE_STAT_QUEUEE ) {
			errstr = ": queuing error detected\n";
			continue;
		}

		if (status & TWE_STAT_PCIABR) {
			errstr = ": PCI abort\n";
			continue;
		}

		while (!(status & TWE_STAT_RQE)) {
			bus_space_read_4(sc->iot, sc->ioh, TWE_READYQUEUE);
			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
		}

		break;
	}

	if (retry < 0) {
		printf(errstr);
		twe_dispose(sc);
		return 1;
	}

	if ((ccb = twe_get_ccb(sc)) == NULL) {
		printf(": out of ccbs\n");
		twe_dispose(sc);
		return 1;
	}

	ccb->ccb_xs = NULL;
	ccb->ccb_data = pb;
	ccb->ccb_length = TWE_SECTOR_SIZE;
	ccb->ccb_state = TWE_CCB_READY;
	cmd = ccb->ccb_cmd;
	cmd->cmd_unit_host = TWE_UNITHOST(0, 0);
	cmd->cmd_op = TWE_CMD_GPARAM;
	cmd->cmd_count = 1;

	pb->table_id = TWE_PARAM_UC;
	pb->param_id = TWE_PARAM_UC;
	pb->param_size = TWE_MAX_UNITS;
	if (twe_cmd(ccb, BUS_DMA_NOWAIT, 1)) {
		printf(": failed to fetch unit parameters\n");
		twe_dispose(sc);
		return 1;
	}

	/* we are assuming last read status was good */
	printf(": Escalade V%d.%d\n", TWE_MAJV(status), TWE_MINV(status));

	for (nunits = i = 0; i < TWE_MAX_UNITS; i++) {
		if (pb->data[i] == 0)
			continue;

		if ((ccb = twe_get_ccb(sc)) == NULL) {
			printf(": out of ccbs\n");
			twe_dispose(sc);
			return 1;
		}

		ccb->ccb_xs = NULL;
		ccb->ccb_data = cap;
		ccb->ccb_length = TWE_SECTOR_SIZE;
		ccb->ccb_state = TWE_CCB_READY;
		cmd = ccb->ccb_cmd;
		cmd->cmd_unit_host = TWE_UNITHOST(0, 0);
		cmd->cmd_op = TWE_CMD_GPARAM;
		cmd->cmd_count = 1;

		cap->table_id = TWE_PARAM_UI + i;
		cap->param_id = 4;
		cap->param_size = 4;	/* 4 bytes */
		if (twe_cmd(ccb, BUS_DMA_NOWAIT, 1)) {
			printf("%s: error fetching capacity for unit %d\n",
			    sc->sc_dev.dv_xname, i);
			continue;
		}

		nunits++;
		sc->sc_hdr[i].hd_present = 1;
		sc->sc_hdr[i].hd_devtype = 0;
		sc->sc_hdr[i].hd_size = letoh32(*(u_int32_t *)cap->data);
		/* this is evil. they never learn */
		if (sc->sc_hdr[i].hd_size > 0x200000) {
			sc->sc_hdr[i].hd_secs = 63;
			sc->sc_hdr[i].hd_heads = 255;
		} else {
			sc->sc_hdr[i].hd_secs = 32;
			sc->sc_hdr[i].hd_heads = 64;
		}
		TWE_DPRINTF(TWE_D_MISC, ("twed%d: size=%d secs=%d heads=%d\n",
		    i, sc->sc_hdr[i].hd_size, sc->sc_hdr[i].hd_secs,
		    sc->sc_hdr[i].hd_heads));
	}

	if (!nunits)
		nunits++;

	/* TODO: fetch & print cache params? */

	sc->sc_link.adapter_softc = sc;
	sc->sc_link.adapter = &twe_switch;
	sc->sc_link.adapter_target = TWE_MAX_UNITS;
	sc->sc_link.device = &twe_dev;
	sc->sc_link.openings = TWE_MAXCMDS / nunits;
	sc->sc_link.adapter_buswidth = TWE_MAX_UNITS;

	config_found(&sc->sc_dev, &sc->sc_link, scsiprint);

	/* enable interrupts */
	bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL, TWE_CTRL_EINT |
	    /*TWE_CTRL_HOSTI |*/ TWE_CTRL_CATTNI | TWE_CTRL_ERDYI);

	return 0;
}

int
twe_cmd(ccb, flags, wait)
	struct twe_ccb *ccb;
	int flags, wait;
{
	struct twe_softc *sc = ccb->ccb_sc;
	bus_dmamap_t dmap;
	struct twe_cmd *cmd;
	struct twe_segs *sgp;
	int error, i;

	if (ccb->ccb_data && ((u_long)ccb->ccb_data & (TWE_ALIGN - 1))) {
		TWE_DPRINTF(TWE_D_DMA, ("data=%p is unaligned ",ccb->ccb_data));
		ccb->ccb_realdata = ccb->ccb_data;
		ccb->ccb_data = (void *)uvm_km_kmemalloc(kmem_map,
		    uvmexp.kmem_object, ccb->ccb_length, UVM_KMF_NOWAIT);
		if (!ccb->ccb_data) {
			TWE_DPRINTF(TWE_D_DMA, ("2buf alloc failed "));
			twe_put_ccb(ccb);
			return (ENOMEM);
		}
		bcopy(ccb->ccb_realdata, ccb->ccb_data, ccb->ccb_length);
	} else
		ccb->ccb_realdata = NULL;

	dmap = ccb->ccb_dmamap;
	cmd = ccb->ccb_cmd;
	cmd->cmd_status = 0;

	if (ccb->ccb_data) {
		error = bus_dmamap_load(sc->dmat, dmap, ccb->ccb_data,
		    ccb->ccb_length, NULL, flags);
		if (error) {
			if (error == EFBIG)
				printf("more than %d dma segs\n", TWE_MAXOFFSETS);
			else
				printf("error %d loading dma map\n", error);

			twe_put_ccb(ccb);
			return error;
		}
		/* load addresses into command */
		switch (cmd->cmd_op) {
		case TWE_CMD_GPARAM:
		case TWE_CMD_SPARAM:
			sgp = cmd->cmd_param.segs;
			break;
		case TWE_CMD_READ:
		case TWE_CMD_WRITE:
			sgp = cmd->cmd_io.segs;
			break;
		default:
			/* no data transfer */
			TWE_DPRINTF(TWE_D_DMA, ("twe_cmd: unknown sgp op=%x\n",
			    cmd->cmd_op));
			sgp = NULL;
			break;
		}
		TWE_DPRINTF(TWE_D_DMA, ("data=%p<", ccb->ccb_data));
		if (sgp) {
			/*
			 * we know that size is in the upper byte,
			 * and we do not worry about overflow
			 */
			cmd->cmd_op += (2 * dmap->dm_nsegs) << 8;
			bzero (sgp, TWE_MAXOFFSETS * sizeof(*sgp));
			for (i = 0; i < dmap->dm_nsegs; i++, sgp++) {
				sgp->twes_addr = htole32(dmap->dm_segs[i].ds_addr);
				sgp->twes_len  = htole32(dmap->dm_segs[i].ds_len);
				TWE_DPRINTF(TWE_D_DMA, ("%x[%x] ",
				    dmap->dm_segs[i].ds_addr,
				    dmap->dm_segs[i].ds_len));
			}
		}
		TWE_DPRINTF(TWE_D_DMA, ("> "));
		bus_dmamap_sync(sc->dmat, dmap, BUS_DMASYNC_PREWRITE);
	}
	bus_dmamap_sync(sc->dmat, sc->sc_cmdmap, BUS_DMASYNC_PREWRITE);

	if ((error = twe_start(ccb, wait))) {
		bus_dmamap_unload(sc->dmat, dmap);
		twe_put_ccb(ccb);
		return error;
	}

	return wait? twe_complete(ccb) : 0;
}

int
twe_start(ccb, wait)
	struct twe_ccb *ccb;
	int wait;
{
	struct twe_softc*sc = ccb->ccb_sc;
	struct twe_cmd	*cmd = ccb->ccb_cmd;
	u_int32_t	status;
	int i;

	cmd->cmd_op = htole16(cmd->cmd_op);
	cmd->cmd_count = htole16(cmd->cmd_count);

	if (!wait) {

		TWE_DPRINTF(TWE_D_CMD, ("prequeue(%d) ", cmd->cmd_index));
		ccb->ccb_state = TWE_CCB_PREQUEUED;
		TAILQ_INSERT_TAIL(&sc->sc_ccb2q, ccb, ccb_link);
		bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
		    TWE_CTRL_ECMDI);
		return 0;
	}

	for (i = 1000; i--; DELAY(10)) {

		status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
		if (!(status & TWE_STAT_CQF))
			break;
		TWE_DPRINTF(TWE_D_CMD,  ("twe_start stat=%b ",
		    status & TWE_STAT_FLAGS, TWE_STAT_BITS));
	}

	if (!(status & TWE_STAT_CQF)) {
		bus_space_write_4(sc->iot, sc->ioh, TWE_COMMANDQUEUE,
		    ccb->ccb_cmdpa);

		TWE_DPRINTF(TWE_D_CMD, ("queue(%d) ", cmd->cmd_index));
		ccb->ccb_state = TWE_CCB_QUEUED;
		TAILQ_INSERT_TAIL(&sc->sc_ccbq, ccb, ccb_link);
		return 0;

	} else {

		printf("%s: twe_start(%d) timed out\n",
		    sc->sc_dev.dv_xname, cmd->cmd_index);

		return 1;
	}
}

int
twe_complete(ccb)
	struct twe_ccb *ccb;
{
	struct twe_softc *sc = ccb->ccb_sc;
	u_int32_t	status;
	int i;

	for (i = 100000; i--; DELAY(10)) {
		status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
		/* TWE_DPRINTF(TWE_D_CMD,  ("twe_intr stat=%b ",
		    status & TWE_STAT_FLAGS, TWE_STAT_BITS)); */

		while (!(status & TWE_STAT_RQE)) {
			u_int32_t ready;

			ready = bus_space_read_4(sc->iot, sc->ioh,
			    TWE_READYQUEUE);

			TWE_DPRINTF(TWE_D_CMD, ("ready=%x ", ready));

			if (!twe_done(sc, TWE_READYID(ready)) &&
			    ccb->ccb_state == TWE_CCB_FREE) {
				TWE_DPRINTF(TWE_D_CMD, ("complete\n"));
				return 0;
			}

			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
			/* TWE_DPRINTF(TWE_D_CMD,  ("twe_intr stat=%b ",
			    status & TWE_STAT_FLAGS, TWE_STAT_BITS)); */
		}
	}

	return 1;
}

int
twe_done(sc, idx)
	struct twe_softc *sc;
	int	idx;
{
	struct scsi_xfer *xs;
	struct twe_ccb *ccb = &sc->sc_ccbs[idx];
	struct twe_cmd *cmd = ccb->ccb_cmd;

	TWE_DPRINTF(TWE_D_CMD, ("done(%d) ", idx));

	xs = ccb->ccb_xs;

	if (ccb->ccb_state != TWE_CCB_QUEUED) {
		printf("%s: unqueued ccb %d ready\n",
		    sc->sc_dev.dv_xname, idx);
		return 1;
	}

	if (xs) {
		if (xs->cmd->opcode != PREVENT_ALLOW &&
		    xs->cmd->opcode != SYNCHRONIZE_CACHE) {
			bus_dmamap_sync(sc->dmat, ccb->ccb_dmamap,
			    (xs->flags & SCSI_DATA_IN) ?
			    BUS_DMASYNC_POSTREAD :
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->dmat, ccb->ccb_dmamap);
		}
	} else {
		switch (cmd->cmd_op) {
		case TWE_CMD_GPARAM:
		case TWE_CMD_READ:
			bus_dmamap_sync(sc->dmat, ccb->ccb_dmamap,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->dmat, ccb->ccb_dmamap);
			break;
		case TWE_CMD_SPARAM:
		case TWE_CMD_WRITE:
			bus_dmamap_sync(sc->dmat, ccb->ccb_dmamap,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->dmat, ccb->ccb_dmamap);
			break;
		default:
			/* no data */
		}
	}

	if (ccb->ccb_realdata) {
		bcopy(ccb->ccb_data, ccb->ccb_realdata, ccb->ccb_length);
		uvm_km_free(kmem_map, (vaddr_t)ccb->ccb_data, ccb->ccb_length);
		ccb->ccb_data = ccb->ccb_realdata;
		ccb->ccb_realdata = NULL;
	}

	TAILQ_REMOVE(&sc->sc_ccbq, ccb, ccb_link);
	twe_put_ccb(ccb);

	if (xs) {
		xs->resid = 0;
		xs->flags |= ITSDONE;
		scsi_done(xs);
	}

	return 0;
}
void
tweminphys(bp)
	struct buf *bp;
{
	if (bp->b_bcount > TWE_MAXFER)
		bp->b_bcount = TWE_MAXFER;
	minphys(bp);
}

void
twe_copy_internal_data(xs, v, size)
	struct scsi_xfer *xs;
	void *v;
	size_t size;
{
	size_t copy_cnt;

	TWE_DPRINTF(TWE_D_MISC, ("twe_copy_internal_data "));

	if (!xs->datalen)
		printf("uio move not yet supported\n");
	else {
		copy_cnt = MIN(size, xs->datalen);
		bcopy(v, xs->data, copy_cnt);
	}
}

int
twe_scsi_cmd(xs)
	struct scsi_xfer *xs;
{
	struct scsi_link *link = xs->sc_link;
	struct twe_softc *sc = link->adapter_softc;
	struct twe_ccb *ccb;
	struct twe_cmd *cmd;
	struct scsi_inquiry_data inq;
	struct scsi_sense_data sd;
	struct {
		struct scsi_mode_header hd;
		struct scsi_blk_desc bd;
		union scsi_disk_pages dp;
	} mpd;
	struct scsi_read_cap_data rcd;
	u_int8_t target = link->target;
	u_int32_t blockno, blockcnt;
	struct scsi_rw *rw;
	struct scsi_rw_big *rwb;
	int error, op;
	twe_lock_t lock;


	if (target >= TWE_MAX_UNITS || !sc->sc_hdr[target].hd_present ||
	    link->lun != 0) {
		xs->error = XS_DRIVER_STUFFUP;
		return (COMPLETE);
	}

	TWE_DPRINTF(TWE_D_CMD, ("twe_scsi_cmd "));

	xs->error = XS_NOERROR;

	switch (xs->cmd->opcode) {
	case TEST_UNIT_READY:
	case START_STOP:
#if 0
	case VERIFY:
#endif
		TWE_DPRINTF(TWE_D_CMD, ("opc %d tgt %d ", xs->cmd->opcode,
		    target));
		break;

	case REQUEST_SENSE:
		TWE_DPRINTF(TWE_D_CMD, ("REQUEST SENSE tgt %d ", target));
		bzero(&sd, sizeof sd);
		sd.error_code = 0x70;
		sd.segment = 0;
		sd.flags = SKEY_NO_SENSE;
		*(u_int32_t*)sd.info = htole32(0);
		sd.extra_len = 0;
		twe_copy_internal_data(xs, &sd, sizeof sd);
		break;

	case INQUIRY:
		TWE_DPRINTF(TWE_D_CMD, ("INQUIRY tgt %d devtype %x ", target,
		    sc->sc_hdr[target].hd_devtype));
		bzero(&inq, sizeof inq);
		inq.device =
		    (sc->sc_hdr[target].hd_devtype & 4) ? T_CDROM : T_DIRECT;
		inq.dev_qual2 =
		    (sc->sc_hdr[target].hd_devtype & 1) ? SID_REMOVABLE : 0;
		inq.version = 2;
		inq.response_format = 2;
		inq.additional_length = 32;
		strcpy(inq.vendor, "3WARE  ");
		sprintf(inq.product, "Host drive  #%02d", target);
		strcpy(inq.revision, "   ");
		twe_copy_internal_data(xs, &inq, sizeof inq);
		break;

	case MODE_SENSE:
		TWE_DPRINTF(TWE_D_CMD, ("MODE SENSE tgt %d ", target));

		bzero(&mpd, sizeof mpd);
		switch (((struct scsi_mode_sense *)xs->cmd)->page) {
		case 4:
			/* scsi_disk.h says this should be 0x16 */
			mpd.dp.rigid_geometry.pg_length = 0x16;
			mpd.hd.data_length = sizeof mpd.hd + sizeof mpd.bd +
			    mpd.dp.rigid_geometry.pg_length;
			mpd.hd.blk_desc_len = sizeof mpd.bd;

			/* XXX */
			mpd.hd.dev_spec =
			    (sc->sc_hdr[target].hd_devtype & 2) ? 0x80 : 0;
			_lto3b(TWE_SECTOR_SIZE, mpd.bd.blklen);
			mpd.dp.rigid_geometry.pg_code = 4;
			_lto3b(sc->sc_hdr[target].hd_size /
			    sc->sc_hdr[target].hd_heads /
			    sc->sc_hdr[target].hd_secs,
			    mpd.dp.rigid_geometry.ncyl);
			mpd.dp.rigid_geometry.nheads =
			    sc->sc_hdr[target].hd_heads;
			twe_copy_internal_data(xs, (u_int8_t *)&mpd,
			    sizeof mpd);
			break;

		default:
			printf("%s: mode sense page %d not simulated\n",
			    sc->sc_dev.dv_xname,
			    ((struct scsi_mode_sense *)xs->cmd)->page);
			xs->error = XS_DRIVER_STUFFUP;
			return (TRY_AGAIN_LATER);
		}
		break;

	case READ_CAPACITY:
		TWE_DPRINTF(TWE_D_CMD, ("READ CAPACITY tgt %d ", target));
		bzero(&rcd, sizeof rcd);
		_lto4b(sc->sc_hdr[target].hd_size - 1, rcd.addr);
		_lto4b(TWE_SECTOR_SIZE, rcd.length);
		twe_copy_internal_data(xs, &rcd, sizeof rcd);
		break;

	case PREVENT_ALLOW:
		TWE_DPRINTF(TWE_D_CMD, ("PREVENT/ALLOW "));
		return (COMPLETE);

	case READ_COMMAND:
	case READ_BIG:
	case WRITE_COMMAND:
	case WRITE_BIG:
	case SYNCHRONIZE_CACHE:
		lock = TWE_LOCK_TWE(sc);

		if (xs->cmd->opcode != SYNCHRONIZE_CACHE) {
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
			if (blockno >= sc->sc_hdr[target].hd_size ||
			    blockno + blockcnt > sc->sc_hdr[target].hd_size) {
				TWE_UNLOCK_TWE(sc, lock);
				printf("%s: out of bounds %u-%u >= %u\n",
				    sc->sc_dev.dv_xname, blockno, blockcnt,
				    sc->sc_hdr[target].hd_size);
				scsi_done(xs);
				xs->error = XS_DRIVER_STUFFUP;
				return (COMPLETE);
			}
		}

		switch (xs->cmd->opcode) {
		case READ_COMMAND:	op = TWE_CMD_READ;	break;
		case READ_BIG:		op = TWE_CMD_READ;	break;
		case WRITE_COMMAND:	op = TWE_CMD_WRITE;	break;
		case WRITE_BIG:		op = TWE_CMD_WRITE;	break;
		default:		op = TWE_CMD_NOP;	break;
		}

		if ((ccb = twe_get_ccb(sc)) == NULL) {
			scsi_done(xs);
			xs->error = XS_DRIVER_STUFFUP;
			return (COMPLETE);
		}

		ccb->ccb_xs = xs;
		ccb->ccb_data = xs->data;
		ccb->ccb_length = xs->datalen;
		ccb->ccb_state = TWE_CCB_READY;
		cmd = ccb->ccb_cmd;
		cmd->cmd_unit_host = TWE_UNITHOST(target, 0); /* XXX why 0? */
		cmd->cmd_op = op;
		cmd->cmd_count = blockcnt;
		cmd->cmd_io.lba = blockno;

		if ((error = twe_cmd(ccb, ((xs->flags & SCSI_NOSLEEP)?
		    BUS_DMA_NOWAIT : BUS_DMA_WAITOK), xs->flags & SCSI_POLL))) {

			TWE_UNLOCK_TWE(sc, lock);
			TWE_DPRINTF(TWE_D_CMD, ("failed %p ", xs));
			if (xs->flags & SCSI_POLL) {
				xs->error = XS_TIMEOUT;
				return (TRY_AGAIN_LATER);
			} else {
				scsi_done(xs);
				xs->error = XS_DRIVER_STUFFUP;
				return (COMPLETE);
			}
		}

		TWE_UNLOCK_TWE(sc, lock);

		if (xs->flags & SCSI_POLL) {
			scsi_done(xs);
			return (COMPLETE);
		}
		return (SUCCESSFULLY_QUEUED);

	default:
		TWE_DPRINTF(TWE_D_CMD, ("unknown opc %d ", xs->cmd->opcode));
		xs->error = XS_DRIVER_STUFFUP;
	}

	return (COMPLETE);
}

int
twe_intr(v)
	void *v;
{
	struct twe_softc *sc = v;
	struct twe_ccb	*ccb;
	struct twe_cmd	*cmd;
	u_int32_t	status;
	twe_lock_t	lock;
	int		rv = 0;

	status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
	TWE_DPRINTF(TWE_D_INTR,  ("twe_intr stat=%b ",
	    status & TWE_STAT_FLAGS, TWE_STAT_BITS));
#if 0
	if (status & TWE_STAT_HOSTI) {

		bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
		    TWE_CTRL_CHOSTI);
	}
#endif

	if (status & TWE_STAT_CMDI) {

		lock = TWE_LOCK_TWE(sc);
		while (!(status & TWE_STAT_CQF) &&
		    !TAILQ_EMPTY(&sc->sc_ccb2q)) {

			ccb = TAILQ_LAST(&sc->sc_ccb2q, twe_queue_head);
			TAILQ_REMOVE(&sc->sc_ccb2q, ccb, ccb_link);

			ccb->ccb_state = TWE_CCB_QUEUED;
			TAILQ_INSERT_TAIL(&sc->sc_ccbq, ccb, ccb_link);
			bus_space_write_4(sc->iot, sc->ioh, TWE_COMMANDQUEUE,
			    ccb->ccb_cmdpa);

			rv++;

			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
			TWE_DPRINTF(TWE_D_INTR, ("twe_intr stat=%b ",
			    status & TWE_STAT_FLAGS, TWE_STAT_BITS));
		}

		if (TAILQ_EMPTY(&sc->sc_ccb2q))
			bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
			    TWE_CTRL_MCMDI);

		TWE_UNLOCK_TWE(sc, lock);
	}

	if (status & TWE_STAT_RDYI) {

		lock = TWE_LOCK_TWE(sc);
		while (!(status & TWE_STAT_RQE)) {

			u_int32_t ready;

			/*
			 * it seems that reading ready queue
			 * we get all the status bits in each ready word.
			 * i wonder if it's legal to use those for
			 * status and avoid extra read below
			 */
			ready = bus_space_read_4(sc->iot, sc->ioh,
			    TWE_READYQUEUE);

			if (!twe_done(sc, TWE_READYID(ready)))
				rv++;

			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
			TWE_DPRINTF(TWE_D_INTR, ("twe_intr stat=%b ",
			    status & TWE_STAT_FLAGS, TWE_STAT_BITS));
		}
		TWE_UNLOCK_TWE(sc, lock);
	}

	if (status & TWE_STAT_ATTNI) {
		u_int16_t aen;

		/*
		 * we no attentions of interest right now.
		 * one of those would be mirror degradation i think.
		 * or, what else exist in there? maybe 3ware can answer that.
		 */
		bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
		    TWE_CTRL_CATTNI);

		lock = TWE_LOCK_TWE(sc);
		for (aen = -1; aen != TWE_AEN_QEMPTY; ) {
			u_int8_t param_buf[2 * TWE_SECTOR_SIZE + TWE_ALIGN - 1];
			struct twe_param *pb = (void *) (((u_long)param_buf +
			    TWE_ALIGN - 1) & ~(TWE_ALIGN - 1));

			if ((ccb = twe_get_ccb(sc)) == NULL)
				break;

			ccb->ccb_xs = NULL;
			ccb->ccb_data = pb;
			ccb->ccb_length = TWE_SECTOR_SIZE;
			ccb->ccb_state = TWE_CCB_READY;
			cmd = ccb->ccb_cmd;
			cmd->cmd_unit_host = TWE_UNITHOST(0, 0);
			cmd->cmd_op = TWE_CMD_GPARAM;
			cmd->cmd_count = 1;

			pb->table_id = TWE_PARAM_AEN;
			pb->param_id = 2;
			pb->param_size = 2;
			if (twe_cmd(ccb, BUS_DMA_NOWAIT, 1)) {
				printf(": error draining attention queue\n");
				break;
			}
			aen = *(u_int16_t *)pb->data;
			TWE_DPRINTF(TWE_D_AEN, ("aen=%x ", aen));
		}
		TWE_UNLOCK_TWE(sc, lock);
	}

	return rv;
}
