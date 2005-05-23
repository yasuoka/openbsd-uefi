/*	$OpenBSD: zaurus_scoop.c,v 1.10 2005/05/23 22:44:57 pascoe Exp $	*/

/*
 * Copyright (c) 2005 Uwe Stuehler <uwe@bsdx.de>
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
#include <sys/conf.h>
#include <sys/gpio.h>

#include <machine/bus.h>

#include <arm/xscale/pxa2x0var.h>

#include <zaurus/dev/zaurus_scoopreg.h>
#include <zaurus/dev/zaurus_scoopvar.h>

struct scoop_softc {
	struct device sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	u_int16_t sc_gpwr;	/* GPIO state before suspend */
};

int	scoopmatch(struct device *, void *, void *);
void	scoopattach(struct device *, struct device *, void *);

struct cfattach scoop_pxaip_ca = {
	sizeof (struct scoop_softc), scoopmatch, scoopattach
};

struct cfdriver scoop_cd = {
	NULL, "scoop", DV_DULL
};

int	scoop_gpio_pin_read(struct scoop_softc *sc, int);
void	scoop_gpio_pin_write(struct scoop_softc *sc, int, int);
void	scoop_gpio_pin_ctl(struct scoop_softc *sc, int, int);


int
scoopmatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;

	/*
	 * Only the C3000 models (pxa270) are known to have two SCOOPs,
	 * on other models we only find the first one.
	 */
        if ((cputype & ~CPU_ID_XSCALE_COREREV_MASK) == CPU_ID_PXA27X)
	    	return (cf->cf_unit < 2);

	return (cf->cf_unit == 0);
}

void
scoopattach(struct device *parent, struct device *self, void *aux)
{
	struct pxaip_attach_args *pxa = aux;
	struct scoop_softc *sc = (struct scoop_softc *)self;
	bus_addr_t addr;
	bus_size_t size;

	sc->sc_iot = pxa->pxa_iot;

	if (pxa->pxa_addr != -1)
		addr = pxa->pxa_addr;
	else if (sc->sc_dev.dv_unit == 0)
		addr = SCOOP0_BASE;
	else
		addr = SCOOP1_BASE;

	size = pxa->pxa_size < SCOOP_SIZE ? SCOOP_SIZE : pxa->pxa_size;

	if (bus_space_map(sc->sc_iot, addr, size, 0, &sc->sc_ioh) != 0) {
		printf(": failed to map %s\n", sc->sc_dev.dv_xname);
		return;
	}

	printf(": Onboard Peripheral Controller\n");
}

int
scoop_gpio_pin_read(struct scoop_softc *sc, int pin)
{
	u_int16_t rv;
	u_int16_t bit = (1 << pin);

	rv = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR);
	return (rv & bit) != 0 ? 1 : 0;
}

void
scoop_gpio_pin_write(struct scoop_softc *sc, int pin, int level)
{
	u_int16_t rv;
	u_int16_t bit = (1 << pin);

	rv = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR);
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR,
	    level == GPIO_PIN_LOW ? (rv & ~bit) : (rv | bit));
}

void
scoop_gpio_pin_ctl(struct scoop_softc *sc, int pin, int flags)
{
	u_int16_t rv;
	u_int16_t bit = (1 << pin);

	rv = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPCR);
	switch (flags & (GPIO_PIN_INPUT|GPIO_PIN_OUTPUT)) {
	case GPIO_PIN_INPUT:
		rv &= ~bit;
		break;
	case GPIO_PIN_OUTPUT:
		rv |= bit;
		break;
	}
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPCR, rv);
}

/*
 * Turn the LCD background light and contrast signal on or off.
 */
void
scoop_set_backlight(int on, int cont)
{

	if (scoop_cd.cd_ndevs > 1 && scoop_cd.cd_devs[1] != NULL) {
		/* C3000 */
		scoop_gpio_pin_write(scoop_cd.cd_devs[1],
		    SCOOP1_BACKLIGHT_CONT, !cont);
		scoop_gpio_pin_write(scoop_cd.cd_devs[1],
		    SCOOP1_BACKLIGHT_ON, on);
	}
#if 0
	else if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL) {
		scoop_gpio_pin_write(scoop_cd.cd_devs[0],
		    SCOOP0_BACKLIGHT_CONT, cont);
	}
#endif
}

/*
 * Turn the infrared LED on or off (must be on while transmitting).
 */
void
scoop_set_irled(int on)
{
	if (scoop_cd.cd_ndevs > 1 && scoop_cd.cd_devs[1] != NULL)
		/* IR_ON is inverted */
		scoop_gpio_pin_write(scoop_cd.cd_devs[1],
		    SCOOP1_IR_ON, !on);
}

/*
 * Turn the green and orange LEDs on or off.  If the orange LED is on,
 * then it is wired to indicate if A/C is connected.  The green LED has
 * no such predefined function.
 */
void
scoop_led_set(int led, int on)
{

	if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL) {
		if ((led & SCOOP_LED_GREEN) != 0)
			scoop_gpio_pin_write(scoop_cd.cd_devs[0],
			    SCOOP0_LED_GREEN, on);
		if (scoop_cd.cd_ndevs > 1 && (led & SCOOP_LED_ORANGE) != 0)
			scoop_gpio_pin_write(scoop_cd.cd_devs[0],
			    SCOOP0_LED_ORANGE_C3000, on);
	}
}

