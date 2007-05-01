/*	$OpenBSD: if_nxreg.h,v 1.17 2007/05/01 11:44:47 reyk Exp $	*/

/*
 * Copyright (c) 2007 Reyk Floeter <reyk@openbsd.org>
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

/*
 * NetXen NX2031/NX2035 register definitions partially based on:
 * http://www.netxen.com/products/downloads/
 *     Ethernet_Driver_Ref_Guide_Open_Source.pdf
 */

#ifndef _NX_REG_H
#define _NX_REG_H

/*
 * Common definitions
 */

#define NX_MAX_PORTS	4

#define NX_MAX_MTU	ETHER_MTU
#define NX_JUMBO_MTU	8000			/* less than 9k */

/* This driver supported the 3.4.31 (3.4.xx) NIC firmware */
#define NX_FIRMWARE_MAJOR	3
#define NX_FIRMWARE_MINOR	4
#define NX_FIRMWARE_BUILD	31
#define NX_FIRMWARE_VER		0x001f0403

/* Used to indicate various states of the NIC and its firmware */
enum nx_state {
	NX_S_FAIL	= -1,	/* Failed to initialize the device */
	NX_S_OFFLINE	= 0,	/* Firmware is not active yet */
	NX_S_RESET	= 2,	/* Firmware is in reset state */
	NX_S_BOOT	= 3,	/* Chipset is booting the firmware */
	NX_S_LOADED	= 4,	/* Firmware is loaded but not initialized */
	NX_S_RELOADED	= 5,	/* Firmware is reloaded and initialized */
	NX_S_READY	= 6	/* Device has been initialized and is ready */
};

/*
 * Hardware descriptors
 */

struct nx_txdesc {
	u_int64_t		tx_next;	/* reserved */
	u_int32_t		tx_addr2_low;	/* low address of buffer 2 */
	u_int32_t		tx_addr2_high;	/* high address of buffer 2 */
	u_int32_t		tx_length;
#define  NX_TXDESC_LENGTH_S	0		/* length */
#define  NX_TXDESC_LENGTH_M	0x00ffffff
#define  NX_TXDESC_TCPOFF_S	24		/* TCP header offset for TSO */
#define  NX_TXDESC_TCPOFF_M	0xff000000
	u_int8_t		tx_ipoff;	/* IP header offset for TSO */
	u_int8_t		tx_nbuf;	/* number of buffers */
	u_int8_t		tx_flags;
#define  NX_TXDESC_F_VLAN	(1<<8)		/* VLAN tagged */
#define  NX_TXDESC_F_TSO	(1<<1)		/* TSO enabled */
#define  NX_TXDESC_F_CKSUM	(1<<0)		/* checksum enabled */
	u_int8_t		tx_opcode;
#define  NX_TXDESC_OP_STOPSTATS	(1<<9)		/* Stop statistics */
#define  NX_TXDESC_OP_GETSTATS	(1<<8)		/* Get statistics */
#define  NX_TXDESC_OP_TX_TSO	(1<<5)		/* TCP packet, do TSO */
#define  NX_TXDESC_OP_TX_IP	(1<<4)		/* IP packet, compute cksum */
#define  NX_TXDESC_OP_TX_UDP	(1<<3)		/* UDP packet, compute cksum */
#define  NX_TXDESC_OP_TX_TCP	(1<<2)		/* TCP packet, compute cksum */
#define  NX_TXDESC_OP_TX	(1<<1)		/* raw Ethernet packet */
	u_int16_t		tx_handle;	/* handle of the buffer */
	u_int16_t		tx_mss;		/* MSS for the packet */
	u_int8_t		tx_port;	/* interface port */
#define  NX_TXDESC_PORT_S	0
#define  NX_TXDESC_PORT_M	0x0f
	u_int8_t		tx_hdrlength;	/* MAC+IP+TCP length for TSO */
	u_int16_t		tx_reserved;
	u_int32_t		tx_addr3_low;	/* low address of buffer 3 */
	u_int32_t		tx_addr3_high;	/* high address of buffer 3 */
	u_int32_t		tx_addr1_low;	/* low address of buffer 1 */
	u_int32_t		tx_addr1_high;	/* high address of buffer 1 */
	u_int16_t		tx_buf1_length;	/* length of buffer 1 */
	u_int16_t		tx_buf2_length;	/* length of buffer 2 */
	u_int16_t		tx_buf3_length;	/* length of buffer 3 */
	u_int16_t		tx_buf4_length;	/* length of buffer 4 */
	u_int32_t		tx_addr4_low;	/* low address of buffer 4 */
	u_int32_t		tx_addr4_high;	/* high address of buffer 4 */
} __packed;

