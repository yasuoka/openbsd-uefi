/*	$OpenBSD: bwtwo.c,v 1.10 2003/06/27 01:36:53 jason Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <dev/sbus/sbusvar.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wscons_raster.h>
#include <dev/rasops/rasops.h>
#include <machine/fbvar.h>

#define	BWTWO_CTRL_OFFSET	0x400000
#define	BWTWO_CTRL_SIZE	(sizeof(u_int32_t) * 8)
#define	BWTWO_VID_OFFSET	0x800000
#define	BWTWO_VID_SIZE	(1024 * 1024)

#define	FBC_CTRL	0x10		/* control */
#define	FBC_STAT	0x11		/* status */
#define	FBC_START	0x12		/* cursor start */
#define	FBC_END		0x13		/* cursor end */
#define	FBC_VCTRL	0x14		/* 12 bytes of timing goo */

#define	FBC_CTRL_IENAB		0x80	/* interrupt enable */
#define	FBC_CTRL_VENAB		0x40	/* video enable */
#define	FBC_CTRL_TIME		0x20	/* timing enable */
#define	FBC_CTRL_CURS		0x10	/* cursor compare enable */
#define	FBC_CTRL_XTAL		0x0c	/* xtal select (0,1,2,test): */
#define	FBC_CTRL_XTAL_0		0x00	/*  0 */
#define	FBC_CTRL_XTAL_1		0x04	/*  0 */
#define	FBC_CTRL_XTAL_2		0x08	/*  0 */
#define	FBC_CTRL_XTAL_TEST	0x0c	/*  0 */
#define	FBC_CTRL_DIV		0x03	/* divisor (1,2,3,4): */
#define	FBC_CTRL_DIV_1		0x00	/*  / 1 */
#define	FBC_CTRL_DIV_2		0x01	/*  / 2 */
#define	FBC_CTRL_DIV_3		0x02	/*  / 3 */
#define	FBC_CTRL_DIV_4		0x03	/*  / 4 */

#define	FBC_STAT_INTR		0x80	/* interrupt pending */
#define	FBC_STAT_RES		0x70	/* monitor sense: */
#define	FBC_STAT_RES_1024	0x10	/*  1024x768 */
#define	FBC_STAT_RES_1280	0x40	/*  1280x1024 */
#define	FBC_STAT_RES_1152	0x30	/*  1152x900 */
#define	FBC_STAT_RES_1152A	0x40	/*  1152x900x76, A */
#define	FBC_STAT_RES_1600	0x50	/*  1600x1200 */
#define	FBC_STAT_RES_1152B	0x60	/*  1152x900x86, B */
#define	FBC_STAT_ID		0x0f	/* id mask: */
#define	FBC_STAT_ID_COLOR	0x01	/*  color */
#define	FBC_STAT_ID_MONO	0x02	/*  monochrome */
#define	FBC_STAT_ID_MONOECL	0x03	/*  monochrome, ecl */

#define	FBC_READ(sc, reg) \
    bus_space_read_1((sc)->sc_bustag, (sc)->sc_ctrl_regs, (reg))
#define	FBC_WRITE(sc, reg, val) \
    bus_space_write_1((sc)->sc_bustag, (sc)->sc_ctrl_regs, (reg), (val))

struct bwtwo_softc {
	struct sunfb sc_sunfb;
	struct sbusdev sc_sd;
	bus_space_tag_t sc_bustag;
	bus_addr_t sc_paddr;
	bus_space_handle_t sc_ctrl_regs;
	bus_space_handle_t sc_vid_regs;
	int sc_nscreens;
};

struct wsscreen_descr bwtwo_stdscreen = {
	"std",
};

const struct wsscreen_descr *bwtwo_scrlist[] = {
	&bwtwo_stdscreen,
	/* XXX other formats? */
};

struct wsscreen_list bwtwo_screenlist = {
	sizeof(bwtwo_scrlist) / sizeof(struct wsscreen_descr *), bwtwo_scrlist
};

