/*	$OpenBSD: machdep.c,v 1.45 2001/11/28 13:47:39 art Exp $	*/
/*	$NetBSD: machdep.c,v 1.77 1996/10/13 03:47:51 christos Exp $	*/

/*
 * Copyright (c) 1994, 1995 Gordon W. Ross
 * Copyright (c) 1993 Adam Glass
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 *	from: Utah Hdr: machdep.c 1.74 92/12/20
 *	from: @(#)machdep.c	8.10 (Berkeley) 4/20/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/map.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/clist.h>
#include <sys/timeout.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/msgbuf.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/mount.h>
#include <sys/user.h>
#include <sys/exec.h>
#include <sys/core.h>
#include <sys/kcore.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>
#include <sys/syscallargs.h>
#ifdef SYSVMSG
#include <sys/msg.h>
#endif
#ifdef SYSVSEM
#include <sys/sem.h>
#endif
#ifdef SYSVSHM
#include <sys/shm.h>
#endif

#include <uvm/uvm_extern.h>

#include <dev/cons.h>

#include <machine/cpu.h>
#include <machine/db_machdep.h>
#include <machine/dvma.h>
#include <machine/kcore.h>
#include <machine/machdep.h>
#include <machine/mon.h> 
#include <machine/psl.h>
#include <machine/pte.h>
#include <machine/reg.h>

extern char *cpu_string;
extern char version[];
extern short exframesize[];

int physmem;
int fputype;
label_t *nofault;
vm_offset_t vmmap;

vm_map_t exec_map = NULL;
vm_map_t mb_map = NULL;
vm_map_t phys_map = NULL;

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int	safepri = PSL_LOWIPL;

/*
 * Declare these as initialized data so we can patch them.
 */
#ifdef	NBUF
int	nbuf = NBUF;
#else
int	nbuf = 0;
#endif
#ifdef	BUFPAGES
int	bufpages = BUFPAGES;
#else
int	bufpages = 0;
#endif

static caddr_t allocsys __P((caddr_t));
static void identifycpu __P((void));
static void initcpu __P((void));
static void reboot_sync __P((void));
int  reboot2 __P((int, char *)); /* share with sunos_misc.c */

void straytrap __P((struct trapframe));	/* called from locore.s */

/*
 * Console initialization: called early on from main,
 * before vm init or startup.  Do enough configuration
 * to choose and initialize a console.
 */
void
consinit()
{

	cninit();

#ifdef KGDB
	/* XXX - Ask on console for kgdb_dev? */
	zs_kgdb_init();		/* XXX */
	/* Note: kgdb_connect() will just return if kgdb_dev<0 */
	if (boothowto & RB_KDB)
		kgdb_connect(1);
#endif
#ifdef DDB
	/* Now that we have a console, we can stop in DDB. */
	db_machine_init();
	ddb_init();
	if (boothowto & RB_KDB)
		Debugger();
#endif
}

/*
 * allocsys() - Private routine used by cpu_startup() below.
 *
 * Allocate space for system data structures.  We are given
 * a starting virtual address and we return a final virtual
 * address; along the way we set each data structure pointer.
 *
 * We call allocsys() with 0 to find out how much space we want,
 * allocate that much and fill it with zeroes, and then call
 * allocsys() again with the correct base virtual address.
 */

#define	valloc(name, type, num) \
	v = (caddr_t)(((name) = (type *)v) + (num))

