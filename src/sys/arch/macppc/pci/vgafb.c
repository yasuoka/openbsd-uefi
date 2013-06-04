/*	$OpenBSD: vgafb.c,v 1.44 2013/06/04 02:16:14 mpi Exp $	*/
/*	$NetBSD: vga.c,v 1.3 1996/12/02 22:24:54 cgd Exp $	*/

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/buf.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <dev/cons.h>
#include <dev/ofw/openfirm.h>
#include <macppc/macppc/ofw_machdep.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#include <dev/wsfont/wsfont.h>

#include <macppc/pci/vgafbvar.h>

struct cfdriver vgafb_cd = {
	NULL, "vgafb", DV_DULL,
};

void vgafb_setcolor(struct vga_config *vc, unsigned int index,
		    u_int8_t r, u_int8_t g, u_int8_t b);
void vgafb_restore_default_colors(struct vga_config *vc);

extern struct vga_config vgafbcn;

struct wsscreen_descr vgafb_stdscreen = {
	"std",
	0, 0,   /* will be filled in -- XXX shouldn't, it's global */
	0,
	0, 0,
	WSSCREEN_UNDERLINE | WSSCREEN_HILIT |
	WSSCREEN_REVERSE | WSSCREEN_WSCOLORS
};
const struct wsscreen_descr *vgafb_scrlist[] = {
	&vgafb_stdscreen,
	/* XXX other formats, graphics screen? */
};

struct wsscreen_list vgafb_screenlist = {
	sizeof(vgafb_scrlist) / sizeof(struct wsscreen_descr *), vgafb_scrlist
};

struct wsdisplay_accessops vgafb_accessops = {
	vgafb_ioctl,
	vgafb_mmap,
	vgafb_alloc_screen,
	vgafb_free_screen,
	vgafb_show_screen,
	NULL,		/* load_font */
	NULL,		/* scrollback */
	NULL,		/* getchar */
	vgafb_burn,	/* burner */
};

int	vgafb_getcmap(struct vga_config *vc, struct wsdisplay_cmap *cm);
int	vgafb_putcmap(struct vga_config *vc, struct wsdisplay_cmap *cm);

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

#ifdef APERTURE
extern int allowaperture;
#endif


void
vgafb_init(bus_space_tag_t iot, bus_space_tag_t memt, struct vga_config *vc,
    u_int32_t  membase, size_t memsize)
{
	vc->vc_memt = memt;
	vc->membase = membase;
	vc->memsize = memsize;

	if (bus_space_map(vc->vc_memt, membase, memsize,
	    /* XXX */ppc_proc_is_64b ? 0 : 1, &vc->vc_memh))
		panic("vgafb_init: can't map mem space");
}

void
vgafb_restore_default_colors(struct vga_config *vc)
{
	int i;

	for (i = 0; i < 256; i++) {
		const u_char *color;

		color = &rasops_cmap[i * 3];
		vgafb_setcolor(vc, i, color[0], color[1], color[2]);
	}
}

void
vgafb_wsdisplay_attach(struct device *parent, struct vga_config *vc,
    int console)
{
	struct wsemuldisplaydev_attach_args aa;

	aa.console = console;
	aa.scrdata = &vgafb_screenlist;
	aa.accessops = &vgafb_accessops;
	aa.accesscookie = vc;
	aa.defaultscreens = 0;

	/* no need to keep the burner function if no hw support */
	if (cons_backlight_available == 0)
		vgafb_accessops.burn_screen = NULL;
	else {
		vc->vc_backlight_on = WSDISPLAYIO_VIDEO_OFF;
		vgafb_burn(vc, WSDISPLAYIO_VIDEO_ON, 0);	/* paranoia */
	}

	config_found(parent, &aa, wsemuldisplaydevprint);
}

