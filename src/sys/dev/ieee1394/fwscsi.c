/*	$OpenBSD: fwscsi.c,v 1.9 2002/12/30 11:52:21 tdeval Exp $	*/

/*
 * Copyright (c) 2002 Thierry Deval.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/timeout.h>

#include <dev/rndvar.h>
#include <machine/bus.h>

#ifdef	__NetBSD__
#include <dev/scsipi/scsi_all.h>
#include <dev/scsipi/scsipi_all.h>
#include <dev/scsipi/scsiconf.h>
#else
#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#endif

#include <dev/std/ieee1212reg.h>
#include <dev/std/ieee1212var.h>
#include <dev/std/sbp2reg.h>
#include <dev/std/sbp2var.h>
#include <dev/ieee1394/ieee1394reg.h>
#include <dev/ieee1394/ieee1394var.h>
#include <dev/ieee1394/fwohcivar.h>
#include <dev/ieee1394/fwnodevar.h>
#include <dev/ieee1394/fwnodereg.h>

#ifdef	FWSCSI_DEBUG
#include <sys/syslog.h>
extern int log_open;
int fwscsi_oldlog;
#define	DPRINTF(x)	if (fwscsidebug&3) do {				\
	fwscsi_oldlog = log_open; log_open = 1;				\
	addlog x; log_open = fwscsi_oldlog;				\
} while (0)
#define	DPRINTFN(n,x)	if ((fwscsidebug&3)>(n)) do {			\
	fwscsi_oldlog = log_open; log_open = 1;				\
	addlog x; log_open = fwscsi_oldlog;				\
} while (0)
#ifdef	FW_MALLOC_DEBUG
#define	MPRINTF(x,y)	DPRINTF(("%s[%d]: %s 0x%08x\n",			\
			    __func__, __LINE__, (x), (u_int32_t)(y)))
#else	/* !FW_MALLOC_DEBUG */
#define	MPRINTF(x,y)
#endif	/* FW_MALLOC_DEBUG */
int	fwscsidebug = 0;
#else	/* FWSCSI_DEBUG */
#define	DPRINTF(x)
#define	DPRINTFN(n,x)
#define	MPRINTF(x,y)
#endif	/* !FWSCSI_DEBUG */

#ifdef	__NetBSD__
int  fwscsi_match(struct device *, struct cfdata *, void *);
#else
int  fwscsi_match(struct device *, void *, void *);
#endif
void fwscsi_attach(struct device *, struct device *, void *);
void fwscsi_config_thread(void *);
void fwscsi_login_cb(void *, struct sbp2_status_notification *);
void fwscsi_agent_init(void *);
void fwscsi_status_notify(void *, struct sbp2_status_notification *);
int  fwscsi_detach(struct device *, int);
#ifdef	__NetBSD__
void fwscsi_scsipi_request(struct scsipi_channel *, scsipi_adapter_req_t,
    void *);
void fwscsi_scsipi_minphys(struct buf *);
#else
int  fwscsi_scsi_cmd(struct scsi_xfer *);
void fwscsi_minphys(struct buf *);
#endif
void fwscsi_command_timeout(void *);
void fwscsi_command_wait(void *, struct sbp2_status_notification *);
void fwscsi_command_data(struct ieee1394_abuf *, int);

typedef struct fwscsi_orb_data {
	u_int32_t	data_hash;
	u_int32_t	data_mask;
	size_t		data_len;
	caddr_t		data_addr;
	struct ieee1394_abuf *data_ab;
	TAILQ_ENTRY(fwscsi_orb_data) data_chain;
} fwscsi_orb_data;
static TAILQ_HEAD(fwscsi_data_tq, fwscsi_orb_data) fwscsi_datalist;
static int fwscsi_data_valid;

typedef struct fwscsi_status {
	u_int8_t	flags;
	u_int8_t	status;
	u_int16_t	orb_offset_hi;
	u_int32_t	orb_offset_lo;
	u_int8_t	scsi_status;
	u_int8_t	sense_key;
#define	FWSCSI_INFO_VALID	0x80
#define	FWSCSI_MEI		0x70
#define	FWSCSI_SENSE_KEY	0x0F
	u_int8_t	sense_code;
	u_int8_t	sense_qual;
	u_int32_t	information;
	u_int32_t	cmd_spec_info;
	u_int32_t	sense_key_info;
	u_int32_t	vendor_info[2];
} fwscsi_status;

#ifdef	__OpenBSD__
struct scsi_adapter fwscsi_switch = {
	fwscsi_scsi_cmd,	/* scsi_cmd */
	fwscsi_minphys,		/* scsi_minphys */
	NULL,			/* open_target_lu */
	NULL,			/* close_target_lu */
	NULL			/* ioctl */
};

struct scsi_device fwscsi_dev = {
	NULL,			/* Use default error handler */
	NULL,			/* have a queue, served by this */
	NULL,			/* have no async handler */
	NULL			/* Use default 'done' routine */
};
#endif