static caddr_t
allocsys(v)
	register caddr_t v;
{

#ifdef SYSVSHM
	valloc(shmsegs, struct shmid_ds, shminfo.shmmni);
#endif
#ifdef SYSVSEM
	valloc(sema, struct semid_ds, seminfo.semmni);
	valloc(sem, struct sem, seminfo.semmns);
	/* This is pretty disgusting! */
	valloc(semu, int, (seminfo.semmnu * seminfo.semusz) / sizeof(int));
#endif
#ifdef SYSVMSG
	valloc(msgpool, char, msginfo.msgmax);
	valloc(msgmaps, struct msgmap, msginfo.msgseg);
	valloc(msghdrs, struct msg, msginfo.msgtql);
	valloc(msqids, struct msqid_ds, msginfo.msgmni);
#endif

#ifndef BUFCACHEPERCENT
#define BUFCACHEPERCENT 5
#endif
	/*
	 * Determine how many buffers to allocate. By default we allocate
	 * the BSD standard of use 10% of memory for the first 2 Meg,
	 * 5% of remaining.  But this might cause systems with large 
	 * core (32MB) to fail to boot due to small KVM space.  Reduce
	 * BUFCACHEPERCENT in this case.
	 * Insure a minimum of 16 buffers.
	 * Allocate 1/2 as many swap buffer headers as file i/o buffers.
	 */
	if (bufpages == 0) {
		/* We always have more than 2MB of memory. */
		bufpages = (btoc(2 * 1024 * 1024) + physmem) *
		    BUFCACHEPERCENT / 100;
	}
	if (nbuf == 0) {
		nbuf = bufpages;
		if (nbuf < 16)
			nbuf = 16;
	}
	/* Restrict to at most 70% filled kvm */
	if (nbuf * MAXBSIZE >
	    (VM_MAX_KERNEL_ADDRESS-VM_MIN_KERNEL_ADDRESS) * 7 / 10)
		nbuf = (VM_MAX_KERNEL_ADDRESS-VM_MIN_KERNEL_ADDRESS) /
			MAXBSIZE * 7 / 10;
	
	/* More buffer pages than fits into the buffers is senseless.  */
	if (bufpages > nbuf * MAXBSIZE / PAGE_SIZE)
		bufpages = nbuf * MAXBSIZE / PAGE_SIZE;
	valloc(buf, struct buf, nbuf);
	return v;
}
#undef	valloc

/*
 * cpu_startup: allocate memory for variable-sized tables,
 * initialize cpu, and do autoconfiguration.
 *
 * This is called early in init_main.c:main(), after the
 * kernel memory allocator is ready for use, but before
 * the creation of processes 1,2, and mountroot, etc.
 */
