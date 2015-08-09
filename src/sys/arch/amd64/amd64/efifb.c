/*	$OpenBSD$	*/

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
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

#include <uvm/uvm_extern.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <machine/biosvar.h>

int	 efifb_match(struct device *, void *, void *);
void	 efifb_attach(struct device *, struct device *, void *);
int	 efifb_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	 efifb_mmap(void *, off_t, int);
int	 efifb_alloc_screen(void *, const struct wsscreen_descr *, void **,
	    int *, int *, long *);
void	 efifb_free_screen(void *, void *);
int	 efifb_show_screen(void *, void *, int, void (*cb) (void *, int, int),
	    void *);
int	 efifb_list_font(void *, struct wsdisplay_font *);
int	 efifb_load_font(void *, void *, struct wsdisplay_font *);

struct efifb {
	struct rasops_info	 rinfo;
	bus_space_tag_t		 iot;
};

struct efifb_softc {
	struct device	 sc_dev;
	struct efifb	*sc_fb;
	int		 sc_nscr;
};

struct cfattach efifb_ca = {
	sizeof(struct efifb_softc), efifb_match, efifb_attach, NULL
};

struct wsscreen_descr efifb_std_descr = { "efi" };

const struct wsscreen_descr *efifb_descrs[] = {
	&efifb_std_descr
};

const struct wsscreen_list efifb_screen_list = {
	nitems(efifb_descrs), efifb_descrs
};

struct wsdisplay_accessops efifb_accessops = {
	.ioctl = efifb_ioctl,
	.mmap = efifb_mmap,
	.alloc_screen = efifb_alloc_screen,
	.free_screen = efifb_free_screen,
	.show_screen = efifb_show_screen,
	.load_font = efifb_load_font,
	.list_font = efifb_list_font
};

struct cfdriver efifb_cd = {
	NULL, "efifb", DV_DULL
};

struct efifb efifb_console;



int
efifb_match(struct device *parent, void *cf, void *aux)
{
	return (1);
}

void
efifb_attach(struct device *parent, struct device *self, void *aux)
{
	struct efifb_softc	*sc = (struct efifb_softc *)self;
	struct wsemuldisplaydev_attach_args
				 aa;

	printf("\n");
	if (1) {
		aa.console = 1;	/* XXX */
		sc->sc_fb = &efifb_console;
	}
	aa.scrdata = &efifb_screen_list;
	aa.accessops = &efifb_accessops;
	aa.accesscookie = sc;
	aa.defaultscreens = 0;

	config_found(self, &aa, wsemuldisplaydevprint);
}






int
efifb_cnattach(void)
{
	extern bios_efifb_t	*bios_efifb;
	struct efifb		*fb = &efifb_console;
	struct rasops_info	*ri = &fb->rinfo;
	long			 defattr = 0;

	if (bios_efifb == NULL)
		return (-1);

	memset(&efifb_console, 0, sizeof(efifb_console));

	fb->iot = X86_BUS_SPACE_MEM;
	ri->ri_bits = (u_char *)PMAP_DIRECT_MAP(bios_efifb->fb_addr);

	ri->ri_width = bios_efifb->fb_width;
	ri->ri_height = bios_efifb->fb_height;
	ri->ri_depth = bios_efifb->fb_depth;
	ri->ri_stride = bios_efifb->fb_pixpsl * (ri->ri_depth / 8);

	rasops_init(ri, bios_efifb->fb_height / 8, bios_efifb->fb_width / 8);

	efifb_std_descr.ncols = ri->ri_cols;
	efifb_std_descr.nrows = ri->ri_rows;
	efifb_std_descr.textops = &ri->ri_ops;
	efifb_std_descr.fontwidth = ri->ri_font->fontwidth;
	efifb_std_descr.fontheight = ri->ri_font->fontheight;

	ri->ri_ops.alloc_attr(ri, 0, 0, 0, &defattr);

	wsdisplay_cnattach(&efifb_std_descr, fb, 0, 0, defattr);

	return (0);
}

int
efifb_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct efifb_softc	*sc = v;
	struct efifb		*fb = sc->sc_fb;
	struct rasops_info 	*ri = &fb->rinfo;
	struct wsdisplay_fbinfo	*wdf;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_EFIFB	;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->cmsize = 256;	/* XXX? */
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = ri->ri_stride;
		break;
	default:
		return (-1);
	}

	return (0);
}

paddr_t
efifb_mmap(void *v, off_t off, int prot)
{
	struct efifb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_fb->rinfo;

	return bus_space_mmap(sc->sc_fb->iot,
	    (bus_addr_t)ri->ri_bits, off, prot, 0);
}

int
efifb_alloc_screen(void *v, const struct wsscreen_descr *descr,
    void **cookiep, int *curxp, int *curyp, long *attrp)
{
	struct efifb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_fb->rinfo;

	if (sc->sc_nscr > 0)
		return (ENOMEM);

	*cookiep = ri;
	*curxp = *curyp = 0;
	ri->ri_ops.alloc_attr(ri, 0, 0, 0, attrp);
	sc->sc_nscr++;

	return 0;
}

void
efifb_free_screen(void *v, void *cookiep)
{
	struct efifb_softc	*sc = v;

	sc->sc_nscr--;
}

int
efifb_show_screen(void *v, void *cookie, int waitok,
    void (*cb) (void *, int, int), void *cb_arg)
{
	return 0;
}

int
efifb_load_font(void *v, void *cookie, struct wsdisplay_font *font)
{
	struct efifb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_fb->rinfo;

	return (rasops_load_font(ri, cookie, font));
}

int
efifb_list_font(void *v, struct wsdisplay_font *font)
{
	struct efifb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_fb->rinfo;

	return (rasops_list_font(ri, font));
}
