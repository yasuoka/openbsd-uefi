/*	$OpenBSD: machdep.c,v 1.48 2006/05/20 22:38:52 deraadt Exp $	*/
/*	$NetBSD: machdep.c,v 1.3 2003/05/07 22:58:18 fvdl Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1982, 1987, 1990 The Regents of the University of California.
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
 *	@(#)machdep.c	7.4 (Berkeley) 6/3/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/exec.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/msgbuf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/extent.h>
#include <sys/core.h>
#include <sys/kcore.h>
#include <sys/syscallargs.h>

#ifdef SYSVMSG
#include <sys/msg.h>
#endif

#ifdef KGDB
#include <sys/kgdb.h>
#endif

#include <dev/cons.h>
#include <stand/boot/bootarg.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_page.h>

#include <sys/sysctl.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/gdt.h>
#include <machine/pio.h>
#include <machine/psl.h>
#include <machine/reg.h>
#include <machine/specialreg.h>
#include <machine/fpu.h>
#include <machine/mtrr.h>
#include <machine/biosvar.h>
#include <machine/mpbiosvar.h>
#include <machine/reg.h>
#include <machine/kcore.h>

#include <dev/isa/isareg.h>
#include <machine/isa_machdep.h>
#include <dev/ic/i8042reg.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#endif

#include "isa.h"
#include "isadma.h"
#include "ksyms.h"

#include "acpi.h"
#if NACPI > 0
#include <dev/acpi/acpivar.h>
#endif


/* the following is used externally (sysctl_hw) */
char machine[] = MACHINE;

#ifdef CPURESET_DELAY
int	cpureset_delay = CPURESET_DELAY;
#else
int     cpureset_delay = 2000; /* default to 2s */
#endif

int	physmem;
u_int64_t	dumpmem_low;
u_int64_t	dumpmem_high;
extern int	boothowto;
int	cpu_class;

char	*ssym = NULL;
vaddr_t kern_end;

vaddr_t	msgbuf_vaddr;
paddr_t msgbuf_paddr;

vaddr_t	idt_vaddr;
paddr_t	idt_paddr;

vaddr_t lo32_vaddr;
paddr_t lo32_paddr;

int kbd_reset;

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

#ifdef NBUF
int	nbuf = NBUF;
#else
int	nbuf = 0;
#endif

#ifndef BUFCACHEPERCENT
#define BUFCACHEPERCENT 10
#endif

#ifdef BUFPAGES
int	bufpages = BUFPAGES;
#else
int	bufpages = 0;
#endif
int bufcachepercent = BUFCACHEPERCENT;

#ifdef DEBUG
int sigdebug = 0;
pid_t sigpid = 0;
#define SDB_FOLLOW      0x01
#endif

extern	paddr_t avail_start, avail_end;

void (*delay_func)(int) = i8254_delay;
void (*microtime_func)(struct timeval *) = i8254_microtime;
void (*initclock_func)(void) = i8254_initclocks;

struct mtrr_funcs *mtrr_funcs;

/*
 * Format of boot information passed to us by 32-bit /boot
 */
typedef struct _boot_args32 {
	int	ba_type;
	int	ba_size;
	int	ba_nextX;	/* a ptr in 32-bit world, but not here */
	char	ba_arg[1];
} bootarg32_t;

#define BOOTARGC_MAX	NBPG	/* one page */

#ifdef NFSCLIENT
bios_bootmac_t *bios_bootmac;
#endif

/* locore copies the arguments from /boot to here for us */
char bootinfo[BOOTARGC_MAX];
int bootinfo_size = BOOTARGC_MAX;

void getbootinfo(char *, int);

/* Data passed to us by /boot, filled in by getbootinfo() */
#if NAPM > 0 || defined(DEBUG)
bios_apminfo_t	*apm;
#endif
#if NPCI > 0
bios_pciinfo_t	*bios_pciinfo;
#endif
bios_diskinfo_t	*bios_diskinfo;
bios_memmap_t	*bios_memmap;
u_int32_t	bios_cksumlen;

/*
 * Size of memory segments, before any memory is stolen.
 */
phys_ram_seg_t mem_clusters[VM_PHYSSEG_MAX];
int	mem_cluster_cnt;

vaddr_t	allocsys(vaddr_t);
void	setup_buffers(vaddr_t *);
int	cpu_dump(void);
int	cpu_dumpsize(void);
u_long	cpu_dump_mempagecnt(void);
void	dumpsys(void);
void	init_x86_64(paddr_t);

#ifdef KGDB
#ifndef KGDB_DEVNAME
#define KGDB_DEVNAME	"com"
#endif /* KGDB_DEVNAME */
char kgdb_devname[] = KGDB_DEVNAME;
#if NCOM > 0
#ifndef KGDBADDR
#define KGDBADDR	0x3f8
#endif /* KGDBADDR */
int comkgdbaddr = KGDBADDR;
#ifndef KGDBRATE
#define KGDBRATE	TTYDEF_SPEED
#endif /* KGDBRATE */
int comkgdbrate = KGDBRATE;
#ifndef KGDBMODE
#define KGDBMODE	((TTYDEF_CFLAG & ~(CSIZE | CSTOPB | PARENB)) | CS8)
#endif /* KGDBMODE */
int comkgdbmode = KGDBMODE;
#endif /* NCOM */
void	kgdb_port_init(void);
#endif /* KGDB */

#ifdef APERTURE
#ifdef INSECURE
int allowaperture = 1;
#else
int allowaperture = 0;
#endif
#endif

/*
 * Machine-dependent startup code
 */
void
cpu_startup(void)
{
	vaddr_t v;
	vsize_t sz;
	vaddr_t minaddr, maxaddr;

	msgbuf_vaddr = PMAP_DIRECT_MAP(msgbuf_paddr);
	initmsgbuf((caddr_t)msgbuf_vaddr, round_page(MSGBUFSIZE));

	printf("%s", version);

	printf("real mem = %u (%uK)\n", ctob(physmem), ctob(physmem)/1024);

	if (physmem >= btoc(1ULL << 32)) {
		extern int amdgart_enable;

		amdgart_enable = 1;
	}

	/*
	 * Find out how much space we need, allocate it,
	 * and then give everything true virtual addresses.
	 */
	sz = allocsys(0);
	if ((v = uvm_km_zalloc(kernel_map, round_page(sz))) == 0)
		panic("startup: no room for tables");
	if (allocsys(v) - v != sz)
		panic("startup: table size inconsistency");

	/*
	 * Now allocate buffers proper. They are different than the above
	 * in that they usually occupy more virtual memory than physical.
	 */
	setup_buffers(&maxaddr);

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
				   16*NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate a submap for physio
	 */
	minaddr = vm_map_min(kernel_map);
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
				   VM_PHYS_SIZE, 0, FALSE, NULL);

	printf("avail mem = %lu (%luK)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free)/1024);
	printf("using %u buffers containing %u bytes (%uK) of memory\n",
	    nbuf, bufpages * PAGE_SIZE, bufpages * PAGE_SIZE / 1024);

	bufinit();

	if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support - c; continuing..\n");
#endif
	}

	/* Safe for i/o port / memory space allocation to use malloc now. */
	x86_bus_space_mallocok();
}


/*
 * The following defines are for the code in setup_buffers that tries to
 * ensure that enough ISA DMAable memory is still left after the buffercache
 * has been allocated.
 */
#define CHUNKSZ		(3 * 1024 * 1024)
#define ISADMA_LIMIT	(16 * 1024 * 1024)	/* XXX wrong place */
#define ALLOC_PGS(sz, limit, pgs) \
    uvm_pglistalloc((sz), 0, (limit), PAGE_SIZE, 0, &(pgs), 1, 0)
