/*	$OpenBSD: if_xlreg.h,v 1.7 1998/11/11 23:25:02 jason Exp $	*/

/*
 * Copyright (c) 1997, 1998
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
 *	$FreeBSD: if_xlreg.h,v 1.8 1998/10/22 15:52:25 wpaul Exp $
 */

#define XL_EE_READ	0x0080	/* read, 5 bit address */
#define XL_EE_WRITE	0x0040	/* write, 5 bit address */
#define XL_EE_ERASE	0x00c0	/* erase, 5 bit address */
#define XL_EE_EWEN	0x0030	/* erase, no data needed */
#define XL_EE_BUSY	0x8000

#define XL_EE_EADDR0	0x00	/* station address, first word */
#define XL_EE_EADDR1	0x01	/* station address, next word, */
#define XL_EE_EADDR2	0x02	/* station address, last word */
#define XL_EE_PRODID	0x03	/* product ID code */
#define XL_EE_MDATA_DATE	0x04	/* manufacturing data, date */
#define XL_EE_MDATA_DIV		0x05	/* manufacturing data, division */
#define XL_EE_MDATA_PCODE	0x06	/* manufacturing data, product code */
#define XL_EE_MFG_ID	0x07
#define XL_EE_PCI_PARM	0x08
#define XL_EE_ROM_ONFO	0x09
#define XL_EE_OEM_ADR0	0x0A
#define	XL_EE_OEM_ADR1	0x0B
#define XL_EE_OEM_ADR2	0x0C
#define XL_EE_SOFTINFO1	0x0D
#define XL_EE_COMPAT	0x0E
#define XL_EE_SOFTINFO2	0x0F
#define XL_EE_CAPS	0x10	/* capabilities word */
#define XL_EE_RSVD0	0x11
#define XL_EE_ICFG_0	0x12
#define XL_EE_ICFG_1	0x13
#define XL_EE_RSVD1	0x14
#define XL_EE_SOFTINFO3	0x15
#define XL_EE_RSVD_2	0x16

/*
 * Bits in the capabilities word
 */
#define XL_CAPS_PNP		0x0001
#define XL_CAPS_FULL_DUPLEX	0x0002
#define XL_CAPS_LARGE_PKTS	0x0004
#define XL_CAPS_SLAVE_DMA	0x0008
#define XL_CAPS_SECOND_DMA	0x0010
#define XL_CAPS_FULL_BM		0x0020
#define XL_CAPS_FRAG_BM		0x0040
#define XL_CAPS_CRC_PASSTHRU	0x0080
#define XL_CAPS_TXDONE		0x0100
#define XL_CAPS_NO_TXLENGTH	0x0200
#define XL_CAPS_RX_REPEAT	0x0400
#define XL_CAPS_SNOOPING	0x0800
#define XL_CAPS_100MBPS		0x1000
#define XL_CAPS_PWRMGMT		0x2000

#define XL_PACKET_SIZE 1536
	
/*
 * Register layouts.
 */
#define XL_COMMAND		0x0E
#define XL_STATUS		0x0E

#define XL_TX_STATUS		0x1B
#define XL_TX_FREE		0x1C
#define XL_DMACTL		0x20
#define XL_DOWNLIST_PTR		0x24
#define XL_TX_FREETHRESH	0x2F
#define XL_UPLIST_PTR		0x38
#define XL_UPLIST_STATUS	0x30

#define XL_PKTSTAT_UP_STALLED		0x00002000
#define XL_PKTSTAT_UP_ERROR		0x00004000
#define XL_PKTSTAT_UP_CMPLT		0x00008000

#define XL_DMACTL_DN_CMPLT_REQ		0x00000002
#define XL_DMACTL_DOWN_STALLED		0x00000004
#define XL_DMACTL_UP_CMPLT		0x00000008
#define XL_DMACTL_DOWN_CMPLT		0x00000010
#define XL_DMACTL_UP_RX_EARLY		0x00000020
#define XL_DMACTL_ARM_COUNTDOWN		0x00000040
#define XL_DMACTL_DOWN_INPROG		0x00000080
#define XL_DMACTL_COUNTER_SPEED		0x00000100
#define XL_DMACTL_DOWNDOWN_MODE		0x00000200
#define XL_DMACTL_TARGET_ABORT		0x40000000
#define XL_DMACTL_MASTER_ABORT		0x80000000

/*
 * Command codes. Some command codes require that we wait for
 * the CMD_BUSY flag to clear. Those codes are marked as 'mustwait.'
 */
