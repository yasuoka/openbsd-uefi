/*	$OpenBSD: genassym.c,v 1.3 1996/04/28 10:57:44 deraadt Exp $ */

/*
 * Copyright (c) 1995 Theo de Raadt
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
 *	This product includes software developed under OpenBSD by
 *	Theo de Raadt for Willowglen Singapore.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 1982, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)genassym.c	8.3 (Berkeley) 1/4/94
 */

/* XXXX */
#define _VA_LIST_ _BSD_VA_LIST_
#define _PTRDIFF_T_ _BSD_PTRDIFF_T_

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/map.h>
#include <sys/proc.h>
#include <sys/mbuf.h>
#include <sys/msgbuf.h>
#include <sys/syscall.h>
#include <sys/user.h>

#include <machine/cpu.h>
#include <machine/trap.h>
#include <machine/psl.h>
#include <machine/nvram.h>
#include <machine/reg.h>
#include <machine/autoconf.h>
#include <machine/prom.h>
#include <machine/pte.h>
#include <vm/vm.h>

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

extern int errno;

#if defined(MVME162) || defined(MVME167)
#ifndef M68040
#error	"MVME162/MVME167 requires M68040 support"
#endif
#endif

#if defined(MVME147)
#ifndef M68030
#error	"MVME147 requires M68030 support"
#endif
#endif

#if defined(MVME172) || defined(MVME177)
#ifndef M68060
#error	"MVME172/MVME177 requires M68060 support"
#endif
#endif

void
def(what, val)
	char *what;
	int val;
{

	if (printf("#define\t%s\t%d\n", what, val) < 0) {
		(void)fprintf(stderr, "genassym: printf: %s\n",
		    strerror(errno));
		exit(1);
	}
}

void
flush()
{

	if (fflush(stdout) || fsync(fileno(stdout)) < 0) {
		(void)fprintf(stderr, "genassym: flush stdout: %s\n",
		    strerror(errno));
		exit(1);
	}
}

#define	off(what, s, m)	def(what, (int)offsetof(s, m))

