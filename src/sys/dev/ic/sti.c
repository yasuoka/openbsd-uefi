/*	$OpenBSD: sti.c,v 1.6 2001/09/19 20:50:58 mickey Exp $	*/

/*
 * Copyright (c) 2000-2001 Michael Shalayeff
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * TODO:
 *	call sti procs asynchronously;
 *	implement console scroll-back;
 *	X11 support.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <vm/vm.h>
#include <uvm/uvm.h>

#include <machine/bus.h>

#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsconsio.h>

#include <hppa/dev/cpudevs.h>

#include <dev/ic/stireg.h>
#include <dev/ic/stivar.h>

struct cfdriver sti_cd = {
	NULL, "sti", DV_DULL
};

void sti_cursor __P((void *v, int on, int row, int col));
int  sti_mapchar __P((void *v, int uni, u_int *index));
void sti_putchar __P((void *v, int row, int col, u_int uc, long attr));
void sti_copycols __P((void *v, int row, int srccol, int dstcol, int ncols));
void sti_erasecols __P((void *v, int row, int startcol, int ncols, long attr));
void sti_copyrows __P((void *v, int srcrow, int dstrow, int nrows));
void sti_eraserows __P((void *v, int row, int nrows, long attr));
int  sti_alloc_attr __P((void *v, int fg, int bg, int flags, long *));

struct wsdisplay_emulops sti_emulops = {
	sti_cursor,
	sti_mapchar,
	sti_putchar,
	sti_copycols,
	sti_erasecols,
	sti_copyrows,
	sti_eraserows,
	sti_alloc_attr
};

int sti_ioctl __P((void *v, u_long cmd, caddr_t data, int flag, struct proc *p));
paddr_t sti_mmap __P((void *v, off_t offset, int prot));
int sti_alloc_screen __P((void *v, const struct wsscreen_descr *type,
	void **cookiep, int *cxp, int *cyp, long *defattr));
	void sti_free_screen __P((void *v, void *cookie));
int sti_show_screen __P((void *v, void *cookie, int waitok,
	void (*cb) __P((void *, int, int)), void *cbarg));
int sti_load_font __P((void *v, void *cookie, struct wsdisplay_font *));

const struct wsdisplay_accessops sti_accessops = {
	sti_ioctl,
	sti_mmap,
	sti_alloc_screen,
	sti_free_screen,
	sti_show_screen,
	sti_load_font
};

struct wsscreen_descr sti_default_screen = {
	"default", 0, 0,
	&sti_emulops,
	0, 0,
	WSSCREEN_REVERSE | WSSCREEN_UNDERLINE
};

const struct wsscreen_descr *sti_default_scrlist[] = {
	&sti_default_screen
};

struct wsscreen_list sti_default_screenlist = {
	sizeof(sti_default_scrlist) / sizeof(sti_default_scrlist[0]),
	sti_default_scrlist
};

enum sti_bmove_funcs {
	bmf_clear, bmf_copy, bmf_invert, bmf_underline
};

int sti_init __P((struct sti_softc *sc, int mode));
int sti_inqcfg __P((struct sti_softc *sc, struct sti_inqconfout *out));
void sti_bmove __P((struct sti_softc *sc, int, int, int, int, int, int,
	enum sti_bmove_funcs));

void
sti_attach_common(sc)
	struct sti_softc *sc;
{
	struct sti_inqconfout cfg;
	bus_space_handle_t fbh;
	struct wsemuldisplaydev_attach_args waa;
	struct sti_dd *dd;
	struct sti_cfg *cc;
	struct sti_fontcfg *ff;
	int error, size, i;

	dd = &sc->sc_dd;
	if (sc->sc_devtype == STI_DEVTYPE1) {
#define	parseshort(o) \
	((bus_space_read_1(sc->memt, sc->romh, (o) + 3) <<  8) | \
	 (bus_space_read_1(sc->memt, sc->romh, (o) + 7)))
#define	parseword(o) \
	((bus_space_read_1(sc->memt, sc->romh, (o) +  3) << 24) | \
	 (bus_space_read_1(sc->memt, sc->romh, (o) +  7) << 16) | \
	 (bus_space_read_1(sc->memt, sc->romh, (o) + 11) <<  8) | \
	 (bus_space_read_1(sc->memt, sc->romh, (o) + 15)))

		dd->dd_type  = bus_space_read_1(sc->memt, sc->romh, 3);
		dd->dd_nmon  = bus_space_read_1(sc->memt, sc->romh, 7);
		dd->dd_grrev = bus_space_read_1(sc->memt, sc->romh, 11);
		dd->dd_lrrev = bus_space_read_1(sc->memt, sc->romh, 15);
		dd->dd_grid[0] = parseword(0x10);
		dd->dd_grid[1] = parseword(0x20);
		dd->dd_fntaddr = parseword(0x30) & ~3;
		dd->dd_maxst   = parseword(0x40);
		dd->dd_romend  = parseword(0x50) & ~3;
		dd->dd_reglst  = parseword(0x60) & ~3;
		dd->dd_maxreent= parseshort(0x70);
		dd->dd_maxtimo = parseshort(0x78);
				/* what happened to 0x80 ? */
		dd->dd_montbl  = parseword(0x90);
		dd->dd_udaddr  = parseword(0xa0) & ~3;
		dd->dd_stimemreq=parseword(0xb0);
		dd->dd_udsize  = parseword(0xc0);
		dd->dd_pwruse  = parseshort(0xd0);
		dd->dd_bussup  = bus_space_read_1(sc->memt, sc->romh, 0xdb);
		dd->dd_ebussup = bus_space_read_1(sc->memt, sc->romh, 0xdf);
		dd->dd_altcodet= bus_space_read_1(sc->memt, sc->romh, 0xe3);
		dd->dd_cfbaddr = parseword(0xf0) & ~3;

		dd->dd_pacode[0x0] = parseword(0x100) & ~3;
		dd->dd_pacode[0x1] = parseword(0x110) & ~3;
		dd->dd_pacode[0x2] = parseword(0x120) & ~3;
		dd->dd_pacode[0x3] = parseword(0x130) & ~3;
		dd->dd_pacode[0x4] = parseword(0x140) & ~3;
		dd->dd_pacode[0x5] = parseword(0x150) & ~3;
		dd->dd_pacode[0x6] = parseword(0x160) & ~3;
		dd->dd_pacode[0x7] = parseword(0x170) & ~3;
		dd->dd_pacode[0x8] = parseword(0x180) & ~3;
		dd->dd_pacode[0x9] = parseword(0x190) & ~3;
		dd->dd_pacode[0xa] = parseword(0x1a0) & ~3;
		dd->dd_pacode[0xb] = parseword(0x1b0) & ~3;
		dd->dd_pacode[0xc] = parseword(0x1c0) & ~3;
		dd->dd_pacode[0xd] = parseword(0x1d0) & ~3;
		dd->dd_pacode[0xe] = parseword(0x1e0) & ~3;
		dd->dd_pacode[0xf] = parseword(0x1f0) & ~3;
	} else	/* STI_DEVTYPE4 */
		bus_space_read_region_4(sc->memt, sc->romh, 0, (u_int32_t *)dd,
		    sizeof(*dd) / 4);

