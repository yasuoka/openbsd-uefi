/*	$OpenBSD: schizovar.h,v 1.3 2003/02/17 01:29:20 henric Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

struct schizo_pbm {
	struct schizo_softc *sp_sc;

	struct schizo_range *sp_range;
	pci_chipset_tag_t sp_pc;
	int sp_nreg;
	int sp_nrange;
	int sp_nintmap;

	bus_space_tag_t		sp_memt;
	bus_space_tag_t		sp_iot;
	bus_space_tag_t		sp_regt;
	bus_space_handle_t	sp_regh;
	bus_space_tag_t		sp_cfgt;
	bus_space_handle_t	sp_cfgh;
	bus_dma_tag_t		sp_dmat;
	int			sp_bus;
	int			sp_flags;
	int			sp_bus_a;
	bus_addr_t		sp_confpaddr;
	struct iommu_state	sp_is;
	struct strbuf_ctl	sp_sb;
	char			pp_flush[0x80];
};

struct schizo_softc {
	struct device sc_dv;
	int sc_node;
	bus_dma_tag_t sc_dmat;
	bus_space_tag_t sc_bust;
	bus_addr_t sc_ctrl;
	bus_space_handle_t sc_ctrlh;
};
