/*	$OpenBSD: m197_machdep.c,v 1.2 2004/11/08 16:39:31 miod Exp $	*/
/*
 * Copyright (c) 1998, 1999, 2000, 2001 Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
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
 *      This product includes software developed by Nivas Madhur.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>

#include <machine/asm_macro.h>
#include <machine/cmmu.h>
#include <machine/cpu.h>
#include <machine/locore.h>
#include <machine/reg.h>
#include <machine/trap.h>
#include <machine/mvme197.h>

#include <uvm/uvm_extern.h>

#include <mvme88k/dev/busswreg.h>

void	m197_bootstrap(void);
void	m197_ext_int(u_int, struct trapframe *);
vaddr_t	m197_memsize(void);
void	m197_setupiackvectors(void);
void	m197_startup(void);

vaddr_t obiova;
vaddr_t flashva;

/*
 * Figure out how much real memory is available.
 * Start looking from the megabyte after the end of the kernel data,
 * until we find non-memory.
 */
vaddr_t
m197_memsize()
{
	unsigned int *volatile look;
	unsigned int *max;
	extern char *end;
#define PATTERN   0x5a5a5a5a
#define STRIDE    (4*1024) 	/* 4k at a time */
#define Roundup(value, stride) (((unsigned)(value) + (stride) - 1) & ~((stride)-1))
	/*
	 * count it up.
	 */
#define	MAXPHYSMEM	0x30000000	/* 768MB */
	max = (void *)MAXPHYSMEM;
	for (look = (void *)Roundup(end, STRIDE); look < max;
	    look = (int *)((unsigned)look + STRIDE)) {
		unsigned save;

		/* if can't access, we've reached the end */
		if (badwordaddr((vaddr_t)look)) {
#if defined(DEBUG)
			printf("%x\n", look);
#endif
			look = (int *)((int)look - STRIDE);
			break;
		}

		/*
		 * If we write a value, we expect to read the same value back.
		 * We'll do this twice, the 2nd time with the opposite bit
		 * pattern from the first, to make sure we check all bits.
		 */
		save = *look;
		if (*look = PATTERN, *look != PATTERN)
			break;
		if (*look = ~PATTERN, *look != ~PATTERN)
			break;
		*look = save;
	}

	return (trunc_page((unsigned)look));
}

void
m197_startup()
{
	/*
	 * Grab the FLASH space that we hardwired in pmap_bootstrap
	 */
	flashva = FLASH_START;
	uvm_map(kernel_map, (vaddr_t *)&flashva, FLASH_SIZE,
	    NULL, UVM_UNKNOWN_OFFSET, 0,
	      UVM_MAPFLAG(UVM_PROT_NONE, UVM_PROT_NONE, UVM_INH_NONE,
	        UVM_ADV_NORMAL, 0));
	if (flashva != FLASH_START)
		panic("flashva %lx: FLASH not free", flashva);

	/*
	 * Grab the OBIO space that we hardwired in pmap_bootstrap
	 */
	obiova = OBIO_START;
	uvm_map(kernel_map, (vaddr_t *)&obiova, OBIO_SIZE,
	    NULL, UVM_UNKNOWN_OFFSET, 0,
	      UVM_MAPFLAG(UVM_PROT_NONE, UVM_PROT_NONE, UVM_INH_NONE,
	        UVM_ADV_NORMAL, 0));
	if (obiova != OBIO_START)
		panic("obiova %lx: OBIO not free", obiova);
}

void
m197_setupiackvectors()
{
	u_int8_t *vaddr = (u_int8_t *)M197_IACK;

	ivec[0] = vaddr + 0x03;	/* We dont use level 0 */
	ivec[1] = vaddr + 0x07;
	ivec[2] = vaddr + 0x0b;
	ivec[3] = vaddr + 0x0f;
	ivec[4] = vaddr + 0x13;
	ivec[5] = vaddr + 0x17;
	ivec[6] = vaddr + 0x1b;
	ivec[7] = vaddr + 0x1f;
}

/*
 * Device interrupt handler for MVME197
 */

void
m197_ext_int(u_int v, struct trapframe *eframe)
{
	int mask, level, src;
	struct intrhand *intr;
	intrhand_t *list;
	int ret;
	u_char vec;

	/* get src and mask */
	mask = *md_intr_mask & 0x07;
	src = *(u_int8_t *)M197_ISRC;

	if (v == T_NON_MASK) {
		/* This is the abort switch */
		level = IPL_NMI;
		vec = BS_ABORTVEC;
	} else {
		/* get level  */
		level = *(u_int8_t *)M197_ILEVEL & 0x07;
	}

#ifdef DIAGNOSTIC
	/*
	 * Interrupting level cannot be 0--0 doesn't produce an interrupt.
	 * Weird! XXX nivas
	 */

	if (level == 0) {
		panic("Bogons... level %x and mask %x", level, mask);
	}
#endif

	uvmexp.intrs++;

	if (v != T_NON_MASK) {
		/* generate IACK and get the vector */
		flush_pipeline();
		if (guarded_access(ivec[level], 1, &vec) == EFAULT) {
			panic("Unable to get vector for this interrupt (level %x)", level);
		}
		flush_pipeline();
		flush_pipeline();
		flush_pipeline();
	}

	if (v != T_NON_MASK || cold == 0) {
		/* block interrupts at level or lower */
		setipl(level);

		enable_interrupt();
	}

	list = &intr_handlers[vec];
	if (SLIST_EMPTY(list)) {
		/* increment intr counter */
		intrcnt[M88K_SPUR_IRQ]++;
		printf("Spurious interrupt (level %x and vec %x)\n",
		       level, vec);
	} else {
#ifdef DIAGNOSTIC
		intr = SLIST_FIRST(list);
		if (intr->ih_ipl != level) {
			panic("Handler ipl %x not the same as level %x. "
			    "vec = 0x%x",
			    intr->ih_ipl, level, vec);
		}
#endif

		/*
		 * Walk through all interrupt handlers in the chain for the
		 * given vector, calling each handler in turn, till some handler
		 * returns a value != 0.
		 */

		ret = 0;
		SLIST_FOREACH(intr, list, ih_link) {
			if (intr->ih_wantframe != 0)
				ret = (*intr->ih_fn)((void *)eframe);
			else
				ret = (*intr->ih_fn)(intr->ih_arg);
			if (ret != 0) {
				intrcnt[level]++;
				intr->ih_count.ec_count++;
				break;
			}
		}

		if (ret == 0) {
			printf("Unclaimed interrupt (level %x and vec %x)\n",
			    level, vec);
		}
	}

	if (v != T_NON_MASK || cold == 0) {
		disable_interrupt();

		/*
		 * Restore the mask level to what it was when the interrupt
		 * was taken.
		 */
		setipl(mask);
	}
}

void
m197_bootstrap()
{
	extern struct cmmu_p cmmu88110;
	extern void set_tcfp(void);

	cmmu = &cmmu88110;
	md_interrupt_func_ptr = &m197_ext_int;
	md_intr_mask = (u_char *)M197_IMASK;
	set_tcfp(); /* Set Time Critical Floating Point Mode */
}
