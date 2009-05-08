/*	$OpenBSD: envy.c,v 1.24 2009/05/08 16:07:26 ratchov Exp $	*/
/*
 * Copyright (c) 2007 Alexandre Ratchov <alex@caoua.org>
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
 * TODO:
 *
 * - add nspdin, nspdout, to struct envy_card
 *
 * - use eeprom version rather isht flag
 *
 * - implement HT mixer, midi uart, spdif, init ADC/DACs for >48kHz modes
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>
#include <sys/malloc.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/envyvar.h>
#include <dev/pci/envyreg.h>
#include <dev/audio_if.h>
#include <machine/bus.h>

#ifdef ENVY_DEBUG
#define DPRINTF(...) do { if (envydebug) printf(__VA_ARGS__); } while(0)
#define DPRINTFN(n, ...) do { if (envydebug > (n)) printf(__VA_ARGS__); } while(0)
int envydebug = 1;
#else
#define DPRINTF(...) do {} while(0)
#define DPRINTFN(n, ...) do {} while(0)
#endif
#define DEVNAME(sc) ((sc)->dev.dv_xname)

int  envymatch(struct device *, void *, void *);
void envyattach(struct device *, struct device *, void *);
int  envydetach(struct device *, int);

int  envy_ccs_read(struct envy_softc *, int);
void envy_ccs_write(struct envy_softc *, int, int);
int  envy_cci_read(struct envy_softc *, int);
void envy_cci_write(struct envy_softc *, int, int);
void envy_i2c_wait(struct envy_softc *);
int  envy_i2c_read(struct envy_softc *, int, int);
void envy_i2c_write(struct envy_softc *, int, int, int);
int  envy_gpio_getstate(struct envy_softc *);
void envy_gpio_setstate(struct envy_softc *, int);
int  envy_gpio_getmask(struct envy_softc *);
void envy_gpio_setmask(struct envy_softc *, int);
int  envy_gpio_getdir(struct envy_softc *);
void envy_gpio_setdir(struct envy_softc *, int);
int  envy_eeprom_gpioxxx(struct envy_softc *, int);
void envy_reset(struct envy_softc *);
int  envy_codec_read(struct envy_softc *, int, int);
void envy_codec_write(struct envy_softc *, int, int, int);
int  envy_intr(void *);

int envy_lineout_getsrc(struct envy_softc *, int);
void envy_lineout_setsrc(struct envy_softc *, int, int);
int envy_spdout_getsrc(struct envy_softc *, int);
void envy_spdout_setsrc(struct envy_softc *, int, int);
void envy_mon_getvol(struct envy_softc *, int, int, int *);
void envy_mon_setvol(struct envy_softc *, int, int, int);

int envy_open(void *, int);
void envy_close(void *);
void *envy_allocm(void *, int, size_t, int, int);
void envy_freem(void *, void *, int);
int envy_query_encoding(void *, struct audio_encoding *);
int envy_set_params(void *, int, int, struct audio_params *, 
    struct audio_params *);
int envy_round_blocksize(void *, int);
size_t envy_round_buffersize(void *, int, size_t);
int envy_trigger_output(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int envy_trigger_input(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int envy_halt_output(void *);
int envy_halt_input(void *);
int envy_getdev(void *, struct audio_device *);
int envy_query_devinfo(void *, struct mixer_devinfo *);
int envy_get_port(void *, struct mixer_ctrl *);
int envy_set_port(void *, struct mixer_ctrl *);
int envy_get_props(void *);

void delta_init(struct envy_softc *);
void delta_codec_write(struct envy_softc *, int, int, int);

void julia_init(struct envy_softc *);
void julia_codec_write(struct envy_softc *, int, int, int);

void unkenvy_init(struct envy_softc *);
void unkenvy_codec_write(struct envy_softc *, int, int, int);
int unkenvy_codec_ndev(struct envy_softc *);

int ak4524_dac_ndev(struct envy_softc *);
void ak4524_dac_devinfo(struct envy_softc *, struct mixer_devinfo *, int);
void ak4524_dac_get(struct envy_softc *, struct mixer_ctrl *, int);
int ak4524_dac_set(struct envy_softc *, struct mixer_ctrl *, int);
int ak4524_adc_ndev(struct envy_softc *);
void ak4524_adc_devinfo(struct envy_softc *, struct mixer_devinfo *, int);
void ak4524_adc_get(struct envy_softc *, struct mixer_ctrl *, int);
int ak4524_adc_set(struct envy_softc *, struct mixer_ctrl *, int);

struct cfattach envy_ca = {
	sizeof(struct envy_softc), envymatch, envyattach, envydetach
};

struct cfdriver envy_cd = {
	NULL, "envy", DV_DULL
};

struct audio_hw_if envy_hw_if = {
	envy_open,		/* open */
	envy_close,		/* close */
	NULL,			/* drain */
	envy_query_encoding,	/* query_encoding */
	envy_set_params,	/* set_params */
	envy_round_blocksize,	/* round_blocksize */
	NULL,			/* commit_settings */
	NULL,			/* init_output */
	NULL,			/* init_input */
	NULL,			/* start_output */
	NULL,			/* start_input */
	envy_halt_output,	/* halt_output */
	envy_halt_input,	/* halt_input */
	NULL,			/* speaker_ctl */
	envy_getdev,		/* getdev */
	NULL,			/* setfd */
	envy_set_port,		/* set_port */
	envy_get_port,		/* get_port */
	envy_query_devinfo,	/* query_devinfo */
	envy_allocm,		/* malloc */
	envy_freem,		/* free */
	envy_round_buffersize,	/* round_buffersize */
	NULL,			/* mappage */
	envy_get_props,		/* get_props */
	envy_trigger_output,	/* trigger_output */
	envy_trigger_input,	/* trigger_input */
	NULL
};

struct pci_matchid envy_matchids[] = {
	{ PCI_VENDOR_ICENSEMBLE, PCI_PRODUCT_ICENSEMBLE_ICE1712 },
	{ PCI_VENDOR_ICENSEMBLE, PCI_PRODUCT_ICENSEMBLE_VT172x }
};

/*
 * correspondence between rates (in frames per second)
 * and values of rate register
 */
