/*	$OpenBSD: wscons_machdep.c,v 1.10 2003/06/04 19:36:33 deraadt Exp $ */

/*
 * Copyright (c) 2001 Aaron Campbell
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/extent.h>

#include <machine/bus.h>

#include <dev/cons.h>

#include "vga.h"
#include "ega.h"
#include "pcdisplay.h"
#if (NVGA > 0) || (NEGA > 0) || (NPCDISPLAY > 0)
#include <dev/ic/mc6845reg.h>
#include <dev/ic/pcdisplayvar.h>
#if (NVGA > 0)
#include <dev/ic/vgareg.h>
#include <dev/ic/vgavar.h>
#endif
#if (NEGA > 0)
#include <dev/isa/egavar.h>
#endif
#if (NPCDISPLAY > 0)
#include <dev/isa/pcdisplayvar.h>
#endif
#endif

#include "wsdisplay.h"
#if NWSDISPLAY > 0
#include <dev/wscons/wsdisplayvar.h>
#endif

#include "pckbc.h"
#if (NPCKBC > 0)
#include <dev/isa/isareg.h>
#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>
#endif
#include "pckbd.h"     /* for pckbc_machdep_cnattach */
#include "ukbd.h"
#if (NPCKBD > 0) || (NUKBD > 0)
#include <dev/wscons/wskbdvar.h>
#endif
#if (NUKBD > 0)
#include <dev/usb/ukbdvar.h>
#endif

void wscnprobe(struct consdev *);
void wscninit(struct consdev *);
void wscnputc(dev_t, char);
int wscngetc(dev_t);
void wscnpollc(dev_t, int);

void
wscnprobe(cp)
	struct consdev *cp;
{
	int maj;

	/* locate the major number */
	for (maj = 0; maj < nchrdev; maj++) {
		if (cdevsw[maj].d_open == wsdisplayopen)
			break;
	}

	if (maj == nchrdev) {
		/* we are not in cdevsw[], give up */
		panic("wsdisplay is not in cdevsw[]");
	}

	cp->cn_dev = makedev(maj, 0);
	cp->cn_pri = CN_INTERNAL;
}

void
wscninit(cp)
	struct consdev *cp;
{
	static int initted;

	if (initted)
		return;

	initted = 1;

#if (NVGA > 0) || (NEGA > 0) || (NPCDISPLAY > 0)
#if (NVGA > 0)
	if (!vga_cnattach(I386_BUS_SPACE_IO, I386_BUS_SPACE_MEM, -1, 1))
		goto dokbd;
#endif
#if (NEGA > 0)
	if (!ega_cnattach(I386_BUS_SPACE_IO, I386_BUS_SPACE_MEM))
		goto dokbd;
#endif
#if (NPCDISPLAY > 0)
	if (!pcdisplay_cnattach(I386_BUS_SPACE_IO, I386_BUS_SPACE_MEM))
		goto dokbd;
#endif
	if (0) goto dokbd;	/* XXX stupid gcc */
dokbd:
#if (NPCKBC > 0)
	if (!pckbc_cnattach(I386_BUS_SPACE_IO, IO_KBD, KBCMDP, PCKBC_KBD_SLOT))
		return;
#endif
#if (NUKBD > 0)
	if (!ukbd_cnattach())
		return;
#endif
#endif  /* VGA | EGA | PCDISPLAY */
	return;
}

void
wscnputc(dev, i)
	dev_t dev;
	char i;
{
	wsdisplay_cnputc(dev, (int)i);
}

int
wscngetc(dev)
	dev_t dev;
{
	return (wskbd_cngetc(dev));
}

void
wscnpollc(dev, on)
	dev_t dev;
	int on;
{
	wskbd_cnpollc(dev, on);
}
