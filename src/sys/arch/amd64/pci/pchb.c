/*	$OpenBSD: pchb.c,v 1.12 2007/11/25 17:11:12 oga Exp $	*/
/*	$NetBSD: pchb.c,v 1.1 2003/04/26 18:39:50 fvdl Exp $	*/
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
/*-
 * Copyright (c) 1996, 1998, 2000 The NetBSD Foundation, Inc.
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
#include <sys/timeout.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/agpvar.h>

#include <dev/rndvar.h>

#include <dev/ic/i82802reg.h>

#define PCISET_BRIDGETYPE_MASK	0x3
#define PCISET_TYPE_COMPAT	0x1
#define PCISET_TYPE_AUX		0x2

#define PCISET_BUSCONFIG_REG	0x48
#define PCISET_BRIDGE_NUMBER(reg)	(((reg) >> 8) & 0xff)
#define PCISET_PCI_BUS_NUMBER(reg)	(((reg) >> 16) & 0xff)

/* XXX should be in dev/ic/i82443reg.h */
#define	I82443BX_SDRAMC_REG	0x76

/* XXX should be in dev/ic/i82424{reg.var}.h */
#define I82424_CPU_BCTL_REG		0x53
#define I82424_PCI_BCTL_REG		0x54

#define I82424_BCTL_CPUMEM_POSTEN	0x01
#define I82424_BCTL_CPUPCI_POSTEN	0x02
#define I82424_BCTL_PCIMEM_BURSTEN	0x01
#define I82424_BCTL_PCI_BURSTEN		0x02

/* XXX should be in dev/ic/amd64htreg.h */
#define AMD64HT_LDT0_BUS	0x94
#define AMD64HT_LDT0_TYPE	0x98
#define AMD64HT_LDT1_BUS	0xb4
#define AMD64HT_LDT1_TYPE	0xb8
#define AMD64HT_LDT2_BUS	0xd4
#define AMD64HT_LDT2_TYPE	0xd8

#define AMD64HT_NUM_LDT		3

#define AMD64HT_LDT_TYPE_MASK		0x0000001f
#define  AMD64HT_LDT_INIT_COMPLETE	0x00000002
#define  AMD64HT_LDT_NC			0x00000004

#define AMD64HT_LDT_SEC_BUS_NUM(reg)	(((reg) >> 8) & 0xff)

struct pchb_softc {
	struct device sc_dev;

	bus_space_tag_t sc_bt;
	bus_space_handle_t sc_bh;

	int sc_rnd_i;
	u_int32_t sc_rnd_ax;
	struct timeout sc_rnd_to;
};

int	pchbmatch(struct device *, void *, void *);
void	pchbattach(struct device *, struct device *, void *);

int	pchb_print(void *, const char *);
int	agpbus_print(void *, const char *);
void	pchb_rnd(void *);
void	pchb_amd64ht_attach (struct device *, struct pci_attach_args *, int);

struct cfattach pchb_ca = {
	sizeof(struct pchb_softc), pchbmatch, pchbattach,
};

struct cfdriver pchb_cd = {
	NULL, "pchb", DV_DULL
};

int
pchbmatch(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_HOST) {
		return (1);
	}

	return (0);
}