struct {
	int rate, reg;
} envy_rates[] = {
	{ 8000, 0x6}, { 9600, 0x3}, {11025, 0xa}, {12000, 2}, {16000, 5},
	{22050, 0x9}, {24000, 0x1}, {32000, 0x4}, {44100, 8}, {48000, 0},
	{64000, 0xf}, {88200, 0xb}, {96000, 0x7}, {-1, -1}
};

/*
 * ESI julia cards don't have EEPROM, use this copy
 */
static unsigned char julia_eeprom[ENVY_EEPROM_MAXSZ] = {
	/* gpio mask/dir/state is from linux */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x20, 0x80, 0xf8, 0xc3, 
	0x9f, 0xff, 0x7f, 
	0x9f, 0xff, 0x7f,
	0x16, 0x80, 0x00
};

struct envy_codec ak4524_dac = {
	"ak4524 dac", ak4524_dac_ndev, ak4524_dac_devinfo, ak4524_dac_get, ak4524_dac_set
}, ak4524_adc = {
	"ak4524 adc", ak4524_adc_ndev, ak4524_adc_devinfo, ak4524_adc_get, ak4524_adc_set
}, unkenvy_codec = {
	"unknown codec", unkenvy_codec_ndev, NULL, NULL, NULL
};

/*
 * array with vendor/product sub-IDs to card info
 */
struct envy_card envy_cards[] = {
	{
		PCI_ID_CODE(0x1412, 0xd630),
		"M-Audio Delta 1010",
		8, &ak4524_adc, 8, &ak4524_dac,
		delta_init,
		delta_codec_write,
		NULL
	}, {
		PCI_ID_CODE(0x1412, 0xd632),
		"M-Audio Delta 66",
		4, &ak4524_adc, 4, &ak4524_dac,
		delta_init,
		delta_codec_write,
		NULL
	}, {
		PCI_ID_CODE(0x1412, 0xd633),
		"M-Audio Delta 44",
		4, &ak4524_adc, 4, &ak4524_dac,
		delta_init,
		delta_codec_write,
		NULL
	}, {
		PCI_ID_CODE(0x1412, 0xd63b),
		"M-Audio Delta 1010LT",
		8, &ak4524_adc, 8, &ak4524_dac,
		delta_init,
		delta_codec_write,
		NULL
	}, {
		0,
		"unknown 1712-based card",
		8, &unkenvy_codec, 8, &unkenvy_codec,
		unkenvy_init,
		unkenvy_codec_write
	}
}, envy_cards_ht[] = {
	{
		PCI_ID_CODE(0x3031, 0x4553),
		"ESI Julia",
		2, &unkenvy_codec, 2, &unkenvy_codec,
		julia_init,
		julia_codec_write,
		julia_eeprom
	}, {
		0,
		"unknown 1724-based card",
		2, &unkenvy_codec, 8, &unkenvy_codec,
		unkenvy_init,
		unkenvy_codec_write
	}
};


/*
 * m-audio delta specific code
 */

void
delta_init(struct envy_softc *sc)
{
	int dev;

	for (dev = 0; dev < sc->card->noch / 2; dev++) {
		envy_codec_write(sc, dev, AK4524_RST, 0x0);
		delay(300);
		envy_codec_write(sc, dev, AK4524_RST,
		    AK4524_RST_AD | AK4524_RST_DA);
		envy_codec_write(sc, dev, AK4524_FMT,
		    AK4524_FMT_IIS24);
		sc->ak[dev].reg[AK4524_DEEMVOL] = AK4524_DEEM_OFF;
		sc->ak[dev].reg[AK4524_ADC_GAIN0] = 0x7f;
		sc->ak[dev].reg[AK4524_ADC_GAIN1] = 0x7f;
		sc->ak[dev].reg[AK4524_DAC_GAIN0] = 0x7f;
		sc->ak[dev].reg[AK4524_DAC_GAIN1] = 0x7f;
	}
}

void
delta_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
	int bits, i, reg;

	reg = envy_gpio_getstate(sc);
	reg &= ~ENVY_GPIO_CSMASK;
	reg |=  ENVY_GPIO_CS(dev);
	envy_gpio_setstate(sc, reg);
	delay(1);

	bits  = 0xa000 | (addr << 8) | data;
	for (i = 0; i < 16; i++) {
		reg &= ~(ENVY_GPIO_CLK | ENVY_GPIO_DOUT);
		reg |= (bits & 0x8000) ? ENVY_GPIO_DOUT : 0;
		envy_gpio_setstate(sc, reg);
		delay(1);

		reg |= ENVY_GPIO_CLK;
		envy_gpio_setstate(sc, reg);
		delay(1);
		bits <<= 1;
	}

	reg |= ENVY_GPIO_CSMASK;
	envy_gpio_setstate(sc, reg);
	delay(1);
}


/*
 * esi julia specific code
 */

void
julia_init(struct envy_softc *sc)
{
	envy_codec_write(sc, 0, 0, 0);	/* reset */
	delay(300);
	envy_codec_write(sc, 0, 0, 0x87);	/* i2s mode */
}

void
julia_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
#define JULIA_AK4358_ADDR	0x11
	envy_i2c_write(sc, JULIA_AK4358_ADDR, addr, data);
}

/*
 * unknown card, ignore codecs setup and hope it works with the power on
 * settings
 */

void
unkenvy_init(struct envy_softc *sc)
{
}

void
unkenvy_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
}

int
unkenvy_codec_ndev(struct envy_softc *sc)
{
	return 0;
}

/*
 * AK 4524 DAC specific code
 */
int
ak4524_dac_ndev(struct envy_softc *sc)
{
	/* 1 mute + 2 volume knobs per channel pair */
	return 3 * (sc->card->noch / 2);
}


