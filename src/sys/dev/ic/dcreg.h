/*	$OpenBSD: dcreg.h,v 1.7 2000/08/02 19:01:06 aaron Exp $ */

/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 * $FreeBSD: src/sys/pci/if_dcreg.h,v 1.9 2000/08/02 16:31:11 wpaul Exp $
 */

/*
 * 21143 and clone common register definitions.
 */

#define DC_BUSCTL		0x00	/* bus control */
#define DC_TXSTART		0x08	/* tx start demand */
#define DC_RXSTART		0x10	/* rx start demand */
#define DC_RXADDR		0x18	/* rx descriptor list start addr */
#define DC_TXADDR		0x20	/* tx descriptor list start addr */
#define DC_ISR			0x28	/* interrupt status register */
#define DC_NETCFG		0x30	/* network config register */
#define DC_IMR			0x38	/* interrupt mask */
#define DC_FRAMESDISCARDED	0x40	/* # of discarded frames */
#define DC_SIO			0x48	/* MII and ROM/EEPROM access */
#define DC_ROM			0x50	/* ROM programming address */
#define DC_TIMER		0x58	/* general timer */
#define DC_10BTSTAT		0x60	/* SIA status */
#define DC_SIARESET		0x68	/* SIA connectivity */
#define DC_10BTCTRL		0x70	/* SIA transmit and receive */
#define DC_WATCHDOG		0x78	/* SIA and general purpose port */

/*
 * There are two general 'types' of MX chips that we need to be
 * concerned with. One is the original 98713, which has its internal
 * NWAY support controlled via the MDIO bits in the serial I/O
 * register. The other is everything else (from the 98713A on up),
 * which has its internal NWAY controlled via CSR13, CSR14 and CSR15,
 * just like the 21143. This type setting also governs which of the
 * 'magic' numbers we write to CSR16. The PNIC II falls into the
 * 98713A/98715/98715A/98725 category.
 */
#define DC_TYPE_98713		0x1
#define DC_TYPE_98713A		0x2
#define DC_TYPE_987x5		0x3

/* Other type of supported chips. */
#define DC_TYPE_21143		0x4	/* Intel 21143 */
#define DC_TYPE_ASIX		0x5	/* ASIX AX88140A/AX88141 */
#define DC_TYPE_AL981		0x6	/* ADMtek AL981 Comet */
#define DC_TYPE_AN983		0x7	/* ADMtek AN983 Centaur */
#define DC_TYPE_DM9102		0x8	/* Davicom DM9102 */
#define DC_TYPE_PNICII		0x9	/* 82c115 PNIC II */
#define DC_TYPE_PNIC		0xA	/* 82c168/82c169 PNIC I */

#define DC_IS_MACRONIX(x)			\
	(x->dc_type == DC_TYPE_98713 ||		\
	 x->dc_type == DC_TYPE_98713A ||	\
	 x->dc_type == DC_TYPE_987x5)

#define DC_IS_ADMTEK(x)				\
	(x->dc_type == DC_TYPE_AL981 ||		\
	 x->dc_type == DC_TYPE_AN983)

#define DC_IS_INTEL(x)		(x->dc_type == DC_TYPE_21143)
#define DC_IS_ASIX(x)		(x->dc_type == DC_TYPE_ASIX)
#define DC_IS_COMET(x)		(x->dc_type == DC_TYPE_AL981)
#define DC_IS_CENTAUR(x)	(x->dc_type == DC_TYPE_AN983)
#define DC_IS_DAVICOM(x)	(x->dc_type == DC_TYPE_DM9102)
#define DC_IS_PNICII(x)		(x->dc_type == DC_TYPE_PNICII)
#define DC_IS_PNIC(x)		(x->dc_type == DC_TYPE_PNIC)

/* MII/symbol mode port types */
#define DC_PMODE_MII		0x1
#define DC_PMODE_SYM		0x2

/*
 * Bus control bits.
 */
#define DC_BUSCTL_RESET		0x00000001
#define DC_BUSCTL_ARBITRATION	0x00000002
#define DC_BUSCTL_SKIPLEN	0x0000007C
#define DC_BUSCTL_BUF_BIGENDIAN	0x00000080
#define DC_BUSCTL_BURSTLEN	0x00003F00
#define DC_BUSCTL_CACHEALIGN	0x0000C000
#define DC_BUSCTL_TXPOLL	0x000E0000
#define DC_BUSCTL_DBO		0x00100000
#define DC_BUSCTL_MRME		0x00200000
#define DC_BUSCTL_MRLE		0x00800000
#define DC_BUSCTL_MWIE		0x01000000
#define DC_BUSCTL_ONNOW_ENB	0x04000000

#define DC_SKIPLEN_1LONG	0x00000004
#define DC_SKIPLEN_2LONG	0x00000008
#define DC_SKIPLEN_3LONG	0x00000010
#define DC_SKIPLEN_4LONG	0x00000020
#define DC_SKIPLEN_5LONG	0x00000040

