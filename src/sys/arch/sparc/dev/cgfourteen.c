/*	$OpenBSD: cgfourteen.c,v 1.28 2005/03/07 16:44:50 miod Exp $	*/
/*	$NetBSD: cgfourteen.c,v 1.7 1997/05/24 20:16:08 pk Exp $ */

/*
 * Copyright (c) 2002, 2003 Miodrag Vallat.  All rights reserved.
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
 *
 * Copyright (c) 1996
 *	The President and Fellows of Harvard College. All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by Harvard University.
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed by Harvard University and
 *	its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *   Based on:
 *	NetBSD: cgthree.c,v 1.28 1996/05/31 09:59:22 pk Exp
 *	NetBSD: cgsix.c,v 1.25 1996/04/01 17:30:00 christos Exp
 */

/*
 * Driver for Campus-II on-board mbus-based video (cgfourteen).
 *
 * Does not handle interrupts, even though they can occur.
 *
 * XXX should defer colormap updates to vertical retrace interrupts
 *
 * XXX should bring hardware cursor code back
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

#include <sparc/dev/cgfourteenreg.h>

#include <dev/cons.h>	/* for prom console hook */

/*
 * per-display variables/state
 */
struct cgfourteen_softc {
	struct	sunfb sc_sunfb;		/* common base part */

	struct 	rom_reg	sc_phys;	/* phys address of frame buffer */
	union	cg14cmap sc_cmap;	/* current colormap */

	struct	cg14ctl  *sc_ctl; 	/* various registers */
	struct	cg14curs *sc_hwc;
	struct 	cg14dac	 *sc_dac;
	struct	cg14xlut *sc_xlut;
	struct 	cg14clut *sc_clut1;
	struct	cg14clut *sc_clut2;
	struct	cg14clut *sc_clut3;
	u_int	*sc_clutincr;

	int	sc_32;			/* can do 32bit at this resolution */
	size_t	sc_vramsize;		/* total video memory size */

	int	sc_nscreens;
};

int cgfourteen_ioctl(void *, u_long, caddr_t, int, struct proc *);
int cgfourteen_alloc_screen(void *, const struct wsscreen_descr *, void **,
    int *, int *, long *);
void cgfourteen_free_screen(void *, void *);
int cgfourteen_show_screen(void *, void *, int, void (*cb)(void *, int, int),
    void *);
paddr_t cgfourteen_mmap(void *, off_t, int);
void cgfourteen_reset(struct cgfourteen_softc *, int);
void cgfourteen_burner(void *, u_int, u_int);
void cgfourteen_prom(void *);

int  cgfourteen_getcmap(union cg14cmap *, struct wsdisplay_cmap *);
int  cgfourteen_putcmap(union cg14cmap *, struct wsdisplay_cmap *);
void cgfourteen_loadcmap(struct cgfourteen_softc *, int, int);
void cgfourteen_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);

struct wsdisplay_accessops cgfourteen_accessops = {
	cgfourteen_ioctl,
	cgfourteen_mmap,
	cgfourteen_alloc_screen,
	cgfourteen_free_screen,
	cgfourteen_show_screen,
	NULL,	/* load_font */
	NULL,	/* scrollback */
	NULL,	/* getchar */
	cgfourteen_burner,
};

void	cgfourteenattach(struct device *, struct device *, void *);
int	cgfourteenmatch(struct device *, void *, void *);

struct cfattach cgfourteen_ca = {
	sizeof(struct cgfourteen_softc), cgfourteenmatch, cgfourteenattach
};

struct cfdriver cgfourteen_cd = {
	NULL, "cgfourteen", DV_DULL
};

/*
 * Match a cgfourteen.
 */
int
cgfourteenmatch(parent, vcf, aux)
	struct device *parent;
	void *vcf, *aux;
{
	struct cfdata *cf = vcf;
	struct confargs *ca = aux;
	struct romaux *ra = &ca->ca_ra;

	if (strcmp(cf->cf_driver->cd_name, ra->ra_name))
		return (0);

	/*
	 * This driver should not be attached without an "addr" locator,
	 * as this is the only way to differentiate the main and secondary
	 * VSIMM.
	 */
	if (cf->cf_loc[0] != -1 && cf->cf_loc[0] != (int)ra->ra_paddr)
		return (0);

	/*
	 * The cgfourteen is a local-bus video adaptor, accessed directly
	 * via the processor, and not through device space or an external
	 * bus. Thus we look _only_ at the obio bus.
	 * Additionally, these things exist only on the Sun4m.
	 */
	if (CPU_ISSUN4M && ca->ca_bustype == BUS_OBIO)
		return (1);

	return (0);
}

