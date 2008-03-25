/*	$OpenBSD: mpcpcibus.c,v 1.37 2008/03/25 22:38:28 kettenis Exp $ */

/*
 * Copyright (c) 1997 Per Fogelstrom
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Generic PCI BUS Bridge driver.
 * specialized hooks for different config methods.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/pcb.h>
#include <machine/bat.h>
#include <machine/powerpc.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <macppc/pci/pcibrvar.h>
#include <macppc/pci/mpc106reg.h>

#include <dev/ofw/openfirm.h>

int	mpcpcibrmatch(struct device *, void *, void *);
void	mpcpcibrattach(struct device *, struct device *, void *);

void	mpc_attach_hook(struct device *, struct device *,
				struct pcibus_attach_args *);
int	mpc_bus_maxdevs(void *, int);
pcitag_t mpc_make_tag(void *, int, int, int);
void	mpc_decompose_tag(void *, pcitag_t, int *, int *, int *);
pcireg_t mpc_conf_read(void *, pcitag_t, int);
void	mpc_conf_write(void *, pcitag_t, int, pcireg_t);

int      mpc_intr_map(void *, pcitag_t, int, int, pci_intr_handle_t *);
const char *mpc_intr_string(void *, pci_intr_handle_t);
int	mpc_intr_line(void *, pci_intr_handle_t);
void     *mpc_intr_establish(void *, pci_intr_handle_t,
            int, int (*func)(void *), void *, char *);
void     mpc_intr_disestablish(void *, void *);
int      mpc_ether_hw_addr(struct ppc_pci_chipset *, u_int8_t *);
u_int32_t mpc_gen_config_reg(void *cpv, pcitag_t tag, int offset);
int	of_ether_hw_addr(struct ppc_pci_chipset *, u_int8_t *);
int	find_node_intr (int parent, u_int32_t *addr, u_int32_t *intr);
u_int32_t pci_iack(void);

void fix_node_irq(int node, struct pcibus_attach_args *pba);

struct cfattach mpcpcibr_ca = {
        sizeof(struct pcibr_softc), mpcpcibrmatch, mpcpcibrattach,
};

struct cfdriver mpcpcibr_cd = {
	NULL, "mpcpcibr", DV_DULL,
};

static int      mpcpcibrprint(void *, const char *pnp);

struct pcibr_config mpc_config;

/*
 * config types
 * bit meanings
 * 0 - standard cf8/cfc type configurations,
 *     sometimes the base addresses for these are different
 * 1 - Config Method #2 configuration - uni-north
 *
 * 2 - 64 bit config bus, data for accesses &4 is at daddr+4;
 */
struct config_type{
	char * compat;
	u_int32_t addr;	/* offset */
	u_int32_t data;	/* offset */
	int config_type;
};
struct config_type config_offsets[] = {
	{"grackle",		0x00c00cf8, 0x00e00cfc, 0 },
	{"bandit",		0x00800000, 0x00c00000, 1 },
	{"uni-north",		0x00800000, 0x00c00000, 3 },
	{"u3-agp",		0x00800000, 0x00c00000, 3 },
	{"u3-ht",		0x00000cf8, 0x00000cfc, 3 },
	{"legacy",		0x00000cf8, 0x00000cfc, 0 },
	{"IBM,27-82660",	0x00000cf8, 0x00000cfc, 0 },
	{NULL,			0x00000000, 0x00000000, 0 },
};

struct powerpc_bus_dma_tag pci_bus_dma_tag = {
	NULL,
	_dmamap_create,
	_dmamap_destroy,
	_dmamap_load,
	_dmamap_load_mbuf,
	_dmamap_load_uio,
	_dmamap_load_raw,
	_dmamap_unload,
	_dmamap_sync,
	_dmamem_alloc,
	_dmamem_free,
	_dmamem_map,
	_dmamem_unmap,
	_dmamem_mmap
};

int
mpcpcibrmatch(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;
	int found = 0;

	if (strcmp(ca->ca_name, mpcpcibr_cd.cd_name) != 0)
		return (found);

	found = 1;

	return found;
}

int pci_map_a = 0;

struct ranges_32 {
	u_int32_t flags;
	u_int32_t pad1;
	u_int32_t pad2;
	u_int32_t base;
	u_int32_t pad3;
	u_int32_t size;
};
void
mpcpcibus_find_ranges_32 (struct pcibr_softc *sc, u_int32_t *range_store,
    int rangesize);