#ifdef STIDEBUG
	printf("dd:\n"
	    "devtype=%x, rev=%x;%d, gid=%x%x, font=%x, mss=%x\n"
	    "end=%x, mmap=%x, msto=%x, timo=%d, mont=%x, ua=%x\n"
	    "memrq=%x, pwr=%d, bus=%x, ebus=%x, altt=%x, cfb=%x\n"
	    "code=",
	    dd->dd_type, dd->dd_grrev, dd->dd_lrrev,
	    dd->dd_grid[0], dd->dd_grid[1],
	    dd->dd_fntaddr, dd->dd_maxst, dd->dd_romend,
	    dd->dd_reglst, dd->dd_maxreent,
	    dd->dd_maxtimo, dd->dd_montbl, dd->dd_udaddr,
	    dd->dd_stimemreq, dd->dd_udsize, dd->dd_pwruse,
	    dd->dd_bussup, dd->dd_altcodet, dd->dd_cfbaddr);
	printf("%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x\n",
	    dd->dd_pacode[0x0], dd->dd_pacode[0x1], dd->dd_pacode[0x2],
	    dd->dd_pacode[0x3], dd->dd_pacode[0x4], dd->dd_pacode[0x5],
	    dd->dd_pacode[0x6], dd->dd_pacode[0x7], dd->dd_pacode[0x8],
	    dd->dd_pacode[0x9], dd->dd_pacode[0xa], dd->dd_pacode[0xb],
	    dd->dd_pacode[0xc], dd->dd_pacode[0xd], dd->dd_pacode[0xe],
	    dd->dd_pacode[0xf]);
