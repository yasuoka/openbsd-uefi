/*	$OpenBSD: m88410.h,v 1.10 2004/04/24 19:51:48 miod Exp $ */
/*
 * Copyright (c) 2001 Steve Murphree, Jr.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Steve Murphree.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef	__M88410_H__
#define	__M88410_H__

#ifdef _KERNEL

/*
 *	MC88410 External Cache Controller definitions
 *	This is only available on MVME197DP/SP models.
 */

#include <machine/asm_macro.h>
#include <machine/psl.h>
#include <mvme88k/dev/busswreg.h>

#define XCC_NOP		"0x00"
#define XCC_FLUSH_PAGE	"0x01"
#define XCC_FLUSH_ALL	"0x02"
#define XCC_INVAL_ALL	"0x03"
#define XCC_ADDR	0xff800000

static __inline__ void
mc88410_flush_page(paddr_t physaddr)
{
	paddr_t xccaddr = XCC_ADDR | (physaddr >> PGSHIFT);
        m88k_psr_type psr;
	u_int16_t bs_gcsr, bs_romcr;

	bs_gcsr = *(volatile u_int16_t *)(BS_BASE + BS_GCSR);
	bs_romcr = *(volatile u_int16_t *)(BS_BASE + BS_ROMCR);
	psr = get_psr();
	/* mask misaligned exceptions */
	set_psr(psr | PSR_MXM);
	/* clear WEN0 and WEN1 in ROMCR (disables writes to FLASH) */
	*(volatile u_int16_t *)(BS_BASE + BS_ROMCR) =
	    bs_romcr & ~(BS_ROMCR_WEN0 | BS_ROMCR_WEN1);
	/* set XCC bit in GCSR (0xff8xxxxx now decodes to mc88410) */
	*(volatile u_int16_t *)(BS_BASE + BS_GCSR) = bs_gcsr | BS_GCSR_XCC;

	/* load the value of upper32 into r2 */
	__asm__ __volatile__("or   r2,r0," XCC_FLUSH_PAGE);
	/* load the value of lower32 into r3 (always 0) */
	__asm__ __volatile__("or   r3,r0,r0");
	/* load the value of xccaddr into r4 */
	__asm__ __volatile__("or.u r5,r0,hi16(%0)" : : "r" (xccaddr));
	__asm__ __volatile__("ld   r4,r5,lo16(%0)" : : "r" (xccaddr));
	/* make the double write. bang! */
	__asm__ __volatile__("st.d r2,r4,0");

	/* spin until the operation starts */
	while ((*(volatile u_int32_t *)(BS_BASE + BS_XCCR) & BS_XCC_FBSY) == 0)
		;

	/* restore PSR and friends */
        set_psr(psr);
	flush_pipeline();
	*(volatile u_int16_t *)(BS_BASE + BS_GCSR) = bs_gcsr;
	*(volatile u_int16_t *)(BS_BASE + BS_ROMCR) = bs_romcr;
}

static __inline__ void
mc88410_flush(void)
{
        m88k_psr_type psr;
	u_int16_t bs_gcsr, bs_romcr;

	bs_gcsr = *(volatile u_int16_t *)(BS_BASE + BS_GCSR);
	bs_romcr = *(volatile u_int16_t *)(BS_BASE + BS_ROMCR);
	psr = get_psr();
	/* mask misaligned exceptions */
	set_psr(psr | PSR_MXM);
	/* clear WEN0 and WEN1 in ROMCR (disables writes to FLASH) */
	*(volatile u_int16_t *)(BS_BASE + BS_ROMCR) =
	    bs_romcr & ~(BS_ROMCR_WEN0 | BS_ROMCR_WEN1);
	/* set XCC bit in GCSR (0xFF8xxxxx now decodes to mc88410) */
	*(volatile u_int16_t *)(BS_BASE + BS_GCSR) = bs_gcsr | BS_GCSR_XCC;

	/* load the value of upper32 into r2 */
	__asm__ __volatile__("or   r2,r0," XCC_FLUSH_ALL);
	/* load the value of lower32 into r3 (always 0) */
	__asm__ __volatile__("or   r3,r0,r0");
	/* load the value of xccaddr into r4 */
	__asm__ __volatile__("or.u r5,r0,hi16(0xff800000)");
	__asm__ __volatile__("or   r4,r5,r0");
	/* make the double write. bang! */
	__asm__ __volatile__("st.d r2,r4,0");

	/* spin until the operation starts */
	while ((*(volatile u_int32_t *)(BS_BASE + BS_XCCR) & BS_XCC_FBSY) == 0)
		;

	/* restore PSR and friends */
        set_psr(psr);
	flush_pipeline();
	*(volatile u_int16_t *)(BS_BASE + BS_GCSR) = bs_gcsr;
	*(volatile u_int16_t *)(BS_BASE + BS_ROMCR) = bs_romcr;
}

static __inline__ void
mc88410_inval(void)
{
        m88k_psr_type psr;
	u_int16_t bs_gcsr, bs_romcr;

	bs_gcsr = *(volatile u_int16_t *)(BS_BASE + BS_GCSR);
	bs_romcr = *(volatile u_int16_t *)(BS_BASE + BS_ROMCR);

	psr = get_psr();
	/* mask misaligned exceptions */
	set_psr(psr | PSR_MXM);
	/* clear WEN0 and WEN1 in ROMCR (disables writes to FLASH) */
	*(volatile u_int16_t *)(BS_BASE + BS_ROMCR) =
	    bs_romcr & ~(BS_ROMCR_WEN0 | BS_ROMCR_WEN1);
	/* set XCC bit in GCSR (0xFF8xxxxx now decodes to mc88410) */
	*(volatile u_int16_t *)(BS_BASE + BS_GCSR) = bs_gcsr | BS_GCSR_XCC;

	/* load the value of upper32 into r2 */
	__asm__ __volatile__("or   r2,r0," XCC_INVAL_ALL);
	/* load the value of lower32 into r3 (always 0) */
	__asm__ __volatile__("or   r3,r0,r0");
	/* load the value of xccaddr into r4 */
	__asm__ __volatile__("or.u r5,r0,hi16(0xff800000)");
	__asm__ __volatile__("or   r4,r5,r0");
	/* make the double write. bang! */
	__asm__ __volatile__("st.d r2,r4,0");

	/* spin until the operation starts */
	while ((*(volatile u_int32_t *)(BS_BASE + BS_XCCR) & BS_XCC_FBSY) == 0)
		;

	/* restore PSR and friends */
        set_psr(psr);
	flush_pipeline();
	*(volatile u_int16_t *)(BS_BASE + BS_GCSR) = bs_gcsr;
	*(volatile u_int16_t *)(BS_BASE + BS_ROMCR) = bs_romcr;
}

static __inline__ void
mc88410_sync(void)
{
	mc88410_flush();
	mc88410_inval();
}

static __inline__ int
mc88410_present(void)
{
	return (*(volatile u_int16_t *)(BS_BASE + BS_GCSR)) & BS_GCSR_B410;
}

#endif	/* _KERNEL */

#endif	/* __M88410_H__ */
