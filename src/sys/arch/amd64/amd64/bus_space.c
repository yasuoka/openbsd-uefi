/*	$OpenBSD: bus_space.c,v 1.8 2007/11/16 16:16:06 deraadt Exp $	*/
/*	$NetBSD: bus_space.c,v 1.2 2003/03/14 18:47:53 christos Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 1998 The NetBSD Foundation, Inc.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/extent.h>

#include <uvm/uvm_extern.h>

#define _X86_BUS_DMA_PRIVATE
#include <machine/bus.h>

#include <dev/isa/isareg.h>
#include <machine/isa_machdep.h>

/*
 * Extent maps to manage I/O and memory space.  Allocate
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
static	long ioport_ex_storage[EXTENT_FIXED_STORAGE_SIZE(8) / sizeof(long)];
static	long iomem_ex_storage[EXTENT_FIXED_STORAGE_SIZE(8) / sizeof(long)];
struct	extent *ioport_ex;
struct	extent *iomem_ex;
static	int ioport_malloc_safe;

int	x86_mem_add_mapping(bus_addr_t, bus_size_t,
	    int, bus_space_handle_t *);

void
x86_bus_space_init(void)
{
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
}

void
x86_bus_space_mallocok(void)
{

	ioport_malloc_safe = 1;
}

int
bus_space_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	int error;
	struct extent *ex;

	/*
	 * Pick the appropriate extent map.
	 */
	if (t == X86_BUS_SPACE_IO) {
		ex = ioport_ex;
		if (flags & BUS_SPACE_MAP_LINEAR)
			return (EINVAL);
	} else if (t == X86_BUS_SPACE_MEM)
		ex = iomem_ex;
	else
		panic("bus_space_map: bad bus space tag");

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
	if (t == X86_BUS_SPACE_IO) {
		*bshp = bpa;
		return (0);
	}

	if (bpa >= IOM_BEGIN && (bpa + size) <= IOM_END) {
		*bshp = (bus_space_handle_t)ISA_HOLE_VADDR(bpa);
		return(0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	error = x86_mem_add_mapping(bpa, size, 0, bshp);
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
_bus_space_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{

	/*
	 * For I/O space, just fill in the handle.
	 */
	if (t == X86_BUS_SPACE_IO) {
		*bshp = bpa;
		return (0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	return (x86_mem_add_mapping(bpa, size, 0, bshp));
}

int
bus_space_alloc(bus_space_tag_t t, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t alignment, bus_size_t boundary, int flags,
    bus_addr_t *bpap, bus_space_handle_t *bshp)
{
	struct extent *ex;
	u_long bpa;
	int error;

	/*
	 * Pick the appropriate extent map.
	 */
	if (t == X86_BUS_SPACE_IO) {
		ex = ioport_ex;
	} else if (t == X86_BUS_SPACE_MEM)
		ex = iomem_ex;
	else
		panic("bus_space_alloc: bad bus space tag");

	/*
	 * Sanity check the allocation against the extent's boundaries.
	 */
	if (rstart < ex->ex_start || rend > ex->ex_end)
		panic("bus_space_alloc: bad region start/end");

	/*
	 * Do the requested allocation.
	 */
	error = extent_alloc_subregion(ex, rstart, rend, size, alignment,
	    0, boundary,
	    EX_FAST | EX_NOWAIT | (ioport_malloc_safe ?  EX_MALLOCOK : 0),
	    &bpa);

	if (error)
		return (error);

	/*
	 * For I/O space, that's all she wrote.
	 */
	if (t == X86_BUS_SPACE_IO) {
		*bshp = *bpap = bpa;
		return (0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	error = x86_mem_add_mapping(bpa, size, 0, bshp);
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
x86_mem_add_mapping(bus_addr_t bpa, bus_size_t size, int cacheable,
    bus_space_handle_t *bshp)
{
	if (cacheable)
		*bshp = PMAP_DIRECT_MAP(bpa);
	else
		*bshp = PMAP_DIRECT_NC_MAP(bpa);
	return 0;
}

/*
 * void _bus_space_unmap(bus_space_tag bst, bus_space_handle bsh,
 *                        bus_size_t size, bus_addr_t *adrp)
 *
 *   This function unmaps memory- or io-space mapped by the function
 *   _bus_space_map().  This function works nearly as same as
 *   bus_space_unmap(), but this function does not ask kernel
 *   built-in extents and returns physical address of the bus space,
 *   for the convenience of the extra extent manager.
 */
void
_bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size,
    bus_addr_t *adrp)
{
	bus_addr_t bpa;

	/*
	 * Find the correct extent and bus physical address.
	 */
	if (t == X86_BUS_SPACE_IO) {
		bpa = bsh;
	} else if (t == X86_BUS_SPACE_MEM) {
		if (bsh >= atdevbase && (bsh + size) <= (atdevbase + IOM_SIZE)) {
			bpa = (bus_addr_t)ISA_PHYSADDR(bsh);
		} else {
			if (bsh >= PMAP_DIRECT_BASE_NC &&
			    bsh < PMAP_DIRECT_END_NC)
				bpa = PMAP_DIRECT_NC_UNMAP(bsh);
			else
				bpa = PMAP_DIRECT_UNMAP(bsh);
		}
	} else {
		panic("_bus_space_unmap: bad bus space tag");
	}

	if (adrp != NULL) {
		*adrp = bpa;
	}
}

void
bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{
	struct extent *ex;
	bus_addr_t bpa;

	/*
	 * Find the correct extent and bus physical address.
	 */
	if (t == X86_BUS_SPACE_IO) {
		ex = ioport_ex;
		bpa = bsh;
	} else if (t == X86_BUS_SPACE_MEM) {
		ex = iomem_ex;

		if (bsh >= atdevbase &&
		    (bsh + size) <= (atdevbase + IOM_SIZE)) {
			bpa = (bus_addr_t)ISA_PHYSADDR(bsh);
			goto ok;
		}

		if (bsh >= PMAP_DIRECT_BASE_NC &&
		    bsh < PMAP_DIRECT_END_NC)
			bpa = PMAP_DIRECT_NC_UNMAP(bsh);
		else
			bpa = PMAP_DIRECT_UNMAP(bsh);
	} else
		panic("bus_space_unmap: bad bus space tag");

ok:
	if (extent_free(ex, bpa, size,
	    EX_NOWAIT | (ioport_malloc_safe ? EX_MALLOCOK : 0))) {
		printf("bus_space_unmap: %s 0x%lx, size 0x%lx\n",
		    (t == X86_BUS_SPACE_IO) ? "port" : "pa", bpa, size);
		printf("bus_space_unmap: can't free region\n");
	}
}

void
bus_space_free(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{

	/* bus_space_unmap() does all that we need to do. */
	bus_space_unmap(t, bsh, size);
}

int
bus_space_subregion(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp)
{

	*nbshp = bsh + offset;
	return (0);
}

paddr_t
bus_space_mmap(bus_space_tag_t t, bus_addr_t addr, off_t off, int prot, int flags)
{

	/* Can't mmap I/O space. */
	if (t == X86_BUS_SPACE_IO)
		return (-1);

	/*
	 * "addr" is the base address of the device we're mapping.
	 * "off" is the offset into that device.
	 *
	 * Note we are called for each "page" in the device that
	 * the upper layers want to map.
	 */
	return (atop(addr + off));
}
