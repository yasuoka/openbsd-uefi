/*	$OpenBSD: uvm_km.c,v 1.77 2010/06/27 03:03:49 thib Exp $	*/
/*	$NetBSD: uvm_km.c,v 1.42 2001/01/14 02:10:01 thorpej Exp $	*/

/* 
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
 * Copyright (c) 1991, 1993, The Regents of the University of California.  
 *
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *	This product includes software developed by Charles D. Cranor,
 *      Washington University, the University of California, Berkeley and 
 *      its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)vm_kern.c   8.3 (Berkeley) 1/12/94
 * from: Id: uvm_km.c,v 1.1.2.14 1998/02/06 05:19:27 chs Exp
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
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

/*
 * uvm_km.c: handle kernel memory allocation and management
 */

/*
 * overview of kernel memory management:
 *
 * the kernel virtual address space is mapped by "kernel_map."   kernel_map
 * starts at VM_MIN_KERNEL_ADDRESS and goes to VM_MAX_KERNEL_ADDRESS.
 * note that VM_MIN_KERNEL_ADDRESS is equal to vm_map_min(kernel_map).
 *
 * the kernel_map has several "submaps."   submaps can only appear in 
 * the kernel_map (user processes can't use them).   submaps "take over"
 * the management of a sub-range of the kernel's address space.  submaps
 * are typically allocated at boot time and are never released.   kernel
 * virtual address space that is mapped by a submap is locked by the 
 * submap's lock -- not the kernel_map's lock.
 *
 * thus, the useful feature of submaps is that they allow us to break
 * up the locking and protection of the kernel address space into smaller
 * chunks.
 *
 * the vm system has several standard kernel submaps, including:
 *   kmem_map => contains only wired kernel memory for the kernel
 *		malloc.   *** access to kmem_map must be protected
 *		by splvm() because we are allowed to call malloc()
 *		at interrupt time ***
 *   pager_map => used to map "buf" structures into kernel space
 *   exec_map => used during exec to handle exec args
 *   etc...
 *
 * the kernel allocates its private memory out of special uvm_objects whose
 * reference count is set to UVM_OBJ_KERN (thus indicating that the objects
 * are "special" and never die).   all kernel objects should be thought of
 * as large, fixed-sized, sparsely populated uvm_objects.   each kernel 
 * object is equal to the size of kernel virtual address space (i.e. the
 * value "VM_MAX_KERNEL_ADDRESS - VM_MIN_KERNEL_ADDRESS").
 *
 * most kernel private memory lives in kernel_object.   the only exception
 * to this is for memory that belongs to submaps that must be protected
 * by splvm(). each of these submaps manages their own pages.
 *
 * note that just because a kernel object spans the entire kernel virtual
 * address space doesn't mean that it has to be mapped into the entire space.
 * large chunks of a kernel object's space go unused either because 
 * that area of kernel VM is unmapped, or there is some other type of 
 * object mapped into that range (e.g. a vnode).    for submap's kernel
 * objects, the only part of the object that can ever be populated is the
 * offsets that are managed by the submap.
 *
 * note that the "offset" in a kernel object is always the kernel virtual
 * address minus the VM_MIN_KERNEL_ADDRESS (aka vm_map_min(kernel_map)).
 * example:
 *   suppose VM_MIN_KERNEL_ADDRESS is 0xf8000000 and the kernel does a
 *   uvm_km_alloc(kernel_map, PAGE_SIZE) [allocate 1 wired down page in the
 *   kernel map].    if uvm_km_alloc returns virtual address 0xf8235000,
 *   then that means that the page at offset 0x235000 in kernel_object is
 *   mapped at 0xf8235000.   
 *
 * kernel objects have one other special property: when the kernel virtual
 * memory mapping them is unmapped, the backing memory in the object is
 * freed right away.   this is done with the uvm_km_pgremove() function.
 * this has to be done because there is no backing store for kernel pages
 * and no need to save them after they are no longer referenced.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kthread.h>

#include <uvm/uvm.h>

/*
 * global data structures
 */