struct nx_rxdesc {
	u_int16_t		rx_handle;	/* handle of the buffer */
	u_int16_t		rx_reserved;
	u_int32_t		rx_length;	/* length of the buffer */
	u_int64_t		rx_addr;	/* address of the buffer */
} __packed;

struct nx_statusdesc {
	u_int16_t		rx_port;
#define  NX_STSDESC_PORT_S	0		/* interface port */
#define  NX_STSDESC_PORT_M	0x000f
#define  NX_STSDESC_STS_S	4		/* completion status */
#define  NX_STSDESC_STS_M	0x00f0
#define   NX_STSDESC_STS_NOCHK	1		/* checksum not verified */
#define   NX_STSDESC_STS_CHKOK	2		/* checksum verified ok */
#define  NX_STSDESC_TYPE_S	8		/* type/index of the ring */
#define  NX_STSDESC_TYPE_M	0x0f00
#define  NX_STSDESC_OPCODE_S	12		/* opcode */
#define  NX_STSDESC_OPCODE_M	0xf000
#define   NX_STSDESC_OPCODE	0xa		/* received packet */
	u_int16_t		rx_length;	/* total length of the packet */
	u_int16_t		rx_handle;	/* handle of the buffer */
	u_int16_t		rx_owner;
#define  NX_STSDESC_OWNER_S	0		/* owner of the descriptor */
#define  NX_STSDESC_OWNER_M	0x0003
#define   NX_STSDESC_OWNER_HOST	1		/* owner is the host (t.b.d) */
#define   NX_STSDESC_OWNER_CARD	2		/* owner is the card */
#define  NX_STSDESC_PROTO_S	2		/* protocol type */
#define  NX_STSDESC_PROTO_M	0x003c
} __packed;

/*
 * Memory layout
 */

#define NXBAR0			PCI_MAPREG_START
#define NXBAR4			(PCI_MAPREG_START + 16)

/* PCI memory setup */
#define NXPCIMEM_SIZE_128MB	0x08000000	/* 128MB size */
#define NXPCIMEM_SIZE_32MB	0x02000000	/* 32MB size */

#define NXPCIMAP_DDR_NET	0x00000000
#define NXPCIMAP_DDR_MD		0x02000000
#define NXPCIMAP_DIRECT_CRB	0x04400000
#define NXPCIMAP_CRB		0x06000000

/* Offsets inside NXPCIMAP_CRB */
#define NXMEMMAP_WINDOW0_START	0x00000000
#define NXMEMMAP_WINDOW0_END	0x01ffffff
#define NXMEMMAP_WINDOW_SIZE	0x02000000
#define NXMEMMAP_PCIE		0x00100000
#define NXMEMMAP_NIU		0x00600000
#define NXMEMMAP_PPE_0		0x01100000
#define NXMEMMAP_PPE_1		0x01200000
#define NXMEMMAP_PPE_2		0x01300000
#define NXMEMMAP_PPE_3		0x01400000
#define NXMEMMAP_PPE_D		0x01500000
#define NXMEMMAP_PPE_I		0x01600000
#define NXMEMMAP_WINDOW1_START	0x02000000
#define NXMEMMAP_WINDOW1_END	0x07ffffff
#define NXMEMMAP_SW		0x02200000	/* XXX 0x02400000? */
#define NXMEMMAP_SIR		0x03200000
#define NXMEMMAP_ROMUSB		0x03300000

#define NXMEMMAP_HWTRANS_M	0xfff00000

/* Window 0 register map  */
#define NXPCIE(_x)		((_x) + 0x06100000)	/* PCI Express */
#define NXNIU(_x)		((_x) + 0x06600000)	/* Network Int Unit */
#define NXPPE_0(_x)		((_x) + 0x07100000)	/* PEGNET 0 */
#define NXPPE_1(_x)		((_x) + 0x07200000)	/* PEGNET 0 */
#define NXPPE_2(_x)		((_x) + 0x07300000)	/* PEGNET 0 */
#define NXPPE_3(_x)		((_x) + 0x07400000)	/* PEGNET 0 */
#define NXPPE_D(_x)		((_x) + 0x07500000)	/* PEGNET D-Cache */
#define NXPPE_I(_x)		((_x) + 0x07600000)	/* PEGNET I-Cache */

