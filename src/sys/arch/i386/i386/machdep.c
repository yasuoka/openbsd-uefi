/*	$OpenBSD: machdep.c,v 1.287 2004/03/26 04:00:59 drahn Exp $	*/
/*	$NetBSD: machdep.c,v 1.214 1996/11/10 03:16:17 thorpej Exp $	*/

/*-
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * Copyright (c) 1993, 1994, 1995, 1996 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1992 Terrence R. Lambert.
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
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/exec.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/timeout.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/msgbuf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/sysctl.h>
#include <sys/syscallargs.h>
#include <sys/core.h>
#include <sys/kcore.h>
#ifdef SYSVMSG
#include <sys/msg.h>
#endif

#ifdef CRYPTO
#include <crypto/cryptodev.h>
#include <crypto/rijndael.h>
#endif

#ifdef KGDB
#include <sys/kgdb.h>
#endif

#include <dev/cons.h>
#include <stand/boot/bootarg.h>

#include <uvm/uvm_extern.h>

#define _I386_BUS_DMA_PRIVATE
#include <machine/bus.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/gdt.h>
#include <machine/pio.h>
#include <machine/bus.h>
#include <machine/psl.h>
#include <machine/reg.h>
#include <machine/specialreg.h>
#include <machine/biosvar.h>

#include <dev/rndvar.h>
#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/ic/i8042reg.h>
#include <dev/ic/mc146818reg.h>
#include <i386/isa/isa_machdep.h>
#include <i386/isa/nvram.h>

#include "apm.h"
#if NAPM > 0
#include <machine/apmvar.h>
#endif

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#endif

#ifdef VM86
#include <machine/vm86.h>
#endif

#include "isa.h"
#include "isadma.h"
#include "npx.h"
#if NNPX > 0
extern struct proc *npxproc;
#endif

#include "bios.h"
#include "com.h"
#include "pccom.h"

#if NPCCOM > 0
#include <sys/termios.h>
#include <dev/ic/comreg.h>
#if NCOM > 0
#include <dev/ic/comvar.h>
#elif NPCCOM > 0
#include <arch/i386/isa/pccomvar.h>
#endif
#endif /* NCOM > 0 || NPCCOM > 0 */

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

/* the following is used externally (sysctl_hw) */
char machine[] = "i386";		/* cpu "architecture" */
char machine_arch[] = "i386";		/* machine == machine_arch */

/*
 * Declare these as initialized data so we can patch them.
 */
#if NAPM > 0
int	cpu_apmhalt = 0;	/* sysctl'd to 1 for halt -p hack */
#endif

#ifdef USER_LDT
int	user_ldt_enable = 0;	/* sysctl'd to 1 to enable */
#endif

#ifdef	NBUF
int	nbuf = NBUF;
#else
int	nbuf = 0;
#endif

#ifndef BUFCACHEPERCENT
#define BUFCACHEPERCENT 5
#endif

#ifdef	BUFPAGES
int	bufpages = BUFPAGES;
#else
int	bufpages = 0;
#endif
int	bufcachepercent = BUFCACHEPERCENT;

extern int	boothowto;
int	physmem;

struct dumpmem {
	paddr_t	start;
	paddr_t	end;
} dumpmem[VM_PHYSSEG_MAX];
u_int ndumpmem;

/*
 * These variables are needed by /sbin/savecore
 */
u_long	dumpmag = 0x8fca0101;	/* magic number */
int 	dumpsize = 0;		/* pages */
long	dumplo = 0; 		/* blocks */

int	cpu_class;
int	i386_fpu_present;
int	i386_fpu_exception;
int	i386_fpu_fdivbug;

int	i386_use_fxsave;
int	i386_has_sse;
int	i386_has_sse2;
int	i386_has_xcrypt;

bootarg_t *bootargp;
paddr_t avail_end;

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

int kbd_reset;
int p4_model;
int setperf_prio = 0;		/* for concurrent handlers */

/*
 * Extent maps to manage I/O and ISA memory hole space.  Allocate
 * storage for 8 regions in each, initially.  Later, ioport_malloc_safe
 * will indicate that it's safe to use malloc() to dynamically allocate
 * region descriptors.
 *
 * N.B. At least two regions are _always_ allocated from the iomem
 * extent map; (0 -> ISA hole) and (end of ISA hole -> end of RAM).
 *
 * The extent maps are not static!  Machine-dependent ISA and EISA
 * routines need access to them for bus address space allocation.
 */
static	long ioport_ex_storage[EXTENT_FIXED_STORAGE_SIZE(16) / sizeof(long)];
static	long iomem_ex_storage[EXTENT_FIXED_STORAGE_SIZE(16) / sizeof(long)];
struct	extent *ioport_ex;
struct	extent *iomem_ex;
static	int ioport_malloc_safe;

caddr_t	allocsys(caddr_t);
void	setup_buffers(vaddr_t *);
void	dumpsys(void);
int	cpu_dump(void);
void	identifycpu(void);
void	init386(paddr_t);
void	consinit(void);
void	(*cpuresetfn)(void);

int	bus_mem_add_mapping(bus_addr_t, bus_size_t,
	    int, bus_space_handle_t *);
int	_bus_dmamap_load_buffer(bus_dma_tag_t, bus_dmamap_t, void *,
    bus_size_t, struct proc *, int, paddr_t *, int *, int);

#ifdef KGDB
#ifndef KGDB_DEVNAME
#ifdef __i386__
#define KGDB_DEVNAME "pccom"
#else
#define KGDB_DEVNAME "com"
#endif
#endif /* KGDB_DEVNAME */
char kgdb_devname[] = KGDB_DEVNAME;
#if (NCOM > 0 || NPCCOM > 0)
#ifndef KGDBADDR
#define KGDBADDR 0x3f8
#endif
int comkgdbaddr = KGDBADDR;
#ifndef KGDBRATE
#define KGDBRATE TTYDEF_SPEED
#endif
int comkgdbrate = KGDBRATE;
#ifndef KGDBMODE
#define KGDBMODE ((TTYDEF_CFLAG & ~(CSIZE | CSTOPB | PARENB)) | CS8) /* 8N1 */
#endif
int comkgdbmode = KGDBMODE;
#endif /* NCOM  || NPCCOM */
void kgdb_port_init(void);
#endif /* KGDB */

#ifdef APERTURE
#ifdef INSECURE
int allowaperture = 1;
#else
int allowaperture = 0;
#endif
#endif

void	winchip_cpu_setup(const char *, int, int);
void	amd_family5_setup(const char *, int, int);
void	amd_family6_setup(const char *, int, int);
void	cyrix3_cpu_setup(const char *, int, int);
void	cyrix6x86_cpu_setup(const char *, int, int);
void	natsem6x86_cpu_setup(const char *, int, int);
void	intel586_cpu_setup(const char *, int, int);
void	intel686_common_cpu_setup(const char *, int, int);
void	intel686_cpu_setup(const char *, int, int);
void	intel686_p4_cpu_setup(const char *, int, int);
void	tm86_cpu_setup(const char *, int, int);
char *	intel686_cpu_name(int);
char *	cyrix3_cpu_name(int, int);
char *	tm86_cpu_name(int);
void	viac3_rnd(void *);
int	p4_cpuspeed(int *);
int	pentium_cpuspeed(int *);

#if defined(I486_CPU) || defined(I586_CPU) || defined(I686_CPU)
static __inline u_char
cyrix_read_reg(u_char reg)
{
	outb(0x22, reg);
	return inb(0x23);
}

static __inline void
cyrix_write_reg(u_char reg, u_char data)
{
	outb(0x22, reg);
	outb(0x23, data);
}
#endif

/*
 * cpuid instruction.  request in eax, result in eax, ebx, ecx, edx.
 * requires caller to provide u_int32_t regs[4] array.
 */
void
cpuid(u_int32_t ax, u_int32_t *regs)
{
	__asm __volatile(
	    "cpuid\n\t"
	    "movl	%%eax, 0(%2)\n\t"
	    "movl	%%ebx, 4(%2)\n\t"
	    "movl	%%ecx, 8(%2)\n\t"
	    "movl	%%edx, 12(%2)\n\t"
	    :"=a" (ax)
	    :"0" (ax), "S" (regs)
	    :"bx", "cx", "dx");
}

/*
 * Machine-dependent startup code
 */
void
cpu_startup()
{
	unsigned i;
	caddr_t v;
	int sz;
	vaddr_t minaddr, maxaddr, va;
	paddr_t pa;

	/*
	 * Initialize error message buffer (at end of core).
	 * (space reserved in pmap_bootstrap)
	 */
	pa = avail_end;
	va = (vaddr_t)msgbufp;
	for (i = 0; i < btoc(MSGBUFSIZE); i++) {
		pmap_kenter_pa(va, pa, VM_PROT_READ|VM_PROT_WRITE);
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
	}
	pmap_update(pmap_kernel());
	initmsgbuf((caddr_t)msgbufp, round_page(MSGBUFSIZE));

	printf("%s", version);
	startrtclock();

	identifycpu();
	printf("real mem  = %u (%uK)\n", ctob(physmem), ctob(physmem)/1024);

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
	setup_buffers(&maxaddr);

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
				   16*NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate a submap for physio
	 */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
				   VM_PHYS_SIZE, 0, FALSE, NULL);

	printf("avail mem = %lu (%uK)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free)/1024);
	printf("using %d buffers containing %u bytes (%uK) of memory\n",
		nbuf, bufpages * PAGE_SIZE, bufpages * PAGE_SIZE / 1024);

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
	ioport_malloc_safe = 1;
}

/*
 * Set up proc0's TSS and LDT.
 */
void
i386_proc0_tss_ldt_init()
{
	struct pcb *pcb;
	int x;

	curpcb = pcb = &proc0.p_addr->u_pcb;
	pcb->pcb_tss.tss_ioopt =
	    ((caddr_t)pcb->pcb_iomap - (caddr_t)&pcb->pcb_tss) << 16;
	for (x = 0; x < sizeof(pcb->pcb_iomap) / 4; x++)
		pcb->pcb_iomap[x] = 0xffffffff;
	pcb->pcb_iomap_pad = 0xff;

	pcb->pcb_ldt_sel = pmap_kernel()->pm_ldt_sel = GSEL(GLDT_SEL, SEL_KPL);
	pcb->pcb_cr0 = rcr0();
	pcb->pcb_tss.tss_ss0 = GSEL(GDATA_SEL, SEL_KPL);
	pcb->pcb_tss.tss_esp0 = (int)proc0.p_addr + USPACE - 16;
	tss_alloc(pcb);

	ltr(pcb->pcb_tss_sel);
	lldt(pcb->pcb_ldt_sel);

	proc0.p_md.md_regs = (struct trapframe *)pcb->pcb_tss.tss_esp0 - 1;
}

/*
 * Allocate space for system data structures.  We are given
 * a starting virtual address and we return a final virtual
 * address; along the way we set each data structure pointer.
 *
 * We call allocsys() with 0 to find out how much space we want,
 * allocate that much and fill it with zeroes, and then call
 * allocsys() again with the correct base virtual address.
 */
