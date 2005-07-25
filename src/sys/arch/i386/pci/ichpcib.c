/*	$OpenBSD: ichpcib.c,v 1.8 2005/07/25 05:25:03 jsg Exp $	*/
/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
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
 * Special driver for the Intel ICHx/ICHx-M LPC bridges that attaches
 * instead of pcib(4). In addition to the core pcib(4) functionality this
 * driver provides support for the Intel SpeedStep technology and
 * power management timer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sysctl.h>
#ifdef __HAVE_TIMECOUNTER
#include <sys/timetc.h>
#endif

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/ichreg.h>

struct ichpcib_softc {
	struct device sc_dev;

	bus_space_tag_t sc_pm_iot;
	bus_space_handle_t sc_pm_ioh;
};

int	ichpcib_match(struct device *, void *, void *);
void	ichpcib_attach(struct device *, struct device *, void *);

int	ichss_present(struct pci_attach_args *);
int	ichss_setperf(int);

/* arch/i386/pci/pcib.c */
void    pcibattach(struct device *, struct device *, void *);

#ifdef __HAVE_TIMECOUNTER
u_int	ichpcib_get_timecount(struct timecounter *tc);

struct timecounter ichpcib_timecounter = {
	ichpcib_get_timecount,	/* get_timecount */
	0,			/* no poll_pps */
	0xffffff,		/* counter_mask */
	3579545,		/* frequency */
	"ICHPM",		/* name */
	1000			/* quality */
};
#endif	/* __HAVE_TIMECOUNTER */

struct cfattach ichpcib_ca = {
	sizeof(struct ichpcib_softc),
	ichpcib_match,
	ichpcib_attach
};

struct cfdriver ichpcib_cd = {
	NULL, "ichpcib", DV_DULL
};

#ifndef SMALL_KERNEL
static void *ichss_cookie;	/* XXX */
extern int setperf_prio;
#endif	/* !SMALL_KERNEL */

const struct pci_matchid ichpcib_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801AA_LPC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801AB_LPC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801BA_LPC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801BAM_LPC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801CA_LPC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801CAM_LPC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801DB_LPC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801DBM_LPC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801EB_LPC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801FB_LPC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_6300ESB_LPC }
};

int
ichpcib_match(struct device *parent, void *match, void *aux)
{
	if (pci_matchbyid((struct pci_attach_args *)aux, ichpcib_devices,
	    sizeof(ichpcib_devices) / sizeof(ichpcib_devices[0])))
		return (2);	/* supersede pcib(4) */
	return (0);
}

void
ichpcib_attach(struct device *parent, struct device *self, void *aux)
{
	struct ichpcib_softc *sc = (struct ichpcib_softc *)self;
	struct pci_attach_args *pa = aux;
	pcireg_t cntl, pmbase;

	/* Check if power management I/O space is enabled */
	cntl = pci_conf_read(pa->pa_pc, pa->pa_tag, ICH_ACPI_CNTL);
	if ((cntl & ICH_ACPI_CNTL_ACPI_EN) == 0) {
		printf(": PM disabled");
		goto corepcib;
	}

	/* Map power management I/O space */
	sc->sc_pm_iot = pa->pa_iot;
	pmbase = pci_conf_read(pa->pa_pc, pa->pa_tag, ICH_PMBASE);
	if (bus_space_map(sc->sc_pm_iot, PCI_MAPREG_IO_ADDR(pmbase),
	    ICH_PMSIZE, 0, &sc->sc_pm_ioh) != 0) {
		printf(": failed to map I/O space");
		goto corepcib;
	}

#ifdef __HAVE_TIMECOUNTER
	/* Register new timecounter */
	ichpcib_timecounter.tc_priv = sc;
	tc_init(&ichpcib_timecounter);

	printf(": %s-bit timer at %lluHz",
	    (ichpcib_timecounter.tc_counter_mask == 0xffffffff ? "32" : "24"),
	    (unsigned long long)ichpcib_timecounter.tc_frequency);
#endif	/* __HAVE_TIMECOUNTER */

#ifndef SMALL_KERNEL
	/* Check for SpeedStep */
	if (ichss_present(pa)) {
		printf(": SpeedStep");

		/* Enable SpeedStep */
		pci_conf_write(pa->pa_pc, pa->pa_tag, ICH_GEN_PMCON1,
		    pci_conf_read(pa->pa_pc, pa->pa_tag, ICH_GEN_PMCON1) |
		    ICH_GEN_PMCON1_SS_EN);

		/* Hook into hw.setperf sysctl */
		ichss_cookie = sc;
		cpu_setperf = ichss_setperf;
		setperf_prio = 2;
	}
#endif	/* !SMALL_KERNEL */

corepcib:
	/* Provide core pcib(4) functionality */
	pcibattach(parent, self, aux);
}

#ifndef SMALL_KERNEL
int
ichss_present(struct pci_attach_args *pa)
{
	pcitag_t br_tag;
	pcireg_t br_id, br_class;

	if (setperf_prio > 2)
		return (0);

	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801DBM_LPC ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801CAM_LPC)
		return (1);
	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801BAM_LPC) {
		/*
		 * Old revisions of the 82815 hostbridge found on
		 * Dell Inspirons 8000 and 8100 don't support
		 * SpeedStep.
		 */
		/*
		 * XXX: dev 0 func 0 is not always a hostbridge,
		 * should be converted to use pchb(4) hook.
		 */
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
ichss_setperf(int level)
{
	struct ichpcib_softc *sc = ichss_cookie;
	u_int8_t state, ostate, cntl;
	int s;

#ifdef DIAGNOSTIC
	if (sc == NULL) {
		printf("%s: no cookie", __func__);
		return (EFAULT);
	}
#endif

	s = splhigh();
	state = bus_space_read_1(sc->sc_pm_iot, sc->sc_pm_ioh, ICH_PM_SS_CNTL);
	ostate = state;

	/* Only two states are available */
	if (level <= 50)
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
		cntl = bus_space_read_1(sc->sc_pm_iot, sc->sc_pm_ioh,
		    ICH_PM_CNTL);
		bus_space_write_1(sc->sc_pm_iot, sc->sc_pm_ioh, ICH_PM_CNTL,
		    cntl | ICH_PM_ARB_DIS);

		/* Do the transition */
		bus_space_write_1(sc->sc_pm_iot, sc->sc_pm_ioh, ICH_PM_SS_CNTL,
		    state);

		/* Restore bus mastering arbitration state */
		bus_space_write_1(sc->sc_pm_iot, sc->sc_pm_ioh, ICH_PM_CNTL,
		    cntl);

		if (update_cpuspeed != NULL)
			update_cpuspeed();
	}
	splx(s);

	return (0);
}
#endif	/* !SMALL_KERNEL */

#ifdef __HAVE_TIMECOUNTER
u_int
ichpcib_get_timecount(struct timecounter *tc)
{
	struct ichpcib_softc *sc = tc->tc_priv;
	u_int u1, u2, u3;

	u2 = bus_space_read_4(sc->sc_pm_iot, sc->sc_pm_ioh, ICH_PM_TMR);
	u3 = bus_space_read_4(sc->sc_pm_iot, sc->sc_pm_ioh, ICH_PM_TMR);
	do {
		u1 = u2;
		u2 = u3;
		u3 = bus_space_read_4(sc->sc_pm_iot, sc->sc_pm_ioh,
		    ICH_PM_TMR);
	} while (u1 > u2 || u2 > u3);

	return (u2);
}
#endif	/* __HAVE_TIMECOUNTER */
