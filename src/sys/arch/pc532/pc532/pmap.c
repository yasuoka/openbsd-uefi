/*	$NetBSD: pmap.c,v 1.10 1995/08/25 07:49:13 phil Exp $	*/

/* 
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and William Jolitz of UUNET Technologies Inc.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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
 *	@(#)pmap.c	7.7 (Berkeley)	5/12/91
 */

/*
 * Derived from hp300 version by Mike Hibler, this version by William
 * Jolitz uses a recursive map [a pde points to the page directory] to
 * map the page tables using the pagetables themselves. This is done to
 * reduce the impact on kernel virtual memory for lots of sparse address
 * space, and to reduce the cost of memory to each process.
 *
 *	Derived from: hp300/@(#)pmap.c	7.1 (Berkeley) 12/5/90
 */

/*
 *	Reno i386 version, from Mike Hibler's hp300 version.
 */

/*
 *  Most recently made to be a pc532 pmap!  (Phil Nelson, 1/14/93)
 */

/*
 *	Manages physical address maps.
 *
 *	In addition to hardware address maps, this
 *	module is called upon to provide software-use-only
 *	maps which may or may not be stored in the same
 *	form as hardware maps.  These pseudo-maps are
 *	used to store intermediate results from copy
 *	operations to and from address spaces.
 *
 *	Since the information managed by this module is
 *	also stored by the logical address mapping module,
 *	this module may throw away valid virtual-to-physical
 *	mappings at almost any time.  However, invalidations
 *	of virtual-to-physical mappings must be done as
 *	requested.
 *
 *	In order to cope with hardware architectures which
 *	make virtual-to-physical map invalidates expensive,
 *	this module may delay invalidate or reduced protection
 *	operations until such time as they are actually
 *	necessary.  This module is given full information as
 *	to which processors are currently using which maps,
 *	and to when physical maps must be made correct.
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/user.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>

/* Prototypes of routines used here. */

vm_offset_t pmap_extract(pmap_t, vm_offset_t);
void pmap_activate(register pmap_t, struct pcb *);
extern vm_offset_t reserve_dumppages __P((vm_offset_t));

/*
 * Allocate various and sundry SYSMAPs used in the days of old VM
 * and not yet converted.  XXX.
 */

#define BSDVM_COMPAT	1

#ifdef DEBUG
struct {
	int kernel;	/* entering kernel mapping */
	int user;	/* entering user mapping */
	int ptpneeded;	/* needed to allocate a PT page */
	int pwchange;	/* no mapping change, just wiring or protection */
	int wchange;	/* no mapping change, just wiring */
	int mchange;	/* was mapped but mapping to different page */
	int managed;	/* a managed page */
	int firstpv;	/* first mapping for this PA */
	int secondpv;	/* second mapping for this PA */
	int ci;		/* cache inhibited */
	int unmanaged;	/* not a managed page */
	int flushes;	/* cache flushes */
} enter_stats;
struct {
	int calls;
	int removes;
	int pvfirst;
	int pvsearch;
	int ptinvalid;
	int uflushes;
	int sflushes;
} remove_stats;

int debugmap = 0;
int pmapdebug = 0; /* 0xffff */
#define PDB_FOLLOW	0x0001
#define PDB_INIT	0x0002
#define PDB_ENTER	0x0004
#define PDB_REMOVE	0x0008
#define PDB_CREATE	0x0010
#define PDB_PTPAGE	0x0020
#define PDB_CACHE	0x0040
#define PDB_BITS	0x0080
#define PDB_COLLECT	0x0100
#define PDB_PROTECT	0x0200
#define PDB_PDRTAB	0x0400
#define PDB_PARANOIA	0x2000
#define PDB_WIRING	0x4000
#define PDB_PVDUMP	0x8000

int pmapvacflush = 0;
#define	PVF_ENTER	0x01
#define	PVF_REMOVE	0x02
#define	PVF_PROTECT	0x04
#define	PVF_TOTAL	0x80
#endif

/*
 * Get PDEs and PTEs for user/kernel address space
 */
#define	pmap_pde(m, v)	(&((m)->pm_pdir[((vm_offset_t)(v) >> PD_SHIFT)&1023]))

#define pmap_pte_pa(pte)	(*(int *)(pte) & PG_FRAME)

#define pmap_pde_v(pte)		((pte)->pd_v)
#define pmap_pte_w(pte)		((pte)->pg_w)
/* #define pmap_pte_ci(pte)	((pte)->pg_ci) */
#define pmap_pte_m(pte)		((pte)->pg_m)
#define pmap_pte_u(pte)		((pte)->pg_u)
#define pmap_pte_v(pte)		((pte)->pg_v)
#define pmap_pte_set_w(pte, v)		((pte)->pg_w = (v))
#define pmap_pte_set_prot(pte, v)	((pte)->pg_prot = (v))


/* for debug output */
#define pg printf

/*
 * Given a map and a machine independent protection code,
 * convert to a ns532 protection code.
 */
#define pte_prot(m, p)	(protection_codes[p])
int	protection_codes[8];

struct user *proc0paddr;
struct pmap	kernel_pmap_store;

vm_offset_t    	avail_start;	/* PA of first available physical page */
vm_offset_t	avail_end;	/* PA of last available physical page */
vm_size_t	mem_size;	/* memory size in bytes */
vm_offset_t	virtual_avail;  /* VA of first avail page (after kernel bss)*/
vm_offset_t	virtual_end;	/* VA of last avail page (end of kernel AS) */
vm_offset_t	vm_first_phys;	/* PA of first managed page */
vm_offset_t	vm_last_phys;	/* PA just past last managed page */
int		ns532pagesperpage;	/* PAGE_SIZE / NS532_PAGE_SIZE */
boolean_t	pmap_initialized = FALSE;	/* Has pmap_init completed? */
short		*pmap_attributes;	/* reference and modify bits */

boolean_t	pmap_testbit();
void		pmap_clear_modify();

#if BSDVM_COMPAT
#include "msgbuf.h"

/*
 * All those kernel PT submaps that BSD is so fond of
 */
struct pte	*CMAP1, *CMAP2, *xxx_mmap;
caddr_t		CADDR1, CADDR2, vmmap;
struct pte	*msgbufmap;
struct msgbuf	*msgbufp;
#endif

vm_offset_t KPTphys;
extern int PDRPDROFF;

/*
 * Bootstrap memory allocator. This function allows for early dynamic
 * memory allocation until the virtual memory system has been bootstrapped.
 * After that point, either kmem_alloc or malloc should be used. This
 * function works by stealing pages from the (to be) managed page pool,
 * stealing virtual address space, then mapping the pages and zeroing them.
 *
 * It should be used from pmap_bootstrap till vm_page_startup, afterwards
 * it cannot be used, and will generate a panic if tried. Note that this
 * memory will never be freed, and in essence it is wired down.
 */
void *
pmap_bootstrap_alloc(size)
        int size;
{
        extern boolean_t vm_page_startup_initialized;
        vm_offset_t val;

        if (vm_page_startup_initialized)
                panic("pmap_bootstrap_alloc: called after startup initialized")\
;
        size = round_page(size);
        val = virtual_avail;

        virtual_avail = pmap_map(virtual_avail, avail_start,
                avail_start + size, VM_PROT_READ|VM_PROT_WRITE);
        avail_start += size;

        blkclr ((caddr_t) val, size);
        return ((void *) val);
}