#define FREE_PGS(pgs) uvm_pglistfree(&(pgs))

/*
 * Allocate space for system data structures.  We are given
 * a starting virtual address and we return a final virtual
 * address; along the way we set each data structure pointer.
 *
 * We call allocsys() with 0 to find out how much space we want,
 * allocate that much and fill it with zeroes, and then call
 * allocsys() again with the correct base virtual address.
 */
vaddr_t
allocsys(vaddr_t v)
{

#define	valloc(name, type, num) \
	    v = (vaddr_t)(((name) = (type *)v) + (num))

#ifdef SYSVMSG
	valloc(msgpool, char, msginfo.msgmax);
	valloc(msgmaps, struct msgmap, msginfo.msgseg);
	valloc(msghdrs, struct msg, msginfo.msgtql);
	valloc(msqids, struct msqid_ds, msginfo.msgmni);
#endif

	/*
	 * Determine how many buffers to allocate.  We use 10% of the
	 * first 2MB of memory, and 10% of the rest, with a minimum of 16
	 * buffers.  We allocate 1/2 as many swap buffer headers as file
	 * i/o buffers.
	 */
	if (bufpages == 0) {
		bufpages = (btoc(2 * 1024 * 1024) + physmem) *
		    bufcachepercent / 100;
	}
	if (nbuf == 0) {
		nbuf = bufpages;
		if (nbuf < 16)
			nbuf = 16;
	}

	/* Restrict to at most 35% filled kvm */
	/* XXX - This needs UBC... */
	if (nbuf >
	    (VM_MAX_KERNEL_ADDRESS-VM_MIN_KERNEL_ADDRESS) / MAXBSIZE * 35 / 100) 
		nbuf = (VM_MAX_KERNEL_ADDRESS-VM_MIN_KERNEL_ADDRESS) /
		    MAXBSIZE * 35 / 100;

	/* More buffer pages than fits into the buffers is senseless.  */
	if (bufpages > nbuf * MAXBSIZE / PAGE_SIZE)
		bufpages = nbuf * MAXBSIZE / PAGE_SIZE;

	valloc(buf, struct buf, nbuf);
	return v;
}

void
setup_buffers(vaddr_t *maxaddr)
{
	vsize_t size;
	vaddr_t addr;
	int base, residual, left, chunk, i;
	struct pglist pgs, saved_pgs;
	struct vm_page *pg;
	int rv;

	size = MAXBSIZE * nbuf;
	addr = vm_map_min(kernel_map);
	if ((rv = uvm_map(kernel_map, &addr, round_page(size),
		    NULL, UVM_UNKNOWN_OFFSET, 0,
		    UVM_MAPFLAG(UVM_PROT_NONE, UVM_PROT_NONE, UVM_INH_NONE,
				UVM_ADV_NORMAL, 0))))
		panic("cpu_startup: cannot allocate VM for buffers %d", rv);
	buffers = (char *)addr;

	base = bufpages / nbuf;
	residual = bufpages % nbuf;
	if (base >= MAXBSIZE / PAGE_SIZE) {
		/* don't want to alloc more physical mem than needed */
		base = MAXBSIZE / PAGE_SIZE;
		residual = 0;
	}

	/*
	 * In case we might need DMA bouncing we have to make sure there
	 * is some memory below 16MB available.  On machines with many
	 * pages reserved for the buffer cache we risk filling all of that
	 * area with buffer pages.  We still want much of the buffers
	 * reside there as that lowers the probability of them needing to
	 * bounce, but we have to set aside some space for DMA buffers too.
	 *
	 * The current strategy is to grab hold of one 3MB chunk below 16MB
	 * first, which we are saving for DMA buffers, then try to get
	 * one chunk at a time for fs buffers, until that is not possible
	 * anymore, at which point we get the rest wherever we may find it.
	 * After that we give our saved area back. That will guarantee at
	 * least 3MB below 16MB left for drivers' attach routines, among
	 * them isadma.  However we still have a potential problem of PCI
	 * devices attached earlier snatching that memory.  This can be
	 * solved by making the PCI DMA memory allocation routines go for
	 * memory above 16MB first.
	 */

	left = bufpages;

	/*
	 * First, save ISA DMA bounce buffer area so we won't lose that
	 * capability.
	 */
	TAILQ_INIT(&saved_pgs);
	TAILQ_INIT(&pgs);
	if (!ALLOC_PGS(CHUNKSZ, ISADMA_LIMIT, saved_pgs)) {
		/*
		 * Then, grab as much ISA DMAable memory as possible
		 * for the buffer cache as it is nice to not need to
		 * bounce all buffer I/O.
		 */
		for (left = bufpages; left > 0; left -= chunk) {
			chunk = min(left, CHUNKSZ / PAGE_SIZE);
			if (ALLOC_PGS(chunk * PAGE_SIZE, ISADMA_LIMIT, pgs))
				break;
		}
	}

	/*
	 * If we need more pages for the buffer cache, get them from anywhere.
	 */
	if (left > 0 && ALLOC_PGS(left * PAGE_SIZE, avail_end, pgs))
		panic("cannot get physical memory for buffer cache");

	/*
	 * Finally, give back the ISA DMA bounce buffer area, so it can be
	 * allocated by the isadma driver later.
	 */
	if (!TAILQ_EMPTY(&saved_pgs))
		FREE_PGS(saved_pgs);

	pg = TAILQ_FIRST(&pgs);
	for (i = 0; i < nbuf; i++) {
		/*
		 * First <residual> buffers get (base+1) physical pages
		 * allocated for them.  The rest get (base) physical pages.
		 *
		 * The rest of each buffer occupies virtual space,
		 * but has no physical memory allocated for it.
		 */
		addr = (vaddr_t)buffers + i * MAXBSIZE;
		for (size = PAGE_SIZE * (i < residual ? base + 1 : base);
		    size > 0; size -= PAGE_SIZE, addr += PAGE_SIZE) {
			pmap_kenter_pa(addr, VM_PAGE_TO_PHYS(pg),
			    VM_PROT_READ|VM_PROT_WRITE);
			pg = TAILQ_NEXT(pg, pageq);
		}
	}
	pmap_update(pmap_kernel());
}

/*
 * Set up proc0's TSS and LDT.
 */
void
x86_64_proc0_tss_ldt_init(void)
{
	struct pcb *pcb;
	int x;

	gdt_init();

	cpu_info_primary.ci_curpcb = pcb = &proc0.p_addr->u_pcb;

	pcb->pcb_flags = 0;
	pcb->pcb_tss.tss_iobase =
	    (u_int16_t)((caddr_t)pcb->pcb_iomap - (caddr_t)&pcb->pcb_tss);
	for (x = 0; x < sizeof(pcb->pcb_iomap) / 4; x++)
		pcb->pcb_iomap[x] = 0xffffffff;

	pcb->pcb_ldt_sel = pmap_kernel()->pm_ldt_sel =
	    GSYSSEL(GLDT_SEL, SEL_KPL);
	pcb->pcb_cr0 = rcr0();
	pcb->pcb_tss.tss_rsp0 = (u_int64_t)proc0.p_addr + USPACE - 16;
	pcb->pcb_tss.tss_ist[0] = (u_int64_t)proc0.p_addr + PAGE_SIZE;
	proc0.p_md.md_regs = (struct trapframe *)pcb->pcb_tss.tss_rsp0 - 1;
	proc0.p_md.md_tss_sel = tss_alloc(pcb);

	ltr(proc0.p_md.md_tss_sel);
	lldt(pcb->pcb_ldt_sel);
}

/*       
 * Set up TSS and LDT for a new PCB.
 */         
         