#endif
	/* divine code size, could be less than STI_END entries */
	for (i = STI_END; !dd->dd_pacode[i]; i--);
	size = dd->dd_pacode[i] - dd->dd_pacode[STI_BEGIN];
	if (sc->sc_devtype == STI_DEVTYPE1)
		size = (size + 3) / 4;
	if (!(sc->sc_code = uvm_km_kmemalloc(kernel_map,
	    uvm.kernel_object, round_page(size), UVM_KMF_NOWAIT))) {
		printf(": cannot allocate %u bytes for code\n", size);
		return;
	}

	/* copy code into memory */
	if (sc->sc_devtype == STI_DEVTYPE1) {
		register u_int8_t *p = (u_int8_t *)sc->sc_code;

		for (i = 0; i < size; i++)
			*p++ = bus_space_read_1(sc->memt, sc->romh,
			    dd->dd_pacode[0] + i * 4 + 3);

	} else	/* STI_DEVTYPE4 */
		bus_space_read_region_4(sc->memt, sc->romh, dd->dd_pacode[0],
		    (u_int32_t *)sc->sc_code, size / 4);

	/* flush from cache */
	MD_CACHE_CTL(sc->sc_code, size, MD_CACHE_FLUSH);

#define	O(i)	(dd->dd_pacode[(i)]? (sc->sc_code + \
	(dd->dd_pacode[(i)] - dd->dd_pacode[0]) / \
	(sc->sc_devtype == STI_DEVTYPE1? 4 : 1)) : NULL)
	sc->init	= (sti_init_t)	O(STI_INIT_GRAPH);
	sc->mgmt	= (sti_mgmt_t)	O(STI_STATE_MGMT);
	sc->unpmv	= (sti_unpmv_t)	O(STI_FONT_UNPMV);
	sc->blkmv	= (sti_blkmv_t)	O(STI_BLOCK_MOVE);
	sc->test	= (sti_test_t)	O(STI_SELF_TEST);
	sc->exhdl	= (sti_exhdl_t)	O(STI_EXCEP_HDLR);
	sc->inqconf	= (sti_inqconf_t)O(STI_INQ_CONF);
	sc->scment	= (sti_scment_t)O(STI_SCM_ENT);
	sc->dmac	= (sti_dmac_t)	O(STI_DMA_CTRL);
	sc->flowc	= (sti_flowc_t)	O(STI_FLOW_CTRL);
	sc->utiming	= (sti_utiming_t)O(STI_UTIMING);
	sc->pmgr	= (sti_pmgr_t)	O(STI_PROC_MGR);
	sc->util	= (sti_util_t)	O(STI_UTIL);

	pmap_protect(pmap_kernel(), sc->sc_code,
	    sc->sc_code + round_page(size), VM_PROT_READ|VM_PROT_EXECUTE);

	cc = &sc->sc_cfg;
	bzero(cc, sizeof (*cc));
	{
		register int i = dd->dd_reglst;
		register u_int32_t *p;
		struct sti_region r;

#ifdef STIDEBUG
		printf ("stiregions @%p:\n", i);
#endif
		r.last = 0;
		for (p = cc->regions; !r.last &&
		     p < &cc->regions[STI_REGION_MAX]; p++) {

			if (sc->sc_devtype == STI_DEVTYPE1)
				*(u_int *)&r = parseword(i), i+= 16;
			else
				*(u_int *)&r = bus_space_read_4(sc->memt, sc->romh, i), i += 4;

			*p = (p == cc->regions? sc->romh : sc->ioh) +
			    (r.offset << PGSHIFT);
#ifdef STIDEBUG
			printf("%x @ 0x%x %s%s%s%s\n",
			    r.length << PGSHIFT, *p, r.sys_only? "sys " : "",
			    r.cache? "cache " : "", r.btlb? "btlb " : "",
			    r.last? "last" : "");
#endif

			/* rom was already mapped */
			if (p != cc->regions) {
				if (bus_space_map(sc->memt, *p,
				    r.length << PGSHIFT, 0, &fbh)) {
#ifdef STIDEBUG
					printf("cannot map region\n");
#endif
					/* who cares: return; */
				} else if (p - cc->regions == 1)
					sc->fbh = fbh;
			}
		}
	}

	if ((error = sti_init(sc, 0))) {
		printf (": can not initialize (%d)\n", error);
		return;
	}

	if ((error = sti_inqcfg(sc, &cfg))) {
		printf (": error %d inquiring config\n", error);
		return;
	}

	if ((error = sti_init(sc, STI_TEXTMODE))) {
		printf (": can not initialize (%d)\n", error);
		return;
	}

	ff = &sc->sc_fontcfg;
	if (sc->sc_devtype == STI_DEVTYPE1) {
		i = dd->dd_fntaddr;
		ff->first  = parseshort(i + 0x00);
		ff->last   = parseshort(i + 0x08);
		ff->width  = bus_space_read_1(sc->memt, sc->romh, i + 0x13);
		ff->height = bus_space_read_1(sc->memt, sc->romh, i + 0x17);
		ff->type   = bus_space_read_1(sc->memt, sc->romh, i + 0x1b);
		ff->bpc    = bus_space_read_1(sc->memt, sc->romh, i + 0x1f);
		ff->uheight= bus_space_read_1(sc->memt, sc->romh, i + 0x33);
		ff->uoffset= bus_space_read_1(sc->memt, sc->romh, i + 0x37);
	} else	/* STI_DEVTYPE4 */
		bus_space_read_region_4(sc->memt, sc->romh, dd->dd_fntaddr,
		    (u_int32_t *)ff, sizeof(*ff) / 4);

	printf(": %s rev %d.%02d;%d\n"
	    "%s: %dx%d frame buffer, %dx%dx%d display, offset %dx%d\n"
	    "%s: %dx%d font type %d, %d bpc, charset %d-%d\n", cfg.name,
	    dd->dd_grrev >> 4, dd->dd_grrev & 0xf, dd->dd_lrrev,
	    sc->sc_dev.dv_xname, cfg.fbwidth, cfg.fbheight,
	    cfg.width, cfg.height, cfg.bpp, cfg.owidth, cfg.oheight,
	    sc->sc_dev.dv_xname,
	    ff->width, ff->height, ff->type,  ff->bpc, ff->first, ff->last);

	/*
	 * parse screen descriptions:
	 *	figure number of fonts supported;
	 *	allocate wscons structures;
	 *	calculate dimentions.
	 */

	sti_default_screen.ncols = cfg.width / ff->width;
	sti_default_screen.nrows = cfg.height / ff->height;
	sti_default_screen.fontwidth = ff->width;
	sti_default_screen.fontheight = ff->height;

	/* attach WSDISPLAY */
	waa.console = sc->sc_dev.dv_unit;
	waa.scrdata = &sti_default_screenlist;
	waa.accessops = &sti_accessops;
	waa.accesscookie = sc;

	config_found(&sc->sc_dev, &waa, wsemuldisplaydevprint);
}