typedef struct fwscsi_softc {
	struct device		  sc_dev;
	struct p1212_dir	**sc_unitdir;
	int			  sc_units;

	struct scsi_link	  sc_adapter_link;

	u_int16_t		  sc_lun;
	u_int8_t		  sc_speed;		/* log2(n / 100M) */
	u_int8_t		  sc_maxpayload;	/* log2(n / 4) */

	struct scsibus_softc	 *sc_bus;
} fwscsi_softc;

struct cfattach fwscsi_ca = {
	sizeof(struct fwscsi_softc), fwscsi_match, fwscsi_attach, fwscsi_detach
};

#ifdef	__OpenBSD__
struct cfdriver fwscsi_cd = {
	NULL, "fwscsi", DV_DULL
};
#endif

struct fwscsi_orb_data *fwscsi_datafind(struct fwscsi_softc *, u_int32_t);
struct fwscsi_orb_data *
fwscsi_datafind(struct fwscsi_softc *sc, u_int32_t hash)
{
	struct fwscsi_orb_data *data;

	TAILQ_FOREACH(data, &fwscsi_datalist, data_chain) {
		if ((data->data_hash & data->data_mask) ==
		    (hash & data->data_mask))
			break;
	}

	return (data);
}

#ifdef	__NetBSD__
int
fwscsi_match(struct device *parent, struct cfdata *match, void *aux)
#else
int
fwscsi_match(struct device *parent, void *match, void *aux)
#endif
{
	struct p1212_key **key;
	struct p1212_dir **udirs = aux;

	key = p1212_find(*udirs, P1212_KEYTYPE_Immediate,
	    P1212_KEYVALUE_Unit_Spec_Id, 0);
	if (!key || key[0]->val != SBP2_UNIT_SPEC_ID) {
		if (key != NULL) {
			free(key, M_DEVBUF);
			MPRINTF("free(DEVBUF)", key);
			key = NULL;	/* XXX */
		}
		return 0;
	}
	free(key, M_DEVBUF);
	MPRINTF("free(DEVBUF)", key);
	key = NULL;	/* XXX */

	key = p1212_find(*udirs, P1212_KEYTYPE_Immediate,
	    P1212_KEYVALUE_Unit_Sw_Version, 0);
	if (!key || key[0]->val != SBP2_UNIT_SW_VERSION) {
		if (key != NULL) {
			free(key, M_DEVBUF);
			MPRINTF("free(DEVBUF)", key);
			key = NULL;	/* XXX */
		}
		return 0;
	}
	free(key, M_DEVBUF);
	MPRINTF("free(DEVBUF)", key);
	key = NULL;	/* XXX */

	key = p1212_find(*udirs, P1212_KEYTYPE_Immediate,
	    SBP2_KEYVALUE_Command_Set_Spec_Id, 0);
	if (!key || key[0]->val != 0x00609E) {
		if (key != NULL) {
			free(key, M_DEVBUF);
			MPRINTF("free(DEVBUF)", key);
			key = NULL;	/* XXX */
		}
		return 0;
	}
	free(key, M_DEVBUF);
	MPRINTF("free(DEVBUF)", key);
	key = NULL;	/* XXX */

	key = p1212_find(*udirs, P1212_KEYTYPE_Immediate,
	    SBP2_KEYVALUE_Command_Set, 0);
	if (!key || key[0]->val != 0x0104D8) {
		if (key != NULL) {
			free(key, M_DEVBUF);
			MPRINTF("free(DEVBUF)", key);
			key = NULL;	/* XXX */
		}
		return 0;
	}
	free(key, M_DEVBUF);
	MPRINTF("free(DEVBUF)", key);
	key = NULL;	/* XXX */

	return 1;
}

