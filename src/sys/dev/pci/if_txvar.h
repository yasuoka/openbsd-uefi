/*-
 * Copyright (c) 1997 Semen Ustimenko
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      $Id: if_txvar.h,v 1.1 1998/09/21 05:24:55 jason Exp $
 *
 */

/*
 * Configuration
 */
#ifndef ETHER_MAX_LEN
#define ETHER_MAX_LEN		1518
#endif
#ifndef ETHER_MIN_LEN
#define ETHER_MIN_LEN		64
#endif
#ifndef ETHER_CRC_LEN
#define ETHER_CRC_LEN		4
#endif
#define TX_RING_SIZE		16
#define RX_RING_SIZE		16
#define EPIC_FULL_DUPLEX	1
#define EPIC_HALF_DUPLEX	0
#define ETHER_MAX_FRAME_LEN	(ETHER_MAX_LEN + ETHER_CRC_LEN)
#define	EPIC_LINK_DOWN		0x00000001

/* PCI identification */
#define SMC_VENDORID		0x10B8
#define CHIPID_83C170		0x0005
#define	PCI_VENDORID(x)		((x) & 0xFFFF)
#define	PCI_CHIPID(x)		(((x) >> 16) & 0xFFFF)

/* PCI configuration */
#define	PCI_CFID	0x00	/* Configuration ID */
#define	PCI_CFCS	0x04	/* Configurtion Command/Status */
#define	PCI_CFRV	0x08	/* Configuration Revision */
#define	PCI_CFLT	0x0c	/* Configuration Latency Timer */
#define	PCI_CBIO	0x10	/* Configuration Base IO Address */
#define	PCI_CBMA	0x14	/* Configuration Base Memory Address */
#define	PCI_CFIT	0x3c	/* Configuration Interrupt */
#define	PCI_CFDA	0x40	/* Configuration Driver Area */

#define	PCI_CONF_WRITE(r, v)	pci_conf_write(config_id, (r), (v))
#define	PCI_CONF_READ(r)	pci_conf_read(config_id, (r))

/* EPIC's registers */
#define	COMMAND		0x0000
#define	INTSTAT		0x0004		/* Interrupt status. See below */
#define	INTMASK		0x0008		/* Interrupt mask. See below */
#define	GENCTL		0x000C
#define	NVCTL		0x0010
#define	EECTL		0x0014		/* EEPROM control **/
#define	TEST1		0x001C		/* XXXXX */
#define	CRCCNT		0x0020		/* CRC error counter */
#define	ALICNT		0x0024		/* FrameTooLang error counter */
#define	MPCNT		0x0028		/* MissedFrames error counters */
#define	MIICTL		0x0030
#define	MIIDATA		0x0034
#define	MIICFG		0x0038
#define IPG		0x003C
#define	LAN0		0x0040		/* MAC address */
#define	LAN1		0x0044		/* MAC address */
#define	LAN2		0x0048		/* MAC address */
#define	ID_CHK		0x004C
#define	MC0		0x0050		/* Multicast filter table */
#define	MC1		0x0054		/* Multicast filter table */
#define	MC2		0x0058		/* Multicast filter table */
#define	MC3		0x005C		/* Multicast filter table */
#define	RXCON		0x0060		/* Rx control register */
#define	TXCON		0x0070		/* Tx control register */
#define	TXSTAT		0x0074
#define	PRCDAR		0x0084		/* RxRing bus address */
#define	PRSTAT		0x00A4
#define	PRCPTHR		0x00B0
#define	PTCDAR		0x00C4		/* TxRing bus address */
#define	ETXTHR		0x00DC

#define	COMMAND_STOP_RX		0x01
#define	COMMAND_START_RX	0x02
#define	COMMAND_TXQUEUED	0x04
#define	COMMAND_RXQUEUED	0x08
#define	COMMAND_NEXTFRAME	0x10
#define	COMMAND_STOP_TDMA	0x20
#define	COMMAND_STOP_RDMA	0x40
#define	COMMAND_TXUGO		0x80

/* Tx threshold */
#define TX_FIFO_THRESH	0x80		/* 0x40 or 0x10 */