/* Window 1 register map */
#define NXPCIE_1(_x)		((_x) + 0x06100000)	/* PCI Express' */
#define NXSW(_x)		((_x) + 0x06200000)	/* Software defined */
#define NXSIR(_x)		((_x) + 0x07200000)	/* 2nd interrupt */
#define NXROMUSB(_x)		((_x) + 0x07300000)	/* ROMUSB */

/* The IMEZ/HMEZ NICs have multiple PCI functions with different registers */
#define NXPCIE_FUNC(_r, _f)	(NXPCIE(_r) + ((_f) * 0x20))

/* Flash layout */
#define NXFLASHMAP_CRBINIT_0	0x00000000	/* CRBINIT */
#define  NXFLASHMAP_CRBINIT_M	0x7fffffff	/* ROM memory barrier */
#define  NXFLASHMAP_CRBINIT_MAX	1023		/* Max CRBINIT entries */
#define NXFLASHMAP_INFO		0x00004000	/* board configuration */
#define NXFLASHMAP_INITCODE	0x00006000	/* chipset-specific code */
#define NXFLASHMAP_BOOTLOADER	0x00010000	/* boot loader */
#define  NXFLASHMAP_BOOTLDSIZE	1024		/* boot loader size */
#define NXFLASHMAP_FIRMWARE_0	0x00043000	/* compressed firmware image */
#define NXFLASHMAP_FIRMWARE_1	0x00200000	/* backup firmware image */
#define NXFLASHMAP_PXE		0x003d0000	/* PXE image */
#define NXFLASHMAP_USER		0x003e8000	/* user-specific ares */
#define NXFLASHMAP_VPD		0x003e8c00	/* vendor private data */
#define NXFLASHMAP_LICENSE	0x003e9000	/* firmware license (?) */
#define NXFLASHMAP_CRBINIT_1	0x003f0000	/* backup of CRBINIT */

/*
 * PCI Express Registers
 */

/* Interrupt Vector */
#define NXISR_INT_VECTOR		NXPCIE(0x00010100)
#define  NXISR_INT_VECTOR_TARGET3	(1<<10)	/* interrupt for function 3 */
#define  NXISR_INT_VECTOR_TARGET2	(1<<9)	/* interrupt for function 2 */
#define  NXISR_INT_VECTOR_TARGET1	(1<<8)	/* interrupt for function 1 */
#define  NXISR_INT_VECTOR_TARGET0	(1<<7)	/* interrupt for function 0 */
#define  NXISR_INT_VECTOR_RC_INT	(1<<5)	/* root complex interrupt */

/* Interrupt Mask */
#define NXISR_INT_MASK			NXPCIE(0x00010104)
#define  NXISR_INT_MASK_TARGET3		(1<<10)	/* mask for function 3 */
#define  NXISR_INT_MASK_TARGET2		(1<<9)	/* mask for function 2 */
#define  NXISR_INT_MASK_TARGET1		(1<<8)	/* mask for function 1 */
#define  NXISR_INT_MASK_TARGET0		(1<<7)	/* mask for function 0 */
#define  NXISR_INT_MASK_RC_INT		(1<<5)	/* root complex mask */

/* SW Window */
#define NXCRB_WINDOW(_f)		NXPCIE_FUNC(0x00010210, _f)
#define  NXCRB_WINDOW_1			(1<<25)	/* Set this flag for Win 1 */

/* Lock registers (semaphores between chipset and driver) */
#define NXSEM_FLASH_LOCK	NXPCIE(0x0001c010)	/* Flash lock */
#define  NXSEM_FLASH_LOCK_M	0xffffffff
#define  NXSEM_FLASH_LOCKED	(1<<0)			/* R/O: is locked */
#define NXSEM_FLASH_UNLOCK	NXPCIE(0x0001c014)	/* Flash unlock */
#define NXSEM_PHY_LOCK		NXPCIE(0x0001c018)	/* PHY lock */
#define  NXSEM_PHY_LOCK_M	0xffffffff
#define  NXSEM_PHY_LOCKED	(1<<0)			/* R/O: is locked */
#define NXSEM_PHY_UNLOCK	PXPCIE(0x0001c01c)	/* PHY unlock */