#ifdef MULTIPROCESSOR
void    
x86_64_init_pcb_tss_ldt(ci)   
	struct cpu_info *ci;
{        
	int x;      
	struct pcb *pcb = ci->ci_idle_pcb;
 
	pcb->pcb_tss.tss_iobase =
	    (u_int16_t)((caddr_t)pcb->pcb_iomap - (caddr_t)&pcb->pcb_tss);
	for (x = 0; x < sizeof(pcb->pcb_iomap) / 4; x++)
		pcb->pcb_iomap[x] = 0xffffffff;

	/* XXXfvdl pmap_kernel not needed */ 
	pcb->pcb_ldt_sel = pmap_kernel()->pm_ldt_sel =
	    GSYSSEL(GLDT_SEL, SEL_KPL);
	pcb->pcb_cr0 = rcr0();
        
        ci->ci_idle_tss_sel = tss_alloc(pcb);
}       
#endif	/* MULTIPROCESSOR */

bios_diskinfo_t *
bios_getdiskinfo(dev)
	dev_t dev;
{
	bios_diskinfo_t *pdi;

	if (bios_diskinfo == NULL)
		return NULL;

	for (pdi = bios_diskinfo; pdi->bios_number != -1; pdi++) {
		if ((dev & B_MAGICMASK) == B_DEVMAGIC) { /* search by bootdev */
			if (pdi->bsd_dev == dev)
				break;
		} else {
			if (pdi->bios_number == dev)
				break;
		}
	}

	if (pdi->bios_number == -1)
		return NULL;
	else
		return pdi;
}

int
bios_sysctl(name, namelen, oldp, oldlenp, newp, newlen, p)
	int *name;
	u_int namelen;
	void *oldp;
	size_t *oldlenp;
	void *newp;
	size_t newlen;
	struct proc *p;
{
	bios_diskinfo_t *pdi;
	extern dev_t bootdev;
	int biosdev;

	/* all sysctl names at this level except diskinfo are terminal */
	if (namelen != 1 && name[0] != BIOS_DISKINFO)
		return (ENOTDIR);	       /* overloaded */

	if (!(bootapiver & BAPIV_VECTOR))
		return EOPNOTSUPP;

	switch (name[0]) {
	case BIOS_DEV:
		if ((pdi = bios_getdiskinfo(bootdev)) == NULL)
			return ENXIO;
		biosdev = pdi->bios_number;
		return sysctl_rdint(oldp, oldlenp, newp, biosdev);
	case BIOS_DISKINFO:
		if (namelen != 2)
			return ENOTDIR;
		if ((pdi = bios_getdiskinfo(name[1])) == NULL)
			return ENXIO;
		return sysctl_rdstruct(oldp, oldlenp, newp, pdi, sizeof(*pdi));
	case BIOS_CKSUMLEN:
		return sysctl_rdint(oldp, oldlenp, newp, bios_cksumlen);
	default:
		return EOPNOTSUPP;
	}
	/* NOTREACHED */
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
	dev_t dev;

	switch (name[0]) {
	case CPU_CONSDEV:
		if (namelen != 1)
			return (ENOTDIR);		/* overloaded */
		if (cn_tab != NULL)
			consdev = cn_tab->cn_dev;
		else
			consdev = NODEV;
		return (sysctl_rdstruct(oldp, oldlenp, newp, &consdev,
		    sizeof consdev));
	case CPU_CHR2BLK:
		if (namelen != 2)
			return (ENOTDIR);		/* overloaded */
		dev = chrtoblk((dev_t)name[1]);
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
	case CPU_BIOS:
		return bios_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen, p);
	case CPU_CPUVENDOR:
		return (sysctl_rdstring(oldp, oldlenp, newp, cpu_vendor));
	case CPU_CPUFEATURE:
		return (sysctl_rdint(oldp, oldlenp, newp, cpu_feature));
	case CPU_KBDRESET:
		if (securelevel > 0)
			return (sysctl_rdint(oldp, oldlenp, newp,
			    kbd_reset));
		else
			return (sysctl_int(oldp, oldlenp, newp, newlen,
			    &kbd_reset));
	case CPU_ALLOWAPERTURE:
		if (namelen != 1)
			return (ENOTDIR);		/* overloaded */
#ifdef APERTURE
		if (securelevel > 0)
			return (sysctl_int_lower(oldp, oldlenp, newp, newlen,
			    &allowaperture));
		else
			return (sysctl_int(oldp, oldlenp, newp, newlen,
			    &allowaperture));
#else
		return (sysctl_rdint(oldp, oldlenp, newp, 0));
#endif
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

/*
 * Send an interrupt to process.
 *
 * Stack is set up to allow sigcode stored
 * in u. to call routine, followed by kcall
 * to sigreturn routine below.  After sigreturn
 * resets the signal mask, the stack, and the
 * frame pointer, it returns to the user
 * specified pc, psl.
 */
void
sendsig(sig_t catcher, int sig, int mask, u_long code, int type,
    union sigval val)
{
	struct proc *p = curproc;
	struct trapframe *tf = p->p_md.md_regs;
	struct sigacts * psp = p->p_sigacts;
	struct sigcontext ksc;
	siginfo_t ksi;
	register_t sp, scp, sip;
	u_long sss;

#ifdef DEBUG
	if ((sigdebug & SDB_FOLLOW) && (!sigpid || p->p_pid == sigpid))
		printf("sendsig: %s[%d] sig %d catcher %p\n",
		    p->p_comm, p->p_pid, sig, catcher);
#endif

	bcopy(tf, &ksc, sizeof(*tf));
	ksc.sc_onstack = psp->ps_sigstk.ss_flags & SS_ONSTACK;
	ksc.sc_mask = mask;
	ksc.sc_fpstate = NULL;

	/* Allocate space for the signal handler context. */
	if ((psp->ps_flags & SAS_ALTSTACK) && !ksc.sc_onstack &&
	    (psp->ps_sigonstack & sigmask(sig))) {
		sp = (register_t)psp->ps_sigstk.ss_sp + psp->ps_sigstk.ss_size;
		psp->ps_sigstk.ss_flags |= SS_ONSTACK;
	} else
		sp = tf->tf_rsp - 128;

	sp &= ~15ULL;	/* just in case */
	sss = (sizeof(ksc) + 15) & ~15;

	if (p->p_md.md_flags & MDP_USEDFPU) {
		fpusave_proc(p, 1);
		sp -= sizeof(struct fxsave64);
		ksc.sc_fpstate = (struct fxsave64 *)sp;
		if (copyout(&p->p_addr->u_pcb.pcb_savefpu.fp_fxsave,
		    (void *)sp, sizeof(struct fxsave64)))
			sigexit(p, SIGILL);
	}

	sip = 0;
	if (psp->ps_siginfo & sigmask(sig)) {
		sip = sp - ((sizeof(ksi) + 15) & ~15);
		sss += (sizeof(ksi) + 15) & ~15;

		initsiginfo(&ksi, sig, code, type, val);
		if (copyout(&ksi, (void *)sip, sizeof(ksi)))
			sigexit(p, SIGILL);
	}
	scp = sp - sss;

	if (copyout(&ksc, (void *)scp, sizeof(ksc)))
		sigexit(p, SIGILL);

	/*
	 * Build context to run handler in.
	 */
	tf->tf_ds = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_es = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_fs = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_gs = LSEL(LUDATA_SEL, SEL_UPL);

	tf->tf_rax = (u_int64_t)catcher;
	tf->tf_rdi = sig;
	tf->tf_rsi = sip;
	tf->tf_rdx = scp;