void
fwscsi_attach(struct device *parent, struct device *self, void *aux)
{
	struct fwnode_softc *fwsc = (struct fwnode_softc *)parent;
	struct fwscsi_softc *sc = (struct fwscsi_softc *)self;
	struct p1212_dir **udir = (struct p1212_dir **)aux;
	struct sbp2_login_orb *login_orb;
	int lun;

	printf("\n");

	for (sc->sc_units = 0; udir[sc->sc_units]; sc->sc_units++) {};
	DPRINTF(("%s: cpl = %d(%08x), %d unit(s)\n", __func__,
	    cpl, cpl, sc->sc_units));

	if (!sc->sc_units) {
		sc->sc_unitdir = NULL;
		return;
	}

	if (!fwscsi_data_valid) {
		TAILQ_INIT(&fwscsi_datalist);
		fwscsi_data_valid = 1;
	}
	sc->sc_adapter_link.adapter_target = 7;
	sc->sc_adapter_link.adapter_buswidth = 8;
	sc->sc_adapter_link.openings = 2;
	sc->sc_adapter_link.device = &fwscsi_dev;
	sc->sc_adapter_link.device_softc = sc;
	sc->sc_adapter_link.adapter = &fwscsi_switch;
	sc->sc_adapter_link.adapter_softc = sc;
	sc->sc_adapter_link.flags = 0;
	sc->sc_adapter_link.inquiry_flags = 0;
	sc->sc_adapter_link.inquiry_flags2 = 0;
	sc->sc_adapter_link.quirks |=
	    SDEV_NOLUNS | SDEV_NOTAGS | SDEV_NOCDB6;

	sc->sc_speed = fwsc->sc_sc1394.sc1394_link_speed;
	sc->sc_maxpayload = fwsc->sc_sc1394.sc1394_max_receive - 1;

	sc->sc_lun = 0;

	sc->sc_unitdir = malloc(sc->sc_units * sizeof(*sc->sc_unitdir),
	    M_DEVBUF, M_NOWAIT);
	MPRINTF("malloc(DEVBUF)", sc->sc_unitdir);
	if (sc->sc_unitdir == NULL) {
		printf("%s: memory allocation failure.\n", sc->sc_dev.dv_xname);
		return;
	}
	bcopy(udir, sc->sc_unitdir, sc->sc_units * sizeof(*sc->sc_unitdir));

	lun = sbp2_init(fwsc, *sc->sc_unitdir);
	if (lun < 0) {
		DPRINTF(("%s: initialization failure... (-1)\n", __func__));
		return;
	}
	sc->sc_lun = (u_int16_t)lun;

#ifdef	FWSCSI_DEBUG
	if (fwscsidebug & 4)
		Debugger();
#endif	/* FWSCSI_DEBUG */

	login_orb = malloc(sizeof(struct sbp2_login_orb), M_1394DATA, M_WAITOK);
	MPRINTF("malloc(1394DATA)", login_orb);
	bzero(login_orb, sizeof(struct sbp2_login_orb));

	login_orb->lun = htons(sc->sc_lun);
	sbp2_login(fwsc, login_orb, fwscsi_login_cb, (void *)sc);
}

void
fwscsi_login_cb(void *arg, struct sbp2_status_notification *notification)
{
	struct fwscsi_softc * const sc = arg;
	struct sbp2_login_orb *login_orb =
	    (struct sbp2_login_orb *)notification->origin;
#ifdef	FWSCSI_DEBUG
	struct sbp2_status_block *status = notification->status;
	int i;

	DPRINTF(("%s: cpl = %d(%08x)\n", __func__, cpl, cpl));

	DPRINTF(("%s: origin=0x%08x csr=0x%016qx", __func__,
	    (u_int32_t)login_orb,
	    ((u_int64_t)(ntohs(status->orb_offset_hi)) << 32) +
	    ntohl(status->orb_offset_lo)));

	for (i = 0; i < sizeof(*status); i++) {
		DPRINTFN(1, ("%s %02.2x", (i % 16)?"":"\n   ",
		    ((u_int8_t *)status)[i]));
	}
	DPRINTF(("\n"));
#endif	/* FWSCSI_DEBUG */

	if (login_orb != NULL) {
		free(login_orb, M_1394DATA);
		MPRINTF("free(1394DATA)", login_orb);
		login_orb = NULL;	/* XXX */
	}

	sc->sc_bus = NULL;

#ifdef	FWSCSI_DEBUG
	if (fwscsidebug & 4)
		Debugger();
#endif	/* FWSCSI_DEBUG */

	if (kthread_create(fwscsi_config_thread, sc, NULL, "%s",
	    sc->sc_dev.dv_xname))
	{
		printf("%s: unable to create config thread\n",
		    sc->sc_dev.dv_xname);
	}
}

void
fwscsi_config_thread(void *arg)
{
	struct device *dev;
	struct fwscsi_softc * const sc = arg;

	dev = config_found((void *)sc, &sc->sc_adapter_link, scsiprint);
	sc->sc_bus = (struct scsibus_softc *)dev;
	if (sc->sc_bus != NULL) {
		sc->sc_bus->moreluns = 0;
		sc->sc_bus->sc_buswidth = 8;
	}

	DPRINTF(("%s: exiting...\n", __func__));

	kthread_exit(0);
}

void
fwscsi_status_notify(void *arg, struct sbp2_status_notification *notification)
{
	struct sbp2_status_block *status = notification->status;
	void *wakemeup = notification->origin;

	DPRINTF(("%s: origin=0x%08x csr=0x%016qx\n", __func__,
	    (u_int32_t)wakemeup,
	    ((u_int64_t)(ntohs(status->orb_offset_hi)) << 32) +
	    ntohl(status->orb_offset_lo)));

	if (wakemeup != NULL) {
		((void **)wakemeup)[0] = status;
		DPRINTF(("%s: Wake-up 0x%08x\n", __func__,
		    (u_int32_t)wakemeup));
		wakeup(wakemeup);
	}
}

int
fwscsi_detach(struct device *self, int flags)
{
	struct fwscsi_softc *sc = (struct fwscsi_softc *)self;
	struct fwnode_softc *fwsc =
	    (struct fwnode_softc *)sc->sc_dev.dv_parent;
	struct device *dev = (struct device *)sc->sc_bus;
	int s, rv;

	DPRINTF(("%s: cpl = %d(%08x)\n", __func__, cpl, cpl));

	rv = 0;

	if (sc->sc_bus) {
		DPRINTF(("%s: detach %s\n", __func__,
		    sc->sc_bus->dv_xname));
		s = splbio();
		rv += config_detach(dev, flags);
		splx(s);
	}
	if (sc->sc_unitdir) {
		sbp2_clean(fwsc, *sc->sc_unitdir, 0);
		free(sc->sc_unitdir, M_DEVBUF);
		MPRINTF("free(DEVBUF)", sc->sc_unitdir);
		sc->sc_unitdir = NULL;	/* XXX */
	}

	return (rv);
}