/*
 * Enable or disable the headphone output connection.
 */
void
scoop_set_headphone(int on)
{
	if (scoop_cd.cd_ndevs < 1 || scoop_cd.cd_devs[0] == NULL)
		return;

	scoop_gpio_pin_ctl(scoop_cd.cd_devs[0], SCOOP0_MUTE_L,
	    GPIO_PIN_OUTPUT);
	scoop_gpio_pin_ctl(scoop_cd.cd_devs[0], SCOOP0_MUTE_R,
	    GPIO_PIN_OUTPUT);

	if (on) {
		scoop_gpio_pin_write(scoop_cd.cd_devs[0], SCOOP0_MUTE_L,
		    GPIO_PIN_HIGH);
		scoop_gpio_pin_write(scoop_cd.cd_devs[0], SCOOP0_MUTE_R,
		    GPIO_PIN_HIGH);
	} else {
		scoop_gpio_pin_write(scoop_cd.cd_devs[0], SCOOP0_MUTE_L,
		    GPIO_PIN_LOW);
		scoop_gpio_pin_write(scoop_cd.cd_devs[0], SCOOP0_MUTE_R,
		    GPIO_PIN_LOW);
	}
}

void
scoop_battery_temp_adc(int enable)
{

	if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL)
		scoop_gpio_pin_write(scoop_cd.cd_devs[0],
		    SCOOP0_ADC_TEMP_ON_C3000, enable);
}

void
scoop_charge_battery(int enable, int voltage_high)
{

	if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL) {
		scoop_gpio_pin_write(scoop_cd.cd_devs[0],
		    SCOOP0_JK_B_C3000, voltage_high);
		scoop_gpio_pin_write(scoop_cd.cd_devs[0],
		    SCOOP0_CHARGE_OFF_C3000, !enable);
	}
}

void
scoop_discharge_battery(int enable)
{

	if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL)
		scoop_gpio_pin_write(scoop_cd.cd_devs[0],
		    SCOOP0_JK_A_C3000, enable);
}

/* XXX */
void scoop_check_mcr(void);
void
scoop_check_mcr(void)
{
	struct	scoop_softc *sc;

	/* C3000 */
	if (scoop_cd.cd_ndevs > 1 && scoop_cd.cd_devs[1] != NULL) {
		
		sc = scoop_cd.cd_devs[0];
		if ((bus_space_read_2(sc->sc_iot, sc->sc_ioh, SCOOP_MCR) &
		    0x100) == 0)
			bus_space_write_2(sc->sc_iot, sc->sc_ioh, SCOOP_MCR,
			    0x0101);

		sc = scoop_cd.cd_devs[1];
		if ((bus_space_read_2(sc->sc_iot, sc->sc_ioh, SCOOP_MCR) &
		    0x100) == 0)
			bus_space_write_2(sc->sc_iot, sc->sc_ioh, SCOOP_MCR,
			    0x0101);
	}
}

void
scoop_suspend(void)
{
	struct scoop_softc *sc;
	u_int32_t rv;

	if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL) {
		sc = scoop_cd.cd_devs[0];
		sc->sc_gpwr = bus_space_read_2(sc->sc_iot, sc->sc_ioh,
		    SCOOP_GPWR);
		/* C3000 */
		bus_space_write_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR,
		    sc->sc_gpwr & ~((1<<SCOOP0_MUTE_L) | (1<<SCOOP0_MUTE_R) |
		    (1<<SCOOP0_JK_A_C3000) | (1<<SCOOP0_ADC_TEMP_ON_C3000) |
		    (1<<SCOOP0_LED_GREEN)));
	}

	/* C3000 */
	if (scoop_cd.cd_ndevs > 1 && scoop_cd.cd_devs[1] != NULL) {
		sc = scoop_cd.cd_devs[1];
		sc->sc_gpwr = bus_space_read_2(sc->sc_iot, sc->sc_ioh,
		    SCOOP_GPWR);
		bus_space_write_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR,
		    sc->sc_gpwr & ~((1<<SCOOP1_RESERVED_4) |
		    (1<<SCOOP1_RESERVED_5) | (1<<SCOOP1_RESERVED_6) |
		    (1<<SCOOP1_BACKLIGHT_CONT) | (1<<SCOOP1_BACKLIGHT_ON) |
		    (1<<SCOOP1_MIC_BIAS)));
		rv = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR);
		bus_space_write_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR,
		    rv | ((1<<SCOOP1_IR_ON) | (1<<SCOOP1_RESERVED_3)));
	}
}

void
scoop_resume(void)
{
	struct scoop_softc *sc;

	if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL) {
		sc = scoop_cd.cd_devs[0];
		bus_space_write_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR,
		    sc->sc_gpwr);
	}

	if (scoop_cd.cd_ndevs > 1 && scoop_cd.cd_devs[1] != NULL) {
		sc = scoop_cd.cd_devs[1];
		bus_space_write_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR,
		    sc->sc_gpwr);
	}
}
