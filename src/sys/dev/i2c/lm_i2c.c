/*	$OpenBSD: lm_i2c.c,v 1.1 2005/12/27 20:47:00 kettenis Exp $	*/

/*
 * Copyright (c) 2005 Mark Kettenis
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
#include <sys/sensors.h>

#include <machine/bus.h>

#include <dev/i2c/i2cvar.h>
#include <dev/ic/nslm7xvar.h>

struct lm_i2c_softc {
	struct lm_softc sc_lmsc;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;
};

int lm_i2c_match(struct device *, void *, void *);
void lm_i2c_attach(struct device *, struct device *, void *);
u_int8_t lm_i2c_readreg(struct lm_softc *, int);
void lm_i2c_writereg(struct lm_softc *, int, int);

struct cfattach lm_i2c_ca = {
	sizeof(struct lm_i2c_softc), lm_i2c_match, lm_i2c_attach
};

int
lm_i2c_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "as99127f") == 0) {
		return (1);
	}
	return (0);
}

void
lm_i2c_attach(struct device *parent, struct device *self, void *aux)
{
	struct lm_i2c_softc *sc = (struct lm_i2c_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t cmd, data;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	/* Bus-independent attachment. */
	sc->sc_lmsc.lm_writereg = lm_i2c_writereg;
	sc->sc_lmsc.lm_readreg = lm_i2c_readreg;

	lm_attach(&sc->sc_lmsc);

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);

	cmd = 0x4a;
	iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, I2C_F_POLL);

	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	printf("%s: satellites at addr 0x%x and addr 0x%x\n",
	    sc->sc_lmsc.sc_dev.dv_xname,
	    0x48 + (data & 0x7), 0x48 + ((data >> 4) & 0x7));
}

u_int8_t
lm_i2c_readreg(struct lm_softc *lmsc, int reg)
{
	struct lm_i2c_softc *sc = (struct lm_i2c_softc *)lmsc;
	u_int8_t cmd, data;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = reg;
	iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	     sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0);

	iic_release_bus(sc->sc_tag, 0);

	return data;
}

void
lm_i2c_writereg(struct lm_softc *lmsc, int reg, int val)
{
	struct lm_i2c_softc *sc = (struct lm_i2c_softc *)lmsc;
	u_int8_t cmd, data;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = reg;
	data = val;
	iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0);

	iic_release_bus(sc->sc_tag, 0);
}