/* Interrupt register bits */
#define INTSTAT_RCC	0x00000001
#define INTSTAT_HCC	0x00000002
#define INTSTAT_RQE	0x00000004
#define INTSTAT_OVW	0x00000008	
#define INTSTAT_RXE	0x00000010	
#define INTSTAT_TXC	0x00000020
#define INTSTAT_TCC	0x00000040	
#define INTSTAT_TQE	0x00000080	
#define INTSTAT_TXU	0x00000100
#define INTSTAT_CNT	0x00000200
#define INTSTAT_PREI	0x00000400
#define INTSTAT_RCT	0x00000800	
#define INTSTAT_FATAL	0x00001000	/* One of DPE,APE,PMA,PTA happend */	
#define INTSTAT_UNUSED1	0x00002000
#define INTSTAT_UNUSED2	0x00004000	
#define INTSTAT_GP2	0x00008000	/* PHY Event */	
#define INTSTAT_INT_ACTV 0x00010000
#define INTSTAT_RXIDLE	0x00020000
#define INTSTAT_TXIDLE	0x00040000
#define INTSTAT_RCIP	0x00080000	
#define INTSTAT_TCIP	0x00100000	
#define INTSTAT_RBE	0x00200000
#define INTSTAT_RCTS	0x00400000	
#define	INTSTAT_RSV	0x00800000
#define	INTSTAT_DPE	0x01000000	/* PCI Fatal error */
#define	INTSTAT_APE	0x02000000	/* PCI Fatal error */
#define	INTSTAT_PMA	0x04000000	/* PCI Fatal error */
#define	INTSTAT_PTA	0x08000000	/* PCI Fatal error */

#define	GENCTL_SOFT_RESET		0x00000001
#define	GENCTL_ENABLE_INTERRUPT		0x00000002
#define	GENCTL_SOFTWARE_INTERRUPT	0x00000004
#define	GENCTL_POWER_DOWN		0x00000008
#define	GENCTL_ONECOPY			0x00000010
#define	GENCTL_BIG_ENDIAN		0x00000020
#define	GENCTL_RECEIVE_DMA_PRIORITY	0x00000040
#define	GENCTL_TRANSMIT_DMA_PRIORITY	0x00000080
#define	GENCTL_RECEIVE_FIFO_THRESHOLD128	0x00000300
#define	GENCTL_RECEIVE_FIFO_THRESHOLD96	0x00000200
#define	GENCTL_RECEIVE_FIFO_THRESHOLD64	0x00000100
#define	GENCTL_RECEIVE_FIFO_THRESHOLD32	0x00000000
#define	GENCTL_MEMORY_READ_LINE		0x00000400
#define	GENCTL_MEMORY_READ_MULTIPLE	0x00000800
#define	GENCTL_SOFTWARE1		0x00001000
#define	GENCTL_SOFTWARE2		0x00002000
#define	GENCTL_RESET_PHY		0x00004000

#define	NVCTL_ENABLE_MEMORY_MAP		0x00000001
#define	NVCTL_CLOCK_RUN_SUPPORTED	0x00000002
#define	NVCTL_GP1_OUTPUT_ENABLE		0x00000004
#define	NVCTL_GP2_OUTPUT_ENABLE		0x00000008
#define	NVCTL_GP1			0x00000010
#define	NVCTL_GP2			0x00000020
#define	NVCTL_CARDBUS_MODE		0x00000040
#define	NVCTL_IPG_DELAY_MASK(x)		((x&0xF)<<7)

#define	RXCON_SAVE_ERRORED_PACKETS	0x00000001
#define	RXCON_RECEIVE_RUNT_FRAMES	0x00000002
#define	RXCON_RECEIVE_BROADCAST_FRAMES	0x00000004
#define	RXCON_RECEIVE_MULTICAST_FRAMES	0x00000008
#define	RXCON_RECEIVE_INVERSE_INDIVIDUAL_ADDRESS_FRAMES	0x00000010
#define	RXCON_PROMISCUOUS_MODE		0x00000020
#define	RXCON_MONITOR_MODE		0x00000040
#define	RXCON_EARLY_RECEIVE_ENABLE	0x00000080
#define	RXCON_EXTERNAL_BUFFER_DISABLE	0x00000000
#define	RXCON_EXTERNAL_BUFFER_16K	0x00000100
#define	RXCON_EXTERNAL_BUFFER_32K	0x00000200
#define	RXCON_EXTERNAL_BUFFER_128K	0x00000300

#define TXCON_EARLY_TRANSMIT_ENABLE	0x00000001
#define TXCON_LOOPBACK_DISABLE		0x00000000
#define TXCON_LOOPBACK_MODE_INT		0x00000002
#define TXCON_LOOPBACK_MODE_PHY		0x00000004
#define TXCON_LOOPBACK_MODE		0x00000006
#define TXCON_FULL_DUPLEX		0x00000006
#define TXCON_SLOT_TIME			0x00000078

#define TXCON_DEFAULT		(TXCON_SLOT_TIME | TXCON_EARLY_TRANSMIT_ENABLE)
#define TRANSMIT_THRESHOLD	0x80

#if defined(EARLY_RX)
 #define RXCON_DEFAULT		(RXCON_EARLY_RECEIVE_ENABLE | RXCON_SAVE_ERRORED_PACKETS)