	tf->tf_rip = (u_int64_t)p->p_sigcode;
	tf->tf_cs = LSEL(LUCODE_SEL, SEL_UPL);
	tf->tf_rflags &= ~(PSL_T|PSL_VM|PSL_AC);
	tf->tf_rsp = scp;
	tf->tf_ss = LSEL(LUDATA_SEL, SEL_UPL);

#ifdef DEBUG
	if ((sigdebug & SDB_FOLLOW) && (!sigpid || p->p_pid == sigpid))
		printf("sendsig(%d): pc 0x%x, catcher 0x%x\n", p->p_pid,
		    tf->tf_rip, tf->tf_rax);
#endif
}

/*
 * System call to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by sendsig (above).
 * Return to previous pc and psl as specified by
 * context left by sendsig. Check carefully to
 * make sure that the user has not modified the
 * psl to gain improper privileges or to cause
 * a machine fault.
 */
int
sys_sigreturn(struct proc *p, void *v, register_t *retval)
{
	struct sys_sigreturn_args /* {
		syscallarg(struct sigcontext *) sigcntxp;
	} */ *uap = v;
	struct sigcontext *scp, ksc;
	struct trapframe *tf = p->p_md.md_regs;
	int error;

	scp = SCARG(uap, sigcntxp);
#ifdef DEBUG
	if ((sigdebug & SDB_FOLLOW) && (!sigpid || p->p_pid == sigpid))
		printf("sigreturn: pid %d, scp %p\n", p->p_pid, scp);
#endif
	if ((error = copyin((caddr_t)scp, &ksc, sizeof ksc)))
		return (error);

	if (((ksc.sc_rflags ^ tf->tf_rflags) & PSL_USERSTATIC) != 0 ||
	    !USERMODE(ksc.sc_cs, ksc.sc_eflags))
		return (EINVAL);

	if (p->p_md.md_flags & MDP_USEDFPU)
		fpusave_proc(p, 0);

	if (ksc.sc_fpstate && (error = copyin(ksc.sc_fpstate,
	    &p->p_addr->u_pcb.pcb_savefpu.fp_fxsave, sizeof (struct fxsave64))))
		return (error);

	ksc.sc_trapno = tf->tf_trapno;
	ksc.sc_err = tf->tf_err;
	bcopy(&ksc, tf, sizeof(*tf));

	/* Restore signal stack. */
	if (ksc.sc_onstack)
		p->p_sigacts->ps_sigstk.ss_flags |= SS_ONSTACK;
	else
		p->p_sigacts->ps_sigstk.ss_flags &= ~SS_ONSTACK;
	p->p_sigmask = ksc.sc_mask & ~sigcantmask;

	return (EJUSTRETURN);
}

int	waittime = -1;
struct pcb dumppcb;

void
boot(int howto)
{

	if (cold) {
		/*
		 * If the system is cold, just halt, unless the user
		 * explicitly asked for reboot.
		 */
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

	boothowto = howto;
	if ((howto & RB_NOSYNC) == 0 && waittime < 0) {
		waittime = 0;

		if (curproc == NULL)
			curproc = &proc0;	/* XXX */
		vfs_shutdown();
		/*
		 * If we've been adjusting the clock, the todr
		 * will be out of synch; adjust it now.
		 */
		if ((howto & RB_TIMEBAD) == 0) {
			resettodr();
		} else {
			printf("WARNING: not updating battery clock\n");
		}
	}

	/* Disable interrupts. */
	splhigh();

	/* Do a dump if requested. */
	if (howto & RB_DUMP)
		dumpsys();

haltsys:
	doshutdownhooks();

#ifdef MULTIPROCESSOR
	x86_broadcast_ipi(X86_IPI_HALT);
#endif

	if (howto & RB_HALT) {
#if NACPI > 0
		extern int acpi_s5;

		delay(500000);
		if (howto & RB_POWERDOWN || acpi_s5)
			acpi_powerdown();
#endif
		printf("\n");
		printf("The operating system has halted.\n");
		printf("Please press any key to reboot.\n\n");
		cnpollc(1);	/* for proper keyboard command handling */
		cngetc();
		cnpollc(0);
	}

	printf("rebooting...\n");
	if (cpureset_delay > 0)
		delay(cpureset_delay * 1000);
	cpu_reset();
	for(;;) ;
	/*NOTREACHED*/
}

/*
 * XXXfvdl share dumpcode.
 */

/*
 * These variables are needed by /sbin/savecore
 */
u_int32_t	dumpmag = 0x8fca0101;	/* magic number */
int 	dumpsize = 0;		/* pages */
long	dumplo = 0; 		/* blocks */

/*
 * cpu_dump: dump the machine-dependent kernel core dump headers.
 */
int
cpu_dump()
{
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	char buf[dbtob(1)];
	kcore_seg_t *segp;
	cpu_kcore_hdr_t *cpuhdrp;
	phys_ram_seg_t *memsegp;
	int i;

	dump = bdevsw[major(dumpdev)].d_dump;

	memset(buf, 0, sizeof buf);
	segp = (kcore_seg_t *)buf;
	cpuhdrp = (cpu_kcore_hdr_t *)&buf[ALIGN(sizeof(*segp))];
	memsegp = (phys_ram_seg_t *)&buf[ALIGN(sizeof(*segp)) +
	    ALIGN(sizeof(*cpuhdrp))];

	/*
	 * Generate a segment header.
	 */
	CORE_SETMAGIC(*segp, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	segp->c_size = dbtob(1) - ALIGN(sizeof(*segp));

	/*
	 * Add the machine-dependent header info.
	 */
	cpuhdrp->ptdpaddr = PTDpaddr;
	cpuhdrp->nmemsegs = mem_cluster_cnt;

	/*
	 * Fill in the memory segment descriptors.
	 */
	for (i = 0; i < mem_cluster_cnt; i++) {
		memsegp[i].start = mem_clusters[i].start;
		memsegp[i].size = mem_clusters[i].size & ~PAGE_MASK;
	}

	return (dump(dumpdev, dumplo, (caddr_t)buf, dbtob(1)));
}

/*
 * This is called by main to set dumplo and dumpsize.
 * Dumps always skip the first PAGE_SIZE of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void
dumpconf()
{
	const struct bdevsw *bdev;
	int nblks, dumpblks;	/* size of dump area */

	if (dumpdev == NODEV)
		goto bad;
	bdev = &bdevsw[major(dumpdev)];

	if (bdev == NULL)
		panic("dumpconf: bad dumpdev=0x%x", dumpdev);
	if (bdev->d_psize == NULL)
		goto bad;
	nblks = (*bdev->d_psize)(dumpdev);
	if (nblks <= ctod(1))
		goto bad;

	dumpblks = cpu_dumpsize();
	if (dumpblks < 0)
		goto bad;
	dumpblks += ctod(cpu_dump_mempagecnt());

	/* If dump won't fit (incl. room for possible label), punt. */
	if (dumpblks > (nblks - ctod(1)))
		goto bad;

	/* Put dump at end of partition */
	dumplo = nblks - dumpblks;

	/* dumpsize is in page units, and doesn't include headers. */
	dumpsize = cpu_dump_mempagecnt();
	return;

 bad:
	dumpsize = 0;
}

/*
 * Doadump comes here after turning off memory management and
 * getting on the dump stack, either when called above, or by
 * the auto-restart code.
 */
#define BYTES_PER_DUMP  PAGE_SIZE /* must be a multiple of pagesize XXX small */
static vaddr_t dumpspace;

vaddr_t
reserve_dumppages(vaddr_t p)
{

	dumpspace = p;
	return (p + BYTES_PER_DUMP);
}

void
dumpsys()
{
	u_long totalbytesleft, bytes, i, n, memseg;
	u_long maddr;
	daddr_t blkno;
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	int error;

	/* Save registers. */
	savectx(&dumppcb);

	if (dumpdev == NODEV)
		return;

	/*
	 * For dumps during autoconfiguration,
	 * if dump device has already configured...
	 */
	if (dumpsize == 0)
		dumpconf();
	if (dumplo <= 0 || dumpsize == 0) {
		printf("\ndump to dev %u,%u not possible\n", major(dumpdev),
		    minor(dumpdev));
		return;
	}
	printf("\ndumping to dev %u,%u offset %ld\n", major(dumpdev),
	    minor(dumpdev), dumplo);

	error = (*bdevsw[major(dumpdev)].d_psize)(dumpdev);
	printf("dump ");
	if (error == -1) {
		printf("area unavailable\n");
		return;
	}

	if ((error = cpu_dump()) != 0)
		goto err;

	totalbytesleft = ptoa(cpu_dump_mempagecnt());
	blkno = dumplo + cpu_dumpsize();
	dump = bdevsw[major(dumpdev)].d_dump;
	error = 0;

	for (memseg = 0; memseg < mem_cluster_cnt; memseg++) {
		maddr = mem_clusters[memseg].start;
		bytes = mem_clusters[memseg].size;

		for (i = 0; i < bytes; i += n, totalbytesleft -= n) {
			/* Print out how many MBs we have left to go. */
			if ((totalbytesleft % (1024*1024)) == 0)
				printf("%ld ", totalbytesleft / (1024 * 1024));

			/* Limit size for next transfer. */
			n = bytes - i;
			if (n > BYTES_PER_DUMP)
				n = BYTES_PER_DUMP;

			(void) pmap_map(dumpspace, maddr, maddr + n,
			    VM_PROT_READ);

			error = (*dump)(dumpdev, blkno, (caddr_t)dumpspace, n);
			if (error)
				goto err;
			maddr += n;
			blkno += btodb(n);		/* XXX? */

#if 0	/* XXX this doesn't work.  grr. */
			/* operator aborting dump? */
			if (sget() != NULL) {
				error = EINTR;
				break;
			}
#endif
		}
	}

 err:
	switch (error) {

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

	case 0:
		printf("succeeded\n");
		break;

	default:
		printf("error %d\n", error);
		break;
	}
	printf("\n\n");
	delay(5000000);		/* 5 seconds */
}

/*
 * Clear registers on exec
 */
void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    register_t *retval)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct trapframe *tf;

