/*	$OpenBSD: arc.c,v 1.42 2006/08/25 02:48:16 dlg Exp $ */

/*
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#if NBIO > 0
#include <sys/ioctl.h>
#include <dev/biovar.h>
#endif

#ifdef ARC_DEBUG
#define ARC_D_INIT	(1<<0)
#define ARC_D_RW	(1<<1)
#define ARC_D_DB	(1<<2)

int arcdebug = 0;

#define DPRINTF(p...)		do { if (arcdebug) printf(p); } while (0)
#define DNPRINTF(n, p...)	do { if ((n) & arcdebug) printf(p); } while (0)

#else
#define DPRINTF(p...)		/* p */
#define DNPRINTF(n, p...)	/* n, p */
#endif

static const struct pci_matchid arc_devices[] = {
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1110 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1120 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1130 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1160 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1170 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1210 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1220 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1230 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1260 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1270 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1280 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1380 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1381 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1680 },
	{ PCI_VENDOR_ARECA,	PCI_PRODUCT_ARECA_ARC1681 }
};

#define ARC_PCI_BAR			PCI_MAPREG_START

#define ARC_REG_INB_MSG0		0x0010
#define  ARC_REG_INB_MSG0_NOP			(0x00000000)
#define  ARC_REG_INB_MSG0_GET_CONFIG		(0x00000001)
#define  ARC_REG_INB_MSG0_SET_CONFIG		(0x00000002)
#define  ARC_REG_INB_MSG0_ABORT_CMD		(0x00000003)
#define  ARC_REG_INB_MSG0_STOP_BGRB		(0x00000004)
#define  ARC_REG_INB_MSG0_FLUSH_CACHE		(0x00000005)
#define  ARC_REG_INB_MSG0_START_BGRB		(0x00000006)
#define  ARC_REG_INB_MSG0_CHK331PENDING		(0x00000007)
#define  ARC_REG_INB_MSG0_SYNC_TIMER		(0x00000008)
#define ARC_REG_INB_MSG1		0x0014
#define ARC_REG_OUTB_ADDR0		0x0018
#define ARC_REG_OUTB_ADDR1		0x001c
#define  ARC_REG_OUTB_ADDR1_FIRMWARE_OK		(1<<31)
#define ARC_REG_INB_DOORBELL		0x0020
#define  ARC_REG_INB_DOORBELL_WRITE_OK		(1<<0)
#define  ARC_REG_INB_DOORBELL_READ_OK		(1<<1)
#define ARC_REG_OUTB_DOORBELL		0x002c
#define  ARC_REG_OUTB_DOORBELL_WRITE_OK		(1<<0)
#define  ARC_REG_OUTB_DOORBELL_READ_OK		(1<<1)
#define ARC_REG_INTRSTAT		0x0030
#define  ARC_REG_INTRSTAT_MSG0			(1<<0)
#define  ARC_REG_INTRSTAT_MSG1			(1<<1)
#define  ARC_REG_INTRSTAT_DOORBELL		(1<<2)
#define  ARC_REG_INTRSTAT_POSTQUEUE		(1<<3)
#define  ARC_REG_INTRSTAT_PCI			(1<<4)
#define ARC_REG_INTRMASK		0x0034
#define  ARC_REG_INTRMASK_MSG0			(1<<0)
#define  ARC_REG_INTRMASK_MSG1			(1<<1)
#define  ARC_REG_INTRMASK_DOORBELL		(1<<2)
#define  ARC_REG_INTRMASK_POSTQUEUE		(1<<3)
#define  ARC_REG_INTRMASK_PCI			(1<<4)
#define ARC_REG_POST_QUEUE		0x0040
#define  ARC_REG_POST_QUEUE_ADDR_SHIFT		5
#define  ARC_REG_POST_QUEUE_IAMBIOS		(1<<30)
#define  ARC_REG_POST_QUEUE_BIGFRAME		(1<<31)
#define ARC_REG_REPLY_QUEUE		0x0044
#define  ARC_REG_REPLY_QUEUE_ADDR_SHIFT		5
#define  ARC_REG_REPLY_QUEUE_ERR		(1<<28)
#define  ARC_REG_REPLY_QUEUE_IAMBIOS		(1<<30)
#define ARC_REG_MSGBUF			0x0a00
#define  ARC_REG_MSGBUF_LEN		1024
#define ARC_REG_IOC_WBUF_LEN		0x0e00
#define ARC_REG_IOC_WBUF		0x0e04
#define ARC_REG_IOC_RBUF_LEN		0x0f00
#define ARC_REG_IOC_RBUF		0x0f04
#define  ARC_REG_IOC_RWBUF_MAXLEN	124 /* for both RBUF and WBUF */

struct arc_msg_firmware_info {
	u_int32_t		signature;
#define ARC_FWINFO_SIGNATURE_GET_CONFIG		(0x87974060)
	u_int32_t		request_len;
	u_int32_t		queue_len;
	u_int32_t		sdram_size;
	u_int32_t		sata_ports;
	u_int8_t		vendor[40];
	u_int8_t		model[8];
	u_int8_t		fw_version[16];
	u_int8_t		device_map[16];
} __packed;

struct arc_msg_scsicmd {
	u_int8_t		bus;
	u_int8_t		target;
	u_int8_t		lun;
	u_int8_t		function;

	u_int8_t		cdb_len;
	u_int8_t		sgl_len;
	u_int8_t		flags;
#define ARC_MSG_SCSICMD_FLAG_SGL_BSIZE_512	(1<<0)
#define ARC_MSG_SCSICMD_FLAG_FROM_BIOS		(1<<1)
#define ARC_MSG_SCSICMD_FLAG_WRITE		(1<<2)
#define ARC_MSG_SCSICMD_FLAG_SIMPLEQ		(0x00)
#define ARC_MSG_SCSICMD_FLAG_HEADQ		(0x08)
#define ARC_MSG_SCSICMD_FLAG_ORDERQ		(0x10)
	u_int8_t		reserved;

	u_int32_t		context;
	u_int32_t		data_len;

#define ARC_MSG_CDBLEN				16
	u_int8_t		cdb[ARC_MSG_CDBLEN];

	u_int8_t		status;
#define ARC_MSG_STATUS_SELTIMEOUT		0xf0
#define ARC_MSG_STATUS_ABORTED			0xf1
#define ARC_MSG_STATUS_INIT_FAIL		0xf2
#define ARC_MSG_SENSELEN			15
	u_int8_t		sense_data[ARC_MSG_SENSELEN];

	/* followed by an sgl */
} __packed;

struct arc_sge {
	u_int32_t		sg_hdr;
#define ARC_SGE_64BIT				(1<<24)
	u_int32_t		sg_lo_addr;
	u_int32_t		sg_hi_addr;
} __packed;

#define ARC_MAX_TARGET		16
#define ARC_MAX_LUN		8
#define ARC_MAX_IOCMDLEN	512
#define ARC_BLOCKSIZE		512

/* the firmware deals with up to 256 or 512 byte command frames. */
/* sizeof(struct arc_msg_scsicmd) + (sizeof(struct arc_sge) * 38) == 508 */
#define ARC_SGL_MAXLEN		38
/* sizeof(struct arc_msg_scsicmd) + (sizeof(struct arc_sge) * 17) == 252 */
#define ARC_SGL_256LEN		17