void
cpu_startup()
{
	caddr_t v;
	int sz, i;
	vm_size_t size;	
	int base, residual;
	vm_offset_t minaddr, maxaddr;
	
	/*
	 * The msgbuf was set up earlier (in sun3_startup.c)
	 * just because it was more convenient to do there.
	 */
	
	/*
	 * Good {morning,afternoon,evening,night}.
	 */
	printf(version);
	identifycpu();
	initfpu();	/* also prints FPU type */

	printf("real mem = %d\n", ctob(physmem));

	/*
	 * Find out how much space we need, allocate it,
	 * and then give everything true virtual addresses.
	 */
	sz = (int)allocsys((caddr_t)0);
	if ((v = (caddr_t)uvm_km_zalloc(kernel_map, round_page(sz))) == 0)
		panic("startup: no room for tables");
	if (allocsys(v) - v != sz)
		panic("startup: table size inconsistency");

	/*
	 * Now allocate buffers proper.  They are different than the above
	 * in that they usually occupy more virtual memory than physical.
	 */
	size = MAXBSIZE * nbuf;
	if (uvm_map(kernel_map, (vaddr_t *) &buffers, round_page(size),
	    NULL, UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(UVM_PROT_NONE, UVM_PROT_NONE, UVM_INH_NONE,
	                UVM_ADV_NORMAL, 0)))
		panic("startup: cannot allocate buffers");
	minaddr = (vm_offset_t)buffers;
	if ((bufpages / nbuf) >= btoc(MAXBSIZE)) {
		/* don't want to alloc more physical mem than needed */
		bufpages = btoc(MAXBSIZE) * nbuf;
	}
	base = bufpages / nbuf;
	residual = bufpages % nbuf;
	for (i = 0; i < nbuf; i++) {
		vsize_t curbufsize;
		vaddr_t curbuf;
		struct vm_page *pg;

		/*
		 * Each buffer has MAXBSIZE bytes of VM space allocated.  Of
		 * that MAXBSIZE space, we allocate and map (base+1) pages
		 * for the first "residual" buffers, and then we allocate
		 * "base" pages for the rest.
		 */
		curbuf = (vm_offset_t) buffers + (i * MAXBSIZE);
		curbufsize = PAGE_SIZE * ((i < residual) ? (base+1) : base);

		while (curbufsize != 0) {
			pg = uvm_pagealloc(NULL, 0, NULL, 0);
			if (pg == NULL)
				panic("cpu_startup: not enough memory for "
				    "buffer cache");
			pmap_enter(kernel_map->pmap, curbuf,
			    VM_PAGE_TO_PHYS(pg), VM_PROT_ALL,
			    VM_PROT_READ|VM_PROT_WRITE|PMAP_WIRED);
			curbuf += PAGE_SIZE;
			curbufsize -= PAGE_SIZE;
		}
	}

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    16*NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * We don't use a submap for physio, and use a separate map
	 * for DVMA allocations.  Our vmapbuf just maps pages into
	 * the kernel map (any kernel mapping is OK) and then the
	 * device drivers clone the kernel mappings into DVMA space.
	 */

	mb_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_MBUF_SIZE, VM_MAP_INTRSAFE, FALSE, NULL);

	printf("avail mem = %ld\n", ptoa(uvmexp.free));
	printf("using %d buffers containing %d bytes of memory\n",
		   nbuf, bufpages * PAGE_SIZE);

	/*
	 * Allocate a virtual page (for use by /dev/mem)
	 * This page is handed to pmap_enter() therefore
	 * it has to be in the normal kernel VA range.
	 */
	vmmap = uvm_km_valloc_wait(kernel_map, NBPG);

	/*
	 * Create the DVMA maps.
	 */
	dvma_init();

	/*
	 * Set up CPU-specific registers, cache, etc.
	 */
	initcpu();

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();

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

/*
 * Set registers on exec.
 */
void
setregs(p, pack, stack, retval)
	register struct proc *p;
	struct exec_package *pack;
	u_long stack;
	register_t *retval;
{
	struct frame *frame = (struct frame *)p->p_md.md_regs;

	frame->f_sr = PSL_USERSET;
	frame->f_pc = pack->ep_entry & ~1;
	frame->f_regs[D0] = 0;
	frame->f_regs[D1] = 0;
	frame->f_regs[D2] = 0;
	frame->f_regs[D3] = 0;
	frame->f_regs[D4] = 0;
	frame->f_regs[D5] = 0;
	frame->f_regs[D6] = 0;
	frame->f_regs[D7] = 0;
	frame->f_regs[A0] = 0;
	frame->f_regs[A1] = 0;
	frame->f_regs[A2] = (int)PS_STRINGS;
	frame->f_regs[A3] = 0;
	frame->f_regs[A4] = 0;
	frame->f_regs[A5] = 0;
	frame->f_regs[A6] = 0;
	frame->f_regs[SP] = stack;

	/* restore a null state frame */
	p->p_addr->u_pcb.pcb_fpregs.fpf_null = 0;
	if (fputype) {
		m68881_restore(&p->p_addr->u_pcb.pcb_fpregs);
	}
	/* XXX - HPUX sigcode hack would go here... */
}

/*
 * Info for CTL_HW
 */
char	machine[] = "sun3";		/* cpu "architecture" */
char	cpu_model[120];
extern	long hostid;