/* static */
void
v_probe(pmap_t pmap, vm_offset_t va)
{
	int *ptr;
	struct pde *pde_entry;
	struct pte *pte_entry;

	/* get a pointer to the top level page table entry */
	pde_entry = pmap_pde(pmap, va);
	if (!pmap_pde_v(pde_entry)) {
		printf("va 0x%x, no top-level entry\n", va);
		return;
	}
	ptr = (int *) pde_entry;
	pte_entry = ((struct pte *) ((*ptr & PG_FRAME) | KERNBASE)) + ptei(va);
	ptr = (int *) pte_entry;
	if (!pmap_pte_v(pte_entry)) {
		printf("pte_entry 0x%x *pte_entry 0x%x\n", ptr, *ptr);
		printf("va 0x%x, no 2nd-level entry\n", va);
		return;
	}
	/* print the page table entry */
	printf("v_probe: va 0x%x pa 0x%x entry 0x%x\n",
		va, (*ptr & PG_FRAME) | (va & ~PG_FRAME), *ptr);
}

static
void
map_page(pmap_t pmap, vm_offset_t va, vm_offset_t pa)
{
	int *ptr;
	struct pde *pde_entry;
	struct pte *pte_entry;

	/* get a pointer to the top level page table entry */
	pde_entry = pmap_pde(pmap, va);
	if (!pmap_pde_v(pde_entry)) {
		printf("map_page(0x%x, 0x%x, 0x%x) failed\n", pmap, va, pa);
		panic("missing 2nd level page table");
	}
	/* get a pointer to the 2nd level table entry */
	ptr = (int *) pde_entry;
	pte_entry = ((struct pte *) (*ptr & PG_FRAME)) + ptei(va);
	if (pmap_pte_v(pte_entry)) {
		printf("map_page(0x%x, 0x%x, 0x%x) failed\n", pmap, va, pa);
		panic("2nd level page table entry already valid");
	}
	/* make the page table entry */
	ptr = (int *) pte_entry;
	*ptr = pa | PG_V | PG_KW;
	/* just to be safe */
	tlbflush();
}

/*
 * Map in physical page 'pa' to virtual address 'va', and install
 * as a 2nd level page table at index 'index' for the given
 * pmap.
 */
static
void
map_page_table(pmap_t pmap, int index, vm_offset_t va, vm_offset_t pa)
{
	int *ptr = (int *) &pmap->pm_pdir[index];
	if (*ptr) {
		printf("2nd level table present for index %x\n", index);
		panic("remapping 2nd level table");
	}
	/* map in the 2nd level table */
	map_page(pmap_kernel(), va, pa);
	/* init the 2nd level table to all invalid */
	bzero(pa, NBPG);
	/* install the 2nd level table */
	*ptr = pa | PG_V | PG_KW;
	/* just to be safe */
	tlbflush();
}

/*
 *	Bootstrap the system enough to run with virtual memory.
 *	Map the kernel's code and data, and allocate the system page table.
 *
 *	On the Ns532 this is called after mapping has already been enabled
 *	and just syncs the pmap module with what has already been done.
 *	[We can't call it easily with mapping off since the kernel is not
 *	mapped with PA == VA, hence we would have to relocate every address
 *	from the linked base (virtual) address 0xFE000000 to the actual
 *	(physical) address starting relative to 0]
 */
struct pte *pmap_pte();

void
pmap_bootstrap(firstaddr, loadaddr)
	vm_offset_t firstaddr;
	vm_offset_t loadaddr;
{
	int x, *ptr;
#if BSDVM_COMPAT
	vm_offset_t va;
	struct pte *pte;
#endif
	extern vm_offset_t maxmem, physmem;

	ns532pagesperpage = 1; /* PAGE_SIZE / NS532_PAGE_SIZE; */

	/*
	 * Initialize protection array.
	 */
	ns532_protection_init(); 

	/* setup avail_start, avail_end, virtual_avail, virtual_end */
	avail_start = firstaddr;
	avail_end = mem_size;

	/* XXX: allow for msgbuf */
	avail_end -= ns532_round_page(sizeof(struct msgbuf));

	virtual_avail = avail_start + KERNBASE;
	virtual_end = VM_MAX_KERNEL_ADDRESS;

	/*
	 * Create Kernel page directory table and page maps.
	 */
	pmap_kernel()->pm_pdir = (pd_entry_t *) (KPTphys + KERNBASE);
	/* recursively map in ptb0 */
	ptr = ((int *) pmap_kernel()->pm_pdir) + PDRPDROFF;
	if (*ptr) {
		printf("ptb0 0x%x offset 0x%x should be 0 but is 0x%x\n",
			pmap_kernel()->pm_pdir, PDRPDROFF, *ptr);
		bpt_to_monitor();
	}
	/* don't add KERNBASE as this has to be a physical address */
	*ptr = KPTphys | PG_V | PG_KW;
	/* fill in the rest of the top-level kernel VA entries */
	for (x = ns532_btod(VM_MIN_KERNEL_ADDRESS);
			x < ns532_btod(VM_MAX_KERNEL_ADDRESS); x++) {
		ptr = (int *) &pmap_kernel()->pm_pdir[x];
		/* only fill in the entries not yet made in _low_level_init() */
		if (!*ptr) {
			/* map in the page table */
			map_page_table(pmap_kernel(), x,
				virtual_avail, avail_start);
			avail_start += NBPG;
			virtual_avail += NBPG;
		}
	}
	/* map in the kernel stack for process 0 */
	/* install avail_start as a 2nd level table for index 0x3f6 */
	map_page_table(pmap_kernel(), 0x3f6, virtual_avail, avail_start);
	avail_start += NBPG;
	virtual_avail += NBPG;
	/* reserve UPAGES pages */
	proc0paddr = (struct user *) virtual_avail;
	curpcb = (struct pcb *) proc0paddr;
	va = ns532_dtob(0x3f6) | ns532_ptob(0x3fe);  /* USRSTACK ? */
	for (x = 0; x < UPAGES; ++x) {
		map_page(pmap_kernel(), va, avail_start);
		map_page(pmap_kernel(), virtual_avail, avail_start);
		bzero(va, NBPG);
		va += NBPG;
		avail_start += NBPG;
		virtual_avail += NBPG;
	}

	simple_lock_init(&pmap_kernel()->pm_lock);
	pmap_kernel()->pm_count = 1;

#ifdef DEBUG
	printf("avail_start   = 0x%x\n", avail_start);
	printf("avail_end     = 0x%x\n", avail_end);
	printf("virtual_avail = 0x%x\n", virtual_avail);
	printf("virtual_end   = 0x%x\n", virtual_end);
#endif

#if BSDVM_COMPAT
	/*
	 * Allocate all the submaps we need
	 */
#define	SYSMAP(c, p, v, n)	\
	v = (c)va; va += ((n)*NS532_PAGE_SIZE); p = pte; pte += (n);

	va = virtual_avail;
	pte = pmap_pte(pmap_kernel(), va);

	SYSMAP(caddr_t		,CMAP1		,CADDR1	   ,1		)
	SYSMAP(caddr_t		,CMAP2		,CADDR2	   ,1		)
	SYSMAP(caddr_t		,xxx_mmap	,vmmap	   ,1		)
	SYSMAP(struct msgbuf *	,msgbufmap	,msgbufp   ,1		)
	virtual_avail = va;
#endif
	virtual_avail = reserve_dumppages(va);
#ifdef DEBUG
	printf("virtual_avail = 0x%x\n", virtual_avail);
#endif
	tlbflush();
	/* XXX why do we do this??? - MM */
	*(int *)PTD = 0;
	tlbflush();
}

