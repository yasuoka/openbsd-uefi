/*	$OpenBSD: fpu.c,v 1.4 2004/02/28 20:33:33 nordin Exp $	*/
/*	$NetBSD: fpu.c,v 1.1 2003/04/26 18:39:28 fvdl Exp $	*/

/*-
 * Copyright (c) 1994, 1995, 1998 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1990 William Jolitz.
 * Copyright (c) 1991 The Regents of the University of California.
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
 *	@(#)npx.c	7.2 (Berkeley) 5/12/91
 */

/*
 * XXXfvdl update copyright notice. this started out as a stripped isa/npx.c
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/vmmeter.h>
#include <sys/signalvar.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/cpufunc.h>
#include <machine/pcb.h>
#include <machine/trap.h>
#include <machine/specialreg.h>
#include <machine/fpu.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

/*
 * We do lazy initialization and switching using the TS bit in cr0 and the
 * MDP_USEDFPU bit in mdproc.
 *
 * DNA exceptions are handled like this:
 *
 * 1) If there is no FPU, return and go to the emulator.
 * 2) If someone else has used the FPU, save its state into that process' PCB.
 * 3a) If MDP_USEDFPU is not set, set it and initialize the FPU.
 * 3b) Otherwise, reload the process' previous FPU state.
 *
 * When a process is created or exec()s, its saved cr0 image has the TS bit
 * set and the MDP_USEDFPU bit clear.  The MDP_USEDFPU bit is set when the
 * process first gets a DNA and the FPU is initialized.  The TS bit is turned
 * off when the FPU is used, and turned on again later when the process' FPU
 * state is saved.
 */

#define	fninit()		__asm("fninit")
#define fwait()			__asm("fwait")
#define	fxsave(addr)		__asm("fxsave %0" : "=m" (*addr))
#define	fxrstor(addr)		__asm("fxrstor %0" : : "m" (*addr))
#define	ldmxcsr(addr)		__asm("ldmxcsr %0" : "=m" (*addr))
#define fldcw(addr)		__asm("fldcw %0" : : "m" (*addr))
#define	clts()			__asm("clts")
#define	stts()			lcr0(rcr0() | CR0_TS)

void fpudna(struct cpu_info *);

/*
 * Init the FPU.
 */
void
fpuinit(struct cpu_info *ci)
{
	lcr0(rcr0() & ~(CR0_EM|CR0_TS));
	fninit();
	lcr0(rcr0() | (CR0_TS));
}

/*
 * Record the FPU state and reinitialize it all except for the control word.
 * Then generate a SIGFPE.
 *
 * Reinitializing the state allows naive SIGFPE handlers to longjmp without
 * doing any fixups.
 */

void
fputrap(struct trapframe *frame)
{
	struct proc *p = curcpu()->ci_fpcurproc;
	struct savefpu *sfp = &p->p_addr->u_pcb.pcb_savefpu;
	u_int16_t cw;
	union sigval sv;

#ifdef DIAGNOSTIC
	/*
	 * At this point, fpcurproc should be curproc.  If it wasn't,
	 * the TS bit should be set, and we should have gotten a DNA exception.
	 */
	if (p != curproc)
		panic("fputrap: wrong proc");
#endif

	fxsave(sfp);
	if (frame->tf_trapno == T_XMM) {
	} else {
		fninit();
		fwait();
		cw = sfp->fp_fxsave.fx_fcw;
		fldcw(&cw);
		fwait();
	}
	sfp->fp_ex_tw = sfp->fp_fxsave.fx_ftw;
	sfp->fp_ex_sw = sfp->fp_fxsave.fx_fsw;
	sv.sival_ptr = (void *)frame->tf_rip;	/* XXX - ? */
	KERNEL_PROC_LOCK(p);
	trapsignal(p, SIGFPE, frame->tf_err, 0 /* XXX */, sv);
	KERNEL_PROC_UNLOCK(p);
}

/*
 * Implement device not available (DNA) exception
 *
 * If we were the last process to use the FPU, we can simply return.
 * Otherwise, we save the previous state, if necessary, and restore our last
 * saved state.
 */