struct arc_io_cmd {
	struct arc_msg_scsicmd	cmd;
	struct arc_sge		sgl[ARC_SGL_MAXLEN];
} __packed;

/* definitions of the firmware commands sent via the doorbells */

struct arc_fw_hdr {
	u_int8_t		byte1;
	u_int8_t		byte2;
	u_int8_t		byte3;
} __packed;

/* the fw header must always equal this */
struct arc_fw_hdr arc_fw_hdr = { 0x5e, 0x01, 0x61 };

struct arc_fw_bufhdr {
	struct arc_fw_hdr	hdr;
	u_int16_t		len;
} __packed;

#define ARC_FW_RAIDINFO		0x20	/* opcode + raid# */
#define ARC_FW_VOLINFO		0x21	/* opcode + vol# */
#define ARC_FW_DISKINFO		0x22	/* opcode + physdisk# */
#define ARC_FW_SYSINFO		0x23	/* opcode. reply is fw_sysinfo */
#define ARC_FW_MUTE_ALARM	0x30	/* opcode only */
#define ARC_FW_SET_ALARM	0x31	/* opcode + 1 byte for setting */
#define  ARC_FW_SET_ALARM_DISABLE		0x00
#define  ARC_FW_SET_ALARM_ENABLE		0x01
#define ARC_FW_NOP		0x38	/* opcode only */

#define ARC_FW_CMD_OK		0x41

struct arc_fw_comminfo {
	u_int8_t		baud_rate;
	u_int8_t		data_bits;
	u_int8_t		stop_bits;
	u_int8_t		parity;
	u_int8_t		flow_control;
} __packed;

struct arc_fw_scsiattr {
	u_int8_t		channel;// channel for SCSI target (0/1)
        u_int8_t		target;
        u_int8_t		lun;
        u_int8_t		tagged;
        u_int8_t		cache;
        u_int8_t		speed;
} __packed;

struct arc_fw_raidinfo {
	u_int8_t		set_name[16];
	u_int32_t		capacity;
	u_int32_t		capacity2;
	u_int32_t		fail_mask;
	u_int8_t		device_array[32];
	u_int8_t		member_devices;
	u_int8_t		new_member_devices;
	u_int8_t		raid_state;
	u_int8_t		volumes;
	u_int8_t		volume_list[16];
	u_int8_t		reserved1[3];
	u_int8_t		free_segments;
	u_int32_t		raw_stripes[8];
	u_int8_t		reserved2[12];
} __packed;

struct arc_fw_volinfo {
	u_int8_t		set_name[16];
	u_int32_t		capacity;
	u_int32_t		capacity2;
	u_int32_t		fail_mask;
	u_int32_t		stripe_size; /* in blocks */
	u_int32_t		new_fail_mask;
	u_int32_t		new_stripe_size;
	u_int32_t		volume_status;
	u_int32_t		progress;
	struct arc_fw_scsiattr	scsi_attr;
	u_int8_t		member_disks;
	u_int8_t		raid_level;
#define ARC_FW_VOL_RAIDLEVEL_0		0x00
#define ARC_FW_VOL_RAIDLEVEL_1		0x01
#define ARC_FW_VOL_RAIDLEVEL_3		0x02
#define ARC_FW_VOL_RAIDLEVEL_5		0x03
#define ARC_FW_VOL_RAIDLEVEL_6		0x04
#define ARC_FW_VOL_RAIDLEVEL_PASSTHRU	0x05
	u_int8_t		new_member_disks;
	u_int8_t		new_raid_level;
	u_int8_t		raid_set_number;
	u_int8_t		reserved[5];
} __packed;

struct arc_fw_diskinfo {
	u_int8_t		model[40];
	u_int8_t		serial[20];
	u_int8_t		firmware_rev[8];
	u_int32_t		capacity;
	u_int32_t		capacity2;
	u_int8_t		device_state;
	u_int8_t		pio_mode;
	u_int8_t		current_udma_mode;
	u_int8_t		udma_mode;
	u_int8_t		drive_select;
	u_int8_t		raid_number; // 0xff unowned
	struct arc_fw_scsiattr	scsi_attr;
	u_int8_t		reserved[40];
} __packed;

struct arc_fw_sysinfo {
	u_int8_t		vendor_name[40];
	u_int8_t		serial_number[16];
	u_int8_t		firmware_version[16];
	u_int8_t		boot_version[16];
	u_int8_t		mb_version[16];
	u_int8_t		model_name[8];

	u_int8_t		local_ip[4];
	u_int8_t		current_ip[4];

	u_int32_t		time_tick;
	u_int32_t		cpu_speed;
	u_int32_t		icache;
	u_int32_t		dcache;
	u_int32_t		scache;
        u_int32_t		memory_size;
        u_int32_t		memory_speed;
        u_int32_t		events;

        u_int8_t		gsiMacAddress[6];
        u_int8_t		gsiDhcp;

	u_int8_t		alarm;
	u_int8_t		channel_usage;
	u_int8_t		max_ata_mode;
	u_int8_t		sdram_ecc;
	u_int8_t		rebuild_priority;
	struct arc_fw_comminfo	comm_a;
	struct arc_fw_comminfo	comm_b;
	u_int8_t		ide_channels;
	u_int8_t		scsi_host_channels;
	u_int8_t		ide_host_channels;
	u_int8_t		max_volume_set;
	u_int8_t		max_raid_set;
	u_int8_t		ether_port;
	u_int8_t		raid6_engine;
	u_int8_t		reserved[75];
} __packed;

int			arc_match(struct device *, void *, void *);
void			arc_attach(struct device *, struct device *, void *);
int			arc_detach(struct device *, int);
int			arc_intr(void *);

struct arc_ccb;
TAILQ_HEAD(arc_ccb_list, arc_ccb);

struct arc_softc {
	struct device		sc_dev;
	struct scsi_link	sc_link;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	bus_dma_tag_t		sc_dmat;

	void			*sc_ih;

	int			sc_req_count;

	struct arc_dmamem	*sc_requests;
	struct arc_ccb		*sc_ccbs;
	struct arc_ccb_list	sc_ccb_free;

	struct scsibus_softc	*sc_scsibus;

	struct rwlock		sc_lock;
	volatile int		sc_talking;
};
#define DEVNAME(_s)		((_s)->sc_dev.dv_xname)

struct cfattach arc_ca = {
	sizeof(struct arc_softc), arc_match, arc_attach, arc_detach
};

struct cfdriver arc_cd = {
	NULL, "arc", DV_DULL
};

/* interface for scsi midlayer to talk to */
int			arc_scsi_cmd(struct scsi_xfer *);
void			arc_minphys(struct buf *);

struct scsi_adapter arc_switch = {
	arc_scsi_cmd, arc_minphys, NULL, NULL, NULL
};

struct scsi_device arc_dev = {
	NULL, NULL, NULL, NULL
};

/* code to deal with getting bits in and out of the bus space */
u_int32_t		arc_read(struct arc_softc *, bus_size_t);
void			arc_read_region(struct arc_softc *, bus_size_t,
			    void *, size_t);
void			arc_write(struct arc_softc *, bus_size_t, u_int32_t);
void			arc_write_region(struct arc_softc *, bus_size_t,
			    void *, size_t);
int			arc_wait_eq(struct arc_softc *, bus_size_t,
			    u_int32_t, u_int32_t);