/*
 *	Initialize the pmap module.
 *	Called by vm_init, to initialize any structures that the pmap
 *	system needs to map virtual memory.
 */
void
pmap_init(phys_start, phys_end)
	vm_offset_t	phys_start, phys_end;
{
	int result;
	vm_offset_t	addr, addr2;
	vm_size_t	npg, s;
	int		rv;

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_INIT))
		printf("pmap_init(0x%x, 0x%x)\n", phys_start, phys_end);
#endif

	if (PAGE_SIZE != NBPG)
		panic("pmap_init: CLSIZE != 1");
	/*
	 * Now that kernel map has been allocated, we can mark as
	 * unavailable regions which we have mapped in locore.
	 */

#if 0
	/* the following reserves the (virtual) i/o space */
	addr = 0xffc00000;
	result = vm_map_find(kernel_map, NULL, (vm_offset_t) 0,
			   &addr, NBPG, FALSE);
	if (result != KERN_SUCCESS) {
		printf("vm_map_find for virtual i/o space failed %d\n", result);
	}

	/* reserve the used page tables following the kernel */
	/* bumped this to 10 pages just to be paranoid */
	addr = (vm_offset_t) KERNBASE + KPTphys;
	vm_object_reference(kernel_object);
	result = vm_map_find(kernel_map, kernel_object, addr,
			   &addr, 10*NBPG, FALSE);
	if (result != KERN_SUCCESS) {
		printf("vm_map_find for kernel page maps failed %d\n", result);
	}
#endif
	/*
	 * Allocate memory for random pmap data structures.  Includes the
	 * pv_head_table and pmap_attributes.
	 */
	npg = atop(phys_end - phys_start);
	s = (vm_size_t) (sizeof(struct pv_entry) * npg + 2*npg);
	s = round_page(s);
	addr = (vm_offset_t) kmem_alloc(kernel_map, s);
	pv_table = (pv_entry_t) addr;
	addr += sizeof(struct pv_entry) * npg;
	pmap_attributes = (short *) addr;

#ifdef DEBUG
	if (pmapdebug & PDB_INIT)
		printf("pmap_init: %x bytes (%x pgs): tbl %x attr %x\n",
		       s, npg, pv_table, pmap_attributes);
#endif

	/*
	 * Now it is safe to enable pv_table recording.
	 */
	vm_first_phys = phys_start;
	vm_last_phys = phys_end;
	pmap_initialized = TRUE;
}

/*
 *	Used to map a range of physical addresses into kernel
 *	virtual address space.
 *
 *	For now, VM is already on, we only need to map the
 *	specified memory.
 */
vm_offset_t
pmap_map(virt, start, end, prot)
	vm_offset_t	virt;
	vm_offset_t	start;
	vm_offset_t	end;
	int		prot;
{
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_map(%x, %x, %x, %x)\n", virt, start, end, prot);
#endif
	while (start < end) {
		pmap_enter(pmap_kernel(), virt, start, prot, FALSE);
		virt += PAGE_SIZE;
		start += PAGE_SIZE;
	}
	return(virt);
}

/*
 *	Create and return a physical map.
 *
 *	If the size specified for the map
 *	is zero, the map is an actual physical
 *	map, and may be referenced by the
 *	hardware.
 *
 *	If the size specified is non-zero,
 *	the map will be used in software only, and
 *	is bounded by that size.
 *
 * [ just allocate a ptd and mark it uninitialize -- should we track
 *   with a table which process has which ptd? -wfj ]
 */

pmap_t
pmap_create(size)
	vm_size_t	size;
{
	register pmap_t pmap;

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_CREATE))
		printf("pmap_create(%x)\n", size);
#endif
	/*
	 * Software use map does not need a pmap
	 */
	if (size)
		return(NULL);

	/* XXX: is it ok to wait here? */
	pmap = (pmap_t) malloc(sizeof *pmap, M_VMPMAP, M_WAITOK);
#ifdef notifwewait
	if (pmap == NULL)
		panic("pmap_create: cannot allocate a pmap");
#endif
	bzero(pmap, sizeof(*pmap));
	pmap_pinit(pmap);
	return (pmap);
}

/*
 * Initialize a preallocated and zeroed pmap structure,
 * such as one in a vmspace structure.
 */
void
pmap_pinit(pmap)
	register struct pmap *pmap;
{
#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_CREATE))
		pg("pmap_pinit(%x)\n", pmap);
#endif

	/*
	 * No need to allocate page table space yet but we do need a
	 * valid page directory table.
	 */
	pmap->pm_pdir = (pd_entry_t *) kmem_alloc(kernel_map, NBPG);

	/* wire in kernel global address entries */
	bcopy(PTD+KPTDI_FIRST, pmap->pm_pdir+KPTDI_FIRST,
		(KPTDI_LAST-KPTDI_FIRST+1)*4);

	/* install self-referential address mapping entry */
	*(int *)(pmap->pm_pdir+PTDPTDI) =
		(int)pmap_extract(pmap_kernel(), (vm_offset_t) pmap->pm_pdir)
		  | PG_V | PG_KW;

	pmap->pm_count = 1;
	simple_lock_init(&pmap->pm_lock);
}

/*
 *	Retire the given physical map from service.
 *	Should only be called if the map contains
 *	no valid mappings.
 */
void
pmap_destroy(pmap)
	register pmap_t pmap;
{
	int count;

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_destroy(%x)\n", pmap);
#endif
	if (pmap == NULL)
		return;

	simple_lock(&pmap->pm_lock);
	count = --pmap->pm_count;
	simple_unlock(&pmap->pm_lock);
	if (count == 0) {
		pmap_release(pmap);
		free((caddr_t)pmap, M_VMPMAP);
	}
}

/*
 * Release any resources held by the given physical map.
 * Called when a pmap initialized by pmap_pinit is being released.
 * Should only be called if the map contains no valid mappings.
 */
void
pmap_release(pmap)
	register struct pmap *pmap;
{
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		pg("pmap_release(%x)\n", pmap);
#endif
#ifdef notdef /* DIAGNOSTIC */
	/* count would be 0 from pmap_destroy... */
	simple_lock(&pmap->pm_lock);
	if (pmap->pm_count != 1)
		panic("pmap_release count");
#endif
	kmem_free(kernel_map, (vm_offset_t)pmap->pm_pdir, NBPG);
}

/*
 *	Add a reference to the specified pmap.
 */
void
pmap_reference(pmap)
	pmap_t	pmap;
{
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_reference(%x)\n", pmap);
#endif
	if (pmap != NULL) {
		simple_lock(&pmap->pm_lock);
		pmap->pm_count++;
		simple_unlock(&pmap->pm_lock);
	}
}

