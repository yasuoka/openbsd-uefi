/*	$OpenBSD: topcat.c,v 1.17 2013/10/20 20:07:23 miod Exp $	*/

/*
 * Copyright (c) 2005, Miodrag Vallat.
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
 */
/*
 * Copyright (c) 1996 Jason R. Thorpe.  All rights reserved.
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 * from: Utah $Hdr: grf_tc.c 1.20 93/08/13$
 *
 *	@(#)grf_tc.c	8.4 (Berkeley) 1/12/94
 */

/*
 * Graphics routines for TOPCAT, CATSEYE and KATHMANDU frame buffers
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/ioctl.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>

#include <hp300/dev/dioreg.h>
#include <hp300/dev/diovar.h>
#include <hp300/dev/diodevs.h>
#include <hp300/dev/intiovar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <hp300/dev/diofbreg.h>
#include <hp300/dev/diofbvar.h>
#include <hp300/dev/topcatreg.h>

struct	topcat_softc {
	struct device	sc_dev;
	struct diofb	*sc_fb;
	struct diofb	sc_fb_store;
	int		sc_scode;
};

int	topcat_dio_match(struct device *, void *, void *);
void	topcat_dio_attach(struct device *, struct device *, void *);
int	topcat_intio_match(struct device *, void *, void *);
void	topcat_intio_attach(struct device *, struct device *, void *);

struct cfattach topcat_dio_ca = {
	sizeof(struct topcat_softc), topcat_dio_match, topcat_dio_attach
};

struct cfattach topcat_intio_ca = {
	sizeof(struct topcat_softc), topcat_intio_match, topcat_intio_attach
};

struct cfdriver topcat_cd = {
	NULL, "topcat", DV_DULL
};

void	topcat_end_attach(struct topcat_softc *, u_int8_t);
int	topcat_reset(struct diofb *, int, struct diofbreg *);
void	topcat_restore(struct diofb *);
int	topcat_setcmap(struct diofb *, struct wsdisplay_cmap *);
void	topcat_setcolor(struct diofb *, u_int);
int	topcat_windowmove(struct diofb *, u_int16_t, u_int16_t, u_int16_t,
	    u_int16_t, u_int16_t, u_int16_t, int16_t, int16_t);

int	topcat_ioctl(void *, u_long, caddr_t, int, struct proc *);
void	topcat_burner(void *, u_int, u_int);

struct	wsdisplay_accessops	topcat_accessops = {
	.ioctl = topcat_ioctl,
	.mmap = diofb_mmap,
	.alloc_screen = diofb_alloc_screen,
	.free_screen = diofb_free_screen,
	.show_screen = diofb_show_screen,
	.burn_screen = topcat_burner
};

/*
 * Attachment glue
 */

int
topcat_intio_match(struct device *parent, void *match, void *aux)
{
	struct intio_attach_args *ia = aux;
	struct diofbreg *fbr;

	fbr = (struct diofbreg *)IIOV(GRFIADDR);

	if (badaddr((caddr_t)fbr))
		return (0);

	if (fbr->id == GRFHWID) {
		switch (fbr->fbid) {
		case GID_TOPCAT:
		case GID_LRCATSEYE:
		case GID_HRCCATSEYE:
		case GID_HRMCATSEYE:
#if 0
		case GID_XXXCATSEYE:
#endif
			ia->ia_addr = (caddr_t)GRFIADDR;
			return (1);
		}
	}

	return (0);
}

void
topcat_intio_attach(struct device *parent, struct device *self, void *aux)
{
	struct topcat_softc *sc = (struct topcat_softc *)self;
	struct diofbreg *fbr;

	fbr = (struct diofbreg *)IIOV(GRFIADDR);
	sc->sc_scode = CONSCODE_INTERNAL;

	if (sc->sc_scode == conscode) {
		sc->sc_fb = &diofb_cn;
	} else {
		sc->sc_fb = &sc->sc_fb_store;
		topcat_reset(sc->sc_fb, sc->sc_scode, fbr);
	}

	topcat_end_attach(sc, fbr->fbid);
}

int
topcat_dio_match(struct device *parent, void *match, void *aux)
{
	struct dio_attach_args *da = aux;

	if (da->da_id == DIO_DEVICE_ID_FRAMEBUFFER) {
		switch (da->da_secid) {
		case DIO_DEVICE_SECID_TOPCAT:
		case DIO_DEVICE_SECID_LRCATSEYE:
		case DIO_DEVICE_SECID_HRCCATSEYE:
		case DIO_DEVICE_SECID_HRMCATSEYE:
#if 0
		case DIO_DEVICE_SECID_XXXCATSEYE:
#endif
			return (1);
		}
	}

	return (0);
}