struct vm_map *kernel_map = NULL;

/*
 * local data structues
 */

static struct vm_map		kernel_map_store;

/*
 * uvm_km_init: init kernel maps and objects to reflect reality (i.e.
 * KVM already allocated for text, data, bss, and static data structures).
 *
 * => KVM is defined by VM_MIN_KERNEL_ADDRESS/VM_MAX_KERNEL_ADDRESS.
 *    we assume that [min -> start] has already been allocated and that
 *    "end" is the end.
 */

void
uvm_km_init(vaddr_t start, vaddr_t end)
{
	vaddr_t base = VM_MIN_KERNEL_ADDRESS;

	/*
	 * next, init kernel memory objects.
	 */

	/* kernel_object: for pageable anonymous kernel memory */
	uao_init();
	uvm.kernel_object = uao_create(VM_MAX_KERNEL_ADDRESS -
				 VM_MIN_KERNEL_ADDRESS, UAO_FLAG_KERNOBJ);

	/*
	 * init the map and reserve already allocated kernel space 
	 * before installing.
	 */

	uvm_map_setup(&kernel_map_store, base, end, VM_MAP_PAGEABLE);
	kernel_map_store.pmap = pmap_kernel();
	if (base != start && uvm_map(&kernel_map_store, &base, start - base,
	    NULL, UVM_UNKNOWN_OFFSET, 0, UVM_MAPFLAG(UVM_PROT_ALL, UVM_PROT_ALL,
	    UVM_INH_NONE, UVM_ADV_RANDOM,UVM_FLAG_FIXED)) != 0)
		panic("uvm_km_init: could not reserve space for kernel");
	
	/*
	 * install!
	 */

	kernel_map = &kernel_map_store;
}

/*
 * uvm_km_suballoc: allocate a submap in the kernel map.   once a submap
 * is allocated all references to that area of VM must go through it.  this
 * allows the locking of VAs in kernel_map to be broken up into regions.
 *
 * => if `fixed' is true, *min specifies where the region described
 *      by the submap must start
 * => if submap is non NULL we use that as the submap, otherwise we
 *	alloc a new map
 */
struct vm_map *
uvm_km_suballoc(struct vm_map *map, vaddr_t *min, vaddr_t *max, vsize_t size,
    int flags, boolean_t fixed, struct vm_map *submap)
{
	int mapflags = UVM_FLAG_NOMERGE | (fixed ? UVM_FLAG_FIXED : 0);

	size = round_page(size);	/* round up to pagesize */

	/*
	 * first allocate a blank spot in the parent map
	 */

	if (uvm_map(map, min, size, NULL, UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(UVM_PROT_ALL, UVM_PROT_ALL, UVM_INH_NONE,
	    UVM_ADV_RANDOM, mapflags)) != 0) {
	       panic("uvm_km_suballoc: unable to allocate space in parent map");
	}

	/*
	 * set VM bounds (min is filled in by uvm_map)
	 */

	*max = *min + size;

	/*
	 * add references to pmap and create or init the submap
	 */

	pmap_reference(vm_map_pmap(map));
	if (submap == NULL) {
		submap = uvm_map_create(vm_map_pmap(map), *min, *max, flags);
		if (submap == NULL)
			panic("uvm_km_suballoc: unable to create submap");
	} else {
		uvm_map_setup(submap, *min, *max, flags);
		submap->pmap = vm_map_pmap(map);
	}

	/*
	 * now let uvm_map_submap plug in it...
	 */

	if (uvm_map_submap(map, *min, *max, submap) != 0)
		panic("uvm_km_suballoc: submap allocation failed");

	return(submap);
}

/*
 * uvm_km_pgremove: remove pages from a kernel uvm_object.
 *
 * => when you unmap a part of anonymous kernel memory you want to toss
 *    the pages right away.    (this gets called from uvm_unmap_...).
 */
