/* $OpenBSD: omap4.c,v 1.4 2011/11/10 23:43:01 uwe Exp $ */

/*
 * Copyright (c) 2011 Uwe Stuehler <uwe@openbsd.org>
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

#include <sys/types.h>
#include <machine/bus.h>

#include <beagle/dev/omapvar.h>

#define ICP_ADDR	0x48240100
#define ICP_SIZE	0x100
#define ICD_ADDR	0x48241000
#define ICD_SIZE	0x1000

#define GTIMER_ADDR	0x48240000
#define GTIMER_SIZE	0x300

#define GPIOx_SIZE	0x1000
#define GPIO1_ADDR	0x4a310000
#define GPIO2_ADDR	0x48055000
#define GPIO3_ADDR	0x48057000
#define GPIO4_ADDR	0x48059000
#define GPIO5_ADDR	0x4805b000
#define GPIO6_ADDR	0x4805d000

#define GPIO1_IRQ	29
#define GPIO2_IRQ	30
#define GPIO3_IRQ	31
#define GPIO4_IRQ	32
#define GPIO5_IRQ	33
#define GPIO6_IRQ	34

#define UARTx_SIZE	0x400
#define UART1_ADDR	0x4806A000
#define UART2_ADDR	0x4806C000
#define UART3_ADDR	0x48020000
#define UART4_ADDR	0x4806E000

#define UART1_IRQ	72
#define UART2_IRQ	73
#define UART3_IRQ	74
#define UART4_IRQ	70

#define HSMMCx_SIZE	0x300
#define HSMMC1_ADDR	0x4809c000
#define HSMMC1_IRQ	83

struct omap_dev omap4_devs[] = {

	/*
	 * Cortex-A9 Interrupt Controller
	 */

	{ .name = "ampintc",
	  .unit = 0,
	  .mem = {
	    { ICP_ADDR, ICD_SIZE },
	    { ICD_ADDR, ICD_SIZE },
	  },
	},

	/*
	 * Cortex-A9 Global Timer
	 */

	{ .name = "amptimer",
	  .unit = 0,
	  .mem = { { GTIMER_ADDR, GTIMER_SIZE } },
	},

	/*
	 * GPIO
	 */

	{ .name = "omgpio",
	  .unit = 0,
	  .mem = { { GPIO1_ADDR, GPIOx_SIZE } },
	  .irq = { GPIO1_IRQ }
	},

	{ .name = "omgpio",
	  .unit = 1,
	  .mem = { { GPIO2_ADDR, GPIOx_SIZE } },
	  .irq = { GPIO2_IRQ }
	},

	{ .name = "omgpio",
	  .unit = 2,
	  .mem = { { GPIO3_ADDR, GPIOx_SIZE } },
	  .irq = { GPIO3_IRQ }
	},

	{ .name = "omgpio",
	  .unit = 3,
	  .mem = { { GPIO4_ADDR, GPIOx_SIZE } },
	  .irq = { GPIO4_IRQ }
	},

	{ .name = "omgpio",
	  .unit = 4,
	  .mem = { { GPIO5_ADDR, GPIOx_SIZE } },
	  .irq = { GPIO5_IRQ }
	},

	{ .name = "omgpio",
	  .unit = 5,
	  .mem = { { GPIO6_ADDR, GPIOx_SIZE } },
	  .irq = { GPIO6_IRQ }
	},

	/*
	 * UART
	 */

	{ .name = "com",
	  .unit = 2,
	  .mem = { { UART3_ADDR, UARTx_SIZE } },
	  .irq = { UART3_IRQ }
	},

	/*
	 * MMC
	 */

	{ .name = "ommmc",
	  .unit = 0,
	  .mem = { { HSMMC1_ADDR, HSMMCx_SIZE } },
	  .irq = { HSMMC1_IRQ }
	}

};

void
omap4_init(void)
{
	omap_set_devs(omap4_devs);
}