caddr_t
allocsys(v)
	register caddr_t v;
{

#define	valloc(name, type, num) \
	    v = (caddr_t)(((name) = (type *)v) + (num))

#ifdef SYSVMSG
	valloc(msgpool, char, msginfo.msgmax);
	valloc(msgmaps, struct msgmap, msginfo.msgseg);
	valloc(msghdrs, struct msg, msginfo.msgtql);
	valloc(msqids, struct msqid_ds, msginfo.msgmni);
#endif

	/*
	 * Determine how many buffers to allocate.  We use 10% of the
	 * first 2MB of memory, and 5% of the rest, with a minimum of 16
	 * buffers.  We allocate 1/2 as many swap buffer headers as file
	 * i/o buffers.
	 */
	if (bufpages == 0) {
		if (physmem < btoc(2 * 1024 * 1024))
			bufpages = physmem / 10;
		else
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
setup_buffers(maxaddr)
	vaddr_t *maxaddr;
{
	vsize_t size;
	vaddr_t addr;
	int base, residual, left, chunk, i;
	struct pglist pgs, saved_pgs;
	struct vm_page *pg;

	size = MAXBSIZE * nbuf;
	if (uvm_map(kernel_map, (vaddr_t *) &buffers, round_page(size),
		    NULL, UVM_UNKNOWN_OFFSET, 0,
		    UVM_MAPFLAG(UVM_PROT_NONE, UVM_PROT_NONE, UVM_INH_NONE,
				UVM_ADV_NORMAL, 0)))
		panic("cpu_startup: cannot allocate VM for buffers");
	addr = (vaddr_t)buffers;

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
 * Info for CTL_HW
 */
char	cpu_model[120];

/*
 * Note: these are just the ones that may not have a cpuid instruction.
 * We deal with the rest in a different way.
 */
const struct cpu_nocpuid_nameclass i386_nocpuid_cpus[] = {
	{ CPUVENDOR_INTEL, "Intel", "386SX",	CPUCLASS_386,
		NULL},				/* CPU_386SX */
	{ CPUVENDOR_INTEL, "Intel", "386DX",	CPUCLASS_386,
		NULL},				/* CPU_386   */
	{ CPUVENDOR_INTEL, "Intel", "486SX",	CPUCLASS_486,
		NULL},				/* CPU_486SX */
	{ CPUVENDOR_INTEL, "Intel", "486DX",	CPUCLASS_486,
		NULL},				/* CPU_486   */
	{ CPUVENDOR_CYRIX, "Cyrix", "486DLC",	CPUCLASS_486,
		NULL},				/* CPU_486DLC */
	{ CPUVENDOR_CYRIX, "Cyrix", "6x86",	CPUCLASS_486,
		cyrix6x86_cpu_setup},		/* CPU_6x86 */
	{ CPUVENDOR_NEXGEN,"NexGen","586",	CPUCLASS_386,
		NULL},				/* CPU_NX586 */
};

const char *classnames[] = {
	"386",
	"486",
	"586",
	"686"
};

const char *modifiers[] = {
	"",
	"OverDrive ",
	"Dual ",
	""
};

const struct cpu_cpuid_nameclass i386_cpuid_cpus[] = {
	{
		"GenuineIntel",
		CPUVENDOR_INTEL,
		"Intel",
		/* Family 4 */
		{ {
			CPUCLASS_486, 
			{
				"486DX", "486DX", "486SX", "486DX2", "486SL",
				"486SX2", 0, "486DX2 W/B",
				"486DX4", 0, 0, 0, 0, 0, 0, 0,
				"486"		/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				"Pentium (A-step)", "Pentium (P5)",
				"Pentium (P54C)", "Pentium (P24T)",
				"Pentium/MMX", "Pentium", 0,
				"Pentium (P54C)", "Pentium/MMX",
				0, 0, 0, 0, 0, 0, 0,
				"Pentium"	/* Default */
			},
			intel586_cpu_setup
		},
		/* Family 6 */
		{
			CPUCLASS_686,
			{
				"Pentium Pro", "Pentium Pro", 0,
				"Pentium II", "Pentium Pro",
				"Pentium II/Celeron",
				"Celeron",
				"Pentium III",
				"Pentium III",
				"Pentium M",
				"Pentium III Xeon",
				"Pentium III", 0, 0,
				0, 0,
				"Pentium Pro, II or III"	/* Default */
			},
			intel686_cpu_setup
		},
		/* Family 7 */
		{
			CPUCLASS_686,
		} ,
		/* Family 8 */
		{
			CPUCLASS_686,
		} ,
		/* Family 9 */
		{
			CPUCLASS_686,
		} ,
		/* Family A */
		{
			CPUCLASS_686,
		} ,
		/* Family B */
		{
			CPUCLASS_686,
		} ,
		/* Family C */
		{
			CPUCLASS_686,
		} ,
		/* Family D */
		{
			CPUCLASS_686,
		} ,
		/* Family E */
		{
			CPUCLASS_686,
		} ,
		/* Family F */
		{
			CPUCLASS_686,
			{
				"Pentium 4", 0, 0, 0,
				0, 0, 0, 0,
				0, 0, 0, 0,
				0, 0, 0, 0,
				"Pentium 4"	/* Default */
			},
			intel686_p4_cpu_setup
		} }
	},
	{
		"AuthenticAMD",
		CPUVENDOR_AMD,
		"AMD",
		/* Family 4 */
		{ {
			CPUCLASS_486, 
			{
				0, 0, 0, "Am486DX2 W/T",
				0, 0, 0, "Am486DX2 W/B",
				"Am486DX4 W/T or Am5x86 W/T 150",
				"Am486DX4 W/B or Am5x86 W/B 150", 0, 0,
				0, 0, "Am5x86 W/T 133/160",
				"Am5x86 W/B 133/160",
				"Am486 or Am5x86"	/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				"K5", "K5", "K5", "K5", 0, 0, "K6",
				"K6", "K6-2", "K6-III", 0, 0, 0,
				"K6-2+/III+", 0, 0,
				"K5 or K6"		/* Default */
			},
			amd_family5_setup
		},
		/* Family 6 */
		{
			CPUCLASS_686,
			{
				0, "Athlon Model 1", "Athlon Model 2",
				"Duron Model 3",
				"Athlon Model 4",
				0, "Athlon XP Model 6",
				"Duron Model 7", 
				"Athlon XP Model 8",
				0, "Athlon XP Model 10",
				0, 0, 0, 0, 0,
				"K7"		/* Default */
			},
			amd_family6_setup
		},
		/* Family 7 */
		{
			CPUCLASS_686,
		} ,
		/* Family 8 */
		{
			CPUCLASS_686,
		} ,
		/* Family 9 */
		{
			CPUCLASS_686,
		} ,
		/* Family A */
		{
			CPUCLASS_686,
		} ,
		/* Family B */
		{
			CPUCLASS_686,
		} ,
		/* Family C */
		{
			CPUCLASS_686,
		} ,
		/* Family D */
		{
			CPUCLASS_686,
		} ,
		/* Family E */
		{
			CPUCLASS_686,
		} ,
		/* Family F */
		{
			CPUCLASS_686,
			{
				0, 0, 0, 0, "Athlon64",
				"Opteron or Athlon64FX", 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"AMD64"			/* DEFAULT */
			},
			amd_family6_setup
		} }
	},
	{
		"CyrixInstead",
		CPUVENDOR_CYRIX,
		"Cyrix",
		/* Family 4 */
		{ {
			CPUCLASS_486,
			{
				0, 0, 0, "MediaGX", 0, 0, 0, 0, "5x86", 0, 0,
				0, 0, 0, 0,
				"486 class"	/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				0, 0, "6x86", 0, "GXm", 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0,
				"586 class"	/* Default */
			},
			cyrix6x86_cpu_setup
		},
		/* Family 6 */
		{
			CPUCLASS_686,
			{
				"6x86MX", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0,
				"686 class"	/* Default */
			},
			NULL
		} }
	},
	{
		"CentaurHauls",
		CPUVENDOR_IDT,
		"IDT",
		/* Family 4, not available from IDT */
		{ {
			CPUCLASS_486, 
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"486 class"		/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				0, 0, 0, 0, "WinChip C6", 0, 0, 0,
				"WinChip 2", "WinChip 3", 0, 0, 0, 0, 0, 0,
				"WinChip"		/* Default */
			},
			winchip_cpu_setup
		},
		/* Family 6 */
		{
			CPUCLASS_686,
			{
				0, 0, 0, 0, 0, 0,
				"C3 Samuel",
				"C3 Samuel 2/Ezra",
				"C3 Ezra-T",
				"C3 Nehemiah", 0, 0, 0, 0, 0, 0,
				"C3"		/* Default */
			},
			cyrix3_cpu_setup
		} }
	},
	{
		"RiseRiseRise",
		CPUVENDOR_RISE,
		"Rise",
		/* Family 4, not available from Rise */
		{ {
			CPUCLASS_486, 
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"486 class"		/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				"mP6", 0, "mP6", 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"mP6"			/* Default */
			},
			NULL
		},
		/* Family 6, not yet available from Rise */
		{
			CPUCLASS_686,
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"686 class"		/* Default */
			},
			NULL
		} }
	},
	{
		"GenuineTMx86",
		CPUVENDOR_TRANSMETA,
		"Transmeta",
		/* Family 4, not available from Transmeta */
		{ {
			CPUCLASS_486, 
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"486 class"		/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				0, 0, 0, 0, "TMS5x00", 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0,
				"TMS5x00"		/* Default */
			},
			tm86_cpu_setup
		},
		/* Family 6, not yet available from Transmeta */
		{
			CPUCLASS_686,
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"686 class"		/* Default */
			},
			NULL
		} }
	},
	{
		"Geode by NSC",
		CPUVENDOR_NS,
		"National Semiconductor",
		/* Family 4, not available from National Semiconductor */
		{ {
			CPUCLASS_486,
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"486 class"	/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				0, 0, 0, 0, "Geode GX1", 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0,
				"586 class"	/* Default */
			},
			natsem6x86_cpu_setup
		} }
	},
	{
		"SiS SiS SiS ",
		CPUVENDOR_SIS,
		"SiS",
		/* Family 4, not available from SiS */
		{ {
			CPUCLASS_486,
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"486 class"	/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				"SiS55x", 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0,
				"586 class"	/* Default */
			},
			NULL
		} }
	}
};

const struct cpu_cpuid_feature i386_cpuid_features[] = {
	{ CPUID_FPU,	"FPU" },
	{ CPUID_VME,	"V86" },
	{ CPUID_DE,	"DE" },
	{ CPUID_PSE,	"PSE" },
	{ CPUID_TSC,	"TSC" },
	{ CPUID_MSR,	"MSR" },
	{ CPUID_PAE,	"PAE" },
	{ CPUID_MCE,	"MCE" },
	{ CPUID_CX8,	"CX8" },
	{ CPUID_APIC,	"APIC" },
	{ CPUID_SYS1,	"SYS" },
	{ CPUID_SEP,	"SEP" },
	{ CPUID_MTRR,	"MTRR" },
	{ CPUID_PGE,	"PGE" },
	{ CPUID_MCA,	"MCA" },
	{ CPUID_CMOV,	"CMOV" },
	{ CPUID_PAT,	"PAT" },
	{ CPUID_PSE36,	"PSE36" },
	{ CPUID_SER,	"SER" },
	{ CPUID_CFLUSH,	"CFLUSH" },
	{ CPUID_ACPI,	"ACPI" },
	{ CPUID_MMX,	"MMX" },
	{ CPUID_FXSR,	"FXSR" },
	{ CPUID_SSE,	"SSE" },
	{ CPUID_SSE2,	"SSE2" },
	{ CPUID_SS,	"SS" },
	{ CPUID_HTT,	"HTT" },
	{ CPUID_TM,	"TM" },
	{ CPUID_SBF,	"SBF" },
	{ CPUID_3DNOW,	"3DNOW" },
};

const struct cpu_cpuid_feature i386_cpuid_ecxfeatures[] = {
	{ CPUIDECX_PNI,		"PNI" },
	{ CPUIDECX_MWAIT,	"MWAIT" },
	{ CPUIDECX_EST,		"EST" },
	{ CPUIDECX_TM2,		"TM2" },
	{ CPUIDECX_CNXTID,	"CNXT-ID" },
};

void
winchip_cpu_setup(cpu_device, model, step)
	const char *cpu_device;
	int model, step;
{
#if defined(I586_CPU)

	switch (model) {
	case 4: /* WinChip C6 */
		cpu_feature &= ~CPUID_TSC;
		/* Disable RDTSC instruction from user-level. */
		lcr4(rcr4() | CR4_TSD);

		printf("%s: TSC disabled\n", cpu_device);
		break;
	}
#endif
}

#if defined(I686_CPU)
/*
 * Note, the VIA C3 Nehemiah provides 4 internal 8-byte buffers, which
 * store random data, and can be accessed a lot quicker than waiting
 * for new data to be generated.  As we are using every 8th bit only
 * due to whitening. Since the RNG generates in excess of 21KB/s at
 * it's worst, collecting 64 bytes worth of entropy should not affect
 * things significantly.
 *
 * Note, due to some weirdness in the RNG, we need at least 7 bytes
 * extra on the end of our buffer.  Also, there is an outside chance
 * that the VIA RNG can "wedge", as the generated bit-rate is variable.
 * We could do all sorts of startup testing and things, but
 * frankly, I don't really see the point.  If the RNG wedges, then the
 * chances of you having a defective CPU are very high.  Let it wedge.
 *
 * Adding to the whole confusion, in order to access the RNG, we need
 * to have FXSR support enabled, and the correct FPU enable bits must
 * be there to enable the FPU in kernel.  It would be nice if all this
 * mumbo-jumbo was not needed in order to use the RNG.  Oh well, life
 * does go on...
 */
#define VIAC3_RNG_BUFSIZ	16		/* 32bit words */
struct timeout viac3_rnd_tmo;
int viac3_rnd_present = 0;

void
viac3_rnd(void *v)
{
	struct timeout *tmo = v;
	unsigned int *p, i, rv, creg0, len = VIAC3_RNG_BUFSIZ;
	static int buffer[VIAC3_RNG_BUFSIZ + 2];	/* XXX why + 2? */

	creg0 = rcr0();		/* Permit access to SIMD/FPU path */
	lcr0(creg0 & ~(CR0_EM|CR0_TS));

	/*
	 * Here we collect the random data from the VIA C3 RNG.  We make
	 * sure that we turn on maximum whitening (%edx[0,1] == "11"), so
	 * that we get the best random data possible.
	 */
	__asm __volatile("rep xstore-rng"
	    : "=a" (rv) : "d" (3), "D" (buffer), "c" (len*sizeof(int))
	    : "memory", "cc");

	lcr0(creg0);

	for (i = 0, p = buffer; i < VIAC3_RNG_BUFSIZ; i++, p++)
		add_true_randomness(*p);

	timeout_add(tmo, (hz > 100) ? (hz / 100) : 1);
}

#ifdef CRYPTO

struct viac3_session {
	u_int32_t	ses_ekey[4 * (MAXNR + 1) + 4];	/* 128 bit aligned */
	u_int32_t	ses_dkey[4 * (MAXNR + 1) + 4];	/* 128 bit aligned */
	u_int8_t	ses_iv[16];			/* 128 bit aligned */
	u_int32_t	ses_cw0;
	int		ses_klen;
	int		ses_used;
	int		ses_pad;			/* to multiple of 16 */
};

struct viac3_softc {
	u_int32_t		op_cw[4];		/* 128 bit aligned */
	u_int8_t		op_iv[16];		/* 128 bit aligned */
	void			*op_buf;

	/* normal softc stuff */
	int32_t			sc_cid;
	int			sc_nsessions;
	struct viac3_session	*sc_sessions;
};

#define VIAC3_SESSION(sid)		((sid) & 0x0fffffff)
#define	VIAC3_SID(crd,ses)		(((crd) << 28) | ((ses) & 0x0fffffff))

static struct viac3_softc *vc3_sc;
int viac3_crypto_present;

void viac3_crypto_setup(void);
int viac3_crypto_newsession(u_int32_t *, struct cryptoini *);
int viac3_crypto_process(struct cryptop *);
int viac3_crypto_freesession(u_int64_t);
static __inline void viac3_cbc(void *, void *, void *, void *, int, void *);

