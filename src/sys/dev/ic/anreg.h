/*	$OpenBSD: anreg.h,v 1.8 2001/09/29 21:54:00 mickey Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/an/if_anreg.h,v 1.9 2001/07/27 16:05:21 brooks Exp $
 */

#pragma pack(1)

#define AN_TIMEOUT	65536

/* Default network name: empty string */
#define AN_DEFAULT_NETNAME	""

/* The nodename must be less than 16 bytes */
#define AN_DEFAULT_NODENAME	"OpenBSD"

#define AN_DEFAULT_IBSS		"OpenBSD IBSS"

/*
 * register space access macros
 */
#define CSR_WRITE_2(sc, reg, val)	\
	bus_space_write_2(sc->an_btag, sc->an_bhandle, reg, val)

#define CSR_READ_2(sc, reg)		\
	bus_space_read_2(sc->an_btag, sc->an_bhandle, reg)

#define CSR_WRITE_1(sc, reg, val)	\
	bus_space_write_1(sc->an_btag, sc->an_bhandle, reg, val)

#define CSR_READ_1(sc, reg)		\
	bus_space_read_1(sc->an_btag, sc->an_bhandle, reg)

/*
 * Size of Aironet I/O space.
 */
#define AN_IOSIZ		0x40

/*
 * Hermes register definitions and what little I know about them.
 */

/* Hermes command/status registers. */
#define AN_COMMAND		0x00
#define AN_PARAM0		0x02
#define AN_PARAM1		0x04
#define AN_PARAM2		0x06
#define AN_STATUS		0x08
#define AN_RESP0		0x0A
#define AN_RESP1		0x0C
#define AN_RESP2		0x0E
#define AN_LINKSTAT		0x10

/* Command register */
#define AN_CMD_BUSY		0x8000	/* busy bit */
#define AN_CMD_NO_ACK		0x0080	/* don't acknowledge command */
#define AN_CMD_CODE_MASK	0x003F
#define AN_CMD_QUAL_MASK	0x7F00

/* Command codes */
#define AN_CMD_NOOP		0x0000	/* no-op */
#define AN_CMD_ENABLE		0x0001	/* enable */
#define AN_CMD_DISABLE		0x0002	/* disable */
#define AN_CMD_FORCE_SYNCLOSS	0x0003	/* force loss of sync */
#define AN_CMD_FW_RESTART	0x0004	/* firmware resrart */
#define AN_CMD_HOST_SLEEP	0x0005
#define AN_CMD_MAGIC_PKT	0x0006
#define AN_CMD_READCFG		0x0008
#define	AN_CMD_SET_MODE		0x0009
#define AN_CMD_ALLOC_MEM	0x000A	/* allocate NIC memory */
#define AN_CMD_TX		0x000B	/* transmit */
#define AN_CMD_DEALLOC_MEM	0x000C
#define AN_CMD_NOOP2		0x0010
#define AN_CMD_ACCESS		0x0021
#define AN_CMD_ALLOC_BUF	0x0028
#define AN_CMD_PSP_NODES	0x0030
#define AN_CMD_SET_PHYREG	0x003E
#define AN_CMD_TX_TEST		0x003F
#define AN_CMD_SLEEP		0x0085
#define AN_CMD_SAVECFG		0x0108

/*
 * Reclaim qualifier bit, applicable to the
 * TX command.
 */
#define AN_RECLAIM		0x0100	/* reclaim NIC memory */

/*
 * ACCESS command qualifier bits.
 */
#define AN_ACCESS_READ		0x0000
#define AN_ACCESS_WRITE		0x0100

/*
 * PROGRAM command qualifier bits.
 */
#define AN_PROGRAM_DISABLE	0x0000
#define AN_PROGRAM_ENABLE_RAM	0x0100
#define AN_PROGRAM_ENABLE_NVRAM	0x0200
#define AN_PROGRAM_NVRAM	0x0300

/* Status register values */
#define AN_STAT_CMD_CODE	0x003F
#define AN_STAT_CMD_RESULT	0x7F00

