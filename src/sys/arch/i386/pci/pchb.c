/*	$OpenBSD: pchb.c,v 1.12 2000/05/01 19:34:22 deraadt Exp $	*/
/*	$NetBSD: pchb.c,v 1.6 1997/06/06 23:29:16 thorpej Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/timeout.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/rndvar.h>

#include <dev/ic/i82802reg.h>

#define PCISET_INTEL_BRIDGETYPE_MASK	0x3
#define PCISET_INTEL_TYPE_COMPAT	0x1
#define PCISET_INTEL_TYPE_AUX		0x2

#define PCISET_INTEL_BUSCONFIG_REG	0x48
#define PCISET_INTEL_BRIDGE_NUMBER(reg)	(((reg) >> 8) & 0xff)
#define PCISET_INTEL_PCI_BUS_NUMBER(reg)	(((reg) >> 16) & 0xff)

#define PCISET_INTEL_SDRAMC_REG	0x76
#define PCISET_INTEL_SDRAMC_IPDLT	(1 << 8)  

/* XXX should be in dev/ic/i82424{reg.var}.h */
#define I82424_CPU_BCTL_REG		0x53
#define I82424_PCI_BCTL_REG		0x54

#define I82424_BCTL_CPUMEM_POSTEN	0x01
#define I82424_BCTL_CPUPCI_POSTEN	0x02
#define I82424_BCTL_PCIMEM_BURSTEN	0x01
#define I82424_BCTL_PCI_BURSTEN		0x02

struct pchb_softc {
	struct device sc_dev;

	bus_space_tag_t bt;
	bus_space_handle_t bh;

	/* rng stuff */
	int ax;
	int i;
	struct timeout sc_tmo;
};

int	pchbmatch __P((struct device *, void *, void *));
void	pchbattach __P((struct device *, struct device *, void *));

int	pchb_print __P((void *, const char *));

struct cfattach pchb_ca = {
	sizeof(struct pchb_softc), pchbmatch, pchbattach
};

struct cfdriver pchb_cd = {
	NULL, "pchb", DV_DULL
};

void pchb_rnd __P((void *v));

int
pchbmatch(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct pci_attach_args *pa = aux;

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_HOST)
		return (1);

	return (0);
}

