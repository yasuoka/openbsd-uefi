/*	$OpenBSD: if_nxe.c,v 1.5 2007/08/14 23:45:25 dlg Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
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

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/proc.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

/*
 * PCI configuration space registers
 */

#define NXE_PCI_BAR_MEM		0x10 /* bar 0 */
#define NXE_PCI_BAR_MEM_128MB		(128 * 1024 * 1024)
#define NXE_PCI_BAR_DOORBELL	0x20 /* bar 4 */

/*
 * doorbell register space
 */

#define NXE_DB			0x00000000
#define  NXE_DB_PEGID			0x00000003
#define  NXE_DB_PEGID_RX		0x00000001 /* rx unit */
#define  NXE_DB_PEGID_TX		0x00000002 /* tx unit */
#define  NXE_DB_PRIVID			0x00000004 /* must be set */
#define  NXE_DB_COUNT(_c)		((_c)<<3) /* count */
#define  NXE_DB_CTXID(_c)		((_c)<<18) /* context id */
#define  NXE_DB_OPCODE_RX_PROD		0x00000000
#define  NXE_DB_OPCODE_RX_JUMBO_PROD	0x10000000
#define  NXE_DB_OPCODE_RX_LRO_PROD	0x20000000
#define  NXE_DB_OPCODE_CMD_PROD		0x30000000
#define  NXE_DB_OPCODE_UPD_CONS		0x40000000
#define  NXE_DB_OPCODE_RESET_CTX	0x50000000

/*
 * register space
 */

/* different PCI functions use different registers sometimes */
#define _F(_f)			((_f) * 0x20)

/*
 * driver ref section 4.2
 *
 * All the hardware registers are mapped in memory. Apart from the registers
 * for the individual hardware blocks, the memory map includes a large number
 * of software definable registers.
 *
 * The following table gives the memory map in the PCI address space.
 */

#define NXE_MAP_DDR_NET		0x00000000
#define NXE_MAP_DDR_MD		0x02000000
#define NXE_MAP_QDR_NET		0x04000000
#define NXE_MAP_DIRECT_CRB	0x04400000
#define NXE_MAP_OCM0		0x05000000
#define NXE_MAP_OCM1		0x05100000
#define NXE_MAP_CRB		0x06000000

/*
 * Since there are a large number of registers they do not fit in a single
 * PCI addressing range. Hence two windows are defined. The window starts at
 * NXE_MAP_CRB, and extends to the end of the register map. The window is set
 * using the NXE_REG_WINDOW_CRB register. The format of the NXE_REG_WINDOW_CRB
 * register is as follows:
 */

#define NXE_WIN_CRB(_f)		(0x06110210 + _F(_f))
#define  NXE_WIN_CRB_0			(0<<25)
#define  NXE_WIN_CRB_1			(1<<25)

/*
 * The memory map inside the register windows are divided into a set of blocks.
 * Each register block is owned by one hardware agent. The following table
 * gives the memory map of the various register blocks in window 0. These
 * registers are all in the CRB register space, so the offsets given here are
 * relative to the base of the CRB offset region (NXE_MAP_CRB).
 */

#define NXE_W0_PCIE		0x00100000 /* PCI Express */
#define NXE_W0_NIU		0x00600000 /* Network Interface Unit */
#define NXE_W0_PPE_0		0x01100000 /* Protocol Processing Engine 0 */
#define NXE_W0_PPE_1		0x01200000 /* Protocol Processing Engine 1 */
#define NXE_W0_PPE_2		0x01300000 /* Protocol Processing Engine 2 */
#define NXE_W0_PPE_3		0x01400000 /* Protocol Processing Engine 3 */
#define NXE_W0_PPE_D		0x01500000 /* PPE D-cache */
#define NXE_W0_PPE_I		0x01600000 /* PPE I-cache */

/*
 * These are the register blocks inside window 1.
 */

#define NXE_W1_PCIE		0x00100000
#define NXE_W1_SW		0x00200000
#define NXE_W1_SIR		0x01200000
#define NXE_W1_ROMUSB		0x01300000


/*
 * autoconf glue
 */

struct nxe_softc {
	struct device		sc_dev;

	bus_dma_tag_t		sc_dmat;

	bus_space_tag_t		sc_memt;
	bus_space_handle_t	sc_memh;
	bus_size_t		sc_mems;
	bus_space_tag_t		sc_dbt;
	bus_space_handle_t	sc_dbh;
	bus_size_t		sc_dbs;

	void			*sc_ih;
};

int			nxe_match(struct device *, void *, void *);
void			nxe_attach(struct device *, struct device *, void *);
int			nxe_intr(void *);

struct cfattach nxe_ca = {
	sizeof(struct nxe_softc),
	nxe_match,
	nxe_attach
};

struct cfdriver nxe_cd = {
	NULL,
	"nxe",
	DV_IFNET
};

/* low level hardware access goo */
u_int32_t		nxe_read(struct nxe_softc *, bus_size_t);
void			nxe_write(struct nxe_softc *, bus_size_t, u_int32_t);

/* misc bits */
#define DEVNAME(_sc)	((_sc)->sc_dev.dv_xname)
#define sizeofa(_a)	(sizeof(_a) / sizeof((_a)[0]))

/* let's go! */

const struct pci_matchid nxe_devices[] = {
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_10GXxR },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_10GCX4 },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_4GCU },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_IMEZ },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_HMEZ },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_IMEZ_2 },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_HMEZ_2 }
};

int
nxe_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args		*pa = aux;

	if (PCI_CLASS(pa->pa_class) != PCI_CLASS_NETWORK)
		return (0);

	return (pci_matchbyid(pa, nxe_devices, sizeofa(nxe_devices)));
}

void
nxe_attach(struct device *parent, struct device *self, void *aux)
{
	struct nxe_softc		*sc = (struct nxe_softc *)self;
	struct pci_attach_args		*pa = aux;
	pcireg_t			memtype;

	sc->sc_dmat = pa->pa_dmat;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, NXE_PCI_BAR_MEM);
	if (pci_mapreg_map(pa, NXE_PCI_BAR_MEM, memtype, 0, &sc->sc_memt,
	    &sc->sc_memh, NULL, &sc->sc_mems, 0) != 0) {
		printf(": unable to map host registers\n");
		return;
	}
	if (sc->sc_mems != NXE_PCI_BAR_MEM_128MB) {
		printf(": unexpected register map size\n");
		goto unmap_mem;
	}

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, NXE_PCI_BAR_DOORBELL);
	if (pci_mapreg_map(pa, NXE_PCI_BAR_DOORBELL, memtype, 0, &sc->sc_dbt,
	    &sc->sc_dbh, NULL, &sc->sc_dbs, 0) != 0) {
		printf(": unable to map doorbell registers\n");
		goto unmap_mem;
	}

	printf("\n");
	return;

unmap_mem:
	bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_mems);
	sc->sc_mems = 0;
}

int
nxe_intr(void *xsc)
{
	return (0);
}

u_int32_t
nxe_read(struct nxe_softc *sc, bus_size_t r)
{
	bus_space_barrier(sc->sc_memt, sc->sc_memh, r, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_memt, sc->sc_memh, r));
}

void
nxe_write(struct nxe_softc *sc, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sc->sc_memt, sc->sc_memh, r, v);
	bus_space_barrier(sc->sc_memt, sc->sc_memh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}
