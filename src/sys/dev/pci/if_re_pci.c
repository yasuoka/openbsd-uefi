/*	$OpenBSD: if_re_pci.c,v 1.23 2008/10/12 00:54:49 brad Exp $	*/

/*
 * Copyright (c) 2005 Peter Valchev <pvalchev@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * PCI front-end for the Realtek 8169
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/rtl81x9reg.h>
#include <dev/ic/revar.h>

struct re_pci_softc {
	/* General */
	struct rl_softc sc_rl;

	/* PCI-specific data */
	void *sc_ih;
	pci_chipset_tag_t sc_pc;
	pcitag_t sc_pcitag;
};

const struct pci_matchid re_pci_devices[] = {
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8101E },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8168 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8169 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8169SC },
	{ PCI_VENDOR_COREGA, PCI_PRODUCT_COREGA_CGLAPCIGT },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DGE528T },
	{ PCI_VENDOR_USR2, PCI_PRODUCT_USR2_USR997902 },
	{ PCI_VENDOR_TTTECH, PCI_PRODUCT_TTTECH_MC322 }
};

#define RE_LINKSYS_EG1032_SUBID 0x00241737

int	re_pci_probe(struct device *, void *, void *);
void	re_pci_attach(struct device *, struct device *, void *);

/*
 * PCI autoconfig definitions
 */
struct cfattach re_pci_ca = {
	sizeof(struct re_pci_softc),
	re_pci_probe,
	re_pci_attach
};

/*
 * Probe for a Realtek 8169/8110 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
re_pci_probe(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcireg_t subid;

	subid = pci_conf_read(pc, pa->pa_tag, PCI_SUBSYS_ID_REG);

	/* C+ mode 8139's */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_REALTEK &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RT8139 &&
	    PCI_REVISION(pa->pa_class) == 0x20)
		return (1);

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_LINKSYS &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_LINKSYS_EG1032 &&
	    subid == RE_LINKSYS_EG1032_SUBID)
		return (1);

	return (pci_matchbyid((struct pci_attach_args *)aux, re_pci_devices,
	    sizeof(re_pci_devices)/sizeof(re_pci_devices[0])));
}

/*
 * PCI-specific attach routine
 */
void
re_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct re_pci_softc	*psc = (struct re_pci_softc *)self;
	struct rl_softc		*sc = &psc->sc_rl;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	const char		*intrstr = NULL;
	bus_size_t		iosize;
	pcireg_t		command;

	/*
	 * Handle power management nonsense.
	 */

	command = pci_conf_read(pc, pa->pa_tag, RL_PCI_CAPID) & 0x000000FF;

	if (command == 0x01) {
		u_int32_t		iobase, membase, irq;

		/* Save important PCI config data. */
		iobase = pci_conf_read(pc, pa->pa_tag,  RL_PCI_LOIO);
		membase = pci_conf_read(pc, pa->pa_tag, RL_PCI_LOMEM);
		irq = pci_conf_read(pc, pa->pa_tag, RL_PCI_INTLINE);

#if 0
		/* Reset the power state. */
		printf(": chip is in D%d power mode "
		    "-- setting to D0", command & RL_PSTATE_MASK);
#endif
		command &= 0xFFFFFFFC;

		/* Restore PCI config data. */
		pci_conf_write(pc, pa->pa_tag, RL_PCI_LOIO, iobase);
		pci_conf_write(pc, pa->pa_tag, RL_PCI_LOMEM, membase);
		pci_conf_write(pc, pa->pa_tag, RL_PCI_INTLINE, irq);
	}

	/*
	 * Map control/status registers.
	 */
	if (pci_mapreg_map(pa, RL_PCI_LOMEM, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->rl_btag, &sc->rl_bhandle, NULL, &iosize, 0)) {
		if (pci_mapreg_map(pa, RL_PCI_LOIO, PCI_MAPREG_TYPE_IO, 0,
		    &sc->rl_btag, &sc->rl_bhandle, NULL, &iosize, 0)) {
			printf(": can't map mem or i/o space\n");
			return;
		}
	}

	/* Allocate interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	psc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, re_intr, sc,
	    sc->sc_dev.dv_xname);
	if (psc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		return;
	}

	sc->sc_dmat = pa->pa_dmat;

	/*
	 * PCI Express check.
	 */
	if (pci_get_capability(pc, pa->pa_tag, PCI_CAP_PCIEXPRESS,
	    NULL, NULL))
		sc->rl_flags |= RL_FLAG_PCIE;

	/* Call bus-independent attach routine */
	if (re_attach(sc, intrstr)) {
		pci_intr_disestablish(pc, psc->sc_ih);
		bus_space_unmap(sc->rl_btag, sc->rl_bhandle, iosize);
	}
}