/*
 *	Remove the given range of addresses from the specified map.
 *
 *	It is assumed that the start and end are properly
 *	rounded to the page size.
 */
void
pmap_remove(pmap, sva, eva)
	struct pmap *pmap;
	register vm_offset_t sva;
	register vm_offset_t eva;
{
	register pt_entry_t *ptp,*ptq;
	vm_offset_t va;
	vm_offset_t pa;
	pt_entry_t *pte;
	pv_entry_t pv, npv;
	int ix;
	int s, bits;

#ifdef DEBUG
	pt_entry_t opte;

	if (pmapdebug & (PDB_FOLLOW|PDB_REMOVE|PDB_PROTECT))
		pg("pmap_remove(%x, %x, %x)\n", pmap, sva, eva);
#endif

	if (pmap == NULL)
		return;

	/* are we current address space or kernel? */
	if (pmap->pm_pdir[PTDPTDI].pd_pfnum == PTDpde.pd_pfnum
		|| pmap == pmap_kernel())
		ptp=PTmap;

	/* otherwise, we are alternate address space */
	else {
		if (pmap->pm_pdir[PTDPTDI].pd_pfnum
			!= APTDpde.pd_pfnum) {
			APTDpde = pmap->pm_pdir[PTDPTDI];
			tlbflush();
		}
		ptp=APTmap;
	     }
#ifdef DEBUG
	remove_stats.calls++;
#endif
	
	/* this is essential since we must check the PDE(sva) for precense */
	while (sva <= eva && !pmap_pde_v(pmap_pde(pmap, sva)))
		sva = (sva & PD_MASK) + (1<<PD_SHIFT);
	sva = ns532_btop(sva);
	eva = ns532_btop(eva);

	for (; sva < eva; sva++) {
		/*
		 * Weed out invalid mappings.
		 * Note: we assume that the page directory table is
	 	 * always allocated, and in kernel virtual.
		 */
		ptq=ptp+sva;
		while((sva & 0x3ff) && !pmap_pte_pa(ptq))
		    {
		    if(++sva >= eva)
		        return;
		    ptq++;
		    }		    


		if(!(sva & 0x3ff)) /* Only check once in a while */
 		    {
		    if (!pmap_pde_v(pmap_pde(pmap, ns532_ptob(sva))))
			{
			/* We can race ahead here, straight to next pde.. */
			sva = (sva & 0xffc00) + (1<<10) -1 ;
			continue;
			}
		    }
	        if(!pmap_pte_pa(ptp+sva))
		    continue;

		pte = ptp + sva;
		pa = pmap_pte_pa(pte);
		va = ns532_ptob(sva);
#ifdef DEBUG
		opte = *pte;
		remove_stats.removes++;
#endif
		/*
		 * Update statistics
		 */
		if (pmap_pte_w(pte))
			pmap->pm_stats.wired_count--;
		pmap->pm_stats.resident_count--;

		/*
		 * Invalidate the PTEs.
		 * XXX: should cluster them up and invalidate as many
		 * as possible at once.
		 */
#ifdef DEBUG
		if (pmapdebug & PDB_REMOVE)
			printf("remove: inv %x ptes at pte %x pa %x va %x\n",
			       ns532pagesperpage, pte, pa, va);
#endif
		bits = ix = 0;
		do {
			bits |= *(int *)pte & (PG_U|PG_M);
			*(int *)pte++ = 0;
			/*TBIS(va + ix * NS532_PAGE_SIZE);*/
		} while (++ix != ns532pagesperpage);
		if (curproc && pmap == &curproc->p_vmspace->vm_pmap)
			pmap_activate(pmap, (struct pcb *)curproc->p_addr);
#if 0
/* commented out in 386 version as well */
		/* are we current address space or kernel? */
		if (pmap->pm_pdir[PTDPTDI].pd_pfnum == PTDpde.pd_pfnum
				|| pmap == pmap_kernel()) {
			_load_ptb0(curpcb->pcb_ptb);
		}
#endif
		tlbflush();

#ifdef needednotdone
reduce wiring count on page table pages as references drop
#endif

		/*
		 * Remove from the PV table (raise IPL since we
		 * may be called at interrupt time).
		 */
		if (pa < vm_first_phys || pa >= vm_last_phys)
			continue;
		pv = pa_to_pvh(pa);
		s = splimp();
		/*
		 * If it is the first entry on the list, it is actually
		 * in the header and we must copy the following entry up
		 * to the header.  Otherwise we must search the list for
		 * the entry.  In either case we free the now unused entry.
		 */
		if (pmap == pv->pv_pmap && va == pv->pv_va) {
			npv = pv->pv_next;
			if (npv) {
				*pv = *npv;
				free((caddr_t)npv, M_VMPVENT);
			} else
				pv->pv_pmap = NULL;
#ifdef DEBUG
			remove_stats.pvfirst++;
#endif
		} else {
			for (npv = pv->pv_next; npv; npv = npv->pv_next) {
#ifdef DEBUG
				remove_stats.pvsearch++;
#endif
				if (pmap == npv->pv_pmap && va == npv->pv_va)
					break;
				pv = npv;
			}
#ifdef DEBUG
			if (npv == NULL) {
				printf("vm_first_phys %x pa %x vm_last_phys %x\n",
					vm_first_phys, pa, vm_last_phys);
				panic("pmap_remove: PA not in pv_tab");
			}
#endif
			pv->pv_next = npv->pv_next;
			free((caddr_t)npv, M_VMPVENT);
			pv = pa_to_pvh(pa);
		}

#ifdef notdef
[tally number of pagetable pages, if sharing of ptpages adjust here]
#endif
		/*
		 * Update saved attributes for managed page
		 */
		pmap_attributes[pa_index(pa)] |= bits;
		splx(s);
	}
#ifdef notdef
[cache and tlb flushing, if needed]
#endif
}

/*
 *	Routine:	pmap_remove_all
 *	Function:
 *		Removes this physical page from
 *		all physical maps in which it resides.
 *		Reflects back modify bits to the pager.
 */
void
pmap_remove_all(pa)
	vm_offset_t pa;
{
	register pv_entry_t pv;
	int s;

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_REMOVE|PDB_PROTECT))
		printf("pmap_remove_all(%x)", pa);
	/*pmap_pvdump(pa);*/
#endif
	/*
	 * Not one of ours
	 */
	if (pa < vm_first_phys || pa >= vm_last_phys)
		return;

	pv = pa_to_pvh(pa);
	s = splimp();
	/*
	 * Do it the easy way for now
	 */
	while (pv->pv_pmap != NULL) {
#ifdef DEBUG
		if (!pmap_pde_v(pmap_pde(pv->pv_pmap, pv->pv_va)) ||
		    pmap_pte_pa(pmap_pte(pv->pv_pmap, pv->pv_va)) != pa)
			panic("pmap_remove_all: bad mapping");
#endif
		pmap_remove(pv->pv_pmap, pv->pv_va, pv->pv_va + PAGE_SIZE);
	}
	splx(s);
}

/*
 *	Routine:	pmap_copy_on_write
 *	Function:
 *		Remove write privileges from all
 *		physical maps for this physical page.
 */