void
ak4524_dac_devinfo(struct envy_softc *sc, struct mixer_devinfo *dev, int idx)
{
	int ndev;

	ndev = sc->card->noch;
	if (idx < ndev) {
		dev->type = AUDIO_MIXER_VALUE;
		dev->mixer_class = ENVY_MIX_CLASSOUT;
		dev->un.v.delta = 2;
		dev->un.v.num_channels = 1;
		snprintf(dev->label.name, MAX_AUDIO_DEV_LEN,
		    AudioNline "%d", idx);
		strlcpy(dev->un.v.units.name, AudioNvolume,
		    MAX_AUDIO_DEV_LEN);
	} else {
		idx -= ndev;
		dev->type = AUDIO_MIXER_ENUM;
		dev->mixer_class = ENVY_MIX_CLASSOUT;
		dev->un.e.member[0].ord = 0;
		strlcpy(dev->un.e.member[0].label.name, AudioNoff,
		    MAX_AUDIO_DEV_LEN);
		dev->un.e.member[1].ord = 1;
		strlcpy(dev->un.e.member[1].label.name, AudioNon,
		   MAX_AUDIO_DEV_LEN);
		dev->un.s.num_mem = 2;
		snprintf(dev->label.name, MAX_AUDIO_DEV_LEN,
		    AudioNmute "%d-%d", 2 * idx, 2 * idx + 1);
	}
}

void
ak4524_dac_get(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val, ndev;

	ndev = sc->card->noch;
	if (idx < ndev) {
		val = envy_codec_read(sc, idx / 2,
		    (idx % 2) + AK4524_DAC_GAIN0);
		ctl->un.value.num_channels = 1;
		ctl->un.value.level[0] = 2 * val;
	} else {
		idx -= ndev;
		val = envy_codec_read(sc, idx, AK4524_DEEMVOL);
		ctl->un.ord = (val & AK4524_MUTE) ? 1 : 0;
	}
}

int
ak4524_dac_set(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val, ndev;

	ndev = sc->card->noch;
	if (idx < ndev) {
		if (ctl->un.value.num_channels != 1)
			return EINVAL;
		val = ctl->un.value.level[0] / 2;
		envy_codec_write(sc, idx / 2,
		    (idx % 2) + AK4524_DAC_GAIN0, val);
	} else {
		idx -= ndev;
		if (ctl->un.ord >= 2)
			return EINVAL;
		val = AK4524_DEEM_OFF | (ctl->un.ord ? AK4524_MUTE : 0);
		envy_codec_write(sc, idx, AK4524_DEEMVOL, val);
	}
	return 0;
}

/*
 * AK 4524 ADC specific code
 */
int
ak4524_adc_ndev(struct envy_softc *sc)
{
	/* one volume per channel */
	return sc->card->nich;
}

void
ak4524_adc_devinfo(struct envy_softc *sc, struct mixer_devinfo *dev, int idx)
{
	dev->type = AUDIO_MIXER_VALUE;
	dev->mixer_class = ENVY_MIX_CLASSIN;
	dev->un.v.delta = 2;
	dev->un.v.num_channels = 1;
	snprintf(dev->label.name, MAX_AUDIO_DEV_LEN, AudioNline "%d", idx);
	strlcpy(dev->un.v.units.name, AudioNvolume, MAX_AUDIO_DEV_LEN);
}

void
ak4524_adc_get(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val;

	val = envy_codec_read(sc, idx / 2, (idx % 2) + AK4524_ADC_GAIN0);
	ctl->un.value.num_channels = 1;
	ctl->un.value.level[0] = 2 * val;
}

int
ak4524_adc_set(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val;

	if (ctl->un.value.num_channels != 1)
		return EINVAL;
	val = ctl->un.value.level[0] / 2;
	envy_codec_write(sc, idx / 2, (idx % 2) + AK4524_ADC_GAIN0, val);
	return 0;
}

/*
 * generic Envy24 and Envy24HT code, common to all cards
 */

int
envy_ccs_read(struct envy_softc *sc, int reg) 
{
	return bus_space_read_1(sc->ccs_iot, sc->ccs_ioh, reg);
}

void
envy_ccs_write(struct envy_softc *sc, int reg, int val)
{
	bus_space_write_1(sc->ccs_iot, sc->ccs_ioh, reg, val);
}

int
envy_cci_read(struct envy_softc *sc, int index)
{
	int val;
	envy_ccs_write(sc, ENVY_CCI_INDEX, index);
	val = envy_ccs_read(sc, ENVY_CCI_DATA);
	return val;
}

void
envy_cci_write(struct envy_softc *sc, int index, int data)
{
	envy_ccs_write(sc, ENVY_CCI_INDEX, index);
	envy_ccs_write(sc, ENVY_CCI_DATA, data);
}

int
envy_gpio_getstate(struct envy_softc *sc)
{
	if (sc->isht) {
		return envy_ccs_read(sc, ENVY_CCS_GPIODATA0) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIODATA1) << 8) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIODATA2) << 16); 
	} else
		return envy_cci_read(sc, ENVY_CCI_GPIODATA);
}

void
envy_gpio_setstate(struct envy_softc *sc, int reg)
{
	if (sc->isht) {
		envy_ccs_write(sc, ENVY_CCS_GPIODATA0, reg & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIODATA1, (reg >> 8) & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIODATA2, (reg >> 16) & 0xff);
	} else
		envy_cci_write(sc, ENVY_CCI_GPIODATA, reg);
}

int
envy_gpio_getmask(struct envy_softc *sc)
{
	if (sc->isht) {
		return envy_ccs_read(sc, ENVY_CCS_GPIOMASK0) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIOMASK1) << 8) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIOMASK2) << 16); 
	} else
		return envy_cci_read(sc, ENVY_CCI_GPIOMASK);
}

void
envy_gpio_setmask(struct envy_softc *sc, int mask)
{
	if (sc->isht) {
		envy_ccs_write(sc, ENVY_CCS_GPIOMASK0, mask & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIOMASK1, (mask >> 8) & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIOMASK2, (mask >> 16) & 0xff);
	} else
		envy_cci_write(sc, ENVY_CCI_GPIOMASK, mask);
}

int
envy_gpio_getdir(struct envy_softc *sc)
{
	if (sc->isht) {
		return envy_ccs_read(sc, ENVY_CCS_GPIODIR0) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIODIR1) << 8) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIODIR2) << 16); 
	} else
		return envy_cci_read(sc, ENVY_CCI_GPIODIR);
}

void
envy_gpio_setdir(struct envy_softc *sc, int dir)
{
	if (sc->isht) {
		envy_ccs_write(sc, ENVY_CCS_GPIODIR0, dir & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIODIR1, (dir >> 8) & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIODIR2, (dir >> 16) & 0xff);
	} else
		envy_cci_write(sc, ENVY_CCI_GPIODIR, dir);
}

