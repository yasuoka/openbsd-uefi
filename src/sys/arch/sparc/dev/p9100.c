/*	$OpenBSD: p9100.c,v 1.16 2003/06/05 22:32:09 miod Exp $	*/

/*
 * Copyright (c) 1999 Jason L. Wright (jason@thought.net)
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

/*
 * color display (p9100) driver.  Based on cgthree.c and the NetBSD
 * p9100 driver.
 *
 * Does not handle interrupts, even though they can occur.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/tty.h>
#include <sys/conf.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/pmap.h>
#include <machine/cpu.h>
#include <machine/conf.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wscons_raster.h>
#include <dev/rasops/rasops.h>
#include <machine/fbvar.h>

#include <sparc/dev/btreg.h>
#include <sparc/dev/btvar.h>
#include <sparc/dev/sbusvar.h>

#include "tctrl.h"
#if NTCTRL > 0
#include <sparc/dev/tctrlvar.h>
#endif

/* per-display variables */
struct p9100_softc {
	struct	sunfb sc_sunfb;		/* common base part */
	struct	sbusdev sc_sd;		/* sbus device */
	struct	rom_reg	sc_phys;	/* phys address description */
	struct	p9100_cmd *sc_cmd;	/* command registers (dac, etc) */
	struct	p9100_ctl *sc_ctl;	/* control registers (draw engine) */
	union	bt_cmap sc_cmap;	/* Brooktree color map */
	u_int32_t	sc_junk;	/* throwaway value */
	int	sc_nscreens;
};

struct wsscreen_descr p9100_stdscreen = {
	"std",
};

const struct wsscreen_descr *p9100_scrlist[] = {
	&p9100_stdscreen,
};

struct wsscreen_list p9100_screenlist = {
	sizeof(p9100_scrlist) / sizeof(struct wsscreen_descr *),
	    p9100_scrlist
};

int p9100_ioctl(void *, u_long, caddr_t, int, struct proc *);
int p9100_alloc_screen(void *, const struct wsscreen_descr *, void **,
    int *, int *, long *);
void p9100_free_screen(void *, void *);
int p9100_show_screen(void *, void *, int, void (*cb)(void *, int, int),
    void *);
paddr_t p9100_mmap(void *, off_t, int);
void p9100_loadcmap(struct p9100_softc *, u_int, u_int);
void p9100_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);
void p9100_burner(void *, u_int, u_int);

struct wsdisplay_accessops p9100_accessops = {
	p9100_ioctl,
	p9100_mmap,
	p9100_alloc_screen,
	p9100_free_screen,
	p9100_show_screen,
	NULL,	/* load_font */
	NULL,	/* scrollback */
	NULL,	/* getchar */
	p9100_burner,
};

int	p9100match(struct device *, void *, void *);
void	p9100attach(struct device *, struct device *, void *);

struct cfattach pnozz_ca = {
	sizeof (struct p9100_softc), p9100match, p9100attach
};

struct cfdriver pnozz_cd = {
	NULL, "pnozz", DV_DULL
};

/*
 * SBus registers mappings
 */
#define	P9100_NREG	3
#define	P9100_REG_CTL	0
#define	P9100_REG_CMD	1
#define	P9100_REG_VRAM	2

/*
 * System control and command registers
 * (IBM RGB528 RamDac, p9100, video coprocessor)
 */
struct p9100_ctl {
	/* System control registers: 0x0000 - 0x00ff */
	struct p9100_scr {
		volatile u_int32_t	unused0;
		volatile u_int32_t	scr;		/* system config reg */
#define	SCR_ID_MASK		0x00000007
#define	SCR_PIXEL_ID_MASK	0x00000007
#define	SCR_PIXEL_MASK		0x1c000000
#define	SCR_PIXEL_8BPP		0x08000000
#define	SCR_PIXEL_16BPP		0x0c000000
#define	SCR_PIXEL_24BPP		0x1c000000
#define	SCR_PIXEL_32BPP		0x14000000
		volatile u_int32_t	ir;		/* interrupt reg */
		volatile u_int32_t	ier;		/* interrupt enable */
		volatile u_int32_t	arbr;		/* alt read bank reg */
		volatile u_int32_t	awbr;		/* alt write bank reg */
		volatile u_int32_t	unused1[58];
	} ctl_scr;