int
sti_init(sc, mode)
	struct sti_softc *sc;
	int mode;
{
	struct {
		struct sti_initflags flags;
		struct sti_initin in;
		struct sti_initout out;
	} a;

	bzero(&a,  sizeof(a));

	a.flags.flags = STI_INITF_WAIT | STI_INITF_CMB | STI_INITF_EBET |
	    (mode & STI_TEXTMODE? STI_INITF_TEXT | STI_INITF_PBET |
	     STI_INITF_PBETI | STI_INITF_ICMT : 0);
	a.in.text_planes = 1;
#ifdef STIDEBUG
	printf("%s: init,%p(%x, %p, %p, %p)\n", sc->sc_dev.dv_xname,
	    sc->init, a.flags.flags, &a.in, &a.out, &sc->sc_cfg);
#endif
	(*sc->init)(&a.flags, &a.in, &a.out, &sc->sc_cfg);
	return (a.out.text_planes != a.in.text_planes || a.out.errno);
}

int
sti_inqcfg(sc, out)
	struct sti_softc *sc;
	struct sti_inqconfout *out;
{
	struct {
		struct sti_inqconfflags flags;
		struct sti_inqconfin in;
	} a;

	bzero(&a,  sizeof(a));
	bzero(out, sizeof(*out));

	a.flags.flags = STI_INQCONFF_WAIT;
	(*sc->inqconf)(&a.flags, &a.in, out, &sc->sc_cfg);

