/*	$OpenBSD: pmap.c,v 1.81 2002/09/05 18:41:19 mickey Exp $	*/

/*
 * Copyright (c) 1998-2002 Michael Shalayeff
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
 *	This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF MIND,
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * References:
 * 1. PA7100LC ERS, Hewlett-Packard, March 30 1999, Public version 1.0
 * 2. PA7300LC ERS, Hewlett-Packard, March 18 1996, Version 1.0
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/pool.h>
#include <sys/extent.h>

#include <uvm/uvm.h>

#include <machine/cpufunc.h>

#ifdef PMAPDEBUG
#define	DPRINTF(l,s)	do {		\
	if ((pmapdebug & (l)) == (l))	\
		printf s;		\
} while(0)
#define	PDB_FOLLOW	0x00000001
#define	PDB_INIT	0x00000002
#define	PDB_ENTER	0x00000004
#define	PDB_REMOVE	0x00000008
#define	PDB_CREATE	0x00000010
#define	PDB_PTPAGE	0x00000020
#define	PDB_CACHE	0x00000040
#define	PDB_BITS	0x00000080
#define	PDB_COLLECT	0x00000100
#define	PDB_PROTECT	0x00000200
#define	PDB_EXTRACT	0x00000400
#define	PDB_VP		0x00000800
#define	PDB_PV		0x00001000
#define	PDB_PARANOIA	0x00002000
#define	PDB_WIRING	0x00004000
#define	PDB_PMAP	0x00008000
#define	PDB_STEAL	0x00010000
#define	PDB_PHYS	0x00020000
#define	PDB_POOL	0x00040000
int pmapdebug = 0
	| PDB_INIT
	| PDB_FOLLOW
/*	| PDB_VP */
/*	| PDB_PV */
/*	| PDB_ENTER */
/*	| PDB_REMOVE */
/*	| PDB_STEAL */
/*	| PDB_PROTECT */
/*	| PDB_PHYS */
	;
#else
#define	DPRINTF(l,s)	/* */
#endif

paddr_t physical_steal, physical_end;

#if defined(HP7100LC_CPU) || defined(HP7300LC_CPU)
int		pmap_hptsize = 256;	/* patchable */
#endif

struct pmap	kernel_pmap_store;
int		pmap_sid_counter, hppa_sid_max = HPPA_SID_MAX;
boolean_t	pmap_initialized = FALSE;
struct pool	pmap_pmap_pool;
struct pool	pmap_pv_pool;
struct simplelock pvalloc_lock;

u_int	hppa_prot[8];

#define	pmap_sid(pmap, va) \
	(((va & 0xc0000000) != 0xc0000000)? pmap->pmap_space : HPPA_SID_KERNEL)

#define	pmap_pvh_attrs(a) \
	(((a) & PTE_PROT(TLB_DIRTY)) | ((a) ^ PTE_PROT(TLB_REFTRAP)))

struct vm_page *
pmap_pagealloc(struct uvm_object *obj, voff_t off)
{
	struct vm_page *pg = uvm_pagealloc(obj, off, NULL,
	    UVM_PGA_USERESERVE | UVM_PGA_ZERO);

	if (!pg) {
		/* wait and pageout */

		return (NULL);
	}

	return (pg);
}

#if defined(HP7100LC_CPU) || defined(HP7300LC_CPU)
/*
 * This hash function is the one used by the hardware TLB walker on the 7100LC.
 */
static inline struct hpt_entry *
pmap_hash(pa_space_t sp, vaddr_t va)
{
	struct hpt_entry *hpt;
	__asm __volatile (
		"extru	%2, 23, 20, %%r22\n\t"	/* r22 = (va >> 8) */
		"zdep	%1, 22, 16, %%r23\n\t"	/* r23 = (sp << 9) */
		"xor	%%r22,%%r23, %%r23\n\t"	/* r23 ^= r22 */
		"mfctl	%%cr24, %%r22\n\t"	/* r22 = sizeof(HPT)-1 */
		"and	%%r22,%%r23, %%r23\n\t"	/* r23 &= r22 */
		"mfctl	%%cr25, %%r22\n\t"	/* r22 = addr of HPT table */
		"or	%%r23, %%r22, %0"	/* %0 = HPT entry */
		: "=r" (hpt) : "r" (sp), "r" (va) : "r22", "r23");
	return hpt;
}
#endif

static __inline void
pmap_sdir_set(pa_space_t space, paddr_t pa)
{
	paddr_t vtop;

	mfctl(CR_VTOP, vtop);
#ifdef PMAPDEBUG
	if (!vtop)
		panic("pmap_sdir_set: zero vtop");
#endif
	asm("stws	%0, 0(%1)\n\tsync"
	    :: "r" (pa), "r" (vtop + (space << 2)));
}

static __inline paddr_t
pmap_sdir_get(pa_space_t space)
{
	paddr_t vtop, pa;

	mfctl(CR_VTOP, vtop);
	asm("ldwx,s	%2(%1), %0\n\tsync"
	    : "=&r" (pa) : "r" (vtop), "r" (space));

	return (pa);
}

static __inline pt_entry_t *
pmap_pde_get(paddr_t pa, vaddr_t va)
{
	pt_entry_t *pde;

	asm("ldwx,s	%2(%1), %0\n\tsync"
	    : "=&r" (pde) : "r" (pa), "r" (va >> 22));

	return (pde);
}

static __inline void
pmap_pde_set(struct pmap *pm, vaddr_t va, paddr_t ptp)
{
#ifdef PMAPDEBUG
	if (ptp & PGOFSET)
		panic("pmap_pde_set, unaligned ptp 0x%x", ptp);
#endif
	DPRINTF(PDB_FOLLOW|PDB_VP,
	    ("pmap_pde_set(%p, 0x%x, 0x%x)\n", pm, va, ptp));

	asm("stws	%0, 0(%1)\n\tsync"
	    :: "r" (ptp), "r" ((paddr_t)pm->pm_pdir + ((va >> 20) & 0xffc)));
}