void
mpcpcibus_find_ranges_64 (struct pcibr_softc *sc, u_int32_t *range_store,
    int rangesize);
void
mpcpcibus_find_ranges_32 (struct pcibr_softc *sc, u_int32_t *range_store,
    int rangesize)
{
	int found;
	unsigned int base = 0;
	unsigned int size = 0;
	struct ranges_32 *prange = (void *)range_store;
	int rangelen;
	int i;

	rangelen = rangesize / sizeof (struct ranges_32);

	/* mac configs */
	sc->sc_membus_space.bus_base = 0;
	sc->sc_membus_space.bus_io = 0;
	sc->sc_iobus_space.bus_base = 0;
	sc->sc_iobus_space.bus_io = 1;

	/* find io(config) base, flag == 0x01000000 */
	found = 0;
	for (i = 0; i < rangelen ; i++) {
		if (prange[i].flags == 0x01000000) {
			/* find last? */
			found = i;
		}
	}
	/* found the io space ranges */
	if (prange[found].flags == 0x01000000) {
		sc->sc_iobus_space.bus_base =
		    prange[found].base;
		sc->sc_iobus_space.bus_size =
		    prange[found].size;
	}

	/* the mem space ranges 
	 * apple openfirmware always puts full
	 * addresses in config information,
	 * it is not necessary to have correct bus
	 * base address, but since 0 is reserved
	 * and all IO and device memory will be in
	 * upper 2G of address space, set to
	 * 0x80000000
	 * start with segment 1 not 0, 0 is config.
	 */
	for (i = 0; i < rangelen ; i++) {
		if (prange[i].flags == 0x02000000) {
#ifdef DEBUG_PCI
			printf("\nfound mem %x %x",
				prange[i].base,
				prange[i].size);
#endif
			if (base != 0) {
				if ((base + size) == prange[i].base)   
					size += prange[i].size;
				else {
					size = prange[i].size;
					base = prange[i].base;
				} 
			} else {
				base = prange[i].base;
				size = prange[i].size;
			}
		}
	}
	sc->sc_membus_space.bus_base = base;
	sc->sc_membus_space.bus_size = size;
}

struct ranges_64 {
	u_int32_t flags;
	u_int32_t pad1;
	u_int32_t pad2;
	u_int32_t pad3;
	u_int32_t base;
	u_int32_t pad4;
	u_int32_t size;
};
void
mpcpcibus_find_ranges_64 (struct pcibr_softc *sc, u_int32_t *range_store,
    int rangesize)
{
	int i, found;
	unsigned int base = 0;
	unsigned int size = 0;
	int rangelen;
	struct ranges_64 *prange = (void *)range_store;

	rangelen = rangesize / sizeof (struct ranges_64);

	/* mac configs */

	sc->sc_membus_space.bus_base = 0;
	sc->sc_membus_space.bus_io = 0;
	sc->sc_iobus_space.bus_base = 0;
	sc->sc_iobus_space.bus_io = 1;

	if (prange[0].flags == 0xabb10113) { /* appl U3; */
		prange[0].flags = 0x01000000;
		prange[0].base = 0xf8070000;
		prange[0].size = 0x00001000;
		prange[1].flags = 0x02000000;
		prange[1].base = 0xf2000000;
		prange[1].size = 0x02800000;      
		rangelen = 2;
	}

	/* find io(config) base, flag == 0x01000000 */
	found = 0;
	for (i = 0; i < rangelen ; i++) {
		if (prange[i].flags == 0x01000000) {
			/* find last? */
			found = i;
		}
	}
	/* found the io space ranges */
	if (prange[found].flags == 0x01000000) {
		sc->sc_iobus_space.bus_base = prange[found].base;
		sc->sc_iobus_space.bus_size = prange[found].size;
	}

	/* the mem space ranges 
	 * apple openfirmware always puts full
	 * addresses in config information,
	 * it is not necessary to have correct bus
	 * base address, but since 0 is reserved
	 * and all IO and device memory will be in
	 * upper 2G of address space, set to
	 * 0x80000000
	 * start with segment 1 not 0, 0 is config.
	 */
	for (i = 0; i < rangelen ; i++) {
		if (prange[i].flags == 0x02000000) {
#ifdef DEBUG_PCI
			printf("\nfound mem %x %x",
				prange[i].base,
				prange[i].size);
#endif
				
			if (base != 0) {
				if ((base + size) == prange[i].base) {
					size += prange[i].size;
				} else {
					base = prange[i].base;
					size = prange[i].size;
				}
			} else {
				base = prange[i].base;
				size = prange[i].size;
			}
		}
	}
	sc->sc_membus_space.bus_base = base;
	sc->sc_membus_space.bus_size = size;
}

