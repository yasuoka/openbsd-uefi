/*	$OpenBSD: vs.c,v 1.44 2004/07/19 20:31:51 miod Exp $	*/

/*
 * Copyright (c) 2004, Miodrag Vallat.
 * Copyright (c) 1999 Steve Murphree, Jr.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Van Jacobson of Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * MVME328S SCSI adaptor driver
 */

/* This card lives in D16 space */
#define	__BUS_SPACE_RESTRICT_D16__

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/dkstat.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/evcount.h>

#include <uvm/uvm.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <machine/autoconf.h>
#include <machine/param.h>

#include <mvme88k/dev/vsreg.h>
#include <mvme88k/dev/vsvar.h>
#include <mvme88k/dev/vme.h>
#include <machine/cmmu.h>

int	vsmatch(struct device *, void *, void *);
void	vsattach(struct device *, struct device *, void *);
int	vs_scsicmd(struct scsi_xfer *);

struct scsi_adapter vs_scsiswitch = {
	vs_scsicmd,
	minphys,
	0,			/* no lun support */
	0,			/* no lun support */
};

struct scsi_device vs_scsidev = {
	NULL,		/* use default error handler */
	NULL,		/* do not have a start function */
	NULL,		/* have no async handler */
	NULL,		/* Use default done routine */
};

struct cfattach vs_ca = {
	sizeof(struct vs_softc), vsmatch, vsattach,
};

struct cfdriver vs_cd = {
	NULL, "vs", DV_DULL,
};

int	do_vspoll(struct vs_softc *, int, int);
void	thaw_queue(struct vs_softc *, int);
M328_SG	vs_alloc_scatter_gather(void);
M328_SG	vs_build_memory_structure(struct vs_softc *, struct scsi_xfer *,
	    bus_addr_t);
int	vs_checkintr(struct vs_softc *, struct scsi_xfer *, int *);
void	vs_chksense(struct scsi_xfer *);
void	vs_dealloc_scatter_gather(M328_SG);
int	vs_eintr(void *);
bus_addr_t vs_getcqe(struct vs_softc *);
bus_addr_t vs_getiopb(struct vs_softc *);
int	vs_initialize(struct vs_softc *);
int	vs_intr(struct vs_softc *);
void	vs_link_sg_element(sg_list_element_t *, vaddr_t, int);
void	vs_link_sg_list(sg_list_element_t *, vaddr_t, int);
int	vs_nintr(void *);
int	vs_poll(struct vs_softc *, struct scsi_xfer *);
void	vs_reset(struct vs_softc *);
void	vs_resync(struct vs_softc *);
void	vs_scsidone(struct vs_softc *, struct scsi_xfer *, int);

static __inline__ void vs_clear_return_info(struct vs_softc *);
static __inline__ int vs_queue_number(int, int);
static __inline__ paddr_t kvtop(vaddr_t);

int
vsmatch(struct device *device, void *cf, void *args)
{
	struct confargs *ca = args;
	bus_space_tag_t iot = ca->ca_iot;
	bus_space_handle_t ioh;
	int rc;

	if (bus_space_map(iot, ca->ca_paddr, S_SHORTIO, 0, &ioh) != 0)
		return 0;
	rc = badvaddr((vaddr_t)bus_space_vaddr(iot, ioh), 1);
	bus_space_unmap(iot, ioh, S_SHORTIO);

	return rc == 0;
}