/*
 * Attach a display.
 */
void
cgfourteenattach(parent, self, args)
	struct device *parent, *self;
	void *args;
{
	struct cgfourteen_softc *sc = (struct cgfourteen_softc *)self;
	struct confargs *ca = args;
	int node, i;
	u_int32_t *lut;
	int isconsole = 0;
	char *nam;

	printf(": ");
	node = ca->ca_ra.ra_node;
	nam = getpropstring(node, "model");
	if (*nam != '\0')
		printf("%s, ", nam);

	isconsole = node == fbnode;

	/*
	 * Sanity checks
	 */
	if (ca->ca_ra.ra_len < 0x10000) {
		printf("\n%s: expected %x bytes of control registers, got %x\n",
		    self->dv_xname, 0x10000, ca->ca_ra.ra_len);
		return;
	}
	if (ca->ca_ra.ra_nreg < CG14_NREG) {
		printf("\n%s: expected %d registers, got %d",
		    self->dv_xname, CG14_NREG, ca->ca_ra.ra_nreg);
		return;
	}

	printf("%dMB", ca->ca_ra.ra_reg[CG14_REG_VRAM].rr_len >> 20);

	/*
	 * Map in the 8 useful pages of registers
	 */
	sc->sc_ctl = (struct cg14ctl *) mapiodev(
	    &ca->ca_ra.ra_reg[CG14_REG_CONTROL], 0, ca->ca_ra.ra_len);

	sc->sc_hwc = (struct cg14curs *) ((u_int)sc->sc_ctl +
					  CG14_OFFSET_CURS);
	sc->sc_dac = (struct cg14dac *) ((u_int)sc->sc_ctl +
					 CG14_OFFSET_DAC);
	sc->sc_xlut = (struct cg14xlut *) ((u_int)sc->sc_ctl +
					   CG14_OFFSET_XLUT);
	sc->sc_clut1 = (struct cg14clut *) ((u_int)sc->sc_ctl +
					    CG14_OFFSET_CLUT1);
	sc->sc_clut2 = (struct cg14clut *) ((u_int)sc->sc_ctl +
					    CG14_OFFSET_CLUT2);
	sc->sc_clut3 = (struct cg14clut *) ((u_int)sc->sc_ctl +
					    CG14_OFFSET_CLUT3);
	sc->sc_clutincr = (u_int *) ((u_int)sc->sc_ctl +
				     CG14_OFFSET_CLUTINCR);

	/*
	 * Stash the physical address of the framebuffer for use by mmap
	 */
	sc->sc_phys = ca->ca_ra.ra_reg[CG14_REG_VRAM];

	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, node, ca->ca_bustype);

	/*
	 * The prom will report depth == 8, since this is the mode it will
	 * get initialized in.
	 * Check if we will be able to use 32 bit mode later (i.e. if it will
	 * fit in the video memory. Note that, if this is not the case, the
	 * VSIMM will usually not appear in the OBP device tree!
	 */
	sc->sc_vramsize = ca->ca_ra.ra_reg[CG14_REG_VRAM].rr_len;
	sc->sc_32 = sc->sc_sunfb.sf_fbsize * 4 <= sc->sc_vramsize;

	sc->sc_sunfb.sf_ro.ri_bits = mapiodev(&ca->ca_ra.ra_reg[CG14_REG_VRAM],
	    0,	/* CHUNKY_XBGR */
	    sc->sc_vramsize);

	printf(", %dx%d\n", sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

	/*
	 * Reset frame buffer controls
	 */
	sc->sc_sunfb.sf_depth = 0;	/* force action */
	cgfourteen_reset(sc, 8);

	/*
	 * Grab the initial colormap
	 */
	lut = (u_int32_t *) sc->sc_clut1->clut_lut;
	for (i = 0; i < CG14_CLUT_SIZE; i++)
		sc->sc_cmap.cm_chip[i] = lut[i];

	/*
	 * Enable the video.
	 */
	cgfourteen_burner(sc, 1, 0);

	sc->sc_sunfb.sf_ro.ri_hw = sc;
	fbwscons_init(&sc->sc_sunfb, isconsole ? 0 : RI_CLEAR);
	fbwscons_setcolormap(&sc->sc_sunfb, cgfourteen_setcolor);

	if (isconsole) {
		fbwscons_console_init(&sc->sc_sunfb, -1);
		shutdownhook_establish(cgfourteen_prom, sc);
	}

	fbwscons_attach(&sc->sc_sunfb, &cgfourteen_accessops, isconsole);
}