void
mpcpcibrattach(struct device *parent, struct device *self, void *aux)
{
	struct pcibr_softc *sc = (struct pcibr_softc *)self;
	struct confargs *ca = aux;
	struct pcibr_config *lcp;
	struct pcibus_attach_args pba;
	int node;
	int of_node = 0;
	char compat[32];
	u_int32_t addr_offset;
	u_int32_t data_offset;
	int i;
	int len;
	int rangesize;
	u_int32_t range_store[32];

	if (ca->ca_node == 0) {
		printf("invalid node on mpcpcibr config\n");
		return;
	}
	len=OF_getprop(ca->ca_node, "name", compat, sizeof (compat));
	compat[len] = '\0';
	if (len > 0)
		printf(" %s", compat);

	len=OF_getprop(ca->ca_node, "compatible", compat,
	    sizeof (compat));
	if (len <= 0 ) {
		len=OF_getprop(ca->ca_node, "name", compat,
			sizeof (compat));
		if (len <= 0) {
			printf(" compatible and name not found\n");
			return;
		}
		compat[len] = 0; 
		if (strcmp (compat, "bandit") != 0) {
			printf(" compatible not found and name %s found\n",
			    compat);
			return;
		}
	}
	compat[len] = 0; 
	if ((rangesize = OF_getprop(ca->ca_node, "ranges",
	    range_store, sizeof (range_store))) <= 0) {
		if (strcmp(compat, "u3-ht") == 0) {
			range_store[0] = 0xabb10113; /* appl U3; */
		} else 
			printf("range lookup failed, node %x\n", ca->ca_node);
	}
	/* translate byte(s) into item count*/

	lcp = sc->sc_pcibr = &sc->pcibr_config;

	if (ppc_proc_is_64b)
		mpcpcibus_find_ranges_64 (sc, range_store, rangesize);
	else
		mpcpcibus_find_ranges_32 (sc, range_store, rangesize);

	addr_offset = 0;
	for (i = 0; config_offsets[i].compat != NULL; i++) {
		struct config_type *co = &config_offsets[i];
		if (strcmp(co->compat, compat) == 0) {
			addr_offset = co->addr; 
			data_offset = co->data; 
			lcp->config_type = co->config_type;
			break;
		}
	}
	if (addr_offset == 0) {
		printf("unable to find match for"
		    " compatible %s\n", compat);
		return;
	}
#ifdef DEBUG_FIXUP
	printf(" mem base %x sz %x io base %x sz %x\n"
	    " config addr %x config data %x\n",
	    sc->sc_membus_space.bus_base,
	    sc->sc_membus_space.bus_size,
	    sc->sc_iobus_space.bus_base,
	    sc->sc_iobus_space.bus_size,
	    addr_offset, data_offset);
#endif

	if ( bus_space_map(&(sc->sc_iobus_space), addr_offset,
		NBPG, 0, &lcp->ioh_cf8) != 0 )
		panic("mpcpcibus: unable to map self");

	if ( bus_space_map(&(sc->sc_iobus_space), data_offset,
		NBPG, 0, &lcp->ioh_cfc) != 0 )
		panic("mpcpcibus: unable to map self");

	of_node = ca->ca_node;

	lcp->node = ca->ca_node;
	lcp->lc_pc.pc_conf_v = lcp;
	lcp->lc_pc.pc_attach_hook = mpc_attach_hook;
	lcp->lc_pc.pc_bus_maxdevs = mpc_bus_maxdevs;
	lcp->lc_pc.pc_make_tag = mpc_make_tag;
	lcp->lc_pc.pc_decompose_tag = mpc_decompose_tag;
	lcp->lc_pc.pc_conf_read = mpc_conf_read;
	lcp->lc_pc.pc_conf_write = mpc_conf_write;
	lcp->lc_pc.pc_ether_hw_addr = of_ether_hw_addr;
	lcp->lc_iot = &sc->sc_iobus_space;
	lcp->lc_memt = &sc->sc_membus_space;

	lcp->lc_pc.pc_intr_v = lcp;
	lcp->lc_pc.pc_intr_map = mpc_intr_map;
	lcp->lc_pc.pc_intr_string = mpc_intr_string;
	lcp->lc_pc.pc_intr_line = mpc_intr_line;
	lcp->lc_pc.pc_intr_establish = mpc_intr_establish;
	lcp->lc_pc.pc_intr_disestablish = mpc_intr_disestablish;

	printf(": %s, Revision 0x%x\n", compat, 
	    mpc_cfg_read_1(lcp, MPC106_PCI_REVID));

	if ((strcmp(compat, "bandit")) != 0)
		pci_addr_fixup(sc, &lcp->lc_pc, 32);

	pba.pba_dmat = &pci_bus_dma_tag;

	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_iobus_space;
	pba.pba_memt = &sc->sc_membus_space;
	pba.pba_pc = &lcp->lc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;
	pba.pba_bridgetag = NULL;

	/* we want to check pci irq settings */
	if (of_node != 0) {
		int nn;

		for (node = OF_child(of_node); node; node = nn) {
			char name[32];
			int len;
			len = OF_getprop(node, "name", name,
			    sizeof(name));
			name[len] = 0;
			fix_node_irq(node, &pba);

			/* iterate section */
			if ((nn = OF_child(node)) != 0)
				continue;

			while ((nn = OF_peer(node)) == 0) {
				node = OF_parent(node);
				if (node == of_node) {
					nn = 0; /* done */
					break;
				}
			}
		}
	}

	config_found(self, &pba, mpcpcibrprint);

}
                 