	/* If we were using the FPU, forget about it. */
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p, 0);

#ifdef USER_LDT
	pmap_ldt_cleanup(p);
#endif

	p->p_md.md_flags &= ~MDP_USEDFPU;
	pcb->pcb_flags = 0;
	pcb->pcb_savefpu.fp_fxsave.fx_fcw = __OpenBSD_NPXCW__;
	pcb->pcb_savefpu.fp_fxsave.fx_mxcsr = __INITIAL_MXCSR__;
	pcb->pcb_savefpu.fp_fxsave.fx_mxcsr_mask = __INITIAL_MXCSR_MASK__;

	tf = p->p_md.md_regs;
	tf->tf_ds = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_es = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_fs = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_gs = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_rdi = 0;
	tf->tf_rsi = 0;
	tf->tf_rbp = 0;
	tf->tf_rbx = 0;
	tf->tf_rdx = 0;
	tf->tf_rcx = 0;
	tf->tf_rax = 0;
	tf->tf_rip = pack->ep_entry;
	tf->tf_cs = LSEL(LUCODE_SEL, SEL_UPL);
	tf->tf_rflags = PSL_USERSET;
	tf->tf_rsp = stack;
	tf->tf_ss = LSEL(LUDATA_SEL, SEL_UPL);

	retval[1] = 0;
}

/*
 * Initialize segments and descriptor tables
 */

struct gate_descriptor *idt;
char idt_allocmap[NIDT];
struct simplelock idt_lock;
char *ldtstore;
char *gdtstore;
extern  struct user *proc0paddr;

void
setgate(gd, func, ist, type, dpl, sel)
	struct gate_descriptor *gd;
	void *func;
	int ist, type, dpl, sel;
{
	gd->gd_looffset = (u_int64_t)func & 0xffff;
	gd->gd_selector = sel;
	gd->gd_ist = ist;
	gd->gd_type = type;
	gd->gd_dpl = dpl;
	gd->gd_p = 1;
	gd->gd_hioffset = (u_int64_t)func >> 16;
	gd->gd_zero = 0;
	gd->gd_xx1 = 0;
	gd->gd_xx2 = 0;
	gd->gd_xx3 = 0;
}

void
unsetgate(gd)
	struct gate_descriptor *gd;
{
	memset(gd, 0, sizeof (*gd));
}

void
setregion(rd, base, limit)
	struct region_descriptor *rd;
	void *base;
	u_int16_t limit;
{
	rd->rd_limit = limit;
	rd->rd_base = (u_int64_t)base;
}

/*
 * Note that the base and limit fields are ignored in long mode.
 */
void
set_mem_segment(sd, base, limit, type, dpl, gran, def32, is64)
	struct mem_segment_descriptor *sd;
	void *base;
	size_t limit;
	int type, dpl, gran, is64;
{
	sd->sd_lolimit = (unsigned)limit;
	sd->sd_lobase = (unsigned long)base;
	sd->sd_type = type;
	sd->sd_dpl = dpl;
	sd->sd_p = 1;
	sd->sd_hilimit = (unsigned)limit >> 16;
	sd->sd_avl = 0;
	sd->sd_long = is64;
	sd->sd_def32 = def32;
	sd->sd_gran = gran;
	sd->sd_hibase = (unsigned long)base >> 24;
}

void
set_sys_segment(sd, base, limit, type, dpl, gran)
	struct sys_segment_descriptor *sd;
	void *base;
	size_t limit;
	int type, dpl, gran;
{
	memset(sd, 0, sizeof *sd);
	sd->sd_lolimit = (unsigned)limit;
	sd->sd_lobase = (u_int64_t)base;
	sd->sd_type = type;
	sd->sd_dpl = dpl;
	sd->sd_p = 1;
	sd->sd_hilimit = (unsigned)limit >> 16;
	sd->sd_gran = gran;
	sd->sd_hibase = (u_int64_t)base >> 24;
}

void cpu_init_idt()
{
	struct region_descriptor region;

	setregion(&region, idt, NIDT * sizeof(idt[0]) - 1);
	lidt(&region); 
}


#define	IDTVEC(name)	__CONCAT(X, name)
typedef void (vector)(void);
extern vector IDTVEC(syscall);
extern vector IDTVEC(syscall32);
extern vector IDTVEC(osyscall);
extern vector IDTVEC(oosyscall);
extern vector *IDTVEC(exceptions)[];

#define	KBTOB(x)	((size_t)(x) * 1024UL)

