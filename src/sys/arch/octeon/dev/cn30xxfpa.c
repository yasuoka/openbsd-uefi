/*	$OpenBSD: cn30xxfpa.c,v 1.5 2015/07/19 00:12:54 jasper Exp $	*/

/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
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
 */

#undef	FPADEBUG

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/vmparam.h>
#include <machine/octeonvar.h>

#include <octeon/dev/cn30xxfpavar.h>
#include <octeon/dev/cn30xxfpareg.h>

#ifdef FPADEBUG
#define	DPRINTF(x)	printf x
#else
#define	DPRINTF(x)
#endif

#define	_DMA_NSEGS	1
#define	_DMA_BUFLEN	0x01000000

/* pool descriptor */
struct cn30xxfpa_desc {
};

struct cn30xxfpa_softc {
	int			sc_initialized;

	bus_space_tag_t		sc_regt;
	bus_space_handle_t	sc_regh;

	bus_space_tag_t		sc_opst;
	bus_space_handle_t	sc_opsh;

	bus_dma_tag_t		sc_dmat;

	struct cn30xxfpa_desc	sc_descs[8];
};

void		cn30xxfpa_bootstrap(struct octeon_config *);
void		cn30xxfpa_reset(void);
void		cn30xxfpa_int_enable(struct cn30xxfpa_softc *, int);
void		cn30xxfpa_buf_dma_alloc(struct cn30xxfpa_buf *);

void		cn30xxfpa_init(struct cn30xxfpa_softc *);
#ifdef notyet
uint64_t	cn30xxfpa_iobdma(struct cn30xxfpa_softc *, int, int);
#endif

#ifdef OCTEON_ETH_DEBUG
void		cn30xxfpa_intr_rml(void *);
#endif

static struct cn30xxfpa_softc	cn30xxfpa_softc;

/* ---- global functions */

void
cn30xxfpa_bootstrap(struct octeon_config *mcp)
{
	struct cn30xxfpa_softc *sc = &cn30xxfpa_softc;

	sc->sc_regt = mcp->mc_iobus_bust;
	sc->sc_opst = mcp->mc_iobus_bust;
	sc->sc_dmat = mcp->mc_iobus_dmat;

	cn30xxfpa_init(sc);
}

void
cn30xxfpa_reset(void)
{
	/* XXX */
}

#ifdef OCTEON_ETH_DEBUG
int	cn30xxfpa_intr_rml_verbose;

void
cn30xxfpa_intr_rml(void *arg)
{
	struct cn30xxfpa_softc *sc;
	uint64_t reg;

	sc = &cn30xxfpa_softc;
	KASSERT(sc != NULL);
	reg = cn30xxfpa_int_summary();
	if (cn30xxfpa_intr_rml_verbose)
		printf("%s: FPA_INT_SUM=0x%016llx\n", __func__, reg);
}

void
cn30xxfpa_int_enable(struct cn30xxfpa_softc *sc, int enable)
{
	const uint64_t int_xxx =
	    FPA_INT_ENB_Q7_PERR | FPA_INT_ENB_Q7_COFF | FPA_INT_ENB_Q7_UND |
	    FPA_INT_ENB_Q6_PERR | FPA_INT_ENB_Q6_COFF | FPA_INT_ENB_Q6_UND |
	    FPA_INT_ENB_Q5_PERR | FPA_INT_ENB_Q5_COFF | FPA_INT_ENB_Q5_UND |
	    FPA_INT_ENB_Q4_PERR | FPA_INT_ENB_Q4_COFF | FPA_INT_ENB_Q4_UND |
	    FPA_INT_ENB_Q3_PERR | FPA_INT_ENB_Q3_COFF | FPA_INT_ENB_Q3_UND |
	    FPA_INT_ENB_Q2_PERR | FPA_INT_ENB_Q2_COFF | FPA_INT_ENB_Q2_UND |
	    FPA_INT_ENB_Q1_PERR | FPA_INT_ENB_Q1_COFF | FPA_INT_ENB_Q1_UND |
	    FPA_INT_ENB_Q0_PERR | FPA_INT_ENB_Q0_COFF | FPA_INT_ENB_Q0_UND |
	    FPA_INT_ENB_FED1_DBE | FPA_INT_ENB_FED1_SBE |
	    FPA_INT_ENB_FED0_DBE | FPA_INT_ENB_FED0_SBE;

	bus_space_write_8(sc->sc_regt, sc->sc_regh, FPA_INT_SUM_OFFSET,
	    int_xxx);
	if (enable)
		bus_space_write_8(sc->sc_regt, sc->sc_regh, FPA_INT_ENB_OFFSET,
		    int_xxx);
}

