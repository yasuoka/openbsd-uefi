/*	$NetBSD: if_levar.h,v 1.4 1996/04/29 20:03:23 christos Exp $	*/

/*
 * LANCE Ethernet driver header file
 *
 * Copyright (c) 1994, 1995 Charles M. Hannum.  All rights reserved.
 *
 * Copyright (C) 1993, Paul Richards. This software may be used, modified,
 *   copied, distributed, and sold, in both source and binary form provided
 *   that the above copyright and these terms are retained. Under no
 *   circumstances is the author responsible for the proper functioning
 *   of this software, nor does the author assume any responsibility
 *   for damages incurred with its use.
 */

/* Board types */
#define	BICC		1
#define	BICC_RDP	0xc
#define	BICC_RAP	0xe

#define	NE2100		2
#define	PCnet_ISA	4
#define	PCnet_PCI	5
#define	NE2100_RDP	0x10
#define	NE2100_RAP	0x12

#define	DEPCA		3
#define	DEPCA_CSR	0x0
#define	DEPCA_CSR_SHE		0x80	/* Shared memory enabled */
#define	DEPCA_CSR_SWAP32	0x40	/* Byte swapped */
#define	DEPCA_CSR_DUM		0x08	/* rev E compatibility */
#define	DEPCA_CSR_IM		0x04	/* Interrupt masked */
#define	DEPCA_CSR_IEN		0x02	/* Interrupt enabled */
#define	DEPCA_CSR_NORMAL \
	(DEPCA_CSR_SHE | DEPCA_CSR_DUM | DEPCA_CSR_IEN)
#define	DEPCA_RDP	0x4
#define	DEPCA_RAP	0x6
#define	DEPCA_ADP	0xc

/*
 * Ethernet software status per interface.
 *
 * Each interface is referenced by a network interface structure,
 * arpcom.ac_if, which the routing code uses to locate the interface.
 * This structure contains the output queue for the interface, its address, ...
 */
struct le_softc {
	struct	device sc_dev;		/* base structure */
	struct	arpcom sc_arpcom;	/* Ethernet common part */

	void	(*sc_copytodesc)	/* Copy to descriptor */
			__P((struct le_softc *, void *, int, int));
	void	(*sc_copyfromdesc)	/* Copy from descriptor */
			__P((struct le_softc *, void *, int, int));
	void	(*sc_copytobuf)		/* Copy to buffer */
			__P((struct le_softc *, void *, int, int));
	void	(*sc_copyfrombuf)	/* Copy from buffer */
			__P((struct le_softc *, void *, int, int));
	void	(*sc_zerobuf)		/* and Zero bytes in buffer */
			__P((struct le_softc *, int, int));

	u_int16_t sc_conf3;		/* CSR3 value */

	void	*sc_mem;		/* base address of RAM -- CPU's view */
	u_long	sc_addr;		/* base address of RAM -- LANCE's view */
	u_long	sc_memsize;		/* size of RAM */

	int	sc_nrbuf;		/* number of receive buffers */
	int	sc_ntbuf;		/* number of transmit buffers */
	int	sc_last_rd;
	int	sc_first_td, sc_last_td, sc_no_td;

	int	sc_initaddr;
	int	sc_rmdaddr;
	int	sc_tmdaddr;
	int	sc_rbufaddr;
	int	sc_tbufaddr;

#ifdef LEDEBUG
	int	sc_debug;
#endif

	void	*sc_ih;
	bus_io_handle_t sc_ioh;
	void	*sc_sh;
	int	sc_card;
	int	sc_rap, sc_rdp;		/* LANCE registers */
};