/*
 * Network Interface Unit (NIU) registers
 */

/* Mode Register (see also NXNIU_RESET_SYS_FIFOS) */
#define NXNIU_MODE			NXNIU(0x00000000)
#define  NXNIU_MODE_XGE			(1<<2)	/* XGE interface enabled */
#define  NXNIU_MODE_GBE			(1<<1)	/* 4 GbE interfaces enabled */
#define  NXNIU_MODE_FC			(1<<0)	/* *Fibre Channel enabled */
#define NXNIU_MODE_DEF			NUI_XGE_ENABLE

/* 10G - 1G Mode Enable Register */
#define NXNIU_XG_SINGLE_TERM		NXNIU(0x00000004)
#define  NXNIU_XG_SINGLE_TERM_ENABLE	(1<<0)	/* Enable 10G + 1G mode */
#define NXNIU_XG_SINGLE_TERM_DEF	0		/* Disabled */

/* XGE Reset Register */
#define NXNIU_XG_RESET			NXNIU(0x0000001c)
#define  NXNIU_XG_RESET_CD		(1<<1)	/* Reset channels CD */
#define  NXNIU_XG_RESET_AB		(1<<0)	/* Reset channels AB */
#define NXNIU_XG_RESET_DEF		(NXNIU_XG_RESET_AB|NXNIU_XG_RESET_CD)

/* Interrupt Mask Register */
#define NXNIU_INT_MASK			NXNIU(0x00000040)
#define  NXNIU_INT_MASK_XG		(1<<6)	/* XGE Interrupt Mask */
#define  NXNIU_INT_MASK_RES5		(1<<5)	/* Reserved bit */
#define  NXNIU_INT_MASK_RES4		(1<<4)	/* Reserved bit */
#define  NXNIU_INT_MASK_GB3		(1<<3)	/* GbE 3 Interrupt Mask */
#define  NXNIU_INT_MASK_GB2		(1<<2)	/* GbE 2 Interrupt Mask */
#define  NXNIU_INT_MASK_GB1		(1<<1)	/* GbE 1 Interrupt Mask */
#define  NXNIU_INT_MASK_GB0		(1<<0)	/* GbE 0 Interrupt Mask */
#define NXNIU_INT_MASK_DEF		(				\
	NXNIU_INT_MASK_XG|NXNIU_INT_MASK_RES5|NXNIU_INT_MASK_RES4|	\
	NXNIU_INT_MASK_GB3|NXNIU_INT_MASK_GB2|NXNIU_INT_MASK_GB1|	\
	NXNIU_INT_MASK_GB0)			/* Reserved bits enabled */

/* Reset System FIFOs Register (needed before changing NXNIU_MODE) */
#define NXNIU_RESET_SYS_FIFOS		NXNIU(0x00000088)
#define  NXNIU_RESET_SYS_FIFOS_RX	(1<<31)	/* Reset all Rx FIFOs */
#define  NXNIU_RESET_SYS_FIFOS_TX	(1<<0)	/* Reset all Tx FIFOs */
#define NXNIU_RESET_SYS_FIFOS_DEF	0	/* Disabled */

/* XGE Configuration 0 Register */
#define NXNIU_XGE_CONFIG0		NXNIU(0x00070000)
#define  NXNIU_XGE_CONFIG0_SOFTRST_FIFO	(1<<31)	/* Soft reset FIFOs */
#define  NXNIU_XGE_CONFIG0_SOFTRST_MAC	(1<<4)	/* Soft reset XGE MAC */
#define  NXNIU_XGE_CONFIG0_RX_ENABLE	(1<<2)	/* Enable frame Rx */
#define  NXNIU_XGE_CONFIG0_TX_ENABLE	(1<<0)	/* Enable frame Tx */
#define NXNIU_XGE_CONFIG0_DEF		0	/* Disabled */