void
pchbattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct pchb_softc *sc = (struct pchb_softc *)self;
	struct pci_attach_args *pa = aux;
	struct pcibus_attach_args pba;
	pcireg_t bcreg;
	u_char bdnum, pbnum;
	int neednl = 1;
	int i;

	/*
	 * Print out a description, and configure certain chipsets which
	 * have auxiliary PCI buses.
	 */

	switch (PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_RCC:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_RCC_ROSB4:
		case PCI_PRODUCT_RCC_CNB20HE:
		case PCI_PRODUCT_RCC_CNB20LE:
		case PCI_PRODUCT_RCC_CMIC_HE:
			bdnum = pci_conf_read(pa->pa_pc, pa->pa_tag,
			    0x44);

			if (bdnum == 0)
				break;
			/*
			 * This host bridge has a second PCI bus.
			 * Configure it.
			 */
			printf(": has pci bus %d\n", bdnum);
			neednl = 0;
			pba.pba_busname = "pci";
			pba.pba_iot = pa->pa_iot;
			pba.pba_memt = pa->pa_memt;
			pba.pba_dmat = pa->pa_dmat;
			pba.pba_bus = bdnum;
			pba.pba_pc = pa->pa_pc;
			config_found(self, &pba, pchb_print);
			break;
		}
		break;
	case PCI_VENDOR_INTEL:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_INTEL_82443BX_AGP:     /* 82443BX AGP (PAC) */
		case PCI_PRODUCT_INTEL_82443BX_NOAGP:   /* 82443BX Host-PCI (no AGP) */
			/*
			 * An incorrect address may be driven on the
			 * DRAM bus, resulting in memory data being
			 * fetched from the wrong location.  This is
			 * the workaround.
			 */
			bcreg = pci_conf_read(pa->pa_pc, pa->pa_tag,
			    PCISET_INTEL_SDRAMC_REG);
			bcreg |= PCISET_INTEL_SDRAMC_IPDLT;
			pci_conf_write(pa->pa_pc, pa->pa_tag,
			    PCISET_INTEL_SDRAMC_REG, bcreg);
			break;
		case PCI_PRODUCT_INTEL_PCI450_PB:
			bcreg = pci_conf_read(pa->pa_pc, pa->pa_tag,
			    PCISET_INTEL_BUSCONFIG_REG);
			bdnum = PCISET_INTEL_BRIDGE_NUMBER(bcreg);
			pbnum = PCISET_INTEL_PCI_BUS_NUMBER(bcreg);
			switch (bdnum & PCISET_INTEL_BRIDGETYPE_MASK) {
			default:
				printf(": bdnum=%x (reserved)", bdnum);
				break;
			case PCISET_INTEL_TYPE_COMPAT:
				printf(": Compatibility PB (bus %d)", pbnum);
				break;
			case PCISET_INTEL_TYPE_AUX:
				printf(": Auxiliary PB (bus %d)\n", pbnum);
				neednl = 0;

				/*
				 * This host bridge has a second PCI bus.
				 * Configure it.
				 */
				pba.pba_busname = "pci";
				pba.pba_iot = pa->pa_iot;
				pba.pba_memt = pa->pa_memt;
				pba.pba_dmat = pa->pa_dmat;
				pba.pba_bus = pbnum;
				pba.pba_pc = pa->pa_pc;
				config_found(self, &pba, pchb_print);
				break;
			}
			break;
		case PCI_PRODUCT_INTEL_CDC:
			bcreg = pci_conf_read(pa->pa_pc, pa->pa_tag,
			    I82424_CPU_BCTL_REG);
			if (bcreg & I82424_BCTL_CPUPCI_POSTEN) {
				bcreg &= ~I82424_BCTL_CPUPCI_POSTEN;
				pci_conf_write(pa->pa_pc, pa->pa_tag,
				    I82424_CPU_BCTL_REG, bcreg);
				printf(": disabled CPU-PCI write posting");
			}
			break;
		case PCI_PRODUCT_INTEL_82810E_MCH:
			sc->bt = pa->pa_memt;
			if (bus_space_map(sc->bt, I82802_IOBASE, I82802_IOSIZE,
			    0, &sc->bh) < 0)
				break;

			/* probe and init rng */
			if (bus_space_read_1(sc->bt, sc->bh,
			    I82802_RNG_HWST) & I82802_RNG_HWST_PRESENT) {
				int r;

				/* enable RNG */
				bus_space_write_1(sc->bt, sc->bh,
						I82802_RNG_HWST,
				    bus_space_read_1(sc->bt, sc->bh,
						I82802_RNG_HWST) |
				    I82802_RNG_HWST_ENABLE);

				/*
				 * see if we can read anything,
				 * and it passed the test
				 */
				for (i = 1000; i-- &&
		    !(bus_space_read_1(sc->bt, sc->bh, I82802_RNG_RNGST) &
		      I82802_RNG_RNGST_DATAV); DELAY(10));

				if (bus_space_read_1(sc->bt, sc->bh,
				    I82802_RNG_RNGST) & I82802_RNG_RNGST_DATAV
				    && (r = bus_space_read_1(sc->bt, sc->bh,
					I82802_RNG_DATA)) != 0
				    /*&& runfipstest()>=0*/) {
					printf (": RNG(%x)", r);

					sc->i = 4;
					timeout_set(&sc->sc_tmo, pchb_rnd, sc);
					timeout_add(&sc->sc_tmo, 1);
				}
			}
			break;
		default:
			break;
		}
	}
	if (neednl)
		printf("\n");
}

int
pchb_print(aux, pnp)
	void *aux;
	const char *pnp;
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}


void
pchb_rnd(v)
	void *v;
{
	struct pchb_softc *sc = v;
	int s, ret = -1;

	s = splhigh();
	if (bus_space_read_1(sc->bt, sc->bh, I82802_RNG_RNGST) &
	    I82802_RNG_RNGST_DATAV)
		ret = bus_space_read_1(sc->bt, sc->bh, I82802_RNG_DATA);
	splx(s);

	if (ret >= 0) {
		if (sc->i--)
			sc->ax = (sc->ax << 8) + ret;
		else {
			sc->i = 4;
			add_true_randomness(sc->ax);
		}
	}
	timeout_add(&sc->sc_tmo, 1);
}