void
envy_i2c_wait(struct envy_softc *sc)
{
	int timeout = 50, st;

        for (;;) {
		st = envy_ccs_read(sc, ENVY_I2C_CTL);
		if (!(st & ENVY_I2C_CTL_BUSY)) 
			break;
		if (timeout == 0) {
			printf("%s: i2c busy timeout\n", DEVNAME(sc));
			break;
		}
		delay(50);
		timeout--;
	}
}

int
envy_i2c_read(struct envy_softc *sc, int dev, int addr)
{
	envy_i2c_wait(sc);
	envy_ccs_write(sc, ENVY_I2C_ADDR, addr);
	envy_i2c_wait(sc);
	envy_ccs_write(sc, ENVY_I2C_DEV, dev << 1);
	envy_i2c_wait(sc);
	return envy_ccs_read(sc, ENVY_I2C_DATA);
}

void
envy_i2c_write(struct envy_softc *sc, int dev, int addr, int data)
{
	if (dev == 0x50) {
		printf("%s: writing on eeprom is evil...\n", DEVNAME(sc));
		return;
	}
	envy_i2c_wait(sc);
	envy_ccs_write(sc, ENVY_I2C_ADDR, addr);
	envy_i2c_wait(sc);
	envy_ccs_write(sc, ENVY_I2C_DATA, data);
	envy_i2c_wait(sc);
	envy_ccs_write(sc, ENVY_I2C_DEV, (dev << 1) | 1);
}

int
envy_codec_read(struct envy_softc *sc, int dev, int addr) {
	return sc->ak[dev].reg[addr];
}

void
envy_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
	DPRINTFN(2, "envy_codec_write: %d, %d, 0x%x\n", dev, addr, data);
	sc->ak[dev].reg[addr] = data;
	sc->card->codec_write(sc, dev, addr, data);
}

int
envy_eeprom_gpioxxx(struct envy_softc *sc, int addr)
{
	int val;

	val = sc->eeprom[addr];
	if (sc->isht) {
		val |= sc->eeprom[++addr] << 8;
		val |= sc->eeprom[++addr] << 16;
	}
	return val;
}

void
envy_reset(struct envy_softc *sc)
{
	int i;

	/*
	 * full reset
	 */
	envy_ccs_write(sc, ENVY_CTL, ENVY_CTL_RESET | ENVY_CTL_NATIVE);
	delay(200);
	envy_ccs_write(sc, ENVY_CTL, ENVY_CTL_NATIVE);
	delay(200);

	/*
	 * read eeprom using i2c device or from a static array
	 */
	if (sc->card->eeprom == NULL) {
		for (i = 0; i < ENVY_EEPROM_MAXSZ; i++) {
			sc->eeprom[i] = envy_i2c_read(sc, ENVY_I2C_DEV_EEPROM, i);
		}
#ifdef ENVY_DEBUG
		printf("%s: eeprom: ", DEVNAME(sc));
		for (i = 0; i < ENVY_EEPROM_MAXSZ; i++) {
			printf(" %02x", (unsigned)sc->eeprom[i]);
		}
		printf("\n");
#endif
	} else 
		memcpy(sc->eeprom, sc->card->eeprom, ENVY_EEPROM_MAXSZ);

	/*
	 * write eeprom values to corresponding registers
	 */
	pci_conf_write(sc->pci_pc, sc->pci_tag, ENVY_CONF, 
	    sc->eeprom[ENVY_EEPROM_CONF] |
	    (sc->eeprom[ENVY_EEPROM_ACLINK] << 8) |
	    (sc->eeprom[ENVY_EEPROM_I2S] << 16) |
	    (sc->eeprom[ENVY_EEPROM_SPDIF] << 24));

	envy_gpio_setmask(sc, envy_eeprom_gpioxxx(sc, ENVY_EEPROM_GPIOMASK));
	envy_gpio_setdir(sc, envy_eeprom_gpioxxx(sc, ENVY_EEPROM_GPIODIR(sc)));
	envy_gpio_setstate(sc, envy_eeprom_gpioxxx(sc, ENVY_EEPROM_GPIOST(sc)));

	DPRINTF("%s: gpio_mask = %02x\n", DEVNAME(sc), 
		envy_gpio_getmask(sc));
	DPRINTF("%s: gpio_dir = %02x\n", DEVNAME(sc), 
		envy_gpio_getdir(sc));
	DPRINTF("%s: gpio_state = %02x\n", DEVNAME(sc), 
		envy_gpio_getstate(sc));

	/*
	 * clear all interrupts and unmask used ones
	 */ 
	envy_ccs_write(sc, ENVY_CCS_INTSTAT, 0xff);
	envy_ccs_write(sc, ENVY_CCS_INTMASK, ~ENVY_CCS_INT_MT);
	if (sc->isht) {
		bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_NSTREAM,
		    4 - sc->card->noch / 2);
		bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_IMASK,
		    ~(ENVY_MT_IMASK_PDMA0 | ENVY_MT_IMASK_RDMA0));
	}
	sc->card->init(sc);
}

int
envy_intr(void *self)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int st;

	st = bus_space_read_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_INTR);
	if (!(st & (ENVY_MT_INTR_PACK | ENVY_MT_INTR_RACK))) {
		return 0;
	}
	if (st & ENVY_MT_INTR_PACK) {
		st = ENVY_MT_INTR_PACK;
		bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_INTR, st);
		sc->ointr(sc->oarg);
	}
	if (st & ENVY_MT_INTR_RACK) {
		st = ENVY_MT_INTR_RACK;
		bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_INTR, st);
		sc->iintr(sc->iarg);
	}
	return 1;
}

