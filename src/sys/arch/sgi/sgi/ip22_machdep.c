/*	$OpenBSD: ip22_machdep.c,v 1.7 2012/05/12 16:48:27 miod Exp $	*/

/*
 * Copyright (c) 2012 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/buf.h>
#include <sys/mount.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/memconf.h>

#include <mips64/arcbios.h>
#include <mips64/archtype.h>

#include <uvm/uvm.h>

#include <sgi/sgi/ip22.h>
#include <sgi/localbus/imcreg.h>
#include <sgi/localbus/imcvar.h>
#include <sgi/hpc/hpcreg.h>
#include <sgi/hpc/iocreg.h>

#include "gio.h"

#if NGIO > 0
#include <sgi/gio/gioreg.h>
#include <sgi/gio/giovar.h>
#include <sgi/gio/lightreg.h>
#endif

extern char *hw_prod;

int	hpc_old = 0;
int	bios_year;

void	ip22_arcbios_walk(void);
int	ip22_arcbios_walk_component(arc_config_t *);
void	ip22_memory_setup(void);
void	ip22_video_setup(void);

/*
 * Walk the ARCBios component tree to get hardware information we can't
 * obtain by other means.
 */

#define	IP22_HAS_L2		0x01
#define	IP22_HAS_AUDIO		0x02
#define	IP22_HAS_ENOUGH_FB	0x04
static int ip22_arcwalk_results = 0;

#if NGIO > 0
static char ip22_fb_names[GIO_MAX_FB][64];
#endif

int
ip22_arcbios_walk_component(arc_config_t *cf)
{
	struct cpu_info *ci = curcpu();
	arc_config_t *child;
	arc_config64_t *cf64 = (arc_config64_t *)cf;
#if NGIO > 0
	static int fbidx = 0;
#endif

	/*
	 * Split secondary caches are not supported.
	 * No IP22 processor module uses them anyway.
	 */
	if (cf->class == arc_CacheClass && cf->type == arc_SecondaryCache) {
		uint64_t key;

		if (bios_is_32bit)
			key = cf->key;
		else
			key = cf64->key;

		/*
		 * Secondary cache information is encoded as WWLLSSSS, where
		 * WW is the number of ways (should be 01)
		 * LL is Log2(line size) (should be 04 or 05)
		 * SS is Log2(cache size in 4KB units) (should be 0007)
		 */
		ci->ci_l2size = (1 << 12) << (key & 0x0000ffff);
		ci->ci_l2line = 1 << ((key >> 16) & 0xff);

		ip22_arcwalk_results |= IP22_HAS_L2;
	}

	if (cf->class == arc_ControllerClass &&
	    cf->type == arc_AudioController) {
		ip22_arcwalk_results |= IP22_HAS_AUDIO;
	}

#if NGIO > 0
	if (cf->class == arc_ControllerClass &&
	    cf->type == arc_DisplayController) {
		if (fbidx >= GIO_MAX_FB) {
			/*
			 * Not worth printing a message. If the system is
			 * configured for glass console, it will get
			 * overwritten anyway.
			 */
		} else {
			const char *id;
			size_t idlen;

			if (bios_is_32bit) {
				idlen = cf->id_len;
				id = (const char *)(vaddr_t)cf->id;
			} else {
				idlen = cf64->id_len;
				id = (const char *)cf64->id;
			}
			if (idlen != 0) {
				/* skip leading spaces */
				while (idlen > 0 && id[0] == ' ') {
					id++;
					idlen--;
				}
				/* skip SGI- prefix */
				if (idlen >= 4 && strncmp(id, "SGI-", 4) == 0) {
					id += 4;
					idlen -= 4;
				}
				if (idlen >= sizeof(ip22_fb_names[0]))
					idlen = sizeof(ip22_fb_names[0]) - 1;
				bcopy(id, ip22_fb_names[fbidx], idlen);
			}
			giofb_names[fbidx] = ip22_fb_names[fbidx];
			fbidx++;
		}
	}
#endif

	if (ip22_arcwalk_results ==
	    (IP22_HAS_L2 | IP22_HAS_AUDIO | IP22_HAS_ENOUGH_FB))
		return 0;	/* abort walk */

	for (child = (arc_config_t *)Bios_GetChild(cf); child != NULL;
	    child = (arc_config_t *)Bios_GetPeer(child)) {
		if (ip22_arcbios_walk_component(child) == 0)
			return 0;
	}

	return 1;	/* continue walk */
}

void
ip22_arcbios_walk()
{
#if NGIO == 0
	ip22_arcwalk_results |= IP22_HAS_ENOUGH_FB;
#endif
	(void)ip22_arcbios_walk_component((arc_config_t *)Bios_GetChild(NULL));
}

#define	IMC_NREGION	3