main()
{
	/* general constants */
	def("UPAGES", UPAGES);
	def("USPACE", USPACE);
	def("NBPG", NBPG);
	def("PGSHIFT", PGSHIFT);
	def("USRSTACK", USRSTACK);

	/* proc fields and values */
	off("P_FORW", struct proc, p_forw);
	off("P_BACK", struct proc, p_back);
	off("P_VMSPACE", struct proc, p_vmspace);
	off("P_ADDR", struct proc, p_addr);
	off("P_PRIORITY", struct proc, p_priority);
	off("P_STAT", struct proc, p_stat);
	off("P_WCHAN", struct proc, p_wchan);
	off("P_FLAG", struct proc, p_flag);
	off("P_MD_FLAGS", struct proc, p_md.md_flags);
	off("P_MD_REGS", struct proc, p_md.md_regs);

	def("SSLEEP", SSLEEP);
	def("SRUN", SRUN);

	/* VM structure fields */
	off("VM_PMAP", struct vmspace, vm_pmap);
	off("PM_STCHG", struct pmap, pm_stchanged);

	/* interrupt/fault metering */
	off("V_SWTCH", struct vmmeter, v_swtch);
	off("V_INTR", struct vmmeter, v_intr);

	/* trap types (should just include trap.h?) */
	def("T_BUSERR", T_BUSERR);
	def("T_ADDRERR", T_ADDRERR);
	def("T_ILLINST", T_ILLINST);
	def("T_ZERODIV", T_ZERODIV);
	def("T_CHKINST", T_CHKINST);
	def("T_TRAPVINST", T_TRAPVINST);
	def("T_PRIVINST", T_PRIVINST);
	def("T_TRACE", T_TRACE);
	def("T_MMUFLT", T_MMUFLT);
	def("T_SSIR", T_SSIR);
	def("T_FMTERR", T_FMTERR);
	def("T_COPERR", T_COPERR);
	def("T_FPERR", T_FPERR);
	def("T_ASTFLT", T_ASTFLT);
	def("T_TRAP15", T_TRAP15);
	def("T_FPEMULI", T_FPEMULI);
	def("T_FPEMULD", T_FPEMULD);

	/* PSL values (should just include psl.h?) */
	def("PSL_S", PSL_S);
	def("PSL_IPL7", PSL_IPL7);
	def("PSL_LOWIPL", PSL_LOWIPL);
	def("PSL_HIGHIPL", PSL_HIGHIPL);
	def("PSL_USER", PSL_USER);
	def("SPL1", PSL_S | PSL_IPL1);
	def("SPL2", PSL_S | PSL_IPL2);
	def("SPL3", PSL_S | PSL_IPL3);
	def("SPL4", PSL_S | PSL_IPL4);
	def("SPL5", PSL_S | PSL_IPL5);
	def("SPL6", PSL_S | PSL_IPL6);

	/* magic */
	def("FC_USERD", FC_USERD);
	def("EIOMAPSIZE", EIOMAPSIZE);
	def("CACHE_ON", CACHE_ON);
	def("CACHE_OFF", CACHE_OFF);
	def("CACHE_CLR", CACHE_CLR);
	def("IC_CLEAR", IC_CLEAR);
	def("DC_CLEAR", DC_CLEAR);

	/* pte/ste bits */
	def("PG_V", PG_V);
	def("PG_NV", PG_NV);
	def("PG_RO", PG_RO);
	def("PG_RW", PG_RW);
	def("PG_CI", PG_CI);
	def("PG_PROT", PG_PROT);
	def("PG_FRAME", PG_FRAME);
	def("SG_V", SG_V);
	def("SG_NV", SG_NV);
	def("SG_RW", SG_RW);
	def("SG_FRAME", SG_FRAME);
	def("SG_ISHIFT", SG_ISHIFT);

	/* pcb fields */
	off("PCB_PS", struct pcb, pcb_ps);
	off("PCB_USTP", struct pcb, pcb_ustp);
	off("PCB_USP", struct pcb, pcb_usp);
	off("PCB_REGS", struct pcb, pcb_regs);
	off("PCB_ONFAULT", struct pcb, pcb_onfault);
	off("PCB_FPCTX", struct pcb, pcb_fpregs);
	def("SIZEOF_PCB", sizeof(struct pcb));
	def("SIZEOF_TRAPFRAME", sizeof(struct trapframe));

	/* exception frame offset/sizes */
	off("FR_SP", struct frame, f_regs[15]);
	off("FR_HW", struct frame, f_sr);
	off("FR_ADJ", struct frame, f_stackadj);

	/* system calls */
	def("SYS_exit", SYS_exit);
	def("SYS_execve", SYS_execve);
	def("SYS_sigreturn", SYS_sigreturn);

	/* errno */
	def("EFAULT", EFAULT);
	def("ENAMETOOLONG", ENAMETOOLONG);

	def("SIZEOF_MVMEPROM_BRDID", sizeof(struct mvmeprom_brdid));
	off("MVMEPROM_BRDID_MODEL", struct mvmeprom_brdid, model);

	off("NVRAM_147_ETHER", struct nvram_147, ether);
	off("NVRAM_147_EMEM", struct nvram_147, emem);

	off("NVRAM_16X_ETHER", struct nvram_16x, conf.ether);

	def("INTIOBASE_147", INTIOBASE_147);
	def("INTIOBASE_162", INTIOBASE_162);
	def("INTIOSIZE_147", INTIOSIZE_147);
	def("INTIOSIZE_162", INTIOSIZE_162);

	def("CPU_147", CPU_147);
	def("CPU_162", CPU_162);
	def("CPU_166", CPU_166);
	def("CPU_167", CPU_167);
	def("CPU_172", CPU_172);
	def("CPU_177", CPU_177);

	def("MMU_68851", MMU_68851);
	def("MMU_68030", MMU_68030);
	def("MMU_68040", MMU_68040);

	off("NETCTRL_DEV", struct prom_netctrl, dev);
	off("NETCTRL_CTRL", struct prom_netctrl, ctrl);
	off("NETCTRL_CMD", struct prom_netctrl, cmd);
	off("NETCTRL_ADDR", struct prom_netctrl, addr);
	off("NETCTRL_LEN", struct prom_netctrl, len);

	exit(0);
}
