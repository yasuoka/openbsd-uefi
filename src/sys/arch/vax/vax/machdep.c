/* $OpenBSD: machdep.c,v 1.24 2000/06/08 22:25:23 niklas Exp $ */
/* $NetBSD: machdep.c,v 1.96 2000/03/19 14:56:53 ragge Exp $	 */

/*
 * Copyright (c) 1994, 1998 Ludd, University of Lule}, Sweden.
 * Copyright (c) 1993 Adam Glass
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1986, 1990 The Regents of the University of California.
 * All rights reserved.
 * 
 * Changed for the VAX port (and for readability) /IC
 * 
 * This code is derived from software contributed to Berkeley by the Systems
 * Programming Group of the University of Utah Computer Science Department.
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
 * from: Utah Hdr: machdep.c 1.63 91/04/24
 * 
 * @(#)machdep.c	7.16 (Berkeley) 6/3/91
 */

#include <sys/signal.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/msgbuf.h>
#include <sys/buf.h>
#include <sys/mbuf.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/timeout.h>
#include <sys/device.h>
#include <sys/exec.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/ptrace.h>
#include <vm/vm.h>
#include <sys/sysctl.h>

#include <dev/cons.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>

#ifdef SYSVMSG
#include <sys/msg.h>
#endif
#ifdef SYSVSEM
#include <sys/sem.h>
#endif
#ifdef SYSVSHM
#include <sys/shm.h>
#endif

#include <net/netisr.h>
#include <net/if.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/ip_var.h>
#endif
#ifdef NETATALK
#include <netatalk/at_extern.h>
#endif
#ifdef NS
#include <netns/ns_var.h>
#endif
#include "ppp.h"	/* For NPPP */
#include "bridge.h"	/* For NBRIDGE */
#if NPPP > 0
#include <net/ppp_defs.h>
#include <net/if_ppp.h>
#endif

#include <machine/sid.h>
#include <machine/pte.h>
#include <machine/mtpr.h>
#include <machine/cpu.h>
#include <machine/macros.h>
#include <machine/nexus.h>
#include <machine/trap.h>
#include <machine/reg.h>
#include <machine/db_machdep.h>
#include <vax/vax/gencons.h>

#ifdef DDB
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#endif

#include "smg.h"

caddr_t allocsys __P((caddr_t));

extern int *chrtoblktbl;
extern int virtual_avail, virtual_end;
/*
 * We do these external declarations here, maybe they should be done
 * somewhere else...
 */
int		want_resched;
char		machine[] = MACHINE;		/* from <machine/param.h> */
char		machine_arch[] = MACHINE_ARCH;	/* from <machine/param.h> */
char		cpu_model[100];
int		physmem;
int		dumpsize = 0;
int		cold = 1; /* coldstart */

#define	IOMAPSZ	100
static	struct map iomap[IOMAPSZ];

vm_map_t exec_map = NULL;
vm_map_t mb_map = NULL;

#ifdef DEBUG
int iospace_inited = 0;
#endif