#define XL_CMD_RESET		0x0000	/* mustwait */
#define XL_CMD_WINSEL		0x0800
#define XL_CMD_COAX_START	0x1000
#define XL_CMD_RX_DISABLE	0x1800
#define XL_CMD_RX_ENABLE	0x2000
#define XL_CMD_RX_RESET		0x2800	/* mustwait */
#define XL_CMD_UP_STALL		0x3000	/* mustwait */
#define XL_CMD_UP_UNSTALL	0x3001
#define XL_CMD_DOWN_STALL	0x3002	/* mustwait */
#define XL_CMD_DOWN_UNSTALL	0x3003
#define XL_CMD_RX_DISCARD	0x4000
#define XL_CMD_TX_ENABLE	0x4800
#define XL_CMD_TX_DISABLE	0x5000
#define XL_CMD_TX_RESET		0x5800	/* mustwait */
#define XL_CMD_INTR_FAKE	0x6000
#define XL_CMD_INTR_ACK		0x6800
#define XL_CMD_INTR_ENB		0x7000
#define XL_CMD_STAT_ENB		0x7800
#define XL_CMD_RX_SET_FILT	0x8000
#define XL_CMD_RX_SET_THRESH	0x8800
#define XL_CMD_TX_SET_THRESH	0x9000
#define XL_CMD_TX_SET_START	0x9800
#define XL_CMD_DMA_UP		0xA000
#define XL_CMD_DMA_STOP		0xA001
#define XL_CMD_STATS_ENABLE	0xA800
#define XL_CMD_STATS_DISABLE	0xB000
#define XL_CMD_COAX_STOP	0xB800

#define XL_CMD_SET_TX_RECLAIM	0xC000 /* 3c905B only */
#define XL_CMD_RX_SET_HASH	0xC800 /* 3c905B only */

#define XL_HASH_SET		0x0400
#define XL_HASHFILT_SIZE	256

/*
 * status codes
 * Note that bits 15 to 13 indicate the currently visible register window
 * which may be anything from 0 to 7.
 */
#define XL_STAT_INTLATCH	0x0001	/* 0 */
#define XL_STAT_ADFAIL		0x0002	/* 1 */
#define XL_STAT_TX_COMPLETE	0x0004	/* 2 */
#define XL_STAT_TX_AVAIL	0x0008	/* 3 first generation */
#define XL_STAT_RX_COMPLETE	0x0010  /* 4 */
#define XL_STAT_RX_EARLY	0x0020	/* 5 */
#define XL_STAT_INTREQ		0x0040  /* 6 */
#define XL_STAT_STATSOFLOW	0x0080  /* 7 */
#define XL_STAT_DMADONE		0x0100	/* 8 first generation */
#define XL_STAT_LINKSTAT	0x0100	/* 8 3c905B */
#define XL_STAT_DOWN_COMPLETE	0x0200	/* 9 */
#define XL_STAT_UP_COMPLETE	0x0400	/* 10 */
#define XL_STAT_DMABUSY		0x0800	/* 11 first generation */
#define XL_STAT_CMDBUSY		0x1000  /* 12 */

/*
 * Interrupts we normally want enabled.
 */
#define XL_INTRS							\
	(XL_STAT_UP_COMPLETE|XL_STAT_STATSOFLOW|XL_STAT_ADFAIL|		\
	 XL_STAT_DOWN_COMPLETE|XL_STAT_TX_COMPLETE|XL_STAT_INTLATCH)

/*
 * Window 0 registers
 */
#define XL_W0_EE_DATA		0x0C
#define XL_W0_EE_CMD		0x0A
#define XL_W0_RSRC_CFG		0x08
#define XL_W0_ADDR_CFG		0x06
#define XL_W0_CFG_CTRL		0x04

#define XL_W0_PROD_ID		0x02
#define XL_W0_MFG_ID		0x00

/*
 * Window 1
 */

#define XL_W1_TX_FIFO		0x10

#define XL_W1_FREE_TX		0x0C
#define XL_W1_TX_STATUS		0x0B
#define XL_W1_TX_TIMER		0x0A
#define XL_W1_RX_STATUS		0x08
#define XL_W1_RX_FIFO		0x00

/*
 * RX status codes
 */
#define XL_RXSTATUS_OVERRUN	0x01
#define XL_RXSTATUS_RUNT	0x02
#define XL_RXSTATUS_ALIGN	0x04
#define XL_RXSTATUS_CRC		0x08
#define XL_RXSTATUS_OVERSIZE	0x10
#define XL_RXSTATUS_DRIBBLE	0x20

/*
 * TX status codes
 */
#define XL_TXSTATUS_RECLAIM	0x02 /* 3c905B only */
#define XL_TXSTATUS_OVERFLOW	0x04
#define XL_TXSTATUS_MAXCOLS	0x08
#define XL_TXSTATUS_UNDERRUN	0x10
#define XL_TXSTATUS_JABBER	0x20
#define XL_TXSTATUS_INTREQ	0x40
#define XL_TXSTATUS_COMPLETE	0x80

