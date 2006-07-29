/*	$OpenBSD: wscons_machdep.c,v 1.1 2006/07/29 14:18:57 miod Exp $	*/
/*
 * Copyright (c) 2006 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
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
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/kernel.h>

#include <machine/vsbus.h>
#include <machine/scb.h>
#include <machine/sid.h>
#include <machine/cpu.h>

#include "wsdisplay.h"
#include "wskbd.h"

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wskbdvar.h>

#include "dzkbd.h"

#include <vax/qbus/dzreg.h>
#include <vax/qbus/dzvar.h>
#include <vax/dec/dzkbdvar.h>

#include "gpx.h"
#include "lcg.h"
#include "lcspx.h"
#include "smg.h"

void (*wsfbcninit)(void) = NULL;

#define	FRAMEBUFFER_PROTOS(fb) \
extern int fb##cnprobe(void); \
extern void fb##cninit(void);

#define	FRAMEBUFFER_PROBE(fb) \
do { \
	if (fb##cnprobe()) { \
		wsfbcninit = fb##cninit; \
		goto found; \
	} \
} while (0)

FRAMEBUFFER_PROTOS(gpx);
FRAMEBUFFER_PROTOS(lcg);
FRAMEBUFFER_PROTOS(lcspx);
FRAMEBUFFER_PROTOS(smg);

#include <dev/cons.h>
cons_decl(ws);

void
wscnprobe(struct consdev *cp)
{
	extern int getmajor(void *);	/* conf.c */
	int major;

	major = getmajor(wsdisplayopen);
	if (major < 0)
		return;

#if NGPX > 0
	FRAMEBUFFER_PROBE(gpx);
#endif
#if NLCG > 0
	FRAMEBUFFER_PROBE(lcg);
#endif
#if NLCSPX > 0
	FRAMEBUFFER_PROBE(lcspx);
#endif
#if NSMG > 0
	FRAMEBUFFER_PROBE(smg);
#endif
	return;

found:
	cp->cn_pri = CN_INTERNAL;
	cp->cn_dev = makedev(major, 0);
}

void
wscninit(struct consdev *cp)
{
	(*wsfbcninit)();

#if NDZKBD > 0
	dzkbd_cnattach(0); /* Connect keyboard and screen together */
#endif
}

void
wscnputc(dev_t dev, int c)
{
#if NWSDISPLAY > 0
	wsdisplay_cnputc(dev, c);
#endif
}

int
wscngetc(dev_t dev)
{
#if NWSKBD > 0
	return (wskbd_cngetc(dev));
#else
	return (0);
#endif
}

void
wscnpollc(dev_t dev, int on)
{
#if NWSKBD > 0
	wskbd_cnpollc(dev, on);
#endif
}