void
pmap_copy_on_write(pa)
	vm_offset_t pa;
{
#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_PROTECT))
		printf("pmap_copy_on_write(%x)", pa);
#endif
	pmap_changebit(pa, /* was PG_RO, TRUE */  PG_RW, FALSE);
}

/*
 *	Set the physical protection on the
 *	specified range of this map as requested.
 */
void
pmap_protect(pmap, sva, eva, prot)
	register pmap_t	pmap;
	vm_offset_t	sva, eva;
	vm_prot_t	prot;
{
	register pt_entry_t *pte;
	register vm_offset_t va;
	register int ix;
	int ns532prot;
	boolean_t firstpage = TRUE;
	register pt_entry_t *ptp;

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_PROTECT))
		printf("pmap_protect(%x, %x, %x, %x)", pmap, sva, eva, prot);
#endif
	if (pmap == NULL)
		return;

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}
	if (prot & VM_PROT_WRITE)
		return;

	/* are we current address space or kernel? */
	if (pmap->pm_pdir[PTDPTDI].pd_pfnum == PTDpde.pd_pfnum
		|| pmap == pmap_kernel())
		ptp=PTmap;

	/* otherwise, we are alternate address space */
	else {
		if (pmap->pm_pdir[PTDPTDI].pd_pfnum
			!= APTDpde.pd_pfnum) {
			APTDpde = pmap->pm_pdir[PTDPTDI];
			tlbflush();
		}
		ptp=APTmap;
	     }
	for (va = sva; va < eva; va += PAGE_SIZE) {
		/*
		 * Page table page is not allocated.
		 * Skip it, we don't want to force allocation
		 * of unnecessary PTE pages just to set the protection.
		 */
		if (!pmap_pde_v(pmap_pde(pmap, va))) {
			/* XXX: avoid address wrap around */
			if (va >= ns532_trunc_pdr((vm_offset_t)-1))
				break;
			va = ns532_round_pdr(va + PAGE_SIZE) - PAGE_SIZE;
			continue;
		}

		pte = ptp + ns532_btop(va);

		/*
		 * Page not valid.  Again, skip it.
		 * Should we do this?  Or set protection anyway?
		 */
		if (!pmap_pte_v(pte))
			continue;

		ix = 0;
		ns532prot = pte_prot(pmap, prot);
		if(va < UPT_MAX_ADDRESS)
			ns532prot |= 2 /*PG_u*/;
		do {
			/* clear VAC here if PG_RO? */
			pmap_pte_set_prot(pte++, ns532prot);
			/*TBIS(va + ix * NS532_PAGE_SIZE);*/
		} while (++ix != ns532pagesperpage);
	}
	if (curproc && pmap == &curproc->p_vmspace->vm_pmap)
		pmap_activate(pmap, (struct pcb *)curproc->p_addr);
}

/*
 *	Insert the given physical page (p) at
 *	the specified virtual address (v) in the
 *	target physical map with the protection requested.
 *
 *	If specified, the page will be wired down, meaning
 *	that the related pte can not be reclaimed.
 *
 *	NB:  This is the only routine which MAY NOT lazy-evaluate
 *	or lose information.  That is, this routine must actually
 *	insert this page into the given map NOW.
 */
void
pmap_enter(pmap, va, pa, prot, wired)
	register pmap_t pmap;
	vm_offset_t va;
	register vm_offset_t pa;
	vm_prot_t prot;
	boolean_t wired;
{
	register pt_entry_t *pte;
	register int npte, ix;
	vm_offset_t opa;
	boolean_t cacheable = TRUE;
	boolean_t checkpv = TRUE;

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_ENTER))
		printf("pmap_enter(%x, %x, %x, %x, %x)\n",
		       pmap, va, pa, prot, wired);
#endif
	if (pmap == NULL)
		return;

	if(va >= VM_MAX_KERNEL_ADDRESS)
		panic("pmap_enter: toobig");
	/* also, should not muck with PTD va! */

#ifdef DEBUG
	if (pmap == pmap_kernel())
		enter_stats.kernel++;
	else
		enter_stats.user++;
#endif

	/*
	 * Page Directory table entry not valid, we need a new PT page
	 */
	pte = pmap_pte(pmap, va);
	if (!pte)
		panic("ptdi %x", pmap->pm_pdir[PTDPTDI]);

#ifdef DEBUG
	if (pmapdebug & PDB_ENTER)
		printf("enter: pte %x, *pte %x ", pte, *(int *)pte);
#endif


	if (pmap_pte_v(pte)) {
		register vm_offset_t opa;

		opa = pmap_pte_pa(pte);

		/*
		 * Mapping has not changed, must be protection or wiring change.
		 */
		if (opa == pa) {
#ifdef DEBUG
			enter_stats.pwchange++;
#endif
			/*
			 * Wiring change, just update stats.
			 * We don't worry about wiring PT pages as they remain
			 * resident as long as there are valid mappings in them.
			 * Hence, if a user page is wired, the PT page will be also.
			 */
			if (wired && !pmap_pte_w(pte) || !wired && pmap_pte_w(pte)) {
#ifdef DEBUG
				if (pmapdebug & PDB_ENTER)
					pg("enter: wiring change -> %x ", wired);
#endif
				if (wired)
					pmap->pm_stats.wired_count++;
				else
					pmap->pm_stats.wired_count--;
#ifdef DEBUG
				enter_stats.wchange++;
#endif
			}
			goto validate;
		}
		
		/*
		 * Mapping has changed, invalidate old range and fall through to
		 * handle validating new mapping.
		 */
#ifdef DEBUG
		if (pmapdebug & PDB_ENTER)
			printf("enter: removing old mapping %x pa %x ", va, opa);
#endif
		pmap_remove(pmap, va, va + NBPG);
#ifdef DEBUG
		enter_stats.mchange++;
#endif
	}

	/*
	 * Enter on the PV list if part of our managed memory
	 * Note that we raise IPL while manipulating pv_table
	 * since pmap_enter can be called at interrupt time.
	 */
/*	if (pmap_valid_page(pa)) in the i386 version ... */
	if (pa >= vm_first_phys && pa < vm_last_phys) {
		register pv_entry_t pv, npv;
		int s;

#ifdef DEBUG
		enter_stats.managed++;
#endif
		pv = pa_to_pvh(pa);
		s = splimp();
#ifdef DEBUG
		if (pmapdebug & PDB_ENTER)
			printf("enter: pv at %x: %x/%x/%x\n",
			       pv, pv->pv_va, pv->pv_pmap, pv->pv_next);
#endif
		/*
		 * No entries yet, use header as the first entry
		 */
		if (pv->pv_pmap == NULL) {
#ifdef DEBUG
			enter_stats.firstpv++;
#endif
			pv->pv_va = va;
			pv->pv_pmap = pmap;
			pv->pv_next = NULL;
			pv->pv_flags = 0;
		}
		/*
		 * There is at least one other VA mapping this page.
		 * Place this entry after the header.
		 */
		else {
			/*printf("second time: ");*/
#ifdef DEBUG
			for (npv = pv; npv; npv = npv->pv_next)
				if (pmap == npv->pv_pmap && va == npv->pv_va)
					panic("pmap_enter: already in pv_tab");
#endif
			npv = (pv_entry_t)
				malloc(sizeof *npv, M_VMPVENT, M_NOWAIT);
			if (npv == NULL)
				panic("pmap_enter: malloc returned NULL");
			npv->pv_va = va;
			npv->pv_pmap = pmap;
			npv->pv_next = pv->pv_next;
			pv->pv_next = npv;
#ifdef DEBUG
			if (!npv->pv_next)
				enter_stats.secondpv++;
#endif
		}
		splx(s);
	}
	/*
	 * Assumption: if it is not part of our managed memory
	 * then it must be device memory which may be volitile.
	 */
	if (pmap_initialized) {
		checkpv = cacheable = FALSE;
#ifdef DEBUG
		enter_stats.unmanaged++;
#endif
	}

	/*
	 * Increment counters
	 */
	pmap->pm_stats.resident_count++;
	if (wired)
		pmap->pm_stats.wired_count++;