void
cpu_startup()
{
	caddr_t		v;
	extern char	version[];
	int		base, residual, i, sz;
	vm_offset_t	minaddr, maxaddr;
	vm_size_t	size;
	extern unsigned int avail_end;

	/*
	 * Initialize error message buffer.
	 */
	initmsgbuf((caddr_t)msgbufp, round_page(MSGBUFSIZE));

	/*
	 * Good {morning,afternoon,evening,night}.
	 * Also call CPU init on systems that need that.
	 */
	printf("%s\n%s\n", version, cpu_model);
        if (dep_call->cpu_conf)
                (*dep_call->cpu_conf)();

	printf("total memory = %d\n", avail_end);
	physmem = btoc(avail_end);
	panicstr = NULL;
	mtpr(AST_NO, PR_ASTLVL);
	spl0();

	dumpsize = physmem + 1;

	/*
	 * Find out how much space we need, allocate it, and then give
	 * everything true virtual addresses.
	 */

	sz = (int) allocsys((caddr_t)0);
	if ((v = (caddr_t)uvm_km_zalloc(kernel_map, round_page(sz))) == 0)
		panic("startup: no room for tables");
	if (((unsigned long)allocsys(v) - (unsigned long)v) != sz)
		panic("startup: table size inconsistency");
	/*
	 * Now allocate buffers proper.	 They are different than the above in
	 * that they usually occupy more virtual memory than physical.
	 */
	size = MAXBSIZE * nbuf;		/* # bytes for buffers */

	/* allocate VM for buffers... area is not managed by VM system */
	if (uvm_map(kernel_map, (vm_offset_t *) &buffers, round_page(size),
		    NULL, UVM_UNKNOWN_OFFSET,
		    UVM_MAPFLAG(UVM_PROT_NONE, UVM_PROT_NONE, UVM_INH_NONE,
				UVM_ADV_NORMAL, 0)) != KERN_SUCCESS)
		panic("cpu_startup: cannot allocate VM for buffers");

	minaddr = (vm_offset_t) buffers;
	if ((bufpages / nbuf) >= btoc(MAXBSIZE)) {
		/* don't want to alloc more physical mem than needed */
		bufpages = btoc(MAXBSIZE) * nbuf;
	}
	base = bufpages / nbuf;
	residual = bufpages % nbuf;
	/* now allocate RAM for buffers */
	for (i = 0 ; i < nbuf ; i++) {
		vm_offset_t curbuf;
		vm_size_t curbufsize;
		struct vm_page *pg;

		/*
		 * First <residual> buffers get (base+1) physical pages
		 * allocated for them.	The rest get (base) physical pages.
		 * 
		 * The rest of each buffer occupies virtual space, but has no
		 * physical memory allocated for it.
		 */
		curbuf = (vm_offset_t) buffers + i * MAXBSIZE;
		curbufsize = CLBYTES * (i < residual ? base + 1 : base);
		while (curbufsize) {
			pg = uvm_pagealloc(NULL, 0, NULL, 0);
			if (pg == NULL)
				panic("cpu_startup: "
				    "not enough RAM for buffer cache");
			pmap_enter(kernel_map->pmap, curbuf,
			    VM_PAGE_TO_PHYS(pg), VM_PROT_READ|VM_PROT_WRITE, TRUE, 
			    VM_PROT_READ|VM_PROT_WRITE);
			curbuf += PAGE_SIZE;
			curbufsize -= PAGE_SIZE;
		}
	}

	/*
	 * Allocate a submap for exec arguments.  This map effectively limits
	 * the number of processes exec'ing at any time.
	 */
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
				 16 * NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

    mclrefcnt = (char *)malloc(NMBCLUSTERS+CLBYTES/MCLBYTES,
        M_MBUF, M_NOWAIT);
    bzero(mclrefcnt, NMBCLUSTERS+CLBYTES/MCLBYTES);
	mb_map = uvm_km_suballoc(kernel_map, (vaddr_t *)&mbutl, &maxaddr, 
		VM_MBUF_SIZE, VM_MAP_INTRSAFE, FALSE, NULL);
	
	timeout_init();

	printf("avail memory = %ld\n", ptoa(uvmexp.free));
	printf("using %d buffers containing %d of memory\n", nbuf, bufpages * CLBYTES);

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */

	bufinit();
	configure();
}

long	dumplo = 0;
long	dumpmag = 0x8fca0101;

void
cpu_dumpconf()
{
	int		nblks;

	/*
	 * XXX include the final RAM page which is not included in physmem.
	 */
	dumpsize = physmem + 1;
	if (dumpdev != NODEV && bdevsw[major(dumpdev)].d_psize) {
		nblks = (*bdevsw[major(dumpdev)].d_psize) (dumpdev);
		if (dumpsize > btoc(dbtob(nblks - dumplo)))
			dumpsize = btoc(dbtob(nblks - dumplo));
		else if (dumplo == 0)
			dumplo = nblks - btodb(ctob(dumpsize));
	}
	/*
	 * Don't dump on the first CLBYTES (why CLBYTES?) in case the dump
	 * device includes a disk label.
	 */
	if (dumplo < btodb(CLBYTES))
		dumplo = btodb(CLBYTES);
}

int
cpu_sysctl(a, b, c, d, e, f, g)
	int	*a;
	u_int	b;
	void	*c, *e;
	size_t	*d, f;
	struct	proc *g;
{
	return (EOPNOTSUPP);
}

void
setstatclockrate(hzrate)
	int hzrate;
{
	panic("setstatclockrate");
}

void
consinit()
{
	/*
	 * Init I/O memory resource map. Must be done before cninit()
	 * is called; we may want to use iospace in the console routines.
	 */
	rminit(iomap, IOSPSZ, (long)1, "iomap", IOMAPSZ);
#ifdef DEBUG
	iospace_inited = 1;
#endif
	cninit();
#ifdef DDB
	{
		extern int end; /* Contains pointer to symsize also */
		extern int *esym;

		ddb_init();
	}
#ifdef DEBUG
	if (sizeof(struct user) > REDZONEADDR)
		panic("struct user inside red zone");
#endif
#ifdef donotworkbyunknownreason
	if (boothowto & RB_KDB)
		Debugger();
#endif
#endif
}

