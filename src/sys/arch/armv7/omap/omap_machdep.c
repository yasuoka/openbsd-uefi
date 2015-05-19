/*	$OpenBSD: omap_machdep.c,v 1.6 2015/05/19 03:30:54 jsg Exp $	*/
/*
 * Copyright (c) 2013 Sylvestre Gallon <ccna.syl@gmail.com>
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
#include <sys/types.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/termios.h>

#include <machine/bus.h>
#include <machine/bootconfig.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

#include <arm/cortex/smc.h>
#include <arm/armv7/armv7var.h>
#include <armv7/armv7/armv7var.h>
#include <armv7/armv7/armv7_machdep.h>

extern void omap4_smc_call(uint32_t, uint32_t);
extern void omdog_reset(void);
extern char *omap_board_name(void);
extern struct board_dev *omap_board_devs(void);
extern void omap_board_init(void);
extern int comcnspeed;
extern int comcnmode;

void
omap_platform_smc_write(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t off, uint32_t op, uint32_t val)
{
	switch (op) {
	case 0x100:	/* PL310 DEBUG */
	case 0x102:	/* PL310 CTL */
		break;
	default:
		panic("platform_smc_write: invalid operation %d", op);
	}

	omap4_smc_call(op, val);
}

void
omap_platform_init_cons(void)
{
	paddr_t paddr;

	switch (board_id) {
	case BOARD_ID_OMAP3_BEAGLE:
	case BOARD_ID_OMAP3_OVERO:
		paddr = 0x49020000;
		break;
	case BOARD_ID_AM335X_BEAGLEBONE:
		paddr = 0x44e09000;
		break;
	case BOARD_ID_OMAP4_PANDA:
		paddr = 0x48020000;
		break;
	}

	comcnattach(&armv7_a4x_bs_tag, paddr, comcnspeed, 48000000, comcnmode);
	comdefaultrate = comcnspeed;
}

void
omap_platform_watchdog_reset(void)
{
	omdog_reset();
}

void
omap_platform_powerdown(void)
{

}

const char *
omap_platform_board_name(void)
{
	return (omap_board_name());
}

void
omap_platform_disable_l2_if_needed(void)
{
	switch (board_id) {
	case BOARD_ID_OMAP4_PANDA:
		/* disable external L2 cache */
		omap4_smc_call(0x102, 0);
		break;
	}
}

void
omap_platform_board_init(void)
{
	omap_board_init();
}

struct armv7_platform omap_platform = {
	.boot_name = "OpenBSD/omap",
	.board_name = omap_platform_board_name,
	.board_init = omap_platform_board_init,
	.smc_write = omap_platform_smc_write,
	.init_cons = omap_platform_init_cons,
	.watchdog_reset = omap_platform_watchdog_reset,
	.powerdown = omap_platform_powerdown,
	.disable_l2_if_needed = omap_platform_disable_l2_if_needed,
};

struct armv7_platform *
omap_platform_match(void)
{
	struct board_dev *devs;

	devs = omap_board_devs();
	if (devs == NULL)
		return (NULL);

	omap_platform.devs = devs;
	return (&omap_platform);
}