void
identifycpu()
{
	/*
	 * actual identification done earlier because i felt like it,
	 * and i believe i will need the info to deal with some VAC, and awful
	 * framebuffer placement problems.  could be moved later.
	 */
	strcpy(cpu_model, "Sun 3/");

	/*
	 * should eventually include whether it has a VAC, mc6888x
	 * version, etc
	 */
	strcat(cpu_model, cpu_string);

	printf("Model: %s (hostid %lx)\n", cpu_model, hostid);
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

	/* all sysctl names at this level are terminal */
	if (namelen != 1)
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
	case CPU_CONSDEV:
		if (cn_tab != NULL)
			consdev = cn_tab->cn_dev;
		else
			consdev = NODEV;
		return (sysctl_rdstruct(oldp, oldlenp, newp, &consdev,
		    sizeof consdev));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

#define SS_RTEFRAME	1
#define SS_FPSTATE	2
#define SS_USERREGS	4

struct sigstate {
	int	ss_flags;		/* which of the following are valid */
	struct	frame ss_frame;		/* original exception frame */
	struct	fpframe ss_fpstate;	/* 68881/68882 state info */
};

/*
 * WARNING: code in locore.s assumes the layout shown for sf_signum
 * thru sf_handler so... don't screw with them!
 */
struct sigframe {
	int	sf_signum;		/* signo for handler */
	siginfo_t *sf_sip;		/* values for SA_SIGINFO */
	struct	sigcontext *sf_scp;	/* context ptr for handler */
	sig_t	sf_handler;		/* handler addr for u_sigc */
	struct	sigstate sf_state;	/* state of the hardware */
	struct	sigcontext sf_sc;	/* actual context */
	siginfo_t sf_si;
};

#ifdef DEBUG
int sigdebug = 0;
int sigpid = 0;
#define SDB_FOLLOW	0x01
#define SDB_KSTACK	0x02
#define SDB_FPSTATE	0x04
#endif

/*
 * Do a sync in preparation for a reboot.
 * XXX - This could probably be common code.
 * XXX - And now, most of it is in vfs_shutdown()
 * XXX - Put waittime checks in there too?
 */
int waittime = -1;	/* XXX - Who else looks at this? -gwr */
void
reboot_sync()
{
	extern struct proc proc0;

	/* Check waittime here to localize its use to this function. */
	if (waittime >= 0)
		return;
	/* fix curproc */
	if (curproc == NULL)
		curproc = &proc0;
	waittime = 0;
	vfs_shutdown();
}

/*
 * Common part of the BSD and SunOS reboot system calls.
 */
__dead int
reboot2(howto, user_boot_string)
	int howto;
	char *user_boot_string;
{
	char *bs, *p;
	char default_boot_string[8];

	/* If system is cold, just halt. (early panic?) */
	if (cold)
		goto haltsys;

	if ((howto & RB_NOSYNC) == 0) {
		reboot_sync();
		/*
		 * If we've been adjusting the clock, the todr
		 * will be out of synch; adjust it now unless
		 * the system was sitting in ddb.
		 */
		if ((howto & RB_TIMEBAD) == 0) {
			resettodr();
		} else {
			printf("WARNING: not updating battery clock\n");
		}
	}

	/* Disable interrupts. */
	splhigh();

	/* Write out a crash dump if asked. */
	if (howto & RB_DUMP)
		dumpsys();

	/* run any shutdown hooks */
	doshutdownhooks();

	if (howto & RB_HALT) {
	haltsys:
		printf("Kernel halted.\n");
		sun3_mon_halt();
	}

	/*
	 * Automatic reboot.
	 */
	bs = user_boot_string;
	if (bs == NULL) {
		/*
		 * Build our own boot string with an empty
		 * boot device/file and (maybe) some flags.
		 * The PROM will supply the device/file name.
		 */
		bs = default_boot_string;
		*bs = '\0';
		if (howto & (RB_KDB|RB_ASKNAME|RB_SINGLE)) {
			/* Append the boot flags. */
			p = bs;
			*p++ = ' ';
			*p++ = '-';
			if (howto & RB_KDB)
				*p++ = 'd';
			if (howto & RB_ASKNAME)
				*p++ = 'a';
			if (howto & RB_SINGLE)
				*p++ = 's';
			*p = '\0';
		}
	}
	printf("Kernel rebooting...\n");
	sun3_mon_reboot(bs);
	for (;;) ;
	/*NOTREACHED*/
}

/*
 * BSD reboot system call
 * XXX - Should be named: cpu_reboot maybe? -gwr
 * XXX - It would be nice to allow a second argument
 * that specifies a machine-dependent boot string that
 * is passed to the boot program if RB_STRING is set.
 */
__dead void
boot(howto)
	int howto;
{
	(void) reboot2(howto, NULL);
	for(;;);
	/* NOTREACHED */
}

/*
 * These variables are needed by /sbin/savecore
 */
u_long	dumpmag = 0x8fca0101;	/* magic number */
int 	dumpsize = 0;		/* pages */
long	dumplo = 0; 		/* blocks */

/* Our private scratch page for dumping the MMU. */
vm_offset_t dumppage_va;
vm_offset_t dumppage_pa;

#define		DUMP_EXTRA	3	/* CPU-dependent extra pages */

/*
 * This is called by cpu_startup to set dumplo, dumpsize.
 * Dumps always skip the first block of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void
dumpconf()
{
	int nblks;	/* size of dump area */
	int maj;
	int (*getsize) __P((dev_t));

	if (dumpdev == NODEV)
		return;

	maj = major(dumpdev);
	if (maj < 0 || maj >= nblkdev)
		panic("dumpconf: bad dumpdev=0x%x", dumpdev);
	getsize = bdevsw[maj].d_psize;
	if (getsize == NULL)
		return;
	nblks = (*getsize)(dumpdev);
	if (nblks <= ctod(1))
		return;

	/* Position dump image near end of space, page aligned. */
	dumpsize = physmem + DUMP_EXTRA; /* pages */
	dumplo = nblks - ctod(dumpsize);
	dumplo &= ~(ctod(1)-1);

	/* If it does not fit, truncate it by moving dumplo. */
	/* Note: Must force signed comparison (fixes PR#887) */
	if (dumplo < ((long)ctod(1))) {
		dumplo = ctod(1);
		dumpsize = dtoc(nblks - dumplo);
	}
}