/*
 * Window 2
 */
#define XL_W2_RESET_OPTIONS	0x0C	/* 3c905B only */
#define XL_W2_STATION_MASK_HI	0x0A
#define XL_W2_STATION_MASK_MID	0x08
#define XL_W2_STATION_MASK_LO	0x06
#define XL_W2_STATION_ADDR_HI	0x04
#define XL_W2_STATION_ADDR_MID	0x02
#define XL_W2_STATION_ADDR_LO	0x00

#define XL_RESETOPT_FEATUREMASK	0x0001|0x0002|0x004
#define XL_RESETOPT_D3RESETDIS	0x0008
#define XL_RESETOPT_DISADVFD	0x0010
#define XL_RESETOPT_DISADV100	0x0020
#define XL_RESETOPT_DISAUTONEG	0x0040
#define XL_RESETOPT_DEBUGMODE	0x0080
#define XL_RESETOPT_FASTAUTO	0x0100
#define XL_RESETOPT_FASTEE	0x0200
#define XL_RESETOPT_FORCEDCONF	0x0400
#define XL_RESETOPT_TESTPDTPDR	0x0800
#define XL_RESETOPT_TEST100TX	0x1000
#define XL_RESETOPT_TEST100RX	0x2000

/*
 * Window 3 (fifo management)
 */
#define XL_W3_INTERNAL_CFG	0x00
#define XL_W3_RESET_OPT		0x08
#define XL_W3_FREE_TX		0x0C
#define XL_W3_FREE_RX		0x0A
#define XL_W3_MAC_CTRL		0x06

#define XL_ICFG_CONNECTOR_MASK	0x00F00000
#define XL_ICFG_CONNECTOR_BITS	20

#define XL_ICFG_RAMSIZE_MASK	0x00000007
#define XL_ICFG_RAMWIDTH	0x00000008
#define XL_ICFG_ROMSIZE_MASK	(0x00000040|0x00000080)
#define XL_ICFG_DISABLE_BASSD	0x00000100
#define XL_ICFG_RAMLOC		0x00000200
#define XL_ICFG_RAMPART		(0x00010000|0x00020000)
#define XL_ICFG_XCVRSEL		(0x00100000|0x00200000|0x00400000)
#define XL_ICFG_AUTOSEL		0x01000000

#define XL_XCVR_10BT		0x00
#define XL_XCVR_AUI		0x01
#define XL_XCVR_RSVD_0		0x02
#define XL_XCVR_COAX		0x03
#define XL_XCVR_100BTX		0x04
#define XL_XCVR_100BFX		0x05
#define XL_XCVR_MII		0x06
#define XL_XCVR_RSVD_1		0x07
#define XL_XCVR_AUTO		0x08	/* 3c905B only */

#define XL_MACCTRL_DEFER_EXT_END	0x0001
#define XL_MACCTRL_DEFER_0		0x0002
#define XL_MACCTRL_DEFER_1		0x0004
#define XL_MACCTRL_DEFER_2		0x0008
#define XL_MACCTRL_DEFER_3		0x0010
#define XL_MACCTRL_DUPLEX		0x0020
#define XL_MACCTRL_ALLOW_LARGE_PACK	0x0040
#define XL_MACCTRL_EXTEND_AFTER_COL	0x0080 (3c905B only)
#define XL_MACCTRL_FLOW_CONTROL_ENB	0x0100 (3c905B only)
#define XL_MACCTRL_VLT_END		0x0200 (3c905B only)

/*
 * The 'reset options' register contains power-on reset values
 * loaded from the EEPROM. This includes the supported media
 * types on the card. It is also known as the media options register.
 */
#define XL_W3_MEDIA_OPT		0x08

#define XL_MEDIAOPT_BT4		0x0001	/* MII */
#define XL_MEDIAOPT_BTX		0x0002	/* on-chip */
#define XL_MEDIAOPT_BFX		0x0004	/* on-chip */
#define XL_MEDIAOPT_BT		0x0008	/* on-chip */
#define XL_MEDIAOPT_BNC		0x0010	/* on-chip */
#define XL_MEDIAOPT_AUI		0x0020	/* on-chip */
#define XL_MEDIAOPT_MII		0x0040	/* MII */
#define XL_MEDIAOPT_VCO		0x0100	/* 1st gen chip only */

#define XL_MEDIAOPT_10FL	0x0100	/* 3x905B only, on-chip */
#define XL_MEDIAOPT_MASK	0x01FF

/*
 * Window 4 (diagnostics)
 */
