/*	$OpenBSD: atascsi.h,v 1.20 2007/03/22 05:15:39 pascoe Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
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

struct atascsi;

/*
 * ATA commands
 */

#define ATA_C_IDENTIFY		0xec
#define ATA_C_FLUSH_CACHE	0xe7
#define ATA_C_FLUSH_CACHE_EXT	0xea /* lba48 */
#define ATA_C_READDMA		0xc8
#define ATA_C_WRITEDMA		0xca
#define ATA_C_READDMA_EXT	0x25
#define ATA_C_WRITEDMA_EXT	0x35
#define ATA_C_PACKET		0xa0
#define ATA_C_READ_FPDMA	0x60
#define ATA_C_WRITE_FPDMA	0x61

struct ata_identify {
	u_int16_t	config;		/*   0 */
	u_int16_t	ncyls;		/*   1 */
	u_int16_t	reserved1;	/*   2 */
	u_int16_t	nheads;		/*   3 */
	u_int16_t	track_size;	/*   4 */
	u_int16_t	sector_size;	/*   5 */
	u_int16_t	nsectors;	/*   6 */
	u_int16_t	reserved2[3];	/*   7 vendor unique */
	u_int8_t	serial[20];	/*  10 */
	u_int16_t	buffer_type;	/*  20 */
	u_int16_t	buffer_size;	/*  21 */
	u_int16_t	ecc;		/*  22 */
	u_int8_t	firmware[8];	/*  23 */
	u_int8_t	model[40];	/*  27 */
	u_int16_t	multi;		/*  47 */
	u_int16_t	dwcap;		/*  48 */
	u_int16_t	cap;		/*  49 */
	u_int16_t	reserved3;	/*  50 */
	u_int16_t	piomode;	/*  51 */
	u_int16_t	dmamode;	/*  52 */
	u_int16_t	validinfo;	/*  53 */
	u_int16_t	curcyls;	/*  54 */
	u_int16_t	curheads;	/*  55 */
	u_int16_t	cursectrk;	/*  56 */
	u_int16_t	curseccp[2];	/*  57 */
	u_int16_t	mult2;		/*  59 */
	u_int16_t	addrsec[2];	/*  60 */
	u_int16_t	worddma;	/*  62 */
	u_int16_t	dworddma;	/*  63 */
	u_int16_t	advpiomode;	/*  64 */
	u_int16_t	minmwdma;	/*  65 */
	u_int16_t	recmwdma;	/*  66 */
	u_int16_t	minpio;		/*  67 */
	u_int16_t	minpioflow;	/*  68 */
	u_int16_t	reserved4[2];	/*  69 */
	u_int16_t	typtime[2];	/*  71 */
	u_int16_t	reserved5[2];	/*  73 */
	u_int16_t	qdepth;		/*  75 */
	u_int16_t	satacap;	/*  76 */
	u_int16_t	reserved6;	/*  77 */
	u_int16_t	satafsup;	/*  78 */
	u_int16_t	satafen;	/*  79 */
	u_int16_t	majver;		/*  80 */
	u_int16_t	minver;		/*  81 */
	u_int16_t	cmdset82;	/*  82 */
	u_int16_t	cmdset83;	/*  83 */
	u_int16_t	cmdset84;	/*  84 */
	u_int16_t	features85;	/*  85 */
	u_int16_t	features86;	/*  86 */
	u_int16_t	features87;	/*  87 */
	u_int16_t	ultradma;	/*  88 */
	u_int16_t	erasetime;	/*  89 */
	u_int16_t	erasetimex;	/*  90 */
	u_int16_t	apm;		/*  91 */
	u_int16_t	masterpw;	/*  92 */
	u_int16_t	hwreset;	/*  93 */
	u_int16_t	acoustic;	/*  94 */
	u_int16_t	stream_min;	/*  95 */
	u_int16_t	stream_xfer_d;	/*  96 */
	u_int16_t	stream_lat;	/*  97 */
	u_int16_t	streamperf[2];	/*  98 */
	u_int16_t	addrsecxt[4];	/* 100 */
	u_int16_t	stream_xfer_p;	/* 104 */
	u_int16_t	padding1;	/* 105 */
	u_int16_t	phys_sect_sz;	/* 106 */
	u_int16_t	seek_delay;	/* 107 */
	u_int16_t	naa_ieee_oui;	/* 108 */
	u_int16_t	ieee_oui_uid;	/* 109 */
	u_int16_t	uid_mid;	/* 110 */
	u_int16_t	uid_low;	/* 111 */
	u_int16_t	resv_wwn[4];	/* 112 */
	u_int16_t	incits;		/* 116 */
	u_int16_t	words_lsec[2];	/* 117 */
	u_int16_t	cmdset119;	/* 119 */
	u_int16_t	features120;	/* 120 */
	u_int16_t	padding2[6];
	u_int16_t	rmsn;		/* 127 */
	u_int16_t	securestatus;	/* 128 */
	u_int16_t	vendor[31];	/* 129 */
	u_int16_t	padding3[16];	/* 160 */
	u_int16_t	curmedser[30];	/* 176 */
	u_int16_t	sctsupport;	/* 206 */
	u_int16_t	padding4[48];	/* 207 */
	u_int16_t	integrity;	/* 255 */
} __packed;

