/*	$OpenBSD: if_wi_pci.c,v 1.18 2002/03/30 21:57:40 millert Exp $	*/

/*
 * Copyright (c) 2001, 2002 Todd C. Miller <Todd.Miller@courtesan.com>
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
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * PCI attachment for the Wavelan driver.  There are two basic types
 * of PCI card supported:
 *
 * 1) Cards based on the Prism2.5 Mini-PCI chipset
 * 2) Cards that use a dumb ISA->PCI bridge
 *
 * Only the first type are "true" PCI cards.
 *
 * The latter are often sold as "PCI wireless card adapters" and are
 * sold by several vendors.  Most are simply rebadged versions of the
 * Eumitcom WL11000P or Global Sun Technology GL24110P02.
 * These cards use the PLX 9052 dumb bridge chip to connect a PCMCIA
 * wireless card to the PCI bus.  Because it is a dumb bridge and
 * not a true PCMCIA bridge, the PCMCIA subsystem is not involved
 * (or even required).  The PLX 9052 provides multiple PCI address
 * space mappings.  The primary mappings at PCI registers 0x10 (mem)
 * and 0x14 (I/O) are for the PLX chip itself, *NOT* the PCMCIA card.
 * The mem and I/O spaces for the PCMCIA card are mapped to 0x18 and
 * 0x1C respectively.
 * The PLX 9050/9052 datasheet may be downloaded from PLX at
 *	http://www.plxtech.com/products/toolbox/9050.htm
 *
 * This driver also supports the TMD7160 dumb bridge chip which is used
 * on some versions of the NDC/Sohoware NCP130.  The TMD7160 provides
 * two PCI I/O registers.  The first, at 0x14, is for the TMD7160
 * chip itself.  The second, at 0x18, is for the Prism2 chip.
 * The datasheet for the TMD7160 does not seem to be publicly available.
 * The magic for initializing the chip was gleened from NDC's version of
 * the Linux wlan driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <net/if_ieee80211.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/if_wireg.h>
#include <dev/ic/if_wi_ieee.h>
#include <dev/ic/if_wivar.h>

/* Values for pp_type */
#define WI_PCI_PRISM		0x01	/* Intersil Mini-PCI */
#define WI_PCI_PLX		0x02	/* PLX 905x dumb bridge */
#define WI_PCI_TMD		0x03	/* TMD 7160 dumb bridge */

const struct wi_pci_product *wi_pci_lookup(struct pci_attach_args *pa);
int	wi_pci_match(struct device *, void *, void *);
void	wi_pci_attach(struct device *, struct device *, void *);
int	wi_intr(void *);
int	wi_attach(struct wi_softc *, int);

struct cfattach wi_pci_ca = {
	sizeof (struct wi_softc), wi_pci_match, wi_pci_attach
};

static const struct wi_pci_product {
	pci_vendor_id_t pp_vendor;
	pci_product_id_t pp_product;
	int pp_type;
} wi_pci_products[] = {
	{ PCI_VENDOR_GLOBALSUN, PCI_PRODUCT_GLOBALSUN_GL24110P, WI_PCI_PLX },
	{ PCI_VENDOR_GLOBALSUN, PCI_PRODUCT_GLOBALSUN_GL24110P02, WI_PCI_PLX },
	{ PCI_VENDOR_EUMITCOM, PCI_PRODUCT_EUMITCOM_WL11000P, WI_PCI_PLX },
	{ PCI_VENDOR_USR2, PCI_PRODUCT_USR2_USR11000P, WI_PCI_PLX },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CRWE777A, WI_PCI_PLX },
	{ PCI_VENDOR_NETGEAR, PCI_PRODUCT_NETGEAR_MA301, WI_PCI_PLX },
	{ PCI_VENDOR_NDC, PCI_PRODUCT_NDC_NCP130, WI_PCI_PLX },
	{ PCI_VENDOR_NDC, PCI_PRODUCT_NDC_NCP130A2, WI_PCI_TMD },
	{ PCI_VENDOR_INTERSIL, PCI_PRODUCT_INTERSIL_MINI_PCI_WLAN, WI_PCI_PRISM },
	{ 0, 0 }
};

const struct wi_pci_product *
wi_pci_lookup(pa)
	struct pci_attach_args *pa;
{
	const struct wi_pci_product *pp;

	for (pp = wi_pci_products; pp->pp_product != 0; pp++) {
		if (PCI_VENDOR(pa->pa_id) == pp->pp_vendor && 
		    PCI_PRODUCT(pa->pa_id) == pp->pp_product)
			return (pp);
	}

	return (NULL);
}

int
wi_pci_match(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	return (wi_pci_lookup(aux) != NULL);
}