void
vsattach(struct device *parent, struct device *self, void *args)
{
	struct vs_softc *sc = (struct vs_softc *)self;
	struct confargs *ca = args;
	int evec;
	int tmp;

	/* get the next available vector for the error interrupt */
	evec = vme_findvec(ca->ca_vec);

	if (ca->ca_vec < 0 || evec < 0) {
		printf(": no more interrupts!\n");
		return;
	}
	if (ca->ca_ipl < 0)
		ca->ca_ipl = IPL_BIO;

	printf(" vec 0x%x: ", evec);

	sc->sc_paddr = ca->ca_paddr;
	sc->sc_iot = ca->ca_iot;
	if (bus_space_map(sc->sc_iot, sc->sc_paddr, S_SHORTIO, 0,
	    &sc->sc_ioh) != 0) {
		printf("can't map registers!\n");
		return;
	}

	sc->sc_ipl = ca->ca_ipl;
	sc->sc_nvec = ca->ca_vec;
	sc->sc_evec = evec;

	if (vs_initialize(sc))
		return;

	sc->sc_link.adapter_softc = sc;
	sc->sc_link.adapter_target = sc->sc_pid;
	sc->sc_link.adapter = &vs_scsiswitch;
	sc->sc_link.device = &vs_scsidev;
	sc->sc_link.luns = 1;
	sc->sc_link.openings = NUM_IOPB / 8;

	sc->sc_ih_n.ih_fn = vs_nintr;
	sc->sc_ih_n.ih_arg = sc;
	sc->sc_ih_n.ih_wantframe = 0;
	sc->sc_ih_n.ih_ipl = ca->ca_ipl;

	sc->sc_ih_e.ih_fn = vs_eintr;
	sc->sc_ih_e.ih_arg = sc;
	sc->sc_ih_e.ih_wantframe = 0;
	sc->sc_ih_e.ih_ipl = ca->ca_ipl;

	vmeintr_establish(sc->sc_nvec, &sc->sc_ih_n);
	vmeintr_establish(sc->sc_evec, &sc->sc_ih_e);

	evcount_attach(&sc->sc_intrcnt_n, self->dv_xname,
	    (void *)&sc->sc_ih_n.ih_ipl, &evcount_intr);
	snprintf(sc->sc_intrname_e, sizeof sc->sc_intrname_e,
	    "%s_err", self->dv_xname);
	evcount_attach(&sc->sc_intrcnt_e, sc->sc_intrname_e,
	    (void *)&sc->sc_ih_e.ih_ipl, &evcount_intr);

	/*
	 * attach all scsi units on us, watching for boot device
	 * (see dk_establish).
	 */
	tmp = bootpart;
	if (sc->sc_paddr != bootaddr)
		bootpart = -1;		/* invalid flag to dk_establish */
	config_found(self, &sc->sc_link, scsiprint);
	bootpart = tmp;		    /* restore old value */
}

int
do_vspoll(struct vs_softc *sc, int to, int canreset)
{
	int i;
	int crsw;

	if (to <= 0 ) to = 50000;
	/* use cmd_wait values? */
	i = 10000;

	while (((crsw = CRSW) & (M_CRSW_CRBV | M_CRSW_CC)) == 0) {
		if (--i <= 0) {
			i = 50000;
			--to;
			if (to <= 0) {
				if (canreset) {
					vs_reset(sc);
					vs_resync(sc);
				}
				printf("%s: timeout %d crsw 0x%x\n",
				    sc->sc_dev.dv_xname, to, crsw);
				return 1;
			}
		}
	}
	return 0;
}

int
vs_poll(struct vs_softc *sc, struct scsi_xfer *xs)
{
	int status;
	int to;

	to = xs->timeout / 1000;
	for (;;) {
		if (do_vspoll(sc, to, 1)) {
			xs->error = XS_SELTIMEOUT;
			xs->status = -1;
			xs->flags |= ITSDONE;
			vs_clear_return_info(sc);
			if (xs->flags & SCSI_POLL)
				return (COMPLETE);
			break;
		}
		if (vs_checkintr(sc, xs, &status)) {
			vs_scsidone(sc, xs, status);
		}

		if (CRSW & M_CRSW_ER)
			CRB_CLR_ER;
		CRB_CLR_DONE;

		if (xs->flags & ITSDONE)
			break;
	}

	vs_clear_return_info(sc);
	return (COMPLETE);
}

void
thaw_queue(struct vs_softc *sc, int target)
{
	THAW(target);

	/* loop until thawed */
	while (THAW_REG & M_THAW_TWQE)
		;
}

void
vs_scsidone(struct vs_softc *sc, struct scsi_xfer *xs, int stat)
{
	int tgt;
	xs->status = stat;

	while (xs->status == SCSI_CHECK) {
		vs_chksense(xs);
		tgt = vs_queue_number(xs->sc_link->target, sc->sc_pid);
		thaw_queue(sc, tgt);
	}

	tgt = vs_queue_number(xs->sc_link->target, sc->sc_pid);
	xs->flags |= ITSDONE;

	/* thaw all work queues */
	thaw_queue(sc, tgt);
	scsi_done(xs);
}