void
ip22_memory_setup()
{
	uint i, bank, shift;
	uint32_t memc0, memc1;
	uint32_t memc;
	paddr_t base[IMC_NREGION], size[IMC_NREGION], limit;
	paddr_t start0, end0, start1, end1;
	struct phys_mem_desc *mem;

	/*
	 * Figure out the top of memory, as reported by ARCBios.
	 */

	limit = 0;
	for (i = 0, mem = mem_layout; i < MAXMEMSEGS; i++, mem++) {
		if (mem->mem_last_page > limit)
			limit = mem->mem_last_page;
	}
	limit = ptoa(limit);

	/*
	 * Figure out where the memory controller has put memory.
	 */

	memc0 = imc_read(IMC_MEMCFG0);
	memc1 = imc_read(IMC_MEMCFG1);

	shift = IMC_MEMC_LSHIFT;
	/* Revision D onwards uses larger units, to allow for more memory */
	if ((imc_read(IMC_SYSID) & IMC_SYSID_REVMASK) >= 5)
		shift = IMC_MEMC_LSHIFT_HUGE;

	for (bank = 0; bank < IMC_NREGION; bank++) {
		memc = (bank & 2) ? memc1 : memc0;
		if ((bank & 1) == 0)
			memc >>= IMC_MEMC_BANK_SHIFT;
		memc &= IMC_MEMC_BANK_MASK;

		if ((memc & IMC_MEMC_VALID) == 0) {
			base[bank] = size[bank] = 0;
			continue;
		}

		base[bank] = (memc & IMC_MEMC_ADDR_MASK) >> IMC_MEMC_ADDR_SHIFT;
		base[bank] <<= shift;

		size[bank] = (memc & IMC_MEMC_SIZE_MASK) >> IMC_MEMC_SIZE_SHIFT;
		size[bank]++;
		size[bank] <<= shift;
	}

	/*
	 * Perform sanity checks on the above data..
	 */

	/* memory should not start below 128MB */
	for (bank = 0; bank < IMC_NREGION; bank++)
		if (size[bank] != 0 && base[bank] < (1ULL << 27))
			goto dopanic;

	/* banks should not overlap */
	for (bank = 1; bank < IMC_NREGION; bank++) {
		if (size[bank] == 0)
			continue;
		start0 = base[bank];
		end0 = base[bank] + size[bank];
		for (i = 0; i < bank; i++) {
			if (size[i] == 0)
				continue;
			start1 = base[i];
			end1 = base[i] + size[i];
			if (end0 > start1 && start0 < end1)
				goto dopanic;
		}
	}

	/*
	 * Now register all the memory beyond what ARCBios stopped at.
	 */

	for (bank = 0; bank < IMC_NREGION; bank++) {
		if (size[bank] == 0)
			continue;

		start0 = base[bank];
		end0 = base[bank] + size[bank];
		if (end0 <= limit)
			continue;

		if (start0 < limit)
			start0 = limit;

		memrange_register(atop(start0), atop(end0), 0);
	}

	return;

dopanic:
	bios_printf("** UNEXPECTED MEMORY CONFIGURATION **\n");
	bios_printf("MEMC0 %08x MEMC1 %08x\n", memc0, memc1);
	bios_printf("Please contact <sgi@openbsd.org>\n"
	    "Halting system.\n");
	Bios_Halt();
	for (;;) ;
}

void
ip22_video_setup()
{
#if NGIO > 0
	/*
	 * According to Linux, the base address of the console device,
	 * if there is a glass console, can be obtained by invoking the
	 * 8th function pointer of the vendor-specific vector table.
	 *
	 * This function returns a pointer to a list of addresses (or
	 * whatever struct it is), which second field is the address we
	 * are looking for.
	 *
	 * However, the address does not point to the base address of the
	 * slot the frame buffer, but to some registers in it. While this
	 * might help identifying the actual frame buffer type, at the
	 * moment we are only interested in the base address.
	 */

	long (*get_gfxinfo)(void);
	vaddr_t fbaddr;
	paddr_t fbphys;

	if (bios_is_32bit) {
		int32_t *vec, *addr;

		vec = (int32_t *)(int64_t)(int32_t)ArcBiosBase32->vendor_vect;
		get_gfxinfo = (long (*)(void))(int64_t)vec[8];
		addr = (int32_t *)(int64_t)(*get_gfxinfo)();
		fbaddr = addr[1];
	} else {
		int64_t *vec, *addr;

		vec = (int64_t *)ArcBiosBase64->vendor_vect;
		get_gfxinfo = (long (*)(void))vec[8];
		addr = (int64_t *)(*get_gfxinfo)();
		fbaddr = addr[1];
	}

	if (fbaddr >= CKSEG1_BASE && fbaddr < CKSSEG_BASE)
		fbphys = CKSEG1_TO_PHYS(fbaddr);
	else if (IS_XKPHYS(fbaddr))
		fbphys = XKPHYS_TO_PHYS(fbaddr);
	else
		return;

	if (fbphys < GIO_ADDR_GFX || fbphys >= GIO_ADDR_END)
		return;

	/*
	 * Try to convert the address to a slot base or, for light(4)
	 * frame buffers, a frame buffer base.
	 *
	 * Verified addresses:
	 * grtwo	slot + 0x00000000
	 * impact	?
	 * light	?
	 * newport	slot + 0x000f0000 (NEWPORT_REX3_OFFSET)
	 */

	/* light(4) only exists on IP20 */
	if (sys_config.system_type == SGI_IP20) {
		paddr_t tmp = fbphys & ~((paddr_t)LIGHT_SIZE - 1);
		if (tmp == LIGHT_ADDR_0 || tmp == LIGHT_ADDR_1) {
			giofb_consaddr = tmp;
			return;
		}
	}

	if (fbphys < GIO_ADDR_EXP0)
		giofb_consaddr = GIO_ADDR_GFX;
	else if (fbphys < GIO_ADDR_EXP1)
		giofb_consaddr = GIO_ADDR_EXP0;
	else
		giofb_consaddr = GIO_ADDR_EXP1;
#endif
}