validate:
	/*
	 * Now validate mapping with desired protection/wiring.
	 * Assume uniform modified and referenced status for all
	 * Ns532 pages in a MACH page.
	 */
	npte = (pa & PG_FRAME) | pte_prot(pmap, prot) | PG_V;
	npte |= (*(int *)pte & (PG_M|PG_U));
	if (wired)
		npte |= PG_W;
	if (va < VM_MAXUSER_ADDRESS)	/* i.e. below USRSTACK */
		npte |= PG_u;
	else if (va < UPT_MAX_ADDRESS)
		/* pagetables need to be user RW, for some reason, and the
		 * user area must be writable too.  Anything above
		 * VM_MAXUSER_ADDRESS is protected from user access by
		 * the user data and code segment descriptors, so this is OK.
		 *
		 * andrew@werple.apana.org.au
		 */
		npte |= PG_u | PG_RW;

#ifdef DEBUG
	if (pmapdebug & PDB_ENTER)
		printf("enter: new pte value %x\n", npte);
#endif
	ix = 0;
	do {
		*(int *)pte++ = npte;
		/*TBIS(va);*/
		npte += NS532_PAGE_SIZE;
		va += NS532_PAGE_SIZE;
	} while (++ix != ns532pagesperpage);
	pte--;
#ifdef DEBUGx
cache, tlb flushes
#endif
#if 0
	pads(pmap);
	_load_ptb0(((struct pcb *)curproc->p_addr)->pcb_ptb);
#endif
	tlbflush();
}

/*
 *      pmap_page_protect:
 *
 *      Lower the permission for all mappings to a given page.
 */
void
pmap_page_protect(phys, prot)
        vm_offset_t     phys;
        vm_prot_t       prot;
{
        switch (prot) {
        case VM_PROT_READ:
        case VM_PROT_READ|VM_PROT_EXECUTE:
                pmap_copy_on_write(phys);
                break;
        case VM_PROT_ALL:
                break;
        default:
                pmap_remove_all(phys);
                break;
        }
}

/*
 *	Routine:	pmap_change_wiring
 *	Function:	Change the wiring attribute for a map/virtual-address
 *			pair.
 *	In/out conditions:
 *			The mapping must already exist in the pmap.
 */
void
pmap_change_wiring(pmap, va, wired)
	register pmap_t	pmap;
	vm_offset_t	va;
	boolean_t	wired;
{
	register pt_entry_t *pte;
	register int ix;

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_change_wiring(%x, %x, %x)\n", pmap, va, wired);
#endif
	if (pmap == NULL)
		return;

	pte = pmap_pte(pmap, va);
#ifdef DEBUG
	/*
	 * Page table page is not allocated.
	 * Should this ever happen?  Ignore it for now,
	 * we don't want to force allocation of unnecessary PTE pages.
	 */
	if (!pmap_pde_v(pmap_pde(pmap, va))) {
		if (pmapdebug & PDB_PARANOIA)
			pg("pmap_change_wiring: invalid PDE for %x\n", va);
		return;
	}
	/*
	 * Page not valid.  Should this ever happen?
	 * Just continue and change wiring anyway.
	 */
	if (!pmap_pte_v(pte)) {
		if (pmapdebug & PDB_PARANOIA)
			pg("pmap_change_wiring: invalid PTE for %x\n", va);
	}
#endif
	if (wired && !pmap_pte_w(pte) || !wired && pmap_pte_w(pte)) {
		if (wired)
			pmap->pm_stats.wired_count++;
		else
			pmap->pm_stats.wired_count--;
	}
	/*
	 * Wiring is not a hardware characteristic so there is no need
	 * to invalidate TLB.
	 */
	ix = 0;
	do {
		pmap_pte_set_w(pte++, wired);
	} while (++ix != ns532pagesperpage);
}

/*
 *	Routine:	pmap_pte
 *	Function:
 *		Extract the page table entry associated
 *		with the given map/virtual_address pair.
 * [ what about induced faults -wfj]
 */

struct pte *pmap_pte(pmap, va)
	register pmap_t	pmap;
	vm_offset_t va;
{
#ifdef DEBUGx
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_pte(%x, %x) ->\n", pmap, va);
#endif
	if (pmap && pmap_pde_v(pmap_pde(pmap, va))) {

		/* are we current address space or kernel? */
		if (pmap->pm_pdir[PTDPTDI].pd_pfnum == PTDpde.pd_pfnum
			|| pmap == pmap_kernel())
			return ((struct pte *) vtopte(va));

		/* otherwise, we are alternate address space */
		else {
			if (pmap->pm_pdir[PTDPTDI].pd_pfnum
				!= APTDpde.pd_pfnum) {
				APTDpde = pmap->pm_pdir[PTDPTDI];
				tlbflush();
			}
			return((struct pte *) avtopte(va));
		}
	}
	return(0);
}

/*
 *	Routine:	pmap_extract
 *	Function:
 *		Extract the physical page address associated
 *		with the given map/virtual_address pair.
 */

vm_offset_t
pmap_extract(pmap, va)
	register pmap_t	pmap;
	vm_offset_t va;
{
	register vm_offset_t pa;

#ifdef DEBUGx
	if (pmapdebug & PDB_FOLLOW)
		pg("pmap_extract(%x, %x) -> ", pmap, va);
#endif
	pa = 0;
	if (pmap && pmap_pde_v(pmap_pde(pmap, va))) {
		pa = *(int *) pmap_pte(pmap, va);
	}
	if (pa)
		pa = (pa & PG_FRAME) | (va & ~PG_FRAME);
#ifdef DEBUGx
	if (pmapdebug & PDB_FOLLOW)
		printf("%x\n", pa);
#endif
	return(pa);
}

/*
 *	Copy the range specified by src_addr/len
 *	from the source map to the range dst_addr/len
 *	in the destination map.
 *
 *	This routine is only advisory and need not do anything.
 */
void pmap_copy(dst_pmap, src_pmap, dst_addr, len, src_addr)
	pmap_t		dst_pmap;
	pmap_t		src_pmap;
	vm_offset_t	dst_addr;
	vm_size_t	len;
	vm_offset_t	src_addr;
{
/* printf ("pmap_copy: dst=0x%x src=0x%x d_addr=0x%x len=0x%x s_addr=0x%x\n",
	dst_pmap, src_pmap, dst_addr, len, src_addr); */
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_copy(%x, %x, %x, %x, %x)\n",
		       dst_pmap, src_pmap, dst_addr, len, src_addr);