int
vs_scsicmd(struct scsi_xfer *xs)
{
	struct scsi_link *slp = xs->sc_link;
	struct vs_softc *sc = slp->adapter_softc;
	int flags, option;
	unsigned int iopb_len;
	bus_addr_t cqep, iopb;
	M328_CMD *m328_cmd;

	flags = xs->flags;
	if (flags & SCSI_POLL) {
		cqep = sh_MCE;
		iopb = sh_MCE_IOPB;
	} else {
		cqep = vs_getcqe(sc);
		if (cqep == 0) {
			xs->error = XS_DRIVER_STUFFUP;
			return (TRY_AGAIN_LATER);
		}
		iopb = vs_getiopb(sc);
	}

	vs_bzero(iopb, IOPB_LONG_SIZE);

	iopb_len = IOPB_SHORT_SIZE + xs->cmdlen;
	bus_space_write_region_1(sc->sc_iot, sc->sc_ioh, iopb + IOPB_SCSI_DATA,
	    (u_int8_t *)xs->cmd, xs->cmdlen);

	vs_write(2, iopb + IOPB_CMD, IOPB_PASSTHROUGH);
	vs_write(2, iopb + IOPB_UNIT, IOPB_UNIT_VALUE(slp->target, slp->lun));
	vs_write(1, iopb + IOPB_NVCT, sc->sc_nvec);
	vs_write(1, iopb + IOPB_EVCT, sc->sc_evec);

	/*
	 * Since the 88k doesn't support cache snooping, we have
	 * to flush the cache for a write and flush with inval for
	 * a read, prior to starting the IO.
	 */
	dma_cachectl((vaddr_t)xs->data, xs->datalen,
	    flags & SCSI_DATA_IN ? DMA_CACHE_SYNC_INVAL : DMA_CACHE_SYNC);
	
	option = 0;
	if (flags & SCSI_DATA_OUT)
		option |= M_OPT_DIR;

	if (flags & SCSI_POLL) {
		vs_write(2, iopb + IOPB_OPTION, option);
		vs_write(2, iopb + IOPB_LEVEL, 0);
	} else {
		vs_write(2, iopb + IOPB_OPTION, option | M_OPT_IE);
		vs_write(2, iopb + IOPB_LEVEL, sc->sc_ipl);
	}
	vs_write(2, iopb + IOPB_ADDR, ADDR_MOD);

	/*
	 * Wait until we can use the command queue entry.
	 * Should only have to wait if the master command
	 * queue entry is busy and we are polling.
	 */
	while (vs_read(2, cqep + CQE_QECR) & M_QECR_GO)
		;

	vs_write(2, cqep + CQE_IOPB_ADDR, iopb);
	vs_write(1, cqep + CQE_IOPB_LENGTH, iopb_len);
	vs_write(1, cqep + CQE_WORK_QUEUE,
	    flags & SCSI_POLL ? 0 : vs_queue_number(slp->target, sc->sc_pid));

	MALLOC(m328_cmd, M328_CMD*, sizeof(M328_CMD), M_DEVBUF, M_WAITOK);

	m328_cmd->xs = xs;
	if (xs->datalen != 0)
		m328_cmd->top_sg_list = vs_build_memory_structure(sc, xs, iopb);
	else
		m328_cmd->top_sg_list = NULL;

	vs_write(4, cqep + CQE_CTAG, (u_int32_t)m328_cmd);

	if (crb_read(2, CRB_CRSW) & M_CRSW_AQ)
		vs_write(2, cqep + CQE_QECR, M_QECR_AA);

	vs_write(2, cqep + CQE_QECR, vs_read(2, cqep + CQE_QECR) | M_QECR_GO);

	if (flags & SCSI_POLL) {
		/* poll for the command to complete */
		return vs_poll(sc, xs);
	}

	return (SUCCESSFULLY_QUEUED);
}

void
vs_chksense(struct scsi_xfer *xs)
{
	int s;
	struct scsi_link *slp = xs->sc_link;
	struct vs_softc *sc = slp->adapter_softc;
	struct scsi_sense *ss;

	/* ack and clear the error */
	if (CRSW & M_CRSW_ER)
		CRB_CLR_ER;
	CRB_CLR_DONE;
	xs->status = 0;

	vs_bzero(sh_MCE_IOPB, IOPB_LONG_SIZE);
	/* This is a command, so point to it */
	ss = (void *)(bus_space_vaddr(sc->sc_iot, sc->sc_ioh) +
	    sh_MCE_IOPB + IOPB_SCSI_DATA);
	ss->opcode = REQUEST_SENSE;
	ss->byte2 = slp->lun << 5;
	ss->length = sizeof(struct scsi_sense_data);

	mce_iopb_write(2, IOPB_CMD, IOPB_PASSTHROUGH);
	mce_iopb_write(2, IOPB_OPTION, 0);
	mce_iopb_write(1, IOPB_NVCT, sc->sc_nvec);
	mce_iopb_write(1, IOPB_EVCT, sc->sc_evec);
	mce_iopb_write(2, IOPB_LEVEL, 0 /* sc->sc_ipl */);
	mce_iopb_write(2, IOPB_ADDR, ADDR_MOD);
	mce_iopb_write(4, IOPB_BUFF, kvtop((vaddr_t)&xs->sense));
	mce_iopb_write(4, IOPB_LENGTH, sizeof(struct scsi_sense_data));
	mce_iopb_write(2, IOPB_UNIT, IOPB_UNIT_VALUE(slp->target, slp->lun));

	vs_bzero(sh_MCE, CQE_SIZE);
	mce_write(2, CQE_IOPB_ADDR, sh_MCE_IOPB);
	mce_write(1, CQE_IOPB_LENGTH,
	    IOPB_SHORT_SIZE + sizeof(struct scsi_sense));
	mce_write(1, CQE_WORK_QUEUE, 0);
	mce_write(2, CQE_QECR, M_QECR_GO);

	/* poll for the command to complete */
	s = splbio();
	do_vspoll(sc, 0, 1);
	xs->status = vs_read(2, sh_RET_IOPB + IOPB_STATUS) >> 8;
	splx(s);
}