/* Linkstat register */
#define AN_LINKSTAT_ASSOCIATED		0x0400
#define AN_LINKSTAT_AUTHFAIL		0x0300
#define AN_LINKSTAT_ASSOC_FAIL		0x8400
#define AN_LINKSTAT_DISASSOC		0x8200
#define AN_LINKSTAT_DEAUTH		0x8100
#define AN_LINKSTAT_SYNCLOST_TSF	0x8004
#define AN_LINKSTAT_SYNCLOST_HOSTREQ	0x8003
#define AN_LINKSTAT_SYNCLOST_AVGRETRY	0x8002
#define AN_LINKSTAT_SYNCLOST_MAXRETRY	0x8001
#define AN_LINKSTAT_SYNCLOST_MISSBEACON	0x8000

/* memory handle management registers */
#define AN_RX_FID		0x20
#define AN_ALLOC_FID		0x22
#define AN_TX_CMP_FID		0x24

/*
 * Buffer Access Path (BAP) registers.
 * These are I/O channels. I believe you can use each one for
 * any desired purpose independently of the other. In general
 * though, we use BAP1 for reading and writing LTV records and
 * reading received data frames, and BAP0 for writing transmit
 * frames. This is a convention though, not a rule.
 */
#define AN_SEL0			0x18
#define AN_SEL1			0x1A
#define AN_OFF0			0x1C
#define AN_OFF1			0x1E
#define AN_DATA0		0x36
#define AN_DATA1		0x38
#define AN_BAP0			AN_DATA0
#define AN_BAP1			AN_DATA1

#define AN_OFF_BUSY		0x8000
#define AN_OFF_ERR		0x4000
#define AN_OFF_DONE		0x2000
#define AN_OFF_DATAOFF		0x0FFF

/* Event registers */
#define AN_EVENT_STAT		0x30	/* Event status */
#define AN_INT_EN		0x32	/* Interrupt enable/disable */
#define AN_EVENT_ACK		0x34	/* Ack event */

/* Events */
#define AN_EV_CLR_STUCK_BUSY	0x4000	/* clear stuck busy bit */
#define AN_EV_WAKEREQUEST	0x2000	/* awaken from PSP mode */
#define AN_EV_AWAKE		0x0100	/* station woke up from PSP mode*/
#define AN_EV_LINKSTAT		0x0080	/* link status available */
#define AN_EV_CMD		0x0010	/* command completed */
#define AN_EV_ALLOC		0x0008	/* async alloc/reclaim completed */
#define AN_EV_TX_EXC		0x0004	/* async xmit completed with failure */
#define AN_EV_TX		0x0002	/* async xmit completed succesfully */
#define AN_EV_RX		0x0001	/* async rx completed */

#define AN_INTRS	\
	(AN_EV_RX|AN_EV_TX|AN_EV_TX_EXC|AN_EV_ALLOC|AN_EV_LINKSTAT)

/* Host software registers */
#define AN_SW0			0x28
#define AN_SW1			0x2A
#define AN_SW2			0x2C
#define AN_SW3			0x2E

#define AN_CNTL			0x14

#define AN_CNTL_AUX_ENA		0xC000
#define AN_CNTL_AUX_ENA_STAT	0xC000
#define AN_CNTL_AUX_DIS_STAT	0x0000
#define AN_CNTL_AUX_ENA_CNTL	0x8000
#define AN_CNTL_AUX_DIS_CNTL	0x4000

#define AN_AUX_PAGE		0x3A
#define AN_AUX_OFFSET		0x3C
#define AN_AUX_DATA		0x3E

/*
 * Length, Type, Value (LTV) record definitions and RID values.
 */
struct an_ltv_gen {
	u_int16_t		an_len;
	u_int16_t		an_type;
	u_int16_t		an_val[1];
};

