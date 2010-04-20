/*	$OpenBSD: oosiop_syscon.c,v 1.1 2010/04/20 22:53:24 miod Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <machine/autoconf.h>
#include <machine/board.h>
#include <machine/bus.h>

#include <aviion/dev/sysconvar.h>

#include <dev/ic/oosiopreg.h>
#include <dev/ic/oosiopvar.h>

int	oosiop_syscon_match(struct device *, void *, void *);
void	oosiop_syscon_attach(struct device *, struct device *, void *);

struct oosiop_syscon_softc {
	struct oosiop_softc	sc_base;
	struct intrhand		sc_ih;
};

const struct cfattach oosiop_syscon_ca = {
	sizeof(struct oosiop_syscon_softc),
	oosiop_syscon_match, oosiop_syscon_attach
};

int
oosiop_syscon_match(struct device *parent, void *match, void *aux)
{
	if (avtyp != AV_530)
		return 0;

	/* XXX check IOFUSE register */
	return 1;
}

void
oosiop_syscon_attach(struct device *parent, struct device *self, void *aux)
{
	struct oosiop_syscon_softc *ssc = (struct oosiop_syscon_softc *)self;
	struct oosiop_softc *sc = (struct oosiop_softc *)self;
	struct confargs *ca = aux;
	bus_space_handle_t ioh;

	if (bus_space_map(ca->ca_iot, ca->ca_paddr, OOSIOP_NREGS, 0,
	    &ioh) != 0) {
		printf(": can't map registers\n");
		return;
	}
	sc->sc_bst = ca->ca_iot;
	sc->sc_bsh = ioh;
	sc->sc_dmat = NULL;	/* no real use of tag yet */

	sc->sc_freq = 33333333;	/* XXX 25MHz models? */
	sc->sc_chip = OOSIOP_700;
	sc->sc_id = 7;		/* XXX */

	sc->sc_scntl0 = OOSIOP_SCNTL0_EPC | OOSIOP_SCNTL0_EPG;
	sc->sc_dmode = OOSIOP_DMODE_BL_4;
	sc->sc_dwt = 0x4f;	/* maximum DMA timeout allowable */
	sc->sc_ctest7 = OOSIOP_CTEST7_DC;

	oosiop_attach(sc);

	ssc->sc_ih.ih_fn = (int(*)(void *))oosiop_intr;
	ssc->sc_ih.ih_arg = sc;
	ssc->sc_ih.ih_flags = 0;
	ssc->sc_ih.ih_ipl = ca->ca_ipl;
	sysconintr_establish(INTSRC_SCSI1, &ssc->sc_ih, self->dv_xname);
}
