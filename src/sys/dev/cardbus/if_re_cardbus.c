/*	$OpenBSD: if_re_cardbus.c,v 1.9 2006/09/19 07:23:02 mickey Exp $	*/

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
 * Cardbus front-end for the Realtek 8169
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

#include <dev/pci/pcidevs.h>

#include <dev/cardbus/cardbusvar.h>

#include <dev/ic/rtl81x9reg.h>
#include <dev/ic/revar.h>

struct re_cardbus_softc {
	/* General */
	struct rl_softc sc_rl;

	/* Cardbus-specific data */
	void *sc_ih;
	cardbus_devfunc_t ct;
	cardbustag_t sc_tag;
	int sc_csr;
	int sc_cben;
	int sc_bar_reg;
	pcireg_t sc_bar_val;
	int sc_intrline;

	bus_size_t sc_mapsize;
};

int	re_cardbus_probe(struct device *, void *, void *);
void	re_cardbus_attach(struct device *, struct device *, void *);
int	re_cardbus_detach(struct device *, int);
void	re_cardbus_setup(struct rl_softc *);

void	re_cardbus_shutdown(void *);
void	re_cardbus_powerhook(int, void *);

/*
 * Cardbus autoconfig definitions
 */
struct cfattach re_cardbus_ca = {
	sizeof(struct re_cardbus_softc),
	re_cardbus_probe,
	re_cardbus_attach,
	re_cardbus_detach
};

const struct cardbus_matchid re_cardbus_devices[] = {
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8169 },
};

/*
 * Probe for a RealTek 8169/8110 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
re_cardbus_probe(struct device *parent, void *match, void *aux)
{
	return (cardbus_matchbyid((struct cardbus_attach_args *)aux,
	    re_cardbus_devices,
	    sizeof(re_cardbus_devices)/sizeof(re_cardbus_devices[0])));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
re_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct re_cardbus_softc	*csc = (struct re_cardbus_softc *)self;
	struct rl_softc		*sc = &csc->sc_rl;
	struct cardbus_attach_args *ca = aux;
	struct cardbus_softc *psc =
	    (struct cardbus_softc *)sc->sc_dev.dv_parent;
	cardbus_chipset_tag_t cc = psc->sc_cc;
	cardbus_function_tag_t cf = psc->sc_cf;
	cardbus_devfunc_t ct = ca->ca_ct;
	bus_addr_t adr;

	
	sc->sc_dmat = ca->ca_dmat;
	csc->ct = ct;
	csc->sc_tag = ca->ca_tag;
	csc->sc_intrline = ca->ca_intrline;

	/*
	 * Map control/status registers.
	 */
	if (Cardbus_mapreg_map(ct, RL_PCI_LOMEM, CARDBUS_MAPREG_TYPE_MEM, 0,
	    &sc->rl_btag, &sc->rl_bhandle, &adr, &csc->sc_mapsize) == 0) {
		csc->sc_cben = CARDBUS_MEM_ENABLE;
		csc->sc_csr |= CARDBUS_COMMAND_MEM_ENABLE;
		csc->sc_bar_reg = RL_PCI_LOMEM;
		csc->sc_bar_val = adr | CARDBUS_MAPREG_TYPE_MEM;
	} else {
		printf(": can't map mem space\n");
		return;
	}

	/* Enable power */
	Cardbus_function_enable(ct);

	/* Get chip out of powersave mode (if applicable), initialize
	 * config registers */
	re_cardbus_setup(sc);

	/* Allocate interrupt */
	csc->sc_ih = cardbus_intr_establish(cc, cf, csc->sc_intrline,
	    IPL_NET, re_intr, sc);
	if (csc->sc_ih == NULL) {
		printf(": couldn't establish interrupt at %s",
		    ca->ca_intrline);
		Cardbus_function_disable(csc->ct);
		return;
	}
	printf(": irq %d", ca->ca_intrline);

	sc->sc_flags |= RL_ENABLED;
	sc->rl_type = RL_8169;

	sc->sc_sdhook = shutdownhook_establish(re_cardbus_shutdown, sc);
	sc->sc_pwrhook = powerhook_establish(re_cardbus_powerhook, sc);

	/* Call bus-independent (common) attach routine */
	re_attach(sc);
}

/*
 * Get chip out of power-saving mode, init registers
 */