void
init_x86_64(first_avail)
	paddr_t first_avail;
{
	extern void consinit(void);
	extern struct extent *iomem_ex;
	struct region_descriptor region;
	struct mem_segment_descriptor *ldt_segp;
	int x, first16q, ist;
	u_int64_t seg_start, seg_end;
	u_int64_t seg_start1, seg_end1;

	cpu_init_msrs(&cpu_info_primary);

	proc0.p_addr = proc0paddr;
	cpu_info_primary.ci_curpcb = &proc0.p_addr->u_pcb;

	x86_bus_space_init();

	consinit();	/* XXX SHOULD NOT BE DONE HERE */

	/*
	 * Initailize PAGE_SIZE-dependent variables.
	 */
	uvm_setpagesize();

#if 0
	uvmexp.ncolors = 2;
#endif
 
	/*
	 * Boot arguments are in a single page specified by /boot.
	 *
	 * We require the "new" vector form, as well as memory ranges
	 * to be given in bytes rather than KB.
	 *
	 * locore copies the data into bootinfo[] for us.
	 */
	if ((bootapiver & (BAPIV_VECTOR | BAPIV_BMEMMAP)) ==
	    (BAPIV_VECTOR | BAPIV_BMEMMAP)) {
		if (bootinfo_size >= sizeof(bootinfo))
			panic("boot args too big");

		getbootinfo(bootinfo, bootinfo_size);
	} else
		panic("invalid /boot");

	avail_start = PAGE_SIZE; /* BIOS leaves data in low memory */
				 /* and VM system doesn't work with phys 0 */
#ifdef MULTIPROCESSOR
	if (avail_start < MP_TRAMPOLINE + PAGE_SIZE)
		avail_start = MP_TRAMPOLINE + PAGE_SIZE;
#endif

	/*
	 * Call pmap initialization to make new kernel address space.
	 * We must do this before loading pages into the VM system.
	 */
	pmap_bootstrap(VM_MIN_KERNEL_ADDRESS,
	    IOM_END + trunc_page(KBTOB(biosextmem)));

	if (avail_start != PAGE_SIZE)
		pmap_prealloc_lowmem_ptps();

	if (mem_cluster_cnt == 0) {
		/*
		 * Allocate the physical addresses used by RAM from the iomem
		 * extent map.  This is done before the addresses are
		 * page rounded just to make sure we get them all.
		 */
		if (extent_alloc_region(iomem_ex, 0, KBTOB(biosbasemem),
		    EX_NOWAIT)) {
			/* XXX What should we do? */
			printf("WARNING: CAN'T ALLOCATE BASE MEMORY FROM "
			    "IOMEM EXTENT MAP!\n");
		}
		mem_clusters[0].start = 0;
		mem_clusters[0].size = trunc_page(KBTOB(biosbasemem));
		physmem += atop(mem_clusters[0].size);
		if (extent_alloc_region(iomem_ex, IOM_END, KBTOB(biosextmem),
		    EX_NOWAIT)) {
			/* XXX What should we do? */
			printf("WARNING: CAN'T ALLOCATE EXTENDED MEMORY FROM "
			    "IOMEM EXTENT MAP!\n");
		}
#if 0
#if NISADMA > 0
		/*
		 * Some motherboards/BIOSes remap the 384K of RAM that would
		 * normally be covered by the ISA hole to the end of memory
		 * so that it can be used.  However, on a 16M system, this
		 * would cause bounce buffers to be allocated and used.
		 * This is not desirable behaviour, as more than 384K of
		 * bounce buffers might be allocated.  As a work-around,
		 * we round memory down to the nearest 1M boundary if
		 * we're using any isadma devices and the remapped memory
		 * is what puts us over 16M.
		 */
		if (biosextmem > (15*1024) && biosextmem < (16*1024)) {
			char pbuf[9];

			format_bytes(pbuf, sizeof(pbuf),
			    biosextmem - (15*1024));
			printf("Warning: ignoring %s of remapped memory\n",
			    pbuf);
			biosextmem = (15*1024);
		}
#endif
#endif
		mem_clusters[1].start = IOM_END;
		mem_clusters[1].size = trunc_page(KBTOB(biosextmem));
		physmem += atop(mem_clusters[1].size);

		mem_cluster_cnt = 2;

		avail_end = IOM_END + trunc_page(KBTOB(biosextmem));
	}

	/*
	 * If we have 16M of RAM or less, just put it all on
	 * the default free list.  Otherwise, put the first
	 * 16M of RAM on a lower priority free list (so that
	 * all of the ISA DMA'able memory won't be eaten up
	 * first-off).
	 */
	if (avail_end <= (16 * 1024 * 1024))
		first16q = VM_FREELIST_DEFAULT;
	else
		first16q = VM_FREELIST_FIRST16;

	/* Make sure the end of the space used by the kernel is rounded. */
	first_avail = round_page(first_avail);
	kern_end = KERNBASE + first_avail;

	/*
	 * Now, load the memory clusters (which have already been
	 * rounded and truncated) into the VM system.
	 *
	 * NOTE: WE ASSUME THAT MEMORY STARTS AT 0 AND THAT THE KERNEL
	 * IS LOADED AT IOM_END (1M).
	 */
	for (x = 0; x < mem_cluster_cnt; x++) {
		seg_start = mem_clusters[x].start;
		seg_end = mem_clusters[x].start + mem_clusters[x].size;
		seg_start1 = 0;
		seg_end1 = 0;

		if (seg_start > 0xffffffffULL) {
			printf("skipping %lld bytes of memory above 4GB\n",
			    seg_end - seg_start);
			continue;
		}
		if (seg_end > 0x100000000ULL) {
			printf("skipping %lld bytes of memory above 4GB\n",
			    seg_end - 0x100000000ULL);
			seg_end = 0x100000000ULL;
		}

		/*
		 * Skip memory before our available starting point.
		 */
		if (seg_end <= avail_start)
			continue;

		if (avail_start >= seg_start && avail_start < seg_end) {
			if (seg_start != 0)
				panic("init_x86_64: memory doesn't start at 0");
			seg_start = avail_start;
			if (seg_start == seg_end)
				continue;
		}

		/*
		 * If this segment contains the kernel, split it
		 * in two, around the kernel.
		 */
		if (seg_start <= IOM_END && first_avail <= seg_end) {
			seg_start1 = first_avail;
			seg_end1 = seg_end;
			seg_end = IOM_END;
		}

		/* First hunk */
		if (seg_start != seg_end) {
			if (seg_start <= (16 * 1024 * 1024) &&
			    first16q != VM_FREELIST_DEFAULT) {
				u_int64_t tmp;

				if (seg_end > (16 * 1024 * 1024))
					tmp = (16 * 1024 * 1024);
				else
					tmp = seg_end;
#if DEBUG_MEMLOAD
				printf("loading 0x%qx-0x%qx (0x%lx-0x%lx)\n",
				    (unsigned long long)seg_start,
				    (unsigned long long)tmp,
				    atop(seg_start), atop(tmp));
#endif
				uvm_page_physload(atop(seg_start),
				    atop(tmp), atop(seg_start),
				    atop(tmp), first16q);
				seg_start = tmp;
			}

			if (seg_start != seg_end) {
#if DEBUG_MEMLOAD
				printf("loading 0x%qx-0x%qx (0x%lx-0x%lx)\n",
				    (unsigned long long)seg_start,
				    (unsigned long long)seg_end,
				    atop(seg_start), atop(seg_end));
#endif
				uvm_page_physload(atop(seg_start),
				    atop(seg_end), atop(seg_start),
				    atop(seg_end), VM_FREELIST_DEFAULT);
			}
		}

		/* Second hunk */
		if (seg_start1 != seg_end1) {
			if (seg_start1 <= (16 * 1024 * 1024) &&
			    first16q != VM_FREELIST_DEFAULT) {
				u_int64_t tmp;

				if (seg_end1 > (16 * 1024 * 1024))
					tmp = (16 * 1024 * 1024);
				else
					tmp = seg_end1;
#if DEBUG_MEMLOAD
				printf("loading 0x%qx-0x%qx (0x%lx-0x%lx)\n",
				    (unsigned long long)seg_start1,
				    (unsigned long long)tmp,
				    atop(seg_start1), atop(tmp));
#endif
				uvm_page_physload(atop(seg_start1),
				    atop(tmp), atop(seg_start1),
				    atop(tmp), first16q);
				seg_start1 = tmp;
			}

			if (seg_start1 != seg_end1) {
#if DEBUG_MEMLOAD
				printf("loading 0x%qx-0x%qx (0x%lx-0x%lx)\n",
				    (unsigned long long)seg_start1,
				    (unsigned long long)seg_end1,
				    atop(seg_start1), atop(seg_end1));
#endif
				uvm_page_physload(atop(seg_start1),
				    atop(seg_end1), atop(seg_start1),
				    atop(seg_end1), VM_FREELIST_DEFAULT);
			}
		}
	}

	/*
	 * Steal memory for the message buffer (at end of core).
	 */
	{
		struct vm_physseg *vps = NULL;
		psize_t sz = round_page(MSGBUFSIZE);
		psize_t reqsz = sz;

		for (x = 0; x < vm_nphysseg; x++) {
			vps = &vm_physmem[x];
			if (ptoa(vps->avail_end) == avail_end)
				break;
		}
		if (x == vm_nphysseg)
			panic("init_x86_64: can't find end of memory");

		/* Shrink so it'll fit in the last segment. */
		if ((vps->avail_end - vps->avail_start) < atop(sz))
			sz = ptoa(vps->avail_end - vps->avail_start);

		vps->avail_end -= atop(sz);
		vps->end -= atop(sz);
		msgbuf_paddr = ptoa(vps->avail_end);

		/* Remove the last segment if it now has no pages. */
		if (vps->start == vps->end) {
			for (vm_nphysseg--; x < vm_nphysseg; x++)
				vm_physmem[x] = vm_physmem[x + 1];
		}

		/* Now find where the new avail_end is. */
		for (avail_end = 0, x = 0; x < vm_nphysseg; x++)
			if (vm_physmem[x].avail_end > avail_end)
				avail_end = vm_physmem[x].avail_end;
		avail_end = ptoa(avail_end);

		/* Warn if the message buffer had to be shrunk. */
		if (sz != reqsz)
			printf("WARNING: %ld bytes not available for msgbuf "
			    "in last cluster (%ld used)\n", reqsz, sz);
	}

	/*
	 * XXXfvdl todo: acpi wakeup code.
	 */

	pmap_growkernel(VM_MIN_KERNEL_ADDRESS + 32 * 1024 * 1024);

	pmap_kenter_pa(idt_vaddr, idt_paddr, VM_PROT_READ|VM_PROT_WRITE);
	pmap_kenter_pa(idt_vaddr + PAGE_SIZE, idt_paddr + PAGE_SIZE,
	    VM_PROT_READ|VM_PROT_WRITE);

	pmap_kenter_pa(lo32_vaddr, lo32_paddr, VM_PROT_READ|VM_PROT_WRITE);

	idt = (struct gate_descriptor *)idt_vaddr;
	gdtstore = (char *)(idt + NIDT);
	ldtstore = gdtstore + DYNSEL_START;

	/* make gdt gates and memory segments */
	set_mem_segment(GDT_ADDR_MEM(gdtstore, GCODE_SEL), 0, 0xfffff, SDT_MEMERA,
	    SEL_KPL, 1, 0, 1);

	set_mem_segment(GDT_ADDR_MEM(gdtstore, GDATA_SEL), 0, 0xfffff, SDT_MEMRWA,
	    SEL_KPL, 1, 0, 1);

	set_sys_segment(GDT_ADDR_SYS(gdtstore, GLDT_SEL), ldtstore, LDT_SIZE - 1,
	    SDT_SYSLDT, SEL_KPL, 0);

	set_mem_segment(GDT_ADDR_MEM(gdtstore, GUCODE_SEL), 0,
	    atop(VM_MAXUSER_ADDRESS) - 1, SDT_MEMERA, SEL_UPL, 1, 0, 1);

	set_mem_segment(GDT_ADDR_MEM(gdtstore, GUDATA_SEL), 0,
	    atop(VM_MAXUSER_ADDRESS) - 1, SDT_MEMRWA, SEL_UPL, 1, 0, 1);

	/* make ldt gates and memory segments */
	setgate((struct gate_descriptor *)(ldtstore + LSYS5CALLS_SEL),
	    &IDTVEC(oosyscall), 0, SDT_SYS386CGT, SEL_UPL,
	    GSEL(GCODE_SEL, SEL_KPL));

	*(struct mem_segment_descriptor *)(ldtstore + LUCODE_SEL) =
	    *GDT_ADDR_MEM(gdtstore, GUCODE_SEL);
	*(struct mem_segment_descriptor *)(ldtstore + LUDATA_SEL) =
	    *GDT_ADDR_MEM(gdtstore, GUDATA_SEL);

	/*
	 * 32 bit GDT entries.
	 */

	set_mem_segment(GDT_ADDR_MEM(gdtstore, GUCODE32_SEL), 0,
	    atop(VM_MAXUSER_ADDRESS) - 1, SDT_MEMERA, SEL_UPL, 1, 1, 0);

	set_mem_segment(GDT_ADDR_MEM(gdtstore, GUDATA32_SEL), 0,
	    atop(VM_MAXUSER_ADDRESS) - 1, SDT_MEMRWA, SEL_UPL, 1, 1, 0);

	/*
	 * 32 bit LDT entries.
	 */
	ldt_segp = (struct mem_segment_descriptor *)(ldtstore + LUCODE32_SEL);
	set_mem_segment(ldt_segp, 0, atop(VM_MAXUSER_ADDRESS32) - 1,
	    SDT_MEMERA, SEL_UPL, 1, 1, 0);
	ldt_segp = (struct mem_segment_descriptor *)(ldtstore + LUDATA32_SEL);
	set_mem_segment(ldt_segp, 0, atop(VM_MAXUSER_ADDRESS32) - 1,
	    SDT_MEMRWA, SEL_UPL, 1, 1, 0);

	/*
	 * Other entries.
	 */
	memcpy((struct gate_descriptor *)(ldtstore + LSOL26CALLS_SEL),
	    (struct gate_descriptor *)(ldtstore + LSYS5CALLS_SEL),
	    sizeof (struct gate_descriptor));
	memcpy((struct gate_descriptor *)(ldtstore + LBSDICALLS_SEL),
	    (struct gate_descriptor *)(ldtstore + LSYS5CALLS_SEL),
	    sizeof (struct gate_descriptor));

	/* exceptions */
	for (x = 0; x < 32; x++) {
		ist = (x == 8) ? 1 : 0;
		setgate(&idt[x], IDTVEC(exceptions)[x], ist, SDT_SYS386IGT,
		    (x == 3 || x == 4) ? SEL_UPL : SEL_KPL,
		    GSEL(GCODE_SEL, SEL_KPL));
		idt_allocmap[x] = 1;
	}

	/* new-style interrupt gate for syscalls */
	setgate(&idt[128], &IDTVEC(osyscall), 0, SDT_SYS386IGT, SEL_UPL,
	    GSEL(GCODE_SEL, SEL_KPL));
	idt_allocmap[128] = 1;

	setregion(&region, gdtstore, DYNSEL_START - 1);
	lgdt(&region);

	cpu_init_idt();

#ifdef DDB
	db_machine_init();
	ddb_init();
	if (boothowto & RB_KDB)
		Debugger();
#endif
#ifdef KGDB
	kgdb_port_init();
	if (boothowto & RB_KDB) {
		kgdb_debug_init = 1;
		kgdb_connect(1);
	}
#endif

	intr_default_setup();

	softintr_init();
	splraise(IPL_IPI);
	enable_intr();

        /* Make sure maxproc is sane */ 
        if (maxproc > cpu_maxproc())
                maxproc = cpu_maxproc();
}