int			arc_wait_ne(struct arc_softc *, bus_size_t,
			    u_int32_t, u_int32_t);
int			arc_msg0(struct arc_softc *, u_int32_t);

#define arc_push(_s, _r)	arc_write((_s), ARC_REG_POST_QUEUE, (_r))
#define arc_pop(_s)		arc_read((_s), ARC_REG_REPLY_QUEUE)

/* wrap up the bus_dma api */
struct arc_dmamem {
	bus_dmamap_t		adm_map;
	bus_dma_segment_t	adm_seg;
	size_t			adm_size;
	caddr_t			adm_kva;
};
#define ARC_DMA_MAP(_adm)	((_adm)->adm_map)
#define ARC_DMA_DVA(_adm)	((_adm)->adm_map->dm_segs[0].ds_addr)
#define ARC_DMA_KVA(_adm)	((void *)(_adm)->adm_kva)

struct arc_dmamem	*arc_dmamem_alloc(struct arc_softc *, size_t);
void			arc_dmamem_free(struct arc_softc *,
			    struct arc_dmamem *);

/* stuff to manage a scsi command */
struct arc_ccb {
	struct arc_softc	*ccb_sc;
	int			ccb_id;

	struct scsi_xfer	*ccb_xs;

	bus_dmamap_t		ccb_dmamap;
	bus_addr_t		ccb_offset;
	struct arc_io_cmd	*ccb_cmd;
	u_int32_t		ccb_cmd_post;

	TAILQ_ENTRY(arc_ccb)	ccb_link;
};

int			arc_alloc_ccbs(struct arc_softc *);
struct arc_ccb		*arc_get_ccb(struct arc_softc *);
void			arc_put_ccb(struct arc_softc *, struct arc_ccb *);
int			arc_load_xs(struct arc_ccb *);
int			arc_complete(struct arc_softc *, struct arc_ccb *,
			    int);
void			arc_scsi_cmd_done(struct arc_softc *, struct arc_ccb *,
			    u_int32_t);

/* real stuff for dealing with the hardware */
int			arc_map_pci_resources(struct arc_softc *,
			    struct pci_attach_args *);
int			arc_query_firmware(struct arc_softc *);

/* stuff to do messaging via the doorbells */
void			arc_lock(struct arc_softc *);
void			arc_unlock(struct arc_softc *);
void			arc_wait(struct arc_softc *);
u_int8_t		arc_msg_cksum(void *, u_int16_t);
int			arc_msgbuf(struct arc_softc *, void *, size_t,
			    void *, size_t);

/* bioctl */
#if NBIO > 0
int			arc_bioctl(struct device *, u_long, caddr_t);
int			arc_bio_inq(struct arc_softc *, struct bioc_inq *);
int			arc_bio_vol(struct arc_softc *, struct bioc_vol *);
int			arc_bio_disk(struct arc_softc *, struct bioc_disk *);
int			arc_bio_alarm(struct arc_softc *, struct bioc_alarm *);
int			arc_bio_alarm_state(struct arc_softc *,
			    struct bioc_alarm *);

int			arc_bio_getvol(struct arc_softc *, int,
			    struct arc_fw_volinfo *);
#endif

int
arc_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, arc_devices,
	    sizeof(arc_devices) / sizeof(arc_devices[0])));
}

void
arc_attach(struct device *parent, struct device *self, void *aux)
{
	struct arc_softc		*sc = (struct arc_softc *)self;
	struct pci_attach_args		*pa = aux;
	struct device			*child;

	sc->sc_talking = 0;
	rw_init(&sc->sc_lock, "arcmsg");

	if (arc_map_pci_resources(sc, pa) != 0) {
		/* error message printed by arc_map_pci_resources */
		return;
	}

	if (arc_query_firmware(sc) != 0) {
		/* error message printed by arc_query_firmware */
		return;
	}

	if (arc_alloc_ccbs(sc) != 0) {
		/* error message printed by arc_alloc_ccbs */
		return;
	}

	sc->sc_link.device = &arc_dev;
	sc->sc_link.adapter = &arc_switch;
	sc->sc_link.adapter_softc = sc;
	sc->sc_link.adapter_target = ARC_MAX_TARGET;
	sc->sc_link.adapter_buswidth = ARC_MAX_TARGET;
	sc->sc_link.openings = sc->sc_req_count / ARC_MAX_TARGET;

	child = config_found(self, &sc->sc_link, scsiprint);
	sc->sc_scsibus = (struct scsibus_softc *)child;

	/* XXX enable interrupts */
	arc_write(sc, ARC_REG_INTRMASK,
	    ~(ARC_REG_INTRMASK_POSTQUEUE|ARC_REG_INTRSTAT_DOORBELL));

#if NBIO > 0
	if (bio_register(self, arc_bioctl) != 0)
		panic("%s: bioctl registration failed\n", DEVNAME(sc));
#endif

	return;
}

int
arc_detach(struct device *self, int flags)
{
	struct arc_softc		*sc = (struct arc_softc *)self;

	if (arc_msg0(sc, ARC_REG_INB_MSG0_STOP_BGRB) != 0)
		printf("%s: timeout waiting to stop bg rebuild\n");

	if (arc_msg0(sc, ARC_REG_INB_MSG0_FLUSH_CACHE) != 0)
		printf("%s: timeout waiting to flush cache\n");

	return (0);
}