int bwtwo_ioctl(void *, u_long, caddr_t, int, struct proc *);
int bwtwo_alloc_screen(void *, const struct wsscreen_descr *, void **,
    int *, int *, long *);
void bwtwo_free_screen(void *, void *);
int bwtwo_show_screen(void *, void *, int, void (*cb)(void *, int, int),
    void *);
paddr_t bwtwo_mmap(void *, off_t, int);
int bwtwo_is_console(int);
void bwtwo_burner(void *, u_int, u_int);
void bwtwo_updatecursor(struct rasops_info *);

struct wsdisplay_accessops bwtwo_accessops = {
	bwtwo_ioctl,
	bwtwo_mmap,
	bwtwo_alloc_screen,
	bwtwo_free_screen,
	bwtwo_show_screen,
	NULL,	/* load_font */
	NULL,	/* scrollback */
	NULL,	/* getchar */
	bwtwo_burner,
};

int	bwtwomatch(struct device *, void *, void *);
void	bwtwoattach(struct device *, struct device *, void *);

struct cfattach bwtwo_ca = {
	sizeof (struct bwtwo_softc), bwtwomatch, bwtwoattach
};

struct cfdriver bwtwo_cd = {
	NULL, "bwtwo", DV_DULL
};

int
bwtwomatch(parent, vcf, aux)
	struct device *parent;
	void *vcf, *aux;
{
	struct cfdata *cf = vcf;
	struct sbus_attach_args *sa = aux;

	return (strcmp(cf->cf_driver->cd_name, sa->sa_name) == 0);
}

void    
bwtwoattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct bwtwo_softc *sc = (struct bwtwo_softc *)self;
	struct sbus_attach_args *sa = aux;
	struct wsemuldisplaydev_attach_args waa;
	int console;

	sc->sc_bustag = sa->sa_bustag;
	sc->sc_paddr = sbus_bus_addr(sa->sa_bustag, sa->sa_slot, sa->sa_offset);

	fb_setsize(&sc->sc_sunfb, 1, 1152, 900, sa->sa_node, 0);

	if (sa->sa_nreg != 1) {
		printf(": expected %d registers, got %d\n", 1, sa->sa_nreg);
		goto fail;
	}

	/*
	 * Map just CTRL and video RAM.
	 */
	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset + BWTWO_CTRL_OFFSET,
	    BWTWO_CTRL_SIZE, 0, 0, &sc->sc_ctrl_regs) != 0) {
		printf(": cannot map ctrl registers\n");
		goto fail_ctrl;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset + BWTWO_VID_OFFSET,
	    sc->sc_sunfb.sf_fbsize, BUS_SPACE_MAP_LINEAR,
	    0, &sc->sc_vid_regs) != 0) {
		printf(": cannot map vid registers\n");
		goto fail_vid;
	}

	console = bwtwo_is_console(sa->sa_node);

	sbus_establish(&sc->sc_sd, &sc->sc_sunfb.sf_dev);

	bwtwo_burner(sc, 1, 0);

	printf("\n");

	sc->sc_sunfb.sf_ro.ri_bits = (void *)bus_space_vaddr(sc->sc_bustag,
	    sc->sc_vid_regs);
	sc->sc_sunfb.sf_ro.ri_hw = sc;
	fbwscons_init(&sc->sc_sunfb, console ? 0 : RI_CLEAR);
	
	bwtwo_stdscreen.capabilities = sc->sc_sunfb.sf_ro.ri_caps;
	bwtwo_stdscreen.nrows = sc->sc_sunfb.sf_ro.ri_rows;
	bwtwo_stdscreen.ncols = sc->sc_sunfb.sf_ro.ri_cols;
	bwtwo_stdscreen.textops = &sc->sc_sunfb.sf_ro.ri_ops;

	if (console) {
		sc->sc_sunfb.sf_ro.ri_updatecursor = bwtwo_updatecursor;
		fbwscons_console_init(&sc->sc_sunfb, &bwtwo_stdscreen, -1,
		    bwtwo_burner);
	}

	waa.console = console;
	waa.scrdata = &bwtwo_screenlist;
	waa.accessops = &bwtwo_accessops;
	waa.accesscookie = sc;
	config_found(self, &waa, wsemuldisplaydevprint);

	return;

