/*	$OpenBSD: if_rl_cardbus.c,v 1.1 2002/06/08 00:10:54 aaron Exp $ */
/*	$NetBSD: if_rl_cardbus.c,v 1.3.8.3 2001/11/14 19:14:02 nathanw Exp $	*/

/*
 * Copyright (c) 2000 Masanori Kanaoka
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * if_rl_cardbus.c:
 *	Cardbus specific routines for RealTek 8139 ethernet adapter.
 *	Tested for 
 *		- elecom-Laneed	LD-10/100CBA (Accton MPX5030)
 *		- MELCO		LPC3-TX-CB   (RealTek 8139)
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/device.h>
 
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_media.h>

#include <machine/endian.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/miivar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/cardbus/cardbusvar.h>
#include <dev/cardbus/cardbusdevs.h>

/*
 * Default to using PIO access for this driver. On SMP systems,
 * there appear to be problems with memory mapped mode: it looks like
 * doing too many memory mapped access back to back in rapid succession
 * can hang the bus. I'm inclined to blame this on crummy design/construction
 * on the part of RealTek. Memory mapped mode does appear to work on
 * uniprocessor systems though.
 */
#define RL_USEIOSPACE 

#include <dev/ic/rtl81x9reg.h>

/*
 * Various supported device vendors/types and their names.
 */
static const struct rl_type rl_cardbus_devs[] = {
	{ CARDBUS_VENDOR_ACCTON, CARDBUS_PRODUCT_ACCTON_MPX5030	},
	{ CARDBUS_VENDOR_REALTEK, CARDBUS_PRODUCT_REALTEK_RT8138 },
	{ CARDBUS_VENDOR_REALTEK, CARDBUS_PRODUCT_REALTEK_RT8139 },
	{ CARDBUS_VENDOR_COREGA, CARDBUS_PRODUCT_COREGA_CB_TXD },
	{ 0, 0 }
};

struct rl_cardbus_softc {
	struct rl_softc sc_rl;	/* real rtk softc */ 

	/* CardBus-specific goo. */
	void *sc_ih;
	cardbus_devfunc_t sc_ct;
	cardbustag_t sc_tag;
	int sc_csr;
	int sc_cben;
	int sc_bar_reg;
	pcireg_t sc_bar_val;
	bus_size_t sc_mapsize;
	int sc_intrline;
};

static int rl_cardbus_match	__P((struct device *, void *, void *));
static void rl_cardbus_attach	__P((struct device *, struct device *, void *));
static int rl_cardbus_detach	__P((struct device *, int));
void rl_cardbus_setup		__P((struct rl_cardbus_softc *));

struct cfattach rl_cardbus_ca = {
	sizeof(struct rl_cardbus_softc), rl_cardbus_match, rl_cardbus_attach,
	    rl_cardbus_detach
};

const struct rl_type *rl_cardbus_lookup
	__P((const struct cardbus_attach_args *));

const struct rl_type *
rl_cardbus_lookup(ca)
	const struct cardbus_attach_args *ca;
{
	const struct rl_type *t;

	for (t = rl_cardbus_devs; t->rl_vid != 0; t++){ 	
		if (CARDBUS_VENDOR(ca->ca_id) == t->rl_vid &&
		    CARDBUS_PRODUCT(ca->ca_id) == t->rl_did) {
			return (t);
		}
	}
	return (NULL);
}

int
rl_cardbus_match(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	struct cardbus_attach_args *ca = aux;

	if (rl_cardbus_lookup(ca) != NULL)
		return (1);

	return (0);
}


void
rl_cardbus_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct rl_cardbus_softc *csc = (struct rl_cardbus_softc *)self;
	struct rl_softc *sc = &csc->sc_rl;
	struct cardbus_attach_args *ca = aux;
	struct cardbus_softc *psc =
	    (struct cardbus_softc *)sc->sc_dev.dv_parent;
	cardbus_chipset_tag_t cc = psc->sc_cc;
	cardbus_function_tag_t cf = psc->sc_cf;                            
	cardbus_devfunc_t ct = ca->ca_ct;
	const struct rl_type *t;
	bus_addr_t adr;

	sc->sc_dmat = ca->ca_dmat;
	csc->sc_ct = ct;
	csc->sc_tag = ca->ca_tag;
	csc->sc_intrline = ca->ca_intrline;

	t = rl_cardbus_lookup(ca); 
	if (t == NULL) { 
		printf("\n"); 
		panic("rl_cardbus_attach: impossible");
	 } 
	
	/*
	 * Map control/status registers.
	 */
	csc->sc_csr = CARDBUS_COMMAND_MASTER_ENABLE;
#ifdef RL_USEIOSPACE
	if (Cardbus_mapreg_map(ct, RL_PCI_LOIO, CARDBUS_MAPREG_TYPE_IO, 0,
	    &sc->rl_btag, &sc->rl_bhandle, &adr, &csc->sc_mapsize) == 0) {
#if rbus
#else
		(*ct->ct_cf->cardbus_io_open)(cc, 0, adr, adr+csc->sc_mapsize);
#endif
		csc->sc_cben = CARDBUS_IO_ENABLE;
		csc->sc_csr |= CARDBUS_COMMAND_IO_ENABLE;
		csc->sc_bar_reg = RL_PCI_LOIO;
		csc->sc_bar_val = adr | CARDBUS_MAPREG_TYPE_IO;
	}