bus_addr_t
vs_getcqe(struct vs_softc *sc)
{
	bus_addr_t cqep;
	int qhdp;

	qhdp = mcsb_read(2, MCSB_QHDP);
	cqep = sh_CQE(qhdp);

	if (vs_read(2, cqep + CQE_QECR) & M_QECR_GO) {
		/* should never happen */
		return 0;
	}

	if (++qhdp == NUM_CQE)
		qhdp = 0;
	mcsb_write(2, MCSB_QHDP, qhdp);

	vs_bzero(cqep, CQE_SIZE);
	return cqep;
}

bus_addr_t
vs_getiopb(struct vs_softc *sc)
{
	bus_addr_t iopb;
	int qhdp;

	/*
	 * Since we are always invoked after vs_getcqe(), qhdp has already
	 * been incremented...
	 */
	qhdp = mcsb_read(2, MCSB_QHDP);
	if (--qhdp < 0)
		qhdp = NUM_CQE - 1;

	iopb = sh_IOPB(qhdp);
	return iopb;
}

int
vs_initialize(struct vs_softc *sc)
{
	int i, msr;

	/*
	 * Reset the board, and wait for it to get ready.
	 * The reset signal is applied for 70 usec, and the board status
	 * is not tested until 100 usec after the reset signal has been
	 * cleared, per the manual (MVME328/D1) pages 4-6 and 4-9.
	 */

	mcsb_write(2, MCSB_MCR, M_MCR_RES | M_MCR_SFEN);
	delay(70);
	mcsb_write(2, MCSB_MCR, M_MCR_SFEN);

	delay(100);
	i = 0;
	for (;;) {
		msr = mcsb_read(2, MCSB_MSR);
		if ((msr & (M_MSR_BOK | M_MSR_CNA)) == M_MSR_BOK)
			break;
		if (++i > 5000) {
			printf("board reset failed, status %x\n", msr);
			return 1;
		}
		delay(1000);
	}

	/* initialize channels id */
	sc->sc_pid = csb_read(1, CSB_PID);
	sc->sc_sid = csb_read(1, CSB_SID);

	CRB_CLR_DONE;
	mcsb_write(2, MCSB_QHDP, 0);

	vs_bzero(sh_CIB, CIB_SIZE);
	cib_write(2, CIB_NCQE, NUM_CQE);
	cib_write(2, CIB_BURST, 0);
	cib_write(2, CIB_NVECT, (sc->sc_ipl << 8) | sc->sc_nvec);
	cib_write(2, CIB_EVECT, (sc->sc_ipl << 8) | sc->sc_evec);
	cib_write(2, CIB_PID, sc->sc_pid);
	cib_write(2, CIB_SID, 0);	/* disable second channel */
	cib_write(2, CIB_CRBO, sh_CRB);
	cib_write(4, CIB_SELECT, SELECTION_TIMEOUT);
	cib_write(4, CIB_WQTIMO, 4);
	cib_write(4, CIB_VMETIMO, 0 /* VME_BUS_TIMEOUT */);
	cib_write(2, CIB_ERR_FLGS, M_ERRFLGS_RIN | M_ERRFLGS_RSE);
	cib_write(2, CIB_SBRIV, (sc->sc_ipl << 8) | sc->sc_evec);
	cib_write(1, CIB_SOF0, 0x15);
	cib_write(1, CIB_SRATE0, 100 / 4);
	cib_write(1, CIB_SOF1, 0);
	cib_write(1, CIB_SRATE1, 0);

	vs_bzero(sh_MCE_IOPB, IOPB_LONG_SIZE);
	mce_iopb_write(2, IOPB_CMD, CNTR_INIT);
	mce_iopb_write(2, IOPB_OPTION, 0);
	mce_iopb_write(1, IOPB_NVCT, sc->sc_nvec);
	mce_iopb_write(1, IOPB_EVCT, sc->sc_evec);
	mce_iopb_write(2, IOPB_LEVEL, 0 /* sc->sc_ipl */);
	mce_iopb_write(2, IOPB_ADDR, SHIO_MOD);
	mce_iopb_write(4, IOPB_BUFF, sh_CIB);
	mce_iopb_write(4, IOPB_LENGTH, CIB_SIZE);

	vs_bzero(sh_MCE, CQE_SIZE);
	mce_write(2, CQE_IOPB_ADDR, sh_MCE_IOPB);
	mce_write(1, CQE_IOPB_LENGTH, IOPB_LONG_SIZE);
	mce_write(1, CQE_WORK_QUEUE, 0);
	mce_write(2, CQE_QECR, M_QECR_GO);

	/* poll for the command to complete */
	do_vspoll(sc, 0, 1);

	/* initialize work queues */
	for (i = 1; i < 8; i++) {
		vs_bzero(sh_MCE_IOPB, IOPB_LONG_SIZE);
		mce_iopb_write(2, WQCF_CMD, CNTR_INIT_WORKQ);
		mce_iopb_write(2, WQCF_OPTION, 0);
		mce_iopb_write(1, WQCF_NVCT, sc->sc_nvec);
		mce_iopb_write(1, WQCF_EVCT, sc->sc_evec);
		mce_iopb_write(2, WQCF_ILVL, 0 /* sc->sc_ipl */);
		mce_iopb_write(2, WQCF_WORKQ, i);
		mce_iopb_write(2, WQCF_WOPT, M_WOPT_FE | M_WOPT_IWQ);
		mce_iopb_write(2, WQCF_SLOTS, JAGUAR_MAX_Q_SIZ);
		mce_iopb_write(4, WQCF_CMDTO, 4);	/* 1 second */

		vs_bzero(sh_MCE, CQE_SIZE);
		mce_write(2, CQE_IOPB_ADDR, sh_MCE_IOPB);
		mce_write(1, CQE_IOPB_LENGTH, IOPB_LONG_SIZE);
		mce_write(1, CQE_WORK_QUEUE, 0);
		mce_write(2, CQE_QECR, M_QECR_GO);

		/* poll for the command to complete */
		do_vspoll(sc, 0, 1);
		if (CRSW & M_CRSW_ER)
			CRB_CLR_ER;
		CRB_CLR_DONE;
#if 0
		delay(500);
#endif
	}

	/* start queue mode */
	mcsb_write(2, MCSB_MCR, mcsb_read(2, MCSB_MCR) | M_MCR_SQM);

	do_vspoll(sc, 0, 1);
	if (CRSW & M_CRSW_ER) {
		printf("initialization error, status = 0x%x\n",
		    vs_read(2, sh_RET_IOPB + IOPB_STATUS));
		CRB_CLR_DONE;
		return 1;
	}
	CRB_CLR_DONE;

	/* reset SCSI bus */
	vs_reset(sc);
	/* sync all devices */
	vs_resync(sc);
	printf("SCSI ID %d\n", sc->sc_pid);
	return 0;
}

