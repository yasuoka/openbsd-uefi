/*	$OpenBSD: macobio.c,v 1.7 2002/09/15 09:01:58 deraadt Exp $	*/
/*	$NetBSD: obio.c,v 1.6 1999/05/01 10:36:08 tsubai Exp $	*/

/*-
 * Copyright (C) 1998	Internet Research Institute, Inc.
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
 *	This product includes software developed by
 *	Internet Research Institute, Inc.
 * 4. The name of the author may not be used to endorse or promote products
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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

void macobio_attach(struct device *, struct device *, void *);
int macobio_match(struct device *, void *, void *);
int macobio_print(void *, const char *);
void *undef_mac_establish(void * lcv, int irq, int type, int level,
    int (*ih_fun)(void *), void *ih_arg, char *name);
void mac_intr_disestab(void *lcp, void *arg);

struct macobio_softc {
	struct device sc_dev;
	int sc_node;
	struct ppc_bus_space sc_membus_space;
	int	sc_id; /* copy of the PCI pa_id */
	u_int8_t *obiomem;
};
struct cfdriver macobio_cd = {
	NULL, "macobio", DV_DULL,
};


struct cfattach macobio_ca = {
	sizeof(struct macobio_softc), macobio_match, macobio_attach
};

int
macobio_match(parent, cf, aux)
	struct device *parent;
	void *cf;
	void *aux;
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_APPLE)
		switch (PCI_PRODUCT(pa->pa_id)) {

		case PCI_PRODUCT_APPLE_GC:
		case PCI_PRODUCT_APPLE_OHARE:
		case PCI_PRODUCT_APPLE_HEATHROW:
		case PCI_PRODUCT_APPLE_PADDINGTON:
		case PCI_PRODUCT_APPLE_KEYLARGO:
		case PCI_PRODUCT_APPLE_PANGEA_MACIO:
			return 1;
		}

	return 0;
}

#define HEATHROW_FCR_OFFSET 0x38
u_int32_t *heathrow_FCR = NULL;

/*
 * Attach all the sub-devices we can find
 */
void
macobio_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct macobio_softc *sc = (struct macobio_softc *)self;
	struct pci_attach_args *pa = aux;
	struct confargs ca;
	int node, child, namelen;
	u_int32_t reg[20];
	int32_t intr[8];
	char name[32];
	int need_interrupt_controller = 0;

	sc->sc_id = pa->pa_id; /* save of type for later */

	switch (PCI_PRODUCT(pa->pa_id)) {

	/* XXX should not use name */
	case PCI_PRODUCT_APPLE_GC:
		node = OF_finddevice("/bandit/gc");
		need_interrupt_controller = 1;
		break;

	case PCI_PRODUCT_APPLE_OHARE:
		node = OF_finddevice("/bandit/ohare");
		need_interrupt_controller = 1;
		break;

	case PCI_PRODUCT_APPLE_HEATHROW:
	case PCI_PRODUCT_APPLE_PADDINGTON:
		node = OF_finddevice("mac-io");
		if (node == -1)
			node = OF_finddevice("/pci/mac-io");
		if (OF_getprop(node, "assigned-addresses", reg, sizeof(reg))
			== (sizeof (reg[0]) * 5))
		{
			/* always ??? */
			heathrow_FCR = mapiodev(reg[2] + HEATHROW_FCR_OFFSET,
				4);
		}
		break;
	case PCI_PRODUCT_APPLE_KEYLARGO:
	case PCI_PRODUCT_APPLE_PANGEA_MACIO:
		node = OF_finddevice("mac-io");
		if (node == -1)
			node = OF_finddevice("/pci/mac-io");
		if (OF_getprop(node, "assigned-addresses", reg, sizeof(reg))
			== (sizeof (reg[0]) * 5))
		{
			 sc->obiomem = mapiodev(reg[2], 0x100);
		}

		break;
	default:
		printf(": unknown macobio controller\n");
		return;
	}
	sc->sc_node = node;

	if (OF_getprop(node, "assigned-addresses", reg, sizeof(reg)) < 12)
		return;

	ca.ca_baseaddr = reg[2];

	sc->sc_membus_space.bus_base = ca.ca_baseaddr;

	sc->sc_membus_space.bus_reverse = 1;

	ca.ca_iot = &sc->sc_membus_space;
	ca.ca_dmat = pa->pa_dmat;

	printf("\n");

	/*
	 * This might be a hack, but it makes the interrupt controller
	 * attach as expected if a device node existed in the OF tree.
	 */
	if (need_interrupt_controller) {
		/* force attachment of legacy interrupt controllers */
		ca.ca_name = "legacy-interrupt-controller";
		ca.ca_node = 0;

		ca.ca_nreg  = 0;
		ca.ca_nintr = 0;

		ca.ca_reg = 0;
		ca.ca_intr = 0;

		config_found(self, &ca, macobio_print);
	}

	for (child = OF_child(node); child; child = OF_peer(child)) {
		namelen = OF_getprop(child, "name", name, sizeof(name));
		if (namelen < 0)
			continue;
		if (namelen >= sizeof(name))
			continue;

		name[namelen] = 0;
		ca.ca_name = name;
		ca.ca_node = child;

		ca.ca_nreg  = OF_getprop(child, "reg", reg, sizeof(reg));
		ca.ca_nintr = OF_getprop(child, "AAPL,interrupts", intr,
				sizeof(intr));
		if (ca.ca_nintr == -1)
			ca.ca_nintr = OF_getprop(child, "interrupts", intr,
					sizeof(intr));

		ca.ca_reg = reg;
		ca.ca_intr = intr;

		config_found(self, &ca, macobio_print);
	}
}

