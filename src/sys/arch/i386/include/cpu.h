/*	$OpenBSD: cpu.h,v 1.58 2004/02/14 15:09:22 grange Exp $	*/
/*	$NetBSD: cpu.h,v 1.35 1996/05/05 19:29:26 christos Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	@(#)cpu.h	5.4 (Berkeley) 5/9/91
 */

#ifndef _I386_CPU_H_
#define _I386_CPU_H_

/*
 * Definitions unique to i386 cpu support.
 */
#include <machine/psl.h>
#include <machine/frame.h>
#include <machine/segments.h>

/*
 * definitions of cpu-dependent requirements
 * referenced in generic code
 */
#define	cpu_swapin(p)			/* nothing */

/*
 * Arguments to hardclock, softclock and statclock
 * encapsulate the previous machine state in an opaque
 * clockframe; for now, use generic intrframe.
 *
 * XXX intrframe has a lot of gunk we don't need.
 */
#define clockframe intrframe

#define	CLKF_USERMODE(frame)	USERMODE((frame)->if_cs, (frame)->if_eflags)
#define	CLKF_PC(frame)		((frame)->if_eip)
#define	CLKF_INTR(frame)	(IDXSEL((frame)->if_cs) == GICODE_SEL)

/*
 * Preempt the current process if in interrupt from user mode,
 * or after the current trap/syscall if in system mode.
 */
int	want_resched;		/* resched() was called */
#define	need_resched()		(want_resched = 1, setsoftast())

/*
 * Give a profiling tick to the current process when the user profiling
 * buffer pages are invalid.  On the i386, request an ast to send us
 * through trap(), marking the proc as needing a profiling tick.
 */
#define	need_proftick(p)	((p)->p_flag |= P_OWEUPC, setsoftast())

/*
 * Notify the current process (p) that it has a signal pending,
 * process as soon as possible.
 */
#define	signotify(p)		setsoftast()

/*
 * We need a machine-independent name for this.
 */
#define	DELAY(x)		delay(x)
void	delay(int);

#if defined(I586_CPU) || defined(I686_CPU)
/*
 * High resolution clock support (Pentium only)
 */
void	calibrate_cyclecounter(void);
#ifndef	HZ
extern u_quad_t pentium_base_tsc;
#define CPU_CLOCKUPDATE()						\
	do {								\
		if (pentium_mhz) {					\
			__asm __volatile("cli\n"			\
					 "rdtsc\n"			\
					 "sti\n"			\
					 : "=A" (pentium_base_tsc)	\
					 : );				\
		}							\
	} while (0)
#endif
#endif

/*
 * pull in #defines for kinds of processors
 */
#include <machine/cputypes.h>

struct cpu_nocpuid_nameclass {
	int cpu_vendor;
	const char *cpu_vendorname;
	const char *cpu_name;
	int cpu_class;
	void (*cpu_setup)(const char *, int, int);
};

struct cpu_cpuid_nameclass {
	const char *cpu_id;
	int cpu_vendor;
	const char *cpu_vendorname;
	struct cpu_cpuid_family {
		int cpu_class;
		const char *cpu_models[CPU_MAXMODEL+2];
		void (*cpu_setup)(const char *, int, int);
	} cpu_family[CPU_MAXFAMILY - CPU_MINFAMILY + 1];
};

struct cpu_cpuid_feature {
	int feature_bit;
	const char *feature_name;
};

#ifdef _KERNEL
extern int cpu;
extern int cpu_class;
extern int cpu_feature;
extern int cpu_ecxfeature;
extern int cpu_apmwarn;
extern int cpu_apmhalt;
extern int cpuid_level;
extern const struct cpu_nocpuid_nameclass i386_nocpuid_cpus[];
extern const struct cpu_cpuid_nameclass i386_cpuid_cpus[];

#if defined(I586_CPU) || defined(I686_CPU)
extern int pentium_mhz;
#endif

#ifdef I586_CPU
/* F00F bug fix stuff for pentium cpu */
extern int cpu_f00f_bug;
void fix_f00f(void);
#endif

/* dkcsum.c */
void	dkcsumattach(void);

extern int i386_use_fxsave;
extern int i386_has_sse;
extern int i386_has_sse2;

/* machdep.c */
void	dumpconf(void);
void	cpu_reset(void);
void	i386_proc0_tss_ldt_init(void);
void	cpuid(u_int32_t, u_int32_t *);

