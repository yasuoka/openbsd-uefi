/* $OpenBSD: machdep.c,v 1.258 2013/11/02 23:10:29 miod Exp $	*/
/*
 * Copyright (c) 1998, 1999, 2000, 2001 Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
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
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 *
 */
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/msgbuf.h>
#include <sys/syscallargs.h>
#include <sys/exec.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/extent.h>
#include <sys/core.h>
#include <sys/kcore.h>

#include <uvm/uvm.h>

#include <machine/asm.h>
#include <machine/asm_macro.h>
#include <machine/board.h>
#include <machine/bug.h>
#include <machine/bugio.h>
#include <machine/cmmu.h>
#include <machine/cpu.h>
#include <machine/kcore.h>
#include <machine/reg.h>
#ifdef M88100
#include <machine/m88100.h>
#endif
#ifdef MVME197
#include <machine/m88410.h>
#endif

#include <mvme88k/mvme88k/clockvar.h>

#include <dev/cons.h>

#include <net/if.h>

#include "ksyms.h"
#if DDB
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#include <ddb/db_var.h>
#endif /* DDB */

/*
 * Dummy routines and data to be able to handle unexpected situations and
 * at least report them, until enough of the kernel is initialized.
 */
static u_int
dummy_func(void)
{
	return 0;
}
static const struct board dummy_board = {
    .getipl = (u_int (*)(void))dummy_func,
    .setipl = (u_int (*)(u_int))dummy_func,
    .raiseipl = (u_int (*)(u_int))dummy_func
};
#ifdef MULTIPROCESSOR
struct cpu_info dummy_cpu = {
	.ci_flags = CIF_ALIVE | CIF_PRIMARY,
	.ci_cpuid = 0,
	.ci_mp_atomic_begin =
	    (uint32_t (*)(__cpu_simple_lock_t *, uint*))dummy_func,
	.ci_mp_atomic_end =
	    (void (*)(uint32_t, __cpu_simple_lock_t *, uint))dummy_func
};
#endif

void	consinit(void);
void	cpu_hatch_secondary_processors(void *);
void	dumpconf(void);
void	dumpsys(void);
void	identifycpu(void);
void	mvme_bootstrap(void);
void	mvme88k_vector_init(uint32_t *, uint32_t *);
void	myetheraddr(u_char *);
void	savectx(struct pcb *);
void	secondary_main(void);
vaddr_t	secondary_pre_main(void);
void	_doboot(void);

extern int kernelstart;
register_t kernel_vbr;
intrhand_t intr_handlers[NVMEINTR];

const struct board *platform = &dummy_board;

int physmem;	  /* available physical memory, in pages */

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

#ifdef MULTIPROCESSOR
__cpu_simple_lock_t cpu_hatch_mutex;
__cpu_simple_lock_t cpu_boot_mutex = __SIMPLELOCK_LOCKED;
#endif

/*
 * 32 or 34 bit physical address bus depending upon the CPU flavor.
 * 32 bit DMA.
 */
struct uvm_constraint_range  dma_constraint = { 0x0, 0xffffffffUL};
struct uvm_constraint_range *uvm_md_constraints[] = { NULL };

/*
 * Info for CTL_HW
 */
char  machine[] = MACHINE;	 /* cpu "architecture" */
char  cpu_model[120];

int bootdev;					/* set in locore.S */
int cputyp;					/* set in locore.S */
int brdtyp;					/* set in locore.S */
int cpuspeed = 25;				/* safe guess */
u_int dumb_delay_const = 25;

vaddr_t first_addr;
vaddr_t last_addr;

extern struct user *proc0paddr;

struct intrhand	clock_ih;
struct intrhand	statclock_ih;

/*
 * Statistics clock interval and variance, in usec.  Variance must be a
 * power of two.  Since this gives us an even number, not an odd number,
 * we discard one case and compensate.  That is, a variance of 4096 would
 * give us offsets in [0..4095].  Instead, we take offsets in [1..4095].
 * This is symmetric about the point 2048, or statvar/2, and thus averages
 * to that value (assuming uniform random numbers).
 */
int statvar = 8192;
int statmin;			/* statclock interval - 1/2*variance */

#if defined (MVME187) || defined (MVME197)
#define ETHERPAGES 16
void *etherbuf = NULL;
int etherlen;
#endif

#if defined(MVME181) || defined(MVME188)
/*
 * Interrupt masks, one per IPL level.
 */
u_int32_t int_mask_val[NIPLS];
#endif

