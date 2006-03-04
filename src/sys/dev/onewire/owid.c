/*	$OpenBSD: owid.c,v 1.1 2006/03/04 16:27:03 grange Exp $	*/

/*
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
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
 * 1-Wire ID family type device driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sensors.h>

#include <dev/onewire/onewiredevs.h>
#include <dev/onewire/onewirereg.h>
#include <dev/onewire/onewirevar.h>

struct owid_softc {
	struct device		sc_dev;

	void *			sc_onewire;
	u_int64_t		sc_rom;

	struct sensor		sc_sensor;

	int			sc_dying;
};

int	owid_match(struct device *, void *, void *);
void	owid_attach(struct device *, struct device *, void *);
int	owid_detach(struct device *, int);
int	owid_activate(struct device *, enum devact);

struct cfattach owid_ca = {
	sizeof(struct owid_softc),
	owid_match,
	owid_attach,
	owid_detach,
	owid_activate
};

struct cfdriver owid_cd = {
	NULL, "owid", DV_DULL
};

static const struct onewire_matchfam owid_fams[] = {
	{ ONEWIRE_FAMILY_DS1990 }
};

int
owid_match(struct device *parent, void *match, void *aux)
{
	return (onewire_matchbyfam(aux, owid_fams,
	    sizeof(owid_fams) /sizeof(owid_fams[0])));
}

void
owid_attach(struct device *parent, struct device *self, void *aux)
{
	struct owid_softc *sc = (struct owid_softc *)self;
	struct onewire_attach_args *oa = aux;

	sc->sc_onewire = oa->oa_onewire;
	sc->sc_rom = oa->oa_rom;

	/* Initialize sensor */
	strlcpy(sc->sc_sensor.device, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensor.device));
	sc->sc_sensor.type = SENSOR_INTEGER;
	strlcpy(sc->sc_sensor.desc, "ID", sizeof(sc->sc_sensor.desc));
	sc->sc_sensor.value = ONEWIRE_ROM_SN(sc->sc_rom);
	sensor_add(&sc->sc_sensor);

	printf("\n");
}

int
owid_detach(struct device *self, int flags)
{
	struct owid_softc *sc = (struct owid_softc *)self;

	sensor_del(&sc->sc_sensor);

	return (0);
}

int
owid_activate(struct device *self, enum devact act)
{
	struct owid_softc *sc = (struct owid_softc *)self;

	switch (act) {
	case DVACT_ACTIVATE:
		return (EOPNOTSUPP);
	case DVACT_DEACTIVATE:
		sc->sc_dying = 1;
		break;
	}

	return (0);
}
