/*	$NetBSD: dvma.c,v 1.3 1995/10/10 21:37:29 gwr Exp $	*/

/*
 * Copyright (c) 1995 Gordon W. Ross
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 4. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Gordon W. Ross
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/user.h>
#include <sys/core.h>
#include <sys/exec.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/reg.h>
#include <machine/pte.h>
#include <machine/pmap.h>
#include <machine/dvma.h>

#include "cache.h"

/* Resource map used by dvma_mapin/dvma_mapout */
#define	NUM_DVMA_SEGS ((DVMA_SEGMAP_SIZE / NBSG) + 1)
struct map dvma_segmap[NUM_DVMA_SEGS];

/* DVMA page map managed with help from the VM system. */
vm_map_t dvma_pgmap;
/* Note: Could use separate pagemap for obio if needed. */

void dvma_init()
{
	int size;

	/*
	 * Create the map used for small, permanent DVMA page
	 * allocations, such as may be needed by drivers for
	 * control structures shared with the device.
	 */
	dvma_pgmap = vm_map_create(pmap_kernel(),
	    DVMA_PAGEMAP_BASE, DVMA_PAGEMAP_END, TRUE);
	if (dvma_pgmap == NULL)
		panic("dvma_init: unable to create DVMA page map.");

	/*
	 * Create the VM pool used for mapping whole segments
	 * into DVMA space for the purpose of data transfer.
	 */
	rminit(dvma_segmap,
		   DVMA_SEGMAP_SIZE,
		   DVMA_SEGMAP_BASE,
		   "dvma_segmap",
		   NUM_DVMA_SEGS);
}

/*
 * Allocate actual memory pages in DVMA space.
 * (idea for implementation borrowed from Chris Torek.)
 */
caddr_t dvma_malloc(bytes)
	size_t bytes;
{
    caddr_t new_mem;
    vm_size_t new_size;

    if (!bytes)
		return NULL;
    new_size = sun3_round_page(bytes);
    new_mem = (caddr_t) kmem_alloc(dvma_pgmap, new_size);
    if (!new_mem)
		panic("dvma_malloc: no space in dvma_pgmap");
    /* The pmap code always makes DVMA pages non-cached. */
    return new_mem;
}

/*
 * Free pages from dvma_malloc()
 */
void dvma_free(addr, size)
	caddr_t	addr;
	size_t	size;
{
	kmem_free(dvma_pgmap, (vm_offset_t)addr, (vm_size_t)size);
}

/*
 * Given a DVMA address, return the physical address that
 * would be used by some OTHER bus-master besides the CPU.
 * (Examples: on-board ie/le, VME xy board).
 */
long dvma_kvtopa(kva, bustype)
	long kva;
	int bustype;
{
	long mask;

	if (kva < DVMA_SPACE_START || kva >= DVMA_SPACE_END)
		panic("dvma_kvtopa: bad dmva addr=0x%x\n", kva);

	switch (bustype) {
	case BUS_OBIO:
		mask = DVMA_OBIO_SLAVE_MASK;
		break;
	case BUS_VME16:
	case BUS_VME32:
		mask = DVMA_VME_SLAVE_MASK;
		break;
	default:
		panic("dvma_kvtopa: bad bus type %d\n", bustype);
	}

	return(kva & mask);
}

/*
 * Given a range of kernel virtual space, remap all the
 * pages found there into the DVMA space (dup mappings).
 * This IS safe to call at interrupt time.
 * (Typically called at SPLBIO)
 */
caddr_t dvma_mapin(char *kva, int len)
{
	vm_offset_t seg_kva, seg_dma, seg_len, seg_off;
	register vm_offset_t v, x;
	register int sme;
	int s;

	/* Get seg-aligned address and length. */
	seg_kva = (vm_offset_t)kva;
	seg_len = (vm_offset_t)len;
	/* seg-align beginning */
	seg_off = seg_kva & SEGOFSET;
	seg_kva -= seg_off;
	seg_len += seg_off;
	/* seg-align length */
	seg_len = sun3_round_seg(seg_len);

	s = splimp();

	/* Allocate the DVMA segment(s) */
	seg_dma = rmalloc(dvma_segmap, seg_len);

#ifdef	DIAGNOSTIC
	if (seg_dma & SEGOFSET)
		panic("dvma_mapin: seg not aligned");
#endif

	if (seg_dma != 0) {
		/* Duplicate the mappings into DMA space. */
		v = seg_kva;
		x = seg_dma;
		while (seg_len > 0) {
			sme = get_segmap(v);
#ifdef	DIAGNOSTIC
			if (sme == SEGINV)
				panic("dvma_mapin: seg not mapped");
#endif
#ifdef	HAVECACHE
			/* flush write-back on old mappings */
			if (cache_size)
				cache_flush_segment(v);
#endif
			set_segmap_allctx(x, sme);
			v += NBSG;
			x += NBSG;
			seg_len -= NBSG;
		}
		seg_dma += seg_off;
	}

	splx(s);
	return ((caddr_t)seg_dma);
}

/*
 * Free some DVMA space allocated by the above.
 * This IS safe to call at interrupt time.
 * (Typically called at SPLBIO)
 */
void dvma_mapout(char *dma, int len)
{
	vm_offset_t seg_dma, seg_len, seg_off;
	register vm_offset_t v, x;
	register int sme;
	int s;

	/* Get seg-aligned address and length. */
	seg_dma = (vm_offset_t)dma;
	seg_len = (vm_offset_t)len;
	/* seg-align beginning */
	seg_off = seg_dma & SEGOFSET;
	seg_dma -= seg_off;
	seg_len += seg_off;
	/* seg-align length */
	seg_len = sun3_round_seg(seg_len);

	s = splimp();

	/* Flush cache and remove DVMA mappings. */
	v = seg_dma;
	x = v + seg_len;
	while (v < x) {
		sme = get_segmap(v);
#ifdef	DIAGNOSTIC
		if (sme == SEGINV)
			panic("dvma_mapout: seg not mapped");
#endif
#ifdef	HAVECACHE
		/* flush write-back on the DVMA mappings */
		if (cache_size)
			cache_flush_segment(v);
#endif
		set_segmap_allctx(v, SEGINV);
		v += NBSG;
	}

	rmfree(dvma_segmap, seg_len, seg_dma);
	splx(s);
}
