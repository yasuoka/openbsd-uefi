/*	$OpenBSD: elan520.c,v 1.6 2004/02/14 15:09:22 grange Exp $	*/
/*	$NetBSD: elan520.c,v 1.4 2002/10/02 05:47:15 thorpej Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
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

/*
 * Device driver for the AMD Elan SC520 System Controller.  This attaches
 * where the "pchb" driver might normally attach, and provides support for
 * extra features on the SC520, such as the watchdog timer and GPIO.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <arch/i386/pci/elan520reg.h>

struct elansc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_memt;
	bus_space_handle_t	sc_memh;
} *elansc;

int	elansc_match(struct device *, void *, void *);
void	elansc_attach(struct device *, struct device *, void *);
int	elansc_cpuspeed(int *);
int	elansc_setperf(int);

void	elansc_wdogctl(struct elansc_softc *, int, uint16_t);
#define elansc_wdogctl_reset(sc)	elansc_wdogctl(sc, 1, 0)
#define elansc_wdogctl_write(sc, val)	elansc_wdogctl(sc, 0, val)
int	elansc_wdogctl_cb(void *, int);

struct cfattach elansc_ca = {
	sizeof(struct elansc_softc), elansc_match, elansc_attach
};

struct cfdriver elansc_cd = {
	NULL, "elansc", DV_DULL
};

int
elansc_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_AMD &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_ELANSC520)
		return (10);	/* beat pchb */

	return (0);
}

static const char *elansc_speeds[] = {
	"(reserved 00)",
	"100MHz",
	"133MHz",
	"(reserved 11)",
};

#define RSTBITS "\20\x07SCP\x06HRST\x05SRST\x04WDT\x03SD\x02PRGRST\x01PWRGOOD"

void
elansc_attach(struct device *parent, struct device *self, void *aux)
{
	struct elansc_softc *sc = (void *) self;
	struct pci_attach_args *pa = aux;
	uint16_t rev;
	uint8_t ressta, cpuctl;

	sc->sc_memt = pa->pa_memt;
	if (bus_space_map(sc->sc_memt, MMCR_BASE_ADDR, NBPG, 0,
	    &sc->sc_memh) != 0) {
		printf(": unable to map registers\n");
		return;
	}

	rev = bus_space_read_2(sc->sc_memt, sc->sc_memh, MMCR_REVID);
	cpuctl = bus_space_read_1(sc->sc_memt, sc->sc_memh, MMCR_CPUCTL);
	ressta = bus_space_read_1(sc->sc_memt, sc->sc_memh, MMCR_RESSTA);

	printf(": product %d stepping %d.%d, CPU clock %s, reset %b\n",
	    (rev & REVID_PRODID) >> REVID_PRODID_SHIFT,
	    (rev & REVID_MAJSTEP) >> REVID_MAJSTEP_SHIFT,
	    (rev & REVID_MINSTEP),
	    elansc_speeds[cpuctl & CPUCTL_CPU_CLK_SPD_MASK],
	    ressta, RSTBITS);

	/*
	 * Determine cause of the last reset, and issue a warning if it
	 * was due to watchdog expiry.
	 */
	if (ressta & RESSTA_WDT_RST_DET)
		printf("%s: WARNING: LAST RESET DUE TO WATCHDOG EXPIRATION!\n",
		    sc->sc_dev.dv_xname);
	bus_space_write_1(sc->sc_memt, sc->sc_memh, MMCR_RESSTA, ressta);

	/* Set up the watchdog registers with some defaults. */
	elansc_wdogctl_write(sc, WDTMRCTL_WRST_ENB | WDTMRCTL_EXP_SEL30);

	/* ...and clear it. */
	elansc_wdogctl_reset(sc);

	wdog_register(sc, elansc_wdogctl_cb);
	elansc = sc;
	cpu_cpuspeed = elansc_cpuspeed;
	cpu_setperf = elansc_setperf;
}

