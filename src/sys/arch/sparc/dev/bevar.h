/*	$OpenBSD: bevar.h,v 1.9 1998/11/02 05:50:59 jason Exp $	*/

/*
 * Copyright (c) 1998 Theo de Raadt and Jason L. Wright.
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
 * 3. The name of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

struct besoftc {
	struct	device sc_dev;
	struct	sbusdev sc_sd;		/* sbus device */
	struct	intrhand sc_ih;		/* interrupt vectoring */
	struct	arpcom sc_arpcom;	/* ethernet common */
	struct	ifmedia sc_ifmedia;	/* interface media */

	struct	qec_softc *sc_qec;	/* QEC parent */
	struct	qecregs *sc_qr;		/* QEC registers */
	struct	be_bregs *sc_br;	/* be registers */
	struct	be_cregs *sc_cr;	/* channel registers */
	struct	be_tregs *sc_tr;	/* transceiver registers */

	u_int	sc_rev;

	int	sc_channel;		/* channel number */
	int	sc_promisc;
	int	sc_burst;
	int	sc_tcvr_type;
	int	sc_nticks;		/* negotiation ticks */

	struct	be_bufs *sc_bufs, *sc_bufs_dva;
	struct	be_desc *sc_desc, *sc_desc_dva;

	int	sc_no_td, sc_first_td, sc_last_td;
	int	sc_last_rd;
};