int
arc_intr(void *arg)
{
	struct arc_softc		*sc = arg;
	struct arc_ccb			*ccb = NULL;
	char				*kva = ARC_DMA_KVA(sc->sc_requests);
	struct arc_io_cmd		*cmd;
	u_int32_t			reg, intrstat;

	intrstat = arc_read(sc, ARC_REG_INTRSTAT);
	if (intrstat == 0x0)
		return (0);
	arc_write(sc, ARC_REG_INTRSTAT, intrstat);

	if (intrstat & ARC_REG_INTRSTAT_DOORBELL) {
		if (sc->sc_talking) {
			/* if an ioctl is talking, wake it up */
			arc_write(sc, ARC_REG_INTRMASK,
			    ~ARC_REG_INTRMASK_POSTQUEUE);
			wakeup(sc);
		} else {
			/* otherwise drop it */
			reg = arc_read(sc, ARC_REG_OUTB_DOORBELL);
			arc_write(sc, ARC_REG_OUTB_DOORBELL, reg);
			if (reg & ARC_REG_OUTB_DOORBELL_WRITE_OK)
				arc_write(sc, ARC_REG_INB_DOORBELL,
				    ARC_REG_INB_DOORBELL_READ_OK);
		}
	}

	while ((reg = arc_pop(sc)) != 0xffffffff) {
		cmd = (struct arc_io_cmd *)(kva +
		    ((reg << ARC_REG_REPLY_QUEUE_ADDR_SHIFT) -
		    (u_int32_t)ARC_DMA_DVA(sc->sc_requests)));
		ccb = &sc->sc_ccbs[letoh32(cmd->cmd.context)];

		bus_dmamap_sync(sc->sc_dmat, ARC_DMA_MAP(sc->sc_requests),
		    ccb->ccb_offset, ARC_MAX_IOCMDLEN,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		arc_scsi_cmd_done(sc, ccb, reg);
	}

	return (1);
}

int
arc_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link		*link = xs->sc_link;
	struct arc_softc		*sc = link->adapter_softc;
	struct arc_ccb			*ccb;
	struct arc_msg_scsicmd		*cmd;
	u_int32_t			reg;
	int				rv = SUCCESSFULLY_QUEUED;
	int				s;

	if (xs->cmdlen > ARC_MSG_CDBLEN) {
		bzero(&xs->sense, sizeof(xs->sense));
		xs->sense.error_code = SSD_ERRCODE_VALID | 0x70;
		xs->sense.flags = SKEY_ILLEGAL_REQUEST;
		xs->sense.add_sense_code = 0x20;
		xs->error = XS_SENSE;
		s = splbio();
		scsi_done(xs);
		splx(s);
		return (COMPLETE);
	}

	s = splbio();
	ccb = arc_get_ccb(sc);
	splx(s);
	if (ccb == NULL) {
		xs->error = XS_DRIVER_STUFFUP;
		s = splbio();
		scsi_done(xs);
		splx(s);
		return (COMPLETE);
	}

	ccb->ccb_xs = xs;

	if (arc_load_xs(ccb) != 0) {
		xs->error = XS_DRIVER_STUFFUP;
		s = splbio();
		arc_put_ccb(sc, ccb);
		scsi_done(xs);
		splx(s);
		return (COMPLETE);
	}

	cmd = &ccb->ccb_cmd->cmd;
	reg = ccb->ccb_cmd_post;

	/* bus is always 0 */
	cmd->target = link->target;
	cmd->lun = link->lun;
	cmd->function = 1; /* XXX magic number */

	cmd->cdb_len = xs->cmdlen;
	cmd->sgl_len = ccb->ccb_dmamap->dm_nsegs;
	if (xs->flags & SCSI_DATA_OUT)
		cmd->flags = ARC_MSG_SCSICMD_FLAG_WRITE;
	if (ccb->ccb_dmamap->dm_nsegs > ARC_SGL_256LEN) {
		cmd->flags |= ARC_MSG_SCSICMD_FLAG_SGL_BSIZE_512;
		reg |= ARC_REG_POST_QUEUE_BIGFRAME;
	}

	cmd->context = htole32(ccb->ccb_id);
	cmd->data_len = htole32(xs->datalen);

	bcopy(xs->cmd, cmd->cdb, xs->cmdlen);

	/* we've built the command, lets put it on the hw */
	bus_dmamap_sync(sc->sc_dmat, ARC_DMA_MAP(sc->sc_requests),
	    ccb->ccb_offset, ARC_MAX_IOCMDLEN,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	s = splbio();
	arc_push(sc, reg);
	if (xs->flags & SCSI_POLL) {
		rv = COMPLETE;
		if (arc_complete(sc, ccb, xs->timeout) != 0) {
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
		}
	}
	splx(s);

	return (rv);
}

int
arc_load_xs(struct arc_ccb *ccb)
{
	struct arc_softc		*sc = ccb->ccb_sc;
	struct scsi_xfer		*xs = ccb->ccb_xs;
	bus_dmamap_t			dmap = ccb->ccb_dmamap;
	struct arc_sge			*sgl = ccb->ccb_cmd->sgl, *sge;
	u_int64_t			addr;
	int				i, error;

	if (xs->datalen == 0)
		return (0);

	error = bus_dmamap_load(sc->sc_dmat, dmap,
	    xs->data, xs->datalen, NULL,
	    (xs->flags & SCSI_NOSLEEP) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		printf("%s: error %d loading dmamap\n", DEVNAME(sc), error);
		return (1);
	}

	for (i = 0; i < dmap->dm_nsegs; i++) {
		sge = &sgl[i];

		sge->sg_hdr = htole32(ARC_SGE_64BIT | dmap->dm_segs[i].ds_len);
		addr = dmap->dm_segs[i].ds_addr;
		sge->sg_hi_addr = htole32((u_int32_t)(addr >> 32));
		sge->sg_lo_addr = htole32((u_int32_t)addr);
	}

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_PREREAD :
	    BUS_DMASYNC_PREWRITE);

	return (0);
}

void
arc_scsi_cmd_done(struct arc_softc *sc, struct arc_ccb *ccb, u_int32_t reg)
{
	struct scsi_xfer		*xs = ccb->ccb_xs;
	struct arc_msg_scsicmd		*cmd;

	if (xs->datalen != 0) {
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap, 0,
		    ccb->ccb_dmamap->dm_mapsize, (xs->flags & SCSI_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap);
	}

	/* timeout_del */
	xs->flags |= ITSDONE;

	if (reg & ARC_REG_REPLY_QUEUE_ERR) {
		cmd = &ccb->ccb_cmd->cmd;

		switch (cmd->status) {
		case ARC_MSG_STATUS_SELTIMEOUT:
		case ARC_MSG_STATUS_ABORTED:
		case ARC_MSG_STATUS_INIT_FAIL:
			xs->status = SCSI_OK;
			xs->error = XS_SELTIMEOUT;
			break;

		case SCSI_CHECK:
			bzero(&xs->sense, sizeof(xs->sense));
			bcopy(cmd->sense_data, &xs->sense,
			    min(ARC_MSG_SENSELEN, sizeof(xs->sense)));
			xs->sense.error_code = SSD_ERRCODE_VALID | 0x70;
			xs->status = SCSI_CHECK;
			xs->error = XS_SENSE;
			xs->resid = 0;
			break;

		default:
			/* unknown device status */
			xs->error = XS_BUSY; /* try again later? */
			xs->status = SCSI_BUSY;
			break;
		}
	} else {
		xs->status = SCSI_OK;
		xs->error = XS_NOERROR;
		xs->resid = 0;
	}

	arc_put_ccb(sc, ccb);
	scsi_done(xs);
}

int
arc_complete(struct arc_softc *sc, struct arc_ccb *nccb, int timeout)
{
	struct arc_ccb			*ccb = NULL;
	char				*kva = ARC_DMA_KVA(sc->sc_requests);
	struct arc_io_cmd		*cmd;
	u_int32_t			reg;

	do {
		reg = arc_pop(sc);
		if (reg == 0xffffffff) {
			if (timeout-- == 0)
				return (1);

			delay(1000);
			continue;
		}

		cmd = (struct arc_io_cmd *)(kva +
		    ((reg << ARC_REG_REPLY_QUEUE_ADDR_SHIFT) -
		    ARC_DMA_DVA(sc->sc_requests)));
		ccb = &sc->sc_ccbs[letoh32(cmd->cmd.context)];

		bus_dmamap_sync(sc->sc_dmat, ARC_DMA_MAP(sc->sc_requests),
		    ccb->ccb_offset, ARC_MAX_IOCMDLEN,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		arc_scsi_cmd_done(sc, ccb, reg);
	} while (nccb != ccb);

	return (0);
}

void
arc_minphys(struct buf *bp)
{
	if (bp->b_bcount > MAXPHYS)
		bp->b_bcount = MAXPHYS;
	minphys(bp);
}

int
arc_map_pci_resources(struct arc_softc *sc, struct pci_attach_args *pa)
{
	pcireg_t			memtype;
	pci_intr_handle_t		ih;
	const char			*intrstr;

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	memtype = pci_mapreg_type(sc->sc_pc, sc->sc_tag, ARC_PCI_BAR);
	if (pci_mapreg_map(pa, ARC_PCI_BAR, memtype, 0, &sc->sc_iot,
	    &sc->sc_ioh, NULL, &sc->sc_ios, 0) != 0) {
		printf(": unable to map system interface register\n");
		return(1);
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": unable to map interrupt\n");
		goto unmap;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO,
	    arc_intr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL) {
		printf(": unable to map interrupt%s%s\n",
		    intrstr == NULL ? "" : " at ",
		    intrstr == NULL ? "" : intrstr);
		goto unmap;
	}
	printf(": %s\n", intrstr);

	return (0);

unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	sc->sc_ios = 0;
	return (1);
}