static __inline pt_entry_t *
pmap_pde_alloc(struct pmap *pm, vaddr_t va, struct vm_page **pdep)
{
	struct vm_page *pg;
	paddr_t pa;

	DPRINTF(PDB_FOLLOW|PDB_VP,
	    ("pmap_pde_alloc(%p, 0x%x, %p)\n", pm, va, pdep));

	if ((pg = pmap_pagealloc(&pm->pm_obj, va)) == NULL)
		return (NULL);

	pa = VM_PAGE_TO_PHYS(pg);

	DPRINTF(PDB_FOLLOW|PDB_VP, ("pmap_pde_alloc: pde %x\n", pa));

	pg->flags &= ~PG_BUSY;		/* never busy */
	pg->wire_count = 1;		/* no mappings yet */
	pmap_pde_set(pm, va, pa);
	pm->pm_stats.resident_count++;	/* count PTP as resident */
	pm->pm_ptphint = pg;
	if (pdep)
		*pdep = pg;
	return ((pt_entry_t *)pa);
}

static __inline struct vm_page *
pmap_pde_ptp(struct pmap *pm, pt_entry_t *pde)
{
	paddr_t pa = (paddr_t)pde;

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_pde_ptp(%p, %p)\n", pm, pde));

	if (pm->pm_ptphint && VM_PAGE_TO_PHYS(pm->pm_ptphint) == pa)
		return (pm->pm_ptphint);

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_pde_ptp: lookup 0x%x\n", pa));

	return (PHYS_TO_VM_PAGE(pa));
}

static __inline void
pmap_pde_release(struct pmap *pmap, vaddr_t va, struct vm_page *ptp)
{
	DPRINTF(PDB_FOLLOW|PDB_PV,
	    ("pmap_pde_release(%p, 0x%x, %p)\n", pmap, va, ptp));

	ptp->wire_count--;
	if (ptp->wire_count <= 1 && pmap != pmap_kernel()) {
		DPRINTF(PDB_FOLLOW|PDB_PV,
		    ("pmap_pde_release: disposing ptp %p\n", ptp));

		if (pmap->pm_ptphint == ptp)
			pmap->pm_ptphint = TAILQ_FIRST(&pmap->pm_obj.memq);
#if 0
		pmap_pde_set(pmap, va, 0);
		pmap->pm_stats.resident_count--;
		ptp->wire_count = 0;
		uvm_pagefree(ptp);
#endif
	}
}

static __inline pt_entry_t
pmap_pte_get(pt_entry_t *pde, vaddr_t va)
{
	pt_entry_t pte;

	asm("ldwx,s	%2(%1),%0"
	    : "=&r" (pte) : "r" (pde),  "r" ((va >> 12) & 0x3ff));

	return (pte);
}

static __inline void
pmap_pte_set(pt_entry_t *pde, vaddr_t va, pt_entry_t pte)
{
	DPRINTF(PDB_FOLLOW|PDB_VP, ("pmap_pte_set(%p, 0x%x, 0x%x)\n",
	    pde, va, pte));

#ifdef PMAPDEBUG
	if (!pde)
		panic("pmap_pte_set: zero pde");

	if (pte && pmap_initialized && pte < physical_end &&
	    hppa_trunc_page(pte) != (paddr_t)&gateway_page)
		panic("pmap_pte_set: invalid pte 0x%x", pte);
#if 0
	if (pte && !(pte & PTE_PROT(TLB_UNCACHABLE)) &&
	    hppa_trunc_page(pte) != (paddr_t)&gateway_page) {
		printf("pmap_pte_set: cached pte\n");
		Debugger();
	}
#endif
	if ((paddr_t)pde & PGOFSET)
		panic("pmap_pte_set, unaligned pde %p", pde);
#endif
	asm("stws	%0, 0(%1)"
	    :: "r" (pte), "r" ((paddr_t)pde + ((va >> 10) & 0xffc)));
}

static __inline pt_entry_t
pmap_vp_find(struct pmap *pm, vaddr_t va)
{
	pt_entry_t *pde;

	if (!(pde = pmap_pde_get(pm->pm_pdir, va)))
		return (0);

	return (pmap_pte_get(pde, va));
}

#ifdef DDB
void
pmap_dump_table(pa_space_t space, vaddr_t sva)
{
	pa_space_t sp;

	for (sp = 0; sp <= hppa_sid_max; sp++) {
		paddr_t pa;
		pt_entry_t *pde, pte;
		vaddr_t va, pdemask = 1;

		if (((int)space >= 0 && sp != space) ||
		    !(pa = pmap_sdir_get(sp)))
			continue;

		for (va = sva? sva : 0; va < VM_MAX_KERNEL_ADDRESS;
		    va += PAGE_SIZE) {
			if (pdemask != (va & PDE_MASK)) {
				pdemask = va & PDE_MASK;
				if (!(pde = pmap_pde_get(pa, va))) {
					va += ~PDE_MASK + 1 - PAGE_SIZE;
					continue;
				}
				printf("%x:0x%08x:\n", sp, pde);
			}

			if (!(pte = pmap_pte_get(pde, va)))
				continue;

			printf("0x%08x-0x%08x:%b\n", va, pte & ~PAGE_MASK,
			    TLB_PROT(pte & PAGE_MASK), TLB_BITS);
		}
	}
}

void
pmap_dump_pv(paddr_t pa)
{
	struct pv_head *pvh;
	struct pv_entry *pve;
	int bank, off;

	bank = vm_physseg_find(atop(pa), &off);
	if (bank != -1)
		return;

	pvh = &vm_physmem[bank].pmseg.pvhead[off];
	simple_lock(&pvh->pvh_lock);
	for(pve = pvh->pvh_list; pve; pve = pve->pv_next)
		printf("%x:%x\n", pve->pv_pmap->pm_space, pve->pv_va);
	simple_unlock(&pvh->pvh_lock);
}
#endif