void
viac3_crypto_setup(void)
{
	int algs[CRYPTO_ALGORITHM_MAX + 1];

	if ((vc3_sc = malloc(sizeof(*vc3_sc), M_DEVBUF, M_NOWAIT)) == NULL)
		return;		/* YYY bitch? */
	bzero(vc3_sc, sizeof(*vc3_sc));

	bzero(algs, sizeof(algs));
	algs[CRYPTO_AES_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;

	vc3_sc->sc_cid = crypto_get_driverid(0);
	if (vc3_sc->sc_cid < 0)
		return;		/* YYY bitch? */

	crypto_register(vc3_sc->sc_cid, algs, viac3_crypto_newsession,
	    viac3_crypto_freesession, viac3_crypto_process);
	i386_has_xcrypt = 1;
}

int
viac3_crypto_newsession(u_int32_t *sidp, struct cryptoini *cri)
{
	struct viac3_softc *sc = vc3_sc;
	struct viac3_session *ses = NULL;
	int sesn, i, cw0;

	if (sc == NULL || sidp == NULL || cri == NULL ||
	    cri->cri_next != NULL || cri->cri_alg != CRYPTO_AES_CBC)
		return (EINVAL);

	switch (cri->cri_klen) {
	case 128:
		cw0 = C3_CRYPT_CWLO_KEY128;
		break;
	case 192:
		cw0 = C3_CRYPT_CWLO_KEY192;
		break;
	case 256:
		cw0 = C3_CRYPT_CWLO_KEY256;
		break;
	default:
		return (EINVAL);
	}
	cw0 |= C3_CRYPT_CWLO_ALG_AES | C3_CRYPT_CWLO_KEYGEN_SW |
	    C3_CRYPT_CWLO_NORMAL;

	if (sc->sc_sessions == NULL) {
		ses = sc->sc_sessions = (struct viac3_session *)malloc(
		    sizeof(*ses), M_DEVBUF, M_NOWAIT);
		if (ses == NULL)
			return (ENOMEM);
		sesn = 0;
		sc->sc_nsessions = 1;
	} else {
		for (sesn = 0; sesn < sc->sc_nsessions; sesn++) {
			if (sc->sc_sessions[sesn].ses_used == 0) {
				ses = &sc->sc_sessions[sesn];
				break;
			}
		}

		if (ses == NULL) {
			sesn = sc->sc_nsessions;
			ses = (struct viac3_session *)malloc((sesn + 1) *
			    sizeof(*ses), M_DEVBUF, M_NOWAIT);
			if (ses == NULL)
				return (ENOMEM);
			bcopy(sc->sc_sessions, ses, sesn * sizeof(*ses));
			bzero(sc->sc_sessions, sesn * sizeof(*ses));
			free(sc->sc_sessions, M_DEVBUF);
			sc->sc_sessions = ses;
			ses = &sc->sc_sessions[sesn];
			sc->sc_nsessions++;
		}
	}

	bzero(ses, sizeof(*ses));
	ses->ses_used = 1;

	get_random_bytes(ses->ses_iv, sizeof(ses->ses_iv));
	ses->ses_klen = cri->cri_klen;
	ses->ses_cw0 = cw0;

	/* Build expanded keys for both directions */
	rijndaelKeySetupEnc(ses->ses_ekey, cri->cri_key, cri->cri_klen);
	rijndaelKeySetupDec(ses->ses_dkey, cri->cri_key, cri->cri_klen);
	for (i = 0; i < 4 * (MAXNR + 1); i++) {
		ses->ses_ekey[i] = ntohl(ses->ses_ekey[i]);
		ses->ses_dkey[i] = ntohl(ses->ses_dkey[i]);
	}

	*sidp = VIAC3_SID(0, sesn);
	return (0);
}

int
viac3_crypto_freesession(u_int64_t tid)
{
	struct viac3_softc *sc = vc3_sc;
	int sesn;
	u_int32_t sid = ((u_int32_t)tid) & 0xffffffff;

	if (sc == NULL)
		return (EINVAL);
	sesn = VIAC3_SESSION(sid);
	if (sesn >= sc->sc_nsessions)
		return (EINVAL);
	bzero(&sc->sc_sessions[sesn], sizeof(sc->sc_sessions[sesn]));
	return (0);
}

static __inline void
viac3_cbc(void *cw, void *src, void *dst, void *key, int rep,
    void *iv)
{
	unsigned int creg0;

	creg0 = rcr0();		/* Permit access to SIMD/FPU path */
	lcr0(creg0 & ~(CR0_EM|CR0_TS));

	/* Do the deed */
	__asm __volatile("pushfl; popfl");
	__asm __volatile("rep xcrypt-cbc" :
	    : "a" (iv), "b" (key), "c" (rep), "d" (cw), "S" (src), "D" (dst)
	    : "memory", "cc");

	lcr0(creg0);
}

int
viac3_crypto_process(struct cryptop *crp)
{
	struct viac3_softc *sc = vc3_sc;
	struct viac3_session *ses;
	struct cryptodesc *crd;
	int sesn, err = 0;
	u_int32_t *key;

	if (crp == NULL || crp->crp_callback == NULL) {
		err = EINVAL;
		goto out;
	}
	crd = crp->crp_desc;
	if (crd == NULL || crd->crd_next != NULL ||
	    crd->crd_alg != CRYPTO_AES_CBC || 
	    (crd->crd_len % 16) != 0) {
		err = EINVAL;
		goto out;
	}

	sesn = VIAC3_SESSION(crp->crp_sid);
	if (sesn >= sc->sc_nsessions) {
		err = EINVAL;
		goto out;
	}
	ses = &sc->sc_sessions[sesn];

	sc->op_buf = (char *)malloc(crd->crd_len, M_DEVBUF, M_NOWAIT);
	if (sc->op_buf == NULL) {
		err = ENOMEM;
		goto out;
	}

	if (crd->crd_flags & CRD_F_ENCRYPT) {
		sc->op_cw[0] = ses->ses_cw0 | C3_CRYPT_CWLO_ENCRYPT;
		key = ses->ses_ekey;
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, sc->op_iv, 16);
		else
			bcopy(ses->ses_iv, sc->op_iv, 16);

		if ((crd->crd_flags & CRD_F_IV_PRESENT) == 0) {
			if (crp->crp_flags & CRYPTO_F_IMBUF)
				m_copyback((struct mbuf *)crp->crp_buf,
				    crd->crd_inject, 16, sc->op_iv);
			else if (crp->crp_flags & CRYPTO_F_IOV)
				cuio_copyback((struct uio *)crp->crp_buf,
				    crd->crd_inject, 16, sc->op_iv);
			else
				bcopy(sc->op_iv,
				    crp->crp_buf + crd->crd_inject, 16);
		}
	} else {
		sc->op_cw[0] = ses->ses_cw0 | C3_CRYPT_CWLO_DECRYPT;
		key = ses->ses_dkey;
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, sc->op_iv, 16);
		else {
			if (crp->crp_flags & CRYPTO_F_IMBUF)
				m_copydata((struct mbuf *)crp->crp_buf,
				    crd->crd_inject, 16, sc->op_iv);
			else if (crp->crp_flags & CRYPTO_F_IOV)
				cuio_copydata((struct uio *)crp->crp_buf,
				    crd->crd_inject, 16, sc->op_iv);
			else
				bcopy(crp->crp_buf + crd->crd_inject,
				    sc->op_iv, 16);
		}
	}

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		m_copydata((struct mbuf *)crp->crp_buf,
		    crd->crd_skip, crd->crd_len, sc->op_buf);
	else if (crp->crp_flags & CRYPTO_F_IOV)
		cuio_copydata((struct uio *)crp->crp_buf,
		    crd->crd_skip, crd->crd_len, sc->op_buf);
	else
		bcopy(crp->crp_buf + crd->crd_skip, sc->op_buf, crd->crd_len);

	sc->op_cw[1] = sc->op_cw[2] = sc->op_cw[3] = 0;
	viac3_cbc(&sc->op_cw, sc->op_buf, sc->op_buf, key,
	    crd->crd_len / 16, sc->op_iv);

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		m_copyback((struct mbuf *)crp->crp_buf,
		    crd->crd_skip, crd->crd_len, sc->op_buf);
	else if (crp->crp_flags & CRYPTO_F_IOV)
		cuio_copyback((struct uio *)crp->crp_buf,
		    crd->crd_skip, crd->crd_len, sc->op_buf);
	else
		bcopy(sc->op_buf, crp->crp_buf + crd->crd_skip, crd->crd_len);

	/* copy out last block for use as next session IV */
	if (crd->crd_flags & CRD_F_ENCRYPT) {
		if (crp->crp_flags & CRYPTO_F_IMBUF)
			m_copydata((struct mbuf *)crp->crp_buf,
			    crd->crd_skip + crd->crd_len - 16, 16, ses->ses_iv);
		else if (crp->crp_flags & CRYPTO_F_IOV)
			cuio_copydata((struct uio *)crp->crp_buf,
			    crd->crd_skip + crd->crd_len - 16, 16, sc->op_iv);
		else
			bcopy(crp->crp_buf + crd->crd_skip + crd->crd_len - 16,
			    sc->op_iv, 16);
	}

out:
	if (sc->op_buf != NULL) {
		bzero(sc->op_buf, crd->crd_len);
		free(sc->op_buf, M_DEVBUF);
		sc->op_buf = NULL;
	}
	crp->crp_etype = err;
	crypto_done(crp);
	return (err);
}

#endif /* CRYPTO */

#endif /* defined(I686_CPU) */

void
cyrix3_cpu_setup(cpu_device, model, step)
	const char *cpu_device;
	int model, step;
{
#if defined(I686_CPU)
	unsigned int val;
#if !defined(SMALL_KERNEL)
	extern void (*pagezero)(void *, size_t);
	extern void i686_pagezero(void *, size_t);

	pagezero = i686_pagezero;
#endif


	switch (model) {
	case 6: /* C3 Samuel 1 */
	case 7: /* C3 Samuel 2 or C3 Ezra */
	case 8: /* C3 Ezra-T */
		__asm __volatile("cpuid"
		    : "=d" (val) : "a" (0x80000001) : "ebx", "ecx");
		if (val & (1U << 31)) {
			cpu_feature |= CPUID_3DNOW;
		} else {
			cpu_feature &= ~CPUID_3DNOW;
		}
		break;

	case 9:
		if (step < 3)
			break;

		/* 
		 * C3 Nehemiah:
		 * First we check for extended feature flags, and then
		 * (if present) retrieve the ones at 0xC0000001.  In this
		 * bit 2 tells us if the RNG is present.  Bit 3 tells us
		 * if the RNG has been enabled.  In order to use the RNG
		 * we need 3 things:  We need an RNG, we need the FXSR bit
		 * enabled in cr4 (SSE/SSE2 stuff), and we need to have
		 * Bit 6 of MSR 0x110B set to 1 (the default), which will
		 * show up as bit 3 set here.
		 */
		__asm __volatile("cpuid" /* Check for RNG */
		    : "=a" (val) : "a" (0xC0000000) : "cc");
		if (val >= 0xC0000001) {
			__asm __volatile("cpuid"
			    : "=d" (val) : "a" (0xC0000001) : "cc");
		}

		/* Enable RNG if present and disabled */
		if (val & 0x44)
			printf("%s:", cpu_device);
		if (val & 0x4) {
			if (!(val & 0x8)) {
				u_int64_t msreg;

				msreg = rdmsr(0x110B);
				msreg |= 0x40;
				wrmsr(0x110B, msreg);
			}
			viac3_rnd_present = 1;
			printf(" RNG");
		}

#ifdef CRYPTO
		/* Enable AES engine if present and disabled */
		if (val & 0x40) {
			if (!(val & 0x80)) {
				u_int64_t msreg;

				msreg = rdmsr(0x1107);
				msreg |= (0x01 << 28);
				wrmsr(0x1107, msreg);
			}
			viac3_crypto_present = 1;
			printf(" AES");
		}
#endif /* CRYPTO */

		printf("\n");
		break;
	}
#endif
}

void
cyrix6x86_cpu_setup(cpu_device, model, step)
	const char *cpu_device;
	int model, step;
{
#if defined(I486_CPU) || defined(I586_CPU) || defined(I686_CPU)
	extern int clock_broken_latch;

	switch (model) {
	case -1: /* M1 w/o cpuid */
	case 2:	/* M1 */
		/* set up various cyrix registers */
		/* Enable suspend on halt */
		cyrix_write_reg(0xc2, cyrix_read_reg(0xc2) | 0x08);
		/* enable access to ccr4/ccr5 */
		cyrix_write_reg(0xC3, cyrix_read_reg(0xC3) | 0x10);
		/* cyrix's workaround  for the "coma bug" */
		cyrix_write_reg(0x31, cyrix_read_reg(0x31) | 0xf8);
		cyrix_write_reg(0x32, cyrix_read_reg(0x32) | 0x7f);
		cyrix_write_reg(0x33, cyrix_read_reg(0x33) & ~0xff);
		cyrix_write_reg(0x3c, cyrix_read_reg(0x3c) | 0x87);
		/* disable access to ccr4/ccr5 */
		cyrix_write_reg(0xC3, cyrix_read_reg(0xC3) & ~0x10);

		printf("%s: xchg bug workaround performed\n", cpu_device);
		break;	/* fallthrough? */
	case 4:
		clock_broken_latch = 1;
		cpu_feature &= ~CPUID_TSC;
		printf("%s: TSC disabled\n", cpu_device);
		break;
	}
#endif
}

#if defined(I586_CPU) || defined(I686_CPU)
void	natsem6x86_cpureset(void);

void
natsem6x86_cpureset(void)
{
	/* reset control SC1100 (datasheet page 170) */
	outl(0xCF8, 0x80009044UL);
	/* system wide reset */
	outb(0xCFC, 0x0F);
}
#endif

void
natsem6x86_cpu_setup(cpu_device, model, step)
	const char *cpu_device;
	int model, step;
{
#if defined(I586_CPU) || defined(I686_CPU)
	extern int clock_broken_latch;

	clock_broken_latch = 1;
	switch (model) {
	case 4:
		cpu_feature &= ~CPUID_TSC;
		printf("%s: TSC disabled\n", cpu_device);
		break;
	}
	cpuresetfn = natsem6x86_cpureset;
#endif
}


void
intel586_cpu_setup(cpu_device, model, step)
	const char *cpu_device;
	int model, step;
{
#if defined(I586_CPU)
	fix_f00f();
	printf("%s: F00F bug workaround installed\n", cpu_device);
#endif
}

void
amd_family5_setup(cpu_device, model, step)
	const char *cpu_device;
	int model, step;
{
	switch (model) {
	case 0:		/* AMD-K5 Model 0 */
		/*
		 * According to the AMD Processor Recognition App Note,
		 * the AMD-K5 Model 0 uses the wrong bit to indicate
		 * support for global PTEs, instead using bit 9 (APIC)
		 * rather than bit 13 (i.e. "0x200" vs. 0x2000".  Oops!).
		 */
		if (cpu_feature & CPUID_APIC)
			cpu_feature = (cpu_feature & ~CPUID_APIC) | CPUID_PGE;
		/*
		 * XXX But pmap_pg_g is already initialized -- need to kick
		 * XXX the pmap somehow.  How does the MP branch do this?
		 */
		break;
	}
}

void
amd_family6_setup(cpu_device, model, step)
	const char *cpu_device;
	int model, step;
{
#if !defined(SMALL_KERNEL) && defined(I686_CPU)
	extern void (*pagezero)(void *, size_t);
	extern void sse2_pagezero(void *, size_t);
	extern void i686_pagezero(void *, size_t);

	if (cpu_feature & CPUID_SSE2)
		pagezero = sse2_pagezero;
	else
		pagezero = i686_pagezero;
#endif
}

void
intel686_common_cpu_setup(const char *cpu_device, int model, int step)
{
	/*
	 * Make sure SYSENTER is disabled.
	 */
	if (cpu_feature & CPUID_SEP)
		wrmsr(MSR_SYSENTER_CS, 0);

#if !defined(SMALL_KERNEL) && defined(I686_CPU)
	if (cpu_ecxfeature & CPUIDECX_EST) {
		if (rdmsr(MSR_MISC_ENABLE) & (1 << 16))
			est_init(cpu_device);
		else
			printf("%s: Enhanced SpeedStep disabled by BIOS\n",
			    cpu_device);
	} else if ((cpu_feature & (CPUID_ACPI | CPUID_TM)) ==
	    (CPUID_ACPI | CPUID_TM))
		p4tcc_init(model, step);

	{
	extern void (*pagezero)(void *, size_t);
	extern void sse2_pagezero(void *, size_t);
	extern void i686_pagezero(void *, size_t);

	if (cpu_feature & CPUID_SSE2)
		pagezero = sse2_pagezero;
	else
		pagezero = i686_pagezero;
	pagezero = bzero;
	}
#endif
}

