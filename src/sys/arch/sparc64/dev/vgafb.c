/*	$OpenBSD: vgafb.c,v 1.32 2003/06/02 20:02:49 jason Exp $	*/

/*
 * Copyright (c) 2001 Jason L. Wright (jason@thought.net)
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
#include <sys/buf.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/pciio.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <dev/pci/pcivar.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wscons_raster.h>
#include <dev/rasops/rasops.h>

struct vgafb_softc {
	struct device sc_dev;
	int sc_nscreens;
	int sc_width, sc_height, sc_depth, sc_linebytes;
	int sc_node, sc_ofhandle;
	bus_space_tag_t sc_mem_t;
	bus_space_tag_t sc_io_t;
	pcitag_t sc_pcitag;
	bus_space_handle_t sc_mem_h;
	bus_addr_t sc_io_addr, sc_mem_addr, sc_mmio_addr;
	bus_size_t sc_io_size, sc_mem_size, sc_mmio_size;
	pci_chipset_tag_t sc_pci_chip;
	int sc_has_rom;
	int sc_console;
	u_int sc_mode;
	u_int8_t sc_cmap_red[256];
	u_int8_t sc_cmap_green[256];
	u_int8_t sc_cmap_blue[256];
	struct rasops_info sc_rasops;
	int *sc_crowp, *sc_ccolp;
};

struct wsscreen_descr vgafb_stdscreen = {
	"std",
	0, 0,	/* will be filled in -- XXX shouldn't, it's global. */
	NULL,
	0, 0,
	WSSCREEN_UNDERLINE | WSSCREEN_HILIT |
	WSSCREEN_REVERSE | WSSCREEN_WSCOLORS
};

const struct wsscreen_descr *vgafb_scrlist[] = {
	&vgafb_stdscreen,
	/* XXX other formats? */
};

struct wsscreen_list vgafb_screenlist = {
	sizeof(vgafb_scrlist) / sizeof(struct wsscreen_descr *), vgafb_scrlist
};

int vgafb_mapregs(struct vgafb_softc *, struct pci_attach_args *);
int vgafb_rommap(struct vgafb_softc *, struct pci_attach_args *);
int vgafb_ioctl(void *, u_long, caddr_t, int, struct proc *);
int vgafb_alloc_screen(void *, const struct wsscreen_descr *, void **,
    int *, int *, long *);
void vgafb_free_screen(void *, void *);
int vgafb_show_screen(void *, void *, int,
    void (*cb)(void *, int, int), void *);
paddr_t vgafb_mmap(void *, off_t, int);
int vgafb_is_console(int);
int vgafb_getcmap(struct vgafb_softc *, struct wsdisplay_cmap *);
int vgafb_putcmap(struct vgafb_softc *, struct wsdisplay_cmap *);
void vgafb_setcolor(struct vgafb_softc *, unsigned int,
    u_int8_t, u_int8_t, u_int8_t);
void vgafb_restore_default_colors(struct vgafb_softc *);
void vgafb_updatecursor(struct rasops_info *ri);
static int a2int(char *, int);

struct wsdisplay_accessops vgafb_accessops = {
	vgafb_ioctl,
	vgafb_mmap,
	vgafb_alloc_screen,
	vgafb_free_screen,
	vgafb_show_screen,
	0 /* load_font */
};

int	vgafbmatch(struct device *, void *, void *);
void	vgafbattach(struct device *, struct device *, void *);

struct cfattach vgafb_ca = {
	sizeof (struct vgafb_softc), vgafbmatch, vgafbattach
};

struct cfdriver vgafb_cd = {
	NULL, "vgafb", DV_DULL
};

int
vgafbmatch(parent, vcf, aux)
	struct device *parent;
	void *vcf, *aux;
{
	struct pci_attach_args *pa = aux;

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_PREHISTORIC &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_PREHISTORIC_VGA)
		return (1);

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_DISPLAY &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_DISPLAY_VGA)
		return (1);

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_DISPLAY &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_DISPLAY_MISC)
		return (1);

	return (0);
}