#ifdef KGDB
void
kgdb_port_init()
{
#if NCOM > 0
	if (!strcmp(kgdb_devname, "com")) {
		bus_space_tag_t tag = X86_BUS_SPACE_IO;
		com_kgdb_attach(tag, comkgdbaddr, comkgdbrate, COM_FREQ,
		    comkgdbmode);
	}
#endif
} 
#endif /* KGDB */

void
cpu_reset()
{

	disable_intr();

	/*
	 * The keyboard controller has 4 random output pins, one of which is
	 * connected to the RESET pin on the CPU in many PCs.  We tell the
	 * keyboard controller to pulse this line a couple of times.
	 */
	outb(IO_KBD + KBCMDP, KBC_PULSE0);
	delay(100000);
	outb(IO_KBD + KBCMDP, KBC_PULSE0);
	delay(100000);

	/*
	 * Try to cause a triple fault and watchdog reset by making the IDT
	 * invalid and causing a fault.
	 */
	memset((caddr_t)idt, 0, NIDT * sizeof(idt[0]));
	__asm __volatile("divl %0,%1" : : "q" (0), "a" (0)); 

#if 0
	/*
	 * Try to cause a triple fault and watchdog reset by unmapping the
	 * entire address space and doing a TLB flush.
	 */
	memset((caddr_t)PTD, 0, PAGE_SIZE);
	tlbflush(); 
#endif

	for (;;);
}