void
vs_resync(struct vs_softc *sc)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (i == sc->sc_pid)
			continue;

		vs_bzero(sh_MCE_IOPB, IOPB_SHORT_SIZE);
		mce_iopb_write(2, DRCF_CMD, CNTR_DEV_REINIT);
		mce_iopb_write(2, DRCF_OPTION, 0); /* no interrupts yet */
		mce_iopb_write(1, DRCF_NVCT, sc->sc_nvec);
		mce_iopb_write(1, DRCF_EVCT, sc->sc_evec);
		mce_iopb_write(2, DRCF_ILVL, 0);
		mce_iopb_write(2, DRCF_UNIT, IOPB_UNIT_VALUE(i, 0));

		vs_bzero(sh_MCE, CQE_SIZE);
		mce_write(2, CQE_IOPB_ADDR, sh_MCE_IOPB);
		mce_write(1, CQE_IOPB_LENGTH, IOPB_SHORT_SIZE);
		mce_write(1, CQE_WORK_QUEUE, 0);
		mce_write(2, CQE_QECR, M_QECR_GO);

		/* poll for the command to complete */
		do_vspoll(sc, 0, 0);
		if (CRSW & M_CRSW_ER)
			CRB_CLR_ER;
		CRB_CLR_DONE;
	}
}

