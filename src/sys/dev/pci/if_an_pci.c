/*	$OpenBSD: if_an_pci.c,v 1.4 2001/06/23 01:44:30 mickey Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999
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
 * $FreeBSD: src/sys/dev/an/if_an_pci.c,v 1.1 2000/01/14 20:40:56 wpaul Exp $
 */

/*
 * This is a PCI shim for the Aironet PC4500/4800 wireless network
 * driver. Aironet makes PCMCIA, ISA and PCI versions of these devices,
 * which all have basically the same interface. The ISA and PCI cards
 * are actually bridge adapters with PCMCIA cards inserted into them,
 * however they appear as normal PCI or ISA devices to the host.
 *
 * All we do here is handle the PCI match and attach and set up an
 * interrupt handler entry point. The PCI version of the card uses
 * a PLX 9050 PCI to "dumb bus" bridge chip, which provides us with
 * multiple PCI address space mappings. The primary mapping at PCI
 * register 0x14 is for the PLX chip itself, *NOT* the Aironet card.
 * The I/O address of the Aironet is actually at register 0x18, which
 * is the local bus mapping register for bus space 0. There are also
 * registers for additional register spaces at registers 0x1C and
 * 0x20, but these are unused in the Aironet devices. To find out
 * more, you need a datasheet for the 9050 from PLX, but you have
 * to go through their sales office to get it. Bleh.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_dl.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/anvar.h>
#include <dev/ic/anreg.h>

#define AN_PCI_PLX_LOIO		0x14	/* PLX chip iobase */
#define AN_PCI_LOIO		0x18	/* Aironet iobase */

int an_pci_match	__P((struct device *, void *, void *));
void an_pci_attach	__P((struct device *, struct device *, void *));

struct cfattach an_pci_ca = {
	sizeof (struct an_softc), an_pci_match, an_pci_attach
};

int
an_pci_match(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_AIRONET &&
	    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AIRONET_PCI352 ||
	     PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AIRONET_PC4500 ||
	     PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AIRONET_PC4800))
		return(1);

	return(0);
}

void
an_pci_attach(parent, self, aux)
	struct device *parent;
	struct device *self;
	void *aux;
{
	struct an_softc *sc = (struct an_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	bus_space_handle_t ioh;
	bus_space_tag_t iot = pa->pa_iot;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcireg_t csr;
	const char *intrstr;

	/* Map the I/O ports. */
	if (pci_mapreg_map(pa, AN_PCI_LOIO, PCI_MAPREG_TYPE_IO, 0,
	    &iot, &ioh, NULL, NULL, 0) != 0) {
		printf(": can't map I/O space\n");
		return;
	}
	sc->an_btag = iot;
	sc->an_bhandle = ioh;

	/* Enable the card. */
	csr = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	pci_conf_write(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG,
	    csr | PCI_COMMAND_MASTER_ENABLE);

	/* Map and establish the interrupt. */
	if (pci_intr_map(pc, pa->pa_intrtag, pa->pa_intrpin,
	    pa->pa_intrline, &ih)) {
		printf("\n%s: couldn't map interrupt\n", sc->sc_dev.dv_xname);
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, an_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("\n%s: couldn't establish interrupt",
		    sc->sc_dev.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	an_attach(sc);
}