#define       OFW_PCI_PHYS_HI_BUSMASK         0x00ff0000
#define       OFW_PCI_PHYS_HI_BUSSHIFT        16
#define       OFW_PCI_PHYS_HI_DEVICEMASK      0x0000f800
#define       OFW_PCI_PHYS_HI_DEVICESHIFT     11
#define       OFW_PCI_PHYS_HI_FUNCTIONMASK    0x00000700
#define       OFW_PCI_PHYS_HI_FUNCTIONSHIFT   8

#define pcibus(x) \
	(((x) & OFW_PCI_PHYS_HI_BUSMASK) >> OFW_PCI_PHYS_HI_BUSSHIFT)
#define pcidev(x) \
	(((x) & OFW_PCI_PHYS_HI_DEVICEMASK) >> OFW_PCI_PHYS_HI_DEVICESHIFT)
#define pcifunc(x) \
	(((x) & OFW_PCI_PHYS_HI_FUNCTIONMASK) >> OFW_PCI_PHYS_HI_FUNCTIONSHIFT)

/* 
 * Find PCI IRQ from OF.
 */
int
find_node_intr(int parent, u_int32_t *addr, u_int32_t *intr)
{
	int iparent, len, mlen, alen, ilen;
	int match, i, step;
	u_int32_t map[144], *mp, *mp1;
	u_int32_t imask[8], maskedaddr[8];
	u_int32_t address_cells, interrupt_cells, mask_cells;

	len = OF_getprop(parent, "interrupt-map", map, sizeof(map));
	mlen = OF_getprop(parent, "interrupt-map-mask", imask, sizeof(imask));
	alen = OF_getprop(parent, "#address-cells",
	    &address_cells, sizeof(address_cells));
	ilen = OF_getprop(parent, "#interrupt-cells",
	    &interrupt_cells, sizeof(interrupt_cells));

	if (len == -1 || mlen == -1 || alen == -1 || ilen == -1)
		goto nomap;

	mask_cells = address_cells + interrupt_cells;
	if (mask_cells != (mlen / sizeof(u_int32_t)))
		goto nomap;
	for (i = 0; i < mask_cells; i++)
		maskedaddr[i] = addr[i] & imask[i];

	/* interrupt-map is formatted as follows
	 * int * #address-cells, int * #interrupt-cells, int, int, int
	 * eg
	 * address-cells = 3
	 * interrupt-cells = 1
	 * 00001000 00000000 00000000 00000000 ff911258 00000034 00000001 
	 * 00001800 00000000 00000000 00000000 ff911258 00000035 00000001 
	 * 00002000 00000000 00000000 00000000 ff911258 00000036 00000001 
	 * | address cells          | | intr | |parent| | irq  | |edge/level|
	 *                            | cells|          | interrupt cells   |
	 *                                              | of parent         |
	 * or at least something close to that.
	 */

	mp = map;
	while (len > mlen) {
		mp1 = mp + mask_cells;

		iparent = *mp1;
		alen = OF_getprop(iparent, "#address-cells",
		    &address_cells, sizeof(address_cells));
		if (alen == -1)
			address_cells = 0;
		ilen = OF_getprop(iparent, "#interrupt-cells",
		    &interrupt_cells, sizeof(interrupt_cells));
		if (ilen == -1)
			goto nomap;

		step = mask_cells + 1 + address_cells + interrupt_cells;

		match = bcmp(maskedaddr, mp, mlen);
		if (match == 0) {
			if (OF_getprop(iparent, "interrupt-controller",
				       NULL, 0) == 0) {
				*intr = mp1[1];
				return 1;
			}
			/* Recurse with new 'addr'. */
			return find_node_intr(iparent, &mp1[1], intr);
		}
		len -= step * sizeof(u_int32_t);
		mp += step;
	}
nomap:
	return -1;
}