void
vs_reset(struct vs_softc *sc)
{
	int s;

	s = splbio();

	vs_bzero(sh_MCE_IOPB, IOPB_SHORT_SIZE);
	mce_iopb_write(2, SRCF_CMD, IOPB_RESET);
	mce_iopb_write(2, SRCF_OPTION, 0);	/* not interrupts yet... */
	mce_iopb_write(1, SRCF_NVCT, sc->sc_nvec);
	mce_iopb_write(1, SRCF_EVCT, sc->sc_evec);
	mce_iopb_write(2, SRCF_ILVL, 0);
	mce_iopb_write(2, SRCF_BUSID, 0);

	vs_bzero(sh_MCE, CQE_SIZE);
	mce_write(2, CQE_IOPB_ADDR, sh_MCE_IOPB);
	mce_write(1, CQE_IOPB_LENGTH, IOPB_SHORT_SIZE);
	mce_write(1, CQE_WORK_QUEUE, 0);
	mce_write(2, CQE_QECR, M_QECR_GO);

	/* poll for the command to complete */
	for (;;) {
		do_vspoll(sc, 0, 0);
		/* ack & clear scsi error condition cause by reset */
		if (CRSW & M_CRSW_ER) {
			CRB_CLR_DONE;
			vs_write(2, sh_RET_IOPB + IOPB_STATUS, 0);
			break;
		}
		CRB_CLR_DONE;
	}

	/* thaw all work queues */
	thaw_queue(sc, 0xff);

	splx(s);
}

/*
 * Process an interrupt from the MVME328
 * We'll generally update: xs->{flags,resid,error,sense,status} and
 * occasionally xs->retries.
 */
int
vs_checkintr(struct vs_softc *sc, struct scsi_xfer *xs, int *status)
{
	u_int32_t len;
	int error;

	len = vs_read(4, sh_RET_IOPB + IOPB_LENGTH);
	error = vs_read(2, sh_RET_IOPB + IOPB_STATUS);
	*status = error >> 8;

	xs->resid = xs->datalen - len;

	if ((error & 0xff) == SCSI_SELECTION_TO) {
		xs->error = XS_SELTIMEOUT;
		xs->status = -1;
		*status = -1;
	}

	return 1;
}

/* normal interrupt routine */
int
vs_nintr(void *vsc)
{
	struct vs_softc *sc = (struct vs_softc *)vsc;
	M328_CMD *m328_cmd;
	struct scsi_xfer *xs;
	int status;
	int s;

	if ((CRSW & CONTROLLER_ERROR) == CONTROLLER_ERROR)
		return vs_eintr(sc);

	/* Got a valid interrupt on this device */
	s = splbio();
	sc->sc_intrcnt_n.ec_count++;
	m328_cmd = (void *)crb_read(4, CRB_CTAG);

	/*
	 * If this is a controller error, there won't be a m328_cmd
	 * pointer in the CTAG field.  Bad things happen if you try
	 * to point to address 0.  But then, we should have caught
	 * the controller error above.
	 */
	if (m328_cmd != NULL) {
		xs = m328_cmd->xs;
		if (m328_cmd->top_sg_list != NULL) {
			vs_dealloc_scatter_gather(m328_cmd->top_sg_list);
			m328_cmd->top_sg_list = (M328_SG)NULL;
		}
		FREE(m328_cmd, M_DEVBUF); /* free the command tag */

		if (vs_checkintr(sc, xs, &status)) {
			vs_scsidone(sc, xs, status);
		}
	}

	/* ack the interrupt */
	if (CRSW & M_CRSW_ER)
		CRB_CLR_ER;
	CRB_CLR_DONE;

	vs_clear_return_info(sc);
	splx(s);

	return 1;
}