/* locore.s */
struct region_descriptor;
void	lgdt(struct region_descriptor *);
void	fillw(short, void *, size_t);

struct pcb;
void	savectx(struct pcb *);
void	switch_exit(struct proc *);
void	proc_trampoline(void);

/* clock.c */
void	initrtclock(void);
void	startrtclock(void);
void	rtcdrain(void *);

/* est.c */
#if !defined(SMALL_KERNEL) && defined(I686_CPU)
void	est_init(const char *);
int     est_cpuspeed(int *);
int     est_setperf(int);
#endif

/* longrun.c */
#if !defined(SMALL_KERNEL) && defined(I586_CPU)
void	longrun_init(void);
int	longrun_cpuspeed(int *);
int	longrun_setperf(int);
#endif

/* p4tcc.c */
#if !defined(SMALL_KERNEL) && defined(I686_CPU)
void	p4tcc_init(int, int);
int     p4tcc_setperf(int);
#endif

/* npx.c */
void	npxdrop(void);
void	npxsave(void);

#if defined(GPL_MATH_EMULATE)
/* math_emulate.c */
int	math_emulate(struct trapframe *);
#endif

#ifdef USER_LDT
/* sys_machdep.h */
extern int user_ldt_enable;
int	i386_get_ldt(struct proc *, void *, register_t *);
int	i386_set_ldt(struct proc *, void *, register_t *);
#endif

/* isa_machdep.c */
void	isa_defaultirq(void);
int	isa_nmi(void);

/* pmap.c */
void	pmap_bootstrap(vaddr_t);

/* vm_machdep.c */
int	kvtop(caddr_t);

#ifdef VM86
/* vm86.c */
void	vm86_gpfault(struct proc *, int);
#endif /* VM86 */

#ifdef GENERIC
/* swapgeneric.c */
void	setconf(void);
#endif /* GENERIC */

#endif /* _KERNEL */

/* 
 * CTL_MACHDEP definitions.
 */
#define	CPU_CONSDEV		1	/* dev_t: console terminal device */
#define	CPU_BIOS		2	/* BIOS variables */
#define	CPU_BLK2CHR		3	/* convert blk maj into chr one */
#define	CPU_CHR2BLK		4	/* convert chr maj into blk one */
#define CPU_ALLOWAPERTURE	5	/* allow mmap of /dev/xf86 */
#define CPU_CPUVENDOR		6	/* cpuid vendor string */
#define CPU_CPUID		7	/* cpuid */
#define CPU_CPUFEATURE		8	/* cpuid features */
#define CPU_APMWARN		9	/* APM battery warning percentage */
#define CPU_KBDRESET		10	/* keyboard reset under pcvt */
#define CPU_APMHALT		11	/* halt -p hack */
#define CPU_USERLDT		12
#define CPU_OSFXSR		13	/* uses FXSAVE/FXRSTOR */
#define CPU_SSE			14	/* supports SSE */
#define CPU_SSE2		15	/* supports SSE2 */
#define CPU_XCRYPT		16	/* supports VIA xcrypt in userland */
#define CPU_MAXID		17	/* number of valid machdep ids */

#define	CTL_MACHDEP_NAMES { \
	{ 0, 0 }, \
	{ "console_device", CTLTYPE_STRUCT }, \
	{ "bios", CTLTYPE_INT }, \
	{ "blk2chr", CTLTYPE_STRUCT }, \
	{ "chr2blk", CTLTYPE_STRUCT }, \
	{ "allowaperture", CTLTYPE_INT }, \
	{ "cpuvendor", CTLTYPE_STRING }, \
	{ "cpuid", CTLTYPE_INT }, \
	{ "cpufeature", CTLTYPE_INT }, \
	{ "apmwarn", CTLTYPE_INT }, \
	{ "kbdreset", CTLTYPE_INT }, \
	{ "apmhalt", CTLTYPE_INT }, \
	{ "userldt", CTLTYPE_INT }, \
	{ "osfxsr", CTLTYPE_INT }, \
	{ "sse", CTLTYPE_INT }, \
	{ "sse2", CTLTYPE_INT }, \
	{ "xcrypt", CTLTYPE_INT }, \
}

#endif /* !_I386_CPU_H_ */