#else
 #define RXCON_DEFAULT		(0)
#endif
/*
 * National Semiconductor's DP83840A Registers and bits
 */
#define	DP83840_OUI	0x080017
#define DP83840_BMCR	0x00	/* Control register */
#define DP83840_BMSR	0x01	/* Status rgister */
#define	DP83840_ANAR	0x04	/* Autonegotiation advertising register */
#define	DP83840_LPAR	0x05	/* Link Partner Ability register */
#define DP83840_ANER	0x06	/* Auto-Negotiation Expansion Register */
#define DP83840_PAR	0x19	/* PHY Address Register */
#define	DP83840_PHYIDR1	0x02
#define	DP83840_PHYIDR2	0x03

#define BMCR_RESET		0x8000
#define BMCR_100MBPS		0x2000	/* 10/100 Mbps */
#define BMCR_AUTONEGOTIATION	0x1000	/* ON/OFF */
#define BMCR_RESTART_AUTONEG	0x0200
#define	BMCR_FULL_DUPLEX	0x0100

#define	BMSR_100BASE_T4		0x8000
#define	BMSR_100BASE_TX_FD	0x4000
#define	BMSR_100BASE_TX		0x2000
#define	BMSR_10BASE_T_FD	0x1000
#define	BMSR_10BASE_T		0x0800
#define	BMSR_AUTONEG_COMPLETE	0x0020
#define BMSR_AUTONEG_ABLE	0x0008
#define	BMSR_LINK_STATUS	0x0004

#define PAR_FULL_DUPLEX		0x0400

#define ANER_MULTIPLE_LINK_FAULT	0x10

/* ANAR and LPAR have the same bits, define them only once */
#define	ANAR_10			0x0020
#define	ANAR_10_FD		0x0040
#define	ANAR_100_TX		0x0080
#define	ANAR_100_TX_FD		0x0100
#define	ANAR_100_T4		0x0200

/*
 * Quality Semiconductor's QS6612 registers and bits
 */
#define	QS6612_OUI		0x006051
#define	QS6612_MCTL		17
#define	QS6612_INTSTAT		29
#define	QS6612_INTMASK		30

#define	MCTL_T4_PRESENT		0x1000	/* External T4 Enabled, ignored */
					/* if AutoNeg is enabled */
#define	MCTL_BTEXT		0x0800	/* Reduces 10baset squelch level */
					/* for extended cable length */

#define	INTSTAT_AN_COMPLETE	0x40	/* Autonegotiation complete */
#define	INTSTAT_RF_DETECTED	0x20	/* Remote Fault detected */
#define	INTSTAT_LINK_STATUS	0x10	/* Link status changed */
#define	INTSTAT_AN_LP_ACK	0x08	/* Autoneg. LP Acknoledge */
#define	INTSTAT_PD_FAULT	0x04	/* Parallel Detection Fault */
#define	INTSTAT_AN_PAGE		0x04	/* Autoneg. Page Received */
#define	INTSTAT_RE_CNT_FULL	0x01	/* Receive Error Counter Full */

#define	INTMASK_THUNDERLAN	0x8000	/* Enable interrupts */

/*
 * Structures definition and Functions prototypes
 */

/* EPIC's hardware descriptors, must be aligned on dword in memory */
/* NB: to make driver happy, this two structures MUST have thier sizes */
/* be divisor of PAGE_SIZE */
struct epic_tx_desc {
	volatile u_int16_t	status;
	volatile u_int16_t	txlength;
	volatile u_int32_t	bufaddr;
	volatile u_int16_t	buflength;
	volatile u_int16_t	control;
	volatile u_int32_t	next;
};
struct epic_rx_desc {
	volatile u_int16_t	status;
	volatile u_int16_t	rxlength;
	volatile u_int32_t	bufaddr;
	volatile u_int32_t	buflength;
	volatile u_int32_t	next;
};

/* This structure defines EPIC's fragment list, maximum number of frags */
/* is 63. Let use maximum, becouse size of struct MUST be divisor of */
/* PAGE_SIZE, and sometimes come mbufs with more then 30 frags */
struct epic_frag_list {
	volatile u_int32_t		numfrags;
	struct {
		volatile u_int32_t	fragaddr;
		volatile u_int32_t	fraglen;
	} frag[63]; 
	volatile u_int32_t		pad;		/* align on 256 bytes */
};

/* This is driver's structure to define EPIC descriptors */
struct epic_rx_buffer {
	struct mbuf *		mbuf;		/* mbuf receiving packet */
};

struct epic_tx_buffer {
	struct mbuf *		mbuf;		/* mbuf contained packet */
};