int
sys_sigreturn(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct sys_sigreturn_args { 
		syscallarg(struct sigcontext *) sigcntxp;
	} *uap = v;
	struct trapframe *scf;
	struct sigcontext *cntx;

	scf = p->p_addr->u_pcb.framep;
	cntx = SCARG(uap, sigcntxp);

	if (uvm_useracc((caddr_t)cntx, sizeof (*cntx), B_READ) == 0)
		return EINVAL;
	/* Compatibility mode? */
	if ((cntx->sc_ps & (PSL_IPL | PSL_IS)) ||
	    ((cntx->sc_ps & (PSL_U | PSL_PREVU)) != (PSL_U | PSL_PREVU)) ||
	    (cntx->sc_ps & PSL_CM)) {
		return (EINVAL);
	}
	if (cntx->sc_onstack & 01)
		p->p_sigacts->ps_sigstk.ss_flags |= SS_ONSTACK;
	else
		p->p_sigacts->ps_sigstk.ss_flags &= ~SS_ONSTACK;
	/* Restore signal mask. */
    p->p_sigmask = cntx->sc_mask & ~sigcantmask;

	scf->fp = cntx->sc_fp;
	scf->ap = cntx->sc_ap;
	scf->pc = cntx->sc_pc;
	scf->sp = cntx->sc_sp;
	scf->psl = cntx->sc_ps;
	return (EJUSTRETURN);
}

struct trampframe {
	unsigned	sig;	/* Signal number */
	unsigned	code;	/* Info code */
	unsigned	scp;	/* Pointer to struct sigcontext */
	unsigned	r0, r1, r2, r3, r4, r5; /* Registers saved when
						 * interrupt */
	unsigned	pc;	/* Address of signal handler */
	unsigned	arg;	/* Pointer to first (and only) sigreturn
				 * argument */
};

void
sendsig(catcher, sig, mask, code, type, val)
	sig_t		catcher;
	int		sig, mask;
	u_long		code;
	int 	type;
	union sigval 	val;
{
	struct	proc	*p = curproc;
	struct	sigacts *psp = p->p_sigacts;
	struct	trapframe *syscf;
	struct	sigcontext *sigctx, gsigctx;
	struct	trampframe *trampf, gtrampf;
	extern	char sigcode[], esigcode[];
	unsigned	cursp;
	int	onstack;

	syscf = p->p_addr->u_pcb.framep;

	onstack = psp->ps_sigstk.ss_flags & SS_ONSTACK;

	/* Allocate space for the signal handler context. */
	if (onstack)
		cursp = ((int)psp->ps_sigstk.ss_sp + psp->ps_sigstk.ss_size);
	else
		cursp = syscf->sp;

	/* Set up positions for structs on stack */
	sigctx = (struct sigcontext *) (cursp - sizeof(struct sigcontext));
	trampf = (struct trampframe *) ((unsigned)sigctx -
	    sizeof(struct trampframe));

	 /* Place for pointer to arg list in sigreturn */
	cursp = (unsigned)sigctx - 8;

	gtrampf.arg = (int) sigctx;
	gtrampf.pc = (unsigned) catcher;
	gtrampf.scp = (int) sigctx;
	gtrampf.code = code;
	gtrampf.sig = sig;

	gsigctx.sc_pc = syscf->pc;
	gsigctx.sc_ps = syscf->psl;
	gsigctx.sc_ap = syscf->ap;
	gsigctx.sc_fp = syscf->fp; 
	gsigctx.sc_sp = syscf->sp; 
	gsigctx.sc_onstack = psp->ps_sigstk.ss_flags & SS_ONSTACK;
	gsigctx.sc_mask = mask;

#if defined(COMPAT_13) || defined(COMPAT_ULTRIX)
	native_sigset_to_sigset13(mask, &gsigctx.__sc_mask13);
#endif

	if (copyout(&gtrampf, trampf, sizeof(gtrampf)) ||
	    copyout(&gsigctx, sigctx, sizeof(gsigctx)))
		sigexit(p, SIGILL);

	syscf->pc = (unsigned) (((char *) PS_STRINGS) - (esigcode - sigcode));
	syscf->psl = PSL_U | PSL_PREVU;
	syscf->ap = cursp;
	syscf->sp = cursp;

	if (onstack)
		psp->ps_sigstk.ss_flags |= SS_ONSTACK;
}

int	waittime = -1;
static	volatile int showto; /* Must be volatile to survive MM on -> MM off */