	return out->errno;
}

void
sti_bmove(sc, x1, y1, x2, y2, h, w, f)
	struct sti_softc *sc;
	int x1, y1, x2, y2, h, w;
	enum sti_bmove_funcs f;
{
	struct {
		struct sti_blkmvflags flags;
		struct sti_blkmvin in;
		struct sti_blkmvout out;
	} a;

	bzero(&a, sizeof(a));

	a.flags.flags = STI_BLKMVF_WAIT;
	switch (f) {
	case bmf_clear:
		a.flags.flags |= STI_BLKMVF_CLR;
		a.in.bg_colour = 0;
		break;
	case bmf_underline:
	case bmf_copy:
		a.in.fg_colour = 1;
		a.in.bg_colour = 0;
		break;
	case bmf_invert:
		a.in.fg_colour = 0;
		a.in.bg_colour = 1;
		break;
	}
	a.in.srcx = x1;
	a.in.srcy = y1;
	a.in.dstx = x2;
	a.in.dsty = y2;
	a.in.height = h;
	a.in.width = w;

	(*sc->blkmv)(&a.flags, &a.in, &a.out, &sc->sc_cfg);
#ifdef STIDEBUG
	if (a.out.errno)
		printf ("%s: blkmv returned %d\n",
			sc->sc_dev.dv_xname, a.out.errno);
#endif
}

int
sti_ioctl(v, cmd, data, flag, p)
	void *v;
	u_long cmd;
	caddr_t data;
	int flag;
	struct proc *p;
{
	/* register struct sti_softc *sc; */

	return -1;
}

paddr_t
sti_mmap(v, offset, prot)
	void *v;
	off_t offset;
	int prot;
{
	/* XXX not finished */
	return offset;
}

int
sti_alloc_screen(v, type, cookiep, cxp, cyp, defattr)
	void *v;
	const struct wsscreen_descr *type;
	void **cookiep;
	int *cxp, *cyp;
	long *defattr;
{
	return -1;
}

void
sti_free_screen(v, cookie)
	void *v;
	void *cookie;
{
}