/*
 * cpu_dumpsize: calculate size of machine-dependent kernel core dump headers.
 */
int
cpu_dumpsize()
{
	int size;

	size = ALIGN(sizeof(kcore_seg_t)) +
	    ALIGN(mem_cluster_cnt * sizeof(phys_ram_seg_t));
	if (roundup(size, dbtob(1)) != dbtob(1))
		return (-1);

	return (1);
}

/*
 * cpu_dump_mempagecnt: calculate the size of RAM (in pages) to be dumped.
 */
u_long
cpu_dump_mempagecnt()
{
	u_long i, n;

	n = 0;
	for (i = 0; i < mem_cluster_cnt; i++)
		n += atop(mem_clusters[i].size);
	return (n);
}

void
cpu_initclocks(void)
{
	(*initclock_func)();
}

void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;
	if ((ci)->ci_curproc != NULL)
		aston((ci)->ci_curproc);
}

/*
 * Allocate an IDT vector slot within the given range.
 * XXX needs locking to avoid MP allocation races.
 * XXXfvdl share idt code
 */

int
idt_vec_alloc(low, high)
	int low;
	int high;
{
	int vec;

	simple_lock(&idt_lock);
	for (vec = low; vec <= high; vec++) {
		if (idt_allocmap[vec] == 0) {
			idt_allocmap[vec] = 1;
			simple_unlock(&idt_lock);
			return vec;
		}
	}
	simple_unlock(&idt_lock);
	return 0;
}

void
idt_vec_set(vec, function)
	int vec;
	void (*function)(void);
{
	/*
	 * Vector should be allocated, so no locking needed.
	 */
	KASSERT(idt_allocmap[vec] == 1);
	setgate(&idt[vec], function, 0, SDT_SYS386IGT, SEL_KPL,
	    GSEL(GCODE_SEL, SEL_KPL));
}

void
idt_vec_free(vec)
	int vec;
{
	simple_lock(&idt_lock);
	unsetgate(&idt[vec]);
	idt_allocmap[vec] = 0;
	simple_unlock(&idt_lock);
}

/*
 * Number of processes is limited by number of available GDT slots.
 */
int
cpu_maxproc(void)
{
#ifdef USER_LDT
	return ((MAXGDTSIZ - DYNSEL_START) / 32);
#else
	return (MAXGDTSIZ - DYNSEL_START) / 16;
#endif
}

#ifdef DIAGNOSTIC
void
splassert_check(int wantipl, const char *func)
{
	int cpl = curcpu()->ci_ilevel;

	if (cpl < wantipl) {
		splassert_fail(wantipl, cpl, func);
	}
}
#endif

void
getbootinfo(char *bootinfo, int bootinfo_size)
{
	bootarg32_t *q;

#undef BOOTINFO_DEBUG
#ifdef BOOTINFO_DEBUG
	printf("bootargv:");
#endif

	for (q = (bootarg32_t *)bootinfo;
	    (q->ba_type != BOOTARG_END) &&
	    ((((char *)q) - bootinfo) < bootinfo_size);
	    q = (bootarg32_t *)(((char *)q) + q->ba_size)) {

		switch (q->ba_type) {
		case BOOTARG_MEMMAP:
			bios_memmap = (bios_memmap_t *)q->ba_arg;
#ifdef BOOTINFO_DEBUG
			printf(" memmap %p", bios_memmap);
#endif
			break;
		case BOOTARG_DISKINFO:
			bios_diskinfo = (bios_diskinfo_t *)q->ba_arg;
#ifdef BOOTINFO_DEBUG
			printf(" diskinfo %p", bios_diskinfo);
#endif
			break;
#if 0
#if NAPM > 0 || defined(DEBUG)
		case BOOTARG_APMINFO:
#ifdef BOOTINFO_DEBUG
			printf(" apminfo %p", q->ba_arg);
#endif
			apm = (bios_apminfo_t *)q->ba_arg;
			break;
#endif
#endif
		case BOOTARG_CKSUMLEN:
			bios_cksumlen = *(u_int32_t *)q->ba_arg;
#ifdef BOOTINFO_DEBUG
			printf(" cksumlen %d", bios_cksumlen);
#endif
			break;
#if 0
#if NPCI > 0
		case BOOTARG_PCIINFO:
			bios_pciinfo = (bios_pciinfo_t *)q->ba_arg;
#ifdef BOOTINFO_DEBUG
			printf(" pciinfo %p", bios_pciinfo);
#endif
			break;
#endif
#endif
		case BOOTARG_CONSDEV:
			if (q->ba_size >= sizeof(bios_consdev_t))
			{
				bios_consdev_t *cdp =
				    (bios_consdev_t*)q->ba_arg;
#include "com.h"
#if NCOM > 0
				extern int comdefaultrate; /* ic/com.c */
				comdefaultrate = cdp->conspeed;
#endif
#ifdef BOOTINFO_DEBUG
				printf(" console 0x%x:%d",
				    cdp->consdev, cdp->conspeed);
#endif
				cnset(cdp->consdev);
			}
			break;
#ifdef NFSCLIENT
		case BOOTARG_BOOTMAC:
			bios_bootmac = (bios_bootmac_t *)q->ba_arg;
			break;
#endif                 

		default:
#ifdef BOOTINFO_DEBUG
			printf(" unsupported arg (%d) %p", q->ba_type,
			    q->ba_arg);
#endif
			break;
		}
	}
#ifdef BOOTINFO_DEBUG
	printf("\n");
#endif
}

int
check_context(const struct reg *regs, struct trapframe *tf)
{
	uint16_t sel;

	if (((regs->r_rflags ^ tf->tf_rflags) & PSL_USERSTATIC) != 0)
		return EINVAL;

	sel = regs->r_es & 0xffff;
	if (sel != 0 && !VALID_USER_DSEL(sel))
		return EINVAL;

	sel = regs->r_fs & 0xffff;
	if (sel != 0 && !VALID_USER_DSEL(sel))
		return EINVAL;

	sel = regs->r_gs & 0xffff;
	if (sel != 0 && !VALID_USER_DSEL(sel))
		return EINVAL;

	sel = regs->r_ds & 0xffff;
	if (!VALID_USER_DSEL(sel))
		return EINVAL;

	sel = regs->r_ss & 0xffff;
	if (!VALID_USER_DSEL(sel)) 
		return EINVAL;

	sel = regs->r_cs & 0xffff;
	if (!VALID_USER_CSEL(sel))
		return EINVAL;

	if (regs->r_rip >= VM_MAXUSER_ADDRESS)
		return EINVAL;

	return 0;
}