void
boot(howto)
	register int howto;
{
	if ((howto & RB_NOSYNC) == 0 && waittime < 0) {
		waittime = 0;
		vfs_shutdown();
		/*
		 * If we've been adjusting the clock, the todr will be out of
		 * synch; adjust it now.
		 */
		resettodr();
	}
	splhigh();		/* extreme priority */
	if (howto & RB_HALT) {
		if (dep_call->cpu_halt)
			(*dep_call->cpu_halt) ();
		printf("halting (in tight loop); hit\n\t^P\n\tHALT\n\n");
		for (;;)
			;
	} else {
		showto = howto;
#ifdef notyet
		/*
		 * If we are provided with a bootstring, parse it and send
		 * it to the boot program.
		 */
		if (b)
			while (*b) {
				showto |= (*b == 'a' ? RB_ASKBOOT : (*b == 'd' ?
				    RB_DEBUG : (*b == 's' ? RB_SINGLE : 0)));
				b++;
			}
#endif
		/*
		 * Now it's time to:
		 *  0. Save some registers that are needed in new world.
		 *  1. Change stack to somewhere that will survive MM off.
		 * (RPB page is good page to save things in).
		 *  2. Actually turn MM off.
		 *  3. Dump away memory to disk, if asked.
		 *  4. Reboot as asked.
		 * The RPB page is _always_ first page in memory, we can
		 * rely on that.
		 */
#ifdef notyet
		asm("	movl	sp, (0x80000200)
			movl	0x80000200, sp
			mfpr	$0x10, -(sp)	# PR_PCBB
			mfpr	$0x11, -(sp)	# PR_SCBB
			mfpr	$0xc, -(sp)	# PR_SBR
			mfpr	$0xd, -(sp)	# PR_SLR
			mtpr	$0, $0x38	# PR_MAPEN
		");
#endif

		if (showto & RB_DUMP)
			dumpsys();
		if (dep_call->cpu_reboot)
			(*dep_call->cpu_reboot)(showto);

		/* cpus that don't handle reboots get the standard reboot. */
		while ((mfpr(PR_TXCS) & GC_RDY) == 0)
			;

		mtpr(GC_CONS|GC_BTFL, PR_TXDB);
	}
	asm("movl %0, r5":: "g" (showto)); /* How to boot */
	asm("movl %0, r11":: "r"(showto)); /* ??? */
	asm("halt");
	panic("Halt sket sej");
}

void
dumpsys()
{
	extern int msgbufmapped;

	msgbufmapped = 0;
	if (dumpdev == NODEV)
		return;
	/*
	 * For dumps during autoconfiguration, if dump device has already
	 * configured...
	 */
	if (dumpsize == 0)
		cpu_dumpconf();
	if (dumplo <= 0) {
		printf("\ndump to dev %u,%u not possible\n", major(dumpdev),
		    minor(dumpdev));
		return;
	}
	printf("\ndumping to dev %u,%u offset %ld\n", major(dumpdev),
	    minor(dumpdev), dumplo);
	printf("dump ");
	switch ((*bdevsw[major(dumpdev)].d_dump) (dumpdev, 0, 0, 0)) {

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

	default:
		printf("succeeded\n");
		break;
	}
}

int
process_read_regs(p, regs)
	struct proc    *p;
	struct reg     *regs;
{
	struct trapframe *tf = p->p_addr->u_pcb.framep;

	bcopy(&tf->r0, &regs->r0, 12 * sizeof(int));
	regs->ap = tf->ap;
	regs->fp = tf->fp;
	regs->sp = tf->sp;
	regs->pc = tf->pc;
	regs->psl = tf->psl;
	return 0;
}

int
process_write_regs(p, regs)
	struct proc    *p;
	struct reg     *regs;
{
	struct trapframe *tf = p->p_addr->u_pcb.framep;

	bcopy(&regs->r0, &tf->r0, 12 * sizeof(int));
	tf->ap = regs->ap;
	tf->fp = regs->fp;
	tf->sp = regs->sp;
	tf->pc = regs->pc;
	tf->psl = (regs->psl|PSL_U|PSL_PREVU) &
		~(PSL_MBZ|PSL_IS|PSL_IPL1F|PSL_CM);
	return 0;
}

int
process_set_pc(p, addr)
	struct	proc *p;
	caddr_t addr;
{
	struct	trapframe *tf;
	void	*ptr;

	if ((p->p_flag & P_INMEM) == 0)
		return (EIO);

	ptr = (char *) p->p_addr->u_pcb.framep;
	tf = ptr;

	tf->pc = (unsigned) addr;

	return (0);
}

int
process_sstep(p, sstep)
	struct proc    *p;
{
	void	       *ptr;
	struct trapframe *tf;

	if ((p->p_flag & P_INMEM) == 0)
		return (EIO);

	ptr = p->p_addr->u_pcb.framep;
	tf = ptr;

	if (sstep)
		tf->psl |= PSL_T;
	else
		tf->psl &= ~PSL_T;

	return (0);
}

#undef PHYSMEMDEBUG
/*
 * Allocates a virtual range suitable for mapping in physical memory.
 * This differs from the bus_space routines in that it allocates on
 * physical page sizes instead of logical sizes. This implementation
 * uses resource maps when allocating space, which is allocated from 
 * the IOMAP submap. The implementation is similar to the uba resource
 * map handling. Size is given in pages.
 * If the page requested is bigger than a logical page, space is
 * allocated from the kernel map instead.
 *
 * It is known that the first page in the iospace area is unused; it may
 * be use by console device drivers (before the map system is inited).
 */
vaddr_t
vax_map_physmem(phys, size)
	paddr_t phys;
	int size;
{
	extern vaddr_t iospace;
	vaddr_t addr;
	int pageno;
	static int warned = 0;

#ifdef DEBUG
	if (!iospace_inited)
		panic("vax_map_physmem: called before rminit()?!?");
#endif
	if (size >= LTOHPN) {
		addr = uvm_km_valloc(kernel_map, size * VAX_NBPG);
		if (addr == 0)
			panic("vax_map_physmem: kernel map full");
	} else {
		pageno = rmalloc(iomap, size);
		if (pageno == 0) {
			if (warned++ == 0) /* Warn only once */
				printf("vax_map_physmem: iomap too small");
			return 0;
		}
		addr = iospace + (pageno * VAX_NBPG);
	}
	ioaccess(addr, phys, size);
#ifdef PHYSMEMDEBUG
	printf("vax_map_physmem: alloc'ed %d pages for paddr %lx, at %lx\n",
	    size, phys, addr);
#endif
	return addr | (phys & VAX_PGOFSET);
}

/*
 * Unmaps the previous mapped (addr, size) pair.
 */
void
vax_unmap_physmem(addr, size)
	vaddr_t addr;
	int size;
{
	extern vaddr_t iospace;
	int pageno = (addr - iospace) / VAX_NBPG;
#ifdef PHYSMEMDEBUG
	printf("vax_unmap_physmem: unmapping %d pages at addr %lx\n", 
	    size, addr);
#endif
	iounaccess(addr, size);
	if (size >= LTOHPN)
		uvm_km_free(kernel_map, addr, size * VAX_NBPG);
	else
		rmfree(iomap, size, pageno);
}

/*
 * Allocate space for system data structures.  We are given a starting
 * virtual address and we return a final virtual address; along the way we
 * set each data structure pointer.
 *
 * We call allocsys() with 0 to find out how much space we want, allocate that
 * much and fill it with zeroes, and then call allocsys() again with the
 * correct base virtual address.
 */
caddr_t
allocsys(v)
    register caddr_t v;
{
#define valloc(name, type, num) v = (caddr_t)(((name) = (type *)v) + (num))

#ifdef REAL_CLISTS
    valloc(cfree, struct cblock, nclist);
#endif
    valloc(timeouts, struct timeout, ntimeout);
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
     * Determine how many buffers to allocate (enough to
     * hold 5% of total physical memory, but at least 16).
     * Allocate 1/2 as many swap buffer headers as file i/o buffers.
     */
    if (bufpages == 0)
        bufpages = (physmem / ((100/BUFCACHEPERCENT) / CLSIZE));
    if (nbuf == 0) {
        nbuf = bufpages;
        if (nbuf < 16)
            nbuf = 16;
    }
    if (nbuf > 200)
        nbuf = 200; /* or we run out of PMEGS */
    /* Restrict to at most 70% filled kvm */
    if (nbuf * MAXBSIZE >
        (VM_MAX_KERNEL_ADDRESS-VM_MIN_KERNEL_ADDRESS) * 7 / 10)
        nbuf = (VM_MAX_KERNEL_ADDRESS-VM_MIN_KERNEL_ADDRESS) /
            MAXBSIZE * 7 / 10;

    /* More buffer pages than fits into the buffers is senseless.  */
    if (bufpages > nbuf * MAXBSIZE / CLBYTES)
        bufpages = nbuf * MAXBSIZE / CLBYTES;

    /* Allocate 1/2 as many swap buffer headers as file i/o buffers.  */
    if (nswbuf == 0) {
        nswbuf = (nbuf / 2) & ~1;   /* force even */
        if (nswbuf > 256)
            nswbuf = 256;       /* sanity */
    }
    valloc(buf, struct buf, nbuf);
    return (v);
}