/* XGE Configuration 1 Register */
#define NXNIU_XGE_CONFIG1		NXNIU(0x00070004)
#define  NXNIU_XGE_CONFIG1_PROMISC	(1<<13)	/* Pass all Rx frames */
#define  NXNIU_XGE_CONFIG1_MCAST_ENABLE	(1<<12) /* Rx all multicast frames */
#define  NXNIU_XGE_CONFIG1_SEQ_ERROR	(1<<10) /* Sequence error detection */
#define  NXNIU_XGE_CONFIG1_NO_PAUSE	(1<<8)	/* Ignore pause frames */
#define  NXNIU_XGE_CONFIG1_LOCALERR	(1<<6)	/* Wire local error */
#define   NXNIU_XGE_CONFIG1_LOCALERR_FE	0	/* Signal with 0xFE */
#define   NXNIU_XGE_CONFIG1_LOCALERR_I	1	/* Signal with Ierr */
#define  NXNIU_XGE_CONFIG1_NO_MAXSIZE	(1<<5)	/* Ignore max Rx size */
#define  NXNIU_XGE_CONFIG1_CRC_TX	(1<<1)	/* Append CRC to Tx frames */
#define  NXNIU_XGE_CONFIG1_CRC_RX	(1<<0)	/* Remove CRC from Rx frames */
#define NXNIU_XGE_CONFIG1_DEF		0	/* Disabled */

/*
 * Software defined registers (used by the firmware or the driver)
 */

/* Chipset state registers */
#define NXSW_ROM_LOCK_ID	NXSW(0x2100)	/* Used for locking the ROM */
#define  NXSW_ROM_LOCK_DRV	0x0d417340	/* Driver ROM lock ID */
#define NXSW_PHY_LOCK_ID	NXSW(0x2120)	/* Used for locking the PHY */
#define  NXSW_PHY_LOCK_DRV	0x44524956	/* Driver PHY lock ID */
#define NXSW_FW_VERSION_MAJOR	NXSW(0x2150)	/* Major f/w version */
#define NXSW_FW_VERSION_MINOR	NXSW(0x2154)	/* Minor f/w version */
#define NXSW_FW_VERSION_BUILD	NXSW(0x2158)	/* Build/Sub f/w version */
#define NXSW_BOOTLD_CONFIG	NXSW(0x21fc)
#define  NXSW_BOOTLD_CONFIG_ROM	0x00000000	/* Load firmware from flasg */
#define  NXSW_BOOTLD_CONFIG_RAM	0x12345678	/* Load firmware from memory */

/* Misc SW registers */
#define NXSW_CMD_PRODUCER_OFF	NXSW(0x2208)	/* Producer CMD ring index */
#define NXSW_CMD_CONSUMER_OFF	NXSW(0x220c)	/* Consumer CMD ring index */
#define NXSW_RCV_PRODUCER_OFF	NXSW(0x2218)	/* Producer Rx ring index */
#define NXSW_RCV_CONSUMER_OFF	NXSW(0x221c)	/* Consumer Rx ring index */
#define NXSW_RCV_GLOBAL_RING	NXSW(0x2220)	/* Address of Rx buffer */
#define NXSW_RCV_STATUS_RING	NXSW(0x2224)	/* Address of Rx status ring */
#define NXSW_RCV_STATUS_PROD	NXSW(0x2228)	/* Producer Rx status index */
#define NXSW_RCV_STATUS_CONS	NXSW(0x222c)	/* Consumer Rx status index */
#define NXSW_CMD_ADDR_HI	NXSW(0x2230)	/* CMD ring phys address */
#define NXSW_CMD_ADDR_LO	NXSW(0x2234)	/* CMD ring phys address */
#define NXSW_CMD_RING_SIZE	NXSW(0x2238)	/* Entries in the CMD ring */
#define NXSW_RCV_RING_SIZE	NXSW(0x223c)	/* Entries in the Rx ring */
#define NXSW_JRCV_RING_SIZE	NXSW(0x2240)	/* Entries in the jumbo ring */
#define NXSW_RCVPEG_STATE	NXSW(0x2248)	/* State of the NX2031 */
#define NXSW_CMDPEG_STATE	NXSW(0x2250)	/* State of the firmware */
#define  NXSW_CMDPEG_STATE_M	0xffff		/* State mask */
#define  NXSW_CMDPEG_INIT_START	0xff00		/* Start of initialization */
#define  NXSW_CMDPEG_INIT_DONE	0xff01		/* Initialization complete */
#define  NXSW_CMDPEG_INIT_FAIL	0xffff		/* Initialization failed */
#define NXSW_GLOBAL_INT_COAL	NXSW(0x2280)	/* Interrupt coalescing */
#define NXSW_INT_COAL_MODE	NXSW(0x2284)	/* Reserved */
#define NXSW_MAX_RCV_BUFS	NXSW(0x2288)	/* Interrupt tuning register */
#define NXSW_TX_INT_THRESHOLD	NXSW(0x228c)	/* Interrupt tuning register */
#define NXSW_RX_PKT_TIMER	NXSW(0x2290)	/* Interrupt tuning register */
#define NXSW_TX_PKT_TIMER	NXSW(0x2294)	/* Interrupt tuning register */
#define NXSW_RX_PKT_CNT		NXSW(0x2298)	/* Rx packet count register */
#define NXSW_RX_TMR_CNT		NXSW(0x229c)	/* Rx timer count register */
#define NXSW_XG_STATE		NXSW(0x22a0)	/* PHY state register */
#define  NXSW_XG_LINK_UP	(1<<4)		/* 10G PHY state up */
#define  NXSW_XG_LINK_DOWN	(1<<5)		/* 10G PHY state down */
#define NXSW_JRCV_PRODUCER_OFF	NXSW(0x2300)	/* Producer jumbo ring index */
#define NXSW_JRCV_CONSUMER_OFF	NXSW(0x2304)	/* Consumer jumbo ring index */
#define NXSW_JRCV_GLOBAL_RING	NXSW(0x2220)	/* Address of jumbo buffer */
#define NXSW_TEMP		NXSW(0x23b4)	/* Temperature sensor */
#define  NXSW_TEMP_STATE_M	0x0000ffff	/* Temp state mask */
#define  NXSW_TEMP_STATE_S	0		/* Temp state shift */
#define   NXSW_TEMP_STATE_NONE	0x0000		/* Temp state is UNSPEC */
#define   NXSW_TEMP_STATE_OK	0x0001		/* Temp state is OK */
#define   NXSW_TEMP_STATE_WARN	0x0002		/* Temp state is WARNING */
#define   NXSW_TEMP_STATE_CRIT	0x0003		/* Temp state is CRITICAL */
#define  NXSW_TEMP_VAL_M	0xffff0000	/* Temp deg celsius mask */
#define  NXSW_TEMP_VAL_S	16		/* Temp deg celsius shift */
#define NXSW_DRIVER_VER		NXSW(0x24a0)	/* Host driver version */

