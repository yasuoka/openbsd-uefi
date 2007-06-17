/*	$OpenBSD: if_malovar.h,v 1.11 2007/06/17 10:18:28 mglocker Exp $ */

/*
 * Copyright (c) 2007 Marcus Glocker <mglocker@openbsd.org>
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

/* simplify bus space access */
#define MALO_READ_1(sc, reg) \
	bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define MALO_READ_2(sc, reg) \
	bus_space_read_2((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define	MALO_READ_MULTI_2(sc, reg, off, size) \
	bus_space_read_multi_2((sc)->sc_iot, (sc)->sc_ioh, (reg), (off), \
	(size))
#define MALO_WRITE_1(sc, reg, val) \
	bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define MALO_WRITE_2(sc, reg, val) \
	bus_space_write_2((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define MALO_WRITE_MULTI_2(sc, reg, off, size) \
	bus_space_write_multi_2((sc)->sc_iot, (sc)->sc_ioh, (reg), (off), \
	(size))

/* miscellaneous */
#define MALO_FW_HELPER_BSIZE	256	/* helper FW block size */
#define MALO_FW_HELPER_LOADED	0x10	/* helper FW loaded */
#define MALO_FW_MAIN_MAXRETRY	20	/* main FW block resend max retry */
#define MALO_CMD_BUFFER_SIZE	256	/* cmd buffer */

/* device flags */
#define MALO_DEVICE_ATTACHED	(1 << 0)
#define MALO_FW_LOADED		(1 << 1)

/*
 * FW command structures
 */
struct malo_cmd_header {
	uint16_t	cmd;
	uint16_t	size;
	uint16_t	seqnum;
	uint16_t	result;
	/* malo_cmd_body */
};

struct malo_cmd_body_spec {
	uint16_t	hw_if_version;
	uint16_t	hw_version;
	uint16_t	num_of_wcb;
	uint16_t	num_of_mcast;
	uint8_t		macaddr[ETHER_ADDR_LEN];
	uint16_t	regioncode;
	uint16_t	num_of_antenna;
	uint32_t	fw_version;
	uint32_t	wcbbase;
	uint32_t	rxpdrdptr;
	uint32_t	rxpdwrptr;
	uint32_t	fw_capinfo;
} __packed;

struct malo_cmd_body_scan {
	uint8_t		bsstype;
	uint8_t		bssid[ETHER_ADDR_LEN];
	/* malo_cmd_tlv_ssid */
	/* malo_cmd_tlv_chanlist */
	/* malo_cmd_tlv_rates */
	/* malo_cmd_tlv_numprobes */
} __packed;

struct malo_cmd_body_auth {
	uint8_t		peermac[ETHER_ADDR_LEN];
	uint8_t		authtype;
} __packed;

#define MALO_OID_BSS		0x00
#define MALO_OID_RATE		0x01
#define MALO_OID_BCNPERIOD	0x02
#define MALO_OID_DTIMPERIOD	0x03
#define MALO_OID_ASSOCTIMEOUT	0x04
#define MALO_OID_RTSTRESH	0x05
#define MALO_OID_SHORTRETRY	0x06
#define MALO_OID_LONGRETRY	0x07
#define MALO_OID_FRAGTRESH	0x08
#define MALO_OID_80211D		0x09
#define MALO_OID_80211H		0x0a
struct malo_cmd_body_snmp {
	uint16_t	action;
	uint16_t	oid;
	uint16_t	size;
	uint8_t		data[128];
} __packed;

struct malo_cmd_body_radio {
	uint16_t	action;
	uint16_t	control;
} __packed;

struct malo_cmd_body_channel {
	uint16_t	action;
	uint16_t	channel;
	uint16_t	rftype;
	uint16_t	reserved;
	uint8_t		channel_list[32];
} __packed;

struct malo_cmd_body_txpower {
	uint16_t	action;
	int16_t		txpower;	
} __packed;

struct malo_cmd_body_antenna {
	uint16_t	action;
	uint16_t	antenna_mode;
} __packed;

struct malo_cmd_body_macctrl {
	uint16_t	action;
	uint16_t	reserved;
} __packed;

struct malo_cmd_body_assoc {
	uint8_t		peermac[ETHER_ADDR_LEN];
	uint16_t	capinfo;
	uint16_t	listenintrv;
	uint16_t	bcnperiod;
	uint8_t		dtimperiod;
	/* malo_cmd_tlv_ssid */
	/* malo_cmd_tlv_phy */
	/* malo_cmd_tlv_cf */
	/* malo_cmd_tlv_rate */
} __packed;

struct malo_cmd_body_rate {
	uint16_t	action;
	uint16_t	hwauto;
	uint16_t	ratebitmap;
} __packed;

/*
 * FW command TLV structures
 */
#define MALO_TLV_TYPE_SSID	0x0000
#define MALO_TLV_TYPE_RATES	0x0001
#define MALO_TLV_TYPE_PHY	0x0003
#define MALO_TLV_TYPE_CF	0x0004
#define MALO_TLV_TYPE_CHANLIST	0x0101
#define MALO_TLV_TYPE_NUMPROBES	0x0102

struct malo_cmd_tlv_ssid {
	uint16_t	type;
	uint16_t	size;
	uint8_t		data[1];
} __packed;

struct malo_cmd_tlv_rates {
	uint16_t	type;
	uint16_t	size;
	uint8_t		data[1];
} __packed;

struct malo_cmd_tlv_phy {
	uint16_t	type;
	uint16_t	size;
	uint8_t		data[1];
} __packed;

struct malo_cmd_tlv_cf {
	uint16_t	type;
	uint16_t	size;
	uint8_t		data[1];
} __packed;

struct malo_cmd_tlv_chanlist_param {
	uint8_t		radiotype;
	uint8_t		channumber;
	uint8_t		scantype;
	uint16_t	minscantime;
	uint16_t	maxscantime;
} __packed;
#define CHANNELS	12
struct malo_cmd_tlv_chanlist {
	uint16_t	type;
	uint16_t	size;
	struct malo_cmd_tlv_chanlist_param data[CHANNELS];
} __packed;

struct malo_cmd_tlv_numprobes {
	uint16_t	type;
	uint16_t	size;
	uint16_t	numprobes;
} __packed;

/* RX descriptor */
#define MALO_RX_STATUS_OK	0x0001
struct malo_rx_desc {
	uint16_t	status;
	uint8_t		snr;
	uint8_t		control;
	uint16_t	pkglen;
	uint8_t		nf;
	uint8_t		rate;
	uint32_t	pkgoffset;
	uint32_t	reserved1;
	uint8_t		priority;
	uint8_t		reserved2[3];
} __packed;

/* TX descriptor */
struct malo_tx_desc {
	uint32_t	status;
	uint32_t	control;
	uint32_t	pkgoffset;
	uint16_t	pkglen;
	uint8_t		dstaddrhigh[2];
	uint8_t		dstaddrlow[4];
	uint8_t		priority;
	uint8_t		flags;
	uint8_t		reserved[2];
} __packed;

/*
 * Softc
 */
struct malo_softc {
	struct device		 sc_dev;
	struct ieee80211com	 sc_ic;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	int			 (*sc_newstate)
				 (struct ieee80211com *, enum ieee80211_state,
				     int);

	int			 sc_flags;
	void			*sc_cmd;
	uint8_t			 sc_cmd_running;
	void			*sc_data;
	uint8_t			 sc_curchan;
};
