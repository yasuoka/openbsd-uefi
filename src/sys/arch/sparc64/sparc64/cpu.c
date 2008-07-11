/*	$OpenBSD: cpu.c,v 1.42 2008/07/11 14:23:53 kettenis Exp $	*/
/*	$NetBSD: cpu.c,v 1.13 2001/05/26 21:27:15 chs Exp $ */

/*
 * Copyright (c) 1996
 *	The President and Fellows of Harvard College. All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by Harvard University.
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Aaron Brown and
 *	Harvard University.
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
 *	@(#)cpu.c	8.5 (Berkeley) 11/23/93
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/reg.h>
#include <machine/trap.h>
#include <machine/hypervisor.h>
#include <machine/openfirm.h>
#include <machine/pmap.h>
#include <machine/sparc64.h>

#include <sparc64/sparc64/cache.h>

#include <sparc64/dev/starfire.h>

/* This is declared here so that you must include a CPU for the cache code. */
struct cacheinfo cacheinfo = {
	us_dcache_flush_page
};

/* Linked list of all CPUs in system. */
struct cpu_info *cpus = NULL;

struct cpu_info *alloc_cpuinfo(struct mainbus_attach_args *);

/* The following are used externally (sysctl_hw). */
char	machine[] = MACHINE;		/* from <machine/param.h> */
char	cpu_model[100];

/* The CPU configuration driver. */
int cpu_match(struct device *, void *, void *);
void cpu_attach(struct device *, struct device *, void *);

struct cfattach cpu_ca = {
	sizeof(struct device), cpu_match, cpu_attach
};

void cpu_init(struct cpu_info *ci);
void cpu_hatch(void);

#define	IU_IMPL(v)	((((u_int64_t)(v))&VER_IMPL) >> VER_IMPL_SHIFT)
#define	IU_VERS(v)	((((u_int64_t)(v))&VER_MASK) >> VER_MASK_SHIFT)

struct cpu_info *
alloc_cpuinfo(struct mainbus_attach_args *ma)
{
	paddr_t pa0, pa;
	vaddr_t va, va0;
	vsize_t sz = 8 * PAGE_SIZE;
	int portid;
	struct cpu_info *cpi, *ci;
	extern paddr_t cpu0paddr;

	portid = getpropint(ma->ma_node, "upa-portid", -1);
	if (portid == -1)
		portid = getpropint(ma->ma_node, "portid", -1);
	if (portid == -1)
		portid = getpropint(ma->ma_node, "cpuid", -1);
	if (portid == -1 && ma->ma_nreg > 0)
		portid = (ma->ma_reg[0].ur_paddr >> 32) & 0x0fffffff;
	if (portid == -1)
		panic("alloc_cpuinfo: portid");

	for (cpi = cpus; cpi != NULL; cpi = cpi->ci_next)
		if (cpi->ci_upaid == portid)
			return cpi;

	va = uvm_km_valloc_align(kernel_map, sz, 8 * PAGE_SIZE);
	if (va == 0)
		panic("alloc_cpuinfo: no virtual space");
	va0 = va;

	pa0 = cpu0paddr;
	cpu0paddr += sz;

	for (pa = pa0; pa < cpu0paddr; pa += PAGE_SIZE, va += PAGE_SIZE)
		pmap_kenter_pa(va, pa, VM_PROT_READ | VM_PROT_WRITE);

	pmap_update(pmap_kernel());

	cpi = (struct cpu_info *)(va0 + CPUINFO_VA - INTSTACK);

	memset((void *)va0, 0, sz);

	/*
	 * Initialize cpuinfo structure.
	 *
	 * Arrange pcb, idle stack and interrupt stack in the same
	 * way as is done for the boot CPU in pmap.c.
	 */
	cpi->ci_next = NULL;
	cpi->ci_curproc = NULL;
	cpi->ci_number = ncpus++;
	cpi->ci_upaid = portid;
	cpi->ci_fpproc = NULL;
#ifdef MULTIPROCESSOR
	cpi->ci_spinup = cpu_hatch;				/* XXX */
#else
	cpi->ci_spinup = NULL;
#endif

	cpi->ci_initstack = cpi;
	cpi->ci_paddr = pa0;
#ifdef SUN4V
	cpi->ci_mmfsa = pa0;
#endif
	cpi->ci_self = cpi;
	cpi->ci_node = ma->ma_node;

	sched_init_cpu(cpi);

	/*
	 * Finally, add itself to the list of active cpus.
	 */
	for (ci = cpus; ci->ci_next != NULL; ci = ci->ci_next)
		;
	ci->ci_next = cpi;
	return (cpi);
}