#define XL_W4_UPPERBYTESOK	0x0D
#define XL_W4_BADSSD		0x0C
#define XL_W4_MEDIA_STATUS	0x0A
#define XL_W4_PHY_MGMT		0x08
#define XL_W4_NET_DIAG		0x06
#define XL_W4_FIFO_DIAG		0x04
#define XL_W4_VCO_DIAG		0x02

#define XL_W4_CTRLR_STAT	0x08
#define XL_W4_TX_DIAG		0x00

#define XL_MII_CLK		0x01
#define XL_MII_DATA		0x02
#define XL_MII_DIR		0x04

#define XL_MEDIA_SQE		0x0008
#define XL_MEDIA_10TP		0x00C0
#define XL_MEDIA_LNK		0x0080
#define XL_MEDIA_LNKBEAT	0x0800

#define XL_MEDIASTAT_CRCSTRIP	0x0004
#define XL_MEDIASTAT_SQEENB	0x0008
#define XL_MEDIASTAT_COLDET	0x0010
#define XL_MEDIASTAT_CARRIER	0x0020
#define XL_MEDIASTAT_JABGUARD	0x0040
#define XL_MEDIASTAT_LINKBEAT	0x0080
#define XL_MEDIASTAT_JABDETECT	0x0200
#define XL_MEDIASTAT_POLREVERS	0x0400
#define XL_MEDIASTAT_LINKDETECT	0x0800
#define XL_MEDIASTAT_TXINPROG	0x1000
#define XL_MEDIASTAT_DCENB	0x4000
#define XL_MEDIASTAT_AUIDIS	0x8000

#define XL_NETDIAG_TEST_LOWVOLT		0x0001
#define XL_NETDIAG_ASIC_REVMASK		(0x0002|0x0004|0x0008|0x0010|0x0020)
#define XL_NETDIAG_UPPER_BYTES_ENABLE	0x0040
#define XL_NETDIAG_STATS_ENABLED	0x0080
#define XL_NETDIAG_TX_FATALERR		0x0100
#define XL_NETDIAG_TRANSMITTING		0x0200
#define XL_NETDIAG_RX_ENABLED		0x0400
#define XL_NETDIAG_TX_ENABLED		0x0800
#define XL_NETDIAG_FIFO_LOOPBACK	0x1000
#define XL_NETDIAG_MAC_LOOPBACK		0x2000
#define XL_NETDIAG_ENDEC_LOOPBACK	0x4000
#define XL_NETDIAG_EXTERNAL_LOOP	0x8000

/*
 * Window 5
 */
#define XL_W5_STAT_ENB		0x0C
#define XL_W5_INTR_ENB		0x0A
#define XL_W5_RECLAIM_THRESH	0x09	/* 3c905B only */
#define XL_W5_RX_FILTER		0x08
#define XL_W5_RX_EARLYTHRESH	0x06
#define XL_W5_TX_AVAILTHRESH	0x02
#define XL_W5_TX_STARTTHRESH	0x00

/*
 * RX filter bits
 */
#define XL_RXFILTER_INDIVIDUAL	0x01
#define XL_RXFILTER_ALLMULTI	0x02
#define XL_RXFILTER_BROADCAST	0x04
#define XL_RXFILTER_ALLFRAMES	0x08
#define XL_RXFILTER_MULTIHASH	0x10 /* 3c905B only */

/*
 * Window 6 (stats)
 */
#define XL_W6_TX_BYTES_OK	0x0C
#define XL_W6_RX_BYTES_OK	0x0A
#define XL_W6_UPPER_FRAMES_OK	0x09
#define XL_W6_DEFERRED		0x08
#define XL_W6_RX_OK		0x07
#define XL_W6_TX_OK		0x06
#define XL_W6_RX_OVERRUN	0x05
#define XL_W6_COL_LATE		0x04
#define XL_W6_COL_SINGLE	0x03
#define XL_W6_COL_MULTIPLE	0x02
#define XL_W6_SQE_ERRORS	0x01
#define XL_W6_CARRIER_LOST	0x00

/*
 * Window 7 (bus master control)
 */
#define XL_W7_BM_ADDR		0x00
#define XL_W7_BM_LEN		0x06
#define XL_W7_BM_STATUS		0x0B
#define XL_W7_BM_TIMEr		0x0A

/*
 * bus master control registers
 */
#define XL_BM_PKTSTAT		0x20
#define XL_BM_DOWNLISTPTR	0x24
#define XL_BM_FRAGADDR		0x28
#define XL_BM_FRAGLEN		0x2C
#define XL_BM_TXFREETHRESH	0x2F
#define XL_BM_UPPKTSTAT		0x30
#define XL_BM_UPLISTPTR		0x38

#define XL_LAST_FRAG		0x80000000

/*
 * Boomerang/Cyclone TX/RX list structure.
 * For the TX lists, bits 0 to 12 of the status word indicate
 * length.
 * This looks suspiciously like the ThunderLAN, doesn't it.
 */