int
envy_lineout_getsrc(struct envy_softc *sc, int out)
{
	int reg, shift, src;

	reg = bus_space_read_2(sc->mt_iot, sc->mt_ioh, ENVY_MT_OUTSRC);
	DPRINTF("%s: outsrc=%x\n", DEVNAME(sc), reg);
	shift = (out  & 1) ? (out & ~1) + 8 : out;
	src = (reg >> shift) & 3;
	if (src == ENVY_MT_OUTSRC_DMA) {
		return ENVY_MIX_OUTSRC_DMA;
	} else if (src == ENVY_MT_OUTSRC_MON) {
		return ENVY_MIX_OUTSRC_MON;
	}
	reg = bus_space_read_4(sc->mt_iot, sc->mt_ioh, ENVY_MT_INSEL);
	DPRINTF("%s: insel=%x\n", DEVNAME(sc), reg);
	reg = (reg >> (out * 4)) & 0xf;
	if (src == ENVY_MT_OUTSRC_LINE)
		return ENVY_MIX_OUTSRC_LINEIN + (reg & 7);
	else
		return ENVY_MIX_OUTSRC_SPDIN + (reg >> 3);
}

void
envy_lineout_setsrc(struct envy_softc *sc, int out, int src)
{
	int reg, shift, mask, sel;
	
	if (src < ENVY_MIX_OUTSRC_DMA) {
		/* 
		 * linein and spdin are used as output source so we
		 * must select the input source channel number
		 */
		if (src < ENVY_MIX_OUTSRC_SPDIN)
			sel = src - ENVY_MIX_OUTSRC_LINEIN;
		else
			sel = (src - ENVY_MIX_OUTSRC_SPDIN) << 3;

		shift = out * ENVY_MT_INSEL_BITS;
		mask = ENVY_MT_INSEL_MASK << shift;
		reg = bus_space_read_4(sc->mt_iot, sc->mt_ioh, ENVY_MT_INSEL);
		reg = (reg & ~mask) | (sel << shift);
		bus_space_write_4(sc->mt_iot, sc->mt_ioh, ENVY_MT_INSEL, reg);
		DPRINTF("%s: insel <- %x\n", DEVNAME(sc), reg);
	}

	/*
	 * set the lineout route register
	 */
	if (src < ENVY_MIX_OUTSRC_SPDIN) {
		sel = ENVY_MT_OUTSRC_LINE;
	} else if (src < ENVY_MIX_OUTSRC_DMA) {
		sel = ENVY_MT_OUTSRC_SPD;
	} else if (src == ENVY_MIX_OUTSRC_DMA) {
		sel = ENVY_MT_OUTSRC_DMA;
	} else {
		sel = ENVY_MT_OUTSRC_MON;
	}
	shift = (out  & 1) ? (out & ~1) + 8 : out;
	mask = ENVY_MT_OUTSRC_MASK << shift;
	reg = bus_space_read_2(sc->mt_iot, sc->mt_ioh, ENVY_MT_OUTSRC);
	reg = (reg & ~mask) | (sel << shift);
	bus_space_write_2(sc->mt_iot, sc->mt_ioh, ENVY_MT_OUTSRC, reg);
	DPRINTF("%s: outsrc <- %x\n", DEVNAME(sc), reg);
}


int
envy_spdout_getsrc(struct envy_softc *sc, int out)
{
	int reg, src, sel;

	reg = bus_space_read_2(sc->mt_iot, sc->mt_ioh, ENVY_MT_SPDROUTE);
	DPRINTF("%s: spdroute=%x\n", DEVNAME(sc), reg);
	src = (out == 0) ? reg : reg >> 2;
	src &= ENVY_MT_SPDSRC_MASK;
	if (src == ENVY_MT_SPDSRC_DMA) {
		return ENVY_MIX_OUTSRC_DMA;
	} else if (src == ENVY_MT_SPDSRC_MON) {
		return ENVY_MIX_OUTSRC_MON;
	}

	sel = (out == 0) ? reg >> 8 : reg >> 12;
	sel &= ENVY_MT_SPDSEL_MASK;
	if (src == ENVY_MT_SPDSRC_LINE)
		return ENVY_MIX_OUTSRC_LINEIN + (sel & 7);
	else
		return ENVY_MIX_OUTSRC_SPDIN + (sel >> 3);
}

void
envy_spdout_setsrc(struct envy_softc *sc, int out, int src)
{
	int reg, shift, mask, sel;
	
	reg = bus_space_read_2(sc->mt_iot, sc->mt_ioh, ENVY_MT_SPDROUTE);
	if (src < ENVY_MIX_OUTSRC_DMA) {
		/* 
		 * linein and spdin are used as output source so we
		 * must select the input source channel number
		 */
		if (src < ENVY_MIX_OUTSRC_SPDIN)
			sel = src - ENVY_MIX_OUTSRC_LINEIN;
		else
			sel = (src - ENVY_MIX_OUTSRC_SPDIN) << 3;

		shift = 8 + out * ENVY_MT_SPDSEL_BITS;
		mask = ENVY_MT_SPDSEL_MASK << shift;
		reg = (reg & ~mask) | (sel << shift);
	}

	/*
	 * set the lineout route register
	 */
	if (src < ENVY_MIX_OUTSRC_SPDIN) {
		sel = ENVY_MT_OUTSRC_LINE;
	} else if (src < ENVY_MIX_OUTSRC_DMA) {
		sel = ENVY_MT_OUTSRC_SPD;
	} else if (src == ENVY_MIX_OUTSRC_DMA) {
		sel = ENVY_MT_OUTSRC_DMA;
	} else {
		sel = ENVY_MT_OUTSRC_MON;
	}
	shift = out * 2;
	mask = ENVY_MT_SPDSRC_MASK << shift;
	reg = (reg & ~mask) | (sel << shift);
	bus_space_write_2(sc->mt_iot, sc->mt_ioh, ENVY_MT_SPDROUTE, reg);
	DPRINTF("%s: spdroute <- %x\n", DEVNAME(sc), reg);
}

void
envy_mon_getvol(struct envy_softc *sc, int idx, int ch, int *val)
{
	int reg;

	bus_space_write_2(sc->mt_iot, sc->mt_ioh, ENVY_MT_MONIDX, idx);
	reg = bus_space_read_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_MONDATA + ch);
	*val = 0x7f - (reg & 0x7f);
}

void
envy_mon_setvol(struct envy_softc *sc, int idx, int ch, int val)
{
	int reg;

	bus_space_write_2(sc->mt_iot, sc->mt_ioh, ENVY_MT_MONIDX, idx);
	reg = 0x7f - val;
	DPRINTF("%s: mon=%d/%d <- %d\n", DEVNAME(sc), reg, ch, val);
	bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_MONDATA + ch, reg);
}