#ifdef	__NetBSD__
void
fwscsi_scsipi_request(struct scsipi_channel *channel,
    scsipi_adapter_req_t req, void *arg)
{
	/*struct scsipi_adapter *adapt = channel->chan_adapter;
	struct fwscsi_softc *sc = (struct fwscsi_softc *)adapt->adapt_dev;*/
	struct scsipi_xfer *xs = arg;
	int i;

	DPRINTF(("Called fwscsi_scsipi_request\n"));

	switch (req) {
	case ADAPTER_REQ_RUN_XFER:
		xs->error = XS_DRIVER_STUFFUP;
		DPRINTF(("Got req_run_xfer\n"));
		DPRINTF(("xs control: 0x%08x, timeout: %d\n", xs->xs_control,
		    xs->timeout));
		DPRINTF(("opcode: 0x%02x\n", (u_int8_t)xs->cmd->opcode));
		for (i = 0; i < 15; i++)
			DPRINTF(("0x%02.2x ",(u_int8_t)xs->cmd->bytes[i]));
		DPRINTF(("\n"));
		scsipi_done(xs);
		break;
	case ADAPTER_REQ_GROW_RESOURCES:
		DPRINTF(("Got req_grow_resources\n"));
		break;
	case ADAPTER_REQ_SET_XFER_MODE:
		DPRINTF(("Got set xfer mode\n"));
		break;
	default:
		panic("Unknown request: %d\n", (int)req);
	}
}
#else
int
fwscsi_scsi_cmd(struct scsi_xfer *xs)
{
	struct sbp2_command_orb *cmd_orb;
	struct fwscsi_orb_data *data_elm;
	struct fwscsi_softc *sc =
	    (struct fwscsi_softc *)xs->sc_link->adapter_softc;
	struct fwnode_softc *fwsc =
	    (struct fwnode_softc *)sc->sc_dev.dv_parent;
	struct fwohci_softc *ohsc;
	struct ieee1394_abuf *data_ab;
	u_int32_t dhash, hash_mask;
	u_int16_t options, host_id;
	size_t datalen;
	int datashift, s;
#ifdef	FWSCSI_DEBUG
	struct uio *data_uio;
	int i;

	DPRINTF(("%s: cpl:%d(%x), xs:0x%08x\n", __func__, cpl, cpl, xs));
	DPRINTF(("  flags:%05x retries:%d timeout:%d target:%d lun:%d\n",
	    xs->flags, xs->retries, xs->timeout,
	    xs->sc_link->target, xs->sc_link->lun));
	DPRINTF(("  cmd[%d]:", xs->cmdlen));
	for (i=0; i<xs->cmdlen; i++)
		DPRINTF((" %02.2x", ((u_int8_t *)xs->cmd)[i]));
	DPRINTF(("\n  data[%u]:0x%08x", xs->datalen, (caddr_t)xs->data));
	if (xs->flags & SCSI_DATA_UIO) {
		data_uio = (struct uio *)xs->data;
		DPRINTF(("/UIO:0x%08x(%d)/0x%08x", (u_int32_t)data_uio->uio_iov,
		    data_uio->uio_iovcnt, data_uio->uio_resid));
	}
	if (xs->bp != NULL)
		DPRINTF((" buf[%u/%u]:0x%08x", xs->bp->b_bcount,
		    xs->bp->b_bufsize, (u_int32_t)xs->bp->b_data));
	DPRINTF(("\n"));
	if (xs->flags & SCSI_DATA_UIO) {
		for (i=0; i<data_uio->uio_iovcnt; i++)
			DPRINTFN(1,("  uio_segment[%d]: 0x%p(%d)\n", i,
			    (void *)data_uio->uio_iov[i].iov_base, 
			    data_uio->uio_iov[i].iov_len));
	}
#endif	/* FWSCSI_DEBUG */

	s = splbio();

	/* Always reset xs->stimeout, lest we timeout_del() with trash. */
	timeout_set(&xs->stimeout, fwscsi_command_timeout, (void *)xs);
	bzero(&xs->sense, sizeof(struct scsi_mode_sense));

	/* Don't probe for unsupported targets. */
	if (xs->sc_link->target != sc->sc_lun || xs->sc_link->lun != 0) {
		DPRINTF(("    device not available...\n"));
		xs->error = XS_SELTIMEOUT;
		xs->status = SCSI_CHECK;
		xs->flags |= ITSDONE | SCSI_SILENT;
		xs->sense.flags = SKEY_ILLEGAL_REQUEST;
		xs->sense.add_sense_code = 0x25; /* LOGIC UNIT NOT SUPPORTED */
		xs->sense.error_code = 0x70;
		scsi_done(xs);
		splx(s);
		return (COMPLETE);
	}

	cmd_orb = malloc(sizeof(struct sbp2_command_orb) + 8,
	    M_1394DATA, M_NOWAIT);
	if (cmd_orb == NULL) {
		printf("%s: can't alloc cmd_orb for target %d lun %d\n",
		    sc->sc_dev.dv_xname, xs->sc_link->target,
		    xs->sc_link->lun);
		xs->error = XS_DRIVER_STUFFUP;
		splx(s);
		return (TRY_AGAIN_LATER);
	}
	MPRINTF("malloc(1394DATA)", cmd_orb);
	bzero(cmd_orb, sizeof(struct sbp2_command_orb) + 8);

	options = 0x8000 | ((fwsc->sc_sc1394.sc1394_link_speed & 0x7) << 8);

	datalen = 0;
	if (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
		ohsc = (struct fwohci_softc *)
		    ((struct device *)fwsc)->dv_parent;
		host_id = ohsc->sc_nodeid;
		DPRINTFN(1, ("%s: host=0x%04hx data=0x%08x[%d/%d]",
		    __func__, host_id, (u_int32_t)(xs->data),
		    xs->datalen, xs->resid));
		datashift = 0;
		datalen = xs->datalen / (1 << SBP2_DATA_SHIFT);
		while (datalen) {
			datashift++;
			datalen /= 2;
		}
		hash_mask = ~(1 << datashift) + 1;
		do {
			dhash = arc4random();
		} while (!dhash || fwscsi_datafind(sc, dhash) != NULL);

		MALLOC(data_elm, struct fwscsi_orb_data *, sizeof(*data_elm),
		    M_1394CTL, M_NOWAIT);
		MPRINTF("MALLOC(1394CTL)", data_elm);
		if (data_elm == NULL) {
			printf("%s: can't alloc data_elm for target %d lun"
			    " %d\n", sc->sc_dev.dv_xname, xs->sc_link->target,
			    xs->sc_link->lun);
			xs->error = XS_DRIVER_STUFFUP;
			free(cmd_orb, M_1394DATA);
			MPRINTF("free(1394DATA)", cmd_orb);
			splx(s);
			return (TRY_AGAIN_LATER);
		}
		bzero(data_elm, sizeof(*data_elm));

		MALLOC(data_ab, struct ieee1394_abuf *, sizeof(*data_ab),
		    M_1394DATA, M_NOWAIT);
		MPRINTF("MALLOC(1394DATA)", data_ab);
		if (data_ab == NULL) {
			printf("%s: can't alloc data_ab for target %d lun"
			    " %d\n", sc->sc_dev.dv_xname, xs->sc_link->target,
			    xs->sc_link->lun);
			xs->error = XS_DRIVER_STUFFUP;
			free(data_elm, M_1394CTL);
			MPRINTF("FREE(1394CTL)", data_elm);
			free(cmd_orb, M_1394DATA);
			MPRINTF("free(1394DATA)", cmd_orb);
			splx(s);
			return (TRY_AGAIN_LATER);
		}
		bzero(data_ab, sizeof(*data_ab));

		data_elm->data_hash = dhash;
		data_elm->data_mask = hash_mask;
		data_elm->data_addr = xs->data;
		data_elm->data_len = xs->datalen;

		data_ab->ab_req = (struct ieee1394_softc *)fwsc;
		data_ab->ab_retlen = 0;
		datalen = roundup(xs->datalen, 4);
		data_ab->ab_length = datalen & 0xffff;
		data_ab->ab_addr = SBP2_DATA_BLOCK +
		    ((u_int64_t)(dhash & hash_mask) << SBP2_DATA_SHIFT);
		data_ab->ab_cb = fwscsi_command_data;
		data_ab->ab_cbarg = xs;

		TAILQ_INSERT_TAIL(&fwscsi_datalist, data_elm, data_chain);
		xs->data = (u_char *)data_elm;

		/* Check direction of data transfer. */
		if (xs->flags & SCSI_DATA_OUT) {
			data_ab->ab_tcode =
			    IEEE1394_TCODE_READ_REQUEST_DATABLOCK;
			options |= (7 + fwsc->sc_sc1394.sc1394_link_speed) << 4;
			DPRINTFN(1, (" -- OUT(%d/%X)\n", datalen,
			    (options >> 4) & 0xf));
		} else {
			data_ab->ab_tcode =
			    IEEE1394_TCODE_WRITE_REQUEST_DATABLOCK;
			options |= 0x0800;
			options |= (7 + fwsc->sc_sc1394.sc1394_link_speed) << 4;
			DPRINTFN(1, (" -- IN(%d/%X)\n", datalen,
			    (options >> 4) & 0xf));
		}
#ifdef	FWSCSI_DEBUG
		for (i=0; i<xs->datalen; i++) {
			DPRINTFN(2, ("%s %02.2x", (i % 16)?"":"\n   ",
			    data_elm->data_addr[i]));
		}
		DPRINTFN(2, ("\n"));
#endif	/* FWSCSI_DEBUG */
		fwsc->sc1394_inreg(data_ab, TRUE);
		data_elm->data_ab = data_ab;

		cmd_orb->data_descriptor.node_id = htons(host_id);
		cmd_orb->data_descriptor.hi =
		    htons((u_int16_t)(data_ab->ab_addr >> 32));
		cmd_orb->data_descriptor.lo =
		    htonl((u_int32_t)(data_ab->ab_addr & 0xFFFFFFFF));
		cmd_orb->data_size = htons(datalen & 0xFFFF);
	}

	cmd_orb->options = htons(options);

	bcopy(xs->cmd, (void*)(&cmd_orb->command_block[0]), xs->cmdlen);
	xs->cmd = (struct scsi_generic *) cmd_orb;

	timeout_add(&xs->stimeout, (xs->timeout * hz) / 1000);
	sbp2_command_add(fwsc, sc->sc_lun, cmd_orb, 8, xs->data,
	    fwscsi_command_wait, (void *)xs);

	splx(s);
	return (SUCCESSFULLY_QUEUED);
}