int
macobio_print(aux, macobio)
	void *aux;
	const char *macobio;
{
#ifdef MACOBIOVERBOSE
	struct confargs *ca = aux;

	if (macobio)
		printf("%s at %s", ca->ca_name, macobio);

	if (ca->ca_nreg > 0)
		printf(" offset 0x%x", ca->ca_reg[0]);

	return UNCONF;
#else
	return QUIET;
#endif
}

void *
undef_mac_establish(lcv, irq, type, level, ih_fun, ih_arg, name)
	void * lcv;
	int irq;
	int type;
	int level;
	int (*ih_fun)(void *);
	void *ih_arg;
	char *name;
{
	printf("mac_intr_establish called, not yet inited\n");
	return 0;
}

void
mac_intr_disestab(lcp, arg)
	void *lcp;
	void *arg;
{
	printf("mac_intr_disestablish called, not yet inited\n");
}

intr_establish_t *mac_intr_establish_func = undef_mac_establish;
intr_disestablish_t *mac_intr_disestablish_func = mac_intr_disestab;

void *
mac_intr_establish(lcv, irq, type, level, ih_fun, ih_arg, name)
	void * lcv;
	int irq;
	int type;
	int level;
	int (*ih_fun)(void *);
	void *ih_arg;
	char *name;
{
	return (*mac_intr_establish_func)(lcv, irq, type, level, ih_fun,
		ih_arg, name);
}
void
mac_intr_disestablish(lcp, arg)
	void *lcp;
	void *arg;
{
	(*mac_intr_disestablish_func)(lcp, arg);
}

void macobio_modem_power(int enable);
void
macobio_modem_power(int enable)
{
	u_int32_t val;
	struct macobio_softc *sc = macobio_cd.cd_devs[0];
	if (PCI_PRODUCT(sc->sc_id) == PCI_PRODUCT_APPLE_KEYLARGO) {
		val = in32rb(sc->obiomem + 0x40);
		if (enable)
			val = val & ~((u_int32_t)1<<25);
		else 
			val = val | ((u_int32_t)1<<25);
		out32rb(sc->obiomem + 0x40, val);
	}
	if (PCI_PRODUCT(sc->sc_id) == PCI_PRODUCT_APPLE_PANGEA_MACIO) {
		if (enable) {
			/* set reset */
			out8(sc->obiomem + 0x006a + 0x03, 0x04);
			/* power modem on */
			out8(sc->obiomem + 0x006a + 0x02, 0x04);
			/* unset reset */
			out8(sc->obiomem + 0x006a + 0x03, 0x05);
		}  else {
			/* disable it how? */
		}
	}
}