	/* Video control registers: 0x0100 - 0x017f */
	struct p9100_vcr {
		volatile u_int32_t	unused0;
		volatile u_int32_t	hcr;		/* horizontal cntr */
		volatile u_int32_t	htr;		/* horizontal total */
		volatile u_int32_t	hsre;		/* horiz sync rising */
		volatile u_int32_t	hbre;		/* horiz blank rising */
		volatile u_int32_t	hbfe;		/* horiz blank fallng */
		volatile u_int32_t	hcp;		/* horiz cntr preload */
		volatile u_int32_t	vcr;		/* vertical cntr */
		volatile u_int32_t	vl;		/* vertical length */
		volatile u_int32_t	vsre;		/* vert sync rising */
		volatile u_int32_t	vbre;		/* vert blank rising */
		volatile u_int32_t	vbfe;		/* vert blank fallng */
		volatile u_int32_t	vcp;		/* vert cntr preload */
		volatile u_int32_t	sra;		/* scrn repaint addr */
		volatile u_int32_t	srtc1;		/* scrn rpnt time 1 */
		volatile u_int32_t	qsf;		/* qsf counter */
		volatile u_int32_t	srtc2;		/* scrn rpnt time 2 */
		volatile u_int32_t	unused1[15];
	} ctl_vcr;

	/* VRAM control registers: 0x0180 - 0x1ff */
	struct p9100_vram {
		volatile u_int32_t	unused0;
		volatile u_int32_t	mc;		/* memory config */
		volatile u_int32_t	rp;		/* refresh period */
		volatile u_int32_t	rc;		/* refresh count */
		volatile u_int32_t	rasmax;		/* ras low maximum */
		volatile u_int32_t	rascur;		/* ras low current */
		volatile u_int32_t	dacfifo;	/* free fifo */
		volatile u_int32_t	unused1[25];
	} ctl_vram;

	/* IBM RGB528 RAMDAC registers: 0x0200 - 0x3ff */
	struct p9100_dac {
		volatile u_int32_t	pwraddr;	/* wr palette address */
		volatile u_int32_t	paldata;	/* palette data */
		volatile u_int32_t	pixmask;	/* pixel mask */
		volatile u_int32_t	prdaddr;	/* rd palette address */
		volatile u_int32_t	idxlow;		/* reg index low */
		volatile u_int32_t	idxhigh;	/* reg index high */
		volatile u_int32_t	regdata;	/* register data */
		volatile u_int32_t	idxctrl;	/* index control */
		volatile u_int32_t	unused1[120];
	} ctl_dac;

	/* Video coprocessor interface: 0x0400 - 0x1fff */
	volatile u_int32_t	ctl_vci[768];
};

#define	SRTC1_VIDEN	0x00000020


/*
 * Select the appropriate register group within the control registers
 * (must be done before any write to a register within the group, but
 * subsequent writes to the same group do not need to reselect).
 */
#define	P9100_SELECT_SCR(sc)	((sc)->sc_junk = (sc)->sc_ctl->ctl_scr.scr)
#define	P9100_SELECT_VCR(sc)	((sc)->sc_junk = (sc)->sc_ctl->ctl_vcr.hcr)
#define	P9100_SELECT_VRAM(sc)	((sc)->sc_junk = (sc)->sc_ctl->ctl_vram.mc)
#define	P9100_SELECT_DAC(sc)	((sc)->sc_junk = (sc)->sc_ctl->ctl_dac.pwraddr)
#define	P9100_SELECT_VCI(sc)	((sc)->sc_junk = (sc)->sc_ctl->ctl_vci[0])

/*
 * For some reason, every write to a DAC register needs to be followed by a
 * read from the ``free fifo number'' register, supposedly to have the write
 * take effect faster...
 */
#define	P9100_FLUSH_DAC(sc) \
	do { \
		P9100_SELECT_VRAM(sc); \
		(sc)->sc_junk = (sc)->sc_ctl->ctl_vram.dacfifo; \
	} while (0)

/*
 * Drawing engine
 */
struct p9100_cmd {
	volatile u_int32_t	cmd_regs[0x800];
};

int
p9100match(parent, vcf, aux)
	struct device *parent;
	void *vcf, *aux;
{
	struct cfdata *cf = vcf;
	struct confargs *ca = aux;
	struct romaux *ra = &ca->ca_ra;

	/*
	 * Mask out invalid flags from the user.
	 */
	cf->cf_flags &= FB_USERMASK;

	if (strcmp("p9100", ra->ra_name))
		return (0);

	return (1);
}

/*
 * Attach a display.
 */