void
intel686_cpu_setup(const char *cpu_device, int model, int step)
{
	u_quad_t msr119;

	intel686_common_cpu_setup(cpu_device, model, step);

	/*
	 * Original PPro returns SYSCALL in CPUID but is non-functional.
	 * From Intel Application Note #485.
	 */
	if ((model == 1) && (step < 3))
		cpu_feature &= ~CPUID_SEP;

	/*
	 * Disable the Pentium3 serial number.
	 */
	if ((model == 7) && (cpu_feature & CPUID_SER)) {
		msr119 = rdmsr(MSR_BBL_CR_CTL);
		msr119 |= 0x0000000000200000LL;
		wrmsr(MSR_BBL_CR_CTL, msr119);

		printf("%s: disabling processor serial number\n", cpu_device);
		cpu_feature &= ~CPUID_SER;
		cpuid_level = 2;
	}
}

void
intel686_p4_cpu_setup(const char *cpu_device, int model, int step)
{
	intel686_common_cpu_setup(cpu_device, model, step);

#if !defined(SMALL_KERNEL) && defined(I686_CPU)
	if (cpu_cpuspeed == NULL) {
		p4_model = model;
		cpu_cpuspeed = p4_cpuspeed;
	}
#endif
}

void
tm86_cpu_setup(cpu_device, model, step)
	const char *cpu_device;
	int model, step;
{
#if !defined(SMALL_KERNEL) && defined(I586_CPU)
	longrun_init();
#endif
}

char *
intel686_cpu_name(model)
	int model;
{
	extern int cpu_cache_edx;
	char *ret = NULL;

	switch (model) {
	case 5:
		switch (cpu_cache_edx & 0xFF) {
		case 0x40:
		case 0x41:
			ret = "Celeron";
			break;
		/* 0x42 should not exist in this model. */
		case 0x43:
			ret = "Pentium II";
			break;
		case 0x44:
		case 0x45:
			ret = "Pentium II Xeon";
			break;
		}
		break;
	case 7:
		switch (cpu_cache_edx & 0xFF) {
		/* 0x40 - 0x42 should not exist in this model. */
		case 0x43:
			ret = "Pentium III";
			break;
		case 0x44:
		case 0x45:
			ret = "Pentium III Xeon";
			break;
		}
		break;
	}

	return (ret);
}

char *
cyrix3_cpu_name(model, step)
	int model, step;
{
	char	*name = NULL;

	switch (model) {
	case 7:
		if (step < 8)
			name = "C3 Samuel 2";
		else
			name = "C3 Ezra";
		break;
	}
	return name;
}

char *
tm86_cpu_name(model)
	int model;
{
	u_int32_t regs[4];
	char *name = NULL;

	cpuid(0x80860001, regs);

	switch(model) {
	case 4:
		if (((regs[1] >> 16) & 0xff) >= 0x3)
			name = "TMS5800";
		else 
			name = "TMS5600";
	}

	return name;
}

void
identifycpu()
{
	extern char cpu_vendor[];
	extern char cpu_brandstr[];
	extern int cpu_id;
#ifdef CPUDEBUG
	extern int cpu_cache_eax, cpu_cache_ebx, cpu_cache_ecx, cpu_cache_edx;
#else
	extern int cpu_cache_edx;
#endif
	const char *name, *modifier, *vendorname, *token;
	const char *cpu_device = "cpu0";
	int class = CPUCLASS_386, vendor, i, max;
	int family, model, step, modif, cachesize;
	const struct cpu_cpuid_nameclass *cpup = NULL;
	void (*cpu_setup)(const char *, int, int);
	char *brandstr_from, *brandstr_to;
	int skipspace;

	if (cpuid_level == -1) {
#ifdef DIAGNOSTIC
		if (cpu < 0 || cpu >=
		    (sizeof i386_nocpuid_cpus/sizeof(struct cpu_nocpuid_nameclass)))
			panic("unknown cpu type %d", cpu);
#endif
		name = i386_nocpuid_cpus[cpu].cpu_name;
		vendor = i386_nocpuid_cpus[cpu].cpu_vendor;
		vendorname = i386_nocpuid_cpus[cpu].cpu_vendorname;
		model = -1;
		step = -1;
		class = i386_nocpuid_cpus[cpu].cpu_class;
		cpu_setup = i386_nocpuid_cpus[cpu].cpu_setup;
		modifier = "";
		token = "";
	} else {
		max = sizeof (i386_cpuid_cpus) / sizeof (i386_cpuid_cpus[0]);
		modif = (cpu_id >> 12) & 3;
		family = (cpu_id >> 8) & 15;
		if (family < CPU_MINFAMILY)
			panic("identifycpu: strange family value");
		model = (cpu_id >> 4) & 15;
		step = cpu_id & 15;
#ifdef CPUDEBUG
		printf("%s: family %x model %x step %x\n", cpu_device, family,
			model, step);
		printf("%s: cpuid level %d cache eax %x ebx %x ecx %x edx %x\n",
			cpu_device, cpuid_level, cpu_cache_eax, cpu_cache_ebx,
			cpu_cache_ecx, cpu_cache_edx);
#endif

		for (i = 0; i < max; i++) {
			if (!strncmp(cpu_vendor,
			    i386_cpuid_cpus[i].cpu_id, 12)) {
				cpup = &i386_cpuid_cpus[i];
				break;
			}
		}

		if (cpup == NULL) {
			vendor = CPUVENDOR_UNKNOWN;
			if (cpu_vendor[0] != '\0')
				vendorname = &cpu_vendor[0];
			else
				vendorname = "Unknown";
			if (family > CPU_MAXFAMILY)
				family = CPU_MAXFAMILY;
			class = family - 3;
			if (class > CPUCLASS_686)
				class = CPUCLASS_686;
			modifier = "";
			name = "";
			token = "";
			cpu_setup = NULL;
		} else {
			token = cpup->cpu_id;
			vendor = cpup->cpu_vendor;
			vendorname = cpup->cpu_vendorname;
			/*
			 * Special hack for the VIA C3 series.
			 *
			 * VIA bought Centaur Technology from IDT in Aug 1999
			 * and marketed the processors as VIA Cyrix III/C3.
			 */
			if (vendor == CPUVENDOR_IDT && family >= 6) {
				vendor = CPUVENDOR_VIA;
				vendorname = "VIA";
			}
			modifier = modifiers[modif];
			if (family > CPU_MAXFAMILY) {
				family = CPU_MAXFAMILY;
				model = CPU_DEFMODEL;
			} else if (model > CPU_MAXMODEL)
				model = CPU_DEFMODEL;
			i = family - CPU_MINFAMILY;

			/* Special hack for the PentiumII/III series. */
			if (vendor == CPUVENDOR_INTEL && family == 6 &&
			    (model == 5 || model == 7)) {
				name = intel686_cpu_name(model);
			/* Special hack for the VIA C3 series. */
			} else if (vendor == CPUVENDOR_VIA && family == 6 &&
				   model == 7) {
				name = cyrix3_cpu_name(model, step);
			/* Special hack for the TMS5x00 series. */
			} else if (vendor == CPUVENDOR_TRANSMETA && 
				  family == 5 && model == 4) {
				name = tm86_cpu_name(model);
			} else
				name = cpup->cpu_family[i].cpu_models[model];
			if (name == NULL) {
				name = cpup->cpu_family[i].cpu_models[CPU_DEFMODEL];
				if (name == NULL)
					name = "";
			}
			class = cpup->cpu_family[i].cpu_class;
			cpu_setup = cpup->cpu_family[i].cpu_setup;
		}
	}

	/* Find the amount of on-chip L2 cache.  Add support for AMD K6-3...*/
	cachesize = -1;
	if (vendor == CPUVENDOR_INTEL && cpuid_level >= 2 && family < 0xf) {
		int intel_cachetable[] = { 0, 128, 256, 512, 1024, 2048 };
		if ((cpu_cache_edx & 0xFF) >= 0x40 &&
		    (cpu_cache_edx & 0xFF) <= 0x45)
			cachesize = intel_cachetable[(cpu_cache_edx & 0xFF) - 0x40];
	}

	/* Remove leading and duplicated spaces from cpu_brandstr */
	brandstr_from = brandstr_to = cpu_brandstr;
	skipspace = 1;
	while (*brandstr_from != '\0') {
		if (!skipspace || *brandstr_from != ' ') {
			skipspace = 0;
			*(brandstr_to++) = *brandstr_from;
		}
		if (*brandstr_from == ' ')
			skipspace = 1;
		brandstr_from++;
	}
	*brandstr_to = '\0';

	if (cpu_brandstr[0] == '\0') {
		snprintf(cpu_brandstr, 48 /* sizeof(cpu_brandstr) */,
		    "%s %s%s", vendorname, modifier, name);
	}

	if (cachesize > -1) {
		snprintf(cpu_model, sizeof(cpu_model),
		    "%s (%s%s%s%s-class, %dKB L2 cache)",
		    cpu_brandstr,
		    ((*token) ? "\"" : ""), ((*token) ? token : ""),
		    ((*token) ? "\" " : ""), classnames[class], cachesize);
	} else {
		snprintf(cpu_model, sizeof(cpu_model),
		    "%s (%s%s%s%s-class)",
		    cpu_brandstr,
		    ((*token) ? "\"" : ""), ((*token) ? token : ""),
		    ((*token) ? "\" " : ""), classnames[class]);
	}

	printf("%s: %s", cpu_device, cpu_model);

#if defined(I586_CPU) || defined(I686_CPU)
	if (cpu_feature & CPUID_TSC) {			/* Has TSC */
		calibrate_cyclecounter();
		if (pentium_mhz > 994) {
			int ghz, fr;

			ghz = (pentium_mhz + 9) / 1000;
			fr = ((pentium_mhz + 9) / 10 ) % 100;
			if (fr)
				printf(" %d.%02d GHz", ghz, fr);
			else
				printf(" %d GHz", ghz);
		} else
			printf(" %d MHz", pentium_mhz);
	}
#endif
	printf("\n");

	if (cpu_feature) {
		int numbits = 0;

		printf("%s: ", cpu_device);
		max = sizeof(i386_cpuid_features)
			/ sizeof(i386_cpuid_features[0]);
		for (i = 0; i < max; i++) {
			if (cpu_feature & i386_cpuid_features[i].feature_bit) {
				printf("%s%s", (numbits == 0 ? "" : ","),
				    i386_cpuid_features[i].feature_name);
				numbits++;
			}
		}
		max = sizeof(i386_cpuid_ecxfeatures)
			/ sizeof(i386_cpuid_ecxfeatures[0]);
		for (i = 0; i < max; i++) {
			if (cpu_ecxfeature &
			    i386_cpuid_ecxfeatures[i].feature_bit) {
				printf("%s%s", (numbits == 0 ? "" : ","),
				    i386_cpuid_ecxfeatures[i].feature_name);
				numbits++;
			}
		}
		printf("\n");
	}

	/* configure the CPU if needed */
	if (cpu_setup != NULL)
		cpu_setup(cpu_device, model, step);

#ifndef SMALL_KERNEL
#if defined(I586_CPU) || defined(I686_CPU)
	if (cpu_cpuspeed == NULL && pentium_mhz != 0)
		cpu_cpuspeed = pentium_cpuspeed;
#endif
#endif

	cpu_class = class;

	/*
	 * Now that we have told the user what they have,
	 * let them know if that machine type isn't configured.
	 */
	switch (cpu_class) {
#if !defined(I386_CPU) && !defined(I486_CPU) && !defined(I586_CPU) && !defined(I686_CPU)
#error No CPU classes configured.
#endif
#ifndef I686_CPU
	case CPUCLASS_686:
		printf("NOTICE: this kernel does not support Pentium Pro CPU class\n");
#ifdef I586_CPU
		printf("NOTICE: lowering CPU class to i586\n");
		cpu_class = CPUCLASS_586;
		break;
#endif
#endif
#ifndef I586_CPU
	case CPUCLASS_586:
		printf("NOTICE: this kernel does not support Pentium CPU class\n");
#ifdef I486_CPU
		printf("NOTICE: lowering CPU class to i486\n");
		cpu_class = CPUCLASS_486;
		break;
#endif
#endif
#ifndef I486_CPU
	case CPUCLASS_486:
		printf("NOTICE: this kernel does not support i486 CPU class\n");
#ifdef I386_CPU
		printf("NOTICE: lowering CPU class to i386\n");
		cpu_class = CPUCLASS_386;
		break;
#endif
#endif
#ifndef I386_CPU
	case CPUCLASS_386:
		printf("NOTICE: this kernel does not support i386 CPU class\n");
		panic("no appropriate CPU class available");
#endif
	default:
		break;
	}

	if (cpu == CPU_486DLC) {
#ifndef CYRIX_CACHE_WORKS
		printf("WARNING: CYRIX 486DLC CACHE UNCHANGED.\n");
#else
#ifndef CYRIX_CACHE_REALLY_WORKS
		printf("WARNING: CYRIX 486DLC CACHE ENABLED IN HOLD-FLUSH MODE.\n");
#else
		printf("WARNING: CYRIX 486DLC CACHE ENABLED.\n");
#endif
#endif
	}

#if defined(I486_CPU) || defined(I586_CPU) || defined(I686_CPU)
	/*
	 * On a 486 or above, enable ring 0 write protection.
	 */
	if (cpu_class >= CPUCLASS_486)
		lcr0(rcr0() | CR0_WP);
#endif

#if defined(I686_CPU)
	/*
	 * If we have FXSAVE/FXRESTOR, use them.
	 */
	if (cpu_feature & CPUID_FXSR) {
		i386_use_fxsave = 1;
		lcr4(rcr4() | CR4_OSFXSR);

		/*
		 * If we have SSE/SSE2, enable XMM exceptions, and
		 * notify userland.
		 */
		if (cpu_feature & (CPUID_SSE|CPUID_SSE2)) {
			if (cpu_feature & CPUID_SSE)
				i386_has_sse = 1;
			if (cpu_feature & CPUID_SSE2)
				i386_has_sse2 = 1;
			lcr4(rcr4() | CR4_OSXMMEXCPT);
		}
	} else
		i386_use_fxsave = 0;
#endif /* I686_CPU */

}

#ifndef SMALL_KERNEL
#ifdef I686_CPU
int
p4_cpuspeed(int *freq)
{
	u_int64_t msr;
	int bus, mult;

	msr = rdmsr(MSR_EBC_FREQUENCY_ID);
	if (p4_model < 2) {
		bus = (msr >> 21) & 0x7;
		switch (bus) {
		case 0:
			bus = 100;
			break;
		case 1:
			bus = 133;
			break;
		}
	} else {
		bus = (msr >> 16) & 0x7;
		switch (bus) {
		case 0:
			bus = 100;
			break;
		case 1:
			bus = 133;
			break;
		case 2:
			bus = 200;
			break;
		}
	}
	mult = ((msr >> 24) & 0xff);
	*freq = bus * mult;
	/* 133MHz actually means 133.(3)MHz */
	if (bus == 133)
		*freq += mult / 3;

	return (0);
}
#endif	/* I686_CPU */