void
fix_node_irq(int node, struct pcibus_attach_args *pba)
{
	struct { 
		u_int32_t phys_hi, phys_mid, phys_lo;
		u_int32_t size_hi, size_lo;
	} addr [8];
	u_int32_t map[144];
	int len;
	pcitag_t tag;
	u_int32_t irq;
	u_int32_t intr;
	int parent;

	pci_chipset_tag_t pc = pba->pba_pc;

	len = OF_getprop(node, "assigned-addresses", addr, sizeof(addr));
	if (len == -1 || len < sizeof(addr[0]))
		return;

	/* if this node has a AAPL,interrupts property, firmware
	 * has initialized the register correctly.
	 */
	len = OF_getprop(node, "AAPL,interrupts", &intr, 4);
	if (len != 4) {

		parent = OF_parent(node);

		irq = -1;

		/* we want the first interrupt, set size_hi to 1 */
		addr[0].size_hi = 1;
		if (find_node_intr(parent, &addr[0].phys_hi, &irq) == -1) {
			len = OF_getprop(node, "interrupts", map,
			    sizeof(map));
			if (len != -1 && len != 4) {
				irq = map[0];
			} else 
				return;
		}
	} else
		irq = intr;
	/* program the interrupt line register with the value
	 * found in openfirmware
	 */

	tag = pci_make_tag(pc, pcibus(addr[0].phys_hi),
	    pcidev(addr[0].phys_hi), pcifunc(addr[0].phys_hi));

	intr = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);
	intr &= ~PCI_INTERRUPT_LINE_MASK;
	intr |= irq & PCI_INTERRUPT_LINE_MASK;
	pci_conf_write(pc, tag, PCI_INTERRUPT_REG, intr);
}

static int
mpcpcibrprint(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return(UNCONF);
}

void
mpc_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
of_ether_hw_addr(struct ppc_pci_chipset *lcpc, u_int8_t *oaddr)
{
	u_int8_t laddr[6];
	struct pcibr_config *lcp = lcpc->pc_conf_v;
	int of_node = lcp->node;
	int node, nn;
	for (node = OF_child(of_node); node; node = nn) {
		char name[32];
		int len;
		len = OF_getprop(node, "name", name,
			sizeof(name));
		name[len] = 0;

		len = OF_getprop(node, "local-mac-address", laddr,
		    sizeof laddr);
		if (sizeof (laddr) == len) {
			bcopy (laddr, oaddr, sizeof laddr);
			return 1;
		}

		/* iterate section */
		if ((nn = OF_child(node)) != 0) {
			continue;
		}
		while ((nn = OF_peer(node)) == 0) {
			node = OF_parent(node);
			if (node == of_node) {
				nn = 0; /* done */
				break;
			}
		}
	}
	oaddr[0] = oaddr[1] = oaddr[2] = 0xff;
	oaddr[3] = oaddr[4] = oaddr[5] = 0xff;
	return 0;
}