#ifdef PMAPDEBUG
int
pmap_check_alias(struct pv_entry *pve, vaddr_t va)
{
	pt_entry_t pte;
	int ret = 0;

	/* check for non-equ aliased mappings */
	for (pte = 0; pve; pve = pve->pv_next) {
		pt_entry_t pte1 = pmap_vp_find(pve->pv_pmap, pve->pv_va);
		if ((va & HPPA_PGAOFF) != (pve->pv_va & HPPA_PGAOFF) &&
		    pte && (pte1 & PTE_PROT(TLB_WRITE))) {
			printf("pmap_pv_enter: "
			    "aliased writable mapping %d:0x%x",
			    pve->pv_pmap->pm_space, pve->pv_va);
			ret++;
		}
		pte |= pte1;
	}

	return (ret);
}
#endif

static __inline struct pv_entry *
pmap_pv_alloc(void)
{
	struct pv_entry *pv;

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_pv_alloc()\n"));

	simple_lock(&pvalloc_lock);

	pv = pool_get(&pmap_pv_pool, 0);

	simple_unlock(&pvalloc_lock);

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_pv_alloc: %p\n", pv));

	return (pv);
}

static __inline void
pmap_pv_free(struct pv_entry *pv)
{
	simple_lock(&pvalloc_lock);

	if (pv->pv_ptp)
		pmap_pde_release(pv->pv_pmap, pv->pv_va, pv->pv_ptp);

	pool_put(&pmap_pv_pool, pv);

	simple_unlock(&pvalloc_lock);
}

static __inline void
pmap_pv_enter(struct pv_head *pvh, struct pv_entry *pve, struct pmap *pm,
    vaddr_t va, struct vm_page *pdep)
{
	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_pv_enter(%p, %p, %p, 0x%x, %p)\n",
	    pvh, pve, pm, va, pdep));
	pve->pv_pmap	= pm;
	pve->pv_va	= va;
	pve->pv_ptp	= pdep;
	simple_lock(&pvh->pvh_lock);		/* lock pv_head */
	pve->pv_next = pvh->pvh_list;
	pvh->pvh_list = pve;
	if (pmap_check_alias(pve, va))
		Debugger();
	simple_unlock(&pvh->pvh_lock);		/* unlock, done! */
}

static __inline struct pv_entry *
pmap_pv_remove(struct pv_head *pvh, struct pmap *pmap, vaddr_t va)
{
	struct pv_entry **pve, *pv;

	simple_lock(&pvh->pvh_lock);		/* lock pv_head */
	for(pv = *(pve = &pvh->pvh_list); pv; pv = *(pve = &(*pve)->pv_next))
		if (pv->pv_pmap == pmap && pv->pv_va == va) {
			*pve = pv->pv_next;
			break;
		}
	simple_unlock(&pvh->pvh_lock);		/* unlock, done! */
	return (pv);
}