#if defined(I586_CPU) || defined(I686_CPU)
int
pentium_cpuspeed(int *freq)
{
	*freq = pentium_mhz;
	return (0);
}
#endif
#endif	/* !SMALL_KERNEL */

#ifdef COMPAT_IBCS2
void ibcs2_sendsig(sig_t, int, int, u_long, int, union sigval);

void
ibcs2_sendsig(catcher, sig, mask, code, type, val)
	sig_t catcher;
	int sig, mask;
	u_long code;
	int type;
	union sigval val;
{
	extern int bsd_to_ibcs2_sig[];

	sendsig(catcher, bsd_to_ibcs2_sig[sig], mask, code, type, val);
}
#endif

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
sendsig(catcher, sig, mask, code, type, val)
	sig_t catcher;
	int sig, mask;
	u_long code;
	int type;
	union sigval val;
{
	struct proc *p = curproc;
	struct pmap *pmap = vm_map_pmap(&p->p_vmspace->vm_map);
	struct trapframe *tf = p->p_md.md_regs;
	struct sigframe *fp, frame;
	struct sigacts *psp = p->p_sigacts;
	int oonstack = psp->ps_sigstk.ss_flags & SS_ONSTACK;

	/* 
	 * Build the argument list for the signal handler.
	 */
	frame.sf_signum = sig;

	/*
	 * Allocate space for the signal handler context.
	 */
	if ((psp->ps_flags & SAS_ALTSTACK) && !oonstack &&
	    (psp->ps_sigonstack & sigmask(sig))) {
		fp = (struct sigframe *)(psp->ps_sigstk.ss_sp +
		    psp->ps_sigstk.ss_size - sizeof(struct sigframe));
		psp->ps_sigstk.ss_flags |= SS_ONSTACK;
	} else {
		fp = (struct sigframe *)tf->tf_esp - 1;
	}

	frame.sf_scp = &fp->sf_sc;
	frame.sf_sip = NULL;
	frame.sf_handler = catcher;

	/*
	 * Build the signal context to be used by sigreturn.
	 */
	frame.sf_sc.sc_err = tf->tf_err;
	frame.sf_sc.sc_trapno = tf->tf_trapno;
	frame.sf_sc.sc_onstack = oonstack;
	frame.sf_sc.sc_mask = mask;
#ifdef VM86
	if (tf->tf_eflags & PSL_VM) {
		frame.sf_sc.sc_gs = tf->tf_vm86_gs;
		frame.sf_sc.sc_fs = tf->tf_vm86_fs;
		frame.sf_sc.sc_es = tf->tf_vm86_es;
		frame.sf_sc.sc_ds = tf->tf_vm86_ds;
		frame.sf_sc.sc_eflags = get_vflags(p);
	} else
#endif
	{
		__asm("movw %%gs,%w0" : "=r" (frame.sf_sc.sc_gs));
		__asm("movw %%fs,%w0" : "=r" (frame.sf_sc.sc_fs));
		frame.sf_sc.sc_es = tf->tf_es;
		frame.sf_sc.sc_ds = tf->tf_ds;
		frame.sf_sc.sc_eflags = tf->tf_eflags;
	}
	frame.sf_sc.sc_edi = tf->tf_edi;
	frame.sf_sc.sc_esi = tf->tf_esi;
	frame.sf_sc.sc_ebp = tf->tf_ebp;
	frame.sf_sc.sc_ebx = tf->tf_ebx;
	frame.sf_sc.sc_edx = tf->tf_edx;
	frame.sf_sc.sc_ecx = tf->tf_ecx;
	frame.sf_sc.sc_eax = tf->tf_eax;
	frame.sf_sc.sc_eip = tf->tf_eip;
	frame.sf_sc.sc_cs = tf->tf_cs;
	frame.sf_sc.sc_esp = tf->tf_esp;
	frame.sf_sc.sc_ss = tf->tf_ss;

	if (psp->ps_siginfo & sigmask(sig)) {
		frame.sf_sip = &fp->sf_si;
		initsiginfo(&frame.sf_si, sig, code, type, val);
#ifdef VM86
		if (sig == SIGURG)	/* VM86 userland trap */
			frame.sf_si.si_trapno = code;
#endif
	}

	/* XXX don't copyout siginfo if not needed? */
	if (copyout(&frame, fp, sizeof(frame)) != 0) {
		/*
		 * Process has trashed its stack; give it an illegal
		 * instruction to halt it in its tracks.
		 */
		sigexit(p, SIGILL);
		/* NOTREACHED */
	}

	/*
	 * Build context to run handler in.
	 */
	__asm("movw %w0,%%gs" : : "r" (GSEL(GUDATA_SEL, SEL_UPL)));
	__asm("movw %w0,%%fs" : : "r" (GSEL(GUDATA_SEL, SEL_UPL)));
	tf->tf_es = GSEL(GUDATA_SEL, SEL_UPL);
	tf->tf_ds = GSEL(GUDATA_SEL, SEL_UPL);
	tf->tf_eip = p->p_sigcode;
	tf->tf_cs = pmap->pm_hiexec > I386_MAX_EXE_ADDR ? 
	    GSEL(GUCODE1_SEL, SEL_UPL) : GSEL(GUCODE_SEL, SEL_UPL);
	tf->tf_eflags &= ~(PSL_T|PSL_VM|PSL_AC);
	tf->tf_esp = (int)fp;
	tf->tf_ss = GSEL(GUDATA_SEL, SEL_UPL);
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
sys_sigreturn(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct sys_sigreturn_args /* {
		syscallarg(struct sigcontext *) sigcntxp;
	} */ *uap = v;
	struct sigcontext *scp, context;
	register struct trapframe *tf;

	tf = p->p_md.md_regs;

	/*
	 * The trampoline code hands us the context.
	 * It is unsafe to keep track of it ourselves, in the event that a
	 * program jumps out of a signal handler.
	 */
	scp = SCARG(uap, sigcntxp);
	if (copyin((caddr_t)scp, &context, sizeof(*scp)) != 0)
		return (EFAULT);

	/*
	 * Restore signal context.
	 */
#ifdef VM86
	if (context.sc_eflags & PSL_VM) {
		tf->tf_vm86_gs = context.sc_gs;
		tf->tf_vm86_fs = context.sc_fs;
		tf->tf_vm86_es = context.sc_es;
		tf->tf_vm86_ds = context.sc_ds;
		set_vflags(p, context.sc_eflags);
	} else
#endif
	{
		/*
		 * Check for security violations.  If we're returning to
		 * protected mode, the CPU will validate the segment registers
		 * automatically and generate a trap on violations.  We handle
		 * the trap, rather than doing all of the checking here.
		 */
		if (((context.sc_eflags ^ tf->tf_eflags) & PSL_USERSTATIC) != 0 ||
		    !USERMODE(context.sc_cs, context.sc_eflags))
			return (EINVAL);

		/* %fs and %gs were restored by the trampoline. */
		tf->tf_es = context.sc_es;
		tf->tf_ds = context.sc_ds;
		tf->tf_eflags = context.sc_eflags;
	}
	tf->tf_edi = context.sc_edi;
	tf->tf_esi = context.sc_esi;
	tf->tf_ebp = context.sc_ebp;
	tf->tf_ebx = context.sc_ebx;
	tf->tf_edx = context.sc_edx;
	tf->tf_ecx = context.sc_ecx;
	tf->tf_eax = context.sc_eax;
	tf->tf_eip = context.sc_eip;
	tf->tf_cs = context.sc_cs;
	tf->tf_esp = context.sc_esp;
	tf->tf_ss = context.sc_ss;

	if (context.sc_onstack & 01)
		p->p_sigacts->ps_sigstk.ss_flags |= SS_ONSTACK;
	else
		p->p_sigacts->ps_sigstk.ss_flags &= ~SS_ONSTACK;
	p->p_sigmask = context.sc_mask & ~sigcantmask;

	return (EJUSTRETURN);
}

int	waittime = -1;
struct pcb dumppcb;

void
boot(howto)
	int howto;
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
		extern struct proc proc0;

		/* protect against curproc->p_stats.foo refs in sync()   XXX */
		if (curproc == NULL)
			curproc = &proc0;

		waittime = 0;
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

	if (howto & RB_HALT) {
#if NAPM > 0
		if (howto & RB_POWERDOWN) {
			int rv;

			printf("\nAttempting to power down...\n");
			/*
			 * Turn off, if we can.  But try to turn disk off and
		 	 * wait a bit first--some disk drives are slow to
			 * clean up and users have reported disk corruption.
			 *
			 * If apm_set_powstate() fails the first time, don't
			 * try to turn the system off.
		 	 */
			delay(500000);
			/*
			 * It's been reported that the following bit of code
			 * is required on most systems <mickey@openbsd.org>
			 * but cause powerdown problem on other systems
			 * <smcho@tsp.korea.ac.kr>.  Use sysctl to set
			 * apmhalt to a non-zero value to skip the offending
			 * code.
			 */
			if (!cpu_apmhalt) {
				apm_set_powstate(APM_DEV_DISK(0xff),
						 APM_SYS_OFF);
				delay(500000);
			}
			rv = apm_set_powstate(APM_DEV_DISK(0xff), APM_SYS_OFF);
			if (rv == 0 || rv == ENXIO) {
				delay(500000);
				(void) apm_set_powstate(APM_DEV_ALLDEVS,
							APM_SYS_OFF);
			}
		}
#endif
		printf("\n");
		printf("The operating system has halted.\n");
		printf("Please press any key to reboot.\n\n");
		cngetc();
	}

	printf("rebooting...\n");
	cpu_reset();
	for(;;) ;
	/*NOTREACHED*/
}

/*
 * This is called by configure to set dumplo and dumpsize.
 * Dumps always skip the first block of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void
dumpconf()
{
	int nblks;	/* size of dump area */
	register int maj, i;

	if (dumpdev == NODEV)
		return;
	maj = major(dumpdev);
	if (maj < 0 || maj >= nblkdev)
		panic("dumpconf: bad dumpdev=0x%x", dumpdev);
	if (bdevsw[maj].d_psize == NULL)
		return;
	nblks = (*bdevsw[maj].d_psize)(dumpdev);
	if (nblks <= ctod(1))
		return;

	/* Always skip the first block, in case there is a label there. */
	if (dumplo < ctod(1))
		dumplo = ctod(1);

	for (i = 0; i < ndumpmem; i++)
		dumpsize = max(dumpsize, dumpmem[i].end);

	/* Put dump at end of partition, and make it fit. */
	if (dumpsize > dtoc(nblks - dumplo - 1))
		dumpsize = dtoc(nblks - dumplo - 1);
	if (dumplo < nblks - ctod(dumpsize) - 1)
		dumplo = nblks - ctod(dumpsize) - 1;
}

/*
 * cpu_dump: dump machine-dependent kernel core dump headers.
 */
int
cpu_dump()
{
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	long buf[dbtob(1) / sizeof (long)];
	kcore_seg_t	*segp;

	dump = bdevsw[major(dumpdev)].d_dump;

	segp = (kcore_seg_t *)buf;

	/*
	 * Generate a segment header.
	 */
	CORE_SETMAGIC(*segp, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	segp->c_size = dbtob(1) - ALIGN(sizeof(*segp));

	return (dump(dumpdev, dumplo, (caddr_t)buf, dbtob(1)));
}

/*
 * Doadump comes here after turning off memory management and
 * getting on the dump stack, either when called above, or by
 * the auto-restart code.
 */
static vaddr_t dumpspace;

vaddr_t
reserve_dumppages(vaddr_t p)
{

	dumpspace = p;
	return (p + PAGE_SIZE);
}

void
dumpsys()
{
	register u_int i, j, npg;
	register int maddr;
	daddr_t blkno;
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	int error;
	register char *str;
	extern int msgbufmapped;

	/* Save registers. */
	savectx(&dumppcb);

	msgbufmapped = 0;	/* don't record dump msgs in msgbuf */
	if (dumpdev == NODEV)
		return;

	/*
	 * For dumps during autoconfiguration,
	 * if dump device has already configured...
	 */
	if (dumpsize == 0)
		dumpconf();
	if (dumplo < 0)
		return;
	printf("\ndumping to dev %x, offset %ld\n", dumpdev, dumplo);

	error = (*bdevsw[major(dumpdev)].d_psize)(dumpdev);
	printf("dump ");
	if (error == -1) {
		printf("area unavailable\n");
		return;
	}

#if 0	/* XXX this doesn't work.  grr. */
	/* toss any characters present prior to dump */
	while (sget() != NULL); /*syscons and pccons differ */
#endif

	/* scan through the dumpmem list */
	dump = bdevsw[major(dumpdev)].d_dump;
	error = cpu_dump();
	for (i = 0; !error && i < ndumpmem; i++) {

		npg = dumpmem[i].end - dumpmem[i].start;
		maddr = ctob(dumpmem[i].start);
		blkno = dumplo + btodb(maddr) + 1;
#if 0
		printf("(%d %ld %d) ", maddr, blkno, npg);
#endif
		for (j = npg; j--; maddr += NBPG, blkno += btodb(NBPG)) {

			/* Print out how many MBs we have more to go. */
			if (dbtob(blkno - dumplo) % (1024 * 1024) < NBPG)
				printf("%d ",
				    (ctob(dumpsize) - maddr) / (1024 * 1024));
#if 0
			printf("(%x %d) ", maddr, blkno);
#endif
			pmap_enter(pmap_kernel(), dumpspace, maddr,
			    VM_PROT_READ, PMAP_WIRED);
			if ((error = (*dump)(dumpdev, blkno,
			    (caddr_t)dumpspace, NBPG)))
				break;

#if 0	/* XXX this doesn't work.  grr. */
			/* operator aborting dump? */
			if (sget() != NULL) {
				error = EINTR;
				break;
			}
#endif
		}
	}

	switch (error) {

	case 0:		str = "succeeded\n\n";			break;
	case ENXIO:	str = "device bad\n\n";			break;
	case EFAULT:	str = "device not ready\n\n";		break;
	case EINVAL:	str = "area improper\n\n";		break;
	case EIO:	str = "i/o error\n\n";			break;
	case EINTR:	str = "aborted from console\n\n";	break;
	default:	str = "error %d\n\n";			break;
	}
	printf(str, error);

	delay(5000000);		/* 5 seconds */
}

#ifdef HZ
/*
 * If HZ is defined we use this code, otherwise the code in
 * /sys/arch/i386/i386/microtime.s is used.  The other code only works
 * for HZ=100.
 */
void
microtime(tvp)
	register struct timeval *tvp;
{
	int s = splhigh();

	*tvp = time;
	tvp->tv_usec += tick;
	splx(s);
	while (tvp->tv_usec >= 1000000) {
		tvp->tv_sec++;
		tvp->tv_usec -= 1000000;
	}
}
#endif /* HZ */

/*
 * Clear registers on exec
 */
void
setregs(p, pack, stack, retval)
	struct proc *p;
	struct exec_package *pack;
	u_long stack;
	register_t *retval;
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct pmap *pmap = vm_map_pmap(&p->p_vmspace->vm_map);
	struct trapframe *tf = p->p_md.md_regs;

#if NNPX > 0
	/* If we were using the FPU, forget about it. */
	if (npxproc == p)
		npxdrop();
#endif

#ifdef USER_LDT
	pmap_ldt_cleanup(p);
#endif

	p->p_md.md_flags &= ~MDP_USEDFPU;
	if (i386_use_fxsave) {
		pcb->pcb_savefpu.sv_xmm.sv_env.en_cw = __OpenBSD_NPXCW__;
		pcb->pcb_savefpu.sv_xmm.sv_env.en_mxcsr = __INITIAL_MXCSR__;
	} else
		pcb->pcb_savefpu.sv_87.sv_env.en_cw = __OpenBSD_NPXCW__;

	__asm("movw %w0,%%gs" : : "r" (LSEL(LUDATA_SEL, SEL_UPL)));
	__asm("movw %w0,%%fs" : : "r" (LSEL(LUDATA_SEL, SEL_UPL)));
	tf->tf_es = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_ds = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_ebp = 0;
	tf->tf_ebx = (int)PS_STRINGS;
	tf->tf_eip = pack->ep_entry;
	tf->tf_cs = pmap->pm_hiexec > I386_MAX_EXE_ADDR ? 
	    LSEL(LUCODE1_SEL, SEL_UPL) : LSEL(LUCODE_SEL, SEL_UPL);
	tf->tf_eflags = PSL_USERSET;
	tf->tf_esp = stack;
	tf->tf_ss = LSEL(LUDATA_SEL, SEL_UPL);

	retval[1] = 0;
}

