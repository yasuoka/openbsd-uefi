/*	$OpenBSD: pmap_bootstrap.c,v 1.39 2007/11/02 19:18:54 martin Exp $	*/
/*	$NetBSD: pmap_bootstrap.c,v 1.50 1999/04/07 06:14:33 scottr Exp $	*/

/* 
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 *	@(#)pmap_bootstrap.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/msgbuf.h>
#include <sys/reboot.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_pmap.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/frame.h>
#include <machine/pmap.h>
#include <machine/pte.h>
#include <machine/vmparam.h>

#include <ufs/mfs/mfs_extern.h>

#include "zsc.h"

#if NZSC > 0
extern int	zsinited;
#endif

/*
 * These are used to map the RAM:
 */
int	numranges;	/* = 0 == don't use the ranges */
u_long	low[8];
u_long	high[8];
u_long	maxaddr;	/* PA of the last physical page */
int	vidlen;
#define VIDMAPSIZE	atop(vidlen)
extern u_int32_t	mac68k_vidphys;
extern u_int32_t	videoaddr;
extern u_int32_t	videorowbytes;
extern u_int32_t	videosize;
static u_int32_t	newvideoaddr;

void	bootstrap_mac68k(int);

/*
 * pmap_bootstrap() is very tricky on mac68k because it can be called
 * with the MMU either on or off.  If it's on, we assume that it's mapped
 * with the same PA <=> LA mapping that we eventually want.
 * The page sizes and the protections will be wrong, anyway.
 *
 * nextpa is the first address following the loaded kernel.  On a IIsi
 * on 12 May 1996, that was 0xf9000 beyond firstpa.
 */

#define	RELOC(v, t)	*((t*)((u_int)&(v)))
#define PA2VA(v, t)	*((t*)((u_int)&(v) - firstpa))

/*
 * Present a totally tricky view of the world here...
 * - count video mappings in the internal IO space (which will
 *   require some contortion later on)
 * - no external IO space
 */
#define	MACHINE_IIOMAPSIZE	(IIOMAPSIZE + VIDMAPSIZE)
#define	MACHINE_INTIOBASE	IOBase
#define	MACHINE_EIOMAPSIZE	0
	 
	/*	vidpa		internal video space for some machines
	 *			PT pages		VIDMAPSIZE pages
	 *
	 * XXX note that VIDMAPSIZE, hence vidlen, is needed very early
	 *     in pmap_bootstrap(), so slightly abuse the purpose of
	 *     PMAP_MD_LOCALS here...
	 */
#define	PMAP_MD_LOCALS \
	paddr_t vidpa; \
	paddr_t avail_next; \
	int avail_remaining; \
	int avail_range; \
	int i; \
	\
	vidlen = round_page(((videosize >> 16) & 0xffff) * videorowbytes + \
	    m68k_page_offset(mac68k_vidphys));

#define PMAP_MD_RELOC1() \
do { \
	vidpa = eiopa - VIDMAPSIZE * sizeof(pt_entry_t); \
} while (0)

	/*
	 * Validate the internal IO space, ROM space, and
	 * framebuffer PTEs (RW+CI).
	 *
	 * Note that this is done after the fake IIO space has been
	 * validated, hence the rom and video pte were already written to
	 * with incorrect values.
	 */
#define	PMAP_MD_MAPIOSPACE() \
do { \
	if (vidlen != 0) { \
		pte = PA2VA(vidpa, u_int *); \
		epte = pte + VIDMAPSIZE; \
		protopte = trunc_page(mac68k_vidphys) | \
		    PG_RW | PG_V | PG_CI; \
		while (pte < epte) { \
			*pte++ = protopte; \
			protopte += NBPG; \
		} \
	} \
} while (0)

#define	PMAP_MD_RELOC2() \
do { \
	IOBase = iiobase; \
	if (vidlen != 0) { \
		newvideoaddr = iiobase + ptoa(IIOMAPSIZE) \
				+ m68k_page_offset(mac68k_vidphys); \
	} \
} while (0)

