/*	$OpenBSD: ravenreg.h,v 1.1 2001/06/26 21:57:41 smurph Exp $ */

/*
 * Copyright (c) 2001 Steve Murphree, Jr.
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
 *	This product includes software developed under OpenBSD for RTMX Inc
 *	by Per Fogelstrom, Opsycon AB.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * ravenreg.h: Motorola 'Raven' PowerPC to PCI bridge controller
 */

#ifndef _MACHINE_RAVENREG_H_
#define _MACHINE_RAVENREG_H_

#define RAVEN_REG		0xFEFF0000
#define RAVEN_VENDOR		0xFEFF0000
#define RAVEN_MAGIC		0x10574801 /* vendor information */
#define RAVEN_DEVICE		0xFEFF0002
#define RAVEN_REVID		0xFEFF0005
#define RAVEN_GCSR		0xFEFF0008
#define RAVEN_FEAT		0xFEFF000A
#define RAVEN_MARB		0xFEFF000E
#define RAVEN_PIACK		0xFEFF0030

#define RAVEN_MSADD0		0xFEFF0040 
#define RAVEN_MSADD0_PREP	0xC000FCFF
#define RAVEN_MSOFF0		0xFEFF0044
#define RAVEN_MSOFF0_PREP	0x400000C2
#define RAVEN_MSADD1		0xFEFF0048
#define RAVEN_MSADD1_PREP	0x00000000
#define RAVEN_MSOFF1		0xFEFF004C
#define RAVEN_MSOFF1_PREP	0x00000002
#define RAVEN_MSADD2		0xFEFF0050
#define RAVEN_MSADD2_PREP	0x00000000
#define RAVEN_MSOFF2		0xFEFF0054
#define RAVEN_MSOFF2_PREP	0x00000002
#define RAVEN_MSADD3		0xFEFF0058
#define RAVEN_MSADD3_PREP	0x8000BFFF
#define RAVEN_MSOFF3		0xFEFF005C
#define RAVEN_MSOFF3_PREP	0x800000C0

/* Where we map the PCI memory space - MAP A*/
#define RAVEN_V_PCI_MEM_SPACE	0xc0000000	/* Viritual */
#define RAVEN_P_PCI_MEM_SPACE	0xc0000000	/* Physical */

/* Where we map the PCI I/O space - MAP A*/
#define RAVEN_P_ISA_IO_SPACE	0x80000000
#define RAVEN_V_ISA_IO_SPACE	0x80000000
#define RAVEN_V_PCI_IO_SPACE	0x80000000
#define RAVEN_P_PCI_IO_SPACE	0x80000000

#define PREP_CONFIG_ADD		0x80000CF8
#define PREP_CONFIG_DAT		0x80000CFC

/* Where we map the config space */
#define RAVEN_PCI_CONF_SPACE	(RAVEN_V_ISA_IO_SPACE + 0x00800000)

/* Where we map the PCI memory space - MAP B*/
#define RAVEN_P_PCI_MEM_SPACE_MAP_B	0x80000000	/* Physical */

/* Where we map the PCI I/O space - MAP B*/
#define RAVEN_P_PCI_IO_SPACE_MAP_B	0xfe000000

/* offsets from base pointer */
#define	RAVEN_REGOFFS(x)	((x) | 0x80000000)

/* Where PCI devices sees CPU memory. */
#define	RAVEN_PCI_CPUMEM	0x80000000

#define RAVEN_PCI_VENDOR	0x00
#define RAVEN_PCI_DEVICE	0x02
#define RAVEN_PCI_CMD		0x04
#define RAVEN_PCI_STAT		0x06
#define RAVEN_PCI_REVID		0x08
#define RAVEN_PCI_IO		0x10
#define RAVEN_PCI_MEM		0x14
#define RAVEN_PCI_PSADD0	0x80
#define RAVEN_PCI_PSADD0_VAL	0x8000FBFF
#define RAVEN_PCI_PSOFF0	0x84
#define RAVEN_PCI_PSOFF0_VAL	0x800000F0
#define RAVEN_PCI_PSADD1	0x88
#define RAVEN_PCI_PSADD1_VAL	0xC000FCFF
#define RAVEN_PCI_PSOFF1	0x8C
#define RAVEN_PCI_PSOFF1_VAL	0x400000F0
#define RAVEN_PCI_PSADD2	0x90
#define RAVEN_PCI_PSADD2_VAL	0x00000000
#define RAVEN_PCI_PSOFF2	0x94
#define RAVEN_PCI_PSOFF2_VAL	0x00000000
#define RAVEN_PCI_PSADD3	0x98
#define RAVEN_PCI_PSADD3_VAL	0x00000000
#define RAVEN_PCI_PSOFF3	0x9C
#define RAVEN_PCI_PSOFF3_VAL	0x00000000

#define RAVEN_CMD_IOSP		0x0001
#define RAVEN_CMD_MEMSP		0x0002
#define RAVEN_CMD_MASTR		0x0004

#endif /* _MACHINE_RAVENREG_H_ */