int
vgafb_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct vga_config *vc = v;
	struct wsdisplay_fbinfo *wdf;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_PCIVGA;
		return 0;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = cons_height;
		wdf->width  = cons_width;
		wdf->depth  = cons_depth;
		wdf->cmsize = 256;
		return 0;

	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = cons_linebytes;
		return 0;

	case WSDISPLAYIO_GETCMAP:
		return vgafb_getcmap(vc, (struct wsdisplay_cmap *)data);

	case WSDISPLAYIO_PUTCMAP:
		return vgafb_putcmap(vc, (struct wsdisplay_cmap *)data);

	case WSDISPLAYIO_SMODE:
		vc->vc_mode = *(u_int *)data;
		/* track the state of the display,
		 * if returning to WSDISPLAYIO_MODE_EMUL
		 * restore the last palette, workaround for
		 * bad accellerated X servers that does not restore
		 * the correct palette.
		 */
		if (cons_depth == 8)
			vgafb_restore_default_colors(vc);
		break;

	case WSDISPLAYIO_GETPARAM:
	{
		struct wsdisplay_param *dp = (struct wsdisplay_param *)data;

		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			if (cons_backlight_available != 0) {
				dp->min = MIN_BRIGHTNESS;
				dp->max = MAX_BRIGHTNESS;
				dp->curval = cons_brightness;
				return 0;
			}
			return -1;
		case WSDISPLAYIO_PARAM_BACKLIGHT:
			if (cons_backlight_available != 0) {
				dp->min = 0;
				dp->max = 1;
				dp->curval = vc->vc_backlight_on;
				return 0;
			} else
				return -1;
		}
	}
		return -1;

	case WSDISPLAYIO_SETPARAM:
	{
		struct wsdisplay_param *dp = (struct wsdisplay_param *)data;

		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			if (cons_backlight_available == 1) {
				of_setbrightness(dp->curval);
				return 0;
			} else
				return -1;
		case WSDISPLAYIO_PARAM_BACKLIGHT:
			if (cons_backlight_available != 0) {
				vgafb_burn(vc,
				    dp->curval ? WSDISPLAYIO_VIDEO_ON :
				      WSDISPLAYIO_VIDEO_OFF, 0);
				return 0;
			} else
				return -1;
		}
	}
		return -1;

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

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

paddr_t
vgafb_mmap(void *v, off_t off, int prot)
{
	struct vga_config *vc = v;

	if (off & PGOFSET)
		return (-1);

	switch (vc->vc_mode) {
	case WSDISPLAYIO_MODE_MAPPED:
#ifdef APERTURE
		if (allowaperture == 0)
			return (-1);
#endif

		if (vc->mmiosize == 0)
			return (-1);

		if (off >= vc->membase && off < (vc->membase + vc->memsize))
			return (off);

		if (off >= vc->mmiobase && off < (vc->mmiobase + vc->mmiosize))
			return (off);
		break;

	case WSDISPLAYIO_MODE_DUMBFB:
		if (off >= 0x00000 && off < vc->memsize)
			return (vc->membase + off);
		break;

	}

	return (-1);
}

int
vgafb_cnattach(bus_space_tag_t iot, bus_space_tag_t memt, int type, int check)
{
	struct vga_config *vc = &vgafbcn;
	struct rasops_info *ri = &vc->ri;
	long defattr;
	int i;

	vgafb_init(iot, memt, vc, cons_addr, cons_linebytes * cons_height);

	if (cons_depth == 8)
		vgafb_restore_default_colors(vc);

	ri->ri_flg = RI_CENTER;
	ri->ri_depth = cons_depth;
	ri->ri_bits = (void *)vc->vc_memh;
	ri->ri_width = cons_width;
	ri->ri_height = cons_height;
	ri->ri_stride = cons_linebytes;
	ri->ri_hw = vc;

	/* Clear the screen */
	for (i = 0; i < cons_linebytes * cons_height; i++)
		bus_space_write_1(memt,	vc->vc_memh, i, 0);

	rasops_init(ri, 160, 160);	/* XXX */

	vgafb_stdscreen.nrows = ri->ri_rows;
	vgafb_stdscreen.ncols = ri->ri_cols;
	vgafb_stdscreen.textops = &ri->ri_ops;

	ri->ri_ops.alloc_attr(ri, 0, 0, 0, &defattr);

	wsdisplay_cnattach(&vgafb_stdscreen, ri, 0, 0, defattr);
	vc->nscreens++;

	return (0);
}