int
arc_query_firmware(struct arc_softc *sc)
{
	struct arc_msg_firmware_info	fwinfo;
	char				string[81]; /* sizeof(vendor)*2+1 */

	if (arc_wait_eq(sc, ARC_REG_OUTB_ADDR1, ARC_REG_OUTB_ADDR1_FIRMWARE_OK,
	    ARC_REG_OUTB_ADDR1_FIRMWARE_OK) != 0) {
		printf("%s: timeout waiting for firmware ok\n");
		return (1);
	}

	if (arc_msg0(sc, ARC_REG_INB_MSG0_GET_CONFIG) != 0) {
		printf("%s: timeout waiting for get config\n");
		return (1);
	}

	arc_read_region(sc, ARC_REG_MSGBUF, &fwinfo, sizeof(fwinfo));

	DNPRINTF(ARC_D_INIT, "%s: signature: 0x%08x\n", DEVNAME(sc),
	    letoh32(fwinfo.signature));

	if (letoh32(fwinfo.signature) != ARC_FWINFO_SIGNATURE_GET_CONFIG) {
		printf("%s: invalid firmware info from iop\n", DEVNAME(sc));
		return (1);
	}

	DNPRINTF(ARC_D_INIT, "%s: request_len: %d\n", DEVNAME(sc),
	    letoh32(fwinfo.request_len));
	DNPRINTF(ARC_D_INIT, "%s: queue_len: %d\n", DEVNAME(sc),
	    letoh32(fwinfo.queue_len));
	DNPRINTF(ARC_D_INIT, "%s: sdram_size: %d\n", DEVNAME(sc),
	    letoh32(fwinfo.sdram_size));
	DNPRINTF(ARC_D_INIT, "%s: sata_ports: %d\n", DEVNAME(sc),
	    letoh32(fwinfo.sata_ports), letoh32(fwinfo.sata_ports));

#ifdef ARC_DEBUG
	scsi_strvis(string, fwinfo.vendor, sizeof(fwinfo.vendor));
	DNPRINTF(ARC_D_INIT, "%s: vendor: \"%s\"\n", DEVNAME(sc), string);
	scsi_strvis(string, fwinfo.model, sizeof(fwinfo.model));
	DNPRINTF(ARC_D_INIT, "%s: model: \"%s\"\n", DEVNAME(sc), string);
#endif /* ARC_DEBUG */

	scsi_strvis(string, fwinfo.fw_version, sizeof(fwinfo.fw_version));
	DNPRINTF(ARC_D_INIT, "%s: model: \"%s\"\n", DEVNAME(sc), string);

	/* device map? */

	if (letoh32(fwinfo.request_len) != ARC_MAX_IOCMDLEN) {
		printf("%s: unexpected request frame size (%d != %d)\n",
		    DEVNAME(sc), letoh32(fwinfo.request_len), ARC_MAX_IOCMDLEN);
		return (1);
	}

	sc->sc_req_count = letoh32(fwinfo.queue_len);

	if (arc_msg0(sc, ARC_REG_INB_MSG0_START_BGRB) != 0) {
		printf("%s: timeout waiting to start bg rebuild\n");
		return (1);
	}

	printf("%s: %d SATA Ports, %dMB SDRAM, FW Version: %s\n",
	    DEVNAME(sc), letoh32(fwinfo.sata_ports),
	    letoh32(fwinfo.sdram_size), string);

	return (0);
}