/* error interrupts */
int
vs_eintr(void *vsc)
{
	struct vs_softc *sc = (struct vs_softc *)vsc;
	M328_CMD *m328_cmd;
	struct scsi_xfer *xs;
	int crsw, ecode;
	int s;

	/* Got a valid interrupt on this device */
	s = splbio();
	sc->sc_intrcnt_e.ec_count++;

	crsw = vs_read(2, sh_CEVSB + CEVSB_CRSW);
	ecode = vs_read(1, sh_CEVSB + CEVSB_ERROR);
	m328_cmd = (void *)crb_read(4, CRB_CTAG);
	xs = m328_cmd != NULL ? m328_cmd->xs : NULL;

	if (crsw & M_CRSW_RST) {
		printf("%s: bus reset\n", sc->sc_dev.dv_xname);
		vs_clear_return_info(sc);
		splx(s);
		return 1;
	}

	if (xs == NULL)
		printf("%s: ", sc->sc_dev.dv_xname);
	else {
		printf("%s(target %d): ",
		    sc->sc_dev.dv_xname, xs->sc_link->target);
	}

	switch (ecode) {
	case CEVSB_ERR_TYPE:
		printf("IOPB type error\n");
		break;
	case CEVSB_ERR_TO:
		printf("timeout\n");
		if (xs != NULL) {
			xs->error = XS_SELTIMEOUT;
			xs->status = -1;
			xs->flags |= ITSDONE;
			scsi_done(xs);
		}
		break;
	case CEVSB_ERR_TR:
		printf("reconnect error\n");
		break;
	case CEVSB_ERR_OF:
		printf("overflow\n");
		break;
	case CEVSB_ERR_BD:
		printf("bad direction\n");
		break;
	case CEVSB_ERR_NR:
		printf("non-recoverable error\n");
		break;
	case CEVSB_ERR_PANIC:
		printf("board panic\n");
		break;
	default:
		printf("unexpected error %x\n", ecode);
		break;
	}

	if (CRSW & M_CRSW_ER)
		CRB_CLR_ER;
	CRB_CLR_DONE;

	thaw_queue(sc, 0xff);
	vs_clear_return_info(sc);
	splx(s);

	return 1;
}

static void
vs_clear_return_info(struct vs_softc *sc)
{
	vs_bzero(sh_RET_IOPB, CRB_SIZE + IOPB_LONG_SIZE);
}

/*
 * Choose the work queue number for a specific target.
 *
 * Targets on the primary channel should be mapped to queues 1-7,
 * so we assign each target the queue matching its own number, except for
 * target zero which gets assigned to the queue matching the controller id.
 */
static int
vs_queue_number(int target, int host)
{
	return target == 0 ? host : target;
}

/*
 * Useful functions for scatter/gather list
 */

M328_SG
vs_alloc_scatter_gather(void)
{
	M328_SG sg;

	MALLOC(sg, M328_SG, sizeof(struct m328_sg), M_DEVBUF, M_WAITOK);
	bzero(sg, sizeof(struct m328_sg));

	return (sg);
}

void
vs_dealloc_scatter_gather(M328_SG sg)
{
	int i;

	if (sg->level > 0) {
		for (i = 0; sg->down[i] && i < MAX_SG_ELEMENTS; i++) {
			vs_dealloc_scatter_gather(sg->down[i]);
		}
	}
	FREE(sg, M_DEVBUF);
}

void
vs_link_sg_element(sg_list_element_t *element, vaddr_t phys_add, int len)
{
	element->count.bytes = len;
	element->addrlo = phys_add;
	element->addrhi = phys_add >> 16;
	element->link = 0; /* FALSE */
	element->transfer_type = NORMAL_TYPE;
	element->memory_type = LONG_TRANSFER;
	element->address_modifier = ADRM_EXT_S_D;
}

void
vs_link_sg_list(sg_list_element_t *list, vaddr_t phys_add, int elements)
{
	list->count.scatter.gather = elements;
	list->addrlo = phys_add;
	list->addrhi = phys_add >> 16;
	list->link = 1;	   /* TRUE */
	list->transfer_type = NORMAL_TYPE;
	list->memory_type = LONG_TRANSFER;
	list->address_modifier = ADRM_EXT_S_D;
}

