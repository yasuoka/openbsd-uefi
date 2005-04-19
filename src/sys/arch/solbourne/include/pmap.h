/*	$OpenBSD: pmap.h,v 1.1 2005/04/19 21:30:18 miod Exp $	*/
/*
 * Copyright (c) 2005, Miodrag Vallat
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_SOLBOURNE_PMAP_H_
#define _SOLBOURNE_PMAP_H_

#include <machine/pte.h>

/*
 * PMAP structure
 */
struct pmap {
	pd_entry_t		*pm_segtab;	/* first level table */
	paddr_t			pm_psegtab;	/* pa of above */

	int			pm_refcount;	/* reference count */
	struct simplelock	pm_lock;
	struct pmap_statistics	pm_stats;	/* pmap statistics */
};

typedef struct pmap *pmap_t;

/*
 * Extra constants passed in the low bits of pa in pmap_enter() to
 * request specific memory attributes.
 */

#define	PMAP_NC		1
#define	PMAP_OBIO	PMAP_NC
#define	PMAP_BWS	2

/*
 * Macro to pass iospace bits in the low bits of pa in pmap_enter().
 * Provided for source code compatibility - we don't need such bits.
 */

#define	PMAP_IOENC(x)	0

#ifdef _KERNEL

extern struct pmap kernel_pmap_store;

#define	kvm_recache(addr, npages) 	kvm_setcache(addr, npages, 1)
#define	kvm_uncache(addr, npages) 	kvm_setcache(addr, npages, 0)
#define	pmap_copy(a,b,c,d,e)		do { /* nothing */ } while (0)
#define	pmap_deactivate(p)		do { /* nothing */ } while (0)
#define	pmap_kernel()			(&kernel_pmap_store)
#define	pmap_phys_address(frame)	ptoa(frame)
#define	pmap_resident_count(p)		((p)->pm_stats.resident_count)
#define	pmap_update(p)			do { /* nothing */ } while (0)
#define	pmap_wired_count(p)		((p)->pm_stats.wired_count)

#define PMAP_PREFER(fo, ap)		pmap_prefer((fo), (ap))

struct proc;
void		kvm_setcache(caddr_t, int, int);
void		switchexit(struct proc *);		/* locore.s */
void		pmap_activate(struct proc *);
void		pmap_bootstrap(size_t);
void		pmap_cache_enable(void);
void		pmap_changeprot(pmap_t, vaddr_t, vm_prot_t, int);
boolean_t	pmap_clear_modify(struct vm_page *);
boolean_t	pmap_clear_reference(struct vm_page *);
void		pmap_copy_page(struct vm_page *, struct vm_page *);
pmap_t		pmap_create(void);
void		pmap_destroy(pmap_t);
int		pmap_enter(pmap_t, vaddr_t, paddr_t, vm_prot_t, int);
boolean_t	pmap_extract(pmap_t, vaddr_t, paddr_t *);
void		pmap_init(void);
boolean_t	pmap_is_modified(struct vm_page *);
boolean_t	pmap_is_referenced(struct vm_page *);
void		pmap_kenter_pa(vaddr_t, paddr_t, vm_prot_t);
void		pmap_kremove(vaddr_t, vsize_t);
vaddr_t		pmap_map(vaddr_t, paddr_t, paddr_t, int);
int		pmap_pa_exists(paddr_t);
void		pmap_page_protect(struct vm_page *, vm_prot_t);
void		pmap_prefer(vaddr_t, vaddr_t *);
void		pmap_proc_iflush(struct proc *, vaddr_t, vsize_t);
void		pmap_protect(pmap_t, vaddr_t, vaddr_t, vm_prot_t);
void		pmap_reference(pmap_t);
void		pmap_release(pmap_t);
void		pmap_redzone(void);
void		pmap_remove(pmap_t, vaddr_t, vaddr_t);
void		pmap_unwire(pmap_t, vaddr_t);
void		pmap_virtual_space(vaddr_t *, vaddr_t *);
void		pmap_writetext(unsigned char *, int);
void		pmap_zero_page(struct vm_page *);

#endif /* _KERNEL */

#endif /* _SOLBOURNE_PMAP_H_ */