#if NBIO > 0
int
arc_bioctl(struct device *self, u_long cmd, caddr_t addr)
{
	struct arc_softc		*sc = (struct arc_softc *)self;
	int				error = 0;

	switch (cmd) {
	case BIOCINQ:
		error = arc_bio_inq(sc, (struct bioc_inq *)addr);
		break;

	case BIOCVOL:
		error = arc_bio_vol(sc, (struct bioc_vol *)addr);
		break;

	case BIOCDISK:
		error = arc_bio_disk(sc, (struct bioc_disk *)addr);
		break;

	case BIOCALARM:
		error = arc_bio_alarm(sc, (struct bioc_alarm *)addr);
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

int
arc_bio_alarm(struct arc_softc *sc, struct bioc_alarm *ba)
{
	u_int8_t			request[2];
	u_int8_t			reply[1];
	size_t				len;
	int				error = 0;

	switch (ba->ba_opcode) {
	case BIOC_SAENABLE:
	case BIOC_SADISABLE:
		request[0] = ARC_FW_SET_ALARM;
		request[1] = (ba->ba_opcode == BIOC_SAENABLE) ?
		    ARC_FW_SET_ALARM_ENABLE : ARC_FW_SET_ALARM_DISABLE;
		len = sizeof(request);

		break;

	case BIOC_SASILENCE:
		request[0] = ARC_FW_MUTE_ALARM;
		len = 1;

		break;

	case BIOC_GASTATUS:
		/* system info is too big/ugly to deal with here */
		return (arc_bio_alarm_state(sc, ba));

	default:
		return (EOPNOTSUPP);
	}

	arc_lock(sc);
	error = arc_msgbuf(sc, request, len, reply, sizeof(reply));
	arc_unlock(sc);

	if (error != 0)
		return (error);

	if (reply[0] != ARC_FW_CMD_OK)
		return (EIO);

	return (0);
}

int
arc_bio_alarm_state(struct arc_softc *sc, struct bioc_alarm *ba)
{
	u_int8_t			request = ARC_FW_SYSINFO;
	struct arc_fw_sysinfo		*sysinfo;
	int				error = 0;

	sysinfo = malloc(sizeof(struct arc_fw_sysinfo), M_TEMP, M_WAITOK);
	if (sysinfo == NULL)
		return (ENOMEM);

	request = ARC_FW_SYSINFO;

	arc_lock(sc);
	error = arc_msgbuf(sc, &request, sizeof(request),
	    sysinfo, sizeof(struct arc_fw_sysinfo));
	arc_unlock(sc);

	if (error != 0)
		goto out;

	ba->ba_status = sysinfo->alarm;

out:
	free(sysinfo, M_TEMP);
	return (error);
}


int
arc_bio_inq(struct arc_softc *sc, struct bioc_inq *bi)
{
	u_int8_t			request[2];
	struct arc_fw_sysinfo		*sysinfo;
	struct arc_fw_volinfo		*volinfo;
	int				maxvols, nvols = 0, i;
	int				error = 0;

	sysinfo = malloc(sizeof(struct arc_fw_sysinfo), M_TEMP, M_WAITOK);
	if (sysinfo == NULL)
		return (ENOMEM);

	volinfo = malloc(sizeof(struct arc_fw_volinfo), M_TEMP, M_WAITOK);
	if (volinfo == NULL) {
		free(sysinfo, M_TEMP);
		return (ENOMEM);
	}

	arc_lock(sc);

	request[0] = ARC_FW_SYSINFO;
	error = arc_msgbuf(sc, request, 1, sysinfo,
	    sizeof(struct arc_fw_sysinfo));
	if (error != 0)
		goto out;

	maxvols = sysinfo->max_volume_set;

	request[0] = ARC_FW_VOLINFO;
	for (i = 0; i < maxvols; i++) {
		request[1] = i;
		error = arc_msgbuf(sc, request, sizeof(request), volinfo,
		    sizeof(struct arc_fw_volinfo));
		if (error != 0)
			goto out;

		/*
		 * i cant find an easy way to see if the volume exists or not
		 * except to say that if it has no capacity then it isnt there.
		 * ignore passthru volumes, bioc_vol doesnt understand them.
		 */
		if (volinfo->capacity != 0 &&
		    volinfo->raid_level != ARC_FW_VOL_RAIDLEVEL_PASSTHRU)
			nvols++;
	}

	strlcpy(bi->bi_dev, DEVNAME(sc), sizeof(bi->bi_dev));
	bi->bi_novol = nvols;
out:
	arc_unlock(sc);
	free(volinfo, M_TEMP);
	free(sysinfo, M_TEMP);
	return (error);
}

int
arc_bio_getvol(struct arc_softc *sc, int vol, struct arc_fw_volinfo *volinfo)
{
	u_int8_t			request[2];
	struct arc_fw_sysinfo		*sysinfo;
	int				error = 0;
	int				maxvols, nvols = 0, i;

	sysinfo = malloc(sizeof(struct arc_fw_sysinfo), M_TEMP, M_WAITOK);
	if (sysinfo == NULL)
		return (ENOMEM);

	request[0] = ARC_FW_SYSINFO;
	error = arc_msgbuf(sc, request, 1, sysinfo,
	    sizeof(struct arc_fw_sysinfo));
	if (error != 0)
		goto out;

	maxvols = sysinfo->max_volume_set;

	request[0] = ARC_FW_VOLINFO;
	for (i = 0; i < maxvols; i++) {
		request[1] = i;
		error = arc_msgbuf(sc, request, sizeof(request), volinfo,
		    sizeof(struct arc_fw_volinfo));
		if (error != 0)
			goto out;

		if (volinfo->capacity == 0 ||
		    volinfo->raid_level == ARC_FW_VOL_RAIDLEVEL_PASSTHRU)
			continue;

		if (nvols == vol)
			break;

		nvols++;
	}

	if (nvols != vol || volinfo->capacity == 0 ||
	    volinfo->raid_level == ARC_FW_VOL_RAIDLEVEL_PASSTHRU) {
		error = ENODEV;
		goto out;
	}

out:
	free(sysinfo, M_TEMP);
	return (error);
}

int
arc_bio_vol(struct arc_softc *sc, struct bioc_vol *bv)
{
	struct arc_fw_volinfo		*volinfo;
	struct scsi_link		*sc_link;
	struct device			*dev;
	int				error = 0;

	volinfo = malloc(sizeof(struct arc_fw_volinfo), M_TEMP, M_WAITOK);
	if (volinfo == NULL)
		return (ENOMEM);

	arc_lock(sc);
	error = arc_bio_getvol(sc, bv->bv_volid, volinfo);
	arc_unlock(sc);

	if (error != 0)
		goto out;

	sc_link = sc->sc_scsibus->sc_link[volinfo->scsi_attr.target]
	    [volinfo->scsi_attr.lun];
	if (sc_link == NULL) {
		error = ENODEV;
		goto out;
	}
	dev = sc_link->device_softc;

	bv->bv_percent = -1;
	bv->bv_seconds = 0;

	bv->bv_status = BIOC_SVONLINE;
	bv->bv_size = (u_int64_t)letoh32(volinfo->capacity) * ARC_BLOCKSIZE;

	switch (volinfo->raid_level) {
	case ARC_FW_VOL_RAIDLEVEL_0:
		bv->bv_level = 0;
		break;
	case ARC_FW_VOL_RAIDLEVEL_1:
		bv->bv_level = 1;
		break;
	case ARC_FW_VOL_RAIDLEVEL_3:
		bv->bv_level = 3;
		break;
	case ARC_FW_VOL_RAIDLEVEL_5:
		bv->bv_level = 5;
		break;
	case ARC_FW_VOL_RAIDLEVEL_6:
		bv->bv_level = 6;
		break;
	case ARC_FW_VOL_RAIDLEVEL_PASSTHRU:
	default:
		bv->bv_level = -1;
		break;
	}

	bv->bv_nodisk = volinfo->member_disks;
	strlcpy(bv->bv_dev, dev->dv_xname, sizeof(bv->bv_dev));

out:
	free(volinfo, M_TEMP);
	return (error);
}

int
arc_bio_disk(struct arc_softc *sc, struct bioc_disk *bd)
{
	u_int8_t			request[2];
	struct arc_fw_volinfo		*volinfo;
	struct arc_fw_raidinfo		*raidinfo;
	struct arc_fw_diskinfo		*diskinfo;
	int				error = 0;
	char				model[81];
	char				serial[41];
	char				rev[17];

	volinfo = malloc(sizeof(struct arc_fw_volinfo), M_TEMP, M_WAITOK);
	if (volinfo == NULL)
		return (ENOMEM);

	raidinfo = malloc(sizeof(struct arc_fw_raidinfo), M_TEMP, M_WAITOK);
	if (raidinfo == NULL) {
		free(volinfo, M_TEMP);
		return (ENOMEM);
	}

	diskinfo = malloc(sizeof(struct arc_fw_diskinfo), M_TEMP, M_WAITOK);
	if (diskinfo == NULL) {
		free(raidinfo, M_TEMP);
		free(volinfo, M_TEMP);
		return (ENOMEM);
	}

	arc_lock(sc);

	error = arc_bio_getvol(sc, bd->bd_volid, volinfo);
	if (error != 0)
		goto out;

	request[0] = ARC_FW_RAIDINFO;
	request[1] = volinfo->raid_set_number;
	error = arc_msgbuf(sc, request, sizeof(request), raidinfo,
	    sizeof(struct arc_fw_raidinfo));
	if (error != 0)
		goto out;

	if (bd->bd_diskid > raidinfo->member_devices) {
		error = ENODEV;
		goto out;
	}

	request[0] = ARC_FW_DISKINFO;
	request[1] = raidinfo->device_array[bd->bd_diskid];
	error = arc_msgbuf(sc, request, sizeof(request), diskinfo,
	    sizeof(struct arc_fw_diskinfo));
	if (error != 0)
		goto out;

#if 0
	bd->bd_channel = diskinfo->scsi_attr.channel;
	bd->bd_target = diskinfo->scsi_attr.target;
	bd->bd_lun = diskinfo->scsi_attr.lun;
#endif
	/*
	 * the firwmare doesnt seem to fill scsi_attr in, so fake it with
	 * the diskid.
	 */
	bd->bd_channel = 0;
	bd->bd_target = raidinfo->device_array[bd->bd_diskid];
	bd->bd_lun = 0;

	bd->bd_status = BIOC_SDONLINE;
	bd->bd_size = (u_int64_t)letoh32(diskinfo->capacity) * ARC_BLOCKSIZE;

	scsi_strvis(model, diskinfo->model, sizeof(diskinfo->model));
	scsi_strvis(serial, diskinfo->serial, sizeof(diskinfo->serial));
	scsi_strvis(rev, diskinfo->firmware_rev,
	    sizeof(diskinfo->firmware_rev));

	snprintf(bd->bd_vendor, sizeof(bd->bd_vendor), "%s %s",
	    model, rev);
	strlcpy(bd->bd_serial, serial, sizeof(bd->bd_serial));

out:
	arc_unlock(sc);
	free(diskinfo, M_TEMP);
	free(raidinfo, M_TEMP);
	free(volinfo, M_TEMP);
	return (error);
}
#endif /* NBIO > 0 */

u_int8_t
arc_msg_cksum(void *cmd, u_int16_t len)
{
	u_int8_t			*buf = cmd;
	u_int8_t			cksum;
	int				i;

	cksum = (u_int8_t)(len >> 8) + (u_int8_t)len;
	for (i = 0; i < len; i++)
		cksum += buf[i];

	return (cksum);
}


int
arc_msgbuf(struct arc_softc *sc, void *wptr, size_t wbuflen, void *rptr,
    size_t rbuflen)
{
	u_int8_t			rwbuf[ARC_REG_IOC_RWBUF_MAXLEN];
	u_int8_t			*wbuf, *rbuf;
	int				wlen, wdone = 0, rlen, rdone = 0;
	struct arc_fw_bufhdr		*bufhdr;
	u_int32_t			reg, rwlen;
	int				error = 0;
#ifdef ARC_DEBUG
	int				i;
#endif

	DNPRINTF(ARC_D_DB, "%s: arc_msgbuf wbuflen: %d rbuflen: %d\n",
	    DEVNAME(sc), wbuflen, rbuflen);

	if (arc_read(sc, ARC_REG_OUTB_DOORBELL) != 0)
		return (EBUSY);

	wlen = sizeof(struct arc_fw_bufhdr) + wbuflen + 1; /* 1 for cksum */
	wbuf = malloc(wlen, M_TEMP, M_WAITOK);
	if (wbuf == NULL)
		return (ENOMEM);

	rlen = sizeof(struct arc_fw_bufhdr) + rbuflen + 1; /* 1 for cksum */
	rbuf = malloc(rlen, M_TEMP, M_WAITOK);
	if (rbuf == NULL) {
		free(wbuf, M_TEMP);
		return (ENOMEM);
	}

	DNPRINTF(ARC_D_DB, "%s: arc_msgbuf wlen: %d rlen: %d\n", DEVNAME(sc),
	    wlen, rlen);

	bufhdr = (struct arc_fw_bufhdr *)wbuf;
	bufhdr->hdr = arc_fw_hdr;
	bufhdr->len = htole16(wbuflen);
	bcopy(wptr, wbuf + sizeof(struct arc_fw_bufhdr), wbuflen);
	wbuf[wlen - 1] = arc_msg_cksum(wptr, wbuflen);

	reg = ARC_REG_OUTB_DOORBELL_READ_OK;

	do {
		if ((reg & ARC_REG_OUTB_DOORBELL_READ_OK) && wdone < wlen) {
			bzero(rwbuf, sizeof(rwbuf));
			rwlen = (wlen - wdone) % sizeof(rwbuf);
			bcopy(&wbuf[wdone], rwbuf, rwlen);

#ifdef ARC_DEBUG
			if (arcdebug & ARC_D_DB) {
				printf("%s: write %d:", DEVNAME(sc), rwlen);
				for (i = 0; i < rwlen; i++)
					printf(" 0x%02x", rwbuf[i]);
				printf("\n");
			}
#endif

			/* copy the chunk to the hw */
			arc_write(sc, ARC_REG_IOC_WBUF_LEN, rwlen);
			arc_write_region(sc, ARC_REG_IOC_WBUF, rwbuf,
			    sizeof(rwbuf));

			/* say we have a buffer for the hw */
			arc_write(sc, ARC_REG_INB_DOORBELL,
			    ARC_REG_INB_DOORBELL_WRITE_OK);

			wdone += rwlen;
		}

		while ((reg = arc_read(sc, ARC_REG_OUTB_DOORBELL)) == 0)
			arc_wait(sc);
		arc_write(sc, ARC_REG_OUTB_DOORBELL, reg);

		DNPRINTF(ARC_D_DB, "%s: reg: 0x%08x\n", DEVNAME(sc), reg);

		if ((reg & ARC_REG_OUTB_DOORBELL_WRITE_OK) && rdone < rlen) {
			rwlen = arc_read(sc, ARC_REG_IOC_RBUF_LEN);
			if (rwlen > sizeof(rwbuf)) {
				DNPRINTF(ARC_D_DB, "%s:  rwlen too big\n",
				    DEVNAME(sc));
				error = EIO;
				goto out;
			}

			arc_read_region(sc, ARC_REG_IOC_RBUF, rwbuf,
			    sizeof(rwbuf));

			arc_write(sc, ARC_REG_INB_DOORBELL,
			    ARC_REG_INB_DOORBELL_READ_OK);

#ifdef ARC_DEBUG
			printf("%s:  len: %d+%d=%d/%d\n", DEVNAME(sc),
			    rwlen, rdone, rwlen + rdone, rlen);
			if (arcdebug & ARC_D_DB) {
				printf("%s: read:", DEVNAME(sc));
				for (i = 0; i < rwlen; i++)
					printf(" 0x%02x", rwbuf[i]);
				printf("\n");
			}
#endif

			if ((rdone + rwlen) > rlen) {
				DNPRINTF(ARC_D_DB, "%s:  rwbuf too big\n",
				    DEVNAME(sc));
				error = EIO;
				goto out;
			}

			bcopy(rwbuf, &rbuf[rdone], rwlen);
			rdone += rwlen;
		}
	} while (rdone != rlen);

	bufhdr = (struct arc_fw_bufhdr *)rbuf;
	if (memcmp(&bufhdr->hdr, &arc_fw_hdr, sizeof(bufhdr->hdr)) != 0 ||
	    bufhdr->len != htole16(rbuflen)) {
		DNPRINTF(ARC_D_DB, "%s:  rbuf hdr is wrong\n", DEVNAME(sc));
		error = EIO;
		goto out;
	}

	bcopy(rbuf + sizeof(struct arc_fw_bufhdr), rptr, rbuflen);

	if (rbuf[rlen - 1] != arc_msg_cksum(rptr, rbuflen)) {
		DNPRINTF(ARC_D_DB, "%s:  invalid cksum\n", DEVNAME(sc));
		error = EIO;
		goto out;
	}

out:
	free(wbuf, M_TEMP);
	free(rbuf, M_TEMP);

	return (error);
}

void
arc_lock(struct arc_softc *sc)
{
	int				s;

	rw_enter_write(&sc->sc_lock);
	s = splbio();
	arc_write(sc, ARC_REG_INTRMASK, ~ARC_REG_INTRMASK_POSTQUEUE);
	sc->sc_talking = 1;
	splx(s);
}

void
arc_unlock(struct arc_softc *sc)
{
	int				s;

	s = splbio();
	sc->sc_talking = 0;
	arc_write(sc, ARC_REG_INTRMASK,
	    ~(ARC_REG_INTRMASK_POSTQUEUE|ARC_REG_INTRSTAT_DOORBELL));
	splx(s);
	rw_exit_write(&sc->sc_lock);
}

void
arc_wait(struct arc_softc *sc)
{
	int				s;

	s = splbio();
	arc_write(sc, ARC_REG_INTRMASK,
	    ~(ARC_REG_INTRMASK_POSTQUEUE|ARC_REG_INTRSTAT_DOORBELL));
	if (tsleep(sc, PWAIT, "arcdb", hz) == EWOULDBLOCK)
		arc_write(sc, ARC_REG_INTRMASK, ~ARC_REG_INTRMASK_POSTQUEUE);
	splx(s);
}

u_int32_t
arc_read(struct arc_softc *sc, bus_size_t r)
{
	u_int32_t			v;

	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_READ);
	v = bus_space_read_4(sc->sc_iot, sc->sc_ioh, r);

	DNPRINTF(ARC_D_RW, "%s: arc_read 0x%x 0x%08x\n", DEVNAME(sc), r, v);

	return (v);
}

void
arc_read_region(struct arc_softc *sc, bus_size_t r, void *buf, size_t len)
{
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, len,
	    BUS_SPACE_BARRIER_READ);
	bus_space_read_raw_region_4(sc->sc_iot, sc->sc_ioh, r, buf, len);
}