uint64_t
cn30xxfpa_int_summary(void)
{
	struct cn30xxfpa_softc *sc = &cn30xxfpa_softc;
	uint64_t summary;

	summary = bus_space_read_8(sc->sc_regt, sc->sc_regh, FPA_INT_SUM_OFFSET);
	bus_space_write_8(sc->sc_regt, sc->sc_regh, FPA_INT_SUM_OFFSET, summary);
	return summary;
}
#endif

int
cn30xxfpa_buf_init(int poolno, size_t size, size_t nelems,
    struct cn30xxfpa_buf **rfb)
{
	struct cn30xxfpa_softc *sc = &cn30xxfpa_softc;
	struct cn30xxfpa_buf *fb;
	int nsegs;
	paddr_t paddr;

	nsegs = 1/* XXX */;
	fb = malloc(sizeof(*fb) + sizeof(*fb->fb_dma_segs) * nsegs, M_DEVBUF,
	    M_WAITOK | M_ZERO);
	if (fb == NULL)
		return 1;
	fb->fb_poolno = poolno;
	fb->fb_size = size;
	fb->fb_nelems = nelems;
	fb->fb_len = size * nelems;
	fb->fb_dmat = sc->sc_dmat;
	fb->fb_dma_segs = (void *)(fb + 1);
	fb->fb_dma_nsegs = nsegs;

	cn30xxfpa_buf_dma_alloc(fb);

	for (paddr = fb->fb_paddr; paddr < fb->fb_paddr + fb->fb_len;
	    paddr += fb->fb_size)
		cn30xxfpa_buf_put_paddr(fb, paddr);

	*rfb = fb;

	return 0;
}

void *
cn30xxfpa_buf_get(struct cn30xxfpa_buf *fb)
{
	paddr_t paddr;
	vaddr_t addr;

	paddr = cn30xxfpa_buf_get_paddr(fb);
	if (paddr == 0)
		addr = 0;
	else
		addr = fb->fb_addr + (vaddr_t/* XXX */)(paddr - fb->fb_paddr);
	return (void *)addr;
}

void
cn30xxfpa_buf_dma_alloc(struct cn30xxfpa_buf *fb)
{
	int status;
	int nsegs;
	caddr_t va;

	status = bus_dmamap_create(fb->fb_dmat, fb->fb_len,
	    fb->fb_len / PAGE_SIZE,	/* # of segments */
	    fb->fb_len,			/* we don't use s/g for FPA buf */
	    PAGE_SIZE,			/* OCTEON hates >PAGE_SIZE boundary */
	    0, &fb->fb_dmah);
	if (status != 0)
		panic("%s failed", "bus_dmamap_create");

	status = bus_dmamem_alloc(fb->fb_dmat, fb->fb_len, 128, 0,
	    fb->fb_dma_segs, fb->fb_dma_nsegs, &nsegs, 0);
	if (status != 0 || fb->fb_dma_nsegs != nsegs)
		panic("%s failed", "bus_dmamem_alloc");

	status = bus_dmamem_map(fb->fb_dmat, fb->fb_dma_segs, fb->fb_dma_nsegs,
	    fb->fb_len, &va, 0);
	if (status != 0)
		panic("%s failed", "bus_dmamem_map");

	status = bus_dmamap_load(fb->fb_dmat, fb->fb_dmah, va, fb->fb_len,
	    NULL,		/* kernel */
	    0);
	if (status != 0)
		panic("%s failed", "bus_dmamap_load");

	fb->fb_addr = (vaddr_t)va;
	fb->fb_paddr = fb->fb_dma_segs[0].ds_addr;
}