void
uvm_km_pgremove(struct uvm_object *uobj, vaddr_t start, vaddr_t end)
{
	struct vm_page *pp;
	voff_t curoff;
	UVMHIST_FUNC("uvm_km_pgremove"); UVMHIST_CALLED(maphist);

	KASSERT(uobj->pgops == &aobj_pager);

	for (curoff = start ; curoff < end ; curoff += PAGE_SIZE) {
		pp = uvm_pagelookup(uobj, curoff);
		if (pp == NULL)
			continue;

		UVMHIST_LOG(maphist,"  page %p, busy=%ld", pp,
		    pp->pg_flags & PG_BUSY, 0, 0);

		if (pp->pg_flags & PG_BUSY) {
			atomic_setbits_int(&pp->pg_flags, PG_WANTED);
			UVM_UNLOCK_AND_WAIT(pp, &uobj->vmobjlock, 0,
			    "km_pgrm", 0);
			simple_lock(&uobj->vmobjlock);
			curoff -= PAGE_SIZE; /* loop back to us */
			continue;
		} else {
			/* free the swap slot... */
			uao_dropswap(uobj, curoff >> PAGE_SHIFT);

			/*
			 * ...and free the page; note it may be on the
			 * active or inactive queues.
			 */
			uvm_lock_pageq();
			uvm_pagefree(pp);
			uvm_unlock_pageq();
		}
	}
}


/*
 * uvm_km_pgremove_intrsafe: like uvm_km_pgremove(), but for "intrsafe"
 *    objects
 *
 * => when you unmap a part of anonymous kernel memory you want to toss
 *    the pages right away.    (this gets called from uvm_unmap_...).
 * => none of the pages will ever be busy, and none of them will ever
 *    be on the active or inactive queues (because these objects are
 *    never allowed to "page").
 */

void
uvm_km_pgremove_intrsafe(vaddr_t start, vaddr_t end)
{
	struct vm_page *pg;
	vaddr_t va;
	paddr_t pa;

	for (va = start; va < end; va += PAGE_SIZE) {
		if (!pmap_extract(pmap_kernel(), va, &pa))
			continue;
		pg = PHYS_TO_VM_PAGE(pa);
		if (pg == NULL)
			panic("uvm_km_pgremove_intrsafe: no page");
		uvm_pagefree(pg);
	}
}

/*
 * uvm_km_kmemalloc: lower level kernel memory allocator for malloc()
 *
 * => we map wired memory into the specified map using the obj passed in
 * => NOTE: we can return NULL even if we can wait if there is not enough
 *	free VM space in the map... caller should be prepared to handle
 *	this case.
 * => we return KVA of memory allocated
 * => flags: NOWAIT, VALLOC - just allocate VA, TRYLOCK - fail if we can't
 *	lock the map
 * => low, high, alignment, boundary, nsegs are the corresponding parameters
 *	to uvm_pglistalloc
 * => flags: ZERO - correspond to uvm_pglistalloc flags
 */

