/*	$OpenBSD: vmparam.h,v 1.30 2005/04/11 15:13:01 deraadt Exp $	*/
/*	$NetBSD: vmparam.h,v 1.13 1997/07/12 16:20:03 perry Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)vmparam.h	8.1 (Berkeley) 6/11/93
 */

#ifndef _SPARC_VMPARAM_H_
#define _SPARC_VMPARAM_H_

/*
 * Machine dependent constants for Sun-4c SPARC
 */

/*
 * USRTEXT is the start of the user text/data space, while USRSTACK
 * is the top (end) of the user stack.
 */
#define	USRTEXT		0x2000			/* Start of user text */
#define	USRSTACK	KERNBASE		/* Start of user stack */

/*
 * Virtual memory related constants, all in bytes
 */
#ifndef MAXTSIZ
#define	MAXTSIZ		(16*1024*1024)		/* max text size */
#endif
#ifndef DFLDSIZ
#define	DFLDSIZ		(32*1024*1024)		/* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		(128*1024*1024)		/* max data size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		(512*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		MAXDSIZ			/* max stack size */
#endif

#define STACKGAP_RANDOM	64*1024
#define STACKGAP_RANDOM_SUN4M 256*1024

/*
 * Size of shared memory map
 */
#ifndef SHMMAXPGS
#define SHMMAXPGS	1024
#endif

/*
 * User/kernel map constants.  Note that sparc/vaddrs.h defines the
 * IO space virtual base, which must be the same as VM_MAX_KERNEL_ADDRESS:
 * tread with care.
 */
#define VM_MIN_ADDRESS		((vaddr_t)0)
#define VM_MAX_ADDRESS		((vaddr_t)KERNBASE)
#define VM_MAXUSER_ADDRESS	((vaddr_t)KERNBASE)
#define VM_MIN_KERNEL_ADDRESS	((vaddr_t)KERNBASE)
#define VM_MAX_KERNEL_ADDRESS	((vaddr_t)0xfe000000)

#define VM_PHYSSEG_MAX		32	/* we only have one "hole" */
#define VM_PHYSSEG_STRAT	VM_PSTRAT_BSEARCH
#define VM_PHYSSEG_NOADD		/* can't add RAM after vm_mem_init */

/*
 * pmap specific data stored in the vm_physmem[] array
 */


/* XXX - belongs in pmap.h, but put here because of ordering issues */
struct pvlist {
	struct		pvlist *pv_next;	/* next pvlist, if any */
	struct		pmap *pv_pmap;		/* pmap of this va */
	vaddr_t		pv_va;			/* virtual address */
	int		pv_flags;		/* flags (below) */
};

#define __HAVE_VM_PAGE_MD
struct vm_page_md {
	struct pvlist pv_head;
};

#define VM_MDPAGE_INIT(pg) do {			\
	(pg)->mdpage.pv_head.pv_next = NULL;	\
	(pg)->mdpage.pv_head.pv_pmap = NULL;	\
	(pg)->mdpage.pv_head.pv_va = 0;		\
	(pg)->mdpage.pv_head.pv_flags = 0;	\
} while (0)

#define VM_NFREELIST		1
#define VM_FREELIST_DEFAULT	0

#if defined (_KERNEL) && !defined(_LOCORE)
struct vm_map;
#define		dvma_mapin(map,va,len,canwait)	dvma_mapin_space(map,va,len,canwait,0)
vaddr_t		dvma_mapin_space(struct vm_map *, vaddr_t, int, int, int);
void		dvma_mapout(vaddr_t, vaddr_t, int);
#endif

#endif /* _SPARC_VMPARAM_H_ */