#define DC_CACHEALIGN_NONE	0x00000000
#define DC_CACHEALIGN_8LONG	0x00004000
#define DC_CACHEALIGN_16LONG	0x00008000
#define DC_CACHEALIGN_32LONG	0x0000C000

#define DC_BURSTLEN_USECA	0x00000000
#define DC_BURSTLEN_1LONG	0x00000100
#define DC_BURSTLEN_2LONG	0x00000200
#define DC_BURSTLEN_4LONG	0x00000400
#define DC_BURSTLEN_8LONG	0x00000800
#define DC_BURSTLEN_16LONG	0x00001000
#define DC_BURSTLEN_32LONG	0x00002000

#define DC_TXPOLL_OFF		0x00000000
#define DC_TXPOLL_1		0x00020000
#define DC_TXPOLL_2		0x00040000
#define DC_TXPOLL_3		0x00060000
#define DC_TXPOLL_4		0x00080000
#define DC_TXPOLL_5		0x000A0000
#define DC_TXPOLL_6		0x000C0000
#define DC_TXPOLL_7		0x000E0000

/*
 * Interrupt status bits.
 */
#define DC_ISR_TX_OK		0x00000001
#define DC_ISR_TX_IDLE		0x00000002
#define DC_ISR_TX_NOBUF		0x00000004
#define DC_ISR_TX_JABBERTIMEO	0x00000008
#define DC_ISR_LINKGOOD		0x00000010
#define DC_ISR_TX_UNDERRUN	0x00000020
#define DC_ISR_RX_OK		0x00000040
#define DC_ISR_RX_NOBUF		0x00000080
#define DC_ISR_RX_READ		0x00000100
#define DC_ISR_RX_WATDOGTIMEO	0x00000200
#define DC_ISR_TX_EARLY		0x00000400
#define DC_ISR_TIMER_EXPIRED	0x00000800
#define DC_ISR_LINKFAIL		0x00001000
#define DC_ISR_BUS_ERR		0x00002000
#define DC_ISR_RX_EARLY		0x00004000
#define DC_ISR_ABNORMAL		0x00008000
#define DC_ISR_NORMAL		0x00010000
#define DC_ISR_RX_STATE		0x000E0000
#define DC_ISR_TX_STATE		0x00700000
#define DC_ISR_BUSERRTYPE	0x03800000
#define DC_ISR_100MBPSLINK	0x08000000
#define DC_ISR_MAGICKPACK	0x10000000

#define DC_RXSTATE_STOPPED	0x00000000	/* 000 - Stopped */
#define DC_RXSTATE_FETCH	0x00020000	/* 001 - Fetching descriptor */
#define DC_RXSTATE_ENDCHECK	0x00040000	/* 010 - check for rx end */
#define DC_RXSTATE_WAIT		0x00060000	/* 011 - waiting for packet */
#define DC_RXSTATE_SUSPEND	0x00080000	/* 100 - suspend rx */
#define DC_RXSTATE_CLOSE	0x000A0000	/* 101 - close tx desc */
#define DC_RXSTATE_FLUSH	0x000C0000	/* 110 - flush from FIFO */
#define DC_RXSTATE_DEQUEUE	0x000E0000	/* 111 - dequeue from FIFO */

#define DC_TXSTATE_RESET	0x00000000	/* 000 - reset */
#define DC_TXSTATE_FETCH	0x00100000	/* 001 - fetching descriptor */
#define DC_TXSTATE_WAITEND	0x00200000	/* 010 - wait for tx end */
#define DC_TXSTATE_READING	0x00300000	/* 011 - read and enqueue */
#define DC_TXSTATE_RSVD		0x00400000	/* 100 - reserved */
#define DC_TXSTATE_SETUP	0x00500000	/* 101 - setup packet */
#define DC_TXSTATE_SUSPEND	0x00600000	/* 110 - suspend tx */
#define DC_TXSTATE_CLOSE	0x00700000	/* 111 - close tx desc */

/*
 * Network config bits.
 */
#define DC_NETCFG_RX_HASHPERF	0x00000001
#define DC_NETCFG_RX_ON		0x00000002
#define DC_NETCFG_RX_HASHONLY	0x00000004
#define DC_NETCFG_RX_BADFRAMES	0x00000008
#define DC_NETCFG_RX_INVFILT	0x00000010
#define DC_NETCFG_BACKOFFCNT	0x00000020
#define DC_NETCFG_RX_PROMISC	0x00000040
#define DC_NETCFG_RX_ALLMULTI	0x00000080
#define DC_NETCFG_FULLDUPLEX	0x00000200
#define DC_NETCFG_LOOPBACK	0x00000C00
#define DC_NETCFG_FORCECOLL	0x00001000
#define DC_NETCFG_TX_ON		0x00002000
#define DC_NETCFG_TX_THRESH	0x0000C000
#define DC_NETCFG_TX_BACKOFF	0x00020000
#define DC_NETCFG_PORTSEL	0x00040000	/* 0 == 10, 1 == 100 */
#define DC_NETCFG_HEARTBEAT	0x00080000
#define DC_NETCFG_STORENFWD	0x00200000
#define DC_NETCFG_SPEEDSEL	0x00400000	/* 1 == 10, 0 == 100 */
#define DC_NETCFG_PCS		0x00800000
#define DC_NETCFG_SCRAMBLER	0x01000000
#define DC_NETCFG_NO_RXCRC	0x02000000
#define DC_NETCFG_RX_ALL	0x40000000
#define DC_NETCFG_CAPEFFECT	0x80000000