void
arc_write(struct arc_softc *sc, bus_size_t r, u_int32_t v)
{
	DNPRINTF(ARC_D_RW, "%s: arc_write 0x%x 0x%08x\n", DEVNAME(sc), r, v);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, r, v);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

void
arc_write_region(struct arc_softc *sc, bus_size_t r, void *buf, size_t len)
{
	bus_space_write_raw_region_4(sc->sc_iot, sc->sc_ioh, r, buf, len);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, len,
	    BUS_SPACE_BARRIER_WRITE);
}

int
arc_wait_eq(struct arc_softc *sc, bus_size_t r, u_int32_t mask,
    u_int32_t target)
{
	int				i;

	DNPRINTF(ARC_D_RW, "%s: arc_wait_eq 0x%x 0x%08x 0x%08x\n",
	    DEVNAME(sc), r, mask, target);

	for (i = 0; i < 10000; i++) {
		if ((arc_read(sc, r) & mask) == target)
			return (0);
		delay(1000);
	}

	return (1);
}

int
arc_wait_ne(struct arc_softc *sc, bus_size_t r, u_int32_t mask,
    u_int32_t target)
{
	int				i;

	DNPRINTF(ARC_D_RW, "%s: arc_wait_ne 0x%x 0x%08x 0x%08x\n",
	    DEVNAME(sc), r, mask, target);

	for (i = 0; i < 10000; i++) {
		if ((arc_read(sc, r) & mask) != target)
			return (0);
		delay(1000);
	}

	return (1);
}

