/*	$OpenBSD: vm_machdep.c,v 1.45 2006/06/23 13:46:05 mickey Exp $	*/
/*	$NetBSD: vm_machdep.c,v 1.61 1996/05/03 19:42:35 christos Exp $	*/

/*-
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1982, 1986 The Regents of the University of California.
 * Copyright (c) 1989, 1990 William Jolitz
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department, and William Jolitz.
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
 *	@(#)vm_machdep.c	7.3 (Berkeley) 5/13/91
 */

/*
 *	Utah $Hdr: vm_machdep.c 1.16.1.1 89/06/23$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/user.h>
#include <sys/core.h>
#include <sys/exec.h>
#include <sys/ptrace.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/gdt.h>
#include <machine/reg.h>
#include <machine/specialreg.h>

#include "npx.h"

/*
 * Finish a fork operation, with process p2 nearly set up.
 * Copy and update the kernel stack and pcb, making the child
 * ready to run, and marking it so that it can return differently
 * than the parent.  Returns 1 in the child process, 0 in the parent.
 * We currently double-map the user area so that the stack is at the same
 * address in each process; in the future we will probably relocate
 * the frame pointers on the stack after copying.
 */
void
cpu_fork(p1, p2, stack, stacksize, func, arg)
	struct proc *p1, *p2;
	void *stack;
	size_t stacksize;
	void (*func)(void *);
	void *arg;
{
	struct pcb *pcb = &p2->p_addr->u_pcb;
	struct trapframe *tf;
	struct switchframe *sf;

#if NNPX > 0
	npxsave_proc(p1, 1);
#endif

	p2->p_md.md_flags = p1->p_md.md_flags;

	/* Copy pcb from proc p1 to p2. */
	if (p1 == curproc) {
		/* Sync the PCB before we copy it. */
		savectx(curpcb);
	}
#ifdef DIAGNOSTIC
	else if (p1 != &proc0)
		panic("cpu_fork: curproc");
#endif
	*pcb = p1->p_addr->u_pcb;

	/*
	 * Preset these so that gdt_compact() doesn't get confused if called
	 * during the allocations below.
	 *
	 * Note: pcb_ldt_sel is handled in the pmap_activate() call when
	 * we run the new process.
	 */
	p2->p_md.md_tss_sel = GSEL(GNULL_SEL, SEL_KPL);

	/* Fix up the TSS. */
	pcb->pcb_tss.tss_ss0 = GSEL(GDATA_SEL, SEL_KPL);
	pcb->pcb_tss.tss_esp0 = (int)p2->p_addr + USPACE - 16;

	p2->p_md.md_tss_sel = tss_alloc(pcb);

	/*
	 * Copy the trapframe, and arrange for the child to return directly
	 * through rei().
	 */
	p2->p_md.md_regs = tf = (struct trapframe *)pcb->pcb_tss.tss_esp0 - 1;
	*tf = *p1->p_md.md_regs;

	/*
	 * If specified, give the child a different stack.
	 */
	if (stack != NULL)
		tf->tf_esp = (u_int)stack + stacksize;

	sf = (struct switchframe *)tf - 1;
	sf->sf_ppl = 0;
	sf->sf_esi = (int)func;
	sf->sf_ebx = (int)arg;
	sf->sf_eip = (int)proc_trampoline;
	pcb->pcb_esp = (int)sf;
}

void
cpu_swapout(p)
	struct proc *p;
{

#if NNPX > 0
	/*
	 * Make sure we save the FP state before the user area vanishes.
	 */
	npxsave_proc(p, 1);
#endif
}

/*
 * cpu_exit is called as the last action during exit.
 *
 * We clean up a little and then call switch_exit() with the old proc as an
 * argument.  switch_exit() first switches to proc0's context, then does the
 * vmspace_free() and kmem_free() that we don't do here, and finally jumps
 * into switch() to wait for another process to wake up.
 */
void
cpu_exit(p)
	register struct proc *p;
{
#if NNPX > 0
	/* If we were using the FPU, forget about it. */
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		npxsave_proc(p, 0);
#endif

	switch_exit(p);
}

void
cpu_wait(p)
	struct proc *p;
{
	tss_free(p->p_md.md_tss_sel);
}

/*
 * Dump the machine specific segment at the start of a core dump.
 */
struct md_core {
	struct reg intreg;
	struct fpreg freg;
};

int
cpu_coredump(p, vp, cred, chdr)
	struct proc *p;
	struct vnode *vp;
	struct ucred *cred;
	struct core *chdr;
{
	struct md_core md_core;
	struct coreseg cseg;
	int error;

	CORE_SETMAGIC(*chdr, COREMAGIC, MID_I386, 0);
	chdr->c_hdrsize = ALIGN(sizeof(*chdr));
	chdr->c_seghdrsize = ALIGN(sizeof(cseg));
	chdr->c_cpusize = sizeof(md_core);

	/* Save integer registers. */
	error = process_read_regs(p, &md_core.intreg);
	if (error)
		return error;

	/* Save floating point registers. */
	error = process_read_fpregs(p, &md_core.freg);
	if (error)
		return error;

	CORE_SETMAGIC(cseg, CORESEGMAGIC, MID_I386, CORE_CPU);
	cseg.c_addr = 0;
	cseg.c_size = chdr->c_cpusize;

	error = vn_rdwr(UIO_WRITE, vp, (caddr_t)&cseg, chdr->c_seghdrsize,
	    (off_t)chdr->c_hdrsize, UIO_SYSSPACE, IO_NODELOCKED|IO_UNIT, cred,
	    NULL, p);
	if (error)
		return error;