#define DC_OPMODE_NORM		0x00000000
#define DC_OPMODE_INTLOOP	0x00000400
#define DC_OPMODE_EXTLOOP	0x00000800

#define DC_TXTHRESH_72BYTES	0x00000000
#define DC_TXTHRESH_96BYTES	0x00004000
#define DC_TXTHRESH_128BYTES	0x00008000
#define DC_TXTHRESH_160BYTES	0x0000C000


/*
 * Interrupt mask bits.
 */
#define DC_IMR_TX_OK		0x00000001
#define DC_IMR_TX_IDLE		0x00000002
#define DC_IMR_TX_NOBUF		0x00000004
#define DC_IMR_TX_JABBERTIMEO	0x00000008
#define DC_IMR_LINKGOOD		0x00000010
#define DC_IMR_TX_UNDERRUN	0x00000020
#define DC_IMR_RX_OK		0x00000040
#define DC_IMR_RX_NOBUF		0x00000080
#define DC_IMR_RX_READ		0x00000100
#define DC_IMR_RX_WATDOGTIMEO	0x00000200
#define DC_IMR_TX_EARLY		0x00000400
#define DC_IMR_TIMER_EXPIRED	0x00000800
#define DC_IMR_LINKFAIL		0x00001000
#define DC_IMR_BUS_ERR		0x00002000
#define DC_IMR_RX_EARLY		0x00004000
#define DC_IMR_ABNORMAL		0x00008000
#define DC_IMR_NORMAL		0x00010000
#define DC_IMR_100MBPSLINK	0x08000000
#define DC_IMR_MAGICKPACK	0x10000000

#define DC_INTRS	\
	(DC_IMR_RX_OK|DC_IMR_TX_OK|DC_IMR_RX_NOBUF|DC_IMR_RX_WATDOGTIMEO|\
	DC_IMR_TX_NOBUF|DC_IMR_TX_UNDERRUN|DC_IMR_BUS_ERR|		\
	DC_IMR_ABNORMAL|DC_IMR_NORMAL/*|DC_IMR_TX_EARLY*/)
/*
 * Serial I/O (EEPROM/ROM) bits.
 */
#define DC_SIO_EE_CS		0x00000001	/* EEPROM chip select */
#define DC_SIO_EE_CLK		0x00000002	/* EEPROM clock */
#define DC_SIO_EE_DATAIN	0x00000004	/* EEPROM data output */
#define DC_SIO_EE_DATAOUT	0x00000008	/* EEPROM data input */
#define DC_SIO_ROMDATA4		0x00000010
#define DC_SIO_ROMDATA5		0x00000020
#define DC_SIO_ROMDATA6		0x00000040
#define DC_SIO_ROMDATA7		0x00000080
#define DC_SIO_EESEL		0x00000800
#define DC_SIO_ROMSEL		0x00001000
#define DC_SIO_ROMCTL_WRITE	0x00002000
#define DC_SIO_ROMCTL_READ	0x00004000
#define DC_SIO_MII_CLK		0x00010000	/* MDIO clock */
#define DC_SIO_MII_DATAOUT	0x00020000	/* MDIO data out */
#define DC_SIO_MII_DIR		0x00040000	/* MDIO dir */
#define DC_SIO_MII_DATAIN	0x00080000	/* MDIO data in */

#define DC_EECMD_WRITE		0x140
#define DC_EECMD_READ		0x180
#define DC_EECMD_ERASE		0x1c0

#define DC_EE_NODEADDR_OFFSET	0x70
#define DC_EE_NODEADDR		10

/*
 * General purpose timer register
 */
#define DC_TIMER_VALUE		0x0000FFFF
#define DC_TIMER_CONTINUOUS	0x00010000

/*
 * 10baseT status register
 */
#define DC_TSTAT_MIIACT		0x00000001 /* MII port activity */
#define DC_TSTAT_LS100		0x00000002 /* link status of 100baseTX */
#define DC_TSTAT_LS10		0x00000004 /* link status of 10baseT */
#define DC_TSTAT_AUTOPOLARITY	0x00000008
#define DC_TSTAT_AUIACT		0x00000100 /* AUI activity */
#define DC_TSTAT_10BTACT	0x00000200 /* 10baseT activity */
#define DC_TSTAT_NSN		0x00000400 /* non-stable FLPs detected */
#define DC_TSTAT_REMFAULT	0x00000800
#define DC_TSTAT_ANEGSTAT	0x00007000
#define DC_TSTAT_LP_CAN_NWAY	0x00008000 /* link partner supports NWAY */
#define DC_TSTAT_LPCODEWORD	0xFFFF0000 /* link partner's code word */