void
ip22_setup()
{
	u_long cpuspeed;
	volatile uint32_t *sysid;

	/*
	 * Get CPU information.
	 */
	bootcpu_hwinfo.c0prid = cp0_get_prid();
	bootcpu_hwinfo.c1prid = cp1_get_prid();
	cpuspeed = bios_getenvint("cpufreq");
	if (sys_config.system_type == SGI_IP20)
		cpuspeed <<= 1;
	if (cpuspeed < 100)
		cpuspeed = 100;		/* reasonable default */
	bootcpu_hwinfo.clock = cpuspeed * 1000000;
	bootcpu_hwinfo.type = (bootcpu_hwinfo.c0prid >> 8) & 0xff;

	/*
	 * Scan ARCBios component list for useful information (L2 cache
	 * configuration, audio device availability)
	 */
	ip22_arcbios_walk();

	/*
	 * Figure out what critter we are running on.
	 */
	switch (sys_config.system_type) {
	case SGI_IP20:
		if (ip22_arcwalk_results & IP22_HAS_AUDIO)
			hw_prod = "Indigo";
		else
			hw_prod = "VME Indigo";
		break;
	case SGI_IP22:
		sysid = (volatile uint32_t *)
		    PHYS_TO_XKPHYS(HPC_BASE_ADDRESS_0 + IOC_BASE + IOC_SYSID,
		      CCA_NC);
		if (*sysid & 0x01) {
			sys_config.system_subtype = IP22_INDIGO2;
			hw_prod = "Indigo2";
		} else {
			if (ip22_arcwalk_results & IP22_HAS_AUDIO) {
				sys_config.system_subtype = IP22_INDY;
				hw_prod = "Indy";
			} else {
				sys_config.system_subtype = IP22_CHALLS;
				hw_prod = "Challenge S";
			}
		}
		break;
	case SGI_IP26:
		sys_config.system_subtype = IP22_INDIGO2;
		hw_prod = "POWER Indigo2 R8000";
		break;
	case SGI_IP28:
		sys_config.system_subtype = IP22_INDIGO2;
		hw_prod = "POWER Indigo2 R10000";
		break;
	}

	/*
	 * Figure out how many TLB entries are available.
	 */
	switch (bootcpu_hwinfo.type) {
#ifdef CPU_R10000
	case MIPS_R10000:
		bootcpu_hwinfo.tlbsize = 64;
		break;
#endif
	default:	/* R4x00, R5000 */
		bootcpu_hwinfo.tlbsize = 48;
		break;
	}

	/*
	 * Compute memory layout. ARCBios may not report all memory (on
	 * Indigo, it seems to only report up to 128MB, and on Indigo2,
	 * up to 256MB).
	 */
	ip22_memory_setup();

	/*
	 * Get glass console information, if necessary.
	 */
	ip22_video_setup();

	/*
	 * Register DMA-reachable memory constraints.
	 * hpc(4) revision 1 and 1.5 only use 28-bit address pointers, thus
	 * only 256MB are addressable; unfortunately, since physical memory
	 * starts at 128MB, this enforces a 128MB limit.
	 *
	 * The following logic is pessimistic, as IP24 (Indy) systems have
	 * a revision 3 hpc(4) onboard, but will accept older revisions in
	 * expansion boards.
	 */
	switch (sys_config.system_type) {
	default:
		dma_constraint.ucr_low = 0;
		dma_constraint.ucr_high = (1UL << 32) - 1;
		if (sys_config.system_subtype == IP22_INDIGO2)
			break;
		/* FALLTHROUGH */
	case SGI_IP20:
		dma_constraint.ucr_low = 0;
		dma_constraint.ucr_high = (1UL << 28) - 1;
		break;
	}

	/*
	 * Get ARCBios' current time.
	 */
	bios_year = Bios_GetTime()->Year;

	_device_register = arcs_device_register;
}

void
ip22_post_autoconf()
{
	/*
	 * Relax DMA-reachable memory constraints if no 28-bit hpc(4)
	 * device has attached.
	 */
	if (hpc_old == 0) {
		uint64_t dmapages_before, dmapages;

		dmapages_before = uvm_pagecount(&dma_constraint);
		dma_constraint.ucr_high = (1UL << 32) - 1;
		dmapages = uvm_pagecount(&dma_constraint);
		if (dmapages_before != dmapages) {
			bufadjust(bufcachepercent * dmapages / 100);
			bufhighpages = bufpages;
		}
	}
}
