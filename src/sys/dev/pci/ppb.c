/*	$OpenBSD: ppb.c,v 1.20 2007/11/24 21:33:58 kettenis Exp $	*/
/*	$NetBSD: ppb.c,v 1.16 1997/06/06 23:48:05 thorpej Exp $	*/

/*
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/workq.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/ppbreg.h>

struct ppb_softc {
	struct device sc_dev;		/* generic device glue */
	pci_chipset_tag_t sc_pc;	/* our PCI chipset... */
	pcitag_t sc_tag;		/* ...and tag. */
	pci_intr_handle_t sc_ih[4];
	struct device *sc_psc;
	int sc_cap_off;
	struct timeout sc_to;
};

int	ppbmatch(struct device *, void *, void *);
void	ppbattach(struct device *, struct device *, void *);

struct cfattach ppb_ca = {
	sizeof(struct ppb_softc), ppbmatch, ppbattach
};

struct cfdriver ppb_cd = {
	NULL, "ppb", DV_DULL
};

int	ppb_intr(void *);
void	ppb_hotplug_insert(void *, void *);
void	ppb_hotplug_insert_finish(void *);
void	ppb_hotplug_rescan(void *, void *);
void	ppb_hotplug_remove(void *, void *);
int	ppbprint(void *, const char *pnp);

int
ppbmatch(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	/*
	 * This device is mislabeled.  It is not a PCI bridge.
	 */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_VIATECH &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_VIATECH_VT82C586_PWR)
		return (0);
	/*
	 * Check the ID register to see that it's a PCI bridge.
	 * If it is, we assume that we can deal with it; it _should_
	 * work in a standardized way...
	 */
	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_PCI)
		return (1);

	return (0);
}

void
ppbattach(struct device *parent, struct device *self, void *aux)
{
	struct ppb_softc *sc = (void *) self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	struct pcibus_attach_args pba;
	pci_intr_handle_t ih;
	pcireg_t busdata, reg;
	int pin;

	sc->sc_pc = pc;
	sc->sc_tag = pa->pa_tag;

	busdata = pci_conf_read(pc, pa->pa_tag, PPB_REG_BUSINFO);

	if (PPB_BUSINFO_SECONDARY(busdata) == 0) {
		printf(": not configured by system firmware\n");
		return;
	}

	for (pin = PCI_INTERRUPT_PIN_A; pin <= PCI_INTERRUPT_PIN_D; pin++) {
		pa->pa_intrpin = pa->pa_rawintrpin = pin;
		pa->pa_intrline = 0;
		pci_intr_map(pa, &sc->sc_ih[pin - PCI_INTERRUPT_PIN_A]);
	}

#if 0
	/*
	 * XXX can't do this, because we're not given our bus number
	 * (we shouldn't need it), and because we've no way to
	 * decompose our tag.
	 */
	/* sanity check. */
	if (pa->pa_bus != PPB_BUSINFO_PRIMARY(busdata))
		panic("ppbattach: bus in tag (%d) != bus in reg (%d)",
		    pa->pa_bus, PPB_BUSINFO_PRIMARY(busdata));
#endif

	/* Check for PCI Express capabilities and setup hotplug support. */
	if (pci_get_capability(pc, pa->pa_tag, PCI_CAP_PCIEXPRESS,
	    &sc->sc_cap_off, &reg) && (reg & PCI_PCIE_XCAP_SI)) {
		if (pci_intr_map(pa, &ih) == 0 &&
		    pci_intr_establish(pc, ih, IPL_TTY, ppb_intr, sc,
		    self->dv_xname)) {
			printf(": %s", pci_intr_string(pc, ih));

			/* Enable hotplug interrupt. */
			reg = pci_conf_read(pc, pa->pa_tag,
			    sc->sc_cap_off + PCI_PCIE_SLCSR);
			reg |= (PCI_PCIE_SLCSR_HPE | PCI_PCIE_SLCSR_PDE);
			pci_conf_write(pc, pa->pa_tag,
			    sc->sc_cap_off + PCI_PCIE_SLCSR, reg);

			timeout_set(&sc->sc_to, ppb_hotplug_insert_finish, sc);
		}
	}

	printf("\n");

	/*
	 * Attach the PCI bus that hangs off of it.
	 *
	 * XXX Don't pass-through Memory Read Multiple.  Should we?
	 * XXX Consult the spec...
	 */
	pba.pba_busname = "pci";
	pba.pba_iot = pa->pa_iot;
	pba.pba_memt = pa->pa_memt;
	pba.pba_dmat = pa->pa_dmat;
	pba.pba_pc = pc;
#if 0
	pba.pba_flags = pa->pa_flags & ~PCI_FLAGS_MRM_OKAY;
#endif
	pba.pba_domain = pa->pa_domain;
	pba.pba_bus = PPB_BUSINFO_SECONDARY(busdata);
	pba.pba_bridgeih = sc->sc_ih;
	pba.pba_bridgetag = &sc->sc_tag;
	pba.pba_intrswiz = pa->pa_intrswiz;
	pba.pba_intrtag = pa->pa_intrtag;

	sc->sc_psc = config_found(self, &pba, ppbprint);
}

int
ppb_intr(void *arg)
{
	struct ppb_softc *sc = arg;
	pcireg_t reg;

	reg = pci_conf_read(sc->sc_pc, sc->sc_tag,
	    sc->sc_cap_off + PCI_PCIE_SLCSR);
	if (reg & PCI_PCIE_SLCSR_PDC) {
		if (reg & PCI_PCIE_SLCSR_PDS)
			workq_add_task(NULL, 0, ppb_hotplug_insert, sc, NULL);
		else
			workq_add_task(NULL, 0, ppb_hotplug_remove, sc, NULL);

		/* Clear interrupts. */
		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    sc->sc_cap_off + PCI_PCIE_SLCSR, reg);
		return (1);
	}

	return (0);
}

#ifdef PCI_MACHDEP_ENUMERATE_BUS
#define pci_enumerate_bus PCI_MACHDEP_ENUMERATE_BUS
#else
extern int pci_enumerate_bus(struct pci_softc *,
    int (*)(struct pci_attach_args *), struct pci_attach_args *);
#endif

void
ppb_hotplug_insert(void *arg1, void *arg2)
{
	struct ppb_softc *sc = arg1;

	/* XXX Powerup the card. */

	/* XXX Turn on LEDs. */

	/* Wait a second for things to settle. */
	timeout_add(&sc->sc_to, 1 * hz);
}

void
ppb_hotplug_insert_finish(void *arg)
{
	workq_add_task(NULL, 0, ppb_hotplug_rescan, arg, NULL);
}

void
ppb_hotplug_rescan(void *arg1, void *arg2)
{
	struct ppb_softc *sc = arg1;

	if (sc->sc_psc)
		pci_enumerate_bus((struct pci_softc *)sc->sc_psc, NULL, NULL);
}

void
ppb_hotplug_remove(void *arg1, void *arg2)
{
	struct ppb_softc *sc = arg1;

	if (sc->sc_psc)
		config_detach_children(sc->sc_psc, DETACH_FORCE);
}

int
ppbprint(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	/* only PCIs can attach to PPBs; easy. */
	if (pnp)
		printf("pci at %s", pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}