uint64_t
cn30xxfpa_query(int poolno)
{
	struct cn30xxfpa_softc *sc = &cn30xxfpa_softc;

	return bus_space_read_8(sc->sc_regt, sc->sc_regh,
	    FPA_QUE0_AVAILABLE_OFFSET + sizeof(uint64_t) * poolno);
}

/* ---- local functions */

void	cn30xxfpa_init_bus(struct cn30xxfpa_softc *);
void	cn30xxfpa_init_bus_space(struct cn30xxfpa_softc *);
void	cn30xxfpa_init_regs(struct cn30xxfpa_softc *);

void
cn30xxfpa_init(struct cn30xxfpa_softc *sc)
{
	if (sc->sc_initialized != 0)
		panic("%s: already initialized", __func__);
	sc->sc_initialized = 1;

	cn30xxfpa_init_bus(sc);
	cn30xxfpa_init_regs(sc);
#ifdef OCTEON_ETH_DEBUG
	cn30xxfpa_int_enable(sc, 1);
#endif
}

void
cn30xxfpa_init_bus(struct cn30xxfpa_softc *sc)
{
	cn30xxfpa_init_bus_space(sc);
}

void
cn30xxfpa_init_bus_space(struct cn30xxfpa_softc *sc)
{
	int status;

	status = bus_space_map(sc->sc_regt, FPA_BASE, FPA_SIZE, 0, &sc->sc_regh);
	if (status != 0)
		panic("can't map %s space", "register");

	/* CN30XX-HM-1.0 Table 4-25 */
	status = bus_space_map(sc->sc_opst,
	    0x0001180028000000ULL/* XXX */, 0x0200/* XXX */, 0, &sc->sc_opsh);
	if (status != 0)
		panic("can't map %s space", "operations");
}

void
cn30xxfpa_init_regs(struct cn30xxfpa_softc *sc)
{

	bus_space_write_8(sc->sc_regt, sc->sc_regh, FPA_CTL_STATUS_OFFSET,
	    FPA_CTL_STATUS_ENB);

/* XXX */
#ifdef OCTEON_ETH_DEBUG
	bus_space_write_8(sc->sc_regt, sc->sc_regh, FPA_INT_ENB_OFFSET,
	    FPA_INT_ENB_Q7_PERR | FPA_INT_ENB_Q7_COFF | FPA_INT_ENB_Q7_UND |
	    FPA_INT_ENB_Q6_PERR | FPA_INT_ENB_Q6_COFF | FPA_INT_ENB_Q6_UND |
	    FPA_INT_ENB_Q5_PERR | FPA_INT_ENB_Q5_COFF | FPA_INT_ENB_Q5_UND |
	    FPA_INT_ENB_Q4_PERR | FPA_INT_ENB_Q4_COFF | FPA_INT_ENB_Q4_UND |
	    FPA_INT_ENB_Q3_PERR | FPA_INT_ENB_Q3_COFF | FPA_INT_ENB_Q3_UND |
	    FPA_INT_ENB_Q2_PERR | FPA_INT_ENB_Q2_COFF | FPA_INT_ENB_Q2_UND |
	    FPA_INT_ENB_Q1_PERR | FPA_INT_ENB_Q1_COFF | FPA_INT_ENB_Q1_UND |
	    FPA_INT_ENB_Q0_PERR | FPA_INT_ENB_Q0_COFF | FPA_INT_ENB_Q0_UND |
	    FPA_INT_ENB_FED1_DBE | FPA_INT_ENB_FED1_SBE |
	    FPA_INT_ENB_FED0_DBE | FPA_INT_ENB_FED0_SBE);
#endif
}