#define AN_OPMODE_IBSS_ADHOC			0x0000
#define AN_OPMODE_INFRASTRUCTURE_STATION	0x0001
#define AN_OPMODE_AP				0x0002
#define AN_OPMODE_AP_REPEATER			0x0003
#define AN_OPMODE_UNMODIFIED_PAYLOAD		0x0100
#define AN_OPMODE_AIRONET_EXTENSIONS		0x0200
#define AN_OPMODE_AP_EXTENSIONS			0x0400

#define AN_RXMODE_BC_MC_ADDR			0x0000
#define AN_RXMODE_BC_ADDR			0x0001
#define AN_RXMODE_ADDR				0x0002
#define AN_RXMODE_80211_MONITOR_CURBSS		0x0003
#define AN_RXMODE_80211_MONITOR_ANYBSS		0x0004
#define AN_RXMODE_LAN_MONITOR_CURBSS		0x0005
#define AN_RXMODE_NO_8023_HEADER		0x0100

#define AN_RATE_1MBPS				0x0002
#define AN_RATE_2MBPS				0x0004
#define AN_RATE_5_5MBPS				0x000B
#define AN_RATE_11MBPS				0x0016

#define AN_DEVTYPE_PC4500			0x0065
#define AN_DEVTYPE_PC4800			0x006D

#define AN_SCANMODE_ACTIVE			0x0000
#define AN_SCANMODE_PASSIVE			0x0001
#define AN_SCANMODE_AIRONET_ACTIVE		0x0002

#define AN_AUTHTYPE_NONE			0x0000
#define AN_AUTHTYPE_OPEN			0x0001
#define AN_AUTHTYPE_SHAREDKEY			0x0002
#define AN_AUTHTYPE_PRIVACY_IN_USE		0x0100
#define AN_AUTHTYPE_ALLOW_UNENCRYPTED		0x0200

#define AN_PSAVE_NONE				0x0000
#define AN_PSAVE_CAM				0x0001
#define AN_PSAVE_PSP				0x0002
#define AN_PSAVE_PSP_CAM			0x0003

#define AN_RADIOTYPE_80211_FH			0x0001
#define AN_RADIOTYPE_80211_DS			0x0002
#define AN_RADIOTYPE_LM2000_DS			0x0004

#define AN_DIVERSITY_FACTORY_DEFAULT		0x0000
#define AN_DIVERSITY_ANTENNA_1_ONLY		0x0001
#define AN_DIVERSITY_ANTENNA_2_ONLY		0x0002
#define AN_DIVERSITY_ANTENNA_1_AND_2		0x0003

#define AN_TXPOWER_FACTORY_DEFAULT		0x0000
#define AN_TXPOWER_50MW				50
#define AN_TXPOWER_100MW			100
#define AN_TXPOWER_250MW			250

#define AN_DEF_SSID_LEN		7
#define AN_DEF_SSID		"tsunami"


#define AN_ENCAP_ACTION_RX	0x0001
#define AN_ENCAP_ACTION_TX	0x0002

#define AN_RXENCAP_NONE		0x0000
#define AN_RXENCAP_RFC1024	0x0001

#define AN_TXENCAP_RFC1024	0x0000
#define AN_TXENCAP_80211	0x0002

#define AN_STATUS_OPMODE_CONFIGURED		0x0001
#define AN_STATUS_OPMODE_MAC_ENABLED		0x0002
#define AN_STATUS_OPMODE_RX_ENABLED		0x0004
#define AN_STATUS_OPMODE_IN_SYNC		0x0010
#define AN_STATUS_OPMODE_ASSOCIATED		0x0020
#define AN_STATUS_OPMODE_ERROR			0x8000


/*
 * Statistics
 */
#define AN_RID_16BITS_CUM	0xFF60	/* Cumulative 16-bit stats counters */
#define AN_RID_16BITS_DELTA	0xFF61	/* 16-bit stats (since last clear) */
#define AN_RID_16BITS_DELTACLR	0xFF62	/* 16-bit stats, clear on read */
#define AN_RID_32BITS_CUM	0xFF68	/* Cumulative 32-bit stats counters */
#define AN_RID_32BITS_DELTA	0xFF69	/* 32-bit stats (since last clear) */
#define AN_RID_32BITS_DELTACLR	0xFF6A	/* 32-bit stats, clear on read */