void
pmap_bootstrap(vstart)
	vaddr_t vstart;
{
	extern char etext, etext1;
	extern u_int totalphysmem, *ie_mem;
	extern paddr_t hppa_vtop;
	vaddr_t va, endaddr, addr = hppa_round_page(vstart), t;
	vsize_t size;
#if 0 && (defined(HP7100LC_CPU) || defined(HP7300LC_CPU))
	struct vp_entry *hptp;
#endif
	struct pmap *kpm;
	int npdes;

	DPRINTF(PDB_FOLLOW|PDB_INIT, ("pmap_bootstrap(0x%x)\n", vstart));

	uvm_setpagesize();

	hppa_prot[VM_PROT_NONE | VM_PROT_NONE  | VM_PROT_NONE]    =TLB_AR_NA;
	hppa_prot[VM_PROT_READ | VM_PROT_NONE  | VM_PROT_NONE]    =TLB_AR_R;
	hppa_prot[VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_NONE]    =TLB_AR_RW;
	hppa_prot[VM_PROT_READ | VM_PROT_WRITE | VM_PROT_NONE]    =TLB_AR_RW;
	hppa_prot[VM_PROT_NONE | VM_PROT_NONE  | VM_PROT_EXECUTE] =TLB_AR_RX;
	hppa_prot[VM_PROT_READ | VM_PROT_NONE  | VM_PROT_EXECUTE] =TLB_AR_RX;
	hppa_prot[VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_EXECUTE] =TLB_AR_RWX;
	hppa_prot[VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE] =TLB_AR_RWX;

	/*
	 * Initialize kernel pmap
	 */
	kpm = &kernel_pmap_store;
	bzero(kpm, sizeof(*kpm));
	simple_lock_init(&kpm->pm_obj.vmobjlock);
	kpm->pm_obj.pgops = NULL;
	TAILQ_INIT(&kpm->pm_obj.memq);
	kpm->pm_obj.uo_npages = 0;
	kpm->pm_obj.uo_refs = 1;
	kpm->pm_space = HPPA_SID_KERNEL;
	kpm->pm_pid = HPPA_PID_KERNEL;
	kpm->pm_pdir_pg = NULL;
	kpm->pm_pdir = addr;
	bzero((void *)addr, PAGE_SIZE);
	fdcache(HPPA_SID_KERNEL, addr, PAGE_SIZE);
	addr += PAGE_SIZE;

	/*
	 * Allocate various tables and structures.
	 */

	mtctl(addr, CR_VTOP);
	hppa_vtop = addr;
	size = hppa_round_page((hppa_sid_max + 1) * 4);
	bzero((void *)addr, size);
	fdcache(HPPA_SID_KERNEL, addr, size);
	DPRINTF(PDB_INIT, ("vtop: 0x%x @ 0x%x\n", size, addr));
	addr += size;
	pmap_sdir_set(HPPA_SID_KERNEL, kpm->pm_pdir);

	ie_mem = (u_int *)addr;
	addr += 0x8000;

#if 0 && (defined(HP7100LC_CPU) || defined(HP7300LC_CPU))
	if (pmap_hptsize && (cpu_type == hpcxl || cpu_type == hpcxl2)) {
		int error;

		if (pmap_hptsize > pdc_hwtlb.max_size)
			pmap_hptsize = pdc_hwtlb.max_size;
		else if (pmap_hptsize < pdc_hwtlb.min_size)
			pmap_hptsize = pdc_hwtlb.min_size;

		size = pmap_hptsize * sizeof(*hptp);
		bzero((void *)addr, size);
		/* Allocate the HPT */
		for (hptp = (struct vp_entry *)addr, i = pmap_hptsize; i--;)
			hptp[i].vp_tag = 0xffff;

		DPRINTF(PDB_INIT, ("hpt_table: 0x%x @ %p\n", size, addr));

		if ((error = (cpu_hpt_init)(addr, size)) < 0) {
			printf("WARNING: HPT init error %d\n", error);
		} else {
			printf("HPT: %d entries @ 0x%x\n",
			    pmap_hptsize / sizeof(struct vp_entry), addr);
		}

		/* TODO find a way to avoid using cr*, use cpu regs instead */
		mtctl(addr, CR_VTOP);
		mtctl(size - 1, CR_HPTMASK);
		addr += size;
	}
#endif	/* HP7100LC_CPU | HP7300LC_CPU */

	/* XXX PCXS needs this inserted into an IBTLB */
	t = (vaddr_t)&etext1;
	if (btlb_insert(HPPA_SID_KERNEL, 0, 0, &t,
	    pmap_sid2pid(HPPA_SID_KERNEL) |
	    pmap_prot(pmap_kernel(), UVM_PROT_RX)) < 0)
		panic("pmap_bootstrap: cannot block map kernel text");
	kpm->pm_stats.wired_count = kpm->pm_stats.resident_count =
	    physmem = atop(t);

	if (&etext < &etext1) {
		physical_steal = (vaddr_t)&etext;
		physical_end = (vaddr_t)&etext1;
		DPRINTF(PDB_INIT, ("physpool: 0x%x @ 0x%x\n",
		    physical_end - physical_steal, physical_steal));
	}

	/*
	 * NOTE: we no longer trash the BTLB w/ unused entries,
	 * lazy map only needed pieces (see bus_mem_add_mapping() for refs).
	 */

	addr = hppa_round_page(addr);
	size = hppa_round_page(sizeof(struct pv_head) * totalphysmem);
	bzero ((caddr_t)addr, size);

	DPRINTF(PDB_INIT, ("pmseg.pvhead: 0x%x @ 0x%x\n", size, addr));

	/* one for the start of the kernel virtual */
	npdes = 1 + (totalphysmem + btoc(PDE_SIZE) - 1) / btoc(PDE_SIZE);
	endaddr = addr + size + npdes * PAGE_SIZE;
	uvm_page_physload(0, totalphysmem,
	    atop(endaddr), totalphysmem, VM_FREELIST_DEFAULT);
	vm_physmem[0].pmseg.pvhead = (struct pv_head *)addr;
	addr += size;

	/* map the pdes */
	for (va = 0; npdes--; va += PDE_SIZE, addr += PAGE_SIZE) {

		/* last pde is for the start of kernel virtual */
		if (!npdes)
			va = SYSCALLGATE;
		/* now map the pde for the physmem */
		bzero((void *)addr, PAGE_SIZE);
		DPRINTF(PDB_INIT|PDB_VP, ("pde premap 0x%x 0x%x\n", va, addr));
		pmap_pde_set(kpm, va, addr);
		kpm->pm_stats.resident_count++; /* count PTP as resident */
	}

	/* TODO optimize/inline the kenter */
	for (va = 0; va < ptoa(totalphysmem); va += PAGE_SIZE) {
		extern struct user *proc0paddr;
		vm_prot_t prot = UVM_PROT_RW;

		if (va < (vaddr_t)&etext1)
			prot = UVM_PROT_RX;
		else if (va == (vaddr_t)proc0paddr + USPACE)
			prot = UVM_PROT_NONE;

		pmap_kenter_pa(va, va, prot);
	}

	DPRINTF(PDB_INIT, ("bootstrap: mapped %p - 0x%x\n", &etext1, endaddr));
}

void
pmap_init()
{
	DPRINTF(PDB_FOLLOW|PDB_INIT, ("pmap_init()\n"));

	simple_lock_init(&pvalloc_lock);

	pool_init(&pmap_pmap_pool, sizeof(struct pmap), 0, 0, 0, "pmappl",
	    &pool_allocator_nointr);
	pool_init(&pmap_pv_pool, sizeof(struct pv_entry), 0, 0, 0, "pmappv",
	    &pool_allocator_nointr);

	pmap_initialized = TRUE;

	/*
	 * map SysCall gateways page once for everybody
	 * NB: we'll have to remap the phys memory
	 *     if we have any at SYSCALLGATE address (;
	 */
	{
		pt_entry_t *pde;

		if (!(pde = pmap_pde_get(pmap_kernel()->pm_pdir, SYSCALLGATE)) &&
		    !(pde = pmap_pde_alloc(pmap_kernel(), SYSCALLGATE, NULL)))
			panic("pmap_init: cannot allocate pde");

		pmap_pte_set(pde, SYSCALLGATE, (paddr_t)&gateway_page |
		    PTE_PROT(TLB_GATE_PROT));
	}
}

void
pmap_virtual_space(vaddr_t *startp, vaddr_t *endp)
{
	*startp = SYSCALLGATE + PAGE_SIZE;
	*endp = VM_MAX_KERNEL_ADDRESS;
}

