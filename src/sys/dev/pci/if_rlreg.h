/*	$OpenBSD: if_rlreg.h,v 1.4 1998/11/19 07:01:56 jason Exp $	*/

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
 *	$FreeBSD: if_rlreg.h,v 1.2 1998/11/18 21:03:58 wpaul Exp $
 */

/*
 * RealTek 8129/8139 register offsets
 */
#define	RL_IDR0		0x0000		/* ID register 0 (station addr) */
#define RL_IDR1		0x0001		/* Must use 32-bit accesses (?) */
#define RL_IDR2		0x0002
#define RL_IDR3		0x0003
#define RL_IDR4		0x0004
#define RL_IDR5		0x0005
					/* 0006-0007 reserved */
#define RL_MAR0		0x0008		/* Multicast hash table */
#define RL_MAR1		0x0009
#define RL_MAR2		0x000A
#define RL_MAR3		0x000B
#define RL_MAR4		0x000C
#define RL_MAR5		0x000D
#define RL_MAR6		0x000E
#define RL_MAR7		0x000F

#define RL_TXSTAT0	0x0010		/* status of TX descriptor 0 */
#define RL_TXSTAT1	0x0014		/* status of TX descriptor 1 */
#define RL_TXSTAT2	0x0018		/* status of TX descriptor 2 */
#define RL_TXSTAT3	0x001C		/* status of TX descriptor 3 */

#define RL_TXADDR0	0x0020		/* address of TX descriptor 0 */
#define RL_TXADDR1	0x0024		/* address of TX descriptor 1 */
#define RL_TXADDR2	0x0028		/* address of TX descriptor 2 */
#define RL_TXADDR3	0x002C		/* address of TX descriptor 3 */

#define RL_RXADDR		0x0030	/* RX ring start address */
#define RL_RX_EARLY_BYTES	0x0034	/* RX early byte count */
#define RL_RX_EARLY_STAT	0x0036	/* RX early status */
#define RL_COMMAND	0x0037		/* command register */
#define RL_CURRXADDR	0x0038		/* current address of packet read */
#define RL_CURRXBUF	0x003A		/* current RX buffer address */
#define RL_IMR		0x003C		/* interrupt mask register */
#define RL_ISR		0x003E		/* interrupt status register */
#define RL_TXCFG	0x0040		/* transmit config */
#define RL_RXCFG	0x0044		/* receive config */
#define RL_TIMERCNT	0x0048		/* timer count register */
#define RL_MISSEDPKT	0x004C		/* missed packet counter */
#define RL_EECMD	0x0050		/* EEPROM command register */
#define RL_CFG0		0x0051		/* config register #0 */
#define RL_CFG1		0x0052		/* config register #1 */
					/* 0053-0057 reserved */
#define RL_MEDIASTAT	0x0058		/* media status register (8139) */
					/* 0059-005A reserved */
#define RL_MII		0x005A		/* 8129 chip only */
#define RL_HALTCLK	0x005B
#define RL_MULTIINTR	0x005C		/* multiple interrupt */
#define RL_PCIREV	0x005E		/* PCI revision value */
					/* 005F reserved */
#define RL_TXSTAT_ALL	0x0060		/* TX status of all descriptors */

/* Direct PHY access registers only available on 8139 */
#define RL_BMCR		0x0062		/* PHY basic mode control */
#define RL_BMSR		0x0064		/* PHY basic mode status */
#define RL_ANAR		0x0066		/* PHY autoneg advert */
#define RL_LPAR		0x0068		/* PHY link partner ability */
#define RL_ANER		0x006A		/* PHY autoneg expansion */

#define RL_DISCCNT	0x006C		/* disconnect counter */
#define RL_FALSECAR	0x006E		/* false carrier counter */
#define RL_NWAYTST	0x0070		/* NWAY test register */
#define RL_RX_ER	0x0072		/* RX_ER counter */
#define RL_CSCFG	0x0074		/* CS configuration register */


/*
 * TX config register bits
 */