	error = vn_rdwr(UIO_WRITE, vp, (caddr_t)&md_core, sizeof(md_core),
	    (off_t)(chdr->c_hdrsize + chdr->c_seghdrsize), UIO_SYSSPACE,
	    IO_NODELOCKED|IO_UNIT, cred, NULL, p);
	if (error)
		return error;

	chdr->c_nseg++;
	return 0;
}

/*
 * Move pages from one kernel virtual address to another.
 * Both addresses are assumed to reside in the Sysmap.
 */
void
pagemove(from, to, size)
	caddr_t from, to;
	size_t size;
{
	u_int32_t ofpte, otpte;
#ifdef MULTIPROCESSOR
	u_int32_t cpumask = 0;
#endif

#ifdef DIAGNOSTIC
	if ((size & PAGE_MASK) != 0)
		panic("pagemove");
#endif
	while (size > 0) {
		ofpte = pmap_pte_bits((vaddr_t)from);
		otpte = pmap_pte_bits((vaddr_t)to);
		pmap_pte_set((vaddr_t)to,
		    pmap_pte_paddr((vaddr_t)from), ofpte);
		pmap_pte_set((vaddr_t)from, 0, 0);
#if defined(I386_CPU) && !defined(MULTIPROCESSOR)
		if (cpu_class != CPUCLASS_386)
#endif
		{
			if (otpte & PG_V)
#ifdef MULTIPROCESSOR
				pmap_tlb_shootdown(pmap_kernel(), (vaddr_t)to,
				    otpte, &cpumask);
#else
				pmap_update_pg((vaddr_t)to);
#endif
			if (ofpte & PG_V)
#ifdef MULTIPROCESSOR
				pmap_tlb_shootdown(pmap_kernel(),
				    (vaddr_t)from, ofpte, &cpumask);
#else
				pmap_update_pg((vaddr_t)from);
#endif
		}

		from += PAGE_SIZE;
		to += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
#ifdef MULTIPROCESSOR
	pmap_tlb_shootnow(cpumask);
#else
#if defined(I386_CPU)
	if (cpu_class == CPUCLASS_386)
		tlbflush();		
#endif
#endif
}

/*
 * Convert kernel VA to physical address
 */
int
kvtop(caddr_t addr)
{
	paddr_t pa;

	if (pmap_extract(pmap_kernel(), (vaddr_t)addr, &pa) == FALSE)
		panic("kvtop: zero page frame");
	return((int)pa);
}

/*
 * Map an IO request into kernel virtual address space.  Requests fall into
 * one of five catagories:
 *
 *	B_PHYS|B_UAREA:	User u-area swap.
 *			Address is relative to start of u-area (p_addr).
 *	B_PHYS|B_PAGET:	User page table swap.
 *			Address is a kernel VA in usrpt (Usrptmap).
 *	B_PHYS|B_DIRTY:	Dirty page push.
 *			Address is a VA in proc2's address space.
 *	B_PHYS|B_PGIN:	Kernel pagein of user pages.
 *			Address is VA in user's address space.
 *	B_PHYS:		User "raw" IO request.
 *			Address is VA in user's address space.
 *
 * All requests are (re)mapped into kernel VA space via the useriomap
 * (a name with only slightly more meaning than "kernelmap")
 */
void
vmapbuf(struct buf *bp, vsize_t len)
{
	vaddr_t faddr, taddr, off;
	paddr_t fpa;

	if ((bp->b_flags & B_PHYS) == 0)
		panic("vmapbuf");
	faddr = trunc_page((vaddr_t)(bp->b_saveaddr = bp->b_data));
	off = (vaddr_t)bp->b_data - faddr;
	len = round_page(off + len);
	taddr= uvm_km_valloc_wait(phys_map, len);
	bp->b_data = (caddr_t)(taddr + off);
	/*
	 * The region is locked, so we expect that pmap_pte() will return
	 * non-NULL.
	 * XXX: unwise to expect this in a multithreaded environment.
	 * anything can happen to a pmap between the time we lock a
	 * region, release the pmap lock, and then relock it for
	 * the pmap_extract().
	 *
	 * no need to flush TLB since we expect nothing to be mapped
	 * where we we just allocated (TLB will be flushed when our
	 * mapping is removed).
	 */
	while (len) {
		pmap_extract(vm_map_pmap(&bp->b_proc->p_vmspace->vm_map),
		    faddr, &fpa);
		pmap_kenter_pa(taddr, fpa, VM_PROT_READ|VM_PROT_WRITE);
		faddr += PAGE_SIZE;
		taddr += PAGE_SIZE;
		len -= PAGE_SIZE;
	}
	pmap_update(pmap_kernel());
}

/*
 * Free the io map PTEs associated with this IO operation.
 * We also invalidate the TLB entries and restore the original b_addr.
 */
void
vunmapbuf(struct buf *bp, vsize_t len)
{
	vaddr_t addr, off;

	if ((bp->b_flags & B_PHYS) == 0)
		panic("vunmapbuf");
	addr = trunc_page((vaddr_t)bp->b_data);
	off = (vaddr_t)bp->b_data - addr;
	len = round_page(off + len);
	pmap_kremove(addr, len);
	pmap_update(pmap_kernel());
	uvm_km_free_wakeup(phys_map, addr, len);
	bp->b_data = bp->b_saveaddr;
	bp->b_saveaddr = 0;
}
