/*	$OpenBSD: vmparam.h,v 1.8 2001/03/29 18:52:19 drahn Exp $	*/
/*	$NetBSD: vmparam.h,v 1.1 1996/09/30 16:34:38 ws Exp $	*/

/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MACHINE_VMPARAM_H
#define MACHINE_VMPARAM_H

#define	USRTEXT		CLBYTES
#define	USRSTACK	VM_MAXUSER_ADDRESS

#ifndef	MAXTSIZ
#define	MAXTSIZ		(16*1024*1024)		/* max text size */
#endif

#ifndef	DFLDSIZ
#define	DFLDSIZ		(32*1024*1024)		/* default data size */
#endif

#ifndef	MAXDSIZ
#define	MAXDSIZ		(512*1024*1024)		/* max data size */
#endif

#ifndef	DFLSSIZ
#define	DFLSSIZ		(1*1024*1024)		/* default stack size */
#endif

#ifndef	MAXSSIZ
#define	MAXSSIZ		(32*1024*1024)		/* max stack size */
#endif

/*
 * Min & Max swap space allocation chunks
 */
#define	DMMIN		32
#define	DMMAX		4096

/*
 * Size of shared memory map
 */
#ifndef	SHMMAXPGS
#define	SHMMAXPGS	1024
#endif

/*
 * Size of User Raw I/O map
 */
#define	USRIOSIZE	1024

/*
 * The time for a process to be blocked before being very swappable.
 * This is a number of seconds which the system takes as being a non-trivial
 * amount of real time.  You probably shouldn't change this;
 * it is used in subtle ways (fractions and multiples of it are, that is, like
 * half of a ``long time'', almost a long time, etc.)
 * It is related to human patience and other factors which don't really
 * change over time.
 */
#define	MAXSLP 		20

/*
 * Would like to have MAX addresses = 0, but this doesn't (currently) work
 */
#define	VM_MIN_ADDRESS		((vm_offset_t)0)
#define	VM_MAXUSER_ADDRESS	((vm_offset_t)0xfffff000)
#define	VM_MAX_ADDRESS		VM_MAXUSER_ADDRESS
#define	VM_MIN_KERNEL_ADDRESS	((vm_offset_t)(KERNEL_SR << ADDR_SR_SHFT))

/* ppc_kvm_size is so that vm space can be stolen before vm is fully
 * initialized.
 */
#define VM_KERN_ADDR_SIZE_DEF SEGMENT_LENGTH
extern vm_offset_t ppc_kvm_size;
#define VM_KERN_ADDRESS_SIZE  (ppc_kvm_size)
#define	VM_MAX_KERNEL_ADDRESS	((vm_offset_t)((KERNEL_SR << ADDR_SR_SHFT) \
						+ VM_KERN_ADDRESS_SIZE))

#define	MACHINE_NEW_NONCONTIG	/* VM <=> pmap interface modifier */

#define	VM_KMEM_SIZE		(NKMEMCLUSTERS * CLBYTES)
#define	VM_MBUF_SIZE		(NMBCLUSTERS * CLBYTES)
#define	VM_PHYS_SIZE		(USRIOSIZE * CLBYTES)

struct pmap_physseg {
	struct pv_entry *pvent;
	char *attrs;
	/* NULL ??? */
};

#define	VM_PHYSSEG_MAX	32	/* actually we could have this many segments */
#define	VM_PHYSSEG_STRAT	VM_PSTRAT_BSEARCH
#define	VM_PHYSSEG_NOADD	/* can't add RAM after vm_mem_init */

#define VM_NFREELIST		1
#define VM_FREELIST_DEFAULT	0

#endif