int
sti_show_screen(v, cookie, waitok, cb, cbarg)
	void *v;
	void *cookie;
	int waitok;
	void (*cb) __P((void *, int, int));
	void *cbarg;
{
	return 0;
}

int
sti_load_font(v, cookie, font)
	void *v;
	void *cookie;
	struct wsdisplay_font *font;
{
	return -1;
}

void
sti_cursor(v, on, row, col)
	void *v;
	int on, row, col;
{
	register struct sti_softc *sc = v;

	sti_bmove(sc, row * sc->sc_fontcfg.height, col * sc->sc_fontcfg.width,
		  row * sc->sc_fontcfg.height, col * sc->sc_fontcfg.width,
		  sc->sc_fontcfg.width, sc->sc_fontcfg.height, bmf_invert);
}

int
sti_mapchar(v, uni, index)
	void *v;
	int uni;
	u_int *index;
{
	if (uni < 256)
		*index = uni;

	return 1;
}

void
sti_putchar(v, row, col, uc, attr)
	void *v;
	int row, col;
	u_int uc;
	long attr;
{
	register struct sti_softc *sc = v;
	struct {
		struct sti_unpmvflags flags;
		struct sti_unpmvin in;
		struct sti_unpmvout out;
	} a;

	bzero(&a, sizeof(a));

	a.flags.flags = STI_UNPMVF_WAIT;
	/* XXX does not handle text attributes */
	a.in.fg_colour = 1;
	a.in.bg_colour = 0;
	a.in.x = col * sc->sc_fontcfg.width;
	a.in.y = row * sc->sc_fontcfg.height;
	a.in.font_addr = 0/*STI_FONTAD(sc->sc_devtype, sc->sc_rom)*/;
	a.in.index = uc;
	(*sc->unpmv)(&a.flags, &a.in, &a.out, &sc->sc_cfg);
}

void
sti_copycols(v, row, srccol, dstcol, ncols)
	void *v;
	int row, srccol, dstcol, ncols;
{
	register struct sti_softc *sc = v;

	sti_bmove(sc,
	    row * sc->sc_fontcfg.height, srccol * sc->sc_fontcfg.width,
	    row * sc->sc_fontcfg.height, dstcol * sc->sc_fontcfg.width,
	    ncols * sc->sc_fontcfg.width, sc->sc_fontcfg.height,
	    bmf_copy);
}

void
sti_erasecols(v, row, startcol, ncols, attr)
	void *v;
	int row, startcol, ncols;
	long attr;
{
	register struct sti_softc *sc = v;

	sti_bmove(sc,
	    row * sc->sc_fontcfg.height, startcol * sc->sc_fontcfg.width,
	    row * sc->sc_fontcfg.height, startcol * sc->sc_fontcfg.width,
	    ncols * sc->sc_fontcfg.width, sc->sc_fontcfg.height,
	    bmf_clear);
}

void
sti_copyrows(v, srcrow, dstrow, nrows)
	void *v;
	int srcrow, dstrow, nrows;
{
	register struct sti_softc *sc = v;

	sti_bmove(sc,
	    srcrow * sc->sc_fontcfg.height, 0,
	    dstrow * sc->sc_fontcfg.height, 0,
	    sc->sc_cfg.fb_width, nrows + sc->sc_fontcfg.height,
	    bmf_copy);
}

void
sti_eraserows(v, srcrow, nrows, attr)
	void *v;
	int srcrow, nrows;
	long attr;
{
	register struct sti_softc *sc = v;

	sti_bmove(sc,
	    srcrow * sc->sc_fontcfg.height, 0,
	    srcrow * sc->sc_fontcfg.height, 0,
	    sc->sc_cfg.fb_width, nrows + sc->sc_fontcfg.height,
	    bmf_clear);
}

int
sti_alloc_attr(v, fg, bg, flags, pattr)
	void *v;
	int fg, bg, flags;
	long *pattr;
{
	/* register struct sti_softc *sc = v; */

	*pattr = 0;

	return 0;
}