vaddr_t
uvm_km_kmemalloc_pla(struct vm_map *map, struct uvm_object *obj, vsize_t size,
    int flags, paddr_t low, paddr_t high, paddr_t alignment, paddr_t boundary,
    int nsegs)
{
	vaddr_t kva, loopva;
	voff_t offset;
	struct vm_page *pg;
	struct pglist pgl;
	int pla_flags;
	UVMHIST_FUNC("uvm_km_kmemalloc"); UVMHIST_CALLED(maphist);

	UVMHIST_LOG(maphist,"  (map=%p, obj=%p, size=0x%lx, flags=%d)",
		    map, obj, size, flags);
	KASSERT(vm_map_pmap(map) == pmap_kernel());
	/* UVM_KMF_VALLOC => !UVM_KMF_ZERO */
	KASSERT(!(flags & UVM_KMF_VALLOC) ||
	    !(flags & UVM_KMF_ZERO));

	/*
	 * setup for call
	 */

	size = round_page(size);
	kva = vm_map_min(map);	/* hint */
	if (nsegs == 0)
		nsegs = atop(size);

	/*
	 * allocate some virtual space
	 */

	if (__predict_false(uvm_map(map, &kva, size, obj, UVM_UNKNOWN_OFFSET,
	      0, UVM_MAPFLAG(UVM_PROT_RW, UVM_PROT_RW, UVM_INH_NONE,
			  UVM_ADV_RANDOM, (flags & UVM_KMF_TRYLOCK))) != 0)) {
		UVMHIST_LOG(maphist, "<- done (no VM)",0,0,0,0);
		return(0);
	}

	/*
	 * if all we wanted was VA, return now
	 */

	if (flags & UVM_KMF_VALLOC) {
		UVMHIST_LOG(maphist,"<- done valloc (kva=0x%lx)", kva,0,0,0);
		return(kva);
	}

	/*
	 * recover object offset from virtual address
	 */

	if (obj != NULL)
		offset = kva - vm_map_min(kernel_map);
	else
		offset = 0;

	UVMHIST_LOG(maphist, "  kva=0x%lx, offset=0x%lx", kva, offset,0,0);

	/*
	 * now allocate and map in the memory... note that we are the only ones
	 * whom should ever get a handle on this area of VM.
	 */
	TAILQ_INIT(&pgl);
	pla_flags = 0;
	if ((flags & UVM_KMF_NOWAIT) ||
	    ((flags & UVM_KMF_CANFAIL) &&
	    uvmexp.swpgonly - uvmexp.swpages <= atop(size)))
		pla_flags |= UVM_PLA_NOWAIT;
	else
		pla_flags |= UVM_PLA_WAITOK;
	if (flags & UVM_KMF_ZERO)
		pla_flags |= UVM_PLA_ZERO;
	if (uvm_pglistalloc(size, low, high, alignment, boundary, &pgl, nsegs,
	    pla_flags) != 0) {
		/* Failed. */
		uvm_unmap(map, kva, kva + size);
		return (0);
	}

	loopva = kva;
	while (loopva != kva + size) {
		pg = TAILQ_FIRST(&pgl);
		TAILQ_REMOVE(&pgl, pg, pageq);
		uvm_pagealloc_pg(pg, obj, offset, NULL);
		atomic_clearbits_int(&pg->pg_flags, PG_BUSY);
		UVM_PAGE_OWN(pg, NULL);

		/*
		 * map it in: note that we call pmap_enter with the map and
		 * object unlocked in case we are kmem_map.
		 */

		if (obj == NULL) {
			pmap_kenter_pa(loopva, VM_PAGE_TO_PHYS(pg),
			    UVM_PROT_RW);
		} else {
			pmap_enter(map->pmap, loopva, VM_PAGE_TO_PHYS(pg),
			    UVM_PROT_RW,
			    PMAP_WIRED | VM_PROT_READ | VM_PROT_WRITE);
		}
		loopva += PAGE_SIZE;
		offset += PAGE_SIZE;
	}
	KASSERT(TAILQ_EMPTY(&pgl));
	pmap_update(pmap_kernel());

	UVMHIST_LOG(maphist,"<- done (kva=0x%lx)", kva,0,0,0);
	return(kva);
}

/*
 * uvm_km_free: free an area of kernel memory
 */

void
uvm_km_free(struct vm_map *map, vaddr_t addr, vsize_t size)
{
	uvm_unmap(map, trunc_page(addr), round_page(addr+size));
}

/*
 * uvm_km_free_wakeup: free an area of kernel memory and wake up
 * anyone waiting for vm space.
 *
 * => XXX: "wanted" bit + unlock&wait on other end?
 */