int
cgfourteen_ioctl(dev, cmd, data, flags, p)
	void *dev;
	u_long cmd;
	caddr_t data;
	int flags;
	struct proc *p;
{
	struct cgfourteen_softc *sc = dev;
	struct wsdisplay_cmap *cm;
	struct wsdisplay_fbinfo *wdf;
	int error;

	/*
	 * Note that, although the emulation (text) mode is running in a
	 * 8-bit plane, we advertize the frame buffer as 32-bit if it can
	 * support this mode.
	 */
	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		if (sc->sc_32)
			*(u_int *)data = WSDISPLAY_TYPE_SUN24;
		else
			*(u_int *)data = WSDISPLAY_TYPE_UNKNOWN;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width = sc->sc_sunfb.sf_width;
		wdf->depth = sc->sc_32 ? 32 : 8;
		wdf->cmsize = sc->sc_32 ? 0 : 256;
		break;
	case WSDISPLAYIO_LINEBYTES:
		if (sc->sc_32)
			*(u_int *)data = sc->sc_sunfb.sf_linebytes * 4;
		else
			*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;

	case WSDISPLAYIO_GETCMAP:
		cm = (struct wsdisplay_cmap *)data;
		error = cgfourteen_getcmap(&sc->sc_cmap, cm);
		if (error)
			return (error);
		break;

	case WSDISPLAYIO_PUTCMAP:
		cm = (struct wsdisplay_cmap *)data;
		error = cgfourteen_putcmap(&sc->sc_cmap, cm);
		if (error)
			return (error);
		/* XXX should use retrace interrupt */
		cgfourteen_loadcmap(sc, cm->index, cm->count);
		break;

	case WSDISPLAYIO_SMODE:
		if (*(int *)data == WSDISPLAYIO_MODE_EMUL) {
			/* Back from X11 to text mode */
			cgfourteen_reset(sc, 8);
		} else {
			/* Starting X11, try to switch to 32 bit mode */
			if (sc->sc_32)
				cgfourteen_reset(sc, 32);
		}
		break;

	default:
		return (-1);	/* not supported yet */
	}
	return (0);
}

int
cgfourteen_alloc_screen(v, type, cookiep, curxp, curyp, attrp)
	void *v;
	const struct wsscreen_descr *type;
	void **cookiep;
	int *curxp, *curyp;
	long *attrp;
{
	struct cgfourteen_softc *sc = v;

	if (sc->sc_nscreens > 0)
		return (ENOMEM);

	*cookiep = &sc->sc_sunfb.sf_ro;
	*curyp = 0;
	*curxp = 0;
	sc->sc_sunfb.sf_ro.ri_ops.alloc_attr(&sc->sc_sunfb.sf_ro,
	    WSCOL_BLACK, WSCOL_WHITE, WSATTR_WSCOLORS, attrp);
	sc->sc_nscreens++;
	return (0);
}

void
cgfourteen_free_screen(v, cookie)
	void *v;
	void *cookie;
{
	struct cgfourteen_softc *sc = v;

	sc->sc_nscreens--;
}

int
cgfourteen_show_screen(v, cookie, waitok, cb, cbarg)
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
cgfourteen_mmap(v, offset, prot)
	void *v;
	off_t offset;
	int prot;
{
	struct cgfourteen_softc *sc = v;
	
	if (offset & PGOFSET)
		return (-1);

	/* Allow mapping as a dumb framebuffer from offset 0 */
	if (offset >= 0 && offset < sc->sc_sunfb.sf_fbsize * 4) {
		return (REG2PHYS(&sc->sc_phys, offset) | PMAP_NC);
	}

	return (-1);
}

/* Initialize the framebuffer, storing away useful state for later reset */
void
cgfourteen_reset(sc, depth)
	struct cgfourteen_softc *sc;
	int depth;
{

	if (sc->sc_sunfb.sf_depth != depth) {
		if (depth == 8) {
			/*
			 * Enable the video and put it in 8 bit mode
			 */
			sc->sc_ctl->ctl_mctl = CG14_MCTL_ENABLEVID |
			    CG14_MCTL_PIXMODE_8 | CG14_MCTL_POWERCTL;

			fbwscons_setcolormap(&sc->sc_sunfb,
			    cgfourteen_setcolor);
		} else {
			/*
			 * Clear the screen to black
			 */
			bzero(sc->sc_sunfb.sf_ro.ri_bits,
			    sc->sc_sunfb.sf_fbsize);

			/*
			 * Enable the video, and put in 32 bit mode.
			 */
			sc->sc_ctl->ctl_mctl = CG14_MCTL_ENABLEVID |
			    CG14_MCTL_PIXMODE_32 | CG14_MCTL_POWERCTL;

			/*
			 * Zero the xlut to enable direct-color mode
			 */
			bzero(sc->sc_xlut, CG14_CLUT_SIZE);
		}
	}

	sc->sc_sunfb.sf_depth = depth;
}