int
cpu_match(parent, vcf, aux)
	struct device *parent;
	void *vcf;
	void *aux;
{
	struct mainbus_attach_args *ma = aux;
#ifndef MULTIPROCESSOR
	int portid;
#endif
	char buf[32];

	if (OF_getprop(ma->ma_node, "device_type", buf, sizeof(buf)) <= 0 ||
	    strcmp(buf, "cpu") != 0)
		return (0);

#ifndef MULTIPROCESSOR
	/*
	 * On singleprocessor kernels, only match the CPU we're
	 * running on.
	 */
	portid = getpropint(ma->ma_node, "upa-portid", -1);
	if (portid == -1)
		portid = getpropint(ma->ma_node, "portid", -1);
	if (portid == -1)
		portid = getpropint(ma->ma_node, "cpuid", -1);
	if (portid == -1 && ma->ma_nreg > 0)
		portid = (ma->ma_reg[0].ur_paddr >> 32) & 0xff;
	if (portid == -1)
		return (0);

	if (portid != cpus->ci_upaid)
		return (0);
#endif

	return (1);
}

/*
 * Attach the CPU.
 * Discover interesting goop about the virtual address cache
 * (slightly funny place to do it, but this is where it is to be found).
 */
void
cpu_attach(parent, dev, aux)
	struct device *parent;
	struct device *dev;
	void *aux;
{
	int node;
	u_int clk;
	int impl, vers;
	struct mainbus_attach_args *ma = aux;
	struct cpu_info *ci;
	const char *sep;
	register int i, l;
	u_int64_t ver = 0;
	extern u_int64_t cpu_clockrate[];

	if (CPU_ISSUN4U || CPU_ISSUN4US)
		ver = getver();
	impl = IU_IMPL(ver);
	vers = IU_VERS(ver);

	/* tell them what we have */
	if (strncmp(parent->dv_xname, "core", 4) == 0)
		node = OF_parent(ma->ma_node);
	else
		node = ma->ma_node;

	/*
	 * Allocate cpu_info structure if needed.
	 */
	ci = alloc_cpuinfo(ma);

	clk = getpropint(node, "clock-frequency", 0);
	if (clk == 0) {
		/*
		 * Try to find it in the OpenPROM root...
		 */
		clk = getpropint(findroot(), "clock-frequency", 0);
	}
	if (clk) {
		cpu_clockrate[0] = clk; /* Tell OS what frequency we run on */
		cpu_clockrate[1] = clk/1000000;
	}
	snprintf(cpu_model, sizeof cpu_model, "%s (rev %d.%d) @ %s MHz",
	    ma->ma_name, vers >> 4, vers & 0xf, clockfreq(clk));
	printf(": %s\n", cpu_model);

	if (ci->ci_upaid == cpu_myid())
		cpu_init(ci);

	cacheinfo.c_physical = 1; /* Dunno... */
	cacheinfo.c_split = 1;
	l = getpropint(node, "icache-line-size", 0);
	if (l == 0)
		l = getpropint(node, "l1-icache-line-size", 0);
	cacheinfo.ic_linesize = l;
	for (i = 0; (1 << i) < l && l; i++)
		/* void */;
	if ((1 << i) != l && l)
		panic("bad icache line size %d", l);
	cacheinfo.ic_l2linesize = i;
	cacheinfo.ic_totalsize = getpropint(node, "icache-size", 0);
	if (cacheinfo.ic_totalsize == 0)
		cacheinfo.ic_totalsize = getpropint(node, "l1-icache-size", 0);
	if (cacheinfo.ic_totalsize == 0)
		cacheinfo.ic_totalsize = l *
		    getpropint(node, "icache-nlines", 64) *
		    getpropint(node, "icache-associativity", 1);

	l = getpropint(node, "dcache-line-size", 0);
	if (l == 0)
		l = getpropint(node, "l1-dcache-line-size", 0);
	cacheinfo.dc_linesize = l;
	for (i = 0; (1 << i) < l && l; i++)
		/* void */;
	if ((1 << i) != l && l)
		panic("bad dcache line size %d", l);
	cacheinfo.dc_l2linesize = i;
	cacheinfo.dc_totalsize = getpropint(node, "dcache-size", 0);
	if (cacheinfo.dc_totalsize == 0)
		cacheinfo.dc_totalsize = getpropint(node, "l1-dcache-size", 0);
	if (cacheinfo.dc_totalsize == 0)
		cacheinfo.dc_totalsize = l *
		    getpropint(node, "dcache-nlines", 128) *
		    getpropint(node, "dcache-associativity", 1);
	
	l = getpropint(node, "ecache-line-size", 0);
	if (l == 0)
		l = getpropint(node, "l2-cache-line-size", 0);
	cacheinfo.ec_linesize = l;
	for (i = 0; (1 << i) < l && l; i++)
		/* void */;
	if ((1 << i) != l && l)
		panic("bad ecache line size %d", l);
	cacheinfo.ec_l2linesize = i;
	cacheinfo.ec_totalsize = getpropint(node, "ecache-size", 0);
	if (cacheinfo.ec_totalsize == 0)
		cacheinfo.ec_totalsize = getpropint(node, "l2-cache-size", 0);
	if (cacheinfo.ec_totalsize == 0)
		cacheinfo.ec_totalsize = l *
		    getpropint(node, "ecache-nlines", 32768) *
		    getpropint(node, "ecache-associativity", 1);
	
	/*
	 * XXX - The following will have to do until
	 * we have per-cpu cache handling.
	 */
	cacheinfo.c_l2linesize =
		min(cacheinfo.ic_l2linesize,
		    cacheinfo.dc_l2linesize);
	cacheinfo.c_linesize =
		min(cacheinfo.ic_linesize,
		    cacheinfo.dc_linesize);
	cacheinfo.c_totalsize =
		cacheinfo.ic_totalsize +
		cacheinfo.dc_totalsize;

	if (cacheinfo.c_totalsize == 0)
		return;
	
	sep = " ";
	printf("%s: physical", dev->dv_xname);
	if (cacheinfo.ic_totalsize > 0) {
		printf("%s%ldK instruction (%ld b/l)", sep,
		       (long)cacheinfo.ic_totalsize/1024,
		       (long)cacheinfo.ic_linesize);
		sep = ", ";
	}
	if (cacheinfo.dc_totalsize > 0) {
		printf("%s%ldK data (%ld b/l)", sep,
		       (long)cacheinfo.dc_totalsize/1024,
		       (long)cacheinfo.dc_linesize);
		sep = ", ";
	}
	if (cacheinfo.ec_totalsize > 0) {
		printf("%s%ldK external (%ld b/l)", sep,
		       (long)cacheinfo.ec_totalsize/1024,
		       (long)cacheinfo.ec_linesize);
	}
	printf("\n");
	cache_enable();
}