struct xl_frag {
	u_int32_t		xl_addr;	/* 63 addr/len pairs */
	u_int32_t		xl_len;
};

struct xl_list {
	u_int32_t		xl_next;	/* final entry has 0 nextptr */
	u_int32_t		xl_status;
	struct xl_frag		xl_frag[63];
};

struct xl_list_onefrag {
	u_int32_t		xl_next;	/* final entry has 0 nextptr */
	u_int32_t		xl_status;
	struct xl_frag		xl_frag;
};

#define XL_MAXFRAGS		63
#define XL_RX_LIST_CNT		16
#define XL_TX_LIST_CNT		16
#define XL_MIN_FRAMELEN		60

struct xl_list_data {
	struct xl_list_onefrag	xl_rx_list[XL_RX_LIST_CNT];
	struct xl_list		xl_tx_list[XL_TX_LIST_CNT];
	unsigned char		xl_pad[XL_MIN_FRAMELEN];
};

struct xl_chain {
	struct xl_list		*xl_ptr;
	struct mbuf		*xl_mbuf;
	struct xl_chain		*xl_next;
};

struct xl_chain_onefrag {
	struct xl_list_onefrag	*xl_ptr;
	struct mbuf		*xl_mbuf;
	struct xl_chain_onefrag	*xl_next;
};

struct xl_chain_data {
	struct xl_chain_onefrag	xl_rx_chain[XL_RX_LIST_CNT];
	struct xl_chain		xl_tx_chain[XL_TX_LIST_CNT];

	struct xl_chain_onefrag	*xl_rx_head;

	struct xl_chain		*xl_tx_head;
	struct xl_chain		*xl_tx_tail;
	struct xl_chain		*xl_tx_free;
};

#define XL_RXSTAT_LENMASK	0x00001FFF
#define XL_RXSTAT_UP_ERROR	0x00004000
#define XL_RXSTAT_UP_CMPLT	0x00008000
#define XL_RXSTAT_UP_OVERRUN	0x00010000
#define XL_RXSTAT_RUNT		0x00020000
#define XL_RXSTAT_ALIGN		0x00040000
#define XL_RXSTAT_CRC		0x00080000
#define XL_RXSTAT_OVERSIZE	0x00100000
#define XL_RXSTAT_DRIBBLE	0x00800000
#define XL_RXSTAT_UP_OFLOW	0x01000000
#define XL_RXSTAT_IPCKERR	0x02000000	/* 3c905B only */
#define XL_RXSTAT_TCPCKERR	0x04000000	/* 3c905B only */
#define XL_RXSTAT_UDPCKERR	0x08000000	/* 3c905B only */
#define XL_RXSTAT_BUFEN		0x10000000	/* 3c905B only */
#define XL_RXSTAT_IPCKOK	0x20000000	/* 3c905B only */
#define XL_RXSTAT_TCPCOK	0x40000000	/* 3c905B only */
#define XL_RXSTAT_UDPCKOK	0x80000000	/* 3c905B only */

#define XL_TXSTAT_LENMASK	0x00001FFF
#define XL_TXSTAT_CRCDIS	0x00002000
#define XL_TXSTAT_TX_INTR	0x00008000
#define XL_TXSTAT_DL_COMPLETE	0x00010000
#define XL_TXSTAT_IPCKSUM	0x02000000	/* 3c905B only */
#define XL_TXSTAT_TCPCKSUM	0x04000000	/* 3c905B only */
#define XL_TXSTAT_UDPCKSUM	0x08000000	/* 3c905B only */
#define XL_TXSTAT_DL_INTR	0x80000000

#define XL_CAPABILITY_BM	0x20


struct xl_type {
	u_int16_t		xl_vid;
	u_int16_t		xl_did;
	char			*xl_name;
};

struct xl_mii_frame {
	u_int8_t		mii_stdelim;
	u_int8_t		mii_opcode;
	u_int8_t		mii_phyaddr;
	u_int8_t		mii_regaddr;
	u_int8_t		mii_turnaround;
	u_int16_t		mii_data;
};

/*
 * MII constants
 */
#define XL_MII_STARTDELIM	0x01
#define XL_MII_READOP		0x02
#define XL_MII_WRITEOP		0x01
#define XL_MII_TURNAROUND	0x02

/*
 * The 3C905B adapters implement a few features that we want to
 * take advantage of, namely the multicast hash filter. With older
 * chips, you only have the option of turning on reception of all
 * multicast frames, which is kind of lame.
 */
#define XL_TYPE_905B	1
#define XL_TYPE_90X	2

#define XL_FLAG_FORCEDELAY	1
#define XL_FLAG_SCHEDDELAY	2
#define XL_FLAG_DELAYTIMEO	3	