int
mpc_ether_hw_addr(struct ppc_pci_chipset *p, u_int8_t *s)
{
	printf("mpc_ether_hw_addr not supported\n");
	return(0);
}

int
mpc_bus_maxdevs(void *cpv, int busno)
{
	return(32);
}

#define BUS_SHIFT 16
#define DEVICE_SHIFT 11
#define FNC_SHIFT 8

pcitag_t
mpc_make_tag(void *cpv, int bus, int dev, int fnc)
{
	return (bus << BUS_SHIFT) | (dev << DEVICE_SHIFT) | (fnc << FNC_SHIFT);
}

void
mpc_decompose_tag(void *cpv, pcitag_t tag, int *busp, int *devp, int *fncp)
{
	if (busp != NULL)
		*busp = (tag >> BUS_SHIFT) & 0xff;
	if (devp != NULL)
		*devp = (tag >> DEVICE_SHIFT) & 0x1f;
	if (fncp != NULL)
		*fncp = (tag >> FNC_SHIFT) & 0x7;
}

u_int32_t
mpc_gen_config_reg(void *cpv, pcitag_t tag, int offset)
{
	struct pcibr_config *cp = cpv;
	unsigned int bus, dev, fcn;
	u_int32_t reg;

	mpc_decompose_tag(cpv, tag, &bus, &dev, &fcn);

	if (cp->config_type & 1) {
		/* Config Mechanism #2 */
		if (bus == 0) {
			if (dev < 11)
				return 0xffffffff;
			/*
			 * Need to do config type 0 operation
			 *  1 << (11?+dev) | fcn << 8 | reg
			 * 11? is because pci spec states
			 * that 11-15 is reserved.
			 */
			reg = 1 << (dev) | fcn << 8 | offset;
			
		} else {
			if (dev > 15)
				return 0xffffffff;
			/*
			 * config type 1 
			 */
			reg =  tag | offset | 1;
		}
	} else {
		/* config mechanism #2, type 0
		 * standard cf8/cfc config
		 */
		reg =  0x80000000 | tag  | offset;
	}

	return reg;
}

/* #define DEBUG_CONFIG  */
pcireg_t
mpc_conf_read(void *cpv, pcitag_t tag, int offset)
{
	struct pcibr_config *cp = cpv;
	pcireg_t data;
	u_int32_t reg;
	int s;
	int daddr = 0;
	faultbuf env;
	void *oldh;

	if (offset & 3 || offset < 0 || offset >= 0x100) {
#ifdef DEBUG_CONFIG 
		printf ("pci_conf_read: bad reg %x\n", offset);
#endif /* DEBUG_CONFIG */
		return(~0);
	}

	reg = mpc_gen_config_reg(cpv, tag, offset);
	/* if invalid tag, return -1 */
	if (reg == 0xffffffff)
		return(~0);

	if ((cp->config_type & 2) && (offset & 0x04))
		daddr += 4;

	s = splhigh();

	oldh = curpcb->pcb_onfault;
	if (setfault(&env)) {
		/* we faulted during the read? */
		curpcb->pcb_onfault = oldh;
		splx(s);
		return 0xffffffff;
	}

	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, reg);
	bus_space_read_4(cp->lc_iot, cp->ioh_cf8, 0); /* XXX */
	data = bus_space_read_4(cp->lc_iot, cp->ioh_cfc, daddr);
	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, 0); /* disable */
	bus_space_read_4(cp->lc_iot, cp->ioh_cf8, 0); /* XXX */

	curpcb->pcb_onfault = oldh;

	splx(s);
#ifdef DEBUG_CONFIG
	if (!((offset == 0) && (data == 0xffffffff))) {
		unsigned int bus, dev, fcn;
		mpc_decompose_tag(cpv, tag, &bus, &dev, &fcn);
		printf("mpc_conf_read bus %x dev %x fcn %x offset %x", bus, dev, fcn,
			offset);
		printf(" daddr %x reg %x",daddr, reg);
		printf(" data %x\n", data);
	}
#endif

	return(data);
}