void
pchbattach(struct device *parent, struct device *self, void *aux)
{
	struct pchb_softc *sc = (struct pchb_softc *)self;
	struct pci_attach_args *pa = aux;
	struct agpbus_attach_args apa;
	int has_agp, i, r;

	has_agp = 0;

	switch (PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_AMD:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_AMD_AMD64_HT:
			for (i = 0; i < AMD64HT_NUM_LDT; i++)
				pchb_amd64ht_attach(self, pa, i);
			break;
		}
		break;
	case PCI_VENDOR_INTEL:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_INTEL_82915G_HB:
		case PCI_PRODUCT_INTEL_82925X_HB:
		case PCI_PRODUCT_INTEL_82945GP_MCH:
		case PCI_PRODUCT_INTEL_82955X_HB:
			sc->sc_bt = pa->pa_memt;
			if (bus_space_map(sc->sc_bt, I82802_IOBASE,
			    I82802_IOSIZE, 0, &sc->sc_bh)) {
				break;
			}

			/* probe and init rng */
			if (!(bus_space_read_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_HWST) & I82802_RNG_HWST_PRESENT)) {
				break;
			}

			/* enable RNG */
			bus_space_write_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_HWST,
			    bus_space_read_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_HWST) | I82802_RNG_HWST_ENABLE);

			/* see if we can read anything */
			for (i = 1000; i-- &&
			    !(bus_space_read_1(sc->sc_bt,sc->sc_bh,
			    I82802_RNG_RNGST) & I82802_RNG_RNGST_DATAV);)
				DELAY(10);

			if (!(bus_space_read_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_RNGST) & I82802_RNG_RNGST_DATAV)) {
				break;
			}

			r = bus_space_read_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_DATA);

			timeout_set(&sc->sc_rnd_to, pchb_rnd, sc);
			sc->sc_rnd_i = 4;
			pchb_rnd(sc);
			break;
		}
		break;
	}
	printf("\n");

	/*
	 * If we haven't detected AGP yet (via a product ID),
	 * then check for AGP capability on the device.
	 */
	if (has_agp ||
	    pci_get_capability(pa->pa_pc, pa->pa_tag, PCI_CAP_AGP,
			       NULL, NULL) != 0) {
		printf("\n");
		apa.apa_busname = "agp";
		apa.apa_pci_args = *pa;
		config_found(self, &apa, agpbus_print);
		printf("\n");
	}
}

int
pchb_print(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}

int
agpbus_print(void *vaa, const char *pnp)
{
	if (pnp)
		printf("agp at %s", pnp);
	return (UNCONF);
}

void
pchb_amd64ht_attach(struct device *self, struct pci_attach_args *pa, int i)
{
	struct pcibus_attach_args pba;
	pcireg_t type, bus;
	int reg;

	reg = AMD64HT_LDT0_TYPE + i * 0x20;
	type = pci_conf_read(pa->pa_pc, pa->pa_tag, reg);
	if ((type & AMD64HT_LDT_INIT_COMPLETE) == 0 ||
	    (type & AMD64HT_LDT_NC) == 0)
		return;

	reg = AMD64HT_LDT0_BUS + i * 0x20;
	bus = pci_conf_read(pa->pa_pc, pa->pa_tag, reg);
	if (AMD64HT_LDT_SEC_BUS_NUM(bus) > 0) {
		pba.pba_busname = "pci";
		pba.pba_iot = pa->pa_iot;
		pba.pba_memt = pa->pa_memt;
		pba.pba_dmat = pa->pa_dmat;
		pba.pba_bus = AMD64HT_LDT_SEC_BUS_NUM(bus);
		pba.pba_bridgetag = NULL;
		pba.pba_pc = pa->pa_pc;
		config_found(self, &pba, pchb_print);
	}
}

/*
 * Should do FIPS testing as per:
 *	http://csrc.nist.gov/publications/fips/fips140-1/fips1401.pdf
 */
void
pchb_rnd(void *v)
{
	struct pchb_softc *sc = v;

	/*
	 * Don't wait for data to be ready. If it's not there, we'll check
	 * next time.
	 */
	if ((bus_space_read_1(sc->sc_bt, sc->sc_bh, I82802_RNG_RNGST) &
	    I82802_RNG_RNGST_DATAV)) {

		sc->sc_rnd_ax = (sc->sc_rnd_ax << 8) |
		    bus_space_read_1(sc->sc_bt, sc->sc_bh, I82802_RNG_DATA);

		if (!sc->sc_rnd_i--) {
			sc->sc_rnd_i = 4;
			add_true_randomness(sc->sc_rnd_ax);
		}
	}

	timeout_add(&sc->sc_rnd_to, 1);
}