struct pmap *
pmap_create()
{
	struct pmap *pmap;
	pa_space_t space;

	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_create()\n"));
	pmap = pool_get(&pmap_pmap_pool, PR_WAITOK);

	simple_lock_init(&pmap->pm_obj.vmobjlock);
	pmap->pm_obj.pgops = NULL;	/* currently not a mappable object */
	TAILQ_INIT(&pmap->pm_obj.memq);
	pmap->pm_obj.uo_npages = 0;
	pmap->pm_obj.uo_refs = 1;

	if (pmap_sid_counter >= hppa_sid_max) {
		/* collect some */
		panic("pmap_create: outer space");
	} else
		space = ++pmap_sid_counter;

	if ((pmap->pm_pdir_pg = pmap_pagealloc(NULL, 0)) == NULL)
		panic("pmap_create: no pages");
	pmap->pm_ptphint = NULL;
	pmap->pm_pdir = VM_PAGE_TO_PHYS(pmap->pm_pdir_pg);
	pmap_sdir_set(space, pmap->pm_pdir);

	pmap->pm_space = space;
	pmap->pm_pid = (space + 1) << 1;

	pmap->pm_stats.resident_count = 1;
	pmap->pm_stats.wired_count = 0;

	return (pmap);
}

void
pmap_destroy(pmap)
	struct pmap *pmap;
{
	struct vm_page *pg;
	int refs;

	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_destroy(%p)\n", pmap));

	simple_lock(&pmap->pm_obj.vmobjlock);
	refs = --pmap->pm_obj.uo_refs;
	simple_unlock(&pmap->pm_obj.vmobjlock);

	if (refs > 0)
		return;

	TAILQ_FOREACH(pg, &pmap->pm_obj.memq, listq) {
#ifdef DIAGNOSTIC
		if (pg->flags & PG_BUSY)
			panic("pmap_destroy: busy page table page");
#endif
		pg->wire_count = 0;
		uvm_pagefree(pg);
	}

	uvm_pagefree(pmap->pm_pdir_pg);
	pmap->pm_pdir_pg = NULL;	/* XXX cache it? */
	pmap_sdir_set(pmap->pm_space, 0);
	pool_put(&pmap_pmap_pool, pmap);
}

/*
 * Add a reference to the specified pmap.
 */
void
pmap_reference(pmap)
	struct pmap *pmap;
{
	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_reference(%p)\n", pmap));

	simple_lock(&pmap->pm_obj.vmobjlock);
	pmap->pm_obj.uo_refs++;
	simple_unlock(&pmap->pm_obj.vmobjlock);
}

void
pmap_collect(struct pmap *pmap)
{
	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_collect(%p)\n", pmap));
	/* nothing yet */
}

int
pmap_enter(pmap, va, pa, prot, flags)
	struct pmap *pmap;
	vaddr_t va;
	paddr_t pa;
	vm_prot_t prot;
	int flags;
{
	pt_entry_t *pde, pte;
	struct vm_page *ptp = NULL;
	struct pv_head *pvh;
	struct pv_entry *pve;
	int bank, off;
	boolean_t wired = (flags & PMAP_WIRED) != 0;

	DPRINTF(PDB_FOLLOW|PDB_ENTER,
	    ("pmap_enter(%p, 0x%x, 0x%x, 0x%x, 0x%x)\n",
	    pmap, va, pa, prot, flags));

	simple_lock(&pmap->pm_obj.vmobjlock);

	if (!(pde = pmap_pde_get(pmap->pm_pdir, va)) &&
	    !(pde = pmap_pde_alloc(pmap, va, &ptp))) {
		if (flags & PMAP_CANFAIL)
			return (KERN_RESOURCE_SHORTAGE);

		panic("pmap_enter: cannot allocate pde");
	}

	if (!ptp)
		ptp = pmap_pde_ptp(pmap, pde);

	if ((pte = pmap_pte_get(pde, va))) {

		DPRINTF(PDB_ENTER,
		    ("pmap_enter: remapping 0x%x -> 0x%x\n", pte, pa));

		if (pte & PTE_PROT(TLB_EXECUTE))
			ficache(pmap->pm_space, va, PAGE_SIZE);
		pitlb(pmap->pm_space, va);
		fdcache(pmap->pm_space, va, PAGE_SIZE);
		pdtlb(pmap->pm_space, va);

		if (wired && !(pte & PTE_PROT(TLB_WIRED)) == 0)
			pmap->pm_stats.wired_count++;
		else if (!wired && (pte & PTE_PROT(TLB_WIRED)) != 0)
			pmap->pm_stats.wired_count--;

		if (PTE_PAGE(pte) == pa) {
			DPRINTF(PDB_FOLLOW|PDB_ENTER,
			    ("pmap_enter: same page\n"));
			goto enter;
		}

		bank = vm_physseg_find(atop(PTE_PAGE(pte)), &off);
		if (bank != -1) {
			pvh = &vm_physmem[bank].pmseg.pvhead[off];
			simple_lock(&pvh->pvh_lock);
			pve = pmap_pv_remove(pvh, pmap, va);
			pvh->pvh_attrs |= pmap_pvh_attrs(pte);
			simple_unlock(&pvh->pvh_lock);
		} else
			pve = NULL;
	} else {
		DPRINTF(PDB_ENTER,
		    ("pmap_enter: new mapping 0x%x -> 0x%x\n", va, pa));
		pte = PTE_PROT(0);
		pve = NULL;
		pmap->pm_stats.resident_count++;
		if (wired)
			pmap->pm_stats.wired_count++;
		if (ptp)
			ptp->wire_count++;
	}

	bank = vm_physseg_find(atop(pa), &off);
	if (pmap_initialized && bank != -1) {
		if (!pve && !(pve = pmap_pv_alloc())) {
			if (flags & PMAP_CANFAIL) {
				simple_unlock(&pmap->pm_obj.vmobjlock);
				return (KERN_RESOURCE_SHORTAGE);
			}
			panic("pmap_enter: no pv entries available");
		}
		pvh = &vm_physmem[bank].pmseg.pvhead[off];
		pmap_pv_enter(pvh, pve, pmap, va, ptp);
	} else {
		pvh = NULL;
		if (pve)
			pmap_pv_free(pve);
	}

enter:
	/* preserve old ref & mod */
	pte = pa | PTE_PROT(pmap_prot(pmap, prot)) |
	    (pte & PTE_PROT(TLB_UNCACHABLE|TLB_DIRTY|TLB_REFTRAP));
	if (wired)
		pte |= PTE_PROT(TLB_WIRED);
	pmap_pte_set(pde, va, pte);

	simple_unlock(&pmap->pm_obj.vmobjlock);

	DPRINTF(PDB_FOLLOW|PDB_ENTER, ("pmap_enter: leaving\n"));

	return (0);
}

