/*	$OpenBSD: wdc_obio.c,v 1.1 2006/10/07 20:52:40 miod Exp $	*/
/*	$NetBSD: wdc_obio.c,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

/*-
 * Copyright (c) 1998, 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Onno van der Linden.
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
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ata/atavar.h>
#include <dev/ic/wdcvar.h>

#include <landisk/dev/obiovar.h>

struct wdc_obio_softc {
	struct	wdc_softc sc_wdcdev;
	struct	channel_softc *sc_chanptr;
	struct	channel_softc sc_channel;

	void	*sc_ih;
};

int	wdc_obio_match(struct device *, void *, void *);
void	wdc_obio_attach(struct device *, struct device *, void *);

struct cfattach wdc_obio_ca = {
	sizeof(struct wdc_obio_softc), wdc_obio_match, wdc_obio_attach
};

#define	WDC_OBIO_REG_NPORTS	WDC_NREG
#define	WDC_OBIO_REG_SIZE	(WDC_OBIO_REG_NPORTS * 2)
#define	WDC_OBIO_AUXREG_NPORTS	1
#define	WDC_OBIO_AUXREG_SIZE	(WDC_OBIO_AUXREG_NPORTS * 2)
#define	WDC_OBIO_AUXREG_OFFSET	0x2c

int
wdc_obio_match(struct device *parent, void *vcf, void *aux)
{
	struct obio_attach_args *oa = aux;

	if (oa->oa_nio != 1)
		return (0);
	if (oa->oa_nirq != 1)
		return (0);
	if (oa->oa_niomem != 0)
		return (0);

	if (oa->oa_io[0].or_addr == IOBASEUNK)
		return (0);
	if (oa->oa_irq[0].or_irq == IRQUNK)
		return (0);

	/* XXX should probe for hardware */

	oa->oa_io[0].or_size = WDC_OBIO_REG_SIZE;

	return (1);
}

void
wdc_obio_attach(struct device *parent, struct device *self, void *aux)
{
	struct wdc_obio_softc *sc = (void *)self;
	struct obio_attach_args *oa = aux;
	struct channel_softc *chp = &sc->sc_channel;

	printf("\n");

	chp->cmd_iot = chp->ctl_iot = oa->oa_iot;

	if (bus_space_map(chp->cmd_iot, oa->oa_io[0].or_addr,
	    WDC_OBIO_REG_SIZE, 0, &chp->cmd_ioh)
	 || bus_space_map(chp->ctl_iot,
	    oa->oa_io[0].or_addr + WDC_OBIO_AUXREG_OFFSET,
	    WDC_OBIO_AUXREG_SIZE, 0, &chp->ctl_ioh)) {
		printf(": couldn't map registers\n");
		return;
	}
	chp->data32iot = chp->cmd_iot;
	chp->data32ioh = chp->cmd_ioh;

	sc->sc_ih = obio_intr_establish(oa->oa_irq[0].or_irq, IPL_BIO, wdcintr,
	    chp, self->dv_xname);

	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_PREATA;
	sc->sc_wdcdev.PIO_cap = 0;
	sc->sc_chanptr = chp;
	sc->sc_wdcdev.channels = &sc->sc_chanptr;
	sc->sc_wdcdev.nchannels = 1;
	chp->channel = 0;
	chp->wdc = &sc->sc_wdcdev;

	chp->ch_queue = malloc(sizeof(struct channel_queue), M_DEVBUF,
	    M_NOWAIT);
	if (chp->ch_queue == NULL) {
		printf("%s: can't allocate memory for command queue\n",
		    self->dv_xname);
		obio_intr_disestablish(sc->sc_ih);
		return;
	}

	wdcattach(chp);
	wdc_print_current_modes(chp);
}
