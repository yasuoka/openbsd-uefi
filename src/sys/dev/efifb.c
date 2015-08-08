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

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <dev/efifbreg.h>

struct wsscreen_descr efifb_descr = {
	"efi"
};

struct efifb_softc {
	struct rasops_info	sc_ri;
};

struct efifb_softc efifb_console;

int
efifb_cnattach(uint64_t base, uint64_t size, int height, int width, int depth, int pixpsl)
{
	struct efifb_softc	*sc = &efifb_console;
	struct rasops_info	*ri = &sc->sc_ri;
	long			 defattr = 0;

	ri->ri_bits = (u_char *)base;
	ri->ri_width = width;
	ri->ri_height = height;
	ri->ri_depth = depth;
	ri->ri_stride = pixpsl * (ri->ri_depth / 8);

	rasops_init(ri, height/8, width / 8);

	efifb_descr.ncols = ri->ri_cols;
	efifb_descr.nrows = ri->ri_rows;
	efifb_descr.textops = &ri->ri_ops;
	efifb_descr.fontwidth = ri->ri_font->fontwidth;
	efifb_descr.fontheight = ri->ri_font->fontheight;

	ri->ri_ops.alloc_attr(ri, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&efifb_descr, sc, 0, 0, defattr);
	return (0);
}