void
p9100attach(parent, self, args)
	struct device *parent, *self;
	void *args;
{
	struct p9100_softc *sc = (struct p9100_softc *)self;
	struct confargs *ca = args;
	struct wsemuldisplaydev_attach_args waa;
	int node, row, scr;
	int isconsole, fb_depth;

#ifdef DIAGNOSTIC
	if (ca->ca_ra.ra_nreg < P9100_NREG) {
		printf(": expected %d registers, got only %d\n",
		    P9100_NREG, ca->ca_ra.ra_nreg);
		return;
	}
#endif

	sc->sc_sunfb.sf_flags = self->dv_cfdata->cf_flags;
	sc->sc_phys = ca->ca_ra.ra_reg[P9100_REG_VRAM];

	sc->sc_ctl = mapiodev(&(ca->ca_ra.ra_reg[P9100_REG_CTL]), 0,
	    ca->ca_ra.ra_reg[0].rr_len);
	sc->sc_cmd = mapiodev(&(ca->ca_ra.ra_reg[P9100_REG_CMD]), 0,
	    ca->ca_ra.ra_reg[1].rr_len);

	node = ca->ca_ra.ra_node;
	isconsole = node == fbnode;

	P9100_SELECT_SCR(sc);
	scr = sc->sc_ctl->ctl_scr.scr;
	switch (scr & SCR_PIXEL_MASK) {
	case SCR_PIXEL_32BPP:
		fb_depth = 32;
		break;
	case SCR_PIXEL_24BPP:
		fb_depth = 24;
		break;
	case SCR_PIXEL_16BPP:
		fb_depth = 16;
		break;
	default:
#ifdef DIAGNOSTIC
		printf(": unknown color depth code 0x%x, assuming 8\n%s",
		    scr & SCR_PIXEL_MASK, self->dv_xname);
#endif
	case SCR_PIXEL_8BPP:
		fb_depth = 8;
		break;
	}
	fb_setsize(&sc->sc_sunfb, fb_depth, 800, 600, node, ca->ca_bustype);
	sc->sc_sunfb.sf_ro.ri_bits = mapiodev(&sc->sc_phys, 0,
	    round_page(sc->sc_sunfb.sf_fbsize));
	sc->sc_sunfb.sf_ro.ri_hw = sc;

	printf(": rev %x, %dx%d, depth %d\n", scr & SCR_ID_MASK,
	    sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height,
	    sc->sc_sunfb.sf_depth);

	/*
	 * If the framebuffer width is under 1024x768, we will switch from the
	 * PROM font to the more adequate 8x16 font here.
	 * However, we need to adjust two things in this case:
	 * - the display row should be overrided from the current PROM metrics,
	 *   to prevent us from overwriting the last few lines of text.
	 * - if the 80x34 screen would make a large margin appear around it,
	 *   choose to clear the screen rather than keeping old prom output in
	 *   the margins.
	 * XXX there should be a rasops "clear margins" feature
	 */
	fbwscons_init(&sc->sc_sunfb,
	    isconsole && (sc->sc_sunfb.sf_width != 800));
	fbwscons_setcolormap(&sc->sc_sunfb, p9100_setcolor);

	p9100_stdscreen.capabilities = sc->sc_sunfb.sf_ro.ri_caps;
	p9100_stdscreen.nrows = sc->sc_sunfb.sf_ro.ri_rows;
	p9100_stdscreen.ncols = sc->sc_sunfb.sf_ro.ri_cols;
	p9100_stdscreen.textops = &sc->sc_sunfb.sf_ro.ri_ops;

	sbus_establish(&sc->sc_sd, &sc->sc_sunfb.sf_dev);

	/* enable video */
	p9100_burner(sc, 1, 0);

	if (isconsole) {
		if (sc->sc_sunfb.sf_width == 800)
			row = 0;	/* screen has been cleared above */
		else
			row = -1;

		fbwscons_console_init(&sc->sc_sunfb, &p9100_stdscreen, row,
		    p9100_burner);
	}

	waa.console = isconsole;
	waa.scrdata = &p9100_screenlist;
	waa.accessops = &p9100_accessops;
	waa.accesscookie = sc;
	config_found(self, &waa, wsemuldisplaydevprint);
}

int
p9100_ioctl(v, cmd, data, flags, p)
	void *v;
	u_long cmd;
	caddr_t data;
	int flags;
	struct proc *p;
{
	struct p9100_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;
	struct wsdisplay_cmap *cm;
#if NTCTRL > 0
	struct wsdisplay_param *dp;
#endif
	int error;

