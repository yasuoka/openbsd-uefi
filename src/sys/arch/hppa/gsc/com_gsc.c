/*	$OpenBSD: com_gsc.c,v 1.8 2000/03/13 14:39:59 mickey Exp $	*/

/*
 * Copyright (c) 1998 Michael Shalayeff
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
 *	This product includes software developed by Michael Shalayeff.
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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/tty.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

#include <hppa/dev/cpudevs.h>
#include <hppa/gsc/gscbusvar.h>

#define	COMGSC_OFFSET	0x800
struct com_gsc_regs {
	u_int8_t reset;
};

int	com_gsc_probe __P((struct device *, void *, void *));
void	com_gsc_attach __P((struct device *, struct device *, void *));

struct cfattach com_gsc_ca = {
	sizeof(struct com_softc), com_gsc_probe, com_gsc_attach
};

int
com_gsc_probe(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	register struct gsc_attach_args *ga = aux;

	if (ga->ga_type.iodc_type != HPPA_TYPE_FIO ||
	    (ga->ga_type.iodc_sv_model != HPPA_FIO_GRS232 &&
	     (ga->ga_type.iodc_sv_model != HPPA_FIO_RS232)))
		return 0;

	return comprobe1(ga->ga_iot, ga->ga_hpa + COMGSC_OFFSET);
}

void
com_gsc_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	register struct com_softc *sc = (void *)self;
	register struct gsc_attach_args *ga = aux;

	sc->sc_hwflags = 0;
	sc->sc_swflags = 0;
	sc->sc_iot = ga->ga_iot;
	sc->sc_ioh = ga->ga_hpa + COMGSC_OFFSET;
	sc->sc_iobase = (bus_addr_t)ga->ga_hpa + COMGSC_OFFSET;

#if notyet
	*(volatile u_int8_t *)ga->ga_hpa = 0xd0;	/* reset */
	DELAY(1000);
#endif

	/* com_attach_subr(sc); TODO */

	sc->sc_ih = gsc_intr_establish((struct gsc_softc *)parent, IPL_TTY,
				       ga->ga_irq, comintr, sc, &sc->sc_dev);
}

