/*	$OpenBSD: clkbrd.c,v 1.2 2004/09/28 14:34:58 miod Exp $	*/

/*
 * Copyright (c) 2004 Jason L. Wright (jason@thought.net)
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/timeout.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/fhcvar.h>
#include <sparc64/dev/clkbrdreg.h>
#include <sparc64/dev/clkbrdvar.h>

extern	int sparc_led_blink;

int clkbrd_match(struct device *, void *, void *);
void clkbrd_attach(struct device *, struct device *, void *);

struct cfattach clkbrd_ca = {
	sizeof(struct clkbrd_softc), clkbrd_match, clkbrd_attach
};

struct cfdriver clkbrd_cd = {
	NULL, "clkbrd", DV_DULL
};

int
clkbrd_match(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct fhc_attach_args *fa = aux;

	if (strcmp(fa->fa_name, "clock-board") == 0)
		return (1);
	return (0);
}

void
clkbrd_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct clkbrd_softc *sc = (struct clkbrd_softc *)self;
	struct fhc_attach_args *fa = aux;
	int i, slots;
	u_int8_t r;

	sc->sc_bt = fa->fa_bustag;

	timeout_set(&sc->sc_to, clkbrd_led_blink, sc);

	if (fa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	if (fhc_bus_map(sc->sc_bt, fa->fa_reg[0].fbr_slot,
	    fa->fa_reg[0].fbr_offset, fa->fa_reg[0].fbr_size, 0,
	    &sc->sc_creg)) {
		printf(": can't map ctrl regs\n");
		return;
	}

	if (fa->fa_nreg > 2) {
		if (fhc_bus_map(sc->sc_bt, fa->fa_reg[2].fbr_slot,
		    fa->fa_reg[2].fbr_offset, fa->fa_reg[2].fbr_size, 0,
		    &sc->sc_vreg)) {
			printf(": can't map vreg\n");
			return;
		}
		sc->sc_has_vreg = 1;
	}

	r = bus_space_read_1(sc->sc_bt, sc->sc_creg, CLK_STS1);
	switch (r & 0xc0) {
	case 0x40:
		slots = 16;
		break;
	case 0xc0:
		slots = 8;
		break;
	case 0x80:
		if (sc->sc_has_vreg) {
			r = bus_space_read_1(sc->sc_bt, sc->sc_vreg, 0);
			if (r != 0) {
				if (r & 0x80)
					slots = 4;
				else
					slots = 5;
				break;
			}
		}
	default:
		slots = 4;
	}

	printf(": %d slots\n", slots);

	for (i = 0; i < fa->fa_nreg; i++) {
		printf(" r[%d] slot %x offset %x size %x\n",
		    i, fa->fa_reg[i].fbr_slot, fa->fa_reg[i].fbr_offset,
		    fa->fa_reg[i].fbr_size);
	}

	if (sparc_led_blink)
		clkbrd_led_blink(sc);
}

void
clkbrd_led_blink(void *vsc)
{
	struct clkbrd_softc *sc = vsc;
	int i, s;
	u_int8_t r;

	if (sc == NULL) {
		for (i = 0; i < clkbrd_cd.cd_ndevs; i++) {
			sc = clkbrd_cd.cd_devs[i];
			if (sc != NULL)
				clkbrd_led_blink(sc);
		}
		return;
	}

	s = splhigh();
	r = bus_space_read_1(sc->sc_bt, sc->sc_creg, CLK_CTRL);
	r ^= CLK_CTRL_RLED;
	bus_space_write_1(sc->sc_bt, sc->sc_creg, CLK_CTRL, r);
	bus_space_read_1(sc->sc_bt, sc->sc_creg, CLK_CTRL);
	splx(s);

	if (!sparc_led_blink)
		return;

	/*
	 * Blink rate is:
	 *      full cycle every second if completely idle (loadav = 0)
	 *      full cycle every 2 seconds if loadav = 1
	 *      full cycle every 3 seconds if loadav = 2
	 * etc.
	 */
	s = (((averunnable.ldavg[0] + FSCALE) * hz) >> (FSHIFT + 1));
	timeout_add(&sc->sc_to, s);
}