void
topcat_dio_attach(struct device *parent, struct device *self, void *aux)
{
	struct topcat_softc *sc = (struct topcat_softc *)self;
	struct dio_attach_args *da = aux;
	struct diofbreg *fbr;

	sc->sc_scode = da->da_scode;
	if (sc->sc_scode == conscode) {
		fbr = (struct diofbreg *)conaddr;	/* already mapped */
		sc->sc_fb = &diofb_cn;
	} else {
		sc->sc_fb = &sc->sc_fb_store;
		fbr = (struct diofbreg *)
		    iomap(dio_scodetopa(sc->sc_scode), da->da_size);
		if (fbr == NULL ||
		    topcat_reset(sc->sc_fb, sc->sc_scode, fbr) != 0) {
			printf(": can't map framebuffer\n");
			return;
		}
	}

	topcat_end_attach(sc, fbr->fbid);
}

void
topcat_end_attach(struct topcat_softc *sc, u_int8_t id)
{
	const char *fbname = "unknown";

	switch (id) {
	case GID_TOPCAT:
		switch (sc->sc_fb->planes) {
		case 1:
			if (sc->sc_fb->dheight == 400)
				fbname = "HP98542 topcat";
			else
				fbname = "HP98544 topcat";
			break;
		case 4:
			if (sc->sc_fb->dheight == 400)
				fbname = "HP98543 topcat";
			else
				fbname = "HP98545 topcat";
			break;
		case 6:
			fbname = "HP98547 topcat";
			break;
		}
		break;
	case GID_HRCCATSEYE:
		fbname = "HP98550 catseye";	/* also A1416 kathmandu */
		break;
	case GID_LRCATSEYE:
		fbname = "HP98549 catseye";
		break;
	case GID_HRMCATSEYE:
		fbname = "HP98548 catseye";
		break;
	}

	diofb_end_attach(sc, &topcat_accessops, sc->sc_fb,
	    sc->sc_scode == conscode, fbname);
}

/*
 * Initialize hardware and display routines.
 */
int
topcat_reset(struct diofb *fb, int scode, struct diofbreg *fbr)
{
	volatile struct tcboxfb *tc = (struct tcboxfb *)fbr;
	int rc;
	u_int i;

	if ((rc = diofb_fbinquire(fb, scode, fbr)) != 0)
		return (rc);

	/*
	 * If we could not get a valid number of planes, determine it
	 * by writing to the first frame buffer display location,
	 * then reading it back.
	 */
	if (fb->planes == 0) {
		volatile u_int8_t *fbp;
		u_int8_t save;

		fbp = (u_int8_t *)fb->fbkva;
		tc->fben = ~0;
		tc->wen = ~0;
		tc->ren = ~0;
		tc->prr = RR_COPY;
		save = *fbp;
		*fbp = 0xff;
		fb->planemask = *fbp;
		*fbp = save;

		for (fb->planes = 1; fb->planemask >= (1 << fb->planes);
		    fb->planes++);
		if (fb->planes > 8)
			fb->planes = 8;
		fb->planemask = (1 << fb->planes) - 1;
	}

	fb->bmv = topcat_windowmove;
	topcat_restore(fb);
	diofb_fbsetup(fb);
	for (i = 0; i <= fb->planemask; i++)
		topcat_setcolor(fb, i);

	return (0);
}

void
topcat_restore(struct diofb *fb)
{
	volatile struct tcboxfb *tc = (struct tcboxfb *)fb->regkva;

	/*
	 * Catseye looks a lot like a topcat, but not completely.
	 * So, we set some bits to make it work.
	 */
	if (tc->regs.fbid != GID_TOPCAT) {
		while ((tc->catseye_status & 1))
			;
		tc->catseye_status = 0x0;
		tc->vb_select = 0x0;
		tc->tcntrl = 0x0;
		tc->acntrl = 0x0;
		tc->pncntrl = 0x0;
		tc->rug_cmdstat = 0x90;
	}

	/*
	 * Enable reading/writing of all the planes.
	 */
	tc->fben = fb->planemask;
	tc->wen  = fb->planemask;
	tc->ren  = fb->planemask;
	tc->prr  = RR_COPY;

	/* Enable display */
	tc->nblank = 0xff;
}