/*
 * Secondary Interrupt Registers
 */

/* I2Q Register */
#define NXI2Q_CLR_PCI_HI		NXSIR(0x00000034)
#define  NXI2Q_CLR_PCI_HI_PHY		(1<<13)	/* PHY interrupt */
#define NXI2Q_CLR_PCI_HI_DEF		0	/* Cleared */

/*
 * ROMUSB registers
 */

/* Status Register */
#define NXROMUSB_GLB_STATUS		NXROMUSB(0x00000004)	/* ROM Status */
#define  NXROMUSB_GLB_STATUS_DONE	(1<<1)			/* Ready */

/* Reset Unit Register */
#define NXROMUSB_GLB_SW_RESET		NXROMUSB(0x00000008)
#define  NXROMUSB_GLB_SW_RESET_EFC_SIU	(1<<30)	/* EFC_SIU reset */
#define  NXROMUSB_GLB_SW_RESET_NIU	(1<<29)	/* NIU software reset */
#define  NXROMUSB_GLB_SW_RESET_U0QMSQG	(1<<28)	/* Network side QM_SQG reset */
#define  NXROMUSB_GLB_SW_RESET_U1QMSQG	(1<<27)	/* Storage side QM_SQG reset */
#define  NXROMUSB_GLB_SW_RESET_C2C1	(1<<26)	/* Chip to Chip 1 reset */
#define  NXROMUSB_GLB_SW_RESET_C2C0	(1<<25)	/* Chip to Chip 2 reset */
#define  NXROMUSB_GLB_SW_RESET_U1PEGI	(1<<11)	/* Storage Pegasus I-Cache */
#define  NXROMUSB_GLB_SW_RESET_U1PEGD	(1<<10)	/* Storage Pegasus D-Cache */
#define  NXROMUSB_GLB_SW_RESET_U1PEG3	(1<<9)	/* Storage Pegasus3 reset */
#define  NXROMUSB_GLB_SW_RESET_U1PEG2	(1<<8)	/* Storage Pegasus2 reset */
#define  NXROMUSB_GLB_SW_RESET_U1PEG1	(1<<7)	/* Storage Pegasus1 reset */
#define  NXROMUSB_GLB_SW_RESET_U1PEG0	(1<<6)	/* Storage Pegasus0 reset */
#define  NXROMUSB_GLB_SW_RESET_U0PEGI	(1<<11)	/* Network Pegasus I-Cache */
#define  NXROMUSB_GLB_SW_RESET_U0PEGD	(1<<10)	/* Network Pegasus D-Cache */
#define  NXROMUSB_GLB_SW_RESET_U0PEG3	(1<<9)	/* Network Pegasus3 reset */
#define  NXROMUSB_GLB_SW_RESET_U0PEG2	(1<<8)	/* Network Pegasus2 reset */
#define  NXROMUSB_GLB_SW_RESET_U0PEG1	(1<<7)	/* Network Pegasus1 reset */
#define  NXROMUSB_GLB_SW_RESET_U0PEG0	(1<<6)	/* Network Pegasus0 reset */
#define  NXROMUSB_GLB_SW_RESET_PPE	0xf0	/* Protocol Processing Engine */
#define NXROMUSB_GLB_SW_RESET_DEF	0xffffffff