struct {
	u_int8_t r;
	u_int8_t g;
	u_int8_t b;
} vgafb_color[256];

void
vgafb_setcolor(struct vga_config *vc, unsigned int index, u_int8_t r,
    u_int8_t g, u_int8_t b)
{
	vc->vc_cmap_red[index] = r;
	vc->vc_cmap_green[index] = g;
	vc->vc_cmap_blue[index] = b;

	vgafb_color[index].r = r;
	vgafb_color[index].g = g;
	vgafb_color[index].b = b;
	OF_call_method_1("set-colors", cons_display_ofh, 3,
	    &vgafb_color[index], index, 1);
}

int
vgafb_getcmap(struct vga_config *vc, struct wsdisplay_cmap *cm)
{
	u_int index = cm->index;
	u_int count = cm->count;
	int error;

	if (index >= 256 || count > 256 - index)
		return EINVAL;

	error = copyout(&vc->vc_cmap_red[index],   cm->red,   count);
	if (error)
		return error;
	error = copyout(&vc->vc_cmap_green[index], cm->green, count);
	if (error)
		return error;
	error = copyout(&vc->vc_cmap_blue[index],  cm->blue,  count);
	if (error)
		return error;

	return 0;
}

int
vgafb_putcmap(struct vga_config *vc, struct wsdisplay_cmap *cm)
{
	u_int index = cm->index;
	u_int count = cm->count;
	u_int i;
	int error;
	u_int8_t *r, *g, *b;

	if (index >= 256 || count > 256 - index)
		return EINVAL;

	if ((error = copyin(cm->red, &vc->vc_cmap_red[index], count)) != 0)
		return (error);
	if ((error = copyin(cm->green, &vc->vc_cmap_green[index], count)) != 0)
		return (error);
	if ((error = copyin(cm->blue, &vc->vc_cmap_blue[index], count)) != 0)
		return (error);

	r = &(vc->vc_cmap_red[index]);
	g = &(vc->vc_cmap_green[index]);
	b = &(vc->vc_cmap_blue[index]);

	for (i = 0; i < count; i++) {
		vgafb_color[i].r = *r;
		vgafb_color[i].g = *g;
		vgafb_color[i].b = *b;
		r++, g++, b++;
	}
	OF_call_method_1("set-colors", cons_display_ofh, 3,
	    &vgafb_color, index, count);
	return 0;
}

void
vgafb_burn(void *v, u_int on, u_int flags)
{
	struct vga_config *vc = v;

	if (cons_backlight_available == 1 &&
	    vc->vc_backlight_on != on) {
		if (on == WSDISPLAYIO_VIDEO_ON) {
			OF_call_method_1("backlight-on", cons_display_ofh, 0);
		} else {
			OF_call_method_1("backlight-off", cons_display_ofh, 0);
		}
		vc->vc_backlight_on = on;
	}
}

int
vgafb_alloc_screen(void *v, const struct wsscreen_descr *type, void **cookiep,
    int *curxp, int *curyp, long *attrp)
{
	struct vga_config *vc = v;
	struct rasops_info *ri = &vc->ri;

	if (vc->nscreens > 0)
		return (ENOMEM);

	*cookiep = ri;
	*curxp = 0;
	*curyp = 0;
	ri->ri_ops.alloc_attr(ri, 0, 0, 0, attrp);
	vc->nscreens++;

	return (0);
}

void
vgafb_free_screen(void *v, void *cookie)
{
	struct vga_config *vc = v;

	if (vc == &vgafbcn)
		panic("vgafb_free_screen: console");

	vc->nscreens--;
}

int
vgafb_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	return (0);
}
