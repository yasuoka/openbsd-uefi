/*	$OpenBSD: rbox.c,v 1.1 2005/01/14 22:39:26 miod Exp $	*/

/*
 * Copyright (c) 2005, Miodrag Vallat
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
 * from: Utah $Hdr: grf_rb.c 1.15 93/08/13$
 *
 *	@(#)grf_rb.c	8.4 (Berkeley) 1/12/94
 */

/*
 * Graphics routines for the Renaissance, HP98720 Graphics system.
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

#include <dev/cons.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#include <hp300/dev/diofbreg.h>
#include <hp300/dev/diofbvar.h>
#include <hp300/dev/rboxreg.h>

struct	rbox_softc {
	struct device	sc_dev;
	struct diofb	*sc_fb;
	struct diofb	sc_fb_store;
	int		sc_scode;
};

int	rbox_dio_match(struct device *, void *, void *);
void	rbox_dio_attach(struct device *, struct device *, void *);
int	rbox_intio_match(struct device *, void *, void *);
void	rbox_intio_attach(struct device *, struct device *, void *);

struct cfattach rbox_dio_ca = {
	sizeof(struct rbox_softc), rbox_dio_match, rbox_dio_attach
};

struct cfattach rbox_intio_ca = {
	sizeof(struct rbox_softc), rbox_intio_match, rbox_intio_attach
};

struct cfdriver rbox_cd = {
	NULL, "rbox", DV_DULL
};

int	rbox_reset(struct diofb *, int, struct diofbreg *);
void	rbox_setcolor(struct diofb *, u_int,
	    u_int8_t, u_int8_t, u_int8_t);
void	rbox_windowmove(struct diofb *, u_int16_t, u_int16_t,
	    u_int16_t, u_int16_t, u_int16_t, u_int16_t, int);

int	rbox_ioctl(void *, u_long, caddr_t, int, struct proc *);
void	rbox_burner(void *, u_int, u_int);

struct	wsdisplay_accessops	rbox_accessops = {
	rbox_ioctl,
	diofb_mmap,
	diofb_alloc_screen,
	diofb_free_screen,
	diofb_show_screen,
	NULL,	/* load_font */
	NULL,	/* scrollback */
	NULL,	/* getchar */
	rbox_burner
};

/*
 * Attachment glue
 */

int
rbox_intio_match(struct device *parent, void *match, void *aux)
{
	struct intio_attach_args *ia = aux;
	struct diofbreg *fbr;

	fbr = (struct diofbreg *)IIOV(GRFIADDR);

	if (badaddr((caddr_t)fbr))
		return (0);

	if (fbr->id == GRFHWID && fbr->id2 == GID_RENAISSANCE) {
		ia->ia_addr = (caddr_t)GRFIADDR;
		return (1);
	}

	return (0);
}

void
rbox_intio_attach(struct device *parent, struct device *self, void *aux)
{
	struct rbox_softc *sc = (struct rbox_softc *)self;
	struct diofbreg *fbr;

	fbr = (struct diofbreg *)IIOV(GRFIADDR);
	sc->sc_scode = -1;	/* XXX internal i/o */

	if (sc->sc_scode == conscode) {
		sc->sc_fb = &diofb_cn;
	} else {
		sc->sc_fb = &sc->sc_fb_store;
		rbox_reset(sc->sc_fb, sc->sc_scode, fbr);
	}

	diofb_end_attach(sc, &rbox_accessops, sc->sc_fb,
	    sc->sc_scode == conscode, 4 /* XXX */, NULL);
}

int
rbox_dio_match(struct device *parent, void *match, void *aux)
{
	struct dio_attach_args *da = aux;

	if (da->da_id == DIO_DEVICE_ID_FRAMEBUFFER &&
	    da->da_secid == DIO_DEVICE_SECID_RENAISSANCE)
		return (1);

	return (0);
}

