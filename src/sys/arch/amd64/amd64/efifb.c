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
};

struct efifb_softc {
	struct device	 sc_dev;
	struct efifb	*sc_fb;
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
	extern bios_efiinfo_t	*bios_efiinfo;

	if (bios_efiinfo != NULL)
		return (1);

	return (0);
}

void
efifb_attach(struct device *parent, struct device *self, void *aux)
{
	struct efifb_softc	*sc = (struct efifb_softc *)self;
	struct wsemuldisplaydev_attach_args
				 aa;
	struct rasops_info 	*ri;
	int			 ccol, crow;
	long			 defattr;

	printf("\n");
	if (1) {
		aa.console = 1;	/* XXX */
		sc->sc_fb = &efifb_console;
		ri = &sc->sc_fb->rinfo;
	}
	aa.scrdata = &efifb_screen_list;
	aa.accessops = &efifb_accessops;
	aa.accesscookie = sc;
	aa.defaultscreens = 0;

	ccol = ri->ri_ccol;
	crow = ri->ri_crow;

	ri->ri_flg = RI_VCONS;
	rasops_init(ri, 160, 160);

	ri->ri_ops.alloc_attr(ri->ri_active, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&efifb_std_descr, ri->ri_active, ccol, crow, defattr);

	config_found(self, &aa, wsemuldisplaydevprint);
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
		*(u_int *)data = WSDISPLAY_TYPE_EFIFB;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->cmsize = 0;	/* color map is unavailable */
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = ri->ri_stride;
		break;
	case WSDISPLAYIO_SMODE:
		break;
	case WSDISPLAYIO_GETSUPPORTEDDEPTH:
		/* can't change the depth */
		if (ri->ri_depth == 32 || ri->ri_depth == 24)
			*(u_int *)data = WSDISPLAYIO_DEPTH_24;
		else if (ri->ri_depth == 16)
			*(u_int *)data = WSDISPLAYIO_DEPTH_16;
		else if (ri->ri_depth == 15)
			*(u_int *)data = WSDISPLAYIO_DEPTH_15;
		else if (ri->ri_depth == 8)
			*(u_int *)data = WSDISPLAYIO_DEPTH_8;
		else if (ri->ri_depth == 4)
			*(u_int *)data = WSDISPLAYIO_DEPTH_4;
		else if (ri->ri_depth == 1)
			*(u_int *)data = WSDISPLAYIO_DEPTH_1;
		else
			return (-1);
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

	if (off > ri->ri_height * ri->ri_stride)
		return (-1);

	return (PMAP_DIRECT_UNMAP(ri->ri_bits) + off);
}

int
efifb_alloc_screen(void *v, const struct wsscreen_descr *descr,
    void **cookiep, int *curxp, int *curyp, long *attrp)
{
	struct efifb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_fb->rinfo;

	return rasops_alloc_screen(ri, cookiep, curxp, curyp, attrp);
}

void
efifb_free_screen(void *v, void *cookie)
{
	struct efifb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_fb->rinfo;

	rasops_free_screen(ri, cookie);
}

int
efifb_show_screen(void *v, void *cookie, int waitok,
    void (*cb) (void *, int, int), void *cb_arg)
{
	struct efifb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_fb->rinfo;

	return rasops_show_screen(ri, cookie, waitok, cb, cb_arg);
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

#define bmnum(_x) (fls(_x) - ffs(_x) + 1)
#define bmpos(_x) (ffs(_x) - 1)

int
efifb_cnattach(void)
{
	extern bios_efiinfo_t	*bios_efiinfo;
	struct efifb		*fb = &efifb_console;
	struct rasops_info	*ri = &fb->rinfo;
	long			 defattr = 0;
	int			 depth;

	if (bios_efiinfo == NULL || bios_efiinfo->fb_addr == 0)
		return (-1);

	memset(&efifb_console, 0, sizeof(efifb_console));

	ri->ri_bits = (u_char *)PMAP_DIRECT_MAP(bios_efiinfo->fb_addr);

	ri->ri_width = bios_efiinfo->fb_width;
	ri->ri_height = bios_efiinfo->fb_height;

	depth = 0;
	depth = max(depth, fls(bios_efiinfo->fb_red_mask));
	depth = max(depth, fls(bios_efiinfo->fb_green_mask));
	depth = max(depth, fls(bios_efiinfo->fb_blue_mask));
	depth = max(depth, fls(bios_efiinfo->fb_reserved_mask));
	ri->ri_depth = depth;
	ri->ri_stride = bios_efiinfo->fb_pixpsl * (ri->ri_depth / 8);

	ri->ri_rnum = bmnum(bios_efiinfo->fb_red_mask);
	ri->ri_rpos = bmpos(bios_efiinfo->fb_red_mask);
	ri->ri_gnum = bmnum(bios_efiinfo->fb_green_mask);
	ri->ri_gpos = bmpos(bios_efiinfo->fb_green_mask);
	ri->ri_bnum = bmnum(bios_efiinfo->fb_blue_mask);
	ri->ri_bpos = bmpos(bios_efiinfo->fb_blue_mask);
	ri->ri_flg = RI_CLEAR;

	rasops_init(ri, 1600, 1600);

	efifb_std_descr.ncols = ri->ri_cols;
	efifb_std_descr.nrows = ri->ri_rows;
	efifb_std_descr.textops = &ri->ri_ops;
	efifb_std_descr.fontwidth = ri->ri_font->fontwidth;
	efifb_std_descr.fontheight = ri->ri_font->fontheight;
	efifb_std_descr.capabilities = ri->ri_caps;

	ri->ri_ops.alloc_attr(ri, 0, 0, 0, &defattr);

	wsdisplay_cnattach(&efifb_std_descr, ri, 0, 0, defattr);

	return (0);
}