#define RL_TXCFG_CLRABRT	0x00000001	/* retransmit aborted pkt */
#define RL_TXCFG_MXDMA0		0x00000100	/* max DMA burst size */
#define RL_TXCFG_MXDMA1		0x00000200
#define RL_TXCFG_MXDMA2		0x00000400
#define RL_TXCFG_CRCAPPEND	0x00010000	/* CRC append (0 = yes) */
#define RL_TXCFG_LOOPBKTST0	0x00020000	/* loopback test */
#define RL_TXCFG_LOOPBKTST1	0x00040000	/* loopback test */
#define RL_TXCFG_IFG0		0x01000000	/* interframe gap */
#define RL_TXCFG_IFG1		0x02000000	/* interframe gap */

/*
 * Transmit descriptor status register bits.
 */
#define RL_TXSTAT_LENMASK	0x00001FFF
#define RL_TXSTAT_OWN		0x00002000
#define RL_TXSTAT_TX_UNDERRUN	0x00004000
#define RL_TXSTAT_TX_OK		0x00008000
#define RL_TXSTAT_EARLY_THRESH	0x003F0000
#define RL_TXSTAT_COLLCNT	0x0F000000
#define RL_TXSTAT_CARR_HBEAT	0x10000000
#define RL_TXSTAT_OUTOFWIN	0x20000000
#define RL_TXSTAT_TXABRT	0x40000000
#define RL_TXSTAT_CARRLOSS	0x80000000

/*
 * Interrupt status register bits.
 */
#define RL_ISR_RX_OK		0x0001
#define RL_ISR_RX_ERR		0x0002
#define RL_ISR_TX_OK		0x0004
#define RL_ISR_TX_ERR		0x0008
#define RL_ISR_RX_OVERRUN	0x0010
#define RL_ISR_PKT_UNDERRUN	0x0020
#define RL_ISR_FIFO_OFLOW	0x0040	/* 8139 only */
#define RL_ISR_PCS_TIMEOUT	0x4000	/* 8129 only */
#define RL_ISR_SYSTEM_ERR	0x8000

#define RL_INTRS	\
	(RL_ISR_TX_OK|RL_ISR_RX_OK|RL_ISR_RX_ERR|RL_ISR_TX_ERR|		\
	RL_ISR_RX_OVERRUN|RL_ISR_PKT_UNDERRUN|RL_ISR_FIFO_OFLOW|	\
	RL_ISR_PCS_TIMEOUT|RL_ISR_SYSTEM_ERR)

/*
 * Media status register. (8139 only)
 */
#define RL_MEDIASTAT_RXPAUSE	0x01
#define RL_MEDIASTAT_TXPAUSE	0x02
#define RL_MEDIASTAT_LINK	0x04
#define RL_MEDIASTAT_SPEED10	0x08
#define RL_MEDIASTAT_RXFLOWCTL	0x40	/* duplex mode */
#define RL_MEDIASTAT_TXFLOWCTL	0x80	/* duplex mode */

/*
 * Receive config register.
 */
#define RL_RXCFG_RX_ALLPHYS	0x00000001	/* accept all nodes */
#define RL_RXCFG_RX_INDIV	0x00000002	/* match filter */
#define RL_RXCFG_RX_MULTI	0x00000004	/* accept all multicast */
#define RL_RXCFG_RX_BROAD	0x00000008	/* accept all broadcast */
#define RL_RXCFG_RX_RUNT	0x00000010
#define RL_RXCFG_RX_ERRPKT	0x00000020
#define RL_RXCFG_WRAP		0x00000080
#define RL_RXCFG_MAXDMA		(0x00000100|0x00000200|0x00000400)
#define RL_RXCFG_BUFSZ		(0x00000800|0x00001000)
#define RL_RXCFG_FIFOTHRESH	(0x00002000|0x00004000|0x00008000)
#define RL_RXCFG_EARLYTHRESH	(0x01000000|0x02000000|0x04000000)

#define RL_RXBUF_8		0x00000000
#define RL_RXBUF_16		0x00000800
#define RL_RXBUF_32		0x00001000
#define RL_RXBUF_64		(0x00001000|0x00000800)

/*
 * Bits in RX status header (included with RX'ed packet
 * in ring buffer).
 */