void
fwscsi_command_timeout(void *arg)
{
	struct sbp2_status_notification notification;
	struct sbp2_status_block *status;
	int s;

	DPRINTF(("%s: cpl = %d(%08x)\n", __func__, cpl, cpl));

	MALLOC(status, struct sbp2_status_block *, sizeof(*status),
	    M_1394DATA, M_WAITOK);
	bzero(status, sizeof(*status));

	status->flags = 0x41;
	status->flags |= SBP2_STATUS_RESP_TRANS_FAILURE | SBP2_STATUS_DEAD;
	status->status = SBP2_STATUS_OBJECT_ORB | SBP2_STATUS_SERIAL_TIMEOUT;

	notification.origin = ((struct scsi_xfer *)arg)->cmd;
	notification.status = status;
	s = spl0();
	fwscsi_command_wait(arg, &notification);
	splx(s);
}

void
fwscsi_command_wait(void *aux, struct sbp2_status_notification *notification)
{
	struct scsi_xfer *xs = (struct scsi_xfer *)aux;
	struct fwscsi_orb_data *data_elm = (struct fwscsi_orb_data *)xs->data;
	struct fwscsi_softc *sc = xs->sc_link->adapter_softc;
	struct fwnode_softc *fwsc =
	    (struct fwnode_softc *)sc->sc_dev.dv_parent;
	struct sbp2_command_orb *cmd_orb;
	struct ieee1394_abuf *data_ab;
	struct fwscsi_status *status = NULL;
	const char *error_str;
	u_int32_t tmp;
	int s;
#ifdef	FWSCSI_DEBUG
	void *orb = notification->origin;
	int i;
#endif	/* FWSCSI_DEBUG */

	splassert(IPL_NONE);
	DPRINTF(("%s: cpl = %d(%08x)\n", __func__, cpl, cpl));

	s = splbio();
	timeout_del(&xs->stimeout);
	splx(s);
	status = (struct fwscsi_status *)notification->status;

	DPRINTF(("%s: origin=0x%08x csr=0x%016qx", __func__,
	    (u_int32_t)orb,
	    ((u_int64_t)(ntohs(status->orb_offset_hi)) << 32) +
	    ntohl(status->orb_offset_lo)));
#ifdef	FWSCSI_DEBUG
	for (i = 0; i < sizeof(*status); i++) {
		DPRINTFN(2, ("%s %02.2x", (i % 16)?"":"\n   ",
		    ((u_int8_t *)status)[i]));
	}
	DPRINTFN(2, ("\n"));
#endif	/* FWSCSI_DEBUG */

	cmd_orb = (struct sbp2_command_orb *)(xs->cmd);
	xs->cmd = &xs->cmdstore;

	if (data_elm != NULL) {
		data_ab = data_elm->data_ab;
		if (data_ab) {
			data_ab->ab_addr = SBP2_DATA_BLOCK +
			    ((u_int64_t)(data_elm->data_hash &
			     data_elm->data_mask) << SBP2_DATA_SHIFT);
			fwsc->sc1394_unreg(data_ab, TRUE);
			if ((void *)data_ab->ab_data > (void *)1) { /* XXX */
				free(data_ab->ab_data, M_1394DATA);
				MPRINTF("free(1394DATA)", data_ab->ab_data);
				data_ab->ab_data = NULL;	/* XXX */
			}
			FREE(data_ab, M_1394DATA);
			MPRINTF("FREE(1394DATA)", data_ab);
			data_ab = NULL;	/* XXX */
		}

		xs->data = data_elm->data_addr;
		s = splbio();
		TAILQ_REMOVE(&fwscsi_datalist, data_elm, data_chain);
		splx(s);
		FREE(data_elm, M_1394CTL);
		MPRINTF("FREE(1394CTL)", data_elm);
		data_elm = NULL;	/* XXX */
	}

	if (status != NULL &&
	    ((status->flags & SBP2_STATUS_RESPONSE_MASK) ==
	        SBP2_STATUS_RESP_REQ_COMPLETE ||
	     (status->flags & SBP2_STATUS_RESPONSE_MASK) ==
	        SBP2_STATUS_RESP_VENDOR) &&
	    status->status != 0) {
		DPRINTF(("%s: sbp_status 0x%02x, scsi_status 0x%02x --> XS_SENSE\n",
		    __func__, status->status, status->scsi_status));
		xs->error = XS_SENSE;
		xs->status = status->scsi_status & 0x3F;
		xs->sense.error_code = 0x70;
		xs->sense.flags = (status->sense_key & FWSCSI_SENSE_KEY) |
		    ((status->sense_key & FWSCSI_MEI) << 1);
		if (status->sense_key & FWSCSI_INFO_VALID) {
			tmp = ntohl(status->information);
			bcopy(&tmp, &xs->sense.info, sizeof(u_int32_t));
			xs->sense.error_code |= 0x80;
		}
		xs->sense.add_sense_code = status->sense_code;
		xs->sense.add_sense_code_qual = status->sense_qual;
		tmp = ntohl(status->sense_key_info);
		bcopy(&tmp, &xs->sense.fru, sizeof(u_int32_t));
		tmp = ntohl(status->cmd_spec_info);
		bcopy(&tmp, &xs->sense.cmd_spec_info, sizeof(u_int32_t));
		tmp = ntohl(status->vendor_info[0]);
		bcopy(&tmp, &xs->sense.extra_bytes[0], sizeof(u_int32_t));
		tmp = ntohl(status->vendor_info[1]);
		bcopy(&tmp, &xs->sense.extra_bytes[4], sizeof(u_int32_t));
	} else if (status != NULL &&
	    ((status->flags & SBP2_STATUS_RESPONSE_MASK) ==
	        SBP2_STATUS_RESP_TRANS_FAILURE ||
	     (status->flags & SBP2_STATUS_RESPONSE_MASK) ==
	        SBP2_STATUS_RESP_ILLEGAL_REQ)) {

		DPRINTF(("%s: device error (flags 0x%02x, status 0x%02x) --> XS_SENSE\n",
		    __func__, status->flags, status->status));
		xs->error = XS_SENSE;
		xs->status = SCSI_CHECK;
		xs->flags |= ITSDONE;
		if ((status->flags & SBP2_STATUS_RESPONSE_MASK) ==
		    SBP2_STATUS_RESP_TRANS_FAILURE) {
			xs->sense.flags = SKEY_HARDWARE_ERROR;
			xs->sense.add_sense_code = status->status;
		} else {
			xs->sense.flags = SKEY_ILLEGAL_REQUEST;
			xs->sense.add_sense_code = 0x0;
		}

		FREE(status, M_1394DATA);
		MPRINTF("FREE(1394DATA)", status);
		status = NULL;	/* XXX */
		sbp2_command_del(fwsc, sc->sc_lun, cmd_orb);
		free(cmd_orb, M_1394DATA);
		MPRINTF("free(1394DATA)", cmd_orb);
		cmd_orb = NULL;	/* XXX */

		s = splbio();
		scsi_done(xs);
		splx(s);

		return;
	}

	/*
	 * Now, if we've come here with no error code, i.e. we've kept the
	 * initial XS_NOERROR, and the status code signals that we should
	 * check sense, we'll need to set up a request sense cmd block and
	 * push the command back into the ready queue *before* any other
	 * commands for this target/lunit, else we lose the sense info.
	 * We don't support chk sense conditions for the request sense cmd.
	 */
	if (xs->error == XS_NOERROR) {
		if (status->flags & SBP2_STATUS_RESPONSE_MASK) {
			xs->error = XS_SENSE;
			error_str = "SENSE";
		} else if (status->flags & SBP2_STATUS_DEAD) {
			xs->error = XS_DRIVER_STUFFUP;
			error_str = "DRIVER_STUFFUP";
		} else {
			error_str = "NOERROR";
		}
		DPRINTF((" --> XS_%s\n", error_str));
	}

	if (status != NULL) {
		DPRINTF(("%s: Free status(0x%08x)\n", __func__,
		    (u_int32_t)status));
		FREE(status, M_1394DATA);
		MPRINTF("FREE(1394DATA)", status);
		status = NULL;	/* XXX */
	}

	if (cmd_orb != NULL) {
		DPRINTF(("%s: Nullify orb(0x%08x)\n", __func__,
		    (u_int32_t)cmd_orb));
		cmd_orb->options = htons(SBP2_DUMMY_TYPE);
		sbp2_command_del(fwsc, sc->sc_lun, cmd_orb);
		free(cmd_orb, M_1394DATA);
		MPRINTF("free(1394DATA)", cmd_orb);
		cmd_orb = NULL;	/* XXX */
	}

	xs->flags |= ITSDONE;
	xs->resid = 0;

	s = splbio();
	scsi_done(xs);
	splx(s);
}

