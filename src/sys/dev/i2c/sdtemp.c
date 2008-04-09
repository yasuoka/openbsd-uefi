/*	$OpenBSD: sdtemp.c,v 1.3 2008/04/09 22:04:10 deraadt Exp $	*/

/*
 * Copyright (c) 2008 Theo de Raadt
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

#include <dev/i2c/i2cvar.h>

/* JDEC JC-42.4 registers */
#define JC_TEMP			0x05
#define JC_TEMP_SIGN		0x10

/* Sensors */
#define JCTEMP_TEMP		0
#define JCTEMP_NUM_SENSORS	1

struct sdtemp_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct ksensor	sc_sensor[JCTEMP_NUM_SENSORS];
	struct ksensordev sc_sensordev;
};

int	sdtemp_match(struct device *, void *, void *);
void	sdtemp_attach(struct device *, struct device *, void *);
void	sdtemp_refresh(void *);

struct cfattach sdtemp_ca = {
	sizeof(struct sdtemp_softc), sdtemp_match, sdtemp_attach
};

struct cfdriver sdtemp_cd = {
	NULL, "sdtemp", DV_DULL
};

int
sdtemp_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "se97") == 0 ||
	    strcmp(ia->ia_name, "se98") == 0 ||
	    strcmp(ia->ia_name, "mcp9805") == 0 ||
	    strcmp(ia->ia_name, "adt7408") == 0)
		return (1);
	return (0);
}

void
sdtemp_attach(struct device *parent, struct device *self, void *aux)
{
	struct sdtemp_softc *sc = (struct sdtemp_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf(": %s", ia->ia_name);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[JCTEMP_TEMP].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[JCTEMP_TEMP].desc, "Temperature",
	    sizeof(sc->sc_sensor[JCTEMP_TEMP].desc));

	if (sensor_task_register(sc, sdtemp_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[0]);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
sdtemp_refresh(void *arg)
{
	struct sdtemp_softc *sc = arg;
	u_int8_t cmd, data[2];
	int16_t sdata;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = JC_TEMP;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, data, sizeof data, 0) == 0) {
		sdata = ((data[0] << 8) | data[1]) & 0x0fff;
		if (data[0] & JC_TEMP_SIGN)
			sdata = -sdata;
		sc->sc_sensor[JCTEMP_TEMP].value =
		    273150000 + 62500 * sdata;
		sc->sc_sensor[JCTEMP_TEMP].flags &= ~SENSOR_FINVALID;
#if 0
		printf("sdtemp %02x%02x %04x %d\n", data[0], data[1],
		    (u_int)sdata & 0xffff,
		    sc->sc_sensor[JCTEMP_TEMP].value);
#endif
	}

	iic_release_bus(sc->sc_tag, 0);
}