/*
 * Frame Information Structures
 */

#define ATA_FIS_LENGTH		20

struct ata_fis_h2d {
	u_int8_t		type;
#define ATA_FIS_TYPE_H2D		0x27
	u_int8_t		flags;
#define ATA_H2D_FLAGS_CMD		(1<<7)
	u_int8_t		command;
	u_int8_t		features;
#define ATA_H2D_FEATURES_DMA		(1<<0)
#define ATA_H2D_FEATURES_DIR		(1<<2)
#define ATA_H2D_FEATURES_DIR_READ	(1<<2)
#define ATA_H2D_FEATURES_DIR_WRITE	(0<<2)

	u_int8_t		lba_low;
	u_int8_t		lba_mid;
	u_int8_t		lba_high;
	u_int8_t		device;
#define ATA_H2D_DEVICE_LBA		0x40

	u_int8_t		lba_low_exp;
	u_int8_t		lba_mid_exp;
	u_int8_t		lba_high_exp;
	u_int8_t		features_exp;

	u_int8_t		sector_count;
	u_int8_t		sector_count_exp;
	u_int8_t		reserved0;
	u_int8_t		control;

	u_int8_t		reserved1;
	u_int8_t		reserved2;
	u_int8_t		reserved3;
	u_int8_t		reserved4;
} __packed;

struct ata_fis_d2h {
	u_int8_t		type;
#define ATA_FIS_TYPE_D2H		0x34
	u_int8_t		flags;
#define ATA_D2H_FLAGS_INTR		(1<<6)
	u_int8_t		status;
	u_int8_t		error;

	u_int8_t		lba_low;
	u_int8_t		lba_mid;
	u_int8_t		lba_high;
	u_int8_t		device;

	u_int8_t		lba_low_exp;
	u_int8_t		lba_mid_exp;
	u_int8_t		lba_high_exp;
	u_int8_t		reserved0;

	u_int8_t		sector_count;
	u_int8_t		sector_count_exp;
	u_int8_t		reserved1;
	u_int8_t		reserved2;

	u_int8_t		reserved3;
	u_int8_t		reserved4;
	u_int8_t		reserved5;
	u_int8_t		reserved6;
} __packed;

/*
 * ATA interface
 */

struct ata_port {
	struct atascsi		*ap_as;
	int			ap_port;
	int			ap_type;
#define ATA_PORT_T_NONE			0
#define ATA_PORT_T_DISK			1
#define ATA_PORT_T_ATAPI		2
	int			ap_features;
#define ATA_PORT_F_PROBED		(1 << 0)
	int			ap_ncqdepth;
};

struct ata_xfer {
	struct ata_fis_h2d	*fis;
	struct ata_fis_d2h	rfis;
	u_int8_t		*packetcmd;
	u_int8_t		tag;

	u_int8_t		*data;
	size_t			datalen;
	size_t			resid;

	void			(*complete)(struct ata_xfer *);
	struct timeout		stimeout;
	u_int			timeout;

	int			flags;
#define ATA_F_READ			(1<<0)
#define ATA_F_WRITE			(1<<1)
#define ATA_F_NOWAIT			(1<<2)
#define ATA_F_POLL			(1<<3)
#define ATA_F_PIO			(1<<4)
#define ATA_F_PACKET			(1<<5)
#define ATA_F_NCQ			(1<<6)
	volatile int		state;
#define ATA_S_SETUP			0
#define ATA_S_PENDING			1
#define ATA_S_COMPLETE			2
#define ATA_S_ERROR			3
#define ATA_S_TIMEOUT			4
#define ATA_S_ONCHIP			5
#define ATA_S_PUT			6

	void			*atascsi_private;

	void			(*ata_put_xfer)(struct ata_xfer *);
};

#define ATA_QUEUED		0
#define ATA_COMPLETE		1
#define ATA_ERROR		2

/*
 * atascsi
 */

struct atascsi_methods {
	int			(*probe)(void *, int);
	struct ata_xfer *	(*ata_get_xfer)(void *, int );
	int			(*ata_cmd)(struct ata_xfer *);
};

struct atascsi_attach_args {
	void			*aaa_cookie;

	struct atascsi_methods	*aaa_methods;
	void			(*aaa_minphys)(struct buf *);
	int			aaa_nports;
	int			aaa_ncmds;
	int			aaa_capability;
#define ASAA_CAP_NCQ		(1 << 0)
#define ASAA_CAP_NEEDS_RESERVED	(1 << 1)
};

struct atascsi	*atascsi_attach(struct device *, struct atascsi_attach_args *);
int		atascsi_detach(struct atascsi *);

int		atascsi_probe_dev(struct atascsi *, int);
int		atascsi_detach_dev(struct atascsi *, int);
