/*	$OpenBSD: dsrtc.c,v 1.1 2008/04/07 22:36:26 miod Exp $ */

/*
 * Copyright (c) 2001-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/ic/ds1687reg.h>

#include <machine/autoconf.h>
#include <machine/bus.h>

#include <mips64/archtype.h>
#include <mips64/dev/clockvar.h>

#include <sgi/localbus/macebus.h>
#include <sgi/pci/iocreg.h>
#include <sgi/pci/iocvar.h>

bus_space_handle_t clock_h;	/* XXX */

struct	dsrtc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_clkt;
	bus_space_handle_t	sc_clkh, sc_clkh2;

	int			(*read)(struct dsrtc_softc *, int);
	void			(*write)(struct dsrtc_softc *, int, int);
};

int	dsrtc_match_ioc(struct device *, void *, void *);
void	dsrtc_attach_ioc(struct device *, struct device *, void *);
int	dsrtc_match_macebus(struct device *, void *, void *);
void	dsrtc_attach_macebus(struct device *, struct device *, void *);

struct cfdriver dsrtc_cd = {
	NULL, "dsrtc", DV_DULL
};

struct cfattach dsrtc_macebus_ca = {
	sizeof(struct dsrtc_softc), dsrtc_match_macebus, dsrtc_attach_macebus
};

struct cfattach dsrtc_ioc_ca = {
	sizeof(struct dsrtc_softc), dsrtc_match_ioc, dsrtc_attach_ioc
};

int	ip32_dsrtc_read(struct dsrtc_softc *, int);
void	ip32_dsrtc_write(struct dsrtc_softc *, int, int);
int	ip30_dsrtc_read(struct dsrtc_softc *, int);
void	ip30_dsrtc_write(struct dsrtc_softc *, int, int);

void	ds1687_get(void *, time_t, struct tod_time *);
void	ds1687_set(void *, struct tod_time *);

static inline int frombcd(int);
static inline int tobcd(int);
static inline int
frombcd(int x)
{
	return (x >> 4) * 10 + (x & 0xf);
}
static inline int
tobcd(int x)
{
	return (x / 10 * 16) + (x % 10);
}

int
dsrtc_match_ioc(struct device *parent, void *match, void *aux)
{
	if (sys_config.system_type != SGI_OCTANE)
		return 0;

	return 1;
}

void
dsrtc_attach_ioc(struct device *parent, struct device *self, void *aux)
{
	struct dsrtc_softc *sc = (void *)self;
	struct ioc_attach_args *iaa = aux;

	sc->sc_clkt = iaa->iaa_memt;
	if (bus_space_map(sc->sc_clkt, IOC3_BYTEBUS_1, 1, 0, &sc->sc_clkh) ||
	    bus_space_map(sc->sc_clkt, IOC3_BYTEBUS_2, 1, 0, &sc->sc_clkh2)) {
		printf(": can't map registers\n");
		return;
	}

	printf(": DS1687\n");

	sc->read = ip30_dsrtc_read;
	sc->write = ip30_dsrtc_write;

	sys_tod.tod_cookie = self;
	sys_tod.tod_get = ds1687_get;
	sys_tod.tod_set = ds1687_set;
}

int
dsrtc_match_macebus(struct device *parent, void *match, void *aux)
{
	return 1;
}

void
dsrtc_attach_macebus(struct device *parent, struct device *self, void *aux)
{
	struct dsrtc_softc *sc = (void *)self;
	struct confargs *ca = aux;

	sc->sc_clkt = ca->ca_iot;
	if (bus_space_map(sc->sc_clkt, MACE_ISA_RTC_OFFS, 128*256, 0,
	    &sc->sc_clkh)) {
		printf(": can't map registers\n");
		return;
	}

	printf(": DS1687\n");

	sc->read = ip32_dsrtc_read;
	sc->write = ip32_dsrtc_write;

	sys_tod.tod_cookie = self;
	sys_tod.tod_get = ds1687_get;
	sys_tod.tod_set = ds1687_set;

	/*
	 * XXX Expose the clock address space so that it can be used
	 * outside of clock(4). This is rather inelegant, however it
	 * will have to do for now...
	 */
	clock_h = sc->sc_clkh;
}