#define RL_RXSTAT_RXOK		0x00000001
#define RL_RXSTAT_ALIGNERR	0x00000002
#define RL_RXSTAT_CRCERR	0x00000004
#define RL_RXSTAT_GIANT		0x00000008
#define RL_RXSTAT_RUNT		0x00000010
#define RL_RXSTAT_BADSYM	0x00000020
#define RL_RXSTAT_BROAD		0x00002000
#define RL_RXSTAT_INDIV		0x00004000
#define RL_RXSTAT_MULTI		0x00008000
#define RL_RXSTAT_LENMASK	0xFFFF0000

#define RL_RXSTAT_UNFINISHED	0xFFF0		/* DMA still in progress */
/*
 * Command register.
 */
#define RL_CMD_EMPTY_RXBUF	0x0001
#define RL_CMD_TX_ENB		0x0004
#define RL_CMD_RX_ENB		0x0008
#define RL_CMD_RESET		0x0010

/*
 * EEPROM control register
 */
#define RL_EE_DATAOUT		0x01	/* Data out */
#define RL_EE_DATAIN		0x02	/* Data in */
#define RL_EE_CLK		0x04	/* clock */
#define RL_EE_SEL		0x08	/* chip select */
#define RL_EE_MODE		(0x40|0x80)

#define RL_EEMODE_OFF		0x00
#define RL_EEMODE_AUTOLOAD	0x40
#define RL_EEMODE_PROGRAM	0x80
#define RL_EEMODE_WRITECFG	(0x80|0x40)

/* 9346 EEPROM commands */
#define RL_EECMD_WRITE		0x140
#define RL_EECMD_READ		0x180
#define RL_EECMD_ERASE		0x1c0

#define RL_EE_ID		0x00
#define RL_EE_PCI_VID		0x01
#define RL_EE_PCI_DID		0x02
/* Location of station address inside EEPROM */
#define RL_EE_EADDR		0x07

/*
 * MII register (8129 only)
 */
#define RL_MII_CLK		0x01
#define RL_MII_DATAIN		0x02
#define RL_MII_DATAOUT		0x04
#define RL_MII_DIR		0x80	/* 0 == input, 1 == output */

/*
 * Config 0 register
 */
#define RL_CFG0_ROM0		0x01
#define RL_CFG0_ROM1		0x02
#define RL_CFG0_ROM2		0x04
#define RL_CFG0_PL0		0x08
#define RL_CFG0_PL1		0x10
#define RL_CFG0_10MBPS		0x20	/* 10 Mbps internal mode */
#define RL_CFG0_PCS		0x40
#define RL_CFG0_SCR		0x80

/*
 * Config 1 register
 */
#define RL_CFG1_PWRDWN		0x01
#define RL_CFG1_SLEEP		0x02
#define RL_CFG1_IOMAP		0x04
#define RL_CFG1_MEMMAP		0x08
#define RL_CFG1_RSVD		0x10
#define RL_CFG1_DRVLOAD		0x20
#define RL_CFG1_LED0		0x40
#define RL_CFG1_FULLDUPLEX	0x40	/* 8129 only */
#define RL_CFG1_LED1		0x80

/*
 * The RealTek doesn't use a fragment-based descriptor mechanism.
 * Instead, there are only four register sets, each or which represents
 * one 'descriptor.' Basically, each TX descriptor is just a contiguous
 * packet buffer (32-bit aligned!) and we place the buffer addresses in
 * the registers so the chip knows where they are.
 *
 * We can sort of kludge together the same kind of buffer management
 * used in previous drivers, but we have to do buffer copies almost all
 * the time, so it doesn't really buy us much.
 *
 * For reception, there's just one large buffer where the chip stores
 * all received packets.
 */

#define RL_RX_BUF_SZ		RL_RXBUF_64
#define RL_RXBUFLEN		(1 << ((RL_RX_BUF_SZ >> 11) + 13))
#define RL_TX_LIST_CNT		4
#define RL_MIN_FRAMELEN		60
#define RL_TX_EARLYTHRESH	0x80000		/* 256 << 11 */
#define RL_RX_FIFOTHRESH	0x8000		/* 4 << 13 */
#define RL_RX_MAXDMA		0x00000400

