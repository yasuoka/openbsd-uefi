/*	$OpenBSD: exynos_machdep.c,v 1.4 2015/05/27 00:06:14 jsg Exp $	*/
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
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

#include "fdt.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/termios.h>

#include <machine/bus.h>

#if NFDT > 0
#include <machine/fdt.h>
#endif

#include <arm/cortex/smc.h>
#include <arm/armv7/armv7var.h>
#include <armv7/armv7/armv7var.h>
#include <armv7/exynos/exdisplayvar.h>
#include <armv7/armv7/armv7_machdep.h>

extern void exdog_reset(void);
extern char *exynos_board_name(void);
extern struct board_dev *exynos_board_devs(void);
extern void exynos_board_init(void);
extern int comcnspeed;
extern int comcnmode;

static void
exynos_platform_smc_write(bus_space_tag_t iot, bus_space_handle_t ioh, bus_size_t off,
    uint32_t op, uint32_t val)
{
	bus_space_write_4(iot, ioh, off, val);
}

static void
exynos_platform_init_cons(void)
{
	paddr_t paddr;
	size_t size;

	switch (board_id) {
	case BOARD_ID_EXYNOS5_CHROMEBOOK:
#if NFDT > 0
		void *node;
		node = fdt_find_node("/framebuffer");
		if (node != NULL) {
			uint32_t *mem;
			if (fdt_node_property(node, "reg", (char **)&mem) >= 2*sizeof(uint32_t)) {
				paddr = betoh32(*mem++);
				size = betoh32(*mem);
			}
		}
#else
		paddr = 0xbfc00000;
		size = 0x202000;
#endif
		exdisplay_cnattach(&armv7_bs_tag, paddr, size);
		break;
	default:
		printf("board type %x unknown", board_id);
		return;
		/* XXX - HELP */
	}
}

static void
exynos_platform_watchdog_reset(void)
{
	exdog_reset();
}

static void
exynos_platform_powerdown(void)
{

}

const char *
exynos_platform_board_name(void)
{
	return (exynos_board_name());
}

static void
exynos_platform_disable_l2_if_needed(void)
{

}

void
exynos_platform_board_init(void)
{
	exynos_board_init();
}

struct armv7_platform exynos_platform = {
	.boot_name = "OpenBSD/exynos",
	.board_name = exynos_platform_board_name,
	.board_init = exynos_platform_board_init,
	.smc_write = exynos_platform_smc_write,
	.init_cons = exynos_platform_init_cons,
	.watchdog_reset = exynos_platform_watchdog_reset,
	.powerdown = exynos_platform_powerdown,
	.disable_l2_if_needed = exynos_platform_disable_l2_if_needed,
};

struct armv7_platform *
exynos_platform_match(void)
{
	struct board_dev *devs;

	devs = exynos_board_devs();
	if (devs == NULL)
		return (NULL);

	exynos_platform.devs = devs;
	return (&exynos_platform);
}