void
rbox_dio_attach(struct device *parent, struct device *self, void *aux)
{
	struct rbox_softc *sc = (struct rbox_softc *)self;
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
		    rbox_reset(sc->sc_fb, sc->sc_scode, fbr) != 0) {
			printf(": can't map framebuffer\n");
			return;
		}
	}

	diofb_end_attach(sc, &rbox_accessops, sc->sc_fb,
	    sc->sc_scode == conscode, 4 /* XXX */, NULL);
}

/*
 * Initialize hardware and display routines.
 */
int
rbox_reset(struct diofb *fb, int scode, struct diofbreg *fbr)
{
	volatile struct rboxfb *rb = (struct rboxfb *)fbr;
	int rc;
	int i;

	if ((rc = diofb_fbinquire(fb, scode, fbr, 0x20000)) != 0)
		return (rc);

	fb->planes = 8;
	fb->planemask = (1 << fb->planes) - 1;

	rb_waitbusy(rb);

	rb->reset = GRFHWID;
	DELAY(1000);

	rb->interrupt = 0x04;
	rb->video_enable = 0x01;
	rb->drive = 0x01;
	rb->vdrive = 0x0;

	rb->opwen = 0xFF;

	fb->bmv = rbox_windowmove;
	diofb_fbsetup(fb);
	diofb_fontunpack(fb);

	rb_waitbusy(fb->regkva);

	/*
	 * Clear color map
	 */
	for (i = 0; i < 16; i++) {
		*(fb->regkva + 0x63c3 + i*4) = 0x0;
		*(fb->regkva + 0x6403 + i*4) = 0x0;
		*(fb->regkva + 0x6803 + i*4) = 0x0;
		*(fb->regkva + 0x6c03 + i*4) = 0x0;
		*(fb->regkva + 0x73c3 + i*4) = 0x0;
		*(fb->regkva + 0x7403 + i*4) = 0x0;
		*(fb->regkva + 0x7803 + i*4) = 0x0;
		*(fb->regkva + 0x7c03 + i*4) = 0x0;
	}

	rb->rep_rule = RBOX_DUALROP(RR_COPY);

	/*
	 * I cannot figure out how to make the blink planes stop. So, we
	 * must set both colormaps so that when the planes blink, and
	 * the secondary colormap is active, we still get text.
	 */
	CM1RED(fb)[0x00].value = 0x00;
	CM1GRN(fb)[0x00].value = 0x00;
	CM1BLU(fb)[0x00].value = 0x00;
	CM1RED(fb)[0x01].value = 0xFF;
	CM1GRN(fb)[0x01].value = 0xFF;
	CM1BLU(fb)[0x01].value = 0xFF;

	CM2RED(fb)[0x00].value = 0x00;
	CM2GRN(fb)[0x00].value = 0x00;
	CM2BLU(fb)[0x00].value = 0x00;
	CM2RED(fb)[0x01].value = 0xFF;
	CM2GRN(fb)[0x01].value = 0xFF;
	CM2BLU(fb)[0x01].value = 0xFF;

 	rb->blink = 0x00;
	rb->write_enable = 0x01;
	rb->opwen = 0x00;

	/*
	 * Enable display.
	 */
	rb->display_enable = 0x01;

	return (0);
}

int
rbox_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct diofb *fb = v;
	struct wsdisplay_fbinfo *wdf;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_UNKNOWN;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = fb->dheight;
		wdf->width = fb->dwidth;
		wdf->depth = fb->planes;
		wdf->cmsize = 1 << fb->planes;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = (fb->fbwidth * fb->planes) >> 3;
		break;
	case WSDISPLAYIO_GETCMAP:
	case WSDISPLAYIO_PUTCMAP:
		/* XXX TBD */
		break;
	case WSDISPLAYIO_GVIDEO:
	case WSDISPLAYIO_SVIDEO:
		break;
	default:
		return (-1);
	}

	return (0);
}