/*
 * Initialize segments and descriptor tables
 */

union descriptor gdt[NGDT];
union descriptor ldt[NLDT];
struct gate_descriptor idt_region[NIDT];
struct gate_descriptor *idt = idt_region;

extern  struct user *proc0paddr;

void
setgate(gd, func, args, type, dpl, seg)
	struct gate_descriptor *gd;
	void *func;
	int args, type, dpl, seg;
{

	gd->gd_looffset = (int)func;
	gd->gd_selector = GSEL(seg, SEL_KPL);
	gd->gd_stkcpy = args;
	gd->gd_xx = 0;
	gd->gd_type = type;
	gd->gd_dpl = dpl;
	gd->gd_p = 1;
	gd->gd_hioffset = (int)func >> 16;
}

void
setregion(rd, base, limit)
	struct region_descriptor *rd;
	void *base;
	size_t limit;
{

	rd->rd_limit = (int)limit;
	rd->rd_base = (int)base;
}

void
setsegment(sd, base, limit, type, dpl, def32, gran)
	struct segment_descriptor *sd;
	void *base;
	size_t limit;
	int type, dpl, def32, gran;
{

	sd->sd_lolimit = (int)limit;
	sd->sd_lobase = (int)base;
	sd->sd_type = type;
	sd->sd_dpl = dpl;
	sd->sd_p = 1;
	sd->sd_hilimit = (int)limit >> 16;
	sd->sd_xx = 0;
	sd->sd_def32 = def32;
	sd->sd_gran = gran;
	sd->sd_hibase = (int)base >> 24;
}

#define	IDTVEC(name)	__CONCAT(X, name)
extern int IDTVEC(div), IDTVEC(dbg), IDTVEC(nmi), IDTVEC(bpt), IDTVEC(ofl),
    IDTVEC(bnd), IDTVEC(ill), IDTVEC(dna), IDTVEC(dble), IDTVEC(fpusegm),
    IDTVEC(tss), IDTVEC(missing), IDTVEC(stk), IDTVEC(prot), IDTVEC(page),
    IDTVEC(rsvd), IDTVEC(fpu), IDTVEC(align), IDTVEC(syscall),
    IDTVEC(osyscall);

#if defined(I586_CPU)
extern int IDTVEC(f00f_redirect);

int cpu_f00f_bug = 0;

void
fix_f00f(void)
{
	struct region_descriptor region;
	vaddr_t va;
	pt_entry_t *pte;
	void *p;

	/* Allocate two new pages */
	va = uvm_km_zalloc(kernel_map, NBPG*2);
	p = (void *)(va + NBPG - 7*sizeof(*idt));

	/* Copy over old IDT */
	bcopy(idt, p, sizeof(idt_region));
	idt = p;

	/* Fix up paging redirect */
	setgate(&idt[ 14], &IDTVEC(f00f_redirect), 0, SDT_SYS386TGT,
		SEL_KPL, GCODE_SEL);

	/* Map first page RO */
	pte = PTE_BASE + i386_btop(va);
	*pte &= ~PG_RW;

	/* Reload idtr */
	setregion(&region, idt, sizeof(idt_region) - 1);
	lidt(&region);

	/* Tell the rest of the world */
	cpu_f00f_bug = 1;
}
#endif

void
init386(paddr_t first_avail)
{
	int i;
	struct region_descriptor region;
	bios_memmap_t *im;

	proc0.p_addr = proc0paddr;
	curpcb = &proc0.p_addr->u_pcb;

	/*
	 * Initialize the I/O port and I/O mem extent maps.
	 * Note: we don't have to check the return value since
	 * creation of a fixed extent map will never fail (since
	 * descriptor storage has already been allocated).
	 *
	 * N.B. The iomem extent manages _all_ physical addresses
	 * on the machine.  When the amount of RAM is found, the two
	 * extents of RAM are allocated from the map (0 -> ISA hole
	 * and end of ISA hole -> end of RAM).
	 */
	ioport_ex = extent_create("ioport", 0x0, 0xffff, M_DEVBUF,
	    (caddr_t)ioport_ex_storage, sizeof(ioport_ex_storage),
	    EX_NOCOALESCE|EX_NOWAIT);
	iomem_ex = extent_create("iomem", 0x0, 0xffffffff, M_DEVBUF,
	    (caddr_t)iomem_ex_storage, sizeof(iomem_ex_storage),
	    EX_NOCOALESCE|EX_NOWAIT);

	/* make gdt gates and memory segments */
	setsegment(&gdt[GCODE_SEL].sd, 0, 0xfffff, SDT_MEMERA, SEL_KPL, 1, 1);
	setsegment(&gdt[GICODE_SEL].sd, 0, 0xfffff, SDT_MEMERA, SEL_KPL, 1, 1);
	setsegment(&gdt[GDATA_SEL].sd, 0, 0xfffff, SDT_MEMRWA, SEL_KPL, 1, 1);
	setsegment(&gdt[GLDT_SEL].sd, ldt, sizeof(ldt) - 1, SDT_SYSLDT, SEL_KPL,
	    0, 0);
	setsegment(&gdt[GUCODE1_SEL].sd, 0, i386_btop(VM_MAXUSER_ADDRESS) - 1,
	    SDT_MEMERA, SEL_UPL, 1, 1);
	setsegment(&gdt[GUCODE_SEL].sd, 0, i386_btop(I386_MAX_EXE_ADDR) - 1,
	    SDT_MEMERA, SEL_UPL, 1, 1);
	setsegment(&gdt[GUDATA_SEL].sd, 0, i386_btop(VM_MAXUSER_ADDRESS) - 1,
	    SDT_MEMRWA, SEL_UPL, 1, 1);

	/* make ldt gates and memory segments */
	setgate(&ldt[LSYS5CALLS_SEL].gd, &IDTVEC(osyscall), 1, SDT_SYS386CGT,
	    SEL_UPL, GCODE_SEL);
	ldt[LUCODE1_SEL] = gdt[GUCODE1_SEL];
	ldt[LUCODE_SEL] = gdt[GUCODE_SEL];
	ldt[LUDATA_SEL] = gdt[GUDATA_SEL];
	ldt[LBSDICALLS_SEL] = ldt[LSYS5CALLS_SEL];

	/* exceptions */
	setgate(&idt[  0], &IDTVEC(div),     0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  1], &IDTVEC(dbg),     0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  2], &IDTVEC(nmi),     0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  3], &IDTVEC(bpt),     0, SDT_SYS386TGT, SEL_UPL, GCODE_SEL);
	setgate(&idt[  4], &IDTVEC(ofl),     0, SDT_SYS386TGT, SEL_UPL, GCODE_SEL);
	setgate(&idt[  5], &IDTVEC(bnd),     0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  6], &IDTVEC(ill),     0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  7], &IDTVEC(dna),     0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  8], &IDTVEC(dble),    0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  9], &IDTVEC(fpusegm), 0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 10], &IDTVEC(tss),     0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 11], &IDTVEC(missing), 0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 12], &IDTVEC(stk),     0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 13], &IDTVEC(prot),    0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 14], &IDTVEC(page),    0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 15], &IDTVEC(rsvd),    0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 16], &IDTVEC(fpu),     0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 17], &IDTVEC(align),   0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 18], &IDTVEC(rsvd),    0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	for (i = 19; i < NIDT; i++)
		setgate(&idt[i], &IDTVEC(rsvd), 0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[128], &IDTVEC(syscall), 0, SDT_SYS386TGT, SEL_UPL, GCODE_SEL);

	setregion(&region, gdt, sizeof(gdt) - 1);
	lgdt(&region);
	setregion(&region, idt, sizeof(idt_region) - 1);
	lidt(&region);

#if NISA > 0
	isa_defaultirq();
#endif

	consinit();	/* XXX SHOULD NOT BE DONE HERE */
			/* XXX here, until we can use bios for printfs */

	/*
	 * Saving SSE registers won't work if the save area isn't
	 * 16-byte aligned.
	 */
	if (offsetof(struct user, u_pcb.pcb_savefpu) & 0xf)
		panic("init386: pcb_savefpu not 16-byte aligned");

	/* call pmap initialization to make new kernel address space */
	pmap_bootstrap((vaddr_t)atdevbase + IOM_SIZE);

	/*
	 * Boot arguments are in a single page specified by /boot.
	 *
	 * We require the "new" vector form, as well as memory ranges
	 * to be given in bytes rather than KB.
	 */
	if ((bootapiver & (BAPIV_VECTOR | BAPIV_BMEMMAP)) ==
	    (BAPIV_VECTOR | BAPIV_BMEMMAP)) {
		if (bootargc > NBPG)
			panic("too many boot args");

		if (extent_alloc_region(iomem_ex, (paddr_t)bootargv, bootargc,
		    EX_NOWAIT))
			panic("cannot reserve /boot args memory");

		pmap_enter(pmap_kernel(), (vaddr_t)bootargp, (paddr_t)bootargv,
		    VM_PROT_READ|VM_PROT_WRITE,
		    VM_PROT_READ|VM_PROT_WRITE|PMAP_WIRED);

		bios_getopt();

	} else
		panic("/boot too old: upgrade!");

#ifdef DIAGNOSTIC
	if (bios_memmap == NULL)
		panic("no BIOS memory map supplied");
#endif

	/*
	 * account all the memory passed in the map from /boot
	 * calculate avail_end and count the physmem.
	 */
	avail_end = 0;
	physmem = 0;
#ifdef DEBUG
	printf("memmap:");
#endif
	for(i = 0, im = bios_memmap; im->type != BIOS_MAP_END; im++)
		if (im->type == BIOS_MAP_FREE) {
			register paddr_t a, e;

			a = i386_round_page(im->addr);
			e = i386_trunc_page(im->addr + im->size);
			/* skip first four pages */
			if (a < 4 * NBPG)
				a = 4 * NBPG;
#ifdef DEBUG
			printf(" %u-%u", a, e);
#endif

			/* skip shorter than page regions */
			if (a >= e || (e - a) < NBPG) {
#ifdef DEBUG
				printf("-S");
#endif
				continue;
			}
			if ((a > IOM_BEGIN && a < IOM_END) ||
			    (e > IOM_BEGIN && e < IOM_END)) {
#ifdef DEBUG
				printf("-I");
#endif
				continue;
			}

			if (extent_alloc_region(iomem_ex, a, e - a, EX_NOWAIT))
				/* XXX What should we do? */
				printf("\nWARNING: CAN'T ALLOCATE RAM (%x-%x)"
				    " FROM IOMEM EXTENT MAP!\n", a, e);

			physmem += atop(e - a);
			dumpmem[i].start = atop(a);
			dumpmem[i].end = atop(e);
			i++;
			avail_end = max(avail_end, e);
		}

	ndumpmem = i;
	avail_end -= i386_round_page(MSGBUFSIZE);

#ifdef DEBUG
	printf(": %lx\n", avail_end);
#endif
	if (physmem < atop(4 * 1024 * 1024)) {
		printf("\awarning: too little memory available;"
		    "running in degraded mode\npress a key to confirm\n\n");
		cngetc();
	}

#ifdef DEBUG
	printf("physload: ");
#endif
	for (i = 0; i < ndumpmem; i++) {
		paddr_t a, e;
		paddr_t lim;

		a = dumpmem[i].start;
		e = dumpmem[i].end;
		if (a < atop(first_avail) && e > atop(first_avail))
			a = atop(first_avail);
		if (e > atop(avail_end))
			e = atop(avail_end);

		if (a < e) {
			if (a < atop(16 * 1024 * 1024)) {
				lim = MIN(atop(16 * 1024 * 1024), e);
#ifdef DEBUG
				printf(" %x-%x (<16M)", a, lim);
#endif
				uvm_page_physload(a, lim, a, lim,
				    VM_FREELIST_FIRST16);
				if (e > lim) {
#ifdef DEBUG
					printf(" %x-%x", lim, e);
#endif
					uvm_page_physload(lim, e, lim, e,
					    VM_FREELIST_DEFAULT);
				}
			} else {
#ifdef DEBUG
				printf(" %x-%x", a, e);
#endif
				uvm_page_physload(a, e, a, e,
				    VM_FREELIST_DEFAULT);
			}
		}
	}
#ifdef DEBUG
	printf("\n");
#endif
	tlbflush();
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
	if (extmem > (15*1024) && extmem < (16*1024)) {
		printf("Warning: ignoring %dk of remapped memory\n",
		    extmem - (15*1024));
		extmem = (15*1024);
	}
#endif
#endif

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
#endif /* KGDB */
}

struct queue {
	struct queue *q_next, *q_prev;
};

/*
 * insert an element into a queue
 */
void
_insque(v1, v2)
	void *v1;
	void *v2;
{
	register struct queue *elem = v1, *head = v2;
	register struct queue *next;

	next = head->q_next;
	elem->q_next = next;
	head->q_next = elem;
	elem->q_prev = head;
	next->q_prev = elem;
}