#define RL_RXCFG_CONFIG (RL_RX_FIFOTHRESH|RL_RX_BUF_SZ)

struct rl_chain {
	char			rl_desc;	/* descriptor register idx */
	struct mbuf		*rl_mbuf;
	struct rl_chain		*rl_next;
};

struct rl_chain_data {
	u_int16_t		cur_rx;
	caddr_t			rl_rx_buf;
	struct rl_chain		rl_tx_chain[RL_TX_LIST_CNT];

	int			rl_tx_cnt;
	struct rl_chain		*rl_tx_cur;
	struct rl_chain		*rl_tx_free;
};

struct rl_type {
	u_int16_t		rl_vid;
	u_int16_t		rl_did;
	char			*rl_name;
};

struct rl_mii_frame {
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
#define RL_MII_STARTDELIM	0x01
#define RL_MII_READOP		0x02
#define RL_MII_WRITEOP		0x01
#define RL_MII_TURNAROUND	0x02

#define RL_FLAG_FORCEDELAY	1
#define RL_FLAG_SCHEDDELAY	2
#define RL_FLAG_DELAYTIMEO	3	

#define RL_8129			1
#define RL_8139			2

struct rl_softc {
	struct device		sc_dev;		/* us, as a device */
	void *			sc_ih;		/* interrupt vectoring */
	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_dma_mem;
	size_t			sc_dma_mapsize;
	struct arpcom		arpcom;		/* interface info */
	struct mii_data		sc_mii;		/* MII information */
	u_int8_t		rl_type;
	struct rl_chain_data	rl_cdata;
};

/*
 * register space access macros
 */
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

#define RL_TIMEOUT		1000

/*
 * General constants that are fun to know.
 *
 * RealTek PCI vendor ID
 */
#define	RT_VENDORID				0x10EC

/*
 * Accton PCI vendor ID
 */
#define ACCTON_VENDORID				0x1113

/*
 * RealTek chip device IDs.
 */
#define	RT_DEVICEID_8129			0x8129
#define	RT_DEVICEID_8139			0x8139

/*
 * Accton MPX 5030/5038 device ID.
 */
#define ACCTON_DEVICEID_5030			0x1211

/*
 * Texas Instruments PHY identifiers
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

#define RL_PCI_VENDOR_ID	0x00
#define RL_PCI_DEVICE_ID	0x02
#define RL_PCI_COMMAND		0x04
#define RL_PCI_STATUS		0x06
#define RL_PCI_CLASSCODE	0x09
#define RL_PCI_LATENCY_TIMER	0x0D
#define RL_PCI_HEADER_TYPE	0x0E
#define RL_PCI_LOIO		0x10
#define RL_PCI_LOMEM		0x14
#define RL_PCI_BIOSROM		0x30
#define RL_PCI_INTLINE		0x3C
#define RL_PCI_INTPIN		0x3D
#define RL_PCI_MINGNT		0x3E
#define RL_PCI_MINLAT		0x0F
#define RL_PCI_RESETOPT		0x48
#define RL_PCI_EEPROM_DATA	0x4C

#define RL_PCI_CAPID		0xDC /* 8 bits */
#define RL_PCI_NEXTPTR		0xDD /* 8 bits */
#define RL_PCI_PWRMGMTCAP	0xDE /* 16 bits */
#define RL_PCI_PWRMGMTCTRL	0xE0 /* 16 bits */

#define RL_PSTATE_MASK		0x0003
#define RL_PSTATE_D0		0x0000
#define RL_PSTATE_D1		0x0002
#define RL_PSTATE_D2		0x0002
#define RL_PSTATE_D3		0x0003
#define RL_PME_EN		0x0010
#define RL_PME_STATUS		0x8000

#define PHY_UNKNOWN		6

#define RL_PHYADDR_MIN		0x00
#define RL_PHYADDR_MAX		0x1F

#define PHY_BMCR		0x00
#define PHY_BMSR		0x01
#define PHY_VENID		0x02
#define PHY_DEVID		0x03
#define PHY_ANAR		0x04
#define PHY_LPAR		0x05
#define PHY_ANEXP		0x06

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
