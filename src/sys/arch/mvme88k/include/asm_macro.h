/*	$OpenBSD: asm_macro.h,v 1.10 2001/03/08 00:03:22 miod Exp $ */
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#ifndef __MACHINE_M88K_ASM_MACRO_H__
#define __MACHINE_M88K_ASM_MACRO_H__

/*
 ** Various compiler macros used for speed and efficiency.
 ** Anyone can include.
  */

/*
 * PSR_TYPE is the type of the Process Status Register.
 */
typedef unsigned long m88k_psr_type;

/*
 * disable_interrupts_return_psr()
 *
 *    The INTERRUPT_DISABLE bit is set in the PSR and the *PREVIOUS*
 *    PSR is returned.  Intended to be used with set_psr() [below] as in:
 *
 *	{
 *	    m88k_psr_type psr;
 *	        .
 *	        .
 *	    psr = disable_interrupts_return_psr();
 *	        .
 *   		SHORT [time-wise] CRITICAL SECTION HERE
 *	        .
 *	    set_psr(psr);
 *	        .
 *	        .
 */
static __inline__ m88k_psr_type disable_interrupts_return_psr(void)
{
	m88k_psr_type temp, oldpsr;
	__asm__ __volatile__ ("ldcr %0, cr1" : "=r" (oldpsr));
	__asm__ __volatile__ ("set  %1, %0, 1<1>" : "=r" (oldpsr), "=r" (temp));
	__asm__ __volatile__ ("stcr %0, cr1" : "=r" (temp));
	__asm__ __volatile__ ("tcnd ne0, r0, 0");
	return oldpsr;
}
#define disable_interrupt() (void)disable_interrupts_return_psr()

/*
 * Sets the PSR. See comments above.
 */
static __inline__ void set_psr(m88k_psr_type psr)
{
	__asm__ __volatile__ ("stcr %0, cr1" :: "r" (psr));
}

/*
 * Enables interrupts.
 */
static __inline__ m88k_psr_type enable_interrupts_return_psr(void)
{
	m88k_psr_type temp, oldpsr; /* need a temporary register */
	__asm__ __volatile__ ("ldcr %0, cr1" : "=r" (oldpsr));
	__asm__ __volatile__ ("clr  %1, %0, 1<1>" : "=r" (oldpsr), "=r" (temp));
	__asm__ __volatile__ ("stcr %0, cr1" : "=r" (temp));
	return oldpsr;
}
#define enable_interrupt() (void)enable_interrupts_return_psr()

#define db_enable_interrupt enable_interrupt
#define db_disable_interrupt disable_interrupt

/*
 * flushes the data pipeline.
 */
static __inline__ void flush_pipeline(void)
{
	__asm__ __volatile__ ("tcnd ne0, r0, 0");
}
#define db_flush_pipeline flush_pipeline

/*
 * gets the current stack pointer.
 */
static __inline__ unsigned long stack_pointer(void)
{
	register unsigned long sp;
	__asm__ __volatile__ ("or %0,r0,r31" : "=r" (sp));
	return(sp);
}


#endif __MACHINE_M88K_ASM_MACRO_H__