struct pcb dumppcb;
extern vm_offset_t avail_start;

/*
 * Write a crash dump.  The format while in swap is:
 *   kcore_seg_t cpu_hdr;
 *   cpu_kcore_hdr_t cpu_data;
 *   padding (NBPG-sizeof(kcore_seg_t))
 *   pagemap (2*NBPG)
 *   physical memory...
 */
void
dumpsys()
{
	struct bdevsw *dsw;
	kcore_seg_t	*kseg_p;
	cpu_kcore_hdr_t	*chdr_p;
	char *vaddr;
	vm_offset_t paddr;
	int psize, todo, chunk;
	daddr_t blkno;
	int error = 0;

	if (dumpdev == NODEV)
		return;
	if (dumppage_va == 0)
		return;

	/* 
	 * For dumps during autoconfiguration,
	 * if dump device has already configured...
	 */
	if (dumpsize == 0)
		dumpconf();
	if (dumplo <= 0)
		return;
	savectx(&dumppcb);

	dsw = &bdevsw[major(dumpdev)];
	psize = (*(dsw->d_psize))(dumpdev);
	if (psize == -1) {
		printf("dump area unavailable\n");
		return;
	}

	printf("\ndumping to dev %x, offset %ld\n", dumpdev, dumplo);

	/*
	 * Write the dump header, including MMU state.
	 */
	blkno = dumplo;
	todo = dumpsize - DUMP_EXTRA;	/* pages */
	vaddr = (char *)dumppage_va;
	bzero(vaddr, NBPG);

	/* kcore header */
	kseg_p = (kcore_seg_t *)vaddr;
	CORE_SETMAGIC(*kseg_p, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	kseg_p->c_size = (ctob(DUMP_EXTRA) - sizeof(kcore_seg_t));

	/* MMU state */
	chdr_p = (cpu_kcore_hdr_t *) (kseg_p + 1);
	pmap_get_ksegmap(chdr_p->ksegmap);
	error = (*dsw->d_dump)(dumpdev, blkno, vaddr, NBPG);
	if (error)
		goto fail;
	blkno += btodb(NBPG);

	/* translation RAM (page zero) */
	pmap_get_pagemap((int *)vaddr, 0);
	error = (*dsw->d_dump)(dumpdev, blkno, vaddr, NBPG);
	if (error)
		goto fail;
	blkno += btodb(NBPG);

	/* translation RAM (page one) */
	pmap_get_pagemap((int *)vaddr, NBPG);
	error = (*dsw->d_dump)(dumpdev, blkno, vaddr, NBPG);
	if (error)
		goto fail;
	blkno += btodb(NBPG);

	/*
	 * Now dump physical memory.  Have to do it in two chunks.
	 * The first chunk is "unmanaged" (by the VM code) and its
	 * range of physical addresses is not allow in pmap_enter.
	 * However, that segment is mapped linearly, so we can just
	 * use the virtual mappings already in place.  The second
	 * chunk is done the normal way, using pmap_enter.
	 *
	 * Note that vaddr==(paddr+KERNBASE) for paddr=0 through etext.
	 */

	/* Do the first chunk (0 <= PA < avail_start) */
	paddr = 0;
	chunk = btoc(avail_start);
	if (chunk > todo)
		chunk = todo;
	do {
		if ((todo & 0xf) == 0)
			printf("\r%4d", todo);
		vaddr = (char*)(paddr + KERNBASE);
		error = (*dsw->d_dump)(dumpdev, blkno, vaddr, NBPG);
		if (error)
			goto fail;
		paddr += NBPG;
		blkno += btodb(NBPG);
		--todo;
	} while (--chunk > 0);

	/* Do the second chunk (avail_start <= PA < dumpsize) */
	vaddr = (char*)vmmap;	/* Borrow /dev/mem VA */
	do {
		if ((todo & 0xf) == 0)
			printf("\r%4d", todo);
		pmap_enter(pmap_kernel(), vmmap, paddr | PMAP_NC,
			VM_PROT_READ, VM_PROT_READ);
		error = (*dsw->d_dump)(dumpdev, blkno, vaddr, NBPG);
		pmap_remove(pmap_kernel(), vmmap, vmmap + NBPG);
		if (error)
			goto fail;
		paddr += NBPG;
		blkno += btodb(NBPG);
	} while (--todo > 0);

	printf("\rdump succeeded\n");
	return;
fail:
	printf(" dump error=%d\n", error);
}

void
initcpu()
{
	/* XXX: Enable RAM parity/ECC checking? */
	/* XXX: parityenable(); */

	nofault = NULL;	/* XXX - needed? */

#ifdef	HAVECACHE
	cache_enable();
#endif
}

void
straytrap(frame)
	struct trapframe frame;
{
	printf("unexpected trap; vector offset 0x%x from 0x%x\n",
		frame.tf_vector, frame.tf_pc);
#ifdef	DDB
	kdb_trap(-1, (db_regs_t *) &frame);
#endif
}

/* XXX: parityenable() ? */

/*
 * cpu_exec_aout_makecmds():
 *	cpu-dependent a.out format hook for execve().
 * 
 * Determine if the given exec package refers to something which we
 * understand and, if so, set up the vmcmds for it.
 */
int
cpu_exec_aout_makecmds(p, epp)
	struct proc *p;
	struct exec_package *epp;
{
	int error = ENOEXEC;

#ifdef COMPAT_SUNOS
	extern int sunos_exec_aout_makecmds
		__P((struct proc *, struct exec_package *));
	if ((error = sunos_exec_aout_makecmds(p, epp)) == 0)
		return 0;
#endif
	return error;
}