void
cgfourteen_prom(v)
	void *v;
{
	struct cgfourteen_softc *sc = v;
	extern struct consdev consdev_prom;

	if (sc->sc_sunfb.sf_depth != 8) {
		/*
		 * Go back to 8-bit mode.
		 */
		cgfourteen_reset(sc, 8);

		/*
		 * Go back to prom output for the last few messages, so they
		 * will be displayed correctly.
		 */
		cn_tab = &consdev_prom;
	}
}

void
cgfourteen_burner(v, on, flags)
	void *v;
	u_int on, flags;
{
	struct cgfourteen_softc *sc = v;

	/*
	 * We can only use DPMS to power down the display if the chip revision
	 * is greater than 0.
	 */
	if (on) {
		if ((sc->sc_ctl->ctl_rsr & CG14_RSR_REVMASK) > 0)
			sc->sc_ctl->ctl_mctl |= (CG14_MCTL_ENABLEVID |
						 CG14_MCTL_POWERCTL);
		else
			sc->sc_ctl->ctl_mctl |= CG14_MCTL_ENABLEVID;
	} else {
		if ((sc->sc_ctl->ctl_rsr & CG14_RSR_REVMASK) > 0)
			sc->sc_ctl->ctl_mctl &= ~(CG14_MCTL_ENABLEVID |
						  CG14_MCTL_POWERCTL);
		else
			sc->sc_ctl->ctl_mctl &= ~CG14_MCTL_ENABLEVID;
	}
}

/* Read the software shadow colormap */
int
cgfourteen_getcmap(cm, rcm)
	union cg14cmap *cm;
	struct wsdisplay_cmap *rcm;
{
	u_int index = rcm->index, count = rcm->count, i;
	int error;

	if (index >= CG14_CLUT_SIZE || count > CG14_CLUT_SIZE - index)
                return (EINVAL);

	for (i = 0; i < count; i++) {
		if ((error = copyout(&cm->cm_map[index + i][3],
		    &rcm->red[i], 1)) != 0)
			return (error);
		if ((error = copyout(&cm->cm_map[index + i][2],
		    &rcm->green[i], 1)) != 0)
			return (error);
		if ((error = copyout(&cm->cm_map[index + i][1],
		    &rcm->blue[i], 1)) != 0)
			return (error);
	}
	return (0);
}

/* Write the software shadow colormap */
int
cgfourteen_putcmap(cm, rcm)
        union cg14cmap *cm;
        struct wsdisplay_cmap *rcm;
{
	u_int index = rcm->index, count = rcm->count, i;
	int error;

	if (index >= CG14_CLUT_SIZE || count > CG14_CLUT_SIZE - index)
                return (EINVAL);

	for (i = 0; i < count; i++) {
		if ((error = copyin(&rcm->red[i],
		    &cm->cm_map[index + i][3], 1)) != 0)
			return (error);
		if ((error = copyin(&rcm->green[i],
		    &cm->cm_map[index + i][2], 1)) != 0)
			return (error);
		if ((error = copyin(&rcm->blue[i],
		    &cm->cm_map[index + i][1], 1)) != 0)
			return (error);
		cm->cm_map[index +i][0] = 0;	/* no alpha channel */
	}
	return (0);
}

void
cgfourteen_loadcmap(sc, start, ncolors)
	struct cgfourteen_softc *sc;
	int start, ncolors;
{
	/* XXX switch to auto-increment, and on retrace intr */

	/* Setup pointers to source and dest */
	u_int32_t *colp = &sc->sc_cmap.cm_chip[start];
	volatile u_int32_t *lutp = &sc->sc_clut1->clut_lut[start];

	/* Copy by words */
	while (--ncolors >= 0)
		*lutp++ = *colp++;
}

void
cgfourteen_setcolor(v, index, r, g, b)
	void *v;
	u_int index;
	u_int8_t r, g, b;
{
	struct cgfourteen_softc *sc = v;

	/* XXX - Wait for retrace? */

	sc->sc_cmap.cm_map[index][3] = r;
	sc->sc_cmap.cm_map[index][2] = g;
	sc->sc_cmap.cm_map[index][1] = b;
	sc->sc_cmap.cm_map[index][0] = 0;	/* no alpha channel */
	
	cgfourteen_loadcmap(sc, index, 1);
}