struct xl_softc {
#ifdef __OpenBSD__
	struct device		sc_dev;		/* generic device structure */
	void *			sc_ih;		/* interrupt handler cookie */
	bus_space_tag_t		sc_st;		/* bus space tag */
	bus_space_handle_t	sc_sh;		/* bus space handle */
#endif
	struct arpcom		arpcom;		/* interface info */
	struct ifmedia		ifmedia;	/* media info */
	u_int32_t		iobase;		/* pointer to PIO space */
#ifndef XL_USEIOSPACE
	volatile caddr_t	csr;		/* pointer to register map */
#endif
	struct xl_type		*xl_info;	/* 3Com adapter info */
	struct xl_type		*xl_pinfo;	/* phy info */
	u_int8_t		xl_unit;	/* interface number */
	u_int8_t		xl_type;
	u_int8_t		xl_phy_addr;	/* PHY address */
	u_int32_t		xl_xcvr;
	u_int16_t		xl_media;
	u_int16_t		xl_caps;
	u_int8_t		xl_tx_pend;	/* TX pending */
	u_int8_t		xl_want_auto;
	u_int8_t		xl_autoneg;
	u_int8_t		xl_stats_no_timeout;
	caddr_t			xl_ldata_ptr;
	struct xl_list_data	*xl_ldata;
	struct xl_chain_data	xl_cdata;
#ifdef __FreeBSD__
	struct callout_handle	xl_stat_ch;
#endif
};

#define xl_rx_goodframes(x) \
	((x.xl_upper_frames_ok & 0x03) << 8) | x.xl_rx_frames_ok

#define xl_tx_goodframes(x) \
	((x.xl_upper_frames_ok & 0x30) << 4) | x.xl_tx_frames_ok

struct xl_stats {
	u_int8_t		xl_carrier_lost;
	u_int8_t		xl_sqe_errs;
	u_int8_t		xl_tx_multi_collision;
	u_int8_t		xl_tx_single_collision;
	u_int8_t		xl_tx_late_collision;
	u_int8_t		xl_rx_overrun;
	u_int8_t		xl_tx_frames_ok;
	u_int8_t		xl_rx_frames_ok;
	u_int8_t		xl_tx_deferred;
	u_int8_t		xl_upper_frames_ok;
	u_int16_t		xl_rx_bytes_ok;
	u_int16_t		xl_tx_bytes_ok;
	u_int16_t		status;
};

/*
 * register space access macros
 */
#ifdef __FreeBSD__
#ifdef XL_USEIOSPACE
#define CSR_WRITE_4(sc, reg, val)	\
	outl(sc->iobase + (u_int32_t)(reg), val)
#define CSR_WRITE_2(sc, reg, val)	\
	outw(sc->iobase + (u_int32_t)(reg), val)
#define CSR_WRITE_1(sc, reg, val)	\
	outb(sc->iobase + (u_int32_t)(reg), val)

#define CSR_READ_4(sc, reg)	\
	inl(sc->iobase + (u_int32_t)(reg))
#define CSR_READ_2(sc, reg)	\
	inw(sc->iobase + (u_int32_t)(reg))
#define CSR_READ_1(sc, reg)	\
	inb(sc->iobase + (u_int32_t)(reg))
#else
#define CSR_WRITE_4(sc, reg, val)	\
	((*(u_int32_t*)((sc)->csr + (u_int32_t)(reg))) = (u_int32_t)(val))
#define CSR_WRITE_2(sc, reg, val)	\
	((*(u_int16_t*)((sc)->csr + (u_int32_t)(reg))) = (u_int16_t)(val))
#define CSR_WRITE_1(sc, reg, val)	\
	((*(u_int8_t*)((sc)->csr + (u_int32_t)(reg))) = (u_int8_t)(val))

#define CSR_READ_4(sc, reg)	\
	(*(u_int32_t *)((sc)->csr + (u_int32_t)(reg)))
#define CSR_READ_2(sc, reg)	\
	(*(u_int16_t *)((sc)->csr + (u_int32_t)(reg)))
#define CSR_READ_1(sc, reg)	\
	(*(u_int8_t *)((sc)->csr + (u_int32_t)(reg)))
#endif
#endif

#if defined(__OpenBSD__)
#define CSR_WRITE_4(sc, csr, val) \
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, csr, (val))
#define CSR_WRITE_2(sc, csr, val) \
	bus_space_write_2((sc)->sc_st, (sc)->sc_sh, csr, (val))
#define CSR_WRITE_1(sc, csr, val) \
	bus_space_write_1((sc)->sc_st, (sc)->sc_sh, csr, (val))