int
ip32_dsrtc_read(struct dsrtc_softc *sc, int reg)
{
	return bus_space_read_1(sc->sc_clkt, sc->sc_clkh, reg);
}

void
ip32_dsrtc_write(struct dsrtc_softc *sc, int reg, int val)
{
	bus_space_write_1(sc->sc_clkt, sc->sc_clkh, reg, val);
}

int
ip30_dsrtc_read(struct dsrtc_softc *sc, int reg)
{
	bus_space_write_1(sc->sc_clkt, sc->sc_clkh, 0, reg);
	return bus_space_read_1(sc->sc_clkt, sc->sc_clkh2, 0);
}

void
ip30_dsrtc_write(struct dsrtc_softc *sc, int reg, int val)
{
	bus_space_write_1(sc->sc_clkt, sc->sc_clkh, 0, reg);
	bus_space_write_1(sc->sc_clkt, sc->sc_clkh2, 0, val);
}

/*
 * Dallas clock driver.
 */
void
ds1687_get(void *v, time_t base, struct tod_time *ct)
{
	struct dsrtc_softc *sc = v;
	int ctrl, century;

	/* Select bank 1. */
	ctrl = (*sc->read)(sc, DS1687_CTRL_A);
	(*sc->write)(sc, DS1687_CTRL_A, ctrl | DS1687_BANK_1);

	/* Wait for no update in progress. */
	while ((*sc->read)(sc, DS1687_CTRL_A) & DS1687_UIP)
		/* Do nothing. */;

	/* Read the RTC. */
	ct->sec = frombcd((*sc->read)(sc, DS1687_SEC));
	ct->min = frombcd((*sc->read)(sc, DS1687_MIN));
	ct->hour = frombcd((*sc->read)(sc, DS1687_HOUR));
	ct->day = frombcd((*sc->read)(sc, DS1687_DAY));
	ct->mon = frombcd((*sc->read)(sc, DS1687_MONTH));
	ct->year = frombcd((*sc->read)(sc, DS1687_YEAR));
	century = frombcd((*sc->read)(sc, DS1687_CENTURY));

	ct->year += 100 * (century - 19);
}

void
ds1687_set(void *v, struct tod_time *ct)
{
	struct dsrtc_softc *sc = v;
	int year, century, ctrl;

	century = ct->year / 100 + 19;
	year = ct->year % 100;

	/* Select bank 1. */
	ctrl = (*sc->read)(sc, DS1687_CTRL_A);
	(*sc->write)(sc, DS1687_CTRL_A, ctrl | DS1687_BANK_1);

	/* Select data mode 0 (BCD) and 24 hour time. */
	ctrl = (*sc->read)(sc, DS1687_CTRL_B);
	(*sc->write)(sc, DS1687_CTRL_B,
	    (ctrl & ~DS1687_DM_1) | DS1687_24_HR);

	/* Prevent updates. */
	ctrl = (*sc->read)(sc, DS1687_CTRL_B);
	(*sc->write)(sc, DS1687_CTRL_B, ctrl | DS1687_SET_CLOCK);

	/* Update the RTC. */
	(*sc->write)(sc, DS1687_SEC, tobcd(ct->sec));
	(*sc->write)(sc, DS1687_MIN, tobcd(ct->min));
	(*sc->write)(sc, DS1687_HOUR, tobcd(ct->hour));
	(*sc->write)(sc, DS1687_DOW, tobcd(ct->dow));
	(*sc->write)(sc, DS1687_DAY, tobcd(ct->day));
	(*sc->write)(sc, DS1687_MONTH, tobcd(ct->mon));
	(*sc->write)(sc, DS1687_YEAR, tobcd(year));
	(*sc->write)(sc, DS1687_CENTURY, tobcd(century));

	/* Enable updates. */
	(*sc->write)(sc, DS1687_CTRL_B, ctrl);
}