#define DC_ASTAT_DISABLE	0x00000000
#define DC_ASTAT_TXDISABLE	0x00001000
#define DC_ASTAT_ABDETECT	0x00002000
#define DC_ASTAT_ACKDETECT	0x00003000
#define DC_ASTAT_CMPACKDETECT	0x00004000
#define DC_ASTAT_AUTONEGCMP	0x00005000
#define DC_ASTAT_LINKCHECK	0x00006000

/*
 * PHY reset register
 */
#define DC_SIA_RESET		0x00000001
#define DC_SIA_AUI		0x00000008 /* AUI or 10baseT */

/*
 * 10baseT control register
 */
#define DC_TCTL_ENCODER_ENB	0x00000001
#define DC_TCTL_LOOPBACK	0x00000002
#define DC_TCTL_DRIVER_ENB	0x00000004
#define DC_TCTL_LNKPULSE_ENB	0x00000008
#define DC_TCTL_HALFDUPLEX	0x00000040
#define DC_TCTL_AUTONEGENBL	0x00000080
#define DC_TCTL_RX_SQUELCH	0x00000100
#define DC_TCTL_COLL_SQUELCH	0x00000200
#define DC_TCTL_COLL_DETECT	0x00000400
#define DC_TCTL_SQE_ENB		0x00000800
#define DC_TCTL_LINKTEST	0x00001000
#define DC_TCTL_AUTOPOLARITY	0x00002000
#define DC_TCTL_SET_POL_PLUS	0x00004000
#define DC_TCTL_AUTOSENSE	0x00008000	/* 10bt/AUI autosense */
#define DC_TCTL_100BTXHALF	0x00010000
#define DC_TCTL_100BTXFULL	0x00020000
#define DC_TCTL_100BT4		0x00040000

/*
 * Watchdog timer register
 */
#define DC_WDOG_JABBERDIS	0x00000001
#define DC_WDOG_HOSTUNJAB	0x00000002
#define DC_WDOG_JABBERCLK	0x00000004
#define DC_WDOG_RXWDOGDIS	0x00000010
#define DC_WDOG_RXWDOGCLK	0x00000020
#define DC_WDOG_MUSTBEZERO	0x00000100
#define DC_WDOG_CTLWREN		0x08000000

/*
 * Size of a setup frame.
 */
#define DC_SFRAME_LEN		192

/*
 * 21x4x TX/RX list structure.
 */

struct dc_desc {
	u_int32_t		dc_status;
	u_int32_t		dc_ctl;
	u_int32_t		dc_ptr1;
	u_int32_t		dc_ptr2;
};

#define dc_data		dc_ptr1
#define dc_next		dc_ptr2

#define DC_RXSTAT_FIFOOFLOW	0x00000001
#define DC_RXSTAT_CRCERR	0x00000002
#define DC_RXSTAT_DRIBBLE	0x00000004
#define DC_RXSTAT_WATCHDOG	0x00000010
#define DC_RXSTAT_FRAMETYPE	0x00000020	/* 0 == IEEE 802.3 */
#define DC_RXSTAT_COLLSEEN	0x00000040
#define DC_RXSTAT_GIANT		0x00000080
#define DC_RXSTAT_LASTFRAG	0x00000100
#define DC_RXSTAT_FIRSTFRAG	0x00000200
#define DC_RXSTAT_MULTICAST	0x00000400
#define DC_RXSTAT_RUNT		0x00000800
#define DC_RXSTAT_RXTYPE	0x00003000
#define DC_RXSTAT_RXERR		0x00008000
#define DC_RXSTAT_RXLEN		0x3FFF0000
#define DC_RXSTAT_OWN		0x80000000

#define DC_RXBYTES(x)		((x & DC_RXSTAT_RXLEN) >> 16)
#define DC_RXSTAT (DC_RXSTAT_FIRSTFRAG|DC_RXSTAT_LASTFRAG|DC_RXSTAT_OWN)

#define DC_RXCTL_BUFLEN1	0x00000FFF
#define DC_RXCTL_BUFLEN2	0x00FFF000
#define DC_RXCTL_RLINK		0x01000000
#define DC_RXCTL_RLAST		0x02000000

#define DC_TXSTAT_DEFER		0x00000001
#define DC_TXSTAT_UNDERRUN	0x00000002
#define DC_TXSTAT_LINKFAIL	0x00000003
#define DC_TXSTAT_COLLCNT	0x00000078
#define DC_TXSTAT_SQE		0x00000080
#define DC_TXSTAT_EXCESSCOLL	0x00000100
#define DC_TXSTAT_LATECOLL	0x00000200
#define DC_TXSTAT_NOCARRIER	0x00000400
#define DC_TXSTAT_CARRLOST	0x00000800
#define DC_TXSTAT_JABTIMEO	0x00004000
#define DC_TXSTAT_ERRSUM	0x00008000
#define DC_TXSTAT_OWN		0x80000000

