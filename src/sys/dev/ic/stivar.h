/*	$OpenBSD: stivar.h,v 1.2 2001/01/11 21:30:30 mickey Exp $	*/

/*
 * Copyright (c) 2000 Michael Shalayeff
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _IC_STIVAR_H_
#define _IC_STIVAR_H_

/* #define	STIDEBUG */

struct sti_softc {
	struct device sc_dev;
	void *sc_ih;

	u_int	sc_flags;
#define	STI_TEXTMODE	0x0001
#define	STI_CLEARSCR	0x0002
	int	sc_devtype;
	
	bus_space_tag_t iot, memt;
	bus_space_handle_t ioh, romh, fbh;

	struct sti_dd sc_dd;		/* in word format */
	struct sti_font *sc_fonts;	/* in word format */
	struct sti_cfg sc_cfg;
	struct sti_ecfg sc_ecfg;
	struct sti_fontcfg sc_fontcfg;

	struct wsscreen_descr *sc_screens;
	struct wsscreen_list sti_screenlist;

	vaddr_t sc_code;

	sti_init_t	init;
	sti_mgmt_t	mgmt;
	sti_unpmv_t	unpmv;
	sti_blkmv_t	blkmv;
	sti_test_t	test;
	sti_exhdl_t	exhdl;
	sti_inqconf_t	inqconf;
	sti_scment_t	scment;
	sti_dmac_t	dmac;
	sti_flowc_t	flowc;
	sti_utiming_t	utiming;
	sti_pmgr_t	pmgr;
	sti_util_t	util;
};

void sti_attach_common __P((struct sti_softc *sc));
int sti_intr __P((void *v));

#endif /* _IC_STIVAR_H_ */