/*
 * NB: ALIGN OF ABOVE STRUCTURES
 * epic_rx_desc, epic_tx_desc, epic_frag_list - must be aligned on dword
 */

/* Driver status structure */
typedef struct {
#if defined(__OpenBSD__)
	struct device		sc_dev;
	void			*sc_ih;
	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
#else /* __FreeBSD__ */
#if defined(EPIC_USEIOSPACE)
	u_int32_t		iobase;
#else
	caddr_t			csr;
#endif
#endif
	struct ifmedia 		ifmedia;
	struct arpcom		arpcom;
	u_int32_t		unit;
	struct epic_rx_buffer	rx_buffer[RX_RING_SIZE];
	struct epic_tx_buffer	tx_buffer[TX_RING_SIZE];

	/* Each element of array MUST be aligned on dword  */
	/* and bounded on PAGE_SIZE 			   */
	struct epic_rx_desc	*rx_desc;
	struct epic_tx_desc	*tx_desc;
	struct epic_frag_list	*tx_flist;
	u_int32_t		flags;
	u_int32_t		tx_threshold;
	u_int32_t		txcon;
	u_int32_t		phyid;
	u_int32_t		cur_tx;
	u_int32_t		cur_rx;
	u_int32_t		dirty_tx;
	u_int32_t		pending_txs;
	void 			*pool;
} epic_softc_t;

#if defined(__FreeBSD__)
#define EPIC_FORMAT	"tx%d"
#define EPIC_ARGS(sc)	(sc->unit)
#define sc_if arpcom.ac_if
#define sc_macaddr arpcom.ac_enaddr
#if defined(EPIC_USEIOSPACE)
#define CSR_WRITE_4(sc,reg,val) 					\
	outl( (sc)->iobase + (u_int32_t)(reg), (val) )
#define CSR_WRITE_2(sc,reg,val) 					\
	outw( (sc)->iobase + (u_int32_t)(reg), (val) )
#define CSR_WRITE_1(sc,reg,val) 					\
	outb( (sc)->iobase + (u_int32_t)(reg), (val) )
#define CSR_READ_4(sc,reg) 						\
	inl( (sc)->iobase + (u_int32_t)(reg) )
#define CSR_READ_2(sc,reg) 						\
	inw( (sc)->iobase + (u_int32_t)(reg) )
#define CSR_READ_1(sc,reg) 						\
	inb( (sc)->iobase + (u_int32_t)(reg) )
#else
#define CSR_WRITE_1(sc,reg,val) 					\
	((*(u_int8_t*)((sc)->csr + (u_int32_t)(reg))) = (u_int8_t)(val))
#define CSR_WRITE_2(sc,reg,val) 					\
	((*(u_int16_t*)((sc)->csr + (u_int32_t)(reg))) = (u_int16_t)(val))
#define CSR_WRITE_4(sc,reg,val) 					\
	((*(u_int32_t*)((sc)->csr + (u_int32_t)(reg))) = (u_int32_t)(val))
#define CSR_READ_1(sc,reg) 						\
	(*(u_int8_t*)((sc)->csr + (u_int32_t)(reg)))
#define CSR_READ_2(sc,reg) 						\
	(*(u_int16_t*)((sc)->csr + (u_int32_t)(reg)))
#define CSR_READ_4(sc,reg) 						\
	(*(u_int32_t*)((sc)->csr + (u_int32_t)(reg)))
#endif
#else /* __OpenBSD__ */
#define EPIC_FORMAT	"%s"
#define EPIC_ARGS(sc)	(sc->sc_dev.dv_xname)
#define sc_if	arpcom.ac_if
#define sc_macaddr arpcom.ac_enaddr
#define CSR_WRITE_4(sc,reg,val) 					\
	bus_space_write_4( (sc)->sc_st, (sc)->sc_sh, (reg), (val) )
#define CSR_WRITE_2(sc,reg,val) 					\
	bus_space_write_2( (sc)->sc_st, (sc)->sc_sh, (reg), (val) )
#define CSR_WRITE_1(sc,reg,val) 					\
	bus_space_write_1( (sc)->sc_st, (sc)->sc_sh, (reg), (val) )
#define CSR_READ_4(sc,reg) 						\
	bus_space_read_4( (sc)->sc_st, (sc)->sc_sh, (reg) )
#define CSR_READ_2(sc,reg) 						\
	bus_space_read_2( (sc)->sc_st, (sc)->sc_sh, (reg) )
#define CSR_READ_1(sc,reg) 						\
	bus_space_read_1( (sc)->sc_st, (sc)->sc_sh, (reg) )
#endif

#define	PHY_READ_2(sc,reg)	epic_read_phy_register(sc,reg)
#define PHY_WRITE_2(sc,reg,val)	epic_write_phy_register(sc,reg,val)