int
arc_msg0(struct arc_softc *sc, u_int32_t m)
{
	/* post message */
	arc_write(sc, ARC_REG_INB_MSG0, m);
	/* wait for the fw to do it */
	if (arc_wait_eq(sc, ARC_REG_INTRSTAT, ARC_REG_INTRSTAT_MSG0,
	    ARC_REG_INTRSTAT_MSG0) != 0)
		return (1);

	/* ack it */
	arc_write(sc, ARC_REG_INTRSTAT, ARC_REG_INTRSTAT_MSG0);

	return (0);
}

struct arc_dmamem *
arc_dmamem_alloc(struct arc_softc *sc, size_t size)
{
	struct arc_dmamem		*adm;
	int				nsegs;

	adm = malloc(sizeof(struct arc_dmamem), M_DEVBUF, M_NOWAIT);
	if (adm == NULL)
		return (NULL);

	bzero(adm, sizeof(struct arc_dmamem));
	adm->adm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &adm->adm_map) != 0)
		goto admfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &adm->adm_seg,
	    1, &nsegs, BUS_DMA_NOWAIT) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &adm->adm_seg, nsegs, size,
	    &adm->adm_kva, BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, adm->adm_map, adm->adm_kva, size,
	    NULL, BUS_DMA_NOWAIT) != 0)
		goto unmap;

	bzero(adm->adm_kva, size);

	return (adm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, adm->adm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &adm->adm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, adm->adm_map);
admfree:
	free(adm, M_DEVBUF);

	return (NULL);
}

void
arc_dmamem_free(struct arc_softc *sc, struct arc_dmamem *adm)
{
	bus_dmamap_unload(sc->sc_dmat, adm->adm_map);
	bus_dmamem_unmap(sc->sc_dmat, adm->adm_kva, adm->adm_size);
	bus_dmamem_free(sc->sc_dmat, &adm->adm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, adm->adm_map);
	free(adm, M_DEVBUF);
}

int
arc_alloc_ccbs(struct arc_softc *sc)
{
	struct arc_ccb			*ccb;
	u_int8_t			*cmd;
	int				i;

	TAILQ_INIT(&sc->sc_ccb_free);

	sc->sc_ccbs = malloc(sizeof(struct arc_ccb) * sc->sc_req_count,
	    M_DEVBUF, M_WAITOK);
	if (sc->sc_ccbs == NULL) {
		printf("%s: unable to allocate ccbs\n", DEVNAME(sc));
		return (1);
	}
	bzero(sc->sc_ccbs, sizeof(struct arc_ccb) * sc->sc_req_count);

	sc->sc_requests = arc_dmamem_alloc(sc,
	    ARC_MAX_IOCMDLEN * sc->sc_req_count);
	if (sc->sc_requests == NULL) {
		printf("%s: unable to allocate ccb dmamem\n", DEVNAME(sc));
		goto free_ccbs;
	}
	cmd = ARC_DMA_KVA(sc->sc_requests);

	for (i = 0; i < sc->sc_req_count; i++) {
		ccb = &sc->sc_ccbs[i];

		if (bus_dmamap_create(sc->sc_dmat, MAXPHYS, ARC_SGL_MAXLEN,
		    MAXPHYS, 0, 0, &ccb->ccb_dmamap) != 0) {
			printf("%s: unable to create dmamap for ccb %d\n",
			    DEVNAME(sc), i);
			goto free_maps;
		}

		ccb->ccb_sc = sc;
		ccb->ccb_id = i;
		ccb->ccb_offset = ARC_MAX_IOCMDLEN * i;

		ccb->ccb_cmd = (struct arc_io_cmd *)&cmd[ccb->ccb_offset];
		ccb->ccb_cmd_post = (ARC_DMA_DVA(sc->sc_requests) +
		    ccb->ccb_offset) >> ARC_REG_POST_QUEUE_ADDR_SHIFT;

		arc_put_ccb(sc, ccb);
	}

	return (0);

free_maps:
	while ((ccb = arc_get_ccb(sc)) != NULL)
	    bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);
	arc_dmamem_free(sc, sc->sc_requests);

free_ccbs:
	free(sc->sc_ccbs, M_DEVBUF);

	return (1);
}

struct arc_ccb *
arc_get_ccb(struct arc_softc *sc)
{
	struct arc_ccb			*ccb;

	ccb = TAILQ_FIRST(&sc->sc_ccb_free);
	if (ccb != NULL)
		TAILQ_REMOVE(&sc->sc_ccb_free, ccb, ccb_link);

	return (ccb);
}

void
arc_put_ccb(struct arc_softc *sc, struct arc_ccb *ccb)
{
	ccb->ccb_xs = NULL;
	bzero(ccb->ccb_cmd, ARC_MAX_IOCMDLEN);
	TAILQ_INSERT_TAIL(&sc->sc_ccb_free, ccb, ccb_link);
}