#else
	if (Cardbus_mapreg_map(ct, RL_PCI_LOMEM, CARDBUS_MAPREG_TYPE_MEM, 0,
	    &sc->rl_btag, &sc->rl_bhandle, &adr, &csc->sc_mapsize) == 0) {
#if rbus
#else
		(*ct->ct_cf->cardbus_mem_open)(cc, 0, adr, adr+csc->sc_mapsize);
#endif
		csc->sc_cben = CARDBUS_MEM_ENABLE;
		csc->sc_csr |= CARDBUS_COMMAND_MEM_ENABLE;
		csc->sc_bar_reg = RL_PCI_LOMEM;
		csc->sc_bar_val = adr | CARDBUS_MAPREG_TYPE_MEM;
	}
#endif
	else {
		printf("%s: unable to map deviceregisters\n",
			 sc->sc_dev.dv_xname);
		return;
	}

	Cardbus_function_enable(ct);

	rl_cardbus_setup(csc);

	/*
	 * Map and establish the interrupt.
	 */
	csc->sc_ih = cardbus_intr_establish(cc, cf, csc->sc_intrline, IPL_NET,
	    rl_intr, sc);
	if (csc->sc_ih == NULL) {
		printf(": couldn't establish interrupt\n");
		Cardbus_function_disable(csc->sc_ct);
		return;
	}
	printf(": irq %d", csc->sc_intrline);

	/* XXX - hardcode this, for now */
	sc->rl_type = RL_8139;

	rl_attach(sc);
}

int 
rl_cardbus_detach(self, flags)
	struct device *self;
	int flags;
{
	struct rl_cardbus_softc *csc = (void *) self;
	struct rl_softc *sc = &csc->sc_rl;
	struct cardbus_devfunc *ct = csc->sc_ct;
	int	rv;

#ifdef DIAGNOSTIC
	if (ct == NULL)
		panic("%s: data structure lacks\n", sc->sc_dev.dv_xname);
#endif
	rv = rl_detach(sc);
	if (rv)
		return (rv);
	/*
	 * Unhook the interrut handler.
	 */
	if (csc->sc_ih != NULL)
		cardbus_intr_disestablish(ct->ct_cc, ct->ct_cf, csc->sc_ih);
	
	/*
	 * Release bus space and close window.
	 */
	if (csc->sc_bar_reg != 0)
		Cardbus_mapreg_unmap(ct, csc->sc_bar_reg,
			sc->rl_btag, sc->rl_bhandle, csc->sc_mapsize);

	return (0);
}

void 
rl_cardbus_setup(csc)
	struct rl_cardbus_softc *csc;
{
	struct rl_softc *sc = &csc->sc_rl;
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;
	pcireg_t	reg,command;
	int		pmreg;

	/*
	 * Handle power management nonsense.
	 */
	if (cardbus_get_capability(cc, cf, csc->sc_tag,
	    PCI_CAP_PWRMGMT, &pmreg, 0)) {
		command = cardbus_conf_read(cc, cf, csc->sc_tag, pmreg + 4);
		if (command & RL_PSTATE_MASK) {
			pcireg_t		iobase, membase, irq;

			/* Save important PCI config data. */
			iobase = cardbus_conf_read(cc, cf, csc->sc_tag,
			    RL_PCI_LOIO);
			membase = cardbus_conf_read(cc, cf,csc->sc_tag,
			    RL_PCI_LOMEM);
			irq = cardbus_conf_read(cc, cf,csc->sc_tag,
			    PCI_PRODUCT_DELTA_8139);

			/* Reset the power state. */
			printf("%s: chip is is in D%d power mode "
			    "-- setting to D0\n", sc->sc_dev.dv_xname,
			    command & RL_PSTATE_MASK);
			command &= 0xFFFFFFFC;
			cardbus_conf_write(cc, cf, csc->sc_tag,
			    pmreg + 4, command);

			/* Restore PCI config data. */
			cardbus_conf_write(cc, cf, csc->sc_tag,
			    RL_PCI_LOIO, iobase);
			cardbus_conf_write(cc, cf, csc->sc_tag,
			    RL_PCI_LOMEM, membase);
			cardbus_conf_write(cc, cf, csc->sc_tag,
			    PCI_PRODUCT_DELTA_8139, irq);
		}
	}

	/* Make sure the right access type is on the CardBus bridge. */
	(*ct->ct_cf->cardbus_ctrl)(cc, csc->sc_cben);
	(*ct->ct_cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	/* Program the BAR */
	cardbus_conf_write(cc, cf, csc->sc_tag,
		csc->sc_bar_reg, csc->sc_bar_val);

	/* Enable the appropriate bits in the CARDBUS CSR. */
	reg = cardbus_conf_read(cc, cf, csc->sc_tag, 
	    CARDBUS_COMMAND_STATUS_REG);
	reg &= ~(CARDBUS_COMMAND_IO_ENABLE|CARDBUS_COMMAND_MEM_ENABLE);
	reg |= csc->sc_csr;
	cardbus_conf_write(cc, cf, csc->sc_tag, 
	    CARDBUS_COMMAND_STATUS_REG, reg);

	/*
	 * Make sure the latency timer is set to some reasonable
	 * value.
	 */
	reg = cardbus_conf_read(cc, cf, csc->sc_tag, CARDBUS_BHLC_REG);
	if (CARDBUS_LATTIMER(reg) < 0x20) {
		reg &= ~(CARDBUS_LATTIMER_MASK << CARDBUS_LATTIMER_SHIFT);
		reg |= (0x20 << CARDBUS_LATTIMER_SHIFT);
		cardbus_conf_write(cc, cf, csc->sc_tag, CARDBUS_BHLC_REG, reg);
	}
}