void
uvm_km_free_wakeup(struct vm_map *map, vaddr_t addr, vsize_t size)
{
	struct vm_map_entry *dead_entries;

	vm_map_lock(map);
	uvm_unmap_remove(map, trunc_page(addr), round_page(addr+size), 
	     &dead_entries, NULL, FALSE);
	wakeup(map);
	vm_map_unlock(map);

	if (dead_entries != NULL)
		uvm_unmap_detach(dead_entries, 0);
}

/*
 * uvm_km_alloc1: allocate wired down memory in the kernel map.
 *
 * => we can sleep if needed
 */

vaddr_t
uvm_km_alloc1(struct vm_map *map, vsize_t size, vsize_t align, boolean_t zeroit)
{
	vaddr_t kva, loopva;
	voff_t offset;
	struct vm_page *pg;
	UVMHIST_FUNC("uvm_km_alloc1"); UVMHIST_CALLED(maphist);

	UVMHIST_LOG(maphist,"(map=%p, size=0x%lx)", map, size,0,0);
	KASSERT(vm_map_pmap(map) == pmap_kernel());

	size = round_page(size);
	kva = vm_map_min(map);		/* hint */

	/*
	 * allocate some virtual space
	 */

	if (__predict_false(uvm_map(map, &kva, size, uvm.kernel_object,
	    UVM_UNKNOWN_OFFSET, align, UVM_MAPFLAG(UVM_PROT_ALL, UVM_PROT_ALL,
	    UVM_INH_NONE, UVM_ADV_RANDOM, 0)) != 0)) {
		UVMHIST_LOG(maphist,"<- done (no VM)",0,0,0,0);
		return(0);
	}

	/*
	 * recover object offset from virtual address
	 */

	offset = kva - vm_map_min(kernel_map);
	UVMHIST_LOG(maphist,"  kva=0x%lx, offset=0x%lx", kva, offset,0,0);

	/*
	 * now allocate the memory.  we must be careful about released pages.
	 */

	loopva = kva;
	while (size) {
		simple_lock(&uvm.kernel_object->vmobjlock);
		/* allocate ram */
		pg = uvm_pagealloc(uvm.kernel_object, offset, NULL, 0);
		if (pg) {
			atomic_clearbits_int(&pg->pg_flags, PG_BUSY);
			UVM_PAGE_OWN(pg, NULL);
		}
		simple_unlock(&uvm.kernel_object->vmobjlock);
		if (__predict_false(pg == NULL)) {
			if (curproc == uvm.pagedaemon_proc) {
				/*
				 * It is unfeasible for the page daemon to
				 * sleep for memory, so free what we have
				 * allocated and fail.
				 */
				uvm_unmap(map, kva, loopva - kva);
				return (NULL);
			} else {
				uvm_wait("km_alloc1w");	/* wait for memory */
				continue;
			}
		}

		/*
		 * map it in; note we're never called with an intrsafe
		 * object, so we always use regular old pmap_enter().
		 */
		pmap_enter(map->pmap, loopva, VM_PAGE_TO_PHYS(pg),
		    UVM_PROT_ALL, PMAP_WIRED | VM_PROT_READ | VM_PROT_WRITE);

		loopva += PAGE_SIZE;
		offset += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	pmap_update(map->pmap);
	
	/*
	 * zero on request (note that "size" is now zero due to the above loop
	 * so we need to subtract kva from loopva to reconstruct the size).
	 */

	if (zeroit)
		memset((caddr_t)kva, 0, loopva - kva);

	UVMHIST_LOG(maphist,"<- done (kva=0x%lx)", kva,0,0,0);
	return(kva);
}

/*
 * uvm_km_valloc: allocate zero-fill memory in the kernel's address space
 *
 * => memory is not allocated until fault time
 */

vaddr_t
uvm_km_valloc(struct vm_map *map, vsize_t size)
{
	return(uvm_km_valloc_align(map, size, 0, 0));
}

vaddr_t
uvm_km_valloc_try(struct vm_map *map, vsize_t size)
{
	return(uvm_km_valloc_align(map, size, 0, UVM_FLAG_TRYLOCK));
}

vaddr_t
uvm_km_valloc_align(struct vm_map *map, vsize_t size, vsize_t align, int flags)
{
	vaddr_t kva;
	UVMHIST_FUNC("uvm_km_valloc"); UVMHIST_CALLED(maphist);

	UVMHIST_LOG(maphist, "(map=%p, size=0x%lx)", map, size, 0,0);
	KASSERT(vm_map_pmap(map) == pmap_kernel());

	size = round_page(size);
	kva = vm_map_min(map);		/* hint */

	/*
	 * allocate some virtual space.  will be demand filled by kernel_object.
	 */

	if (__predict_false(uvm_map(map, &kva, size, uvm.kernel_object,
	    UVM_UNKNOWN_OFFSET, align, UVM_MAPFLAG(UVM_PROT_ALL, UVM_PROT_ALL,
	    UVM_INH_NONE, UVM_ADV_RANDOM, flags)) != 0)) {
		UVMHIST_LOG(maphist, "<- done (no VM)", 0,0,0,0);
		return(0);
	}

	UVMHIST_LOG(maphist, "<- done (kva=0x%lx)", kva,0,0,0);
	return(kva);
}

/*
 * uvm_km_valloc_wait: allocate zero-fill memory in the kernel's address space
 *
 * => memory is not allocated until fault time
 * => if no room in map, wait for space to free, unless requested size
 *    is larger than map (in which case we return 0)
 */

vaddr_t
uvm_km_valloc_prefer_wait(struct vm_map *map, vsize_t size, voff_t prefer)
{
	vaddr_t kva;
	UVMHIST_FUNC("uvm_km_valloc_prefer_wait"); UVMHIST_CALLED(maphist);

	UVMHIST_LOG(maphist, "(map=%p, size=0x%lx)", map, size, 0,0);
	KASSERT(vm_map_pmap(map) == pmap_kernel());

	size = round_page(size);
	if (size > vm_map_max(map) - vm_map_min(map))
		return(0);

	while (1) {
		kva = vm_map_min(map);		/* hint */

		/*
		 * allocate some virtual space.   will be demand filled
		 * by kernel_object.
		 */

		if (__predict_true(uvm_map(map, &kva, size, uvm.kernel_object,
		    prefer, 0, UVM_MAPFLAG(UVM_PROT_ALL,
		    UVM_PROT_ALL, UVM_INH_NONE, UVM_ADV_RANDOM, 0)) == 0)) {
			UVMHIST_LOG(maphist,"<- done (kva=0x%lx)", kva,0,0,0);
			return(kva);
		}

		/*
		 * failed.  sleep for a while (on map)
		 */

		UVMHIST_LOG(maphist,"<<<sleeping>>>",0,0,0,0);
		tsleep((caddr_t)map, PVM, "vallocwait", 0);
	}
	/*NOTREACHED*/
}

vaddr_t
uvm_km_valloc_wait(struct vm_map *map, vsize_t size)
{
	return uvm_km_valloc_prefer_wait(map, size, UVM_UNKNOWN_OFFSET);
}

int uvm_km_pages_free; /* number of pages currently on free list */

#if defined(__HAVE_PMAP_DIRECT)
/*
 * uvm_km_page allocator, __HAVE_PMAP_DIRECT arch
 * On architectures with machine memory direct mapped into a portion
 * of KVM, we have very little work to do.  Just get a physical page,
 * and find and return its VA.
 */
void
uvm_km_page_init(void)
{
	/* nothing */
}

#else
/*
 * uvm_km_page allocator, non __HAVE_PMAP_DIRECT archs
 * This is a special allocator that uses a reserve of free pages
 * to fulfill requests.  It is fast and interrupt safe, but can only
 * return page sized regions.  Its primary use is as a backend for pool.
 *
 * The memory returned is allocated from the larger kernel_map, sparing
 * pressure on the small interrupt-safe kmem_map.  It is wired, but
 * not zero filled.
 */

#define UVM_KM_PAGES_LOWAT_MAX	(2048)
#define UVM_KM_PAGES_HIWAT_MAX	(4 * UVM_KM_PAGES_LOWAT_MAX)

struct uvm_km_pages {
	struct	mutex mtx;

	/* Low and high water mark for addresses. */
	int	lowat;
	int	hiwat;

	/* Kernel address pool. */
	int	free;
	vaddr_t	page[UVM_KM_PAGES_HIWAT_MAX];

	struct	proc *km_proc;
};

struct uvm_km_pages uvm_km_pages;

void uvm_km_createthread(void *);
void uvm_km_thread(void *);

/*
 * Allocate the initial reserve, and create the thread which will
 * keep the reserve full.  For bootstrapping, we allocate more than
 * the lowat amount, because it may be a while before the thread is
 * running.
 */
void
uvm_km_page_init(void)
{
	int lowat_min;
	int i;

	mtx_init(&uvm_km_pages.mtx, IPL_VM);
	if (!uvm_km_pages.lowat) {
		/* based on physmem, calculate a good value here */
		uvm_km_pages.lowat = physmem / 256;
		lowat_min = physmem < atop(16 * 1024 * 1024) ? 32 : 128;
		if (uvm_km_pages.lowat < lowat_min)
			uvm_km_pages.lowat = lowat_min;
	}
	if (uvm_km_pages.lowat > UVM_KM_PAGES_LOWAT_MAX)
		uvm_km_pages.lowat = UVM_KM_PAGES_LOWAT_MAX;
	uvm_km_pages.hiwat = 4 * uvm_km_pages.lowat;
	if (uvm_km_pages.hiwat > UVM_KM_PAGES_HIWAT_MAX)
		uvm_km_pages.hiwat = UVM_KM_PAGES_HIWAT_MAX;

	for (i = 0; i < uvm_km_pages.hiwat; i++) {
		uvm_km_pages.page[i] = (vaddr_t)uvm_km_kmemalloc(kernel_map,
		    NULL, PAGE_SIZE, UVM_KMF_NOWAIT|UVM_KMF_VALLOC);
		if (uvm_km_pages.page[i] == NULL)
			break;
	}
	uvm_km_pages.free = i;
	for ( ; i < UVM_KM_PAGES_HIWAT_MAX; i++)
		uvm_km_pages.page[i] = NULL;

	/* tone down if really high */
	if (uvm_km_pages.lowat > 512)
		uvm_km_pages.lowat = 512;

	kthread_create_deferred(uvm_km_createthread, NULL);
}

void
uvm_km_createthread(void *arg)
{
	kthread_create(uvm_km_thread, NULL, &uvm_km_pages.km_proc, "kmthread");
}

/*
 * Endless loop.  We grab pages in increments of 16 pages, then
 * quickly swap them into the list.  At some point we can consider
 * returning memory to the system if we have too many free pages,
 * but that's not implemented yet.
 */
void
uvm_km_thread(void *arg)
{
	vaddr_t pg[16];
	int i;

	for (;;) {
		mtx_enter(&uvm_km_pages.mtx);
		if (uvm_km_pages.free >= uvm_km_pages.lowat) {
			msleep(&uvm_km_pages.km_proc, &uvm_km_pages.mtx,
			    PVM, "kmalloc", 0);
		}

		for (i = 0; i < nitems(pg); i++) {
			pg[i] = (vaddr_t)uvm_km_kmemalloc(kernel_map, NULL,
			    PAGE_SIZE, UVM_KMF_VALLOC);
		}

		mtx_enter(&uvm_km_pages.mtx);
		for (i = 0; i < nitems(pg); i++) {
			if (uvm_km_pages.free == nitems(uvm_km_pages.page))
				break;
			else
				uvm_km_pages.page[uvm_km_pages.free++] = pg[i];
		}

		wakeup(&uvm_km_pages.free);
		mtx_leave(&uvm_km_pages.mtx);

		/* Cleanup left-over pages (if any). */
		for (; i < nitems(pg); i++)
			uvm_km_free_wakeup(kernel_map, pg[i], PAGE_SIZE);
	}
}
#endif

void *
uvm_km_getpage_pla(int flags, int *slowdown, paddr_t low, paddr_t high,
    paddr_t alignment, paddr_t boundary)
{
	struct pglist pgl;
	int pla_flags;
	struct vm_page *pg;
	vaddr_t va;

	*slowdown = 0;
	pla_flags = (flags & UVM_KMF_NOWAIT) ? UVM_PLA_NOWAIT : UVM_PLA_WAITOK;
	if (flags & UVM_KMF_ZERO)
		pla_flags |= UVM_PLA_ZERO;
	TAILQ_INIT(&pgl);
	if (uvm_pglistalloc(PAGE_SIZE, low, high, alignment, boundary, &pgl,
	    1, pla_flags) != 0)
		return NULL;
	pg = TAILQ_FIRST(&pgl);
	KASSERT(pg != NULL && TAILQ_NEXT(pg, pageq) == NULL);
	TAILQ_REMOVE(&pgl, pg, pageq);

#ifdef __HAVE_PMAP_DIRECT
	va = pmap_map_direct(pg);
	if (__predict_false(va == 0))
		uvm_pagefree(pg);

#else	/* !__HAVE_PMAP_DIRECT */
	mtx_enter(&uvm_km_pages.mtx);
	while (uvm_km_pages.free == 0) {
		if (flags & UVM_KMF_NOWAIT) {
			mtx_leave(&uvm_km_pages.mtx);
			uvm_pagefree(pg);
			return NULL;
		}
		msleep(&uvm_km_pages.free, &uvm_km_pages.mtx, PVM, "getpage",
		    0);
	}

	va = uvm_km_pages.page[--uvm_km_pages.free];
	if (uvm_km_pages.free < uvm_km_pages.lowat &&
	    curproc != uvm_km_pages.km_proc) {
		*slowdown = 1;
		wakeup(&uvm_km_pages.km_proc);
	}
	mtx_leave(&uvm_km_pages.mtx);


	atomic_setbits_int(&pg->pg_flags, PG_FAKE);
	UVM_PAGE_OWN(pg, NULL);

	pmap_enter(kernel_map->pmap, va, VM_PAGE_TO_PHYS(pg), UVM_PROT_RW,
	    PMAP_WIRED | VM_PROT_READ | VM_PROT_WRITE);
	pmap_update(kernel_map->pmap);

#endif	/* !__HAVE_PMAP_DIRECT */
	return ((void *)va);
}

void
uvm_km_putpage(void *v)
{
	vaddr_t va = (vaddr_t)v;
	struct vm_page *pg;

#ifdef __HAVE_PMAP_DIRECT
	pg = pmap_unmap_direct(va);
#else	/* !__HAVE_PMAP_DIRECT */
	paddr_t pa;
	if (!pmap_extract(pmap_kernel(), va, &pa))
		panic("lost pa");
	pg = PHYS_TO_VM_PAGE(pa);

	KASSERT(pg != NULL);

	pmap_remove(kernel_map->pmap, va, va + PAGE_SIZE);
	pmap_update(kernel_map->pmap);

	mtx_enter(&uvm_km_pages.mtx);
	if (uvm_km_pages.free < uvm_km_pages.hiwat)
		uvm_km_pages.page[uvm_km_pages.free++] = va;
	else
		uvm_km_free_wakeup(kernel_map, va, PAGE_SIZE);
	mtx_leave(&uvm_km_pages.mtx);
#endif	/* !__HAVE_PMAP_DIRECT */

	uvm_pagefree(pg);
}