void
pmap_remove(pmap, sva, eva)
	struct pmap *pmap;
	vaddr_t sva;
	vaddr_t eva;
{
	struct pv_head *pvh;
	struct pv_entry *pve;
	pt_entry_t *pde, pte;
	int bank, off, batch;
	u_int pdemask;

	DPRINTF(PDB_FOLLOW|PDB_REMOVE,
	    ("pmap_remove(%p, 0x%x, 0x%x\n", pmap, sva, eva));

	simple_lock(&pmap->pm_obj.vmobjlock);

	for (pdemask = sva + 1; sva < eva; sva += PAGE_SIZE) {
		if (pdemask != (sva & PDE_MASK)) {
			pdemask = sva & PDE_MASK;
			if (!(pde = pmap_pde_get(pmap->pm_pdir, sva))) {
				sva += ~PDE_MASK + 1 - PAGE_SIZE;
				continue;
			}
			batch = pdemask == sva && sva + ~PDE_MASK + 1 < eva;
		}

		if ((pte = pmap_pte_get(pde, sva))) {

			if (pte & PTE_PROT(TLB_WIRED))
				pmap->pm_stats.wired_count--;
			pmap->pm_stats.resident_count--;

			/* TODO measure here the speed tradeoff
			 * for flushing whole 4M vs per-page
			 * in case of non-complete pde fill
			 */
			if (pte & PTE_PROT(TLB_EXECUTE))
				ficache(pmap->pm_space, sva, PAGE_SIZE);
			pitlb(pmap->pm_space, sva);
			fdcache(pmap->pm_space, sva, PAGE_SIZE);
			pdtlb(pmap->pm_space, sva);

			/* iff properly accounted pde will be dropped anyway */
			if (!batch)
				pmap_pte_set(pde, sva, 0);

			bank = vm_physseg_find(atop(pte), &off);
			if (pmap_initialized && bank != -1) {
				pvh = &vm_physmem[bank].pmseg.pvhead[off];
				simple_lock(&pvh->pvh_lock);
				pvh->pvh_attrs |= pmap_pvh_attrs(pte);
				if ((pve = pmap_pv_remove(pvh, pmap, sva)))
					pmap_pv_free(pve);
				simple_unlock(&pvh->pvh_lock);
			}
		}
	}

	simple_unlock(&pmap->pm_obj.vmobjlock);

	DPRINTF(PDB_FOLLOW|PDB_REMOVE, ("pmap_remove: leaving\n"));
}

void
pmap_write_protect(pmap, sva, eva, prot)
	struct pmap *pmap;
	vaddr_t sva;
	vaddr_t eva;
	vm_prot_t prot;
{
	pt_entry_t *pde, pte;
	u_int tlbprot, pdemask;
	struct pv_head *pvh;
	int bank, off;

	DPRINTF(PDB_FOLLOW|PDB_PMAP,
	    ("pmap_write_protect(%p, %x, %x, %x)\n", pmap, sva, eva, prot));

	sva = hppa_trunc_page(sva);
	tlbprot = PTE_PROT(pmap_prot(pmap, prot));

	simple_lock(&pmap->pm_obj.vmobjlock);

	for(pdemask = sva + 1; sva < eva; sva += PAGE_SIZE) {
		if (pdemask != (sva & PDE_MASK)) {
			pdemask = sva & PDE_MASK;
			if (!(pde = pmap_pde_get(pmap->pm_pdir, sva))) {
				sva += ~PDE_MASK + 1 - PAGE_SIZE;
				continue;
			}
		}
		if ((pte = pmap_pte_get(pde, sva))) {

			DPRINTF(PDB_PMAP,
			    ("pmap_write_protect: va=0x%x pte=0x%x\n",
			    sva,  pte));
			/*
			 * Determine if mapping is changing.
			 * If not, nothing to do.
			 */
			if ((pte & PTE_PROT(TLB_AR_MASK)) == tlbprot)
				continue;

			bank = vm_physseg_find(atop(pte), &off);
			if (bank == -1) {
				printf("pmap_write_protect: unmanaged page?\n");
				return;
			}
			pvh = &vm_physmem[bank].pmseg.pvhead[off];
			simple_lock(&pvh->pvh_lock);
			pvh->pvh_attrs |= pmap_pvh_attrs(pte);
			simple_unlock(&pvh->pvh_lock);

			if (pte & PTE_PROT(TLB_EXECUTE))
				ficache(pmap->pm_space, sva, PAGE_SIZE);
			pitlb(pmap->pm_space, sva);
			fdcache(pmap->pm_space, sva, PAGE_SIZE);
			pdtlb(pmap->pm_space, sva);

			if (!(tlbprot & TLB_WRITE))
				pte &= ~PTE_PROT(TLB_DIRTY);
			pte &= ~PTE_PROT(TLB_AR_MASK);
			pte |= tlbprot;
			pmap_pte_set(pde, sva, pte);
		}
	}

	simple_unlock(&pmap->pm_obj.vmobjlock);
}

void
pmap_page_remove(pg)
	struct vm_page *pg;
{
	struct pv_head *pvh;
	struct pv_entry *pve, *ppve;
	pt_entry_t *pde, pte;
	int bank, off;

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_page_remove(%p)\n", pg));

	bank = vm_physseg_find(atop(VM_PAGE_TO_PHYS(pg)), &off);
	if (bank == -1) {
		printf("pmap_page_remove: unmanaged page?\n");
		return;
	}

	pvh = &vm_physmem[bank].pmseg.pvhead[off];
	if (pvh->pvh_list == NULL)
		return;

	simple_lock(&pvh->pvh_lock);