void    
vgafbattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct vgafb_softc *sc = (struct vgafb_softc *)self;
	struct pci_attach_args *pa = aux;
	struct wsemuldisplaydev_attach_args waa;
	long defattr;

	sc->sc_mem_t = pa->pa_memt;
	sc->sc_io_t = pa->pa_iot;
	sc->sc_node = PCITAG_NODE(pa->pa_tag);
	sc->sc_pcitag = pa->pa_tag;

	sc->sc_depth = getpropint(sc->sc_node, "depth", -1);
	if (sc->sc_depth == -1)
		sc->sc_depth = 8;

	sc->sc_linebytes = getpropint(sc->sc_node, "linebytes", -1);
	if (sc->sc_linebytes == -1)
		sc->sc_linebytes = 1152;

	sc->sc_height = getpropint(sc->sc_node, "height", -1);
	if (sc->sc_height == -1)
		sc->sc_height = 900;

	sc->sc_width = getpropint(sc->sc_node, "width", -1);
	if (sc->sc_width == -1)
		sc->sc_width = 1152;

	sc->sc_console = vgafb_is_console(sc->sc_node);

	if (vgafb_mapregs(sc, pa))
		return;

	if (sc->sc_depth == 24) {
		/* Depth is 24, but rasops really wants bpp */
		sc->sc_rasops.ri_depth = 32;
		/* PROM gets linebytes wrong, ignore it. */
		sc->sc_rasops.ri_stride =
		    (sc->sc_rasops.ri_depth / 8) * sc->sc_width;
	} else {
		sc->sc_rasops.ri_depth = sc->sc_depth;
		sc->sc_rasops.ri_stride = sc->sc_linebytes;
	}

	sc->sc_rasops.ri_flg = RI_CENTER | RI_BSWAP;
	sc->sc_rasops.ri_bits = (void *)bus_space_vaddr(sc->sc_mem_t,
	    sc->sc_mem_h);
	sc->sc_rasops.ri_width = sc->sc_width;
	sc->sc_rasops.ri_height = sc->sc_height;
	sc->sc_rasops.ri_hw = sc;

	rasops_init(&sc->sc_rasops,
	    a2int(getpropstring(optionsnode, "screen-#rows"), 34),
	    a2int(getpropstring(optionsnode, "screen-#columns"), 80));

	vgafb_stdscreen.nrows = sc->sc_rasops.ri_rows;
	vgafb_stdscreen.ncols = sc->sc_rasops.ri_cols;
	vgafb_stdscreen.textops = &sc->sc_rasops.ri_ops;
	sc->sc_rasops.ri_ops.alloc_attr(&sc->sc_rasops,
	    WSCOL_BLACK, WSCOL_WHITE, WSATTR_WSCOLORS, &defattr);

	printf("\n");

	if (sc->sc_console) {
		sc->sc_ofhandle = OF_stdout();

		if (sc->sc_depth == 8) {
			vgafb_restore_default_colors(sc);
		} else {
			/* fix color choice */
			wscol_white = 0;
			wscol_black = 255;
			wskernel_bg = 0;
			wskernel_fg = 255;
		}

		if (romgetcursoraddr(&sc->sc_crowp, &sc->sc_ccolp))
			sc->sc_ccolp = sc->sc_crowp = NULL;
		if (sc->sc_ccolp != NULL)
			sc->sc_rasops.ri_ccol = *sc->sc_ccolp;
		if (sc->sc_crowp != NULL)
			sc->sc_rasops.ri_crow = *sc->sc_crowp;
		sc->sc_rasops.ri_updatecursor = vgafb_updatecursor;

		wsdisplay_cnattach(&vgafb_stdscreen, &sc->sc_rasops,
		    sc->sc_rasops.ri_ccol, sc->sc_rasops.ri_crow, defattr);
	}

	waa.console = sc->sc_console;
	waa.scrdata = &vgafb_screenlist;
	waa.accessops = &vgafb_accessops;
	waa.accesscookie = sc;
	config_found(self, &waa, wsemuldisplaydevprint);

	return;
}

int
vgafb_ioctl(v, cmd, data, flags, p)
	void *v;
	u_long cmd;
	caddr_t data;
	int flags;
	struct proc *p;
{
	struct vgafb_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;
	struct pcisel *sel;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_UNKNOWN;
		break;
	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		if (sc->sc_mode == WSDISPLAYIO_MODE_EMUL &&
		    sc->sc_depth == 8) {
			vgafb_restore_default_colors(sc);
		}
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = sc->sc_height;
		wdf->width  = sc->sc_width;
		wdf->depth  = sc->sc_rasops.ri_depth;
		wdf->cmsize = 256;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_rasops.ri_stride;
		break;
		