/*
 * Receive frame structure.
 */
struct an_rxframe {
	u_int32_t	an_rx_time;		/* 0x00 */
	u_int16_t	an_rx_status;		/* 0x04 */
	u_int16_t	an_rx_payload_len;	/* 0x06 */
	u_int8_t	an_rsvd0;		/* 0x08 */
	u_int8_t	an_rx_signal_strength;	/* 0x09 */
	u_int8_t	an_rx_rate;		/* 0x0A */
	u_int8_t	an_rx_chan;		/* 0x0B */
	u_int8_t	an_rx_assoc_cnt;	/* 0x0C */
	u_int8_t	an_rsvd1[3];		/* 0x0D */
	u_int8_t	an_plcp_hdr[4];		/* 0x10 */
	u_int16_t	an_frame_ctl;		/* 0x14 */
	u_int16_t	an_duration;		/* 0x16 */
	u_int8_t	an_addr1[6];		/* 0x18 */
	u_int8_t	an_addr2[6];		/* 0x1E */
	u_int8_t	an_addr3[6];		/* 0x24 */
	u_int16_t	an_seq_ctl;		/* 0x2A */
	u_int8_t	an_addr4[6];		/* 0x2C */
	u_int16_t	an_gaplen;		/* 0x32 */
};

#define AN_RXGAP_MAX	8

/*
 * Transmit frame structure.
 */
struct an_txframe {
	u_int32_t	an_tx_sw;		/* 0x00 */
	u_int16_t	an_tx_status;		/* 0x04 */
	u_int16_t	an_tx_payload_len;	/* 0x06 */
	u_int16_t	an_tx_ctl;		/* 0x08 */
	u_int16_t	an_tx_assoc_id;		/* 0x0A */
	u_int16_t	an_tx_retry;		/* 0x0C */
	u_int8_t	an_tx_assoc_cnt;	/* 0x0E */
	u_int8_t	an_tx_rate;		/* 0x0F */
	u_int8_t	an_tx_max_long_retries;	/* 0x10 */
	u_int8_t	an_tx_max_short_retries; /*0x11 */
	u_int8_t	an_rsvd0[2];		/* 0x12 */
	u_int16_t	an_frame_ctl;		/* 0x14 */
	u_int16_t	an_duration;		/* 0x16 */
	u_int8_t	an_addr1[6];		/* 0x18 */
	u_int8_t	an_addr2[6];		/* 0x1E */
	u_int8_t	an_addr3[6];		/* 0x24 */
	u_int16_t	an_seq_ctl;		/* 0x2A */
	u_int8_t	an_addr4[6];		/* 0x2C */
	u_int16_t	an_gaplen;		/* 0x32 */
};

struct an_rxframe_802_3 {
	u_int16_t	an_rx_802_3_status;	/* 0x34 */
	u_int16_t	an_rx_802_3_payload_len;/* 0x36 */
	u_int8_t	an_rx_dst_addr[6];	/* 0x38 */
	u_int8_t	an_rx_src_addr[6];	/* 0x3E */
};
#define AN_RXGAP_MAX	8


/*
 * Transmit 802.3 header structure.
 */
struct an_txframe_802_3 {
	u_int16_t	an_tx_802_3_status;	/* 0x34 */
	u_int16_t	an_tx_802_3_payload_len;/* 0x36 */
	u_int8_t	an_tx_dst_addr[6];	/* 0x38 */
	u_int8_t	an_tx_src_addr[6];	/* 0x3E */
};

#define AN_TXSTAT_EXCESS_RETRY	0x0002
#define AN_TXSTAT_LIFE_EXCEEDED	0x0004
#define AN_TXSTAT_AID_FAIL	0x0008
#define AN_TXSTAT_MAC_DISABLED	0x0010
#define AN_TXSTAT_ASSOC_LOST	0x0020