#define DC_TXCTL_BUFLEN1	0x000007FF
#define DC_TXCTL_BUFLEN2	0x003FF800
#define DC_TXCTL_FILTTYPE0	0x00400000
#define DC_TXCTL_PAD		0x00800000
#define DC_TXCTL_TLINK		0x01000000
#define DC_TXCTL_TLAST		0x02000000
#define DC_TXCTL_NOCRC		0x04000000
#define DC_TXCTL_SETUP		0x08000000
#define DC_TXCTL_FILTTYPE1	0x10000000
#define DC_TXCTL_FIRSTFRAG	0x20000000
#define DC_TXCTL_LASTFRAG	0x40000000
#define DC_TXCTL_FINT		0x80000000

#define DC_FILTER_PERFECT	0x00000000
#define DC_FILTER_HASHPERF	0x00400000
#define DC_FILTER_INVERSE	0x10000000
#define DC_FILTER_HASHONLY	0x10400000

#define DC_MAXFRAGS		16
#define DC_RX_LIST_CNT		64
#define DC_TX_LIST_CNT		256
#define DC_MIN_FRAMELEN		60
#define DC_RXLEN		1536

#define DC_INC(x, y)	(x) = (x + 1) % y

struct dc_list_data {
	struct dc_desc		dc_rx_list[DC_RX_LIST_CNT];
	struct dc_desc		dc_tx_list[DC_TX_LIST_CNT];
};

struct dc_chain_data {
	struct mbuf		*dc_rx_chain[DC_RX_LIST_CNT];
	struct mbuf		*dc_tx_chain[DC_TX_LIST_CNT];
	u_int32_t		dc_sbuf[DC_SFRAME_LEN/sizeof(u_int32_t)];
	u_int8_t		dc_pad[DC_MIN_FRAMELEN];
	int			dc_tx_prod;
	int			dc_tx_cons;
	int			dc_tx_cnt;
	int			dc_rx_prod;
};

struct dc_type {
	u_int16_t		dc_vid;
	u_int16_t		dc_did;
};