/* Casper Reset Register */
#define NXROMUSB_GLB_CAS_RESET		NXROMUSB(0x00000038)
#define  NXROMUSB_GLB_CAS_RESET_ENABLE	(1<<0)	/* Enable Casper reset */
#define  NXROMUSB_GLB_CAS_RESET_DISABLE	0
#define NXROMUSB_GLB_CAS_RESET_DEF	0	/* Disabled */

/* Reset register */
#define NXROMUSB_GLB_PEGTUNE		NXROMUSB(0x0000005c)
#define  NXROMUSB_GLB_PEGTUNE_DONE	(1<<0)

/* Chip clock control register */
#define NXROMUSB_GLB_CHIPCLKCONTROL	NXROMUSB(0x000000a8)
#define  NXROMUSB_GLB_CHIPCLKCONTROL_ON	0x00003fff

/* ROM Register */
#define NXROMUSB_ROM_CONTROL		NXROMUSB(0x00010000)
#define NXROMUSB_ROM_OPCODE		NXROMUSB(0x00010004)
#define  NXROMUSB_ROM_OPCODE_READ	0x0000000b
#define NXROMUSB_ROM_ADDR		NXROMUSB(0x00010008)
#define NXROMUSB_ROM_WDATA		NXROMUSB(0x0001000c)
#define NXROMUSB_ROM_ABYTE_CNT		NXROMUSB(0x00010010)
#define NXROMUSB_ROM_DUMMY_BYTE_CNT	NXROMUSB(0x00010014)
#define NXROMUSB_ROM_RDATA		NXROMUSB(0x00010018)
#define NXROMUSB_ROM_AGT_TAG		NXROMUSB(0x0001001c)
#define NXROMUSB_ROM_TIME_PARM		NXROMUSB(0x00010020)
#define NXROMUSB_ROM_CLK_DIV		NXROMUSB(0x00010024)
#define NXROMUSB_ROM_MISS_INSTR		NXROMUSB(0x00010028)

/*
 * Flash data structures
 */

enum nxb_board_types {
	NXB_BOARDTYPE_P1BD		= 0,
	NXB_BOARDTYPE_P1SB		= 1,
	NXB_BOARDTYPE_P1SMAX		= 2,
	NXB_BOARDTYPE_P1SOCK		= 3,

	NXB_BOARDTYPE_P2SOCK31		= 8,
	NXB_BOARDTYPE_P2SOCK35		= 9,

	NXB_BOARDTYPE_P2SB35_4G		= 10,
	NXB_BOARDTYPE_P2SB31_10G	= 11,
	NXB_BOARDTYPE_P2SB31_2G		= 12,
	NXB_BOARDTYPE_P2SB31_10GIMEZ	= 13,
	NXB_BOARDTYPE_P2SB31_10GHMEZ	= 14,
	NXB_BOARDTYPE_P2SB31_10GCX4	= 15
};

#define NXB_MAX_PORTS	NX_MAX_PORTS		/* max supported ports */

struct nxb_info {
	u_int32_t	ni_hdrver;		/* Board info version */
#define  NXB_VERSION	0x00000001		/* board information version */

	u_int32_t	ni_board_mfg;
	u_int32_t	ni_board_type;
	u_int32_t	ni_board_num;

	u_int32_t	ni_chip_id;
	u_int32_t	ni_chip_minor;
	u_int32_t	ni_chip_major;
	u_int32_t	ni_chip_pkg;
	u_int32_t	ni_chip_lot;