/*
 * This is to fake out the console routines, while booting.
 */
cons_decl(boot);
#define bootcnpollc nullcnpollc

struct consdev bootcons = {
	NULL,
	NULL,
	bootcngetc,
	bootcnputc,
	bootcnpollc,
	NULL,
	makedev(14, 0),
	CN_LOWPRI,
};

/*
 * Early console initialization: called early on from main, before vm init.
 * We want to stick to the BUG routines for now, and we'll switch to the
 * real console in cpu_startup().
 */
void
consinit()
{
	cn_tab = &bootcons;

#if defined(DDB)
	db_machine_init();
	ddb_init();
	if (boothowto & RB_KDB)
		Debugger();
#endif
}

void
identifycpu()
{
	struct mvmeprom_brdid brdid;
	char suffix[4];
	u_int i;

	bzero(&brdid, sizeof(brdid));
	bugbrdid(&brdid);

	cpuspeed = platform->cpuspeed(&brdid);

	i = 0;
	if (brdid.suffix[0] >= ' ' && brdid.suffix[0] < 0x7f) {
		if (brdid.suffix[0] != '-')
			suffix[i++] = '-';
		suffix[i++] = brdid.suffix[0];
	}
	if (brdid.suffix[1] >= ' ' && brdid.suffix[1] < 0x7f)
		suffix[i++] = brdid.suffix[1];
	suffix[i++] = '\0';

	snprintf(cpu_model, sizeof cpu_model,
	    "Motorola MVME%x%s, %dMHz", brdtyp, suffix, cpuspeed);
}

void
cpu_initclocks()
{
	platform->init_clocks();
}

void
setstatclockrate(int newhz)
{
	/* function stub */
}

void
cpu_startup()
{
	int i;
	vaddr_t minaddr, maxaddr;

	/*
	 * Good {morning,afternoon,evening,night}.
	 */
	printf(version);
	identifycpu();
	printf("real mem = %u (%uMB)\n", ptoa(physmem),
	    ptoa(physmem)/1024/1024);

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    16 * NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate map for physio.
	 */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_PHYS_SIZE, 0, FALSE, NULL);

	printf("avail mem = %lu (%luMB)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free)/1024/1024);

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();

	/*
	 * Set up interrupt handlers.
	 */
	for (i = 0; i < NVMEINTR; i++)
		SLIST_INIT(&intr_handlers[i]);

	/*
	 * Configure the system.
	 */
	if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support -c; continuing..\n");
#endif
	}
}

__dead void
_doboot()
{
	cold = 0;
	cmmu_shutdown();
	set_vbr(0);		/* restore BUG VBR */
	bugreturn();
	/*NOTREACHED*/
	for (;;);		/* appease gcc */
}

__dead void
boot(howto)
	int howto;
{
	/* take a snapshot before clobbering any registers */
	if (curproc && curproc->p_addr)
		savectx(curpcb);

	/* If system is cold, just halt. */
	if (cold) {
		/* (Unless the user explicitly asked for reboot.) */
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

	boothowto = howto;
	if ((howto & RB_NOSYNC) == 0) {
		vfs_shutdown();
		/*
		 * If we've been adjusting the clock, the todr
		 * will be out of synch; adjust it now unless
		 * the system was sitting in ddb.
		 */
		if ((howto & RB_TIMEBAD) == 0)
			resettodr();
		else
			printf("WARNING: not updating battery clock\n");
	}
	if_downall();

	uvm_shutdown();
	splhigh();		/* Disable interrupts. */

	/* If rebooting and a dump is requested, do it. */
	if (howto & RB_DUMP)
		dumpsys();

haltsys:
	doshutdownhooks();
	if (!TAILQ_EMPTY(&alldevs))
		config_suspend(TAILQ_FIRST(&alldevs), DVACT_POWERDOWN);

	if (howto & RB_HALT) {
		printf("System halted. Press any key to reboot...\n\n");
		cnpollc(1);
		cngetc();
		cnpollc(0);
	}

	if (platform->reboot != NULL)
		platform->reboot(howto);

	doboot();	/* will invoke _doboot on a 1:1 mapped stack */
	/*NOTREACHED*/
}

unsigned dumpmag = 0x8fca0101;	 /* magic number for savecore */
int   dumpsize = 0;	/* also for savecore */
long  dumplo = 0;
cpu_kcore_hdr_t cpu_kcore_hdr;

/*
 * This is called by configure to set dumplo and dumpsize.
 * Dumps always skip the first PAGE_SIZE of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void
dumpconf(void)
{
	int nblks;	/* size of dump area */

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	dumpsize = physmem;

	/* mvme88k only uses a single segment. */
	cpu_kcore_hdr.ram_segs[0].start = 0;
	cpu_kcore_hdr.ram_segs[0].size = ptoa(physmem);
	cpu_kcore_hdr.cputype = cputyp;

	/*
	 * Don't dump on the first block
	 * in case the dump device includes a disk label.
	 */
	if (dumplo < ctod(1))
		dumplo = ctod(1);

	/* Put dump at end of partition, and make it fit. */
	if (dumpsize + 1 > dtoc(nblks - dumplo))
		dumpsize = dtoc(nblks - dumplo) - 1;
	if (dumplo < nblks - ctod(dumpsize) - 1)
		dumplo = nblks - ctod(dumpsize) - 1;
}