/*
 * remove an element from a queue
 */
void
_remque(v)
	void *v;
{
	register struct queue *elem = v;
	register struct queue *next, *prev;

	next = elem->q_next;
	prev = elem->q_prev;
	next->q_prev = prev;
	prev->q_next = next;
	elem->q_prev = 0;
}

/*
 * cpu_exec_aout_makecmds():
 *	cpu-dependent a.out format hook for execve().
 *
 * Determine of the given exec package refers to something which we
 * understand and, if so, set up the vmcmds for it.
 */
int
cpu_exec_aout_makecmds(p, epp)
	struct proc *p;
	struct exec_package *epp;
{
	return ENOEXEC;
}

/*
 * consinit:
 * initialize the system console.
 * XXX - shouldn't deal with this initted thing, but then,
 * it shouldn't be called from init386 either.
 */
void
consinit()
{
	static int initted;

	if (initted)
		return;
	initted = 1;
	cninit();
}

#if (NPCKBC > 0) && (NPCKBD == 0)
/*
 * glue code to support old console code with the
 * mi keyboard controller driver
 */
int
pckbc_machdep_cnattach(kbctag, kbcslot)
	pckbc_tag_t kbctag;
	pckbc_slot_t kbcslot;
{
	return (ENXIO);
}
#endif

#ifdef KGDB
void
kgdb_port_init()
{

#if (NCOM > 0 || NPCCOM > 0)
	if (!strcmp(kgdb_devname, "com") || !strcmp(kgdb_devname, "pccom")) {
		bus_space_tag_t tag = I386_BUS_SPACE_IO;
		com_kgdb_attach(tag, comkgdbaddr, comkgdbrate, COM_FREQ,
		    comkgdbmode);
	}
#endif
}
#endif /* KGDB */

void
cpu_reset()
{
	struct region_descriptor region;

	disable_intr();

	if (cpuresetfn)
		(*cpuresetfn)();

	/* Toggle the hardware reset line on the keyboard controller. */
	outb(IO_KBD + KBCMDP, KBC_PULSE0);
	delay(100000);
	outb(IO_KBD + KBCMDP, KBC_PULSE0);
	delay(100000);

	/*
	 * Try to cause a triple fault and watchdog reset by setting the
	 * IDT to point to nothing.
	 */
	bzero((caddr_t)idt, sizeof(idt_region));
	setregion(&region, idt, sizeof(idt_region) - 1);
	lidt(&region);
	__asm __volatile("divl %0,%1" : : "q" (0), "a" (0));

#if 1
	/*
	 * Try to cause a triple fault and watchdog reset by unmapping the
	 * entire address space.
	 */
	bzero((caddr_t)PTD, NBPG);
	tlbflush(); 
#endif

	for (;;);
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
	extern char cpu_vendor[];
	extern int cpu_id;
#if NAPM > 0
	extern int cpu_apmwarn;
#endif
	dev_t dev;

	switch (name[0]) {
	case CPU_CONSDEV:
		if (namelen != 1)
			return (ENOTDIR);		/* overloaded */

		if (cn_tab != NULL)
			dev = cn_tab->cn_dev;
		else
			dev = NODEV;
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
#if NBIOS > 0
	case CPU_BIOS:
		return bios_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen, p);
#endif
	case CPU_BLK2CHR:
		if (namelen != 2)
			return (ENOTDIR);		/* overloaded */
		dev = blktochr((dev_t)name[1]);
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
	case CPU_CHR2BLK:
		if (namelen != 2)
			return (ENOTDIR);		/* overloaded */
		dev = chrtoblk((dev_t)name[1]);
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
	case CPU_ALLOWAPERTURE:
#ifdef APERTURE
		if (securelevel > 0) 
			return (sysctl_rdint(oldp, oldlenp, newp, 
			    allowaperture));
		else
			return (sysctl_int(oldp, oldlenp, newp, newlen, 
			    &allowaperture));
#else
		return (sysctl_rdint(oldp, oldlenp, newp, 0));
#endif
	case CPU_CPUVENDOR:
		return (sysctl_rdstring(oldp, oldlenp, newp, cpu_vendor));
	case CPU_CPUID:
		return (sysctl_rdint(oldp, oldlenp, newp, cpu_id));
	case CPU_CPUFEATURE:
		return (sysctl_rdint(oldp, oldlenp, newp, cpu_feature));
#if NAPM > 0
	case CPU_APMWARN:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &cpu_apmwarn));
	case CPU_APMHALT:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &cpu_apmhalt));
#endif
	case CPU_KBDRESET:
		if (securelevel > 0) 
			return (sysctl_rdint(oldp, oldlenp, newp, 
			    kbd_reset));
		else
			return (sysctl_int(oldp, oldlenp, newp, newlen, 
			    &kbd_reset));
#ifdef USER_LDT
	case CPU_USERLDT:
		return (sysctl_int(oldp, oldlenp, newp, newlen,
		    &user_ldt_enable));
#endif
	case CPU_OSFXSR:
		return (sysctl_rdint(oldp, oldlenp, newp, i386_use_fxsave));
	case CPU_SSE:
		return (sysctl_rdint(oldp, oldlenp, newp, i386_has_sse));
	case CPU_SSE2:
		return (sysctl_rdint(oldp, oldlenp, newp, i386_has_sse2));
	case CPU_XCRYPT:
		return (sysctl_rdint(oldp, oldlenp, newp, i386_has_xcrypt));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