int
envymatch(struct device *parent, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, envy_matchids,
	    sizeof(envy_matchids) / sizeof(envy_matchids[0]));
}

void
envyattach(struct device *parent, struct device *self, void *aux)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	pci_intr_handle_t ih;
	const char *intrstr;
	int subid;

	sc->pci_tag = pa->pa_tag;
	sc->pci_pc = pa->pa_pc;
	sc->pci_dmat = pa->pa_dmat;
	sc->pci_ih = NULL;
	sc->ibuf.addr = sc->obuf.addr = NULL;
	sc->ccs_iosz = 0;
	sc->mt_iosz = 0;
	sc->isht = (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ICENSEMBLE_VT172x);

	if (pci_mapreg_map(pa, ENVY_CTL_BAR, PCI_MAPREG_TYPE_IO, 0, 
		&sc->ccs_iot, &sc->ccs_ioh, NULL, &sc->ccs_iosz, 0)) {
		printf(": can't map ctl i/o space\n");
		sc->ccs_iosz = 0;
		return;
        }
	if (pci_mapreg_map(pa, ENVY_MT_BAR(sc->isht), PCI_MAPREG_TYPE_IO, 0,
		&sc->mt_iot, &sc->mt_ioh, NULL, &sc->mt_iosz, 0)) {
		printf(": can't map mt i/o space\n");
		sc->mt_iosz = 0;
		return;
        }
	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
	}
	intrstr = pci_intr_string(sc->pci_pc, ih);
	sc->pci_ih = pci_intr_establish(sc->pci_pc, ih, IPL_AUDIO,
	    envy_intr, sc, sc->dev.dv_xname);
	if (sc->pci_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);
	subid = pci_conf_read(sc->pci_pc, sc->pci_tag, PCI_SUBVEND_0);
	sc->card = sc->isht ? envy_cards_ht : envy_cards;
	while (sc->card->subid != subid) {
		if (sc->card->subid == 0) {
			printf("%s: unknown card\n", DEVNAME(sc));
			break;
		}
		sc->card++;
	}
	DPRINTF("%s: type=%s\n", DEVNAME(sc), sc->card->name);
	envy_reset(sc);
	sc->audio = audio_attach_mi(&envy_hw_if, sc, &sc->dev);
}

int
envydetach(struct device *self, int flags)
{
	struct envy_softc *sc = (struct envy_softc *)self;

	if (sc->pci_ih != NULL) {
		pci_intr_disestablish(sc->pci_pc, sc->pci_ih);
		sc->pci_ih = NULL;
	}
	if (sc->ccs_iosz) {
		bus_space_unmap(sc->ccs_iot, sc->ccs_ioh, sc->ccs_iosz);
	}
	if (sc->mt_iosz) {
		bus_space_unmap(sc->mt_iot, sc->mt_ioh, sc->mt_iosz);
	}
	return 0;
}

int
envy_open(void *self, int flags)
{
	return 0;
}

void
envy_close(void *self)
{
}

void *
envy_allocm(void *self, int dir, size_t size, int type, int flags)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int err, rsegs, basereg, wait;
	struct envy_buf *buf;

	if (dir == AUMODE_RECORD) {
		buf = &sc->ibuf;
		basereg = ENVY_MT_RADDR;
	} else {
		buf = &sc->obuf;
		basereg = ENVY_MT_PADDR;
	}
	if (buf->addr != NULL) {
		DPRINTF("%s: multiple alloc, dir = %d\n", DEVNAME(sc), dir);
		return NULL;
	}
	buf->size = size;
	wait = (flags & M_NOWAIT) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK;

#define ENVY_ALIGN	4
#define ENVY_BOUNDARY	0

	err = bus_dmamem_alloc(sc->pci_dmat, buf->size, ENVY_ALIGN, 
	    ENVY_BOUNDARY, &buf->seg, 1, &rsegs, wait);
	if (err) {
		DPRINTF("%s: dmamem_alloc: failed %d\n", DEVNAME(sc), err);
		goto err_ret;
	}

	err = bus_dmamem_map(sc->pci_dmat, &buf->seg, rsegs, buf->size, 
            &buf->addr, wait | BUS_DMA_COHERENT);
	if (err) {
		DPRINTF("%s: dmamem_map: failed %d\n", DEVNAME(sc), err);
		goto err_free;
	}
	
	err = bus_dmamap_create(sc->pci_dmat, buf->size, 1, buf->size, 0,
	    wait, &buf->map);
	if (err) {
		DPRINTF("%s: dmamap_create: failed %d\n", DEVNAME(sc), err);
		goto err_unmap;
	}
	
	err = bus_dmamap_load(sc->pci_dmat, buf->map, buf->addr, 
            buf->size, NULL, wait);
	if (err) {
		DPRINTF("%s: dmamap_load: failed %d\n", DEVNAME(sc), err);
		goto err_destroy;
	}
	bus_space_write_4(sc->mt_iot, sc->mt_ioh, basereg, buf->seg.ds_addr);
	DPRINTF("%s: allocated %ld bytes dir=%d, ka=%p, da=%p\n", 
		DEVNAME(sc), buf->size, dir, buf->addr, (void *)buf->seg.ds_addr);
	return buf->addr;

 err_destroy:
	bus_dmamap_destroy(sc->pci_dmat, buf->map);	
 err_unmap:
	bus_dmamem_unmap(sc->pci_dmat, buf->addr, buf->size);
 err_free:
	bus_dmamem_free(sc->pci_dmat, &buf->seg, 1);
 err_ret:
	return NULL;	
}

void
envy_freem(void *self, void *addr, int type)
{
	struct envy_buf *buf;
	struct envy_softc *sc = (struct envy_softc *)self;
	int dir;

	if (sc->ibuf.addr == addr) {
		buf = &sc->ibuf;
		dir = AUMODE_RECORD;
	} else if (sc->obuf.addr == addr) {
		buf = &sc->obuf;
		dir = AUMODE_PLAY;
	} else {
		DPRINTF("%s: no buf to free\n", DEVNAME(sc));
		return;
	}
	bus_dmamap_destroy(sc->pci_dmat, buf->map);	
	bus_dmamem_unmap(sc->pci_dmat, buf->addr, buf->size);
	bus_dmamem_free(sc->pci_dmat, &buf->seg, 1);
	buf->addr = NULL;
	DPRINTF("%s: freed buffer (mode=%d)\n", DEVNAME(sc), dir);
}