#endif
}

/*
 *	Require that all active physical maps contain no
 *	incorrect entries NOW.  [This update includes
 *	forcing updates of any address map caching.]
 *
 *	Generally used to insure that a thread about
 *	to run will see a semantically correct world.
 */
void pmap_update()
{
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_update()");
#endif
	tlbflush();
}

/*
 *	Routine:	pmap_collect
 *	Function:
 *		Garbage collects the physical map system for
 *		pages which are no longer used.
 *		Success need not be guaranteed -- that is, there
 *		may well be pages which are not referenced, but
 *		others may be collected.
 *	Usage:
 *		Called by the pageout daemon when pages are scarce.
 * [ needs to be written -wfj ]
 */
void
pmap_collect(pmap)
	pmap_t		pmap;
{
	register vm_offset_t pa;
	register pv_entry_t pv;
	register int *pte;
	vm_offset_t kpa;
	int s;

#ifdef DEBUG
	int *pde;
	int opmapdebug;
#endif
	if (pmap != pmap_kernel())
		return;
}

/* [ macro again?, should I force kstack into user map here? -wfj ] */
void
pmap_activate(pmap, pcbp)
	register pmap_t pmap;
	struct pcb *pcbp;
{
#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_PDRTAB))
		pg("pmap_activate(%x, %x)\n", pmap, pcbp);
#endif
	PMAP_ACTIVATE(pmap, pcbp);
#ifdef DEBUG
	{
		int x;
		printf("pde ");
		for(x=0x3f6; x < 0x3fA; x++)
			printf("%x ", pmap->pm_pdir[x]);
		pads(pmap);
		pg(" pcb_ptb %x\n", pcbp->pcb_ptb);
	}
#endif
}

/*
 *	pmap_zero_page zeros the specified (machine independent)
 *	page by mapping the page into virtual memory and using
 *	bzero to clear its contents, one machine dependent page
 *	at a time.
 */
void
pmap_zero_page(phys)
	register vm_offset_t	phys;
{
	register int ix;

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_zero_page(%x)\n", phys);
#endif
	phys >>= PG_SHIFT;
	ix = 0;
	do {
		clearseg(phys++);
	} while (++ix != ns532pagesperpage);
}

/*
 *	pmap_copy_page copies the specified (machine independent)
 *	page by mapping the page into virtual memory and using
 *	bcopy to copy the page, one machine dependent page at a
 *	time.
 */
void
pmap_copy_page(src, dst)
	register vm_offset_t	src, dst;
{
	register int ix;

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_copy_page(%x, %x)", src, dst);
#endif
	src >>= PG_SHIFT;
	dst >>= PG_SHIFT;
	ix = 0;
	do {
		physcopyseg(src++, dst++);
	} while (++ix != ns532pagesperpage);
}


/*
 *	Routine:	pmap_pageable
 *	Function:
 *		Make the specified pages (by pmap, offset)
 *		pageable (or not) as requested.
 *
 *		A page which is not pageable may not take
 *		a fault; therefore, its page table entry
 *		must remain valid for the duration.
 *
 *		This routine is merely advisory; pmap_enter
 *		will specify that these pages are to be wired
 *		down (or not) as appropriate.
 */
void
pmap_pageable(pmap, sva, eva, pageable)
	pmap_t		pmap;
	vm_offset_t	sva, eva;
	boolean_t	pageable;
{
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_pageable(%x, %x, %x, %x)\n",
		       pmap, sva, eva, pageable);
#endif
	/*
	 * If we are making a PT page pageable then all valid
	 * mappings must be gone from that page.  Hence it should
	 * be all zeros and there is no need to clean it.
	 * Assumptions:
	 *	- we are called with only one page at a time
	 *	- PT pages have only one pv_table entry
	 */
	if (pmap == pmap_kernel() && pageable && sva + PAGE_SIZE == eva) {
		register pv_entry_t pv;
		register vm_offset_t pa;

#ifdef DEBUG
		if ((pmapdebug & (PDB_FOLLOW|PDB_PTPAGE)) == PDB_PTPAGE)
			printf("pmap_pageable(%x, %x, %x, %x)\n",
			       pmap, sva, eva, pageable);
#endif
		/*if (!pmap_pde_v(pmap_pde(pmap, sva)))
			return;*/
		if(pmap_pte(pmap, sva) == 0)
			return;
		pa = pmap_pte_pa(pmap_pte(pmap, sva));
		if (pa < vm_first_phys || pa >= vm_last_phys)
			return;
		pv = pa_to_pvh(pa);
		/*if (!ispt(pv->pv_va))
			return;*/
#ifdef DEBUG
		if (pv->pv_va != sva || pv->pv_next) {
			pg("pmap_pageable: bad PT page va %x next %x\n",
			       pv->pv_va, pv->pv_next);
			return;
		}
#endif
		/*
		 * Mark it unmodified to avoid pageout
		 */
		pmap_clear_modify(pa);
#ifdef needsomethinglikethis
		if (pmapdebug & PDB_PTPAGE)
			pg("pmap_pageable: PT page %x(%x) unmodified\n",
			       sva, *(int *)pmap_pte(pmap, sva));
		if (pmapdebug & PDB_WIRING)
			pmap_check_wiring("pageable", sva);
#endif
	}
}

/*
 *	Clear the modify bits on the specified physical page.
 */

void
pmap_clear_modify(pa)
	vm_offset_t	pa;
{
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_clear_modify(%x)", pa);
#endif
	pmap_changebit(pa, PG_M, FALSE);
}

/*
 *	pmap_clear_reference:
 *
 *	Clear the reference bit on the specified physical page.
 */

void pmap_clear_reference(pa)
	vm_offset_t	pa;
{
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_clear_reference(%x)", pa);
#endif
	pmap_changebit(pa, PG_U, FALSE);
}

/*
 *	pmap_is_referenced:
 *
 *	Return whether or not the specified physical page is referenced
 *	by any physical maps.
 */

boolean_t
pmap_is_referenced(pa)
	vm_offset_t	pa;
{
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW) {
		boolean_t rv = pmap_testbit(pa, PG_U);
		printf("pmap_is_referenced(%x) -> %c", pa, "FT"[rv]);
		return(rv);
	}
#endif
	return(pmap_testbit(pa, PG_U));
}

/*
 *	pmap_is_modified:
 *
 *	Return whether or not the specified physical page is modified
 *	by any physical maps.
 */

boolean_t
pmap_is_modified(pa)
	vm_offset_t	pa;
{
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW) {
		boolean_t rv = pmap_testbit(pa, PG_M);
		printf("pmap_is_modified(%x) -> %c\n", pa, "FT"[rv]);
		return(rv);
	}
#endif
	return(pmap_testbit(pa, PG_M));
}

vm_offset_t
pmap_phys_address(ppn)
	int ppn;
{
	return(ns532_ptob(ppn));
}

/*
 * Miscellaneous support routines follow
 */