	u_int32_t	ni_port_mask;
	u_int32_t	ni_peg_mask;
	u_int32_t	ni_icache;
	u_int32_t	ni_dcache;
	u_int32_t	ni_casper;

	u_int32_t	ni_lladdr0_low;
	u_int32_t	ni_lladdr1_low;
	u_int32_t	ni_lladdr2_low;
	u_int32_t	ni_lladdr3_low;

	u_int32_t	ni_mnsync_mode;
	u_int32_t	ni_mnsync_shift_cclk;
	u_int32_t	ni_mnsync_shift_mclk;
	u_int32_t	ni_mnwb_enable;
	u_int32_t	ni_mnfreq_crystal;
	u_int32_t	ni_mnfreq_speed;
	u_int32_t	ni_mnorg;
	u_int32_t	ni_mndepth;
	u_int32_t	ni_mnranks0;
	u_int32_t	ni_mnranks1;
	u_int32_t	ni_mnrd_latency0;
	u_int32_t	ni_mnrd_latency1;
	u_int32_t	ni_mnrd_latency2;
	u_int32_t	ni_mnrd_latency3;
	u_int32_t	ni_mnrd_latency4;
	u_int32_t	ni_mnrd_latency5;
	u_int32_t	ni_mnrd_latency6;
	u_int32_t	ni_mnrd_latency7;
	u_int32_t	ni_mnrd_latency8;
	u_int32_t	ni_mndll[18];
	u_int32_t	ni_mnddr_mode;
	u_int32_t	ni_mnddr_extmode;
	u_int32_t	ni_mntiming0;
	u_int32_t	ni_mntiming1;
	u_int32_t	ni_mntiming2;

	u_int32_t	ni_snsync_mode;
	u_int32_t	ni_snpt_mode;
	u_int32_t	ni_snecc_enable;
	u_int32_t	ni_snwb_enable;
	u_int32_t	ni_snfreq_crystal;
	u_int32_t	ni_snfreq_speed;
	u_int32_t	ni_snorg;
	u_int32_t	ni_sndepth;
	u_int32_t	ni_sndll;
	u_int32_t	ni_snrd_latency;

	u_int32_t	ni_lladdr0_high;
	u_int32_t	ni_lladdr1_high;
	u_int32_t	ni_lladdr2_high;
	u_int32_t	ni_lladdr3_high;

	u_int32_t	ni_magic;
#define  NXB_MAGIC	0x12345678		/* magic value */

	u_int32_t	ni_mnrd_imm;
	u_int32_t	ni_mndll_override;
} __packed;

#define NXB_MAX_PORT_LLADDRS	32

struct nxb_imageinfo {
	u_int32_t	nim_bootld_ver;
	u_int32_t	nim_bootld_size;
	u_int32_t	nim_image_ver;
#define  NXB_IMAGE_MAJOR_S	0
#define  NXB_IMAGE_MAJOR_M	0x000000ff
#define  NXB_IMAGE_MINOR_S	8
#define  NXB_IMAGE_MINOR_M	0x0000ff00
#define  NXB_IMAGE_BUILD_S	16
#define  NXB_IMAGE_BUILD_M	0xffff0000
	u_int32_t	nim_image_size;
} __packed;

struct nxb_userinfo {
	u_int8_t		nu_flash_md5[1024];

	struct nxb_imageinfo	nu_image;

	u_int32_t		nu_primary;
	u_int32_t		nu_secondary;
	u_int64_t		nu_lladdr[NXB_MAX_PORTS * NXB_MAX_PORT_LLADDRS];
	u_int32_t		nu_subsys_id;
	u_int8_t		nu_serial_num[32];
	u_int32_t		nu_bios_ver;

	/* Followed by user-specific data */
} __packed;

/* Appended to the on-disk firmware image, values in network byte order */
struct nxb_firmware_header {
	u_int32_t		 fw_hdrver;
#define NX_FIRMWARE_HDRVER	 0	/* version of the firmware header */
	struct nxb_imageinfo	 fw_image;
#define fw_image_ver		 fw_image.nim_image_ver
#define fw_image_size		 fw_image.nim_image_size
#define fw_bootld_ver		 fw_image.nim_bootld_ver
#define fw_bootld_size		 fw_image.nim_bootld_size
} __packed;

#endif /* _NX_REG_H */