	case WSDISPLAYIO_GETCMAP:
		if (sc->sc_console == 0)
			return (EINVAL);
		return vgafb_getcmap(sc, (struct wsdisplay_cmap *)data);
	case WSDISPLAYIO_PUTCMAP:
		if (sc->sc_console == 0)
			return (EINVAL);
		return vgafb_putcmap(sc, (struct wsdisplay_cmap *)data);

	case WSDISPLAYIO_GPCIID:
		sel = (struct pcisel *)data;
		sel->pc_bus = PCITAG_BUS(sc->sc_pcitag);
		sel->pc_dev = PCITAG_DEV(sc->sc_pcitag);
		sel->pc_func = PCITAG_FUNC(sc->sc_pcitag);
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
vgafb_getcmap(sc, cm)
	struct vgafb_softc *sc;
	struct wsdisplay_cmap *cm;
{
	u_int index = cm->index;
	u_int count = cm->count;
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	error = copyout(&sc->sc_cmap_red[index], cm->red, count);
	if (error)
		return (error);
	error = copyout(&sc->sc_cmap_green[index], cm->green, count);
	if (error)
		return (error);
	error = copyout(&sc->sc_cmap_blue[index], cm->blue, count);
	if (error)
		return (error);
	return (0);
}

int
vgafb_putcmap(sc, cm)
	struct vgafb_softc *sc;
	struct wsdisplay_cmap *cm;
{
	u_int index = cm->index;
	u_int count = cm->count;
	u_int i;
	int error;
	u_char *r, *g, *b;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	if ((error = copyin(cm->red, &sc->sc_cmap_red[index], count)) != 0)
		return (error);
	if ((error = copyin(cm->green, &sc->sc_cmap_green[index], count)) != 0)
		return (error);
	if ((error = copyin(cm->blue, &sc->sc_cmap_blue[index], count)) != 0)
		return (error);

	r = &sc->sc_cmap_red[index];
	g = &sc->sc_cmap_green[index];
	b = &sc->sc_cmap_blue[index];

