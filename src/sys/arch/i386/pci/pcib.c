/*	$OpenBSD: pcib.c,v 1.11 2004/02/19 21:35:56 grange Exp $	*/
/*	$NetBSD: pcib.c,v 1.6 1997/06/06 23:29:16 thorpej Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <dev/isa/isavar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/pci/pcidevs.h>

#include <dev/pci/ichreg.h>

#include "isa.h"
#include "pcibios.h"
#if NPCIBIOS > 0
#include <i386/pci/pcibiosvar.h>
#endif

int	pcibmatch(struct device *, void *, void *);
void	pcibattach(struct device *, struct device *, void *);
void	pcib_callback(struct device *);
int	pcib_print(void *, const char *);

int	ichss_match(void *);
int	ichss_attach(struct device *, void *);
int	ichss_setperf(int);

struct pcib_softc {
	struct device sc_dev;

	/* For power management capable bridges */
	bus_space_tag_t sc_pmt;
	bus_space_handle_t sc_pmh;
};

struct cfattach pcib_ca = {
	sizeof(struct pcib_softc), pcibmatch, pcibattach
};

struct cfdriver pcib_cd = {
	NULL, "pcib", DV_DULL
};

int
pcibmatch(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct pci_attach_args *pa = aux;

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_ISA)
		return (1);

	switch (PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_INTEL:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_INTEL_SIO:
		case PCI_PRODUCT_INTEL_82371MX:
		case PCI_PRODUCT_INTEL_82371AB_ISA:
		case PCI_PRODUCT_INTEL_82440MX_ISA:
			/* The above bridges mis-identify themselves */
			return (1);
		}
	case PCI_VENDOR_SIS:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_SIS_85C503:
			/* mis-identifies itself as a miscellaneous prehistoric */
			return (1);
		}
	}

	return (0);
}

void
pcibattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
#ifndef SMALL_KERNEL
	/*
	 * Detect and activate SpeedStep on ICHx-M chipsets.
	 */
	if (ichss_match(aux) && ichss_attach(self, aux) == 0)
		printf(": SpeedStep");
#endif

	/*
	 * Cannot attach isa bus now; must postpone for various reasons
	 */
	printf("\n");

	config_defer(self, pcib_callback);
}

void
pcib_callback(self)
	struct device *self;
{
	struct isabus_attach_args iba;

#if NPCIBIOS > 0
	pci_intr_post_fixup();
#endif

	/*
	 * Attach the ISA bus behind this bridge.
	 */
	memset(&iba, 0, sizeof(iba));
	iba.iba_busname = "isa";
	iba.iba_iot = I386_BUS_SPACE_IO;
	iba.iba_memt = I386_BUS_SPACE_MEM;
#if NISADMA > 0
	iba.iba_dmat = &isa_bus_dma_tag;
#endif
	config_found(self, &iba, pcib_print);
}

int
pcib_print(aux, pnp)
	void *aux;
	const char *pnp;
{
	/* Only ISAs can attach to pcib's; easy. */
	if (pnp)
		printf("isa at %s", pnp);
	return (UNCONF);
}

#ifndef SMALL_KERNEL
static void *ichss_cookie;	/* XXX */

int
ichss_match(void *aux)
{
	struct pci_attach_args *pa = aux;
	pcitag_t br_tag;
	pcireg_t br_id, br_class;

	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801DBM_LPC ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801CAM_LPC)
		return (1);
	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801BAM_LPC) {
		/*
		 * Old revisions of the 82815 hostbridge found on
		 * Dell Inspirons 8000 and 8100 don't support
		 * SpeedStep.
		 */
		/* XXX: dev 0 func 0 is not always a hostbridge */
		br_tag = pci_make_tag(pa->pa_pc, pa->pa_bus, 0, 0);
		br_id = pci_conf_read(pa->pa_pc, br_tag, PCI_ID_REG);
		br_class = pci_conf_read(pa->pa_pc, br_tag, PCI_CLASS_REG);

		if (PCI_PRODUCT(br_id) == PCI_PRODUCT_INTEL_82815_FULL_HUB &&
		    PCI_REVISION(br_class) < 5)
			return (0);
		return (1);
	}

	return (0);
}

int
ichss_attach(struct device *self, void *aux)
{
	struct pcib_softc *sc = (struct pcib_softc *)self;
	struct pci_attach_args *pa = aux;
	pcireg_t pmbase;

	/* Map power management I/O space */
	sc->sc_pmt = pa->pa_iot;
	pmbase = pci_conf_read(pa->pa_pc, pa->pa_tag, ICH_PMBASE);
	if (bus_space_map(sc->sc_pmt, PCI_MAPREG_IO_ADDR(pmbase),
	    ICH_PMSIZE, 0, &sc->sc_pmh) != 0)
		return (1);

	/* Enable SpeedStep */
	pci_conf_write(pa->pa_pc, pa->pa_tag, ICH_GEN_PMCON1,
	    pci_conf_read(pa->pa_pc, pa->pa_tag, ICH_GEN_PMCON1) |
	    ICH_GEN_PMCON1_SS_EN);

	/* Hook into hw.setperf sysctl */
	ichss_cookie = sc;
	cpu_setperf = ichss_setperf;

	return (0);
}

int
ichss_setperf(int level)
{
	struct pcib_softc *sc = ichss_cookie;
	u_int8_t state, ostate, cntl;
	int s;

#ifdef DIAGNOSTIC
	if (sc == NULL) {
		printf("%s: no cookie", __func__);
		return (EFAULT);
	}
#endif

	s = splhigh();
	state = bus_space_read_1(sc->sc_pmt, sc->sc_pmh, ICH_PM_SS_CNTL);
	ostate = state;

	/* Only two states are available */
	if (level < 50)
		state |= ICH_PM_SS_STATE_LOW;
	else
		state &= ~ICH_PM_SS_STATE_LOW;

	/*
	 * An Intel SpeedStep technology transition _always_ occur on
	 * writes to the ICH_PM_SS_CNTL register, even if the value
	 * written is the same as the previous value. So do the write
	 * only if the state has changed.
	 */
	if (state != ostate) {
		/* Disable bus mastering arbitration */
		cntl = bus_space_read_1(sc->sc_pmt, sc->sc_pmh, ICH_PM_CNTL);
		bus_space_write_1(sc->sc_pmt, sc->sc_pmh, ICH_PM_CNTL,
		    cntl | ICH_PM_ARB_DIS);

		/* Do the transition */
		bus_space_write_1(sc->sc_pmt, sc->sc_pmh, ICH_PM_SS_CNTL,
		    state);

		/* Restore bus mastering arbitration state */
		bus_space_write_1(sc->sc_pmt, sc->sc_pmh, ICH_PM_CNTL,
		    cntl);
	}
	splx(s);

	return (0);
}
#endif	/* !SMALL_KERNEL */
