/*	$OpenBSD: vexpress_a9.c,v 1.2 2015/06/14 05:01:31 jsg Exp $	*/

/*
 * Copyright (c) 2015 Jonathan Gray <jsg@openbsd.org>
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
#include <sys/device.h>
#include <machine/bus.h>
#include <arch/arm/armv7/armv7var.h>

#include <armv7/armv7/armv7var.h>

#define SYSREG_ADDR	0x10000000
#define SYSREG_SIZE	0x1000

#define UARTx_SIZE	0x1000
#define UART0_ADDR	0x10009000
#define UART1_ADDR	0x1000a000
#define UART2_ADDR	0x1000b000
#define UART3_ADDR	0x1000c000

#define UART0_IRQ	5
#define UART1_IRQ	6
#define UART2_IRQ	7
#define UART3_IRQ	8

#define VIRTIO0_ADDR	0x10013000
#define VIRTIO1_ADDR	0x10013200
#define VIRTIO2_ADDR	0x10013400
#define VIRTIO3_ADDR	0x10013600
#define VIRTIO_SIZE	0x200

#define VIRTIO0_IRQ	40
#define VIRTIO1_IRQ	41
#define VIRTIO2_IRQ	42
#define VIRTIO3_IRQ	43

#define RTC_ADDR	0x10017000
#define RTC_SIZE	0x1000
#define RTC_IRQ		4

struct armv7_dev vexpress_a9_devs[] = {
	{ .name = "sysreg",
	  .unit = 0,
	  .mem = { { SYSREG_ADDR, SYSREG_SIZE } },
	},
	{ .name = "pluart",
	  .unit = 0,
	  .mem = { { UART0_ADDR, UARTx_SIZE } },
	  .irq = { UART0_IRQ }
	},
	{ .name = "pluart",
	  .unit = 1,
	  .mem = { { UART1_ADDR, UARTx_SIZE } },
	  .irq = { UART1_IRQ }
	},
	{ .name = "pluart",
	  .unit = 2,
	  .mem = { { UART1_ADDR, UARTx_SIZE } },
	  .irq = { UART2_IRQ }
	},
	{ .name = "pluart",
	  .unit = 3,
	  .mem = { { UART1_ADDR, UARTx_SIZE } },
	  .irq = { UART3_IRQ }
	},
	{ .name = "plrtc",
	  .unit = 0,
	  .mem = { { RTC_ADDR, RTC_SIZE } },
	  .irq = { RTC_IRQ }
	},
	{ .name = "virtio",
	  .unit = 0,
	  .mem = { { VIRTIO0_ADDR, VIRTIO_SIZE } },
	  .irq = { VIRTIO0_IRQ }
	},
	{ .name = "virtio",
	  .unit = 1,
	  .mem = { { VIRTIO1_ADDR, VIRTIO_SIZE } },
	  .irq = { VIRTIO1_IRQ }
	},
	{ .name = "virtio",
	  .unit = 2,
	  .mem = { { VIRTIO2_ADDR, VIRTIO_SIZE } },
	  .irq = { VIRTIO2_IRQ }
	},
	{ .name = "virtio",
	  .unit = 3,
	  .mem = { { VIRTIO3_ADDR, VIRTIO_SIZE } },
	  .irq = { VIRTIO3_IRQ }
	},
};

void
vexpress_a9_init(void)
{
	armv7_set_devs(vexpress_a9_devs);
}