void
rbox_burner(void *v, u_int on, u_int flags)
{
	struct diofb *fb = v;
	volatile struct rboxfb *rb = (struct rboxfb *)fb->regkva;

	if (on) {
		rb->display_enable = 0x01;
	} else {
		rb->display_enable = 0x00;
	}
}

void
rbox_windowmove(struct diofb *fb, u_int16_t sx, u_int16_t sy,
    u_int16_t dx, u_int16_t dy, u_int16_t cx, u_int16_t cy, int rop)
{
	volatile struct rboxfb *rb = (struct rboxfb *)fb->regkva;

	rb_waitbusy(rb);
	rb->rep_rule = RBOX_DUALROP(rop);
	rb->source_y = sy;
	rb->source_x = sx;
	rb->dest_y = dy;
	rb->dest_x = dx;
	rb->wheight = cy;
	rb->wwidth  = cx;
	rb->wmove = 1;
}

/*
 * Renaissance console support
 */

int rbox_console_scan(int, caddr_t, void *);
cons_decl(rbox);

int
rbox_console_scan(int scode, caddr_t va, void *arg)
{
	struct diofbreg *fbr = (struct diofbreg *)va;
	struct consdev *cp = arg;
	u_char *dioiidev;
	int force = 0, pri;

	if (fbr->id != GRFHWID || fbr->id2 != GID_RENAISSANCE)
		return (0);

	pri = CN_INTERNAL;

#ifdef CONSCODE
	/*
	 * Raise our priority, if appropriate.
	 */
	if (scode == CONSCODE) {
		pri = CN_REMOTE;
		force = conforced = 1;
	}
#endif

	/* Only raise priority. */
	if (pri > cp->cn_pri)
		cp->cn_pri = pri;

	/*
	 * If our priority is higher than the currently-remembered
	 * console, stash our priority.
	 */
	if (((cn_tab == NULL) || (cp->cn_pri > cn_tab->cn_pri)) || force) {
		cn_tab = cp;
		if (scode >= DIOII_SCBASE) {
			dioiidev = (u_char *)va;
			return ((dioiidev[0x101] + 1) * 0x100000);
		}
		return (DIO_DEVSIZE);
	}
	return (0);
}

void
rboxcnprobe(struct consdev *cp)
{
	int maj;
	caddr_t va;
	struct diofbreg *fbr;
	int force = 0;

	/* Abort early if console is already forced. */
	if (conforced)
		return;

	for (maj = 0; maj < nchrdev; maj++) {
		if (cdevsw[maj].d_open == wsdisplayopen)
			break;
	}

	if (maj == nchrdev)
		return;

	cp->cn_dev = makedev(maj, 0);
	cp->cn_pri = CN_DEAD;

	/* Look for "internal" framebuffer. */
	va = (caddr_t)IIOV(GRFIADDR);
	fbr = (struct diofbreg *)va;
	if (!badaddr(va) &&
	    fbr->id == GRFHWID && fbr->id2 == GID_RENAISSANCE) {
		cp->cn_pri = CN_INTERNAL;

#ifdef CONSCODE
		if (CONSCODE == -1) {
			force = conforced = 1;
		}
#endif

		/*
		 * If our priority is higher than the currently
		 * remembered console, stash our priority, and
		 * unmap whichever device might be currently mapped.
		 * Since we're internal, we set the saved size to 0
		 * so they don't attempt to unmap our fixed VA later.
		 */
		if (((cn_tab == NULL) || (cp->cn_pri > cn_tab->cn_pri))
		    || force) {
			cn_tab = cp;
			if (convasize)
				iounmap(conaddr, convasize);
			conscode = -1;
			conaddr = va;
			convasize = 0;
		}
	}

	console_scan(rbox_console_scan, cp, HP300_BUS_DIO);
}

void
rboxcninit(struct consdev *cp)
{
	long defattr;

	rbox_reset(&diofb_cn, conscode, (struct diofbreg *)conaddr);
	diofb_alloc_attr(NULL, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&diofb_cn.wsd, &diofb_cn, 0, 0, defattr);
}