ns532_protection_init()
{
	register int *kp, prot;

	kp = protection_codes;
	for (prot = 0; prot < 8; prot++) {
		switch (prot) {
		case VM_PROT_NONE | VM_PROT_NONE | VM_PROT_NONE:
			*kp++ = 0;
			break;
		case VM_PROT_READ | VM_PROT_NONE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_NONE | VM_PROT_EXECUTE:
		case VM_PROT_NONE | VM_PROT_NONE | VM_PROT_EXECUTE:
			*kp++ = PG_RO;
			break;
		case VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_NONE:
		case VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_EXECUTE:
		case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE:
			*kp++ = PG_RW;
			break;
		}
	}
}

boolean_t
pmap_testbit(pa, bit)
	register vm_offset_t pa;
	int bit;
{
	register pv_entry_t pv;
	register int *pte, ix;
	int s;

	if (pa < vm_first_phys || pa >= vm_last_phys)
		return(FALSE);

	pv = pa_to_pvh(pa);
	s = splimp();
	/*
	 * Check saved info first
	 */
	if (pmap_attributes[pa_index(pa)] & bit) {
		splx(s);
		return(TRUE);
	}
	/*
	 * Not found, check current mappings returning
	 * immediately if found.
	 */
	if (pv->pv_pmap != NULL) {
		for (; pv; pv = pv->pv_next) {
			pte = (int *) pmap_pte(pv->pv_pmap, pv->pv_va);
			ix = 0;
			do {
				if (*pte++ & bit) {
					splx(s);
					return(TRUE);
				}
			} while (++ix != ns532pagesperpage);
		}
	}
	splx(s);
	return(FALSE);
}

pmap_changebit(pa, bit, setem)
	register vm_offset_t pa;
	int bit;
	boolean_t setem;
{
	register pv_entry_t pv;
	register int *pte, npte, ix;
	vm_offset_t va;
	int s;
	boolean_t firstpage = TRUE;

#ifdef DEBUG
	if (pmapdebug & PDB_BITS)
		printf("pmap_changebit(%x, %x, %s)",
		       pa, bit, setem ? "set" : "clear");
#endif
	if (pa < vm_first_phys || pa >= vm_last_phys)
		return;

	pv = pa_to_pvh(pa);
	s = splimp();
	/*
	 * Clear saved attributes (modify, reference)
	 */
	if (!setem)
		pmap_attributes[pa_index(pa)] &= ~bit;

	/*
	 * Loop over all current mappings setting/clearing as appropos
	 * If setting RO do we need to clear the VAC?
	 */

	if (pv->pv_pmap != NULL) {
#ifdef DEBUG
		int toflush = 0;
#endif
		for (; pv; pv = pv->pv_next) {
#ifdef DEBUG
			toflush |= (pv->pv_pmap == pmap_kernel()) ? 2 : 1;
#endif
			va = pv->pv_va;

                        /*
                         * XXX don't write protect pager mappings
                         */
                        if (bit == PG_RO) {
                                extern vm_offset_t pager_sva, pager_eva;

                                if (va >= pager_sva && va < pager_eva)
                                        continue;
                        }

			pte = (int *) pmap_pte(pv->pv_pmap, va);
			ix = 0;
			do {
				if (setem)
					npte = *pte | bit;
				else
					npte = *pte & ~bit;
				if (*pte != npte) {
					*pte = npte;
					/*TBIS(va);*/
				}
				va += NS532_PAGE_SIZE;
				pte++;
			} while (++ix != ns532pagesperpage);

			if (curproc && pv->pv_pmap == &curproc->p_vmspace->vm_pmap)
				pmap_activate(pv->pv_pmap, (struct pcb *)curproc->p_addr);
		}
#ifdef somethinglikethis
		if (setem && bit == PG_RO && (pmapvacflush & PVF_PROTECT)) {
			if ((pmapvacflush & PVF_TOTAL) || toflush == 3)
				DCIA();
			else if (toflush == 2)
				DCIS();
			else
				DCIU();
		}
#endif
	}
	splx(s);
}

#ifdef DEBUG
pmap_pvdump(pa)
	vm_offset_t pa;
{
	register pv_entry_t pv;

	printf("pa %x", pa);
	for (pv = pa_to_pvh(pa); pv; pv = pv->pv_next) {
		printf(" -> pmap %x, va %x, flags %x",
		       pv->pv_pmap, pv->pv_va, pv->pv_flags);
		pads(pv->pv_pmap);
	}
	printf(" ");
}

#ifdef notyet
pmap_check_wiring(str, va)
	char *str;
	vm_offset_t va;
{
	vm_map_entry_t entry;
	register int count, *pte;

	va = trunc_page(va);
	if (!pmap_pde_v(pmap_pde(pmap_kernel(), va)) ||
	    !pmap_pte_v(pmap_pte(pmap_kernel(), va)))
		return;

	if (!vm_map_lookup_entry(pt_map, va, &entry)) {
		pg("wired_check: entry for %x not found\n", va);
		return;
	}
	count = 0;
	for (pte = (int *)va; pte < (int *)(va+PAGE_SIZE); pte++)
		if (*pte)
			count++;
	if (entry->wired_count != count)
		pg("*%s*: %x: w%d/a%d\n",
		       str, va, entry->wired_count, count);
}
#endif

/* print address space of pmap*/
pads(pm)
	pmap_t pm;
{
	unsigned va, i, j;
	struct pte *ptep;
	int num=0;

/*	if(pm == pmap_kernel()) return; */
	for (i = 0; i < 1024; i++) 
		if(pm->pm_pdir[i].pd_v)
			for (j = 0; j < 1024 ; j++) {
				va = (i<<22)+(j<<12);
				if (pm == pmap_kernel() && va < 0xfe000000)
						continue;
				if (pm != pmap_kernel() && va > UPT_MAX_ADDRESS)
						continue;
				ptep = pmap_pte(pm, va);
				if(pmap_pte_v(ptep)) {
				    if (num % 4 == 0) printf ("    ");
				    printf("%8x:%8x", va, *(int *)ptep); 
				    if (++num %4 == 0)
				      printf ("\n");
				    else
				      printf (" ");
				}
			} ;
	if (num % 4 != 0) printf ("\n");
}

pmap_print (pmap_t pm, unsigned int start, unsigned int stop)
{
	unsigned va, i, j;
	struct pte *ptep;
	int num;

	printf ("pmap_print: pm_pdir = 0x%x\n", pm->pm_pdir);
	printf ("  map between 0x%x and 0x%x\n", start, stop);
	for (i = 0; i < 1024; i++)
	    if (pm->pm_pdir != 0) {
		if(pm->pm_pdir[i].pd_v && (start>>22) <= i 
			&& (stop>>22) >= i) {
			printf ("1st Level Entry 0x%x, 2nd PA = 0x%x000\n",
				i, pm->pm_pdir[i].pd_pfnum);
			num = 0;
			for (j = 0; j < 1024 ; j++) {
				va = (i<<22)+(j<<12);
				ptep = pmap_pte(pm, va);
				if(ptep->pg_v && start <= va && stop >= va) {
				    if (num % 5 == 0) printf ("    ");
				    printf("%8x:%05x", va, ptep->pg_pfnum); 
				    if (++num %5 == 0)
				      printf ("\n");
				    else
				      printf (" ");
				}
			} ;
			if (num % 5 != 0) printf ("\n");
		};
	};
	if (num % 5 != 0) printf ("\n");
}
#endif