int
envy_query_encoding(void *self, struct audio_encoding *enc)
{
	if (enc->index == 0) {
		strlcpy(enc->name, AudioEslinear_le, sizeof(enc->name));
		enc->encoding = AUDIO_ENCODING_SLINEAR_LE;
		enc->precision = 24;
		enc->flags = 0;
		return 0;
	}
	return EINVAL;
}

int
envy_set_params(void *self, int setmode, int usemode,
    struct audio_params *p, struct audio_params *r)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int i, rate, reg;

	if (setmode == 0) {
		DPRINTF("%s: no params to set\n", DEVNAME(sc));
		return 0;
	}
	if (setmode == (AUMODE_PLAY | AUMODE_RECORD) &&
	    p->sample_rate != r->sample_rate) {
		DPRINTF("%s: play/rec rates mismatch\n", DEVNAME(sc));
		r->sample_rate = p->sample_rate;
	}
	rate = (setmode & AUMODE_PLAY) ? p->sample_rate : r->sample_rate;
	for (i = 0; envy_rates[i].rate < rate; i++) {
		if (envy_rates[i].rate == -1) {
			i--;
			DPRINTF("%s: rate: %d -> %d\n", DEVNAME(sc), rate, i);
			break;
		}
	}
	reg = bus_space_read_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_RATE);
	reg &= ~ENVY_MT_RATEMASK;
	reg |= envy_rates[i].reg;
	bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_RATE, reg);
	if (setmode & AUMODE_PLAY) {
		p->encoding = AUDIO_ENCODING_SLINEAR;
		p->precision = 24;
		p->channels = sc->isht ? sc->card->noch : ENVY_PCHANS;
	}
	if (setmode & AUMODE_RECORD) {
		r->encoding = AUDIO_ENCODING_SLINEAR;
		r->precision = 24;
		r->channels = sc->isht ? sc->card->nich : ENVY_RCHANS;
	}
	return 0;
}

int
envy_round_blocksize(void *self, int blksz)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int mul, pmult, rmult;

	/*
	 * XXX: audio(4) layer doesn't round to the sample size
	 * until it's fixed, roll our own rounding
	 */

	pmult = (sc->isht ? sc->card->noch / 2 : ENVY_PCHANS / 2);
	if (pmult == 0)
		pmult = 1;
	rmult = (sc->isht ? sc->card->nich / 2 : ENVY_RCHANS / 2);
	if (rmult == 0)
		rmult = 1;
	mul = pmult * rmult;
	if ((mul & 1) != 0)
		mul <<= 1;
	if ((mul & 3) != 0)
		mul <<= 1;
	if ((mul & 7) != 0)
		mul <<= 1;
	blksz -= blksz % mul;
	if (blksz == 0)
		blksz = mul;
	return blksz;
}

size_t
envy_round_buffersize(void *self, int dir, size_t bufsz)
{
	return bufsz;
}

int
envy_trigger_output(void *self, void *start, void *end, int blksz,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	size_t bufsz;
	int st;

	bufsz = end - start;
#ifdef ENVY_DEBUG
	if (blksz % (sc->isht ? sc->card->noch * 4 : ENVY_PFRAME_SIZE) != 0) {
		printf("%s: %d: bad output blksz\n", DEVNAME(sc), blksz);
		return EINVAL;
	}
	if (bufsz % blksz) {
		printf("%s: %ld: bad output bufsz\n", DEVNAME(sc), bufsz);
		return EINVAL;
	}
#endif
	bus_space_write_2(sc->mt_iot, sc->mt_ioh, 
	    ENVY_MT_PBUFSZ, bufsz / 4 - 1);
	bus_space_write_2(sc->mt_iot, sc->mt_ioh, 
	    ENVY_MT_PBLKSZ(sc), blksz / 4 - 1);

	sc->ointr = intr;
	sc->oarg = arg;

	st = ENVY_MT_INTR_PACK;
	bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_INTR, st);

	st = bus_space_read_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_CTL);
	st |= ENVY_MT_CTL_PSTART;
	bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_CTL, st);
	return 0;
}

int
envy_trigger_input(void *self, void *start, void *end, int blksz,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	size_t bufsz;
	int st;
	
	bufsz = end - start;
#ifdef ENVY_DEBUG
	if (blksz % (sc->isht ? sc->card->nich * 4 : ENVY_RFRAME_SIZE) != 0) {
		printf("%s: %d: bad input blksz\n", DEVNAME(sc), blksz);
		return EINVAL;
	}
	if (bufsz % blksz != 0) {
		printf("%s: %ld: bad input bufsz\n", DEVNAME(sc), bufsz);
		return EINVAL;
	}
#endif
	bus_space_write_2(sc->mt_iot, sc->mt_ioh, 
	    ENVY_MT_RBUFSZ, bufsz / 4 - 1);
	bus_space_write_2(sc->mt_iot, sc->mt_ioh, 
	    ENVY_MT_RBLKSZ, blksz / 4 - 1);

	sc->iintr = intr;
	sc->iarg = arg;

	st = ENVY_MT_INTR_RACK;
	bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_INTR, st);

	st = bus_space_read_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_CTL);
	st |= ENVY_MT_CTL_RSTART(sc);
	bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_CTL, st);
	return 0;
}

int
envy_halt_output(void *self)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int st;

	st = bus_space_read_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_CTL);
	st &= ~ENVY_MT_CTL_PSTART;
	bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_CTL, st);
	return 0;
}

int
envy_halt_input(void *self)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int st;

	st = bus_space_read_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_CTL);
	st &= ~ENVY_MT_CTL_RSTART(sc);
	bus_space_write_1(sc->mt_iot, sc->mt_ioh, ENVY_MT_CTL, st);
	return 0;
}

int
envy_getdev(void *self, struct audio_device *dev)
{
	struct envy_softc *sc = (struct envy_softc *)self;

	strlcpy(dev->name, sc->isht ? "Envy24HT" : "Envy24", MAX_AUDIO_DEV_LEN);
	strlcpy(dev->version, "-", MAX_AUDIO_DEV_LEN);
	strlcpy(dev->config, sc->card->name, MAX_AUDIO_DEV_LEN);
	return 0;
}