	switch (cmd) {

	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_SB_P9100;
		break;

	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width  = sc->sc_sunfb.sf_width;
		wdf->depth  = sc->sc_sunfb.sf_depth;
		wdf->cmsize = 256;
		break;

	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;

	case WSDISPLAYIO_GETCMAP:
		cm = (struct wsdisplay_cmap *)data;
		error = bt_getcmap(&sc->sc_cmap, cm);
		if (error)
			return (error);
		break;

	case WSDISPLAYIO_PUTCMAP:
		cm = (struct wsdisplay_cmap *)data;
		error = bt_putcmap(&sc->sc_cmap, cm);
		if (error)
			return (error);
		p9100_loadcmap(sc, cm->index, cm->count);
		break;

#if NTCTRL > 0
	case WSDISPLAYIO_GETPARAM:
		dp = (struct wsdisplay_param *)data;

		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			dp->min = 0;
			dp->max = 255;
			dp->curval = tadpole_get_brightness();
			break;
		case WSDISPLAYIO_PARAM_BACKLIGHT:
			dp->min = 0;
			dp->max = 1;
			dp->curval = tadpole_get_video();
			break;
		default:
			return (-1);
		}
		break;

	case WSDISPLAYIO_SETPARAM:
		dp = (struct wsdisplay_param *)data;

		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			tadpole_set_brightness(dp->curval);
			break;
		case WSDISPLAYIO_PARAM_BACKLIGHT:
			tadpole_set_video(dp->curval);
			break;
		default:
			return (-1);
		}
		break;
#endif	/* NTCTRL > 0 */

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		return (-1);	/* not supported yet */
	}

	return (0);
}

int
p9100_alloc_screen(v, type, cookiep, curxp, curyp, attrp)
	void *v;
	const struct wsscreen_descr *type;
	void **cookiep;
	int *curxp, *curyp;
	long *attrp;
{
	struct p9100_softc *sc = v;

	if (sc->sc_nscreens > 0)
		return (ENOMEM);

	*cookiep = &sc->sc_sunfb.sf_ro;
	*curyp = 0;
	*curxp = 0;
	if (sc->sc_sunfb.sf_depth == 8) {
		sc->sc_sunfb.sf_ro.ri_ops.alloc_attr(&sc->sc_sunfb.sf_ro,
		    WSCOL_BLACK, WSCOL_WHITE, WSATTR_WSCOLORS, attrp);
	} else {
		sc->sc_sunfb.sf_ro.ri_ops.alloc_attr(&sc->sc_sunfb.sf_ro,
		    0, 0, 0, attrp);
	}
	sc->sc_nscreens++;
	return (0);
}

void
p9100_free_screen(v, cookie)
	void *v;
	void *cookie;
{
	struct p9100_softc *sc = v;

	sc->sc_nscreens--;
}

int
p9100_show_screen(v, cookie, waitok, cb, cbarg)
	void *v;
	void *cookie;
	int waitok;
	void (*cb)(void *, int, int);
	void *cbarg;
{
	return (0);
}

/*
 * Return the address that would map the given device at the given
 * offset, allowing for the given protection, or return -1 for error.
 */
paddr_t
p9100_mmap(v, offset, prot)
	void *v;
	off_t offset;
	int prot;
{
	struct p9100_softc *sc = v;

	if (offset & PGOFSET)
		return (-1);

	if (offset >= 0 && offset < sc->sc_sunfb.sf_fbsize) {
		return (REG2PHYS(&sc->sc_phys, offset) | PMAP_NC);
	}

	return (-1);
}

void
p9100_setcolor(v, index, r, g, b)
	void *v;
	u_int index;
	u_int8_t r, g, b;
{
	struct p9100_softc *sc = v;
	union bt_cmap *bcm = &sc->sc_cmap;

	bcm->cm_map[index][0] = r;
	bcm->cm_map[index][1] = g;
	bcm->cm_map[index][2] = b;
	p9100_loadcmap(sc, index, 1);
}

void
p9100_loadcmap(sc, start, ncolors)
	struct p9100_softc *sc;
	u_int start, ncolors;
{
	u_char *p;

	P9100_SELECT_DAC(sc);
	sc->sc_ctl->ctl_dac.pwraddr = start << 16;
	P9100_FLUSH_DAC(sc);

	for (p = sc->sc_cmap.cm_map[start], ncolors *= 3; ncolors-- > 0; p++) {
		P9100_SELECT_DAC(sc);
		sc->sc_ctl->ctl_dac.paldata = (*p) << 16;
		P9100_FLUSH_DAC(sc);
	}
}

void
p9100_burner(v, on, flags)
	void *v;
	u_int on, flags;
{
	struct p9100_softc *sc = v;
	u_int32_t vcr;
	int s;

	s = splhigh();
	P9100_SELECT_VCR(sc);
	vcr = sc->sc_ctl->ctl_vcr.srtc1;
	if (on)
		vcr |= SRTC1_VIDEN;
	else
		vcr &= ~SRTC1_VIDEN;
	/* XXX - what about WSDISPLAY_BURN_VBLANK? */
	sc->sc_ctl->ctl_vcr.srtc1 = vcr;
#if NTCTRL > 0
	tadpole_set_video(on);
#endif
	splx(s);
}