struct dc_mii_frame {
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
#define DC_MII_STARTDELIM	0x01
#define DC_MII_READOP		0x02
#define DC_MII_WRITEOP		0x01
#define DC_MII_TURNAROUND	0x02


/*
 * Registers specific to clone devices.
 * This mainly relates to RX filter programming: not all 21x4x clones
 * use the standard DEC filter programming mechanism.
 */

/*
 * ADMtek specific registers and constants for the AL981 and AN983.
 * The AN983 doesn't use the magic PHY registers.
 */
#define DC_AL_PAR0		0xA4	/* station address */
#define DC_AL_PAR1		0xA8	/* station address */
#define DC_AL_MAR0		0xAC	/* multicast hash filter */
#define DC_AL_MAR1		0xB0	/* multicast hash filter */
#define DC_AL_BMCR		0xB4	/* built in PHY control */
#define DC_AL_BMSR		0xB8	/* built in PHY status */
#define DC_AL_VENID		0xBC	/* built in PHY ID0 */
#define DC_AL_DEVID		0xC0	/* built in PHY ID1 */
#define DC_AL_ANAR		0xC4	/* built in PHY autoneg advert */
#define DC_AL_LPAR		0xC8	/* bnilt in PHY link part. ability */
#define DC_AL_ANER		0xCC	/* built in PHY autoneg expansion */

#define DC_ADMTEK_PHYADDR	0x1
#define DC_AL_EE_NODEADDR	4
/* End of ADMtek specific registers */

/*
 * ASIX specific registers.
 */
#define DC_AX_FILTIDX		0x68    /* RX filter index */
#define DC_AX_FILTDATA		0x70    /* RX filter data */

/*
 * Special ASIX-specific bits in the ASIX NETCFG register (CSR6).
 */
#define DC_AX_NETCFG_RX_BROAD	0x00000100 

/*
 * RX Filter Index Register values
 */
#define DC_AX_FILTIDX_PAR0	0x00000000
#define DC_AX_FILTIDX_PAR1	0x00000001
#define DC_AX_FILTIDX_MAR0	0x00000002
#define DC_AX_FILTIDX_MAR1	0x00000003
/* End of ASIX specific registers */

/*
 * Macronix specific registers. The Macronix chips have a special
 * register for reading the NWAY status, which we don't use, plus
 * a magic packet register, which we need to tweak a bit per the
 * Macronix application notes.
 */
#define DC_MX_MAGICPACKET	0x80
#define DC_MX_NWAYSTAT		0xA0

/*
 * Magic packet register
 */
#define DC_MX_MPACK_DISABLE	0x00400000

/*
 * NWAY status register.
 */
#define DC_MX_NWAY_10BTHALF	0x08000000
#define DC_MX_NWAY_10BTFULL	0x10000000
#define DC_MX_NWAY_100BTHALF	0x20000000
#define DC_MX_NWAY_100BTFULL	0x40000000
#define DC_MX_NWAY_100BT4	0x80000000

/*
 * These are magic values that must be written into CSR16
 * (DC_MX_MAGICPACKET) in order to put the chip into proper
 * operating mode. The magic numbers are documented in the
 * Macronix 98715 application notes.
 */
#define DC_MX_MAGIC_98713	0x0F370000
#define DC_MX_MAGIC_98713A	0x0B3C0000
#define DC_MX_MAGIC_98715	0x0B3C0000
#define DC_MX_MAGIC_98725	0x0B3C0000
/* End of Macronix specific registers */

/*
 * PNIC 82c168/82c169 specific registers.
 * The PNIC has its own special NWAY support, which doesn't work,
 * and shortcut ways of reading the EEPROM and MII bus.
 */
#define DC_PN_GPIO		0x60	/* general purpose pins control */
#define DC_PN_PWRUP_CFG		0x90	/* config register, set by EEPROM */
#define DC_PN_SIOCTL		0x98	/* serial EEPROM control register */
#define DC_PN_MII		0xA0	/* MII access register */
#define DC_PN_NWAY		0xB8	/* Internal NWAY register */

/* Serial I/O EEPROM register */
#define DC_PN_SIOCTL_DATA	0x0000003F
#define DC_PN_SIOCTL_OPCODE	0x00000300
#define DC_PN_SIOCTL_BUSY	0x80000000

#define DC_PN_EEOPCODE_ERASE	0x00000300
#define DC_PN_EEOPCODE_READ	0x00000600
#define DC_PN_EEOPCODE_WRITE	0x00000100

/*
 * The first two general purpose pins control speed selection and
 * 100Mbps loopback on the 82c168 chip. The control bits should always
 * be set (to make the data pins outputs) and the speed selction and
 * loopback bits set accordingly when changing media. Physically, this
 * will set the state of a relay mounted on the card.
 */
#define DC_PN_GPIO_DATA0	0x000000001
#define DC_PN_GPIO_DATA1	0x000000002
#define DC_PN_GPIO_DATA2	0x000000004
#define DC_PN_GPIO_DATA3	0x000000008
#define DC_PN_GPIO_CTL0		0x000000010
#define DC_PN_GPIO_CTL1		0x000000020
#define DC_PN_GPIO_CTL2		0x000000040
#define DC_PN_GPIO_CTL3		0x000000080
#define DC_PN_GPIO_SPEEDSEL	DC_PN_GPIO_DATA0/* 1 == 100Mbps, 0 == 10Mbps */
#define DC_PN_GPIO_100TX_LOOP	DC_PN_GPIO_DATA1/* 1 == normal, 0 == loop */
#define DC_PN_GPIO_BNC_ENB	DC_PN_GPIO_DATA2
#define DC_PN_GPIO_100TX_LNK	DC_PN_GPIO_DATA3
#define DC_PN_GPIO_SETBIT(sc, r)			\
	DC_SETBIT(sc, DC_PN_GPIO, ((r) | (r << 4)))
#define DC_PN_GPIO_CLRBIT(sc, r)			\
	{						\
		DC_SETBIT(sc, DC_PN_GPIO, ((r) << 4));	\
		DC_CLRBIT(sc, DC_PN_GPIO, (r));		\
	}
	
/* shortcut MII access register */
#define DC_PN_MII_DATA		0x0000FFFF
#define DC_PN_MII_RESERVER	0x00020000
#define DC_PN_MII_REGADDR	0x007C0000
#define DC_PN_MII_PHYADDR	0x0F800000
#define DC_PN_MII_OPCODE	0x30000000
#define DC_PN_MII_BUSY		0x80000000

#define DC_PN_MIIOPCODE_READ	0x60020000
#define DC_PN_MIIOPCODE_WRITE	0x50020000

/* Internal NWAY bits */
#define DC_PN_NWAY_RESET	0x00000001	/* reset */
#define DC_PN_NWAY_PDOWN	0x00000002	/* power down */
#define DC_PN_NWAY_BYPASS	0x00000004	/* bypass */
#define DC_PN_NWAY_AUILOWCUR	0x00000008	/* AUI low current */
#define DC_PN_NWAY_TPEXTEND	0x00000010	/* low squelch voltage */
#define DC_PN_NWAY_POLARITY	0x00000020	/* 0 == on, 1 == off */
#define DC_PN_NWAY_TP		0x00000040	/* 1 == tp, 0 == AUI */
#define DC_PN_NWAY_AUIVOLT	0x00000080	/* 1 == full, 0 == half */
#define DC_PN_NWAY_DUPLEX	0x00000100	/* LED, 1 == full, 0 == half */
#define DC_PN_NWAY_LINKTEST	0x00000200	/* 0 == on, 1 == off */
#define DC_PN_NWAY_AUTODETECT	0x00000400	/* 1 == off, 0 == on */
#define DC_PN_NWAY_SPEEDSEL	0x00000800	/* LED, 0 = 10, 1 == 100 */
#define DC_PN_NWAY_NWAY_ENB	0x00001000	/* 0 == off, 1 == on */
#define DC_PN_NWAY_CAP10HDX	0x00002000
#define DC_PN_NWAY_CAP10FDX	0x00004000
#define DC_PN_NWAY_CAP100FDX	0x00008000
#define DC_PN_NWAY_CAP100HDX	0x00010000
#define DC_PN_NWAY_CAP100T4	0x00020000
#define DC_PN_NWAY_ANEGRESTART	0x02000000	/* resets when aneg done */
#define DC_PN_NWAY_REMFAULT	0x04000000
#define DC_PN_NWAY_LPAR10HDX	0x08000000
#define DC_PN_NWAY_LPAR10FDX	0x10000000
#define DC_PN_NWAY_LPAR100FDX	0x20000000
#define DC_PN_NWAY_LPAR100HDX	0x40000000
#define DC_PN_NWAY_LPAR100T4	0x80000000

/* End of PNIC specific registers */

struct dc_softc {
	struct device		sc_dev;
	void			*sc_ih;
	struct arpcom		arpcom;		/* interface info */
	mii_data_t		sc_mii;
	bus_space_handle_t	dc_bhandle;	/* bus space handle */
	bus_space_tag_t		dc_btag;	/* bus space tag */
	void			*dc_intrhand;
	struct resource		*dc_irq;
	struct resource		*dc_res;
	u_int8_t		dc_unit;	/* interface number */
	u_int8_t		dc_type;
	u_int8_t		dc_pmode;
	u_int8_t		dc_link;
	u_int8_t		dc_cachesize;
	int			dc_romwidth;
	int			dc_pnic_rx_bug_save;
	unsigned char		*dc_pnic_rx_buf;
	int			dc_if_flags;
	int			dc_if_media;
	u_int32_t		dc_flags;
	u_int32_t		dc_txthresh;
	struct dc_list_data	*dc_ldata;
	caddr_t			dc_ldata_ptr;
	struct dc_chain_data	dc_cdata;
	u_int32_t		dc_csid;
	u_int			dc_revision;
	struct timeout		dc_tick_tmo;
};

#define DC_TX_POLL		0x00000001
#define DC_TX_COALESCE		0x00000002
#define DC_TX_ADMTEK_WAR	0x00000004
#define DC_TX_USE_TX_INTR	0x00000008
#define DC_RX_FILTER_TULIP	0x00000010
#define DC_TX_INTR_FIRSTFRAG	0x00000020
#define DC_PNIC_RX_BUG_WAR	0x00000040
#define DC_TX_FIXED_RING	0x00000080
#define DC_TX_STORENFWD		0x00000100
#define DC_REDUCED_MII_POLL	0x00000200
#define DC_TX_INTR_ALWAYS	0x00000400
#define DC_21143_NWAY		0x00000800
#define DC_128BIT_HASH		0x00001000
#define DC_64BIT_HASH		0x00002000

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->dc_btag, sc->dc_bhandle, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->dc_btag, sc->dc_bhandle, reg)