void
re_cardbus_setup(struct rl_softc *sc)
{
	struct re_cardbus_softc *csc = (struct re_cardbus_softc *)sc;
	cardbus_devfunc_t ct = csc->ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;
	pcireg_t reg, command;
	int pmreg;

	/* Handle power management nonsense */
	if (cardbus_get_capability(cc, cf, csc->sc_tag,
	    PCI_CAP_PWRMGMT, &pmreg, 0)) {
		command = cardbus_conf_read(cc, cf, csc->sc_tag,
		    pmreg + PCI_PMCSR);

		if (command & RL_PSTATE_MASK) {
			pcireg_t iobase, membase, irq;

			/* Save important PCI config data */
			iobase = cardbus_conf_read(cc, cf, csc->sc_tag, RL_PCI_LOIO);
			membase = cardbus_conf_read(cc, cf, csc->sc_tag, RL_PCI_LOMEM);
			irq = cardbus_conf_read(cc, cf, csc->sc_tag, RL_PCI_INTLINE);

			/* Reset the power state */
			printf("%s: chip is in D%d power mode "
			    "-- setting to D0\n", sc->sc_dev.dv_xname,
			    command & RL_PSTATE_MASK);
			command &= RL_PSTATE_MASK;
			cardbus_conf_write(cc, cf, csc->sc_tag, pmreg + PCI_PMCSR,
			    command);

			/* Restore PCI config data */
			cardbus_conf_write(cc, cf, csc->sc_tag, RL_PCI_LOIO, iobase);
			cardbus_conf_write(cc, cf, csc->sc_tag, RL_PCI_LOMEM, membase);
			cardbus_conf_write(cc, cf, csc->sc_tag, RL_PCI_INTLINE, irq);
		}
	}

	/* Make sure the right access type is on the Cardbus bridge */
	(*ct->ct_cf->cardbus_ctrl)(cc, csc->sc_cben);
	(*ct->ct_cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	/* Program the BAR */
	cardbus_conf_write(cc, cf, csc->sc_tag, csc->sc_bar_reg, csc->sc_bar_val);

	/* Enable proper bits in CARDBUS CSR */
	reg = cardbus_conf_read(cc, cf, csc->sc_tag, CARDBUS_COMMAND_STATUS_REG);
	reg &= ~(CARDBUS_COMMAND_IO_ENABLE|CARDBUS_COMMAND_MEM_ENABLE);
	reg |= csc->sc_csr;
	cardbus_conf_write(cc, cf, csc->sc_tag, CARDBUS_COMMAND_STATUS_REG, reg);

	/* Make sure the latency timer is set to some reasonable value */
	reg = cardbus_conf_read(cc, cf, csc->sc_tag, CARDBUS_BHLC_REG);
	if (CARDBUS_LATTIMER(reg) < 0x20) {
		reg &= ~(CARDBUS_LATTIMER_MASK << CARDBUS_LATTIMER_SHIFT);
		reg |= (0x20 << CARDBUS_LATTIMER_SHIFT);
		cardbus_conf_write(cc, cf, csc->sc_tag, CARDBUS_BHLC_REG, reg);
	}
}

/*
 * Cardbus detach function: deallocate all resources
 */
int
re_cardbus_detach(struct device *self, int flags)
{
	struct re_cardbus_softc *csc = (void *)self;
	struct rl_softc *sc = &csc->sc_rl;
	struct cardbus_devfunc *ct = csc->ct;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	/* Remove timeout handler */
	timeout_del(&sc->timer_handle);

	/* Detach PHY */
	if (LIST_FIRST(&sc->sc_mii.mii_phys) != NULL)
		mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete media stuff */
	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);
	ether_ifdetach(ifp);
	if_detach(ifp);

	/* No more hooks */
	if (sc->sc_sdhook != NULL)
		shutdownhook_disestablish(sc->sc_sdhook);
	if (sc->sc_pwrhook != NULL)
		powerhook_disestablish(sc->sc_pwrhook);

	/* Disable interrupts */
	if (csc->sc_ih != NULL)
		cardbus_intr_disestablish(ct->ct_cc, ct->ct_cf, csc->sc_ih);

	/* Free cardbus resources */
	Cardbus_mapreg_unmap(ct, csc->sc_bar_reg, sc->rl_btag, sc->rl_bhandle,
	    csc->sc_mapsize);

	return (0);
}

void
re_cardbus_shutdown(void *arg)
{
	struct rl_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	re_stop(ifp, 1);
}

void
re_cardbus_powerhook(int why, void *arg)
{
	struct rl_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	if (why == PWR_RESUME)
		re_init(ifp);
}