#define CSR_READ_4(sc, csr) \
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, csr)
#define CSR_READ_2(sc, csr) \
	bus_space_read_2((sc)->sc_st, (sc)->sc_sh, csr)
#define CSR_READ_1(sc, csr) \
	bus_space_read_1((sc)->sc_st, (sc)->sc_sh, csr)
#endif /* __OpenBSD__ */

#define XL_SEL_WIN(x)	\
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_WINSEL | x)
#define XL_TIMEOUT		1000

/*
 * General constants that are fun to know.
 *
 * 3Com PCI vendor ID
 */
#define	TC_VENDORID		0x10B7

/*
 * 3Com chip device IDs.
 */
#define	TC_DEVICEID_BOOMERANG_10BT		0x9000
#define TC_DEVICEID_BOOMERANG_10BT_COMBO	0x9001
#define TC_DEVICEID_BOOMERANG_10_100BT		0x9050
#define TC_DEVICEID_BOOMERANG_100BT4		0x9051
#define TC_DEVICEID_CYCLONE_10BT		0x9004
#define TC_DEVICEID_CYCLONE_10BT_COMBO		0x9005
#define TC_DEVICEID_CYCLONE_10_100BT		0x9055
#define TC_DEVICEID_CYCLONE_10_100BT4		0x9056
#define TC_DEVICEID_CYCLONE_10_100BT_SERV	0x9800

/*
 * Texas Instruments PHY identifiers
 *
 * The ThunderLAN manual has a curious and confusing error in it.
 * In chapter 7, which describes PHYs, it says that TI PHYs have
 * the following ID codes, where xx denotes a revision:
 *
 * 0x4000501xx			internal 10baseT PHY
 * 0x4000502xx			TNETE211 100VG-AnyLan PMI
 *
 * The problem here is that these are not valid 32-bit hex numbers:
 * there's one digit too many. My guess is that they mean the internal
 * 10baseT PHY is 0x4000501x and the TNETE211 is 0x4000502x since these
 * are the only numbers that make sense.
 */
#define TI_PHY_VENDORID		0x4000
#define TI_PHY_10BT		0x501F
#define TI_PHY_100VGPMI		0x502F

/*
 * These ID values are for the NS DP83840A 10/100 PHY
 */
#define NS_PHY_VENDORID		0x2000
#define NS_PHY_83840A		0x5C0F

/*
 * Level 1 10/100 PHY
 */
#define LEVEL1_PHY_VENDORID	0x7810
#define LEVEL1_PHY_LXT970	0x000F

/*
 * Intel 82555 10/100 PHY
 */
#define INTEL_PHY_VENDORID	0x0A28
#define INTEL_PHY_82555		0x015F

/*
 * SEEQ 80220 10/100 PHY
 */
#define SEEQ_PHY_VENDORID	0x0016
#define SEEQ_PHY_80220		0xF83F


/*
 * PCI low memory base and low I/O base register, and
 * other PCI registers. Note: some are only available on
 * the 3c905B, in particular those that related to power management.
 */

#define XL_PCI_VENDOR_ID	0x00
#define XL_PCI_DEVICE_ID	0x02
#define XL_PCI_COMMAND		0x04
#define XL_PCI_STATUS		0x06
#define XL_PCI_CLASSCODE	0x09
#define XL_PCI_LATENCY_TIMER	0x0D
#define XL_PCI_HEADER_TYPE	0x0E
#define XL_PCI_LOIO		0x10
#define XL_PCI_LOMEM		0x14
#define XL_PCI_BIOSROM		0x30
#define XL_PCI_INTLINE		0x3C
#define XL_PCI_INTPIN		0x3D
#define XL_PCI_MINGNT		0x3E
#define XL_PCI_MINLAT		0x0F
#define XL_PCI_RESETOPT		0x48
#define XL_PCI_EEPROM_DATA	0x4C

/* 3c905B-only registers */
#define XL_PCI_CAPID		0xDC /* 8 bits */
#define XL_PCI_NEXTPTR		0xDD /* 8 bits */
#define XL_PCI_PWRMGMTCAP	0xDE /* 16 bits */
#define XL_PCI_PWRMGMTCTRL	0xE0 /* 16 bits */

#define XL_PSTATE_MASK		0x0003
#define XL_PSTATE_D0		0x0000
#define XL_PSTATE_D1		0x0002
#define XL_PSTATE_D2		0x0002
#define XL_PSTATE_D3		0x0003
#define XL_PME_EN		0x0010
#define XL_PME_STATUS		0x8000

#define PHY_UNKNOWN		6

#define XL_PHYADDR_MIN		0x00
#define XL_PHYADDR_MAX		0x1F