	for (pve = pvh->pvh_list; pve; ) {
		simple_lock(&pve->pv_pmap->pm_obj.vmobjlock);

		pde = pmap_pde_get(pve->pv_pmap->pm_pdir, pve->pv_va);
		pte = pmap_pte_get(pde, pve->pv_va);
		pmap_pte_set(pde, pve->pv_va, 0);

		if (pte & PTE_PROT(TLB_WIRED))
			pve->pv_pmap->pm_stats.wired_count--;
		pve->pv_pmap->pm_stats.resident_count--;

		simple_unlock(&pve->pmap->pm_obj.vmobjlock);

		pvh->pvh_attrs |= pmap_pvh_attrs(pte);
		ppve = pve;
		pve = pve->pv_next;
		pmap_pv_free(ppve);
	}
	pvh->pvh_list = NULL;
	simple_unlock(&pvh->pvh_lock);

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_page_remove: leaving\n"));

}

void
pmap_unwire(pmap, va)
	struct pmap *pmap;
	vaddr_t	va;
{
	pt_entry_t *pde, pte = 0;

	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_unwire(%p, 0x%x)\n", pmap, va));

	simple_lock(&pmap->pm_obj.vmobjlock);
	if ((pde = pmap_pde_get(pmap->pm_pdir, va))) {
		pte = pmap_pte_get(pde, va);

		if (pte & PTE_PROT(TLB_WIRED)) {
			pte &= ~PTE_PROT(TLB_WIRED);
			pmap->pm_stats.wired_count--;
			pmap_pte_set(pde, va, pte);
		}
	}
	simple_unlock(&pmap->pm_obj.vmobjlock);

	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_unwire: leaving\n"));

#ifdef DIAGNOSTIC
	if (!pte)
		panic("pmap_unwire: invalid va 0x%x", va);
#endif
}

boolean_t
pmap_changebit(struct vm_page *pg, u_int set, u_int clear)
{
	struct pv_head *pvh;
	struct pv_entry *pve;
	pt_entry_t *pde, pte, res;
	int bank, off;

	DPRINTF(PDB_FOLLOW|PDB_BITS,
	    ("pmap_changebit(%p, %x, %x)\n", pg, set, clear));

	bank = vm_physseg_find(atop(VM_PAGE_TO_PHYS(pg)), &off);
	if (bank == -1) {
		printf("pmap_changebit: unmanaged page?\n");
		return(FALSE);
	}

	pvh = &vm_physmem[bank].pmseg.pvhead[off];
	simple_lock(&pvh->pvh_lock);
	res = pvh->pvh_attrs = 0;
	for(pve = pvh->pvh_list; pve; pve = pve->pv_next) {
		simple_lock(&pve->pv_pmap->pm_obj.vmobjlock);
		if ((pde = pmap_pde_get(pve->pv_pmap->pm_pdir, pve->pv_va))) {
			pte = pmap_pte_get(pde, pve->pv_va);
#ifdef PMAPDEBUG
			if (!pte) {
				printf("pmap_changebit: zero pte for 0x%x\n",
				    pve->pv_va);
				continue;
			}
#endif
			if (pte & PTE_PROT(TLB_EXECUTE)) {
				ficache(pve->pv_pmap->pm_space,
				    pve->pv_va, PAGE_SIZE);
				pitlb(pve->pv_pmap->pm_space, pve->pv_va);
			}

			/* XXX flush only if there was mod ? */
			fdcache(pve->pv_pmap->pm_space, pve->pv_va, PAGE_SIZE);
			pdtlb(pve->pv_pmap->pm_space, pve->pv_va);

			res |= pmap_pvh_attrs(pte);
			pte &= ~clear;
			pte |= set;
			pvh->pvh_attrs = pmap_pvh_attrs(pte);

			pmap_pte_set(pde, pve->pv_va, pte);
		}
		simple_unlock(&pve->pv_pmap->pm_obj.vmobjlock);
	}
	simple_unlock(&pvh->pvh_lock);

	return ((res & clear) != 0);
}

boolean_t
pmap_testbit(struct vm_page *pg, u_int bits)
{
	struct pv_head *pvh;
	struct pv_entry *pve;
	pt_entry_t pte;
	int bank, off;

	DPRINTF(PDB_FOLLOW|PDB_BITS, ("pmap_testbit(%p, %x)\n", pg, bits));

	bank = vm_physseg_find(atop(VM_PAGE_TO_PHYS(pg)), &off);
	if (bank == -1) {
		printf("pmap_testbit: unmanaged page?\n");
		return(FALSE);
	}

	pvh = &vm_physmem[bank].pmseg.pvhead[off];
	simple_lock(&pvh->pvh_lock);
	for(pve = pvh->pvh_list; !(pvh->pvh_attrs & bits) && pve;
	    pve = pve->pv_next) {
		simple_lock(&pve->pv_pmap->pm_obj.vmobjlock);
		pte = pmap_vp_find(pve->pv_pmap, pve->pv_va);
		simple_unlock(&pve->pv_pmap->pm_obj.vmobjlock);
		pvh->pvh_attrs |= pmap_pvh_attrs(pte);
	}
	simple_unlock(&pvh->pvh_lock);

	return ((pvh->pvh_attrs & bits) != 0);
}

boolean_t
pmap_extract(pmap, va, pap)
	struct pmap *pmap;
	vaddr_t va;
	paddr_t *pap;
{
	pt_entry_t pte;

	DPRINTF(PDB_FOLLOW|PDB_EXTRACT, ("pmap_extract(%p, %x)\n", pmap, va));

	simple_lock(&pmap->pm_obj.vmobjlock);
	pte = pmap_vp_find(pmap, va);
	simple_unlock(&pmap->pm_obj.vmobjlock);

	if (pte) {
		if (pap)
			*pap = (pte & ~PGOFSET) | (va & PGOFSET);
		return (TRUE);
	}

	return (FALSE);
}