int
envy_query_devinfo(void *self, struct mixer_devinfo *dev)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int i, n, idx, ndev;
	char *classes[] = { 
		AudioCinputs, AudioCoutputs, AudioCmonitor 
	};

	if (dev->index < 0)
		return ENXIO;

	idx = dev->index;
	ndev = ENVY_MIX_NCLASS;
	dev->prev = dev->next = AUDIO_MIXER_LAST;

	/*
	 * classes
	 */
	if (idx < ndev) {
		dev->type = AUDIO_MIXER_CLASS;
		dev->mixer_class = idx;
		strlcpy(dev->label.name, classes[idx], MAX_AUDIO_DEV_LEN);
		return 0;
	}
	idx -= ndev;

	/*
	 * output.lineX_source
	 */
	ndev = sc->isht ? 0 : sc->card->noch;
	if (idx < ndev) {
		n = 0;
		dev->type = AUDIO_MIXER_ENUM;
		dev->mixer_class = ENVY_MIX_CLASSOUT;
		for (i = 0; i < sc->card->nich; i++) {
			dev->un.e.member[n].ord = n;
			snprintf(dev->un.e.member[n++].label.name,
			    MAX_AUDIO_DEV_LEN, AudioNline "%d", i);
		}
		dev->un.e.member[n].ord = n;
		snprintf(dev->un.e.member[n++].label.name, 
			 MAX_AUDIO_DEV_LEN, "play%d", idx);
		if (idx < 2) {
			dev->un.e.member[n].ord = n;
			snprintf(dev->un.e.member[n++].label.name, 
			    MAX_AUDIO_DEV_LEN, "mon%d", idx);
		}
		snprintf(dev->label.name, MAX_AUDIO_DEV_LEN, 
		    "line%u_" AudioNsource, idx);
		dev->un.s.num_mem = n;
		return 0;
	}
	idx -= ndev;

	/*
	 * envy monitor level
	 */
	ndev = sc->isht ? 0 : ENVY_MIX_NMONITOR;
	if (idx < ndev) {
		dev->type = AUDIO_MIXER_VALUE;
		dev->mixer_class = ENVY_MIX_CLASSMON;
		dev->un.v.delta = 2;
		dev->un.v.num_channels = 1;
		snprintf(dev->label.name, MAX_AUDIO_DEV_LEN, 
			 "%s%d", idx < 10 ? "play" : "rec", idx % 10);
		strlcpy(dev->un.v.units.name, AudioNvolume, MAX_AUDIO_DEV_LEN);
		return 0;
	}
	idx -= ndev;

	/*
	 * inputs.xxx
	 */
	ndev = sc->card->adc->ndev(sc);
	if (idx < ndev) {
		sc->card->adc->devinfo(sc, dev, idx);
		return 0;
	}
	idx -= ndev;

	/*
	 * outputs.xxx
	 */
	ndev = sc->card->dac->ndev(sc);
	if (idx < ndev) {
		sc->card->dac->devinfo(sc, dev, idx);
		return 0;
	}
	return ENXIO;
}

int
envy_get_port(void *self, struct mixer_ctrl *ctl)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int val, idx, ndev;

	if (ctl->dev < ENVY_MIX_NCLASS) {
		return EINVAL;
	}

	idx = ctl->dev - ENVY_MIX_NCLASS;
	ndev = sc->isht ? 0 : sc->card->noch;
	if (idx < ndev) {
		ctl->un.ord = envy_lineout_getsrc(sc, idx);
		if (ctl->un.ord >= ENVY_MIX_NOUTSRC)
			ctl->un.ord -= ENVY_MIX_NOUTSRC - sc->card->nich;
		return 0;
	}
	idx -= ndev;
	ndev = sc->isht ? 0 : ENVY_MIX_NMONITOR;
	if (idx < ndev) {
		envy_mon_getvol(sc, idx / 2, idx % 2, &val);
		ctl->un.value.num_channels = 1;
		ctl->un.value.level[0] = 2 * val;
		return 0;
	}
	idx -= ndev;
	ndev = sc->card->adc->ndev(sc);
	if (idx < ndev) {
		sc->card->adc->get(sc, ctl, idx);
		return 0;
	}
	idx -= ndev;
	ndev = sc->card->dac->ndev(sc);
	if (idx < ndev) {
		sc->card->dac->get(sc, ctl, idx);
		return 0;
	}
	return ENXIO;
}

int
envy_set_port(void *self, struct mixer_ctrl *ctl)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int maxsrc, val, idx, ndev;

	if (ctl->dev < ENVY_MIX_NCLASS) {
		return EINVAL;
	}
	
	idx = ctl->dev - ENVY_MIX_NCLASS;
	ndev = sc->isht ? 0 : sc->card->noch;
	if (idx < ndev) {
		maxsrc = sc->card->nich + 1;
		if (idx < 2) 
			maxsrc++;
		if (ctl->un.ord < 0 || ctl->un.ord >= maxsrc)
			return EINVAL;
		if (ctl->un.ord >= sc->card->nich)
			ctl->un.ord += ENVY_MIX_NOUTSRC - sc->card->nich;
		envy_lineout_setsrc(sc, idx, ctl->un.ord);
		return 0;
	}
	idx -= ndev;
	ndev = sc->isht ? 0 : ENVY_MIX_NMONITOR;
	if (idx < ndev) {
		if (ctl->un.value.num_channels != 1) {
			return EINVAL;
		}
		val = ctl->un.value.level[0] / 2;
		envy_mon_setvol(sc, idx / 2, idx % 2, val);
		return 0;
	}
	idx -= ndev;
	ndev = sc->card->adc->ndev(sc);
	if (idx < ndev)
		return sc->card->adc->set(sc, ctl, idx);
	idx -= ndev;
	ndev = sc->card->dac->ndev(sc);
	if (idx < ndev)
		return sc->card->dac->set(sc, ctl, idx);
	return ENXIO;
}

int
envy_get_props(void *self)
{
	return AUDIO_PROP_FULLDUPLEX | AUDIO_PROP_INDEPENDENT;
}