fail_vid:
	bus_space_unmap(sa->sa_bustag, sc->sc_ctrl_regs, BWTWO_CTRL_SIZE);
fail_ctrl:
fail:
;
}

int
bwtwo_ioctl(v, cmd, data, flags, p)
	void *v;
	u_long cmd;
	caddr_t data;
	int flags;
	struct proc *p;
{
	struct bwtwo_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_SUNBW;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width  = sc->sc_sunfb.sf_width;
		wdf->depth  = sc->sc_sunfb.sf_depth;
		wdf->cmsize = 0;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;

	case WSDISPLAYIO_GETCMAP:
	case WSDISPLAYIO_PUTCMAP:
		break;

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		return -1; /* not supported yet */
        }

	return (0);
}

int
bwtwo_alloc_screen(v, type, cookiep, curxp, curyp, attrp)
	void *v;
	const struct wsscreen_descr *type;
	void **cookiep;
	int *curxp, *curyp;
	long *attrp;
{
	struct bwtwo_softc *sc = v;

	if (sc->sc_nscreens > 0)
		return (ENOMEM);

	*cookiep = &sc->sc_sunfb.sf_ro;
	*curyp = 0;
	*curxp = 0;
	sc->sc_sunfb.sf_ro.ri_ops.alloc_attr(&sc->sc_sunfb.sf_ro,
	    0, 0, 0, attrp);
	sc->sc_nscreens++;
	return (0);
}

void
bwtwo_free_screen(v, cookie)
	void *v;
	void *cookie;
{
	struct bwtwo_softc *sc = v;

	sc->sc_nscreens--;
}

int
bwtwo_show_screen(v, cookie, waitok, cb, cbarg)
	void *v;
	void *cookie;
	int waitok;
	void (*cb)(void *, int, int);
	void *cbarg;
{
	return (0);
}

#define	START		(128 * 1024 + 128 * 1024)
#define	NOOVERLAY	(0x04000000)

paddr_t
bwtwo_mmap(v, offset, prot)
	void *v;
	off_t offset;
	int prot;
{
	struct bwtwo_softc *sc = v;

	if (offset & PGOFSET)
		return (-1);

	if (offset >= 0 && offset < sc->sc_sunfb.sf_fbsize)
		return (bus_space_mmap(sc->sc_bustag, sc->sc_paddr,
		    BWTWO_VID_OFFSET + offset, prot, BUS_SPACE_MAP_LINEAR));

	return (-1);
}

int
bwtwo_is_console(node)
	int node;
{
	extern int fbnode;

	return (fbnode == node);
}

void
bwtwo_burner(vsc, on, flags)
	void *vsc;
	u_int on, flags;
{
	struct bwtwo_softc *sc = vsc;
	int s;
	u_int8_t fbc;

	s = splhigh();
	fbc = FBC_READ(sc, FBC_CTRL);
	if (on)
		fbc |= FBC_CTRL_VENAB | FBC_CTRL_TIME;
	else {
		fbc &= ~FBC_CTRL_VENAB;
		if (flags & WSDISPLAY_BURN_VBLANK)
			fbc &= ~FBC_CTRL_TIME;
	}
	FBC_WRITE(sc, FBC_CTRL, fbc);
	splx(s);
}

void
bwtwo_updatecursor(ri)
	struct rasops_info *ri;
{
	struct bwtwo_softc *sc = ri->ri_hw;

	if (sc->sc_sunfb.sf_crowp != NULL)
		*sc->sc_sunfb.sf_crowp = ri->ri_crow;
	if (sc->sc_sunfb.sf_ccolp != NULL)
		*sc->sc_sunfb.sf_ccolp = ri->ri_ccol;
}