/*
 * Doadump comes here after turning off memory management and
 * getting on the dump stack, either when called above, or by
 * the auto-restart code.
 */
void
dumpsys()
{
	int maj;
	int psize;
	daddr_t blkno;	/* current block to write */
				/* dump routine */
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	int pg;			/* page being dumped */
	paddr_t maddr;		/* PA being dumped */
	int error;		/* error code from (*dump)() */
	kcore_seg_t *kseg_p;
	cpu_kcore_hdr_t *chdr_p;
	char dump_hdr[dbtob(1)];	/* XXX assume hdr fits in 1 block */

	extern int msgbufmapped;

	msgbufmapped = 0;

	/* Make sure dump device is valid. */
	if (dumpdev == NODEV)
		return;
	if (dumpsize == 0) {
		dumpconf();
		if (dumpsize == 0)
			return;
	}
	maj = major(dumpdev);
	if (dumplo < 0) {
		printf("\ndump to dev %u,%u not possible\n", maj,
		    minor(dumpdev));
		return;
	}
	dump = bdevsw[maj].d_dump;
	blkno = dumplo;

	printf("\ndumping to dev %u,%u offset %ld\n", maj,
	    minor(dumpdev), dumplo);

#ifdef UVM_SWAP_ENCRYPT
	uvm_swap_finicrypt_all();
#endif

	/* Setup the dump header */
	kseg_p = (kcore_seg_t *)dump_hdr;
	chdr_p = (cpu_kcore_hdr_t *)&dump_hdr[ALIGN(sizeof(*kseg_p))];
	bzero(dump_hdr, sizeof(dump_hdr));

	CORE_SETMAGIC(*kseg_p, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	kseg_p->c_size = dbtob(1) - ALIGN(sizeof(*kseg_p));
	*chdr_p = cpu_kcore_hdr;

	printf("dump ");
	psize = (*bdevsw[maj].d_psize)(dumpdev);
	if (psize == -1) {
		printf("area unavailable\n");
		return;
	}

	/* Dump the header. */
	error = (*dump)(dumpdev, blkno++, (caddr_t)dump_hdr, dbtob(1));
	if (error != 0)
		goto abort;

	maddr = (paddr_t)0;
	for (pg = 0; pg < dumpsize; pg++) {
#define NPGMB	(1024 * 1024 / PAGE_SIZE)
		/* print out how many MBs we have dumped */
		if (pg != 0 && (pg % NPGMB) == 0)
			printf("%d ", pg / NPGMB);
#undef NPGMB
		error = (*dump)(dumpdev, blkno, (caddr_t)maddr, PAGE_SIZE);
		if (error == 0) {
			maddr += PAGE_SIZE;
			blkno += btodb(PAGE_SIZE);
		} else
			break;
	}
abort:
	switch (error) {
	case 0:
		printf("succeeded\n");
		break;

	case ENXIO:
		printf("device bad\n");
		break;

	case EFAULT:
		printf("device not ready\n");
		break;

	case EINVAL:
		printf("area improper\n");
		break;

	case EIO:
		printf("i/o error\n");
		break;

	case EINTR:
		printf("aborted from console\n");
		break;

	default:
		printf("error %d\n", error);
		break;
	}
}

#ifdef MULTIPROCESSOR

/*
 * Secondary CPU early initialization routine.
 * Determine CPU number and set it, then allocate the startup stack.
 *
 * Running on a minimal stack here, with interrupts disabled; do nothing fancy.
 */
vaddr_t
secondary_pre_main()
{
	struct cpu_info *ci;
	vaddr_t init_stack;

	/*
	 * Invoke the CMMU initialization routine as early as possible,
	 * so that we do not risk any memory writes to be lost during
	 * cache setup.
	 */
	cmmu_initialize_cpu(cmmu_cpu_number());

	/*
	 * Now initialize your cpu_info structure.
	 */
	set_cpu_number(cmmu_cpu_number());
	ci = curcpu();
	ci->ci_curproc = &proc0;
	platform->smp_setup(ci);

	splhigh();

	/*
	 * Enable MMU on this processor.
	 */
	pmap_bootstrap_cpu(ci->ci_cpuid);

	/*
	 * Allocate UPAGES contiguous pages for the startup stack.
	 */
	init_stack = uvm_km_zalloc(kernel_map, USPACE);
	if (init_stack == (vaddr_t)NULL) {
		printf("cpu%d: unable to allocate startup stack\n",
		    ci->ci_cpuid);
		__cpu_simple_unlock(&cpu_hatch_mutex);
		for (;;) ;
	}

	return (init_stack);
}

/*
 * Further secondary CPU initialization.
 *
 * We are now running on our startup stack, with proper page tables.
 * There is nothing to do but display some details about the CPU and its CMMUs.
 */
void
secondary_main()
{
	struct cpu_info *ci = curcpu();
	int s;

	cpu_configuration_print(0);
	ncpus++;

	sched_init_cpu(ci);
	nanouptime(&ci->ci_schedstate.spc_runtime);
	ci->ci_curproc = NULL;
	ci->ci_randseed = random();

	__cpu_simple_unlock(&cpu_hatch_mutex);

	/* wait for cpu_boot_secondary_processors() */
	__cpu_simple_lock(&cpu_boot_mutex);
	__cpu_simple_unlock(&cpu_boot_mutex);

	spl0();
	SCHED_LOCK(s);
	set_psr(get_psr() & ~PSR_IND);

	SET(ci->ci_flags, CIF_ALIVE);

	cpu_switchto(NULL, sched_chooseproc());
}

#endif	/* MULTIPROCESSOR */

/*
 * Search for the first available interrupt vector in the range start, end.
 * This should really only be used by VME devices.
 */
int
intr_findvec(int start, int end, int skip)
{
	int vec;

#ifdef DEBUG
	if (start < 0 || end >= NVMEINTR || start > end)
		panic("intr_findvec(%d,%d): bad parameters", start, end);
#endif

	for (vec = start; vec <= end; vec++) {
		if (vec == skip)
			continue;
		if (SLIST_EMPTY(&intr_handlers[vec]))
			return vec;
	}
#ifdef DIAGNOSTIC
	printf("intr_findvec(%d,%d,%d): no vector available\n",
	    start, end, skip);
#endif
	return -1;
}

/*
 * Try to insert ih in the list of handlers for vector vec.
 */
int
intr_establish(int vec, struct intrhand *ih, const char *name)
{
	struct intrhand *intr;
	intrhand_t *list;

	list = &intr_handlers[vec];
	if (!SLIST_EMPTY(list)) {
		intr = SLIST_FIRST(list);
		if (intr->ih_ipl != ih->ih_ipl) {
#ifdef DIAGNOSTIC
			panic("intr_establish: there are other handlers with "
			    "vec (0x%x) at ipl %x, but you want it at %x",
			    vec, intr->ih_ipl, ih->ih_ipl);
#endif /* DIAGNOSTIC */
			return EINVAL;
		}
	}

	evcount_attach(&ih->ih_count, name, &ih->ih_ipl);
	SLIST_INSERT_HEAD(list, ih, ih_link);

	return 0;
}

void
nmihand(void *frame)
{
#ifdef DDB
	printf("Abort switch pressed\n");
	if (db_console) {
		/*
		 * We can't use Debugger() here, as we are coming from an
		 * exception handler, and can't assume anything about the
		 * state we are in. Invoke the post-trap ddb entry directly.
		 */
		extern void m88k_db_trap(int, struct trapframe *);
		m88k_db_trap(T_KDB_ENTRY, (struct trapframe *)frame);
	}
#endif
}

int
sys_sysarch(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
#if 0
	struct sys_sysarch_args	/* {
	   syscallarg(int) op;
	   syscallarg(char *) parm;
	} */ *uap = v;
#endif

	return (ENOSYS);
}

/*
 * machine dependent system variables.
 */

int
cpu_sysctl(name, namelen, oldp, oldlenp, newp, newlen, p)
	int *name;
	u_int namelen;
	void *oldp;
	size_t *oldlenp;
	void *newp;
	size_t newlen;
	struct proc *p;
{
	dev_t consdev;

	/* all sysctl names are this level are terminal */
	if (namelen != 1)
		return (ENOTDIR); /* overloaded */

	switch (name[0]) {
	case CPU_CONSDEV:
		if (cn_tab != NULL)
			consdev = cn_tab->cn_dev;
		else
			consdev = NODEV;
		return (sysctl_rdstruct(oldp, oldlenp, newp, &consdev,
		    sizeof consdev));
	case CPU_CPUTYPE:
		return (sysctl_rdint(oldp, oldlenp, newp, cputyp));
	default:
		return (EOPNOTSUPP);
	}
	/*NOTREACHED*/
}

void
myetheraddr(cp)
	u_char *cp;
{
	struct mvmeprom_brdid brdid;

	bugbrdid(&brdid);
	bcopy(&brdid.etheraddr, cp, 6);
}

void
mvme88k_vector_init(uint32_t *bugvbr, uint32_t *vectors)
{
	extern vaddr_t vector_init(uint32_t *, uint32_t *, int); /* gross */
	unsigned long bugvec[32];
	uint i;

	/*
	 * Set up bootstrap vectors, overwriting the existing BUG vbr
	 * page. This allows us to keep the BUG system call vectors.
	 */

	for (i = 0; i < 16 * 2; i++)
		bugvec[i] = bugvbr[MVMEPROM_VECTOR * 2 + i];
	vector_init(bugvbr, vectors, 1);
	for (i = 0; i < 16 * 2; i++)
		bugvbr[MVMEPROM_VECTOR * 2 + i] = bugvec[i];

	/*
	 * Set up final vectors.
	 */

	kernel_vbr = trunc_page((vaddr_t)&kernelstart);
	vector_init((uint32_t *)kernel_vbr, vectors, 0);
}

/*
 * Called from locore.S during boot,
 * this is the first C code that's run.
 */
void
mvme_bootstrap()
{
	extern struct consdev *cn_tab;
	struct mvmeprom_brdid brdid;
	extern vaddr_t avail_start;
	extern vaddr_t avail_end;
#ifndef MULTIPROCESSOR
	cpuid_t master_cpu;
#endif

	buginit();
	bugbrdid(&brdid);
	brdtyp = brdid.model;

	/*
	 * Use the BUG as console for now. After autoconf, we'll switch to
	 * real hardware.
	 */
	cn_tab = &bootcons;

	/*
	 * Set up interrupt and fp exception handlers based on the machine.
	 */
	switch (brdtyp) {
#ifdef MVME181
	case BRD_180:
	case BRD_181:
		platform = &board_mvme181;
		break;
#endif
#ifdef MVME187
	case BRD_187:
	case BRD_8120:
		platform = &board_mvme187;
		break;
#endif
#ifdef MVME188
	case BRD_188:
		platform = &board_mvme188;
		break;
#endif
#ifdef MVME197
	case BRD_197:
		if (mc88410_present())
			platform = &board_mvme197spdp;
		else
			platform = &board_mvme197le;
		break;
#endif
	default:
		panic("Sorry, this kernel does not support MVME%x", brdtyp);
	}

	platform->bootstrap();

	uvmexp.pagesize = PAGE_SIZE;
	uvm_setpagesize();
	first_addr = round_page(first_addr);
	last_addr = platform->memsize();
	physmem = atop(last_addr);

	cmmu = platform->cmmu;
	setup_board_config();
	master_cpu = cmmu_init();
	set_cpu_number(master_cpu);
#ifdef MULTIPROCESSOR
	platform->smp_setup(curcpu());
#endif
	SET(curcpu()->ci_flags, CIF_ALIVE | CIF_PRIMARY);

#ifdef M88100
	if (CPU_IS88100) {
		m88100_apply_patches();
	}
#endif

	/*
	 * Now that set_cpu_number() set us with a valid cpu_info pointer,
	 * we need to initialize p_addr and curpcb before autoconf, for the
	 * fault handler to behave properly [except for badaddr() faults,
	 * which can be taken care of without a valid curcpu()].
	 */
	proc0.p_addr = proc0paddr;
	curproc = &proc0;
	curpcb = &proc0paddr->u_pcb;

	avail_start = first_addr;	/* first page of memory after kernel image */
	avail_end = last_addr;		/* last page of memory */

#ifdef DEBUG
	printf("MVME%x boot: memory from 0x%x to 0x%x\n",
	    brdtyp, avail_start, avail_end);
#endif
	/*
	 * Tell the VM system about available physical memory.
	 *
	 * The mvme88k boards only have one contiguous area, although BUG
	 * could be set up to configure a non-contiguous scheme; also, we
	 * might want to register ECC memory separately later on...
	 */
	uvm_page_physload(atop(avail_start), atop(avail_end),
	    atop(avail_start), atop(avail_end), 0);

	/*
	 * Initialize message buffer.
	 */
	initmsgbuf((caddr_t)pmap_steal_memory(MSGBUFSIZE, NULL, NULL),
	    MSGBUFSIZE);

	pmap_bootstrap(0, 0x10000);	/* BUG needs 64KB */

#if defined (MVME187) || defined (MVME197)
	/*
	 * Get ethernet buffer - need ETHERPAGES pages physically contiguous.
	 * XXX need to fix ie(4) to support non-1:1 mapped buffers
	 */
	if (brdtyp == BRD_187 || brdtyp == BRD_8120 || brdtyp == BRD_197) {
		etherlen = ETHERPAGES * PAGE_SIZE;
		etherbuf = (void *)uvm_pageboot_alloc(etherlen);
		pmap_cache_ctrl((paddr_t)etherbuf, (paddr_t)etherbuf + etherlen,
		    CACHE_INH);
	}
#endif /* defined (MVME187) || defined (MVME197) */

	/* Initialize the "u-area" pages. */
	bzero((caddr_t)curpcb, USPACE);
#ifdef DEBUG
	printf("leaving mvme_bootstrap()\n");
#endif
}

#ifdef MULTIPROCESSOR
void
cpu_hatch_secondary_processors(void *unused)
{
	struct cpu_info *ci = curcpu();
	cpuid_t cpu;
	int rc;
	extern void secondary_start(void);

	switch (brdtyp) {
#if defined(MVME188) || defined(MVME197)
#ifdef MVME188
	case BRD_188:
#endif
#ifdef MVME197
	case BRD_197:
#endif
		for (cpu = 0; cpu < ncpusfound; cpu++) {
			if (cpu != ci->ci_cpuid) {
				__cpu_simple_lock(&cpu_hatch_mutex);
				rc = spin_cpu(cpu, (vaddr_t)secondary_start);
				switch (rc) {
				case 0:
					__cpu_simple_lock(&cpu_hatch_mutex);
					break;
				default:
					printf("cpu%d: spin_cpu error %d\n",
					    cpu, rc);
					/* FALLTHROUGH */
				case FORKMPU_NO_MPU:
					break;
				}
				__cpu_simple_unlock(&cpu_hatch_mutex);
			}
		}
		break;
#endif
	default:
		break;
	}
}

void
cpu_boot_secondary_processors()
{
	__cpu_simple_unlock(&cpu_boot_mutex);
}
#endif

/*
 * Boot console routines:
 * Enables printing of boot messages before consinit().
 */
void
bootcnprobe(cp)
	struct consdev *cp;
{
	cp->cn_dev = makedev(14, 0);
	cp->cn_pri = CN_LOWPRI;
}

void
bootcninit(cp)
	struct consdev *cp;
{
	/* Nothing to do */
}

int
bootcngetc(dev)
	dev_t dev;
{
	return (buginchr());
}

void
bootcnputc(dev, c)
	dev_t dev;
	int c;
{
	bugoutchr(c);
}

int
getipl(void)
{
	return (int)platform->getipl();
}

int
setipl(int level)
{
	return (int)platform->setipl((u_int)level);
}

int
raiseipl(int level)
{
	return (int)platform->raiseipl((u_int)level);
}

#ifdef MULTIPROCESSOR

void
m88k_send_ipi(int ipi, cpuid_t cpu)
{
	struct cpu_info *ci;

	ci = &m88k_cpus[cpu];
	if (ISSET(ci->ci_flags, CIF_ALIVE))
		platform->send_ipi(ipi, cpu);
}

void
m88k_broadcast_ipi(int ipi)
{
	struct cpu_info *us = curcpu();
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == us)
			continue;

		if (ISSET(ci->ci_flags, CIF_ALIVE))
			platform->send_ipi(ipi, ci->ci_cpuid);
	}
}

#endif

void
delay(int us)
{
	platform->delay(us);
}