int
cpu_myid(void)
{
	char buf[32];

#ifdef SUN4V
	if (CPU_ISSUN4V) {
		uint64_t myid;

		hv_cpu_myid(&myid);
		return myid;
	}
#endif

	if (OF_getprop(findroot(), "name", buf, sizeof(buf)) > 0 &&
	    strcmp(buf, "SUNW,Ultra-Enterprise-10000") == 0)
		return lduwa(0x1fff40000d0UL, ASI_PHYS_NON_CACHED);
	else
		return CPU_UPAID;
}

void
cpu_init(struct cpu_info *ci)
{
#ifdef SUN4V
	paddr_t pa = ci->ci_paddr;
	int err;

	if (CPU_ISSUN4U || CPU_ISSUN4US)
		return;

#define MONDO_QUEUE_SIZE	32
#define QUEUE_ENTRY_SIZE	64

	pa += CPUINFO_VA - INTSTACK;
	pa += PAGE_SIZE;

	ci->ci_cpumq = pa;
	err = hv_cpu_qconf(CPU_MONDO_QUEUE, ci->ci_cpumq, MONDO_QUEUE_SIZE);
	if (err != H_EOK)
		panic("Unable to set cpu mondo queue: %d", err);
	pa += MONDO_QUEUE_SIZE * QUEUE_ENTRY_SIZE;

	ci->ci_devmq = pa;
	err = hv_cpu_qconf(DEVICE_MONDO_QUEUE, ci->ci_devmq, MONDO_QUEUE_SIZE);
	if (err != H_EOK)
		panic("Unable to set device mondo queue: %d", err);
	pa += MONDO_QUEUE_SIZE * QUEUE_ENTRY_SIZE;

	ci->ci_mondo = pa;
	pa += 64;

	ci->ci_cpuset = pa;
	pa += 64;
#endif
}

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL
};

#ifdef MULTIPROCESSOR
void cpu_mp_startup(void);

void
cpu_boot_secondary_processors(void)
{
	struct cpu_info *ci;
	int cpuid, i;
	char buf[32];

	if (OF_getprop(findroot(), "name", buf, sizeof(buf)) > 0 &&
	    strcmp(buf, "SUNW,Ultra-Enterprise-10000") == 0) {
		for (ci = cpus; ci != NULL; ci = ci->ci_next)
			ci->ci_itid = STARFIRE_UPAID2HWMID(ci->ci_upaid);
	} else {
		for (ci = cpus; ci != NULL; ci = ci->ci_next)
			ci->ci_itid = ci->ci_upaid;
	}

	for (ci = cpus; ci != NULL; ci = ci->ci_next) {
		if (ci->ci_upaid == cpu_myid())
			continue;

		if (CPU_ISSUN4V)
			cpuid = ci->ci_upaid;
		else
			cpuid = getpropint(ci->ci_node, "cpuid", -1);

		if (cpuid == -1) {
			prom_start_cpu(ci->ci_node,
			    (void *)cpu_mp_startup, ci->ci_paddr);
		} else {
			prom_start_cpu_by_cpuid(cpuid,
			    (void *)cpu_mp_startup, ci->ci_paddr);
		}

		for (i = 0; i < 2000; i++) {
			sparc_membar(Sync);
			if (ci->ci_flags & CPUF_RUNNING)
				break;
			delay(10000);
		}
	}
}

void
cpu_hatch(void)
{
	struct cpu_info *ci = curcpu();
	int s;

	cpu_init(ci);

	ci->ci_flags |= CPUF_RUNNING;
	sparc_membar(Sync);

	s = splhigh();
	microuptime(&ci->ci_schedstate.spc_runtime);
	splx(s);

	tick_start();

	SCHED_LOCK(s);
	cpu_switchto(NULL, sched_chooseproc());
}
#endif

void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;
	if (ci->ci_curproc != NULL)
		aston(ci->ci_curproc);
}
