/* $OpenBSD: pmap.h,v 1.3 1998/09/12 03:14:49 mickey Exp $ */

/*
 * Copyright 1996 1995 by Open Software Foundation, Inc.   
 *              All Rights Reserved 
 *  
 * Permission to use, copy, modify, and distribute this software and 
 * its documentation for any purpose and without fee is hereby granted, 
 * provided that the above copyright notice appears in all copies and 
 * that both the copyright notice and this permission notice appear in 
 * supporting documentation. 
 *  
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE 
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE. 
 *  
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR 
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT, 
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION 
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. 
 */
/* 
 * Copyright (c) 1990,1993,1994 The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 * 	Utah $Hdr: pmap.h 1.24 94/12/14$
 *	Author: Mike Hibler, Bob Wheeler, University of Utah CSL, 9/90
 */

/*
 *	Pmap header for hppa.
 */

#ifndef	_HPPA_PMAP_H_
#define	_HPPA_PMAP_H_

#include <machine/pte.h>

#define EQUIV_HACK	/* no multiple mapping of kernel equiv space allowed */

#define BTLB		/* Use block TLBs: PA 1.1 and above */

/*
 * This hash function is the one used by the hardware TLB walker on the 7100.
 */
#define pmap_hash(space, va) \
	((((u_int)(space) << 5) ^ (((u_int)va) >> 12)) & (hpt_hashsize-1))

typedef
struct pmap {
	TAILQ_ENTRY(pmap)	pmap_list;	/* pmap free list */
	struct simplelock	pmap_lock;	/* lock on map */
	int			pmap_refcnt;	/* reference count */
	pa_space_t		pmap_space;	/* space for this pmap */
	u_int			pmap_pid;	/* protection id for pmap */
	struct pmap_statistics	pmap_stats;	/* statistics */
} *pmap_t;
extern pmap_t	kernel_pmap;			/* The kernel's map */

/*
 * If HPT is defined, we cache the last miss for each bucket using a
 * structure defined for the 7100 hardware TLB walker. On non-7100s, this
 * acts as a software cache that cuts down on the number of times we have
 * to search the hash chain. (thereby reducing the number of instructions
 * and cache misses incurred during the TLB miss).
 *
 * The pv_entry pointer is the address of the associated hash bucket
 * list for fast tlbmiss search.
 */
struct hpt_entry {
	u_int	hpt_valid:1,	/* Valid bit */
		hpt_vpn:15,	/* Virtual Page Number */
		hpt_space:16;	/* Space ID */
	u_int	hpt_tlbprot;	/* prot/access rights (for TLB load) */
	u_int	hpt_tlbpage;	/* physical page (<<5 for TLB load) */
	void	*hpt_entry;	/* Pointer to associated hash list */
};
#ifdef _KERNEL
extern struct hpt_entry *hpt_table;
extern u_int hpt_hashsize;
#endif /* _KERNEL */

/* keep it at 32 bytes for the cache overall satisfaction */
struct pv_entry {
	struct pv_entry	*pv_next;	/* list of mappings of a given PA */
	struct pv_entry *pv_hash;	/* VTOP hash bucket list */
	pmap_t		pv_pmap;	/* back link to pmap */
	u_int		pv_space;	/* copy of space id from pmap */
	u_int		pv_va;		/* virtual page number */
	u_int		pv_tlbprot;	/* TLB format protection */
	u_int		pv_tlbpage;	/* physical page (for TLB load) */
	u_int		pv_tlbsw;
};

#define NPVPPG (NBPG/32-1)
struct pv_page {
	TAILQ_ENTRY(pv_page) pvp_list;	/* Chain of pages */
	u_int		pvp_nfree;
	struct pv_entry *pvp_freelist;
	u_int		pvp_flag;	/* is it direct mapped */ 
	u_int		pvp_pad[3];	/* align to 32 */
	struct pv_entry pvp_pv[NPVPPG];
};

struct pmap_physseg {
	struct pv_entry *pvent;
};

#define MAX_PID		0xfffa
#define	HPPA_SID_KERNEL	0
#define	HPPA_PID_KERNEL	2

#define KERNEL_ACCESS_ID 1

#define KERNEL_TEXT_PROT (TLB_AR_KRX | (KERNEL_ACCESS_ID << 1))
#define KERNEL_DATA_PROT (TLB_AR_KRW | (KERNEL_ACCESS_ID << 1))

#ifdef _KERNEL
#define cache_align(x)	(((x) + dcache_line_mask) & ~(dcache_line_mask))
extern int dcache_line_mask;

#define	PMAP_STEAL_MEMORY	/* we have some memory to steal */

#define pmap_kernel_va(VA)	\
	(((VA) >= VM_MIN_KERNEL_ADDRESS) && ((VA) <= VM_MAX_KERNEL_ADDRESS))

#define pmap_kernel()			(kernel_pmap)
#define	pmap_resident_count(pmap)	((pmap)->pmap_stats.resident_count)
#define pmap_reference(pmap) \
do { if (pmap) { \
	simple_lock(&pmap->pmap_lock); \
	pmap->pmap_refcnt++; \
	simple_unlock(&pmap->pmap_lock); \
} } while (0)
#define pmap_collect(pmap)
#define pmap_release(pmap)
#define pmap_pageable(pmap, start, end, pageable)
#define pmap_copy(dpmap,spmap,da,len,sa)
#define	pmap_update()
#define	pmap_activate(pmap, pcb)
#define	pmap_deactivate(pmap, pcb)

#define pmap_phys_address(x)	((x) << PGSHIFT)
#define pmap_phys_to_frame(x)	((x) >> PGSHIFT)

static __inline int
pmap_prot(struct pmap *pmap, int prot)
{
	extern u_int kern_prot[], user_prot[];
	return (pmap == kernel_pmap? kern_prot: user_prot)[prot];
}

/* 
 * prototypes.
 */
vm_offset_t kvtophys __P((vm_offset_t addr));
vm_offset_t pmap_map __P((vm_offset_t, vm_offset_t, vm_offset_t, vm_prot_t));
void pmap_bootstrap __P((vm_offset_t *,	vm_offset_t *));
#endif /* _KERNEL */

#endif /* _HPPA_PMAP_H_ */