#ifdef OCTEON_ETH_DEBUG
void	cn30xxfpa_dump_regs(void);
void	cn30xxfpa_dump_bufs(void);
void	cn30xxfpa_dump_buf(int);

#define	_ENTRY(x)	{ #x, x##_OFFSET }

struct cn30xxfpa_dump_reg_ {
	const char *name;
	size_t	offset;
};

const struct cn30xxfpa_dump_reg_ cn30xxfpa_dump_regs_[] = {
	_ENTRY(FPA_INT_SUM),
	_ENTRY(FPA_INT_ENB),
	_ENTRY(FPA_CTL_STATUS),
	_ENTRY(FPA_QUE0_AVAILABLE),
	_ENTRY(FPA_QUE1_AVAILABLE),
	_ENTRY(FPA_QUE2_AVAILABLE),
	_ENTRY(FPA_QUE3_AVAILABLE),
	_ENTRY(FPA_QUE4_AVAILABLE),
	_ENTRY(FPA_QUE5_AVAILABLE),
	_ENTRY(FPA_QUE6_AVAILABLE),
	_ENTRY(FPA_QUE7_AVAILABLE),
	_ENTRY(FPA_WART_CTL),
	_ENTRY(FPA_WART_STATUS),
	_ENTRY(FPA_BIST_STATUS),
	_ENTRY(FPA_QUE0_PAGE_INDEX),
	_ENTRY(FPA_QUE1_PAGE_INDEX),
	_ENTRY(FPA_QUE2_PAGE_INDEX),
	_ENTRY(FPA_QUE3_PAGE_INDEX),
	_ENTRY(FPA_QUE4_PAGE_INDEX),
	_ENTRY(FPA_QUE5_PAGE_INDEX),
	_ENTRY(FPA_QUE6_PAGE_INDEX),
	_ENTRY(FPA_QUE7_PAGE_INDEX),
	_ENTRY(FPA_QUE_EXP),
	_ENTRY(FPA_QUE_ACT),
};

const char *cn30xxfpa_dump_bufs_[8] = {
	[0] = "recv",
	[1] = "wq",
	[2] = "cmdbuf",
	[3] = "gbuf",
};

void
cn30xxfpa_dump(void)
{
	cn30xxfpa_dump_regs();
	cn30xxfpa_dump_bufs();
}

void
cn30xxfpa_dump_regs(void)
{
	struct cn30xxfpa_softc *sc = &cn30xxfpa_softc;
	const struct cn30xxfpa_dump_reg_ *reg;
	uint64_t tmp;
	int i;

	for (i = 0; i < nitems(cn30xxfpa_dump_regs_); i++) {
		reg = &cn30xxfpa_dump_regs_[i];
		tmp = bus_space_read_8(sc->sc_regt, sc->sc_regh, reg->offset);
		printf("\t%-24s: %16llx\n", reg->name, tmp);
	}
}

/*
 * XXX assume pool 7 is unused!
 */
void
cn30xxfpa_dump_bufs(void)
{
	int i;

	for (i = 0; i < 8; i++)
		cn30xxfpa_dump_buf(i);
}

void
cn30xxfpa_dump_buf(int pool)
{
	int i;
	uint64_t ptr;
	const char *name;

	name = cn30xxfpa_dump_bufs_[pool];
	if (name == NULL)
		return;
	printf("%s pool:\n", name);
	for (i = 0; (ptr = cn30xxfpa_load(pool)) != 0; i++) {
		printf("\t%016llx%s", ptr, ((i % 4) == 3) ? "\n" : "");
		cn30xxfpa_store(ptr, OCTEON_POOL_NO_DUMP, 0);
	}
	if (i % 4 != 3)
		printf("\n");
	printf("total = %d buffers\n", i);
	while ((ptr = cn30xxfpa_load(OCTEON_POOL_NO_DUMP)) != 0)
		cn30xxfpa_store(ptr, pool, 0);
}
#endif