int
bus_space_map(t, bpa, size, cacheable, bshp)
	bus_space_tag_t t;
	bus_addr_t bpa;
	bus_size_t size;
	int cacheable;
	bus_space_handle_t *bshp;
{
	int error;
	struct extent *ex;

	/*
	 * Pick the appropriate extent map.
	 */
	switch (t) {
	case I386_BUS_SPACE_IO:
		ex = ioport_ex;
		break;

	case I386_BUS_SPACE_MEM:
		ex = iomem_ex;
		break;

	default:
		panic("bus_space_map: bad bus space tag");
	}

	/*
	 * Before we go any further, let's make sure that this
	 * region is available.
	 */
	error = extent_alloc_region(ex, bpa, size,
	    EX_NOWAIT | (ioport_malloc_safe ? EX_MALLOCOK : 0));
	if (error)
		return (error);

	/*
	 * For I/O space, that's all she wrote.
	 */
	if (t == I386_BUS_SPACE_IO) {
		*bshp = bpa;
		return (0);
	}

	if (IOM_BEGIN <= bpa && bpa <= IOM_END) {
		*bshp = (bus_space_handle_t)ISA_HOLE_VADDR(bpa);
		return (0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	error = bus_mem_add_mapping(bpa, size, cacheable, bshp);
	if (error) {
		if (extent_free(ex, bpa, size, EX_NOWAIT |
		    (ioport_malloc_safe ? EX_MALLOCOK : 0))) {
			printf("bus_space_map: pa 0x%lx, size 0x%lx\n",
			    bpa, size);
			printf("bus_space_map: can't free region\n");
		}
	}

	return (error);
}

int
_bus_space_map(t, bpa, size, cacheable, bshp)
	bus_space_tag_t t;
	bus_addr_t bpa;
	bus_size_t size;
	int cacheable;
	bus_space_handle_t *bshp;
{
	/*
	 * For I/O space, that's all she wrote.
	 */
	if (t == I386_BUS_SPACE_IO) {
		*bshp = bpa;
		return (0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	return (bus_mem_add_mapping(bpa, size, cacheable, bshp));
}

int
bus_space_alloc(t, rstart, rend, size, alignment, boundary, cacheable,
    bpap, bshp)
	bus_space_tag_t t;
	bus_addr_t rstart, rend;
	bus_size_t size, alignment, boundary;
	int cacheable;
	bus_addr_t *bpap;
	bus_space_handle_t *bshp;
{
	struct extent *ex;
	u_long bpa;
	int error;

	/*
	 * Pick the appropriate extent map.
	 */
	switch (t) {
	case I386_BUS_SPACE_IO:
		ex = ioport_ex;
		break;

	case I386_BUS_SPACE_MEM:
		ex = iomem_ex;
		break;

	default:
		panic("bus_space_alloc: bad bus space tag");
	}

	/*
	 * Sanity check the allocation against the extent's boundaries.
	 */
	if (rstart < ex->ex_start || rend > ex->ex_end)
		panic("bus_space_alloc: bad region start/end");

	/*
	 * Do the requested allocation.
	 */
	error = extent_alloc_subregion(ex, rstart, rend, size, alignment, 0,
	    boundary, EX_NOWAIT | (ioport_malloc_safe ?  EX_MALLOCOK : 0),
	    &bpa);

	if (error)
		return (error);

	/*
	 * For I/O space, that's all she wrote.
	 */
	if (t == I386_BUS_SPACE_IO) {
		*bshp = *bpap = bpa;
		return (0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	error = bus_mem_add_mapping(bpa, size, cacheable, bshp);
	if (error) {
		if (extent_free(iomem_ex, bpa, size, EX_NOWAIT |
		    (ioport_malloc_safe ? EX_MALLOCOK : 0))) {
			printf("bus_space_alloc: pa 0x%lx, size 0x%lx\n",
			    bpa, size);
			printf("bus_space_alloc: can't free region\n");
		}
	}

	*bpap = bpa;

	return (error);
}

int
bus_mem_add_mapping(bpa, size, cacheable, bshp)
	bus_addr_t bpa;
	bus_size_t size;
	int cacheable;
	bus_space_handle_t *bshp;
{
	u_long pa, endpa;
	vaddr_t va;
	pt_entry_t *pte;
	bus_size_t map_size;

	pa = i386_trunc_page(bpa);
	endpa = i386_round_page(bpa + size);

#ifdef DIAGNOSTIC
	if (endpa <= pa && endpa != 0)
		panic("bus_mem_add_mapping: overflow");
#endif

	map_size = endpa - pa;

	va = uvm_km_valloc(kernel_map, map_size);
	if (va == 0)
		return (ENOMEM);

	*bshp = (bus_space_handle_t)(va + (bpa & PGOFSET));

	for (; map_size > 0;
	    pa += PAGE_SIZE, va += PAGE_SIZE, map_size -= PAGE_SIZE) {
		pmap_kenter_pa(va, pa, VM_PROT_READ | VM_PROT_WRITE);

		/*
		 * PG_N doesn't exist on 386's, so we assume that
		 * the mainboard has wired up device space non-cacheable
		 * on those machines.
		 */
		if (cpu_class != CPUCLASS_386) {
			pte = kvtopte(va);
			if (cacheable)
				*pte &= ~PG_N;
			else
				*pte |= PG_N;
			pmap_update_pg(va);
		}
	}
	pmap_update(pmap_kernel());

	return 0;
}

void
bus_space_unmap(t, bsh, size)
	bus_space_tag_t t;
	bus_space_handle_t bsh;
	bus_size_t size;
{
	struct extent *ex;
	u_long va, endva;
	bus_addr_t bpa;

	/*
	 * Find the correct extent and bus physical address.
	 */
	if (t == I386_BUS_SPACE_IO) {
		ex = ioport_ex;
		bpa = bsh;
	} else if (t == I386_BUS_SPACE_MEM) {
		ex = iomem_ex;
		bpa = (bus_addr_t)ISA_PHYSADDR(bsh);
		if (IOM_BEGIN <= bpa && bpa <= IOM_END)
			goto ok;

		va = i386_trunc_page(bsh);
		endva = i386_round_page(bsh + size);

#ifdef DIAGNOSTIC
		if (endva <= va)
			panic("bus_space_unmap: overflow");
#endif

		(void) pmap_extract(pmap_kernel(), va, &bpa);
		bpa += (bsh & PGOFSET);

		/*
		 * Free the kernel virtual mapping.
		 */
		uvm_km_free(kernel_map, va, endva - va);
	} else
		panic("bus_space_unmap: bad bus space tag");

ok:
	if (extent_free(ex, bpa, size,
	    EX_NOWAIT | (ioport_malloc_safe ? EX_MALLOCOK : 0))) {
		printf("bus_space_unmap: %s 0x%lx, size 0x%lx\n",
		    (t == I386_BUS_SPACE_IO) ? "port" : "pa", bpa, size);
		printf("bus_space_unmap: can't free region\n");
	}
}

void    
bus_space_free(t, bsh, size)
	bus_space_tag_t t;
	bus_space_handle_t bsh;
	bus_size_t size;
{

	/* bus_space_unmap() does all that we need to do. */
	bus_space_unmap(t, bsh, size);
}

int
bus_space_subregion(t, bsh, offset, size, nbshp)
	bus_space_tag_t t;
	bus_space_handle_t bsh;
	bus_size_t offset, size;
	bus_space_handle_t *nbshp;
{
	*nbshp = bsh + offset;
	return (0);
}

/*
 * Common function for DMA map creation.  May be called by bus-specific
 * DMA map creation functions.
 */
int
_bus_dmamap_create(t, size, nsegments, maxsegsz, boundary, flags, dmamp)
	bus_dma_tag_t t;
	bus_size_t size;
	int nsegments;
	bus_size_t maxsegsz;
	bus_size_t boundary;
	int flags;
	bus_dmamap_t *dmamp;
{
	struct i386_bus_dmamap *map;
	void *mapstore;
	size_t mapsize;

	/*
	 * Allocate and initialize the DMA map.  The end of the map
	 * is a variable-sized array of segments, so we allocate enough
	 * room for them in one shot.
	 *
	 * Note we don't preserve the WAITOK or NOWAIT flags.  Preservation
	 * of ALLOCNOW notifies others that we've reserved these resources,
	 * and they are not to be freed.
	 *
	 * The bus_dmamap_t includes one bus_dma_segment_t, hence
	 * the (nsegments - 1).
	 */
	mapsize = sizeof(struct i386_bus_dmamap) +
	    (sizeof(bus_dma_segment_t) * (nsegments - 1));
	if ((mapstore = malloc(mapsize, M_DEVBUF,
	    (flags & BUS_DMA_NOWAIT) ? M_NOWAIT : M_WAITOK)) == NULL)
		return (ENOMEM);

	bzero(mapstore, mapsize);
	map = (struct i386_bus_dmamap *)mapstore;
	map->_dm_size = size;
	map->_dm_segcnt = nsegments;
	map->_dm_maxsegsz = maxsegsz;
	map->_dm_boundary = boundary;
	map->_dm_flags = flags & ~(BUS_DMA_WAITOK|BUS_DMA_NOWAIT);
	map->dm_mapsize = 0;		/* no valid mappings */
	map->dm_nsegs = 0;

	*dmamp = map;
	return (0);
}

/*
 * Common function for DMA map destruction.  May be called by bus-specific
 * DMA map destruction functions.
 */
void
_bus_dmamap_destroy(t, map)
	bus_dma_tag_t t;
	bus_dmamap_t map;
{

	free(map, M_DEVBUF);
}

/*
 * Common function for loading a DMA map with a linear buffer.  May
 * be called by bus-specific DMA map load functions.
 */
int
_bus_dmamap_load(t, map, buf, buflen, p, flags)
	bus_dma_tag_t t;
	bus_dmamap_t map;
	void *buf;
	bus_size_t buflen;
	struct proc *p;
	int flags;
{
	bus_addr_t lastaddr;
	int seg, error;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	if (buflen > map->_dm_size)
		return (EINVAL);

	seg = 0;
	error = _bus_dmamap_load_buffer(t, map, buf, buflen, p, flags,
	    &lastaddr, &seg, 1);
	if (error == 0) {
		map->dm_mapsize = buflen;
		map->dm_nsegs = seg + 1;
	}
	return (error);
}

/*
 * Like _bus_dmamap_load(), but for mbufs.
 */
int
_bus_dmamap_load_mbuf(t, map, m0, flags)
	bus_dma_tag_t t;
	bus_dmamap_t map;
	struct mbuf *m0;
	int flags;
{
	paddr_t lastaddr;
	int seg, error, first;
	struct mbuf *m;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

#ifdef DIAGNOSTIC
	if ((m0->m_flags & M_PKTHDR) == 0)
		panic("_bus_dmamap_load_mbuf: no packet header");
#endif

	if (m0->m_pkthdr.len > map->_dm_size)
		return (EINVAL);

	first = 1;
	seg = 0;
	error = 0;
	for (m = m0; m != NULL && error == 0; m = m->m_next) {
		error = _bus_dmamap_load_buffer(t, map, m->m_data, m->m_len,
		    NULL, flags, &lastaddr, &seg, first);
		first = 0;
	}
	if (error == 0) {
		map->dm_mapsize = m0->m_pkthdr.len;
		map->dm_nsegs = seg + 1;
	}
	return (error);
}

/*
 * Like _bus_dmamap_load(), but for uios.
 */
int
_bus_dmamap_load_uio(t, map, uio, flags)
	bus_dma_tag_t t;
	bus_dmamap_t map;
	struct uio *uio;
	int flags;
{
	paddr_t lastaddr;
	int seg, i, error, first;
	bus_size_t minlen, resid;
	struct proc *p = NULL;
	struct iovec *iov;
	caddr_t addr;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	resid = uio->uio_resid;
	iov = uio->uio_iov;

	if (resid > map->_dm_size)
		return (EINVAL);

	if (uio->uio_segflg == UIO_USERSPACE) {
		p = uio->uio_procp;
#ifdef DIAGNOSTIC
		if (p == NULL)
			panic("_bus_dmamap_load_uio: USERSPACE but no proc");
#endif
	}

	first = 1;
	seg = 0;
	error = 0;
	for (i = 0; i < uio->uio_iovcnt && resid != 0 && error == 0; i++) {
		/*
		 * Now at the first iovec to load.  Load each iovec
		 * until we have exhausted the residual count.
		 */
		minlen = resid < iov[i].iov_len ? resid : iov[i].iov_len;
		addr = (caddr_t)iov[i].iov_base;

		error = _bus_dmamap_load_buffer(t, map, addr, minlen,
		    p, flags, &lastaddr, &seg, first);
		first = 0;

		resid -= minlen;
	}
	if (error == 0) {
		map->dm_mapsize = uio->uio_resid;
		map->dm_nsegs = seg + 1;
	}
	return (error);
}

/*
 * Like _bus_dmamap_load(), but for raw memory allocated with
 * bus_dmamem_alloc().
 */
int
_bus_dmamap_load_raw(t, map, segs, nsegs, size, flags)
	bus_dma_tag_t t;
	bus_dmamap_t map;
	bus_dma_segment_t *segs;
	int nsegs;
	bus_size_t size;
	int flags;
{
	if (nsegs > map->_dm_segcnt || size > map->_dm_size)
		return (EINVAL);

	/*
	 * Make sure we don't cross any boundaries.
	 */
	if (map->_dm_boundary) {
		bus_addr_t bmask = ~(map->_dm_boundary - 1);
		int i;

		for (i = 0; i < nsegs; i++) {
			if (segs[i].ds_len > map->_dm_maxsegsz)
				return (EINVAL);
			if ((segs[i].ds_addr & bmask) !=
			    ((segs[i].ds_addr + segs[i].ds_len - 1) & bmask))
				return (EINVAL);
		}
	}

	bcopy(segs, map->dm_segs, nsegs * sizeof(*segs));
	map->dm_nsegs = nsegs;
	return (0);
}

/*
 * Common function for unloading a DMA map.  May be called by
 * bus-specific DMA map unload functions.
 */
void
_bus_dmamap_unload(t, map)
	bus_dma_tag_t t;
	bus_dmamap_t map;
{

	/*
	 * No resources to free; just mark the mappings as
	 * invalid.
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;
}

/*
 * Common function for DMA map synchronization.  May be called
 * by bus-specific DMA map synchronization functions.
 */
void
_bus_dmamap_sync(t, map, addr, size, op)
	bus_dma_tag_t t;
	bus_dmamap_t map;
	bus_addr_t addr;
	bus_size_t size;
	int op;
{

	/* Nothing to do here. */
}

/*
 * Common function for DMA-safe memory allocation.  May be called
 * by bus-specific DMA memory allocation functions.
 */
int
_bus_dmamem_alloc(t, size, alignment, boundary, segs, nsegs, rsegs, flags)
	bus_dma_tag_t t;
	bus_size_t size, alignment, boundary;
	bus_dma_segment_t *segs;
	int nsegs;
	int *rsegs;
	int flags;
{

	return (_bus_dmamem_alloc_range(t, size, alignment, boundary,
	    segs, nsegs, rsegs, flags, 0, trunc_page(avail_end)));
}

/*
 * Common function for freeing DMA-safe memory.  May be called by
 * bus-specific DMA memory free functions.
 */
void
_bus_dmamem_free(t, segs, nsegs)
	bus_dma_tag_t t;
	bus_dma_segment_t *segs;
	int nsegs;
{
	struct vm_page *m;
	bus_addr_t addr;
	struct pglist mlist;
	int curseg;

	/*
	 * Build a list of pages to free back to the VM system.
	 */
	TAILQ_INIT(&mlist);
	for (curseg = 0; curseg < nsegs; curseg++) {
		for (addr = segs[curseg].ds_addr;
		    addr < (segs[curseg].ds_addr + segs[curseg].ds_len);
		    addr += PAGE_SIZE) {
			m = PHYS_TO_VM_PAGE(addr);
			TAILQ_INSERT_TAIL(&mlist, m, pageq);
		}
	}

	uvm_pglistfree(&mlist);
}

/*
 * Common function for mapping DMA-safe memory.  May be called by
 * bus-specific DMA memory map functions.
 */
int
_bus_dmamem_map(t, segs, nsegs, size, kvap, flags)
	bus_dma_tag_t t;
	bus_dma_segment_t *segs;
	int nsegs;
	size_t size;
	caddr_t *kvap;
	int flags;
{
	vaddr_t va;
	bus_addr_t addr;
	int curseg;

	size = round_page(size);
	va = uvm_km_valloc(kernel_map, size);
	if (va == 0)
		return (ENOMEM);

	*kvap = (caddr_t)va;

	for (curseg = 0; curseg < nsegs; curseg++) {
		for (addr = segs[curseg].ds_addr;
		    addr < (segs[curseg].ds_addr + segs[curseg].ds_len);
		    addr += PAGE_SIZE, va += PAGE_SIZE, size -= PAGE_SIZE) {
			if (size == 0)
				panic("_bus_dmamem_map: size botch");
			pmap_enter(pmap_kernel(), va, addr,
			    VM_PROT_READ | VM_PROT_WRITE,
			    VM_PROT_READ | VM_PROT_WRITE | PMAP_WIRED);
		}
	}
	pmap_update(pmap_kernel());

	return (0);
}

/*
 * Common function for unmapping DMA-safe memory.  May be called by
 * bus-specific DMA memory unmapping functions.
 */
void
_bus_dmamem_unmap(t, kva, size)
	bus_dma_tag_t t;
	caddr_t kva;
	size_t size;
{

#ifdef DIAGNOSTIC
	if ((u_long)kva & PGOFSET)
		panic("_bus_dmamem_unmap");
#endif

	size = round_page(size);
	uvm_km_free(kernel_map, (vaddr_t)kva, size);
}

/*
 * Common functin for mmap(2)'ing DMA-safe memory.  May be called by
 * bus-specific DMA mmap(2)'ing functions.
 */
paddr_t
_bus_dmamem_mmap(t, segs, nsegs, off, prot, flags)
	bus_dma_tag_t t;
	bus_dma_segment_t *segs;
	int nsegs;
	off_t off;
	int prot, flags;
{
	int i;

	for (i = 0; i < nsegs; i++) {
#ifdef DIAGNOSTIC
		if (off & PGOFSET)
			panic("_bus_dmamem_mmap: offset unaligned");
		if (segs[i].ds_addr & PGOFSET)
			panic("_bus_dmamem_mmap: segment unaligned");
		if (segs[i].ds_len & PGOFSET)
			panic("_bus_dmamem_mmap: segment size not multiple"
			    " of page size");
#endif
		if (off >= segs[i].ds_len) {
			off -= segs[i].ds_len;
			continue;
		}

		return (i386_btop((caddr_t)segs[i].ds_addr + off));
	}

	/* Page not found. */
	return (-1);
}

/**********************************************************************
 * DMA utility functions
 **********************************************************************/
/*
 * Utility function to load a linear buffer.  lastaddrp holds state
 * between invocations (for multiple-buffer loads).  segp contains
 * the starting segment on entrace, and the ending segment on exit.
 * first indicates if this is the first invocation of this function.
 */
int
_bus_dmamap_load_buffer(t, map, buf, buflen, p, flags, lastaddrp, segp, first)
	bus_dma_tag_t t;
	bus_dmamap_t map;
	void *buf;
	bus_size_t buflen;
	struct proc *p;
	int flags;
	paddr_t *lastaddrp;
	int *segp;
	int first;
{
	bus_size_t sgsize;
	bus_addr_t curaddr, lastaddr, baddr, bmask;
	vaddr_t vaddr = (vaddr_t)buf;
	int seg;
	pmap_t pmap;

	if (p != NULL)
		pmap = p->p_vmspace->vm_map.pmap;
	else
		pmap = pmap_kernel();

	lastaddr = *lastaddrp;
	bmask  = ~(map->_dm_boundary - 1);

	for (seg = *segp; buflen > 0 ; ) {
		/*
		 * Get the physical address for this segment.
		 */
		pmap_extract(pmap, vaddr, (paddr_t *)&curaddr);

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = PAGE_SIZE - ((u_long)vaddr & PGOFSET);
		if (buflen < sgsize)
			sgsize = buflen;

		/*
		 * Make sure we don't cross any boundaries.
		 */
		if (map->_dm_boundary > 0) {
			baddr = (curaddr + map->_dm_boundary) & bmask;
			if (sgsize > (baddr - curaddr))
				sgsize = (baddr - curaddr);
		}

		/*
		 * Insert chunk into a segment, coalescing with
		 * previous segment if possible.
		 */
		if (first) {
			map->dm_segs[seg].ds_addr = curaddr;
			map->dm_segs[seg].ds_len = sgsize;
			first = 0;
		} else {
			if (curaddr == lastaddr &&
			    (map->dm_segs[seg].ds_len + sgsize) <=
			     map->_dm_maxsegsz &&
			    (map->_dm_boundary == 0 ||
			     (map->dm_segs[seg].ds_addr & bmask) ==
			     (curaddr & bmask)))
				map->dm_segs[seg].ds_len += sgsize;
			else {
				if (++seg >= map->_dm_segcnt)
					break;
				map->dm_segs[seg].ds_addr = curaddr;
				map->dm_segs[seg].ds_len = sgsize;
			}
		}

		lastaddr = curaddr + sgsize;
		vaddr += sgsize;
		buflen -= sgsize;
	}

	*segp = seg;
	*lastaddrp = lastaddr;

	/*
	 * Did we fit?
	 */
	if (buflen != 0)
		return (EFBIG);		/* XXX better return value here? */
	return (0);
}

/*
 * Allocate physical memory from the given physical address range.
 * Called by DMA-safe memory allocation methods.
 */
int
_bus_dmamem_alloc_range(t, size, alignment, boundary, segs, nsegs, rsegs,
    flags, low, high)
	bus_dma_tag_t t;
	bus_size_t size, alignment, boundary;
	bus_dma_segment_t *segs;
	int nsegs;
	int *rsegs;
	int flags;
	paddr_t low;
	paddr_t high;
{
	paddr_t curaddr, lastaddr;
	struct vm_page *m;
	struct pglist mlist;
	int curseg, error;

	/* Always round the size. */
	size = round_page(size);

	/*
	 * Allocate pages from the VM system.
	 */
	TAILQ_INIT(&mlist);
	error = uvm_pglistalloc(size, low, high,
	    alignment, boundary, &mlist, nsegs, (flags & BUS_DMA_NOWAIT) == 0);
	if (error)
		return (error);

	/*
	 * Compute the location, size, and number of segments actually
	 * returned by the VM code.
	 */
	m = TAILQ_FIRST(&mlist);
	curseg = 0;
	lastaddr = segs[curseg].ds_addr = VM_PAGE_TO_PHYS(m);
	segs[curseg].ds_len = PAGE_SIZE;

	for (m = TAILQ_NEXT(m, pageq); m != NULL; m = TAILQ_NEXT(m, pageq)) {
		curaddr = VM_PAGE_TO_PHYS(m);
#ifdef DIAGNOSTIC
		if (curseg == nsegs) {
			printf("uvm_pglistalloc returned too many\n");
			panic("_bus_dmamem_alloc_range");
		}
		if (curaddr < low || curaddr >= high) {
			printf("uvm_pglistalloc returned non-sensical"
			    " address 0x%lx\n", curaddr);
			panic("_bus_dmamem_alloc_range");
		}
#endif
		if (curaddr == (lastaddr + PAGE_SIZE))
			segs[curseg].ds_len += PAGE_SIZE;
		else {
			curseg++;
			segs[curseg].ds_addr = curaddr;
			segs[curseg].ds_len = PAGE_SIZE;
		}
		lastaddr = curaddr;
	}

	*rsegs = curseg + 1;

	return (0);
}

#ifdef DIAGNOSTIC
void
splassert_check(int wantipl, const char *func)
{
	if (cpl < wantipl) {
		splassert_fail(wantipl, cpl, func);
	}
}
#endif

/* If SMALL_KERNEL this results in an out of line definition of splx.  */
SPLX_OUTLINED_BODY