void
fwscsi_command_data(struct ieee1394_abuf *ab, int rcode)
{
	struct fwscsi_orb_data *data_elm;
	struct ieee1394_abuf *data_ab;
	struct fwnode_softc *sc = (struct fwnode_softc *)ab->ab_req;
	struct scsi_xfer *xs = ab->ab_cbarg;
	size_t datalen;
	caddr_t dataptr;
#ifdef	FWSCSI_DEBUG
	int i;
#endif	/* FWSCSI_DEBUG */

	DPRINTF(("%s: cpl = %d(%08x)\n", __func__, cpl, cpl));

	if (rcode || (xs == NULL)) {
#ifdef	FWSCSI_DEBUG
		DPRINTF(("%s: Bad return code: %d\n", __func__, rcode));
#endif	/* FWSCSI_DEBUG */
		if ((void *)ab->ab_data > (void *)1) {		/* XXX */
			free(ab->ab_data, M_1394DATA);
			MPRINTF("free(1394DATA)", ab->ab_data);
			ab->ab_data = NULL;
		}
		return;
	}

	data_elm = (struct fwscsi_orb_data *)xs->data;

	datalen = MIN(ab->ab_retlen, ab->ab_length);
	dataptr = data_elm->data_addr +
	    (size_t)((ab->ab_addr & SBP2_DATA_MASK) -
	     ((u_int64_t)(data_elm->data_hash & data_elm->data_mask)
	      << SBP2_DATA_SHIFT));

	if ((dataptr < data_elm->data_addr) ||
	    ((dataptr + datalen) >
	     (data_elm->data_addr + roundup(data_elm->data_len, 4)))) {
		DPRINTF(("%s: Data (0x%08x[%d]) out of range (0x%08x[%d])\n",
		    __func__, dataptr, datalen, data_elm->data_addr,
		    data_elm->data_len));
		if ((void *)ab->ab_data > (void *)1) {		/* XXX */
			free(ab->ab_data, M_1394DATA);
			MPRINTF("free(1394DATA)", ab->ab_data);
			ab->ab_data = NULL;
		}
		return;
	}

	DPRINTF(("%s: tcode:%d data:0x%08x[%d/%d/%d/%d]",
	    __func__, ab->ab_tcode, dataptr, ab->ab_length,
	    ab->ab_retlen, data_elm->data_len, xs->resid));

	switch (ab->ab_tcode) {

	/* Got a read so allocate the buffer and write out the response. */
	case	IEEE1394_TCODE_READ_REQUEST_DATABLOCK:

		MALLOC(data_ab, struct ieee1394_abuf *, sizeof(*data_ab),
		    M_1394DATA, M_WAITOK);
		MPRINTF("MALLOC(1394DATA)", data_ab);
		bcopy(ab, data_ab, sizeof(*data_ab));

		data_ab->ab_data = malloc(datalen, M_1394DATA, M_WAITOK);
		MPRINTF("malloc(1394DATA)", data_ab->ab_data);
		bzero(data_ab->ab_data, datalen);
		bcopy(dataptr, data_ab->ab_data,
		    MIN(data_elm->data_len, datalen));

		data_ab->ab_retlen = 0;
		data_ab->ab_cb = NULL;
		data_ab->ab_cbarg = NULL;
		data_ab->ab_tcode = IEEE1394_TCODE_READ_RESPONSE_DATABLOCK;
		data_ab->ab_length = datalen;

#ifdef	FWSCSI_DEBUG
		for (i = 0; i < data_ab->ab_length; i++) {
			DPRINTFN(1, ("%s %02.2x", (i % 16)?"":"\n   ",
			    ((u_int8_t *)data_ab->ab_data)[i]));
		}
#endif	/* FWSCSI_DEBUG */

		sc->sc1394_write(data_ab);

		break;

	case	IEEE1394_TCODE_WRITE_REQUEST_DATABLOCK:

#ifdef	FWSCSI_DEBUG
		for (i = 0; i < ab->ab_retlen; i++) {
			DPRINTFN(1, ("%s %02.2x", (i % 16)?"":"\n   ",
			    ((u_int8_t *)ab->ab_data)[i]));
		}
#endif	/* FWSCSI_DEBUG */

		bcopy(ab->ab_data, dataptr, datalen);

		break;

	default:
		break;
	}
	DPRINTF(("\n"));

	if (xs->resid <= datalen) {
		xs->resid = 0;
		DPRINTFN(1, ("%s: Data block complete", __func__));
	} else {
		xs->resid -= datalen;
		DPRINTFN(1, ("%s: Wait more", __func__));
	}
	DPRINTFN(1, (" -- resid = %d\n", xs->resid));
}
#endif

#ifdef	__NetBSD__
void
fwscsi_scsipi_minphys(struct buf *buf)
#else
void
fwscsi_minphys(struct buf *buf)
#endif
{
	DPRINTF(("%s: cpl = %d(%08x)\n", __func__, cpl, cpl));

#if 0	/* XXX - Only small transfers, until we implement data pages. */
	if (buf->b_bcount > SBP2_MAX_TRANS)
		buf->b_bcount = SBP2_MAX_TRANS;
#else
	if (buf->b_bcount > 0x8000)
		buf->b_bcount = 0x8000;
#endif

	minphys(buf);
}