void
elansc_wdogctl(struct elansc_softc *sc, int do_reset, uint16_t val)
{
	int s;
	uint8_t echo_mode;

	s = splhigh();

	/* Switch off GP bus echo mode. */
	echo_mode = bus_space_read_1(sc->sc_memt, sc->sc_memh, MMCR_GPECHO);
	bus_space_write_1(sc->sc_memt, sc->sc_memh, MMCR_GPECHO,
	    echo_mode & ~GPECHO_GP_ECHO_ENB);

	if (do_reset) {
		/* Reset the watchdog. */
		bus_space_write_2(sc->sc_memt, sc->sc_memh, MMCR_WDTMRCTL,
		    WDTMRCTL_RESET1);
		bus_space_write_2(sc->sc_memt, sc->sc_memh, MMCR_WDTMRCTL,
		    WDTMRCTL_RESET2);
	} else {
		/* Unlock the register. */
		bus_space_write_2(sc->sc_memt, sc->sc_memh, MMCR_WDTMRCTL,
		    WDTMRCTL_UNLOCK1);
		bus_space_write_2(sc->sc_memt, sc->sc_memh, MMCR_WDTMRCTL,
		    WDTMRCTL_UNLOCK2);

		/* Write the value. */
		bus_space_write_2(sc->sc_memt, sc->sc_memh, MMCR_WDTMRCTL,
		   val);
	}

	/* Switch GP bus echo mode back. */
	bus_space_write_1(sc->sc_memt, sc->sc_memh, MMCR_GPECHO, echo_mode);

	splx(s);
}

static const struct {
	int	period;		/* whole seconds */
	uint16_t exp;		/* exponent select */
} elansc_wdog_periods[] = {
	{ 1,	WDTMRCTL_EXP_SEL25 },
	{ 2,	WDTMRCTL_EXP_SEL26 },
	{ 4,	WDTMRCTL_EXP_SEL27 },
	{ 8,	WDTMRCTL_EXP_SEL28 },
	{ 16,	WDTMRCTL_EXP_SEL29 },
	{ 32,	WDTMRCTL_EXP_SEL30 },
};

int
elansc_wdogctl_cb(void *self, int period)
{
	struct elansc_softc *sc = self;
	int i;

	if (period == 0) {
		elansc_wdogctl_write(sc,
		    WDTMRCTL_WRST_ENB | WDTMRCTL_EXP_SEL30);
	} else {
		for (i = 0; i < (sizeof(elansc_wdog_periods) /
		    sizeof(elansc_wdog_periods[0])) - 1; i++)
			if (elansc_wdog_periods[i].period >= period)
				break;
		period = elansc_wdog_periods[i].period;
		elansc_wdogctl_write(sc, WDTMRCTL_ENB |
		    WDTMRCTL_WRST_ENB | elansc_wdog_periods[i].exp);
		elansc_wdogctl_reset(sc);
	}
	return (period);
}

int
elansc_cpuspeed(int *freq)
{
	static const int elansc_mhz[] = { 0, 100, 133, 999 };
	uint8_t cpuctl;

	cpuctl = bus_space_read_1(elansc->sc_memt, elansc->sc_memh,
	    MMCR_CPUCTL);
	*freq = elansc_mhz[cpuctl & CPUCTL_CPU_CLK_SPD_MASK];
	return (0);
}

int
elansc_setperf(int level)
{
	uint32_t eflags;
	uint8_t cpuctl, speed;

	level = (level > 50) ? 100 : 0;

	cpuctl = bus_space_read_1(elansc->sc_memt, elansc->sc_memh,
	    MMCR_CPUCTL);
	speed = (level == 100) ? 2 : 1;
	if ((cpuctl & CPUCTL_CPU_CLK_SPD_MASK) == speed)
		return (0);

	eflags = read_eflags();
	disable_intr();
	bus_space_write_1(elansc->sc_memt, elansc->sc_memh, MMCR_CPUCTL,
	    (cpuctl & ~CPUCTL_CPU_CLK_SPD_MASK) | speed);
	enable_intr();
	write_eflags(eflags);

	return (0);
}