int
topcat_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct diofb *fb = v;
	struct wsdisplay_fbinfo *wdf;
	u_int i;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_TOPCAT;
		break;
	case WSDISPLAYIO_SMODE:
		fb->mapmode = *(u_int *)data;
		if (fb->mapmode == WSDISPLAYIO_MODE_EMUL) {
			topcat_restore(fb);
			for (i = 0; i <= fb->planemask; i++)
				topcat_setcolor(fb, i);
		}
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->width = fb->ri.ri_width;
		wdf->height = fb->ri.ri_height;
		wdf->depth = fb->ri.ri_depth;
		wdf->cmsize = 1 << fb->planes;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = fb->ri.ri_stride;
		break;
	case WSDISPLAYIO_GETCMAP:
		return (diofb_getcmap(fb, (struct wsdisplay_cmap *)data));
	case WSDISPLAYIO_PUTCMAP:
		return (topcat_setcmap(fb, (struct wsdisplay_cmap *)data));
	case WSDISPLAYIO_GVIDEO:
	case WSDISPLAYIO_SVIDEO:
		break;
	default:
		return (-1);
	}

	return (0);
}

void
topcat_burner(void *v, u_int on, u_int flags)
{
	struct diofb *fb = v;
	volatile struct tcboxfb *tc = (struct tcboxfb *)fb->regkva;

	if (on) {
		tc->nblank = 0xff;
	} else {
		tc->nblank = 0;
	}
}

void
topcat_setcolor(struct diofb *fb, u_int index)
{
	volatile struct tcboxfb *tc = (struct tcboxfb *)fb->regkva;

	/* Monochrome topcat may not have the colormap logic present */
	if (fb->planes <= 1)
		return;

	if (tc->regs.fbid != GID_TOPCAT) {
		tccm_waitbusy(tc);
		tc->plane_mask = 0xff;
		tc->cindex = ~index;
		tc->rdata  = fb->cmap.r[index];
		tc->gdata  = fb->cmap.g[index];
		tc->bdata  = fb->cmap.b[index];
		tc->strobe = 0xff;

		tccm_waitbusy(tc);
		tc->cindex = 0;
	} else {
		tccm_waitbusy(tc);
		tc->plane_mask = 0xff;
		tc->rdata  = fb->cmap.r[index];
		tc->gdata  = fb->cmap.g[index];
		tc->bdata  = fb->cmap.b[index];
		tc->cindex = ~index;
		tc->strobe = 0xff;

		tccm_waitbusy(tc);
		tc->rdata  = 0;
		tc->gdata  = 0;
		tc->bdata  = 0;
		tc->cindex = 0;
	}
}

int
topcat_setcmap(struct diofb *fb, struct wsdisplay_cmap *cm)
{
	u_int8_t r[256], g[256], b[256];
	u_int index = cm->index, count = cm->count;
	u_int colcount = 1 << fb->planes;
	int error;

	if (index >= colcount || count > colcount - index)
		return (EINVAL);

	if ((error = copyin(cm->red, r, count)) != 0)
		return (error);
	if ((error = copyin(cm->green, g, count)) != 0)
		return (error);
	if ((error = copyin(cm->blue, b, count)) != 0)
		return (error);

	bcopy(r, fb->cmap.r + index, count);
	bcopy(g, fb->cmap.g + index, count);
	bcopy(b, fb->cmap.b + index, count);

	while (count-- != 0)
		topcat_setcolor(fb, index++);

	return (0);
}

/*
 * Accelerated routines
 */

int
topcat_windowmove(struct diofb *fb, u_int16_t sx, u_int16_t sy,
    u_int16_t dx, u_int16_t dy, u_int16_t cx, u_int16_t cy, int16_t rop,
    int16_t planemask)
{
	volatile struct tcboxfb *tc = (struct tcboxfb *)fb->regkva;

	tc_waitbusy(tc, fb->planemask);

	tc->wen = planemask;
	tc->wmrr = rop;
	if (planemask != 0xff) {
		tc->wen = planemask ^ 0xff;
		tc->wmrr = rop ^ 0x0f;
		tc->wen = fb->planemask;
	}
	tc->source_y = sy;
	tc->source_x = sx;
	tc->dest_y = dy;
	tc->dest_x = dx;
	tc->wheight = cy;
	tc->wwidth = cx;
	tc->wmove = fb->planemask;

	tc_waitbusy(tc, fb->planemask);

	return (0);
}

/*
 * Topcat/catseye console support
 */

void
topcatcninit()
{
	topcat_reset(&diofb_cn, conscode, (struct diofbreg *)conaddr);
	diofb_cnattach(&diofb_cn);
}
