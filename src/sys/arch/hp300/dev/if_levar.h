/*	$NetBSD: if_levar.h,v 1.1 1995/12/10 00:49:37 mycroft Exp $	*/

/*-
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_le.c	8.2 (Berkeley) 11/16/93
 */

/*
 * Ethernet software status per interface.
 *
 * Each interface is referenced by a network interface structure,
 * arpcom.ac_if, which the routing code uses to locate the interface.
 * This structure contains the output queue for the interface, its address, ...
 */
struct	le_softc {
	struct	device sc_dev;		/* base structure */
	struct	arpcom sc_arpcom;	/* Ethernet common part */

	void	(*sc_copytodesc)();	/* Copy to descriptor */
	void	(*sc_copyfromdesc)();	/* Copy from descriptor */

	void	(*sc_copytobuf)();	/* Copy to buffer */
	void	(*sc_copyfrombuf)();	/* Copy from buffer */
	void	(*sc_zerobuf)();	/* and Zero bytes in buffer */

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

	struct	hp_device *sc_hd;
	struct	isr sc_isr;
	struct	lereg0 *sc_r0;		/* DIO registers */
	struct	lereg1 *sc_r1;		/* LANCE registers */
};