void
wi_pci_attach(parent, self, aux)
	struct device *parent;
	struct device *self;
	void *aux;
{
	struct wi_softc *sc = (struct wi_softc *)self;
	struct pci_attach_args *pa = aux;
	const struct wi_pci_product *pp;
	pci_intr_handle_t ih;
	bus_space_handle_t localh, ioh, memh;
	bus_space_tag_t localt;
	bus_space_tag_t iot = pa->pa_iot;
	bus_space_tag_t memt = pa->pa_memt;
	bus_size_t localsize, memsize;
	pci_chipset_tag_t pc = pa->pa_pc;
	u_int32_t command;
	pcireg_t csr;
	const char *intrstr;

	pp = wi_pci_lookup(pa);
	/* Map memory and I/O registers. */
	switch (pp->pp_type) {
	case WI_PCI_PLX:
		if (pci_mapreg_map(pa, WI_PCI_PLX_MEMRES, PCI_MAPREG_TYPE_MEM, 0,
		    &memt, &memh, NULL, &memsize, 0) != 0) {
			printf(": can't map mem space\n");
			return;
		}
		if (pci_mapreg_map(pa, WI_PCI_PLX_IORES, PCI_MAPREG_TYPE_IO, 0,
		    &iot, &ioh, NULL, NULL, 0) != 0) {
			printf(": can't map I/O space\n");
			return;
		}
		if (pci_mapreg_map(pa, WI_PCI_PLX_LOCALRES, PCI_MAPREG_TYPE_IO, 0,
		    &localt, &localh, NULL, &localsize, 0) != 0) {
			printf(": can't map PLX I/O space\n");
			return;
		}
		break;
	case WI_PCI_PRISM:
		if (pci_mapreg_map(pa, WI_PCI_CBMA, PCI_MAPREG_TYPE_MEM,
		    0, &iot, &ioh, NULL, NULL, 0) != 0) {
			printf(": can't map mem space\n");
			return;
		}
		sc->sc_pci = 1;
		break;
	case WI_PCI_TMD:
		if (pci_mapreg_map(pa, WI_PCI_TMD_LOCALRES, PCI_MAPREG_TYPE_IO,
		    0, &localt, &localh, NULL, &localsize, 0) != 0) {
			printf(": can't map TMD I/O space\n");
			return;
		}
		if (pci_mapreg_map(pa, WI_PCI_TMD_IORES, PCI_MAPREG_TYPE_IO,
		    0, &iot, &ioh, NULL, NULL, 0) != 0) {
			printf(": can't map I/O space\n");
			return;
		}
		break;
	}

	sc->wi_btag = iot;
	sc->wi_bhandle = ioh;

	/* Enable the card. */
	csr = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	pci_conf_write(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG,
	    csr | PCI_COMMAND_MASTER_ENABLE);

	/* Make sure interrupts are disabled. */
	CSR_WRITE_2(sc, WI_INT_EN, 0);
	CSR_WRITE_2(sc, WI_EVENT_ACK, 0xFFFF);

	/* Map and establish the interrupt. */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, wi_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	switch (pp->pp_type) {
	case WI_PCI_PLX:
		/*
		 * Tell the PLX chip to enable interrupts.  In most cases
		 * the serial EEPROM has done this for us but some cards
		 * appear not to.
		 */
		command = bus_space_read_4(localt, localh, WI_PLX_INTCSR);
		command |= WI_PLX_INTEN;
		bus_space_write_4(localt, localh, WI_PLX_INTCSR, command);

		/*
		 * Setup the PLX chip for level interrupts and config index 1
		 */
		bus_space_write_1(memt, memh,
		    WI_PLX_COR_OFFSET, WI_PLX_COR_VALUE);

		/* Unmap registers we no longer need access to. */
		bus_space_unmap(localt, localh, localsize);
		bus_space_unmap(memt, memh, memsize);

		wi_attach(sc, 1);
		break;
	case WI_PCI_PRISM:
		bus_space_write_2(iot, ioh, WI_PCI_COR, WI_PCI_SOFT_RESET);
		DELAY(100*1000); /* 100 m sec */
		bus_space_write_2(iot, ioh, WI_PCI_COR, 0x0);
		DELAY(100*1000); /* 100 m sec */

		wi_attach(sc, 0);
		break;
	case WI_PCI_TMD:
		bus_space_write_1(localt, localh, 0, WI_TMD_COR_VALUE);
		/* XXX - correct delay? */
		DELAY(100*1000); /* 100 m sec */
		if (bus_space_read_1(localt, localh, 0) != WI_TMD_COR_VALUE)
			printf(": unable to initialize TMD7160 ");

		/* Unmap registers we no longer need access to. */
		bus_space_unmap(localt, localh, localsize);

		wi_attach(sc, 1);
		break;
	}
}