#define XL_PHY_GENCTL		0x00
#define XL_PHY_GENSTS		0x01
#define XL_PHY_VENID		0x02
#define XL_PHY_DEVID		0x03
#define XL_PHY_ANAR		0x04
#define XL_PHY_LPAR		0x05
#define XL_PHY_ANEXP		0x06

#define PHY_ANAR_NEXTPAGE	0x8000
#define PHY_ANAR_RSVD0		0x4000
#define PHY_ANAR_TLRFLT		0x2000
#define PHY_ANAR_RSVD1		0x1000
#define PHY_ANAR_RSVD2		0x0800
#define PHY_ANAR_RSVD3		0x0400
#define PHY_ANAR_100BT4		0x0200
#define PHY_ANAR_100BTXFULL	0x0100
#define PHY_ANAR_100BTXHALF	0x0080
#define PHY_ANAR_10BTFULL	0x0040
#define PHY_ANAR_10BTHALF	0x0020
#define PHY_ANAR_PROTO4		0x0010
#define PHY_ANAR_PROTO3		0x0008
#define PHY_ANAR_PROTO2		0x0004
#define PHY_ANAR_PROTO1		0x0002
#define PHY_ANAR_PROTO0		0x0001

/*
 * These are the register definitions for the PHY (physical layer
 * interface chip).
 */
/*
 * PHY BMCR Basic Mode Control Register
 */
#define PHY_BMCR			0x00
#define PHY_BMCR_RESET			0x8000
#define PHY_BMCR_LOOPBK			0x4000
#define PHY_BMCR_SPEEDSEL		0x2000
#define PHY_BMCR_AUTONEGENBL		0x1000
#define PHY_BMCR_RSVD0			0x0800	/* write as zero */
#define PHY_BMCR_ISOLATE		0x0400
#define PHY_BMCR_AUTONEGRSTR		0x0200
#define PHY_BMCR_DUPLEX			0x0100
#define PHY_BMCR_COLLTEST		0x0080
#define PHY_BMCR_RSVD1			0x0040	/* write as zero, don't care */
#define PHY_BMCR_RSVD2			0x0020	/* write as zero, don't care */
#define PHY_BMCR_RSVD3			0x0010	/* write as zero, don't care */
#define PHY_BMCR_RSVD4			0x0008	/* write as zero, don't care */
#define PHY_BMCR_RSVD5			0x0004	/* write as zero, don't care */
#define PHY_BMCR_RSVD6			0x0002	/* write as zero, don't care */
#define PHY_BMCR_RSVD7			0x0001	/* write as zero, don't care */
/*
 * RESET: 1 == software reset, 0 == normal operation
 * Resets status and control registers to default values.
 * Relatches all hardware config values.
 *
 * LOOPBK: 1 == loopback operation enabled, 0 == normal operation
 *
 * SPEEDSEL: 1 == 100Mb/s, 0 == 10Mb/s
 * Link speed is selected byt his bit or if auto-negotiation if bit
 * 12 (AUTONEGENBL) is set (in which case the value of this register
 * is ignored).
 *
 * AUTONEGENBL: 1 == Autonegotiation enabled, 0 == Autonegotiation disabled
 * Bits 8 and 13 are ignored when autoneg is set, otherwise bits 8 and 13
 * determine speed and mode. Should be cleared and then set if PHY configured
 * for no autoneg on startup.
 *
 * ISOLATE: 1 == isolate PHY from MII, 0 == normal operation
 *
 * AUTONEGRSTR: 1 == restart autonegotiation, 0 = normal operation
 *
 * DUPLEX: 1 == full duplex mode, 0 == half duplex mode
 *
 * COLLTEST: 1 == collision test enabled, 0 == normal operation
 */

/* 
 * PHY, BMSR Basic Mode Status Register 
 */   
#define PHY_BMSR			0x01
#define PHY_BMSR_100BT4			0x8000
#define PHY_BMSR_100BTXFULL		0x4000
#define PHY_BMSR_100BTXHALF		0x2000
#define PHY_BMSR_10BTFULL		0x1000
#define PHY_BMSR_10BTHALF		0x0800
#define PHY_BMSR_RSVD1			0x0400	/* write as zero, don't care */
#define PHY_BMSR_RSVD2			0x0200	/* write as zero, don't care */
#define PHY_BMSR_RSVD3			0x0100	/* write as zero, don't care */
#define PHY_BMSR_RSVD4			0x0080	/* write as zero, don't care */
#define PHY_BMSR_MFPRESUP		0x0040
#define PHY_BMSR_AUTONEGCOMP		0x0020
#define PHY_BMSR_REMFAULT		0x0010
#define PHY_BMSR_CANAUTONEG		0x0008
#define PHY_BMSR_LINKSTAT		0x0004
#define PHY_BMSR_JABBER			0x0002
#define PHY_BMSR_EXTENDED		0x0001