#define AN_TXCTL_RSVD		0x0001
#define AN_TXCTL_TXOK_INTR	0x0002
#define AN_TXCTL_TXERR_INTR	0x0004
#define AN_TXCTL_HEADER_TYPE	0x0008
#define AN_TXCTL_PAYLOAD_TYPE	0x0010
#define AN_TXCTL_NORELEASE	0x0020
#define AN_TXCTL_NORETRIES	0x0040
#define AN_TXCTL_CLEAR_AID	0x0080
#define AN_TXCTL_STRICT_ORDER	0x0100
#define AN_TXCTL_USE_RTS	0x0200

#define AN_HEADERTYPE_8023	0x0000
#define AN_HEADERTYPE_80211	0x0008

#define AN_PAYLOADTYPE_ETHER	0x0000
#define AN_PAYLOADTYPE_LLC	0x0010

#define AN_TXCTL_80211	\
	(AN_TXCTL_TXOK_INTR|AN_TXCTL_TXERR_INTR|AN_HEADERTYPE_80211|	\
	AN_PAYLOADTYPE_LLC|AN_TXCTL_NORELEASE)

#define AN_TXCTL_8023	\
	(AN_TXCTL_TXOK_INTR|AN_TXCTL_TXERR_INTR|AN_HEADERTYPE_8023|	\
	AN_PAYLOADTYPE_ETHER|AN_TXCTL_NORELEASE)

#define AN_TXGAP_80211		0
#define AN_TXGAP_8023		0

struct an_802_3_hdr {
	u_int16_t		an_8023_status;
	u_int16_t		an_8023_payload_len;
	u_int8_t		an_8023_dst_addr[6];
	u_int8_t		an_8023_src_addr[6];
	u_int16_t		an_8023_dat[3];	/* SNAP header */
	u_int16_t		an_8023_type;
};

struct an_snap_hdr {
	u_int16_t		an_snap_dat[3];	/* SNAP header */
	u_int16_t		an_snap_type;
};

#define AN_INC(x, y)		(x) = (x + 1) % y

#define AN_802_3_OFFSET		0x2E
#define AN_802_11_OFFSET	0x44
#define AN_802_11_OFFSET_RAW	0x3C

#define AN_STAT_BADCRC		0x0001
#define AN_STAT_UNDECRYPTABLE	0x0002
#define AN_STAT_ERRSTAT		0x0003
#define AN_STAT_MAC_PORT	0x0700
#define AN_STAT_1042		0x2000	/* RFC1042 encoded */
#define AN_STAT_TUNNEL		0x4000	/* Bridge-tunnel encoded */
#define AN_STAT_WMP_MSG		0x6000	/* WaveLAN-II management protocol */
#define AN_RXSTAT_MSG_TYPE	0xE000

#define AN_ENC_TX_802_3		0x00
#define AN_ENC_TX_802_11	0x11
#define AN_ENC_TX_E_II		0x0E

#define AN_ENC_TX_1042		0x00
#define AN_ENC_TX_TUNNEL	0xF8

#define AN_TXCNTL_MACPORT	0x00FF
#define AN_TXCNTL_STRUCTTYPE	0xFF00

#define AN_RID_WEP_TEMP	        0xFF15
#define AN_RID_WEP_PERM	        0xFF16

/*
 * SNAP (sub-network access protocol) constants for transmission
 * of IP datagrams over IEEE 802 networks, taken from RFC1042.
 * We need these for the LLC/SNAP header fields in the TX/RX frame
 * structure.
 */
#define AN_SNAP_K1		0xaa	/* assigned global SAP for SNAP */
#define AN_SNAP_K2		0x00
#define AN_SNAP_CONTROL		0x03	/* unnumbered information format */
#define AN_SNAP_WORD0		(AN_SNAP_K1 | (AN_SNAP_K1 << 8))
#define AN_SNAP_WORD1		(AN_SNAP_K2 | (AN_SNAP_CONTROL << 8))
#define AN_SNAPHDR_LEN		0x6



#pragma pack()