#define DC_TIMEOUT		1000
#define ETHER_ALIGN		2

/*
 * General constants that are fun to know.
 */

/* Macronix PCI revision codes. */
#define DC_REVISION_98713	0x00
#define DC_REVISION_98713A	0x10
#define DC_REVISION_98715	0x20
#define DC_REVISION_98715AEC_C	0x25
#define DC_REVISION_98725	0x30

/*
 * 82c168/82c169 PNIC device IDs. Both chips have the same device
 * ID but different revisions. Revision 0x10 is the 82c168, and
 * 0x20 is the 82c169.
 */
#define DC_REVISION_82C168	0x10
#define DC_REVISION_82C169	0x20

/*
 * The ASIX AX88140 and ASIX AX88141 have the same vendor and
 * device IDs but different revision values.
 */
#define DC_REVISION_88140	0x00
#define DC_REVISION_88141	0x10

/*
 * The DMA9102A has the same PCI device ID as the DM9102,
 * but a higher revision code.
 */
#define DC_REVISION_DM9102	0x10
#define DC_REVISION_DM9102A	0x30

/*
 * PCI low memory base and low I/O base register, and
 * other PCI registers.
 */

#define DC_PCI_CFID		0x00	/* Id */
#define DC_PCI_CFCS		0x04	/* Command and status */
#define DC_PCI_CFRV		0x08	/* Revision */
#define DC_PCI_CFLT		0x0C	/* Latency timer */
#define DC_PCI_CFBIO		0x10	/* Base I/O address */
#define DC_PCI_CFBMA		0x14	/* Base memory address */
#define DC_PCI_CCIS		0x28	/* Card info struct */
#define DC_PCI_CSID		0x2C	/* Subsystem ID */
#define DC_PCI_CBER		0x30	/* Expansion ROM base address */
#define DC_PCI_CCAP		0x34	/* Caps pointer - PD/TD chip only */
#define DC_PCI_CFIT		0x3C	/* Interrupt */
#define DC_PCI_CFDD		0x40	/* Device and driver area */
#define DC_PCI_CWUA0		0x44	/* Wake-Up LAN addr 0 */
#define DC_PCI_CWUA1		0x48	/* Wake-Up LAN addr 1 */
#define DC_PCI_SOP0		0x4C	/* SecureON passwd 0 */
#define DC_PCI_SOP1		0x50	/* SecureON passwd 1 */
#define DC_PCI_CWUC		0x54	/* Configuration Wake-Up cmd */
#define DC_PCI_CCID		0xDC	/* Capability ID - PD/TD only */
#define DC_PCI_CPMC		0xE0	/* Pwrmgmt ctl & sts - PD/TD only */

