/*	$OpenBSD: creator_mainbus.c,v 1.2 2002/06/15 01:32:01 fgsch Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net),
 *  Federico G. Schwindt (fgsch@openbsd.org)
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
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wscons_raster.h>
#include <dev/rasops/rasops.h>

#include <sparc64/dev/creatorvar.h>

int	creator_mainbus_match(struct device *, void *, void *);
void	creator_mainbus_attach(struct device *, struct device *, void *);

struct cfattach creator_mainbus_ca = {
	sizeof(struct creator_softc), creator_mainbus_match,
	    creator_mainbus_attach
};

int
creator_mainbus_match(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "SUNW,ffb") == 0 ||
	    strcmp(ma->ma_name, "SUNW,afb") == 0)
		return (1);
	return (0);
}

void
creator_mainbus_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct creator_softc *sc = (struct creator_softc *)self;
	struct mainbus_attach_args *ma = aux;
	extern int fbnode;
	int i, nregs;

	sc->sc_bt = ma->ma_bustag;

	nregs = min(ma->ma_nreg, FFB_NREGS);

	if (nregs < FFB_REG_DFB24) {
		printf(": no dfb24 regs found\n");
		goto fail;
	}

	if (bus_space_map2(sc->sc_bt, 0, ma->ma_reg[FFB_REG_DFB24].ur_paddr,
	    ma->ma_reg[FFB_REG_DFB24].ur_len, 0, NULL, &sc->sc_pixel_h)) {
		printf("%s: failed to map dfb24\n", sc->sc_dv.dv_xname);
		goto fail;
	}

	for (i = 0; i < nregs; i++) {
		sc->sc_addrs[i] = ma->ma_reg[i].ur_paddr;
		sc->sc_sizes[i] = ma->ma_reg[i].ur_len;
	}
	sc->sc_nreg = nregs;

	sc->sc_console = (fbnode == ma->ma_node);
	sc->sc_node = ma->ma_node;

	if (strcmp(ma->ma_name, "SUNW,afb") == 0)
		sc->sc_type = FFB_AFB;

	creator_attach(sc);

	return;

fail:
	if (sc->sc_pixel_h != 0)
		bus_space_unmap(sc->sc_bt, sc->sc_pixel_h,
		    ma->ma_reg[FFB_REG_DFB24].ur_len);
}
