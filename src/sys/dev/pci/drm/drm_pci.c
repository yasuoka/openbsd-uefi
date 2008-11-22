/*-
 * Copyright 2003 Eric Anholt.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"

/*
 * Allocate a physically contiguous DMA-accessible consistent 
 * memory block.
 */
drm_dma_handle_t *
drm_pci_alloc(bus_dma_tag_t dmat, size_t size, size_t align,
    dma_addr_t maxaddr)
{
	drm_dma_handle_t *dmah;
	int ret, nsegs;

	/* Need power-of-two alignment, so fail the allocation if it isn't. */
	if ((align & (align - 1)) != 0) {
		DRM_ERROR("drm_pci_alloc with non-power-of-two alignment %d\n",
		    (int)align);
		return NULL;
	}

	dmah = drm_alloc(sizeof(*dmah), DRM_MEM_DMA);
	if (dmah == NULL)
		return NULL;

	if (bus_dmamap_create(dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &dmah->dmamap) != 0)
		goto dmahfree;
		
	if (bus_dmamem_alloc(dmat, size, align, 0,
	    &dmah->seg, 1, &nsegs, BUS_DMA_NOWAIT) != 0) {
		DRM_ERROR("bus_dmamem_alloc(%zd, %zd) returned %d\n",
		    size, align, ret);
		goto destroy;
	}
	if (nsegs != 1) {
		DRM_ERROR("bus_dmamem_alloc(%zd) returned %d segments\n",
		    size, nsegs);
		goto free;
	}

	if (bus_dmamem_map(dmat, &dmah->seg, 1, size,
	    (caddr_t*)&dmah->addr, BUS_DMA_NOWAIT) != 0) {
		DRM_ERROR("bus_dmamem_map() failed %d\n", ret);
		goto free;
	}

	if (bus_dmamap_load(dmat, dmah->dmamap, dmah->addr, size,
	    NULL, BUS_DMA_NOWAIT) != 0)
		goto unmap;

	dmah->busaddr = dmah->dmamap->dm_segs[0].ds_addr;
	dmah->vaddr = dmah->addr;
	dmah->size = size;

	return dmah;

unmap:
	bus_dmamem_unmap(dmat, dmah->addr, size);
free:
	bus_dmamem_free(dmat, &dmah->seg, 1);
destroy:
	bus_dmamap_destroy(dmat, dmah->dmamap);
dmahfree:
	drm_free(dmah, sizeof(*dmah), DRM_MEM_DMA);

	return (NULL);

}

/*
 * Free a DMA-accessible consistent memory block.
 */
void
drm_pci_free(bus_dma_tag_t dmat, drm_dma_handle_t *dmah)
{
	if (dmah == NULL)
		return;

	bus_dmamap_unload(dmat, dmah->dmamap);
	bus_dmamem_unmap(dmat, dmah->vaddr, dmah->size);
	bus_dmamem_free(dmat, &dmah->seg, 1);
	bus_dmamap_destroy(dmat, dmah->dmamap);

	drm_free(dmah, sizeof(*dmah), DRM_MEM_DMA);
}