/* PCI ID register */
#define DC_CFID_VENDOR		0x0000FFFF
#define DC_CFID_DEVICE		0xFFFF0000

/* PCI command/status register */
#define DC_CFCS_IOSPACE		0x00000001 /* I/O space enable */
#define DC_CFCS_MEMSPACE	0x00000002 /* memory space enable */
#define DC_CFCS_BUSMASTER	0x00000004 /* bus master enable */
#define DC_CFCS_MWI_ENB		0x00000008 /* mem write and inval enable */
#define DC_CFCS_PARITYERR_ENB	0x00000020 /* parity error enable */
#define DC_CFCS_SYSERR_ENB	0x00000080 /* system error enable */
#define DC_CFCS_NEWCAPS		0x00100000 /* new capabilities */
#define DC_CFCS_FAST_B2B	0x00800000 /* fast back-to-back capable */
#define DC_CFCS_DATAPARITY	0x01000000 /* Parity error report */
#define DC_CFCS_DEVSELTIM	0x06000000 /* devsel timing */
#define DC_CFCS_TGTABRT		0x10000000 /* received target abort */
#define DC_CFCS_MASTERABRT	0x20000000 /* received master abort */
#define DC_CFCS_SYSERR		0x40000000 /* asserted system error */
#define DC_CFCS_PARITYERR	0x80000000 /* asserted parity error */

/* PCI revision register */
#define DC_CFRV_STEPPING	0x0000000F
#define DC_CFRV_REVISION	0x000000F0
#define DC_CFRV_SUBCLASS	0x00FF0000
#define DC_CFRV_BASECLASS	0xFF000000

#define DC_21143_PB_REV		0x00000030
#define DC_21143_TB_REV		0x00000030
#define DC_21143_PC_REV		0x00000030
#define DC_21143_TC_REV		0x00000030
#define DC_21143_PD_REV		0x00000041
#define DC_21143_TD_REV		0x00000041

/* PCI latency timer register */
#define DC_CFLT_CACHELINESIZE	0x000000FF
#define DC_CFLT_LATENCYTIMER	0x0000FF00

/* PCI subsystem ID register */
#define DC_CSID_VENDOR		0x0000FFFF
#define DC_CSID_DEVICE		0xFFFF0000

/* PCI cababilities pointer */
#define DC_CCAP_OFFSET		0x000000FF

/* PCI interrupt config register */
#define DC_CFIT_INTLINE		0x000000FF
#define DC_CFIT_INTPIN		0x0000FF00
#define DC_CFIT_MIN_GNT		0x00FF0000
#define DC_CFIT_MAX_LAT		0xFF000000

/* PCI capability register */
#define DC_CCID_CAPID		0x000000FF
#define DC_CCID_NEXTPTR		0x0000FF00
#define DC_CCID_PM_VERS		0x00070000
#define DC_CCID_PME_CLK		0x00080000
#define DC_CCID_DVSPEC_INT	0x00200000
#define DC_CCID_STATE_D1	0x02000000
#define DC_CCID_STATE_D2	0x04000000
#define DC_CCID_PME_D0		0x08000000
#define DC_CCID_PME_D1		0x10000000
#define DC_CCID_PME_D2		0x20000000
#define DC_CCID_PME_D3HOT	0x40000000
#define DC_CCID_PME_D3COLD	0x80000000

/* PCI power management control/status register */
#define DC_CPMC_STATE		0x00000003
#define DC_CPMC_PME_ENB		0x00000100
#define DC_CPMC_PME_STS		0x00008000

#define DC_PSTATE_D0		0x0
#define DC_PSTATE_D1		0x1
#define DC_PSTATE_D2		0x2
#define DC_PSTATE_D3		0x3

/* Device specific region */
/* Configuration and driver area */
#define DC_CFDD_DRVUSE		0x0000FFFF
#define DC_CFDD_SNOOZE_MODE	0x40000000
#define DC_CFDD_SLEEP_MODE	0x80000000

/* Configuration wake-up command register */
#define DC_CWUC_MUST_BE_ZERO	0x00000001
#define DC_CWUC_SECUREON_ENB	0x00000002
#define DC_CWUC_FORCE_WUL	0x00000004
#define DC_CWUC_BNC_ABILITY	0x00000008
#define DC_CWUC_AUI_ABILITY	0x00000010
#define DC_CWUC_TP10_ABILITY	0x00000020
#define DC_CWUC_MII_ABILITY	0x00000040
#define DC_CWUC_SYM_ABILITY	0x00000080
#define DC_CWUC_LOCK		0x00000100

#ifdef __alpha__
#undef vtophys
#define vtophys(va)		alpha_XXX_dmamap((vm_offset_t)va)
#endif

#ifndef ETHER_CRC_LEN
#define ETHER_CRC_LEN	4
#endif

extern void dc_attach_common __P((struct dc_softc *));
extern int dc_intr __P((void *));
extern void dc_reset __P((struct dc_softc *));
