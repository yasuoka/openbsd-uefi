/*	$OpenBSD: process_machdep.c,v 1.10 2004/04/07 18:24:19 mickey Exp $	*/

/*
 * Copyright (c) 1999-2004 Michael Shalayeff
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/user.h>

#include <machine/cpufunc.h>

int
process_read_regs(p, regs)
	struct proc *p;
	struct reg *regs;
{
	struct trapframe *tf = p->p_md.md_regs;

	regs->r_regs[ 0] = tf->tf_sar;
	regs->r_regs[ 1] = tf->tf_r1;
	regs->r_regs[ 2] = tf->tf_rp;
	regs->r_regs[ 3] = tf->tf_r3;
	regs->r_regs[ 4] = tf->tf_r4;
	regs->r_regs[ 5] = tf->tf_r5;
	regs->r_regs[ 6] = tf->tf_r6;
	regs->r_regs[ 7] = tf->tf_r7;
	regs->r_regs[ 8] = tf->tf_r8;
	regs->r_regs[ 9] = tf->tf_r9;
	regs->r_regs[10] = tf->tf_r10;
	regs->r_regs[11] = tf->tf_r11;
	regs->r_regs[12] = tf->tf_r12;
	regs->r_regs[13] = tf->tf_r13;
	regs->r_regs[14] = tf->tf_r14;
	regs->r_regs[15] = tf->tf_r15;
	regs->r_regs[16] = tf->tf_r16;
	regs->r_regs[17] = tf->tf_r17;
	regs->r_regs[18] = tf->tf_r18;
	regs->r_regs[19] = tf->tf_t4;
	regs->r_regs[20] = tf->tf_t3;
	regs->r_regs[21] = tf->tf_t2;
	regs->r_regs[22] = tf->tf_t1;
	regs->r_regs[23] = tf->tf_arg3;
	regs->r_regs[24] = tf->tf_arg2;
	regs->r_regs[25] = tf->tf_arg1;
	regs->r_regs[26] = tf->tf_arg0;
	regs->r_regs[27] = tf->tf_dp;
	regs->r_regs[28] = tf->tf_ret0;
	regs->r_regs[29] = tf->tf_ret1;
	regs->r_regs[30] = tf->tf_sp;
	regs->r_regs[31] = tf->tf_r31;
	regs->r_pc	 = tf->tf_iioq_head;
	regs->r_npc	 = tf->tf_iioq_tail;

	return (0);
}

int
process_read_fpregs(p, fpregs)
	struct proc *p;
	struct fpreg *fpregs;
{
	extern paddr_t fpu_curpcb;

	if (p->p_md.md_regs->tf_cr30 == fpu_curpcb)
		fpu_save((vaddr_t)p->p_addr->u_pcb.pcb_fpregs);
	bcopy(p->p_addr->u_pcb.pcb_fpregs, fpregs, 32*8);

	return (0);
}

#ifdef PTRACE

int
process_write_regs(p, regs)
	struct proc *p;
	struct reg *regs;
{
	struct trapframe *tf = p->p_md.md_regs;

	tf->tf_sar  = regs->r_regs[ 0];
	tf->tf_r1   = regs->r_regs[ 1];
	tf->tf_rp   = regs->r_regs[ 2];
	tf->tf_r3   = regs->r_regs[ 3];
	tf->tf_r4   = regs->r_regs[ 4];
	tf->tf_r5   = regs->r_regs[ 5];
	tf->tf_r6   = regs->r_regs[ 6];
	tf->tf_r7   = regs->r_regs[ 7];
	tf->tf_r8   = regs->r_regs[ 8];
	tf->tf_r9   = regs->r_regs[ 9];
	tf->tf_r10  = regs->r_regs[10];
	tf->tf_r11  = regs->r_regs[11];
	tf->tf_r12  = regs->r_regs[12];
	tf->tf_r13  = regs->r_regs[13];
	tf->tf_r14  = regs->r_regs[14];
	tf->tf_r15  = regs->r_regs[15];
	tf->tf_r16  = regs->r_regs[16];
	tf->tf_r17  = regs->r_regs[17];
	tf->tf_r18  = regs->r_regs[18];
	tf->tf_t4   = regs->r_regs[19];
	tf->tf_t3   = regs->r_regs[20];
	tf->tf_t2   = regs->r_regs[21];
	tf->tf_t1   = regs->r_regs[22];
	tf->tf_arg3 = regs->r_regs[23];
	tf->tf_arg2 = regs->r_regs[24];
	tf->tf_arg1 = regs->r_regs[25];
	tf->tf_arg0 = regs->r_regs[26];
	tf->tf_dp   = regs->r_regs[27];
	tf->tf_ret0 = regs->r_regs[28];
	tf->tf_ret1 = regs->r_regs[29];
	tf->tf_sp   = regs->r_regs[30];
	tf->tf_r31  = regs->r_regs[31];
	tf->tf_iioq_head = regs->r_pc | 3;
	tf->tf_iioq_tail = regs->r_npc | 3;

	return (0);
}

int
process_write_fpregs(p, fpregs)
	struct proc *p;
	struct fpreg *fpregs;
{
	extern paddr_t fpu_curpcb;

	bcopy(fpregs, p->p_addr->u_pcb.pcb_fpregs, 32 * 8);

	if (p->p_md.md_regs->tf_cr30 == fpu_curpcb) {
		mtctl(0, CR_CCR);
		fpu_curpcb = 0;
	}

	return (0);
}

int
process_sstep(p, sstep)
	struct proc *p;
	int sstep;
{
	if (sstep)
		return (EINVAL);

	return (0);
}

int
process_set_pc(p, addr)
	struct proc *p;
	caddr_t addr;
{
	p->p_md.md_regs->tf_iioq_tail = 4 +
	    (p->p_md.md_regs->tf_iioq_head = (register_t)addr | 3);

	return (0);
}

#endif	/* PTRACE */