void
fpudna(struct cpu_info *ci)
{
	struct proc *p;
	int s;

	if (ci->ci_fpsaving) {
		printf("recursive fpu trap; cr0=%x\n", rcr0());
		return;
	}

	s = splipi();

#ifdef MULTIPROCESSOR
	p = ci->ci_curproc;
#else
	p = curproc;
#endif

	/*
	 * Initialize the FPU state to clear any exceptions.  If someone else
	 * was using the FPU, save their state.
	 */
	if (ci->ci_fpcurproc != NULL && ci->ci_fpcurproc != p) 
		fpusave_cpu(ci, 1);
	else {
		clts();
		fninit();
		fwait();
		stts();
	}
	splx(s);

	if (p == NULL) {
		clts();
		return;
	}

	KDASSERT(ci->ci_fpcurproc == NULL);
#ifndef MULTIPROCESSOR
	KDASSERT(p->p_addr->u_pcb.pcb_fpcpu == NULL);
#else
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p, 1);
#endif

	p->p_addr->u_pcb.pcb_cr0 &= ~CR0_TS;
	clts();

	s = splipi();
	ci->ci_fpcurproc = p;
	p->p_addr->u_pcb.pcb_fpcpu = ci;
	splx(s);

	if ((p->p_md.md_flags & MDP_USEDFPU) == 0) {
		fldcw(&p->p_addr->u_pcb.pcb_savefpu.fp_fxsave.fx_fcw);
		ldmxcsr(&p->p_addr->u_pcb.pcb_savefpu.fp_fxsave.fx_mxcsr);
		p->p_md.md_flags |= MDP_USEDFPU;
	} else
		fxrstor(&p->p_addr->u_pcb.pcb_savefpu);
}


void
fpusave_cpu(struct cpu_info *ci, int save)
{
	struct proc *p;
	int s;

	KDASSERT(ci == curcpu());

	p = ci->ci_fpcurproc;
	if (p == NULL)
		return;

	if (save) {
#ifdef DIAGNOSTIC
		if (ci->ci_fpsaving != 0)
			panic("fpusave_cpu: recursive save!");
#endif
		 /*
		  * Set ci->ci_fpsaving, so that any pending exception will be
		  * thrown away.  (It will be caught again if/when the FPU
		  * state is restored.)
		  */
		clts();
		ci->ci_fpsaving = 1;
		fxsave(&p->p_addr->u_pcb.pcb_savefpu);
		ci->ci_fpsaving = 0;
	}

	stts();
	p->p_addr->u_pcb.pcb_cr0 |= CR0_TS;

	s = splipi();
	p->p_addr->u_pcb.pcb_fpcpu = NULL;
	ci->ci_fpcurproc = NULL;
	splx(s);
}

/*
 * Save p's FPU state, which may be on this processor or another processor.
 */
void
fpusave_proc(struct proc *p, int save)
{
	struct cpu_info *ci = curcpu();
	struct cpu_info *oci;

	KDASSERT(p->p_addr != NULL);
	KDASSERT(p->p_flag & P_INMEM);

	oci = p->p_addr->u_pcb.pcb_fpcpu;
	if (oci == NULL)
		return;

#if defined(MULTIPROCESSOR)
	if (oci == ci) {
		int s = splipi();
		fpusave_cpu(ci, save);
		splx(s);
	} else {
#ifdef DIAGNOSTIC
		int spincount;
#endif

		x86_send_ipi(oci,
		    save ? X86_IPI_SYNCH_FPU : X86_IPI_FLUSH_FPU);

#ifdef DIAGNOSTIC
		spincount = 0;
#endif
		while (p->dpl_addr->u_pcb.pcb_fpcpu != NULL)
#ifdef DIAGNOSTIC
		{
			spincount++;
			if (spincount > 10000000) {
				panic("fp_save ipi didn't");
			}
		}
#else
		__splbarrier();		/* XXX replace by generic barrier */
		;
#endif
	}
#else
	KASSERT(ci->ci_fpcurproc == p);
	fpusave_cpu(ci, save);
#endif
}
