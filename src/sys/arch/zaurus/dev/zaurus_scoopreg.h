/*	$OpenBSD: zaurus_scoopreg.h,v 1.6 2005/03/08 23:29:06 uwe Exp $	*/

/*
 * Copyright (c) 2005 Uwe Stuehler <uwe@bsdx.de>
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

#define SCOOP0_BASE	0x10800000
#define SCOOP1_BASE	0x08800040
#define SCOOP_SIZE	0x2c

#define SCOOP_MCR	0x00		/* (R/W) */
#define SCOOP_CDR	0x04		/* (R?/W) */
#define SCOOP_CSR	0x08		/* card status register (R) */
#define SCOOP_CPR	0x0c		/* card power register (R/W) */
#define SCOOP_CCR	0x10		/* card configuration reg.? (R/W) */
#define SCOOP_IRR	0x14		/* XXX for pcic: bit 0x4 role is? */
#define SCOOP_IRM	0x14
#define SCOOP_IMR	0x18		/* (R/W) */
#define SCOOP_ISR	0x1c
#define SCOOP_GPCR	0x20		/* GPIO pin direction (R/W) */
#define SCOOP_GPWR	0x24		/* GPIO pin output level (R/W) */
#define SCOOP_GPRR	0x28

/* GPIO bits */

#define SCOOP0_LED_GREEN		1
#define SCOOP0_JK_B_C3000		2
#define SCOOP0_CHARGE_OFF_C3000		3
#define SCOOP0_MUTE_L			4
#define SCOOP0_MUTE_R			5
#define SCOOP0_AKIN_PULLUP		6
#define SCOOP0_CF_POWER_C3000		6
#define SCOOP0_APM_ON			7
#define SCOOP0_LED_ORANGE_C3000		7
#define SCOOP0_BACKLIGHT_CONT		8
#define SCOOP0_JK_A_C3000		8
#define SCOOP0_MIC_BIAS			9
#define SCOOP0_ADC_TEMP_ON_C3000	9

#define SCOOP1_IR_ON			1
#define SCOOP1_AKIN_PULLUP		2
#define SCOOP1_RESERVED_3		3
#define SCOOP1_RESERVED_4		4
#define SCOOP1_RESERVED_5		5
#define SCOOP1_RESERVED_6		6
#define SCOOP1_BACKLIGHT_CONT		7
#define SCOOP1_BACKLIGHT_ON		8
#define SCOOP1_MIC_BIAS			9