	for (i = 0; i < count; i++) {
		OF_call_method("color!", sc->sc_ofhandle, 4, 0, *r, *g, *b,
		    index);
		r++, g++, b++, index++;
	}
	return (0);
}

void
vgafb_setcolor(sc, index, r, g, b)
	struct vgafb_softc *sc;
	unsigned int index;
	u_int8_t r, g, b;
{
	sc->sc_cmap_red[index] = r;
	sc->sc_cmap_green[index] = g;
	sc->sc_cmap_blue[index] = b;
	OF_call_method("color!", sc->sc_ofhandle, 4, 0, r, g, b, index);
}

void
vgafb_restore_default_colors(struct vgafb_softc *sc)
{
	int i;

	for (i = 0; i < 256; i++) {
		const u_char *color;

		color = &rasops_cmap[i * 3];
		vgafb_setcolor(sc, i, color[0], color[1], color[2]);
	}
	/* compensate for BoW palette */
	vgafb_setcolor(sc, WSCOL_BLACK, 0, 0, 0);
	vgafb_setcolor(sc, 255, 0, 0, 0);	/* cursor */
	vgafb_setcolor(sc, WSCOL_WHITE, 255, 255, 255);
}

int
vgafb_alloc_screen(v, type, cookiep, curxp, curyp, attrp)
	void *v;
	const struct wsscreen_descr *type;
	void **cookiep;
	int *curxp, *curyp;
	long *attrp;
{
	struct vgafb_softc *sc = v;

	if (sc->sc_nscreens > 0)
		return (ENOMEM);

	*cookiep = &sc->sc_rasops;
	*curyp = 0;
	*curxp = 0;
	sc->sc_rasops.ri_ops.alloc_attr(&sc->sc_rasops,
	    WSCOL_BLACK, WSCOL_WHITE, WSATTR_WSCOLORS, attrp);
	sc->sc_nscreens++;
	return (0);
}

void
vgafb_free_screen(v, cookie)
	void *v;
	void *cookie;
{
	struct vgafb_softc *sc = v;

	sc->sc_nscreens--;
}

int
vgafb_show_screen(v, cookie, waitok, cb, cbarg)
	void *v;
	void *cookie;
	int waitok;
	void (*cb)(void *, int, int);
	void *cbarg;
{
	return (0);
}

paddr_t
vgafb_mmap(v, off, prot)
	void *v;
	off_t off;
	int prot;
{
	struct vgafb_softc *sc = v;

	if (off & PGOFSET)
		return (-1);

	switch (sc->sc_mode) {
	case WSDISPLAYIO_MODE_MAPPED:
		if (off >= sc->sc_mem_addr &&
		    off < (sc->sc_mem_addr + sc->sc_mem_size))
			return (bus_space_mmap(sc->sc_mem_t,
			    sc->sc_mem_addr, off - sc->sc_mem_addr,
			    prot, BUS_SPACE_MAP_LINEAR));

		if (off >= sc->sc_mmio_addr &&
		    off < (sc->sc_mmio_addr + sc->sc_mmio_size))
			return (bus_space_mmap(sc->sc_mem_t,
			    sc->sc_mmio_addr, off - sc->sc_mmio_addr,
			    prot, BUS_SPACE_MAP_LINEAR));
		break;

	case WSDISPLAYIO_MODE_DUMBFB:
		if (off >= 0 && off < sc->sc_mem_size)
			return (bus_space_mmap(sc->sc_mem_t, sc->sc_mem_addr,
			    off, prot, BUS_SPACE_MAP_LINEAR));
		break;
	}

	return (-1);
}

static int
a2int(char *cp, int deflt)
{
	int i = 0;

	if (*cp == '\0')
		return (deflt);
	while (*cp != '\0')
		i = i * 10 + *cp++ - '0';
	return (i);
}

int
vgafb_is_console(node)
	int node;
{
	extern int fbnode;

	return (fbnode == node);
}

int
vgafb_mapregs(sc, pa)
	struct vgafb_softc *sc;
	struct pci_attach_args *pa;
{
	bus_addr_t ba;
	bus_size_t bs;
	int hasio = 0, hasmem = 0, hasmmio = 0; 
	u_int32_t i, cf;

	for (i = PCI_MAPREG_START; i < PCI_MAPREG_END; i += 4) {
		cf = pci_conf_read(pa->pa_pc, pa->pa_tag, i);
		if (PCI_MAPREG_TYPE(cf) == PCI_MAPREG_TYPE_IO) {
			if (hasio)
				continue;
			if (pci_io_find(pa->pa_pc, pa->pa_tag, i,
			    &sc->sc_io_addr, &sc->sc_io_size)) {
				printf(": failed to find io at 0x%x\n", i);
				continue;
			}
			hasio = 1;
		} else {
			/* Memory mapping... frame memory or mmio? */
			if (pci_mem_find(pa->pa_pc, pa->pa_tag, i,
			    &ba, &bs, NULL)) {
				printf(": failed to find mem at 0x%x\n", i);
				continue;
			}

			if (bs <= 0x10000) {	/* mmio */
				if (hasmmio)
					continue;
				sc->sc_mmio_addr = ba;
				sc->sc_mmio_size = bs;
				hasmmio = 1;
			} else {
				if (hasmem)
					continue;
				if (bus_space_map(pa->pa_memt, ba, bs,
				    0, &sc->sc_mem_h)) {
					printf(": can't map mem space\n");
					continue;
				}
				sc->sc_mem_addr = ba;
				sc->sc_mem_size = bs;
				hasmem = 1;
			}
		}
	}

	if (hasmmio == 0 || hasmem == 0 || hasio == 0) {
		printf(": failed to find all ports\n");
		goto fail;
	}

	return (0);

fail:
	if (hasmem)
		bus_space_unmap(pa->pa_memt, sc->sc_mem_h, sc->sc_mem_size);
	return (1);
}

void
vgafb_updatecursor(ri)
	struct rasops_info *ri;
{
	struct vgafb_softc *sc = ri->ri_hw;

	if (sc->sc_crowp != NULL)
		*sc->sc_crowp = ri->ri_crow;
	if (sc->sc_ccolp != NULL)
		*sc->sc_ccolp = ri->ri_ccol;
}