M328_SG
vs_build_memory_structure(struct vs_softc *sc, struct scsi_xfer *xs,
     bus_addr_t iopb)
{
	M328_SG sg;
	vaddr_t starting_point_virt, starting_point_phys, point_virt,
	point1_phys, point2_phys, virt;
	unsigned int len;
	int level;

	sg = NULL;	/* Hopefully we need no scatter/gather list */

	/*
	 * We have the following things:
	 *	virt			va of the virtual memory block
	 *	len			length of the virtual memory block
	 *	starting_point_virt	va of the physical memory block
	 *	starting_point_phys	pa of the physical memory block
	 *	point_virt		va of the virtual memory
	 *				    we are checking at the moment
	 *	point1_phys		pa of the physical memory
	 *				    we are checking at the moment
	 *	point2_phys		pa of another physical memory
	 *				    we are checking at the moment
	 */

	level = 0;
	virt = starting_point_virt = (vaddr_t)xs->data;
	point1_phys = starting_point_phys = kvtop((vaddr_t)xs->data);
	len = xs->datalen;

	/*
	 * Check if we need scatter/gather
	 */
	if (trunc_page(virt + len - 1) != trunc_page(virt)) {
		for (point_virt = round_page(starting_point_virt + 1);
		    /* if we do already scatter/gather we have to stay in the loop and jump */
		    point_virt < virt + len || sg != NULL;
		    point_virt += PAGE_SIZE) {		   /* out later */

			point2_phys = kvtop(point_virt);

			if ((point2_phys != trunc_page(point1_phys) + PAGE_SIZE) ||		   /* physical memory is not contiguous */
			    (point_virt - starting_point_virt >= MAX_SG_BLOCK_SIZE && sg)) {   /* we only can access (1<<16)-1 bytes in scatter/gather_mode */
				if (point_virt - starting_point_virt >= MAX_SG_BLOCK_SIZE) {	       /* We were walking too far for one scatter/gather block ... */
					point_virt = trunc_page(starting_point_virt+MAX_SG_BLOCK_SIZE-1);    /* So go back to the beginning of the last matching page */
					/* and generate the physical address of
					 * this location for the next time. */
					point2_phys = kvtop(point_virt);
				}

				if (sg == NULL)
					sg = vs_alloc_scatter_gather();

#if 1 /* broken firmware */
				if (sg->elements >= MAX_SG_ELEMENTS) {
					vs_dealloc_scatter_gather(sg);
					printf("%s: scatter/gather list too large\n",
					    sc->sc_dev.dv_xname);
					return (NULL);
				}
#else /* if the firmware will ever get fixed */
				while (sg->elements >= MAX_SG_ELEMENTS) {
					if (!sg->up) { /* If the list full in this layer ? */
						sg->up = vs_alloc_scatter_gather();
						sg->up->level = sg->level+1;
						sg->up->down[0] = sg;
						sg->up->elements = 1;
					}
					/* link this full list also in physical memory */
					vs_link_sg_list(&(sg->up->list[sg->up->elements-1]),
							kvtop((vaddr_t)sg->list),
							sg->elements);
					sg = sg->up;	  /* Climb up */
				}
				while (sg->level) {  /* As long as we are not a the base level */
					int i;

					i = sg->elements;
					/* We need a new element */
					sg->down[i] = vs_alloc_scatter_gather();
					sg->down[i]->level = sg->level - 1;
					sg->down[i]->up = sg;
					sg->elements++;
					sg = sg->down[i]; /* Climb down */
				}
#endif /* 1 */
				if (point_virt < virt + len) {
					/* linking element */
					vs_link_sg_element(&(sg->list[sg->elements]),
							   starting_point_phys,
							   point_virt - starting_point_virt);
					sg->elements++;
				} else {
					/* linking last element */
					vs_link_sg_element(&(sg->list[sg->elements]),
							   starting_point_phys,
							   virt + len - starting_point_virt);
					sg->elements++;
					break;			       /* We have now collected all blocks */
				}
				starting_point_virt = point_virt;
				starting_point_phys = point2_phys;
			}
			point1_phys = point2_phys;
		}
	}

	/*
	 * Climb up along the right side of the tree until we reach the top.
	 */

	if (sg != NULL) {
		while (sg->up) {
			/* link this list also in physical memory */
			vs_link_sg_list(&(sg->up->list[sg->up->elements-1]),
					kvtop((vaddr_t)sg->list),
					sg->elements);
			sg = sg->up;		       /* Climb up */
		}

		vs_write(2, iopb + IOPB_OPTION,
		    vs_read(2, iopb + IOPB_OPTION) | M_OPT_SG);
		vs_write(2, iopb + IOPB_ADDR,
		    vs_read(2, iopb + IOPB_ADDR) | M_ADR_SG_LINK);
		vs_write(4, iopb + IOPB_BUFF, kvtop((vaddr_t)sg->list));
		vs_write(4, iopb + IOPB_LENGTH, sg->elements);
		vs_write(4, iopb + IOPB_SGTTL, len);
	} else {
		/* no scatter/gather necessary */
		vs_write(4, iopb + IOPB_BUFF, starting_point_phys);
		vs_write(4, iopb + IOPB_LENGTH, len);
	}
	return sg;
}

static paddr_t
kvtop(vaddr_t va)
{
	paddr_t pa;

	pmap_extract(pmap_kernel(), va, &pa);
	/* XXX check for failure */
	return pa;
}
