/*	$OpenBSD: npx.h,v 1.6 2004/02/01 19:05:23 deraadt Exp $	*/
/*	$NetBSD: npx.h,v 1.11 1994/10/27 04:16:11 cgd Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)npx.h	5.3 (Berkeley) 1/18/91
 */

/*
 * 287/387 NPX Coprocessor Data Structures and Constants
 * W. Jolitz 1/90
 */

#ifndef	_I386_NPX_H_
#define	_I386_NPX_H_

/* Environment information of floating point unit */
struct	env87 {
	long	en_cw;		/* control word (16bits) */
	long	en_sw;		/* status word (16bits) */
	long	en_tw;		/* tag word (16bits) */
	long	en_fip;		/* floating point instruction pointer */
	u_short	en_fcs;		/* floating code segment selector */
	u_short	en_opcode;	/* opcode last executed (11 bits ) */
	long	en_foo;		/* floating operand offset */
	long	en_fos;		/* floating operand segment selector */
};

#define EN_SW_IE	0x0001	/* invalid operation */
#define EN_SW_DE	0x0002	/* denormal */
#define EN_SW_ZE	0x0004	/* divide by zero */
#define EN_SW_OE	0x0008	/* overflow */
#define EN_SW_UE	0x0010	/* underflow */
#define EN_SW_PE	0x0020	/* loss of precision */

/* Contents of each floating point accumulator */
struct	fpacc87 {
#ifdef dontdef	/* too unportable */
	u_long	fp_mantlo;	/* mantissa low (31:0) */
	u_long	fp_manthi;	/* mantissa high (63:32) */
	int	fp_exp:15;	/* exponent */
	int	fp_sgn:1;	/* mantissa sign */
#else
	u_char	fp_bytes[10];
#endif
};

#ifdef GPL_MATH_EMULATE
#include <gnu/arch/i386/fpemul/math_emu.h>
#endif

/* Floating point and emulator context */
struct	save87 {
	struct	env87 sv_env;		/* floating point control/status */
	struct	fpacc87 sv_ac[8];	/* accumulator contents, 0-7 */
	u_long	sv_ex_sw;		/* status word for last exception */
	u_long	sv_ex_tw;		/* tag word for last exception */
};

/* Environment of FPU/MMX/SSE/SSE2. */
struct envxmm {
/*0*/	uint16_t en_cw;		/* FPU Control Word */
	uint16_t en_sw;		/* FPU Status Word */
	uint8_t  en_rsvd0;
	uint8_t  en_tw;		/* FPU Tag Word (abridged) */
	uint16_t en_opcode;	/* FPU Opcode */
	uint32_t en_fip;	/* FPU Instruction Pointer */
	uint16_t en_fcs;	/* FPU IP selector */
	uint16_t en_rsvd1;
/*16*/	uint32_t en_foo;	/* FPU Data pointer */
	uint16_t en_fos;	/* FPU Data pointer selector */
	uint16_t en_rsvd2;
	uint32_t en_mxcsr;	/* MXCSR Register State */
	uint32_t en_rsvd3;
};

/* FPU regsters in the extended save format. */
struct fpaccxmm {
	uint8_t	fp_bytes[10];
	uint8_t	fp_rsvd[6];
};

/* SSE/SSE2 registers. */
struct xmmreg {
	uint8_t sse_bytes[16];
};

/* FPU/MMX/SSE/SSE2 context */
struct savexmm {
	struct envxmm sv_env;		/* control/status context */
	struct fpaccxmm sv_ac[8];	/* ST/MM regs */
	struct xmmreg sv_xmmregs[8];	/* XMM regs */
	uint8_t sv_rsvd[16 * 14];
	/* 512-bytes --- end of hardware portion of save area */
	uint32_t sv_ex_sw;		/* saved SW from last exception */
	uint32_t sv_ex_tw;		/* saved TW from last exception */
};

union savefpu {
	struct save87 sv_87;
	struct savexmm sv_xmm;
#ifdef GPL_MATH_EMULATE
	union i387_union gplemu;
#else
	u_char emupad[176];		/* sizeof(i387_union) */
#endif
};

/* Cyrix EMC memory - mapped coprocessor context switch information */
struct	emcsts {
	long	em_msw;		/* memory mapped status register when swtched */
	long	em_tar;		/* memory mapped temp A register when swtched */
	long	em_dl;		/* memory mapped D low register when swtched */
};

/* Intel prefers long real (53 bit) precision */
#define	__iBCS_NPXCW__		0x262
#define __BDE_NPXCW__		0x1272		/* FreeBSD */
#define	__OpenBSD_NPXCW__	0x127f

/*
 * The default MXCSR value at reset is 0x1f80, IA-32 Instruction
 * Set Reference, pg. 3-369.
 */
#define __INITIAL_MXCSR__       0x1f80

/*
 * The standard control word from finit is 0x37F, giving:
 *	round to nearest
 *	64-bit precision
 *	all exceptions masked.
 *
 * Now we want:
 *	affine mode (if we decide to support 287's)
 *	round to nearest
 *	53-bit precision
 *	all exceptions masked.
 *
 * 64-bit precision often gives bad results with high level languages
 * because it makes the results of calculations depend on whether
 * intermediate values are stored in memory or in FPU registers.
 *
 * The iBCS control word has underflow, overflow, zero divide, and invalid
 * operation exceptions unmasked.  But that causes an unexpected exception
 * in the test program 'paranoia' and makes denormals useless (DBL_MIN / 2
 * underflows).  It doesn't make a lot of sense to trap underflow without
 * trapping denormals.
 */

#define	__INITIAL_NPXCW__	__OpenBSD_NPXCW__

void    process_xmm_to_s87(const struct savexmm *, struct save87 *);
void    process_s87_to_xmm(const struct save87 *, struct savexmm *);

#endif /* !_I386_NPX_H_ */