static __inline void
pmap_flush_page(paddr_t pa, int purge)
{
	struct pv_head *pvh;
	struct pv_entry *pve;
	int bank, off;

	bank = vm_physseg_find(atop(pa), &off);
#ifdef DIAGNOSTIC
	if (bank == -1)
		panic("pmap_flush_page: unmanaged page 0x%x", pa);
#endif
	/* purge all possible mappings for the pa */
	pvh = &vm_physmem[bank].pmseg.pvhead[off];
	simple_lock(&pvh->pvh_lock);
	for(pve = pvh->pvh_list; pve; pve = pve->pv_next)
		if (purge)
			pdcache(pve->pv_pmap->pm_space, pve->pv_va, PAGE_SIZE);
		else
			fdcache(pve->pv_pmap->pm_space, pve->pv_va, PAGE_SIZE);
	simple_unlock(&pvh->pvh_lock);
}

void
pmap_zero_page(pa)
	paddr_t pa;
{
	DPRINTF(PDB_FOLLOW|PDB_PHYS, ("pmap_zero_page(%x)\n", pa));

	pmap_flush_page(pa, 1);
	bzero((void *)pa, PAGE_SIZE);
	fdcache(HPPA_SID_KERNEL, pa, PAGE_SIZE);
}

void
pmap_copy_page(spa, dpa)
	paddr_t spa;
	paddr_t dpa;
{
	DPRINTF(PDB_FOLLOW|PDB_PHYS, ("pmap_copy_page(%x, %x)\n", spa, dpa));

	pmap_flush_page(spa, 0);
	pmap_flush_page(dpa, 1);
	bcopy((void *)spa, (void *)dpa, PAGE_SIZE);
	pdcache(HPPA_SID_KERNEL, spa, PAGE_SIZE);
	fdcache(HPPA_SID_KERNEL, dpa, PAGE_SIZE);
}

void
pmap_kenter_pa(va, pa, prot)
	vaddr_t va;
	paddr_t pa;
	vm_prot_t prot;
{
	pt_entry_t *pde, pte;

	DPRINTF(PDB_FOLLOW|PDB_ENTER,
	    ("pmap_kenter_pa(%x, %x, %x)\n", va, pa, prot));

	simple_lock(&pmap->pm_obj.vmobjlock);

	if (!(pde = pmap_pde_get(pmap_kernel()->pm_pdir, va)) &&
	    !(pde = pmap_pde_alloc(pmap_kernel(), va, NULL)))
		panic("pmap_kenter_pa: cannot allocate pde for va=0x%x", va);
#ifdef DIAGNOSTIC
	if ((pte = pmap_pte_get(pde, va)))
		panic("pmap_kenter_pa: 0x%x is already mapped %p:0x%x",
		    va, pde, pte);
#endif

	pte = pa | PTE_PROT(TLB_WIRED|pmap_prot(pmap_kernel(), prot));
	if (pa >= HPPA_IOSPACE)
		pte |= PTE_PROT(TLB_UNCACHABLE);
	pmap_pte_set(pde, va, pte);

	simple_unlock(&pmap->pm_obj.vmobjlock);

#ifdef PMAPDEBUG
	{
		struct pv_head *pvh;
		int bank, off;
		bank = vm_physseg_find(atop(pte), &off);
		if (pmap_initialized && bank != -1) {
			pvh = &vm_physmem[bank].pmseg.pvhead[off];
			simple_lock(&pvh->pvh_lock);
			if (pmap_check_alias(pvh->pvh_list, va))
				Debugger();
			simple_unlock(&pvh->pvh_lock);
		}
	}
#endif
	DPRINTF(PDB_FOLLOW|PDB_ENTER, ("pmap_kenter_pa: leaving\n"));
}

void
pmap_kremove(va, size)
	vaddr_t va;
	vsize_t size;
{
#ifdef PMAPDEBUG
	extern u_int totalphysmem;
#endif
	struct pv_entry *pve;
	struct pv_head *pvh;
	vaddr_t eva = va + size, pdemask;
	pt_entry_t *pde, pte;
	int bank, off;

	DPRINTF(PDB_FOLLOW|PDB_REMOVE,
	    ("pmap_kremove(%x, %x)\n", va, size));
#ifdef PMAPDEBUG
	if (va < ptoa(totalphysmem)) {
		printf("pmap_kremove(%x, %x): unmapping physmem\n", va, size);
		return;
	}
#endif

	simple_lock(&pmap->pm_obj.vmobjlock);

	for (pdemask = va + 1; va < eva; va += PAGE_SIZE) {
		if (pdemask != (va & PDE_MASK)) {
			pdemask = va & PDE_MASK;
			if (!(pde = pmap_pde_get(pmap_kernel()->pm_pdir, va))) {
				va += ~PDE_MASK + 1 - PAGE_SIZE;
				continue;
			}
		}
		if (!(pte = pmap_pte_get(pde, va))) {
#ifdef DEBUG
			printf("pmap_kremove: unmapping unmapped 0x%x\n", va);
#endif
			continue;
		}

		if (pte & PTE_PROT(TLB_EXECUTE))
			ficache(HPPA_SID_KERNEL, va, PAGE_SIZE);
		pitlb(HPPA_SID_KERNEL, va);
		fdcache(HPPA_SID_KERNEL, va, PAGE_SIZE);
		pdtlb(HPPA_SID_KERNEL, va);

		pmap_pte_set(pde, va, 0);
		bank = vm_physseg_find(atop(pte), &off);
		if (pmap_initialized && bank != -1) {
			pvh = &vm_physmem[bank].pmseg.pvhead[off];
			simple_lock(&pvh->pvh_lock);
			pvh->pvh_attrs |= pmap_pvh_attrs(pte);
			/* just in case we have enter/kenter mismatch */
			if ((pve = pmap_pv_remove(pvh, pmap_kernel(), va)))
				pmap_pv_free(pve);
			simple_unlock(&pvh->pvh_lock);
		}
	}

	simple_unlock(&pmap->pm_obj.vmobjlock);

	DPRINTF(PDB_FOLLOW|PDB_REMOVE, ("pmap_kremove: leaving\n"));
}