void
mpc_conf_write(void *cpv, pcitag_t tag, int offset, pcireg_t data)
{
	struct pcibr_config *cp = cpv;
	u_int32_t reg;
	int s;
	int daddr = 0;

	reg = mpc_gen_config_reg(cpv, tag, offset);

	/* if invalid tag, return ??? */
	if (reg == 0xffffffff)
		return;

	if ((cp->config_type & 2) && (offset & 0x04))
		daddr += 4;

#ifdef DEBUG_CONFIG
	{
		unsigned int bus, dev, fcn;
		mpc_decompose_tag(cpv, tag, &bus, &dev, &fcn);
		printf("mpc_conf_write bus %x dev %x fcn %x offset %x", bus,
			dev, fcn, offset);
		printf(" daddr %x reg %x",daddr, reg);
		printf(" data %x\n", data);
	}
#endif

	s = splhigh();

	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, reg);
	bus_space_read_4(cp->lc_iot, cp->ioh_cf8, 0); /* XXX */
	bus_space_write_4(cp->lc_iot, cp->ioh_cfc, daddr, data);
	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, 0); /* disable */
	bus_space_read_4(cp->lc_iot, cp->ioh_cf8, 0); /* XXX */

	splx(s);
}


/*ARGSUSED*/
int
mpc_intr_map(void *lcv, pcitag_t bustag, int buspin, int  line,
    pci_intr_handle_t *ihp)
{
	int error = 0;

	*ihp = -1;
        if (buspin == 0)
                error = 1; /* No IRQ used. */
        else if (buspin > 4) {
                printf("mpc_intr_map: bad interrupt pin %d\n", buspin);
                error = 1;
        }
	if (line == 0xff)
		error = 1;

	if (!error)
		*ihp = line;
	return error;
}

const char *
mpc_intr_string(void *lcv, pci_intr_handle_t ih)
{
	static char str[16];

	snprintf(str, sizeof str, "irq %ld", ih);
	return(str);
}

int
mpc_intr_line(void *lcv, pci_intr_handle_t ih)
{
	return (ih);
}

void *
mpc_intr_establish(void *lcv, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, char *name)
{
	return (*intr_establish_func)(lcv, ih, IST_LEVEL, level, func, arg,
		name);
}

void
mpc_intr_disestablish(void *lcv, void *cookie)
{
	/* XXX We should probably do something clever here.... later */
}

u_int32_t
pci_iack()
{
	/* do pci IACK cycle */
	/* this should be bus allocated. */
	volatile u_int8_t *iack = (u_int8_t *)0xbffffff0;
	u_int8_t val;

	val = *iack;
	return val;
}

void
mpc_cfg_write_1(struct pcibr_config *cp, u_int32_t reg, u_int8_t val)
{
	int s;
	s = splhigh();
	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, MPC106_REGOFFS(reg));
	bus_space_write_1(cp->lc_iot, cp->ioh_cfc, 0, val);
	splx(s);
}

void
mpc_cfg_write_2(struct pcibr_config *cp, u_int32_t reg, u_int16_t val)
{
	int s;
	s = splhigh();
	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, MPC106_REGOFFS(reg));
	bus_space_write_2(cp->lc_iot, cp->ioh_cfc, 0, val);
	splx(s);
}

void
mpc_cfg_write_4(struct pcibr_config *cp, u_int32_t reg, u_int32_t val)
{

	int s;
	s = splhigh();
	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, MPC106_REGOFFS(reg));
	bus_space_write_4(cp->lc_iot, cp->ioh_cfc, 0, val);
	splx(s);
}

u_int8_t
mpc_cfg_read_1(struct pcibr_config *cp, u_int32_t reg)
{
	u_int8_t _v_;

	int s;
	s = splhigh();
	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, MPC106_REGOFFS(reg));
	_v_ = bus_space_read_1(cp->lc_iot, cp->ioh_cfc, 0);
	splx(s);
	return(_v_);
}

u_int16_t
mpc_cfg_read_2(struct pcibr_config *cp, u_int32_t reg)
{
	u_int16_t _v_;

	int s;
	s = splhigh();
	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, MPC106_REGOFFS(reg));
	_v_ = bus_space_read_2(cp->lc_iot, cp->ioh_cfc, 0);
	splx(s);
	return(_v_);
}

u_int32_t
mpc_cfg_read_4(struct pcibr_config *cp, u_int32_t reg)
{
	u_int32_t _v_;

	int s;
	s = splhigh();
	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, MPC106_REGOFFS(reg));
	_v_ = bus_space_read_4(cp->lc_iot, cp->ioh_cfc, 0);
	splx(s);
	return(_v_);
}