/*
 * If the MMU was disabled, compute available memory size from the memory
 * segment information.
 */
#define	PMAP_MD_MEMSIZE() \
do { \
	avail_next = avail_start; \
	avail_remaining = 0; \
	avail_range = -1; \
	for (i = 0; i < numranges; i++) { \
		if (low[i] <= avail_next && avail_next < high[i]) { \
			avail_range = i; \
			avail_remaining = high[i] - avail_next; \
		} else if (avail_range != -1) { \
			avail_remaining += (high[i] - low[i]); \
		} \
	} \
	physmem = atop(avail_remaining + nextpa - firstpa); \
 \
	maxaddr = high[numranges - 1] - ptoa(1); \
	high[numranges - 1] -= \
	    (round_page(MSGBUFSIZE) + ptoa(1)); \
	avail_end = high[numranges - 1]; \
} while (0)

#define PMAP_MD_RELOC3()	/* nothing */

#include <m68k/m68k/pmap_bootstrap.c>

void
bootstrap_mac68k(tc)
	int	tc;
{
#if NZSC > 0
	extern void	zs_init(void);
#endif
	extern caddr_t	esym;
	paddr_t		nextpa;

	if (mac68k_machine.do_graybars)
		printf("Bootstrapping OpenBSD/mac68k.\n");

	mac68k_vidphys = videoaddr;

	if (((tc & 0x80000000) && (mmutype == MMU_68030)) ||
	    ((tc & 0x8000) && (mmutype == MMU_68040))) {

		if (mac68k_machine.do_graybars)
			printf("Getting mapping from MMU.\n");
		(void) get_mapping();
		if (mac68k_machine.do_graybars)
			printf("Done.\n");
	} else {
		/* MMU not enabled.  Fake up ranges. */
		numranges = 1;
		low[0] = 0;
		high[0] = mac68k_machine.mach_memsize * (1024 * 1024);
		if (mac68k_machine.do_graybars)
			printf("Faked range to byte 0x%lx.\n", high[0]);
	}
	nextpa = load_addr + round_page((vaddr_t)esym);

	if (mac68k_machine.do_graybars)
		printf("Bootstrapping the pmap system.\n");

	pmap_bootstrap(nextpa, load_addr);

	if (mac68k_machine.do_graybars)
		printf("Pmap bootstrapped.\n");

	if (!vidlen)
		panic("Don't know how to relocate video!");

	if (mac68k_machine.do_graybars)
		printf("Video address 0x%lx -> 0x%lx.\n",
		    (unsigned long)videoaddr, (unsigned long)newvideoaddr);

	mac68k_set_io_offsets(IOBase);

	/*
	 * If the serial ports are going (for console or 'echo'), then
	 * we need to make sure the IO change gets propagated properly.
	 * This resets the base addresses for the 8530 (serial) driver.
	 *
	 * WARNING!!! No printfs() (etc) BETWEEN zs_init() and the end
	 * of this function (where we start using the MMU, so the new
	 * address is correct).
	 */
#if NZSC > 0
	if (zsinited != 0)
		zs_init();
#endif

	videoaddr = newvideoaddr;
}

void
pmap_init_md()
{
	vaddr_t addr;

	/*
	 * Mark as unavailable the regions which we have mapped in
	 * pmap_bootstrap().
	 */
	addr = (vaddr_t)MACHINE_INTIOBASE;
	if (uvm_map(kernel_map, &addr,
		    ptoa(MACHINE_IIOMAPSIZE),
		    NULL, UVM_UNKNOWN_OFFSET, 0,
		    UVM_MAPFLAG(UVM_PROT_NONE, UVM_PROT_NONE,
				UVM_INH_NONE, UVM_ADV_RANDOM,
				UVM_FLAG_FIXED)))
		panic("pmap_init: bogons in the VM system!");
}
