/*	$OpenBSD: if_gm.c,v 1.6 2002/03/14 01:26:36 millert Exp $	*/
/*	$NetBSD: if_gm.c,v 1.14 2001/07/22 11:29:46 wiz Exp $	*/

/*-
 * Copyright (c) 2000 Tsubai Masanari.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __NetBSD__
#include "opt_inet.h"
#include "opt_ns.h"
#endif /* __NetBSD__ */
#include "bpfilter.h"

#include <sys/param.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#ifdef INET
#include <netinet/in.h>
#ifdef __NetBSD__
#include <netinet/if_inarp.h>
#endif /* __NetBSD__ */
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/ofw/openfirm.h>
#include <macppc/dev/if_gmreg.h>
#include <machine/pio.h>

#define NTXBUF 4
#define NRXBUF 32
#define	GM_BUFSZ	((NRXBUF + NTXBUF + 2) * 2048)

struct gmac_softc {
	struct device sc_dev;
#ifdef __OpenBSD__
	struct arpcom arpcom;	/* per-instance network data */
#define sc_if arpcom.ac_if
#define	sc_enaddr arpcom.ac_enaddr
#else
	struct ethercom sc_ethercom;
#define sc_if sc_ethercom.ec_if
	u_int8_t sc_laddr[6];
#endif
	vaddr_t sc_reg;
	bus_space_handle_t gm_bush;
	bus_space_tag_t    gm_bust;
	bus_dma_tag_t	gm_dmat;
	bus_dmamap_t	sc_bufmap;
	bus_dma_segment_t sc_bufseg[1];
	struct gmac_dma *sc_txlist;
	paddr_t	sc_txlist_pa;
	struct gmac_dma *sc_rxlist;
	paddr_t	sc_rxlist_pa;
	int sc_txnext;
	int sc_rxlast;
	caddr_t sc_txbuf[NTXBUF];
	caddr_t sc_rxbuf[NRXBUF];
	struct mii_data sc_mii;
	struct timeout sc_tmo;
};


int gmac_match(struct device *, void *, void *);
void gmac_attach(struct device *, struct device *, void *);

static __inline u_int gmac_read_reg(struct gmac_softc *, int);
static __inline void gmac_write_reg(struct gmac_softc *, int, u_int);

static __inline void gmac_start_txdma(struct gmac_softc *);
static __inline void gmac_start_rxdma(struct gmac_softc *);
static __inline void gmac_stop_txdma(struct gmac_softc *);
static __inline void gmac_stop_rxdma(struct gmac_softc *);

int gmac_intr(void *);
void gmac_tint(struct gmac_softc *);
void gmac_rint(struct gmac_softc *);
struct mbuf * gmac_get(struct gmac_softc *, caddr_t, int);
void gmac_start(struct ifnet *);
int gmac_put(struct gmac_softc *, caddr_t, struct mbuf *);

void gmac_stop(struct gmac_softc *);
void gmac_reset(struct gmac_softc *);
void gmac_init(struct gmac_softc *);
void gmac_init_mac(struct gmac_softc *);
void gmac_setladrf(struct gmac_softc *);

int gmac_ioctl(struct ifnet *, u_long, caddr_t);
void gmac_watchdog(struct ifnet *);
void gmac_enable_hack(void);

int gmac_mediachange(struct ifnet *);
void gmac_mediastatus(struct ifnet *, struct ifmediareq *);
int gmac_mii_readreg(struct device *, int, int);
void gmac_mii_writereg(struct device *, int, int, int);
void gmac_mii_statchg(struct device *);
void gmac_mii_tick(void *);

u_int32_t ether_crc32_le(const u_int8_t *buf, size_t len);

#ifdef __NetBSD__
#define	letoh32	 le32toh
#endif


struct cfattach gm_ca = {
	sizeof(struct gmac_softc), gmac_match, gmac_attach
};
struct cfdriver gm_cd = {
	NULL, "gm", DV_IFNET
};

int
gmac_match(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_APPLE)
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_APPLE_GMAC:
		case PCI_PRODUCT_APPLE_GMAC2:
			return 1;
		}

	return 0;
}

void
gmac_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct gmac_softc *sc = (void *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	struct ifnet *ifp = &sc->sc_if;
	struct mii_data *mii = &sc->sc_mii;
	const char *intrstr = NULL;
	char intrstrbuf[20];
	bus_addr_t	membase;
	bus_size_t	memsize;
#ifdef __NetBSD__
	int node;
#endif
	int i, nseg, error;
	struct gmac_dma *dp;
	u_char laddr[6];

#ifdef __NetBSD__
	node = pcidev_to_ofdev(pa->pa_pc, pa->pa_tag);
	if (node == 0) {
		printf(": cannot find gmac node\n");
		return;
	}

	OF_getprop(node, "local-mac-address", laddr, sizeof laddr);
	OF_getprop(node, "assigned-addresses", reg, sizeof reg);
	bcopy(laddr, sc->sc_laddr, sizeof laddr);
	sc->sc_reg = reg[2];
#endif
#ifdef __OpenBSD__
	pci_ether_hw_addr(pc, laddr);

	/* proper pci configuration */
	{
		u_int32_t	command;
		command = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
		command |= PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE |
			PCI_COMMAND_MASTER_ENABLE;
		pci_conf_write(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG, command);

#ifdef USE_IO
		if (pci_mapreg_map(pa, 0x10, PCI_MAPREG_TYPE_IO, 0,
		    &sc->gm_bust, &sc->gm_bush, &iobase, &iosize, 0)) {
			printf(": can't map controller i/o space\n");
			return;
		}
#else /* !USE_IO */
		if (pci_mapreg_map(pa, 0x10, PCI_MAPREG_TYPE_MEM, 0,
		    &sc->gm_bust, &sc->gm_bush, &membase, &memsize, 0)) {
			printf(": can't map controller mem space\n");
			return;
		}
#endif /* !USE_IO */

	}
#endif

#if 0
	if (pci_intr_map(pa, &ih)) {
		printf(": unable to map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);

	if (pci_intr_establish(pa->pa_pc, ih, IPL_NET, gmac_intr, sc, "gmac") == NULL) {
		printf(": unable to establish interrupt");
		if (intrstr)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
#endif 
#if 1
	sprintf(intrstrbuf, "irq %d", pa->pa_intrline);
	intrstr = intrstrbuf;
	/*
	if (pci_intr_establish(pa->pa_pc, pa->pa_intrline, IPL_NET,
	* Someone explain how to get the interrupt line correctly from the
	* pci info? pa_intrline returns 60, not 1 like the hardware expects
	* on uni-north G4 system.
	*/
	if (pci_intr_establish(pa->pa_pc, pa->pa_intrline, IPL_NET,
		gmac_intr, sc, "gmac") == NULL)
	{
		printf(": unable to establish interrupt");
		if (intrstr)
			printf(" at %x", pa->pa_intrline);
		printf("\n");
		return;
	}
#endif 

	sc->gm_dmat = pa->pa_dmat;
	{
	vaddr_t va;
	paddr_t pa;
	error = bus_dmamem_alloc(sc->gm_dmat, GM_BUFSZ,
	    PAGE_SIZE, 0, sc->sc_bufseg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		printf(": cannot allocate buffers (%d)\n", error);
		return;
	}

	error = bus_dmamem_map(sc->gm_dmat, sc->sc_bufseg, nseg,
	    GM_BUFSZ, (caddr_t *)&va, BUS_DMA_NOWAIT);
	if (error) {
		printf(": cannot map buffers (%d)\n", error);
		bus_dmamem_free(sc->gm_dmat, sc->sc_bufseg, 1);
		return;
	}

	error = bus_dmamap_create(sc->gm_dmat, GM_BUFSZ, 1, GM_BUFSZ, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &sc->sc_bufmap);
	if (error) {
		printf(": cannot create buffer dmamap (%d)\n", error);
		bus_dmamem_unmap(sc->gm_dmat, (void *)va, GM_BUFSZ);
		bus_dmamem_free(sc->gm_dmat, sc->sc_bufseg, 1);
		return;
	}
	error = bus_dmamap_load(sc->gm_dmat, sc->sc_bufmap, (void *)va,
	    GM_BUFSZ, NULL, BUS_DMA_NOWAIT);
	if (error) {
		printf(": cannot load buffers dmamap (%d)\n", error);
		bus_dmamap_destroy(sc->gm_dmat, sc->sc_bufmap);
		bus_dmamem_unmap(sc->gm_dmat, (void *)va, GM_BUFSZ);
		bus_dmamem_free(sc->gm_dmat, sc->sc_bufseg, nseg);
		return;
	}

	bzero((void *)va, GM_BUFSZ);
	pa = sc->sc_bufseg[0].ds_addr;

	sc->sc_rxlist = (void *)va;
	sc->sc_rxlist_pa = pa;
	va += 0x800;
	pa += 0x800;
	sc->sc_txlist = (void *)va;
	sc->sc_txlist_pa = pa;;
	va += 0x800;
	pa += 0x800;

	dp = sc->sc_rxlist;
	for (i = 0; i < NRXBUF; i++) {
		sc->sc_rxbuf[i] = (void *)va;
		dp->address = htole32(pa);
		dp->cmd = htole32(GMAC_OWN);
		dp++;
		va += 2048;
		pa += 2048;
	}

	dp = sc->sc_txlist;
	for (i = 0; i < NTXBUF; i++) {
		sc->sc_txbuf[i] = (void *)va;
		dp->address = htole32(pa);
		dp++;
		va += 2048;
		pa += 2048;
	}
	}

#ifdef __OpenBSD__
	bcopy(laddr, sc->sc_enaddr, 6);
#endif /* __OpenBSD__ */

	printf(": %s, address %s\n", intrstr, ether_sprintf(laddr));

	gmac_reset(sc);
	gmac_init_mac(sc);

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_ioctl = gmac_ioctl;
	ifp->if_start = gmac_start;
	ifp->if_watchdog = gmac_watchdog;
	ifp->if_flags =
		IFF_BROADCAST | IFF_SIMPLEX | IFF_NOTRAILERS | IFF_MULTICAST;
	ifp->if_flags |= IFF_ALLMULTI;
	IFQ_SET_READY(&ifp->if_snd);

	mii->mii_ifp = ifp;
	mii->mii_readreg = gmac_mii_readreg;
	mii->mii_writereg = gmac_mii_writereg;
	mii->mii_statchg = gmac_mii_statchg;
	timeout_set(&sc->sc_tmo, gmac_mii_tick, sc);

	ifmedia_init(&mii->mii_media, 0, gmac_mediachange, gmac_mediastatus);
#ifdef __NetBSD__
	mii_attach(self, mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY, 0);
#endif /* __NetBSD__ */
#ifdef __OpenBSD__
	mii_phy_probe(self, &sc->sc_mii, 0xffffffff);
#endif  /* __OpenBSD__ */

	/* Choose a default media. */
	if (LIST_FIRST(&mii->mii_phys) == NULL) {
		ifmedia_add(&mii->mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&mii->mii_media, IFM_ETHER|IFM_AUTO);

	if_attach(ifp);
#ifdef __NetBSD__
	ether_ifattach(ifp, laddr);
#else /* !__NetBSD__ */
	ether_ifattach(ifp);
#endif /* !__NetBSD__ */
}

u_int
gmac_read_reg(sc, reg)
	struct gmac_softc *sc;
	int reg;
{
	return bus_space_read_4(sc->gm_bust, sc->gm_bush, reg);
}

void
gmac_write_reg(sc, reg, val)
	struct gmac_softc *sc;
	int reg;
	u_int val;
{
	bus_space_write_4(sc->gm_bust, sc->gm_bush, reg, val);
}

void
gmac_start_txdma(sc)
	struct gmac_softc *sc;
{
	u_int x;

	x = gmac_read_reg(sc, GMAC_TXDMACONFIG);
	x |= 1;
	gmac_write_reg(sc, GMAC_TXDMACONFIG, x);
	x = gmac_read_reg(sc, GMAC_TXMACCONFIG);
	x |= 1;
	gmac_write_reg(sc, GMAC_TXMACCONFIG, x);
}

void
gmac_start_rxdma(sc)
	struct gmac_softc *sc;
{
	u_int x;

	x = gmac_read_reg(sc, GMAC_RXDMACONFIG);
	x |= 1;
	gmac_write_reg(sc, GMAC_RXDMACONFIG, x);
	x = gmac_read_reg(sc, GMAC_RXMACCONFIG);
	x |= 1;
	gmac_write_reg(sc, GMAC_RXMACCONFIG, x);
}

void
gmac_stop_txdma(sc)
	struct gmac_softc *sc;
{
	u_int x;

	x = gmac_read_reg(sc, GMAC_TXDMACONFIG);
	x &= ~1;
	gmac_write_reg(sc, GMAC_TXDMACONFIG, x);
	x = gmac_read_reg(sc, GMAC_TXMACCONFIG);
	x &= ~1;
	gmac_write_reg(sc, GMAC_TXMACCONFIG, x);
}

void
gmac_stop_rxdma(sc)
	struct gmac_softc *sc;
{
	u_int x;

	x = gmac_read_reg(sc, GMAC_RXDMACONFIG);
	x &= ~1;
	gmac_write_reg(sc, GMAC_RXDMACONFIG, x);
	x = gmac_read_reg(sc, GMAC_RXMACCONFIG);
	x &= ~1;
	gmac_write_reg(sc, GMAC_RXMACCONFIG, x);
}

int
gmac_intr(v)
	void *v;
{
	struct gmac_softc *sc = v;
	u_int status;

	status = gmac_read_reg(sc, GMAC_STATUS) & 0xff;
	if (status == 0) {
		return 0;
	}

	if (status & GMAC_INT_RXDONE)
		gmac_rint(sc);

	if (status & GMAC_INT_TXEMPTY)
		gmac_tint(sc);

	return 1;
}

void
gmac_tint(sc)
	struct gmac_softc *sc;
{
	struct ifnet *ifp = &sc->sc_if;

	ifp->if_flags &= ~IFF_OACTIVE;
	ifp->if_timer = 0;
	gmac_start(ifp);
}

void
gmac_rint(sc)
	struct gmac_softc *sc;
{
	struct ifnet *ifp = &sc->sc_if;
	volatile struct gmac_dma *dp;
	struct mbuf *m;
	int i, j, len;
	u_int cmd;

	for (i = sc->sc_rxlast;; i++) {
		if (i == NRXBUF)
			i = 0;

		dp = &sc->sc_rxlist[i];
		cmd = letoh32(dp->cmd);
		if (cmd & GMAC_OWN)
			break;
		len = (cmd >> 16) & GMAC_LEN_MASK;
		len -= 4;	/* CRC */

		if (dp->cmd_hi & htole32(0x40000000)) {
			ifp->if_ierrors++;
			goto next;
		}

		m = gmac_get(sc, sc->sc_rxbuf[i], len);
		if (m == NULL) {
			ifp->if_ierrors++;
			goto next;
		}

#if NBPFILTER > 0
		/*
		 * Check if there's a BPF listener on this interface.
		 * If so, hand off the raw packet to BPF.
		 */
		if (ifp->if_bpf)
			bpf_tap(ifp->if_bpf, sc->sc_rxbuf[i], len);
#endif
#ifdef __OpenBSD__
		ether_input_mbuf(ifp, m);
#else /* !__OpenBSD__ */
		(*ifp->if_input)(ifp, m);
#endif /* !__OpenBSD__ */
		ifp->if_ipackets++;

next:
		dp->cmd_hi = 0;
		__asm __volatile ("sync");
		dp->cmd = htole32(GMAC_OWN);
	}
	sc->sc_rxlast = i;

	/* XXX Make sure free buffers have GMAC_OWN. */
	i++;
	for (j = 1; j < NRXBUF; j++) {
		if (i == NRXBUF)
			i = 0;
		dp = &sc->sc_rxlist[i++];
		dp->cmd = htole32(GMAC_OWN);
	}
}

struct mbuf *
gmac_get(sc, pkt, totlen)
	struct gmac_softc *sc;
	caddr_t pkt;
	int totlen;
{
	struct mbuf *m;
	struct mbuf *top, **mp;
	int len;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == 0)
		return 0;
	m->m_pkthdr.rcvif = &sc->sc_if;
	m->m_pkthdr.len = totlen;
	len = MHLEN;
	top = 0;
	mp = &top;

	while (totlen > 0) {
		if (top) {
			MGET(m, M_DONTWAIT, MT_DATA);
			if (m == 0) {
				m_freem(top);
				return 0;
			}
			len = MLEN;
		}
		if (totlen >= MINCLSIZE) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_free(m);
				m_freem(top);
				return 0;
			}
			len = MCLBYTES;
		}
		m->m_len = len = min(totlen, len);
		bcopy(pkt, mtod(m, caddr_t), len);
		pkt += len;
		totlen -= len;
		*mp = m;
		mp = &m->m_next;
	}

	return top;
}

void
gmac_start(ifp)
	struct ifnet *ifp;
{
	struct gmac_softc *sc = ifp->if_softc;
	struct mbuf *m;
	caddr_t buff;
	int i, tlen;
	volatile struct gmac_dma *dp;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;

	for (;;) {
		if (ifp->if_flags & IFF_OACTIVE)
			break;

		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == 0)
			break;

		/* 5 seconds to watch for failing to transmit */
		ifp->if_timer = 5;
		ifp->if_opackets++;		/* # of pkts */

		i = sc->sc_txnext;
		buff = sc->sc_txbuf[i];
		tlen = gmac_put(sc, buff, m);

		dp = &sc->sc_txlist[i];
		dp->cmd_hi = 0;
		dp->address_hi = 0;
		dp->cmd = htole32(tlen | GMAC_OWN | GMAC_SOP);

		i++;
		if (i == NTXBUF)
			i = 0;
		__asm __volatile ("sync");

		gmac_write_reg(sc, GMAC_TXDMAKICK, i);
		sc->sc_txnext = i;

#if NBPFILTER > 0
		/*
		 * If BPF is listening on this interface, let it see the
		 * packet before we commit it to the wire.
		 */
		if (ifp->if_bpf)
			bpf_tap(ifp->if_bpf, buff, tlen);
#endif
		i++;
		if (i == NTXBUF)
			i = 0;
		if (i == gmac_read_reg(sc, GMAC_TXDMACOMPLETE)) {
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}
	}
}

int
gmac_put(sc, buff, m)
	struct gmac_softc *sc;
	caddr_t buff;
	struct mbuf *m;
{
	struct mbuf *n;
	int len, tlen = 0;

	for (; m; m = n) {
		len = m->m_len;
		if (len == 0) {
			MFREE(m, n);
			continue;
		}
		bcopy(mtod(m, caddr_t), buff, len);
		buff += len;
		tlen += len;
		MFREE(m, n);
	}
	if (tlen > 2048)
		panic("%s: gmac_put packet overflow", sc->sc_dev.dv_xname);

	return tlen;
}

void
gmac_reset(sc)
	struct gmac_softc *sc;
{
	int i, s;

	s = splnet();

	gmac_stop_txdma(sc);
	gmac_stop_rxdma(sc);

	gmac_write_reg(sc, GMAC_SOFTWARERESET, 3);
	for (i = 10; i > 0; i--) {
		delay(300000);				/* XXX long delay */
		if ((gmac_read_reg(sc, GMAC_SOFTWARERESET) & 3) == 0)
			break;
	}
	if (i == 0)
		printf("%s: reset timeout\n", sc->sc_dev.dv_xname);

	sc->sc_txnext = 0;
	sc->sc_rxlast = 0;
	for (i = 0; i < NRXBUF; i++)
		sc->sc_rxlist[i].cmd = htole32(GMAC_OWN);
	__asm __volatile ("sync");

	gmac_write_reg(sc, GMAC_TXDMADESCBASEHI, 0);
	gmac_write_reg(sc, GMAC_TXDMADESCBASELO, sc->sc_txlist_pa);
	gmac_write_reg(sc, GMAC_RXDMADESCBASEHI, 0);
	gmac_write_reg(sc, GMAC_RXDMADESCBASELO, sc->sc_rxlist_pa);
	gmac_write_reg(sc, GMAC_RXDMAKICK, NRXBUF);

	splx(s);
}

void
gmac_stop(sc)
	struct gmac_softc *sc;
{
	struct ifnet *ifp = &sc->sc_if;
	int s;

	s = splnet();

	timeout_del(&sc->sc_tmo);
#ifndef __OenBSD__
	mii_down(&sc->sc_mii);
#endif

	gmac_stop_txdma(sc);
	gmac_stop_rxdma(sc);

	gmac_write_reg(sc, GMAC_INTMASK, 0xffffffff);

	ifp->if_flags &= ~(IFF_UP | IFF_RUNNING);
	ifp->if_timer = 0;

	splx(s);
}

void
gmac_init_mac(sc)
	struct gmac_softc *sc;
{
	int i, tb;
#ifdef __NetBSD__
	u_int8_t *laddr = sc->sc_laddr;
#else /* !__NetBSD__ */
	u_int8_t *laddr = sc->sc_enaddr;
#endif

	__asm ("mftb %0" : "=r"(tb));
	gmac_write_reg(sc, GMAC_RANDOMSEED, tb);

	/* init-mii */
	gmac_write_reg(sc, GMAC_DATAPATHMODE, 4);
	gmac_mii_writereg(&sc->sc_dev, 0, 0, 0x1000);

	gmac_write_reg(sc, GMAC_TXDMACONFIG, 0xffc00);
	gmac_write_reg(sc, GMAC_RXDMACONFIG, 0);
	gmac_write_reg(sc, GMAC_MACPAUSE, 0x1bf0);
	gmac_write_reg(sc, GMAC_INTERPACKETGAP0, 0);
	gmac_write_reg(sc, GMAC_INTERPACKETGAP1, 8);
	gmac_write_reg(sc, GMAC_INTERPACKETGAP2, 4);
	gmac_write_reg(sc, GMAC_MINFRAMESIZE, ETHER_MIN_LEN);
	gmac_write_reg(sc, GMAC_MAXFRAMESIZE, ETHER_MAX_LEN);
	gmac_write_reg(sc, GMAC_PASIZE, 7);
	gmac_write_reg(sc, GMAC_JAMSIZE, 4);
	gmac_write_reg(sc, GMAC_ATTEMPTLIMIT,0x10);
	gmac_write_reg(sc, GMAC_MACCNTLTYPE, 0x8808);

	gmac_write_reg(sc, GMAC_MACADDRESS0, (laddr[4] << 8) | laddr[5]);
	gmac_write_reg(sc, GMAC_MACADDRESS1, (laddr[2] << 8) | laddr[3]);
	gmac_write_reg(sc, GMAC_MACADDRESS2, (laddr[0] << 8) | laddr[1]);
	gmac_write_reg(sc, GMAC_MACADDRESS3, 0);
	gmac_write_reg(sc, GMAC_MACADDRESS4, 0);
	gmac_write_reg(sc, GMAC_MACADDRESS5, 0);
	gmac_write_reg(sc, GMAC_MACADDRESS6, 1);
	gmac_write_reg(sc, GMAC_MACADDRESS7, 0xc200);
	gmac_write_reg(sc, GMAC_MACADDRESS8, 0x0180);
	gmac_write_reg(sc, GMAC_MACADDRFILT0, 0);
	gmac_write_reg(sc, GMAC_MACADDRFILT1, 0);
	gmac_write_reg(sc, GMAC_MACADDRFILT2, 0);
	gmac_write_reg(sc, GMAC_MACADDRFILT2_1MASK, 0);
	gmac_write_reg(sc, GMAC_MACADDRFILT0MASK, 0);

	for (i = 0; i < 0x6c; i+= 4)
		gmac_write_reg(sc, GMAC_HASHTABLE0 + i, 0);

	gmac_write_reg(sc, GMAC_SLOTTIME, 0x40);

	/* XXX */
	gmac_write_reg(sc, GMAC_TXMACCONFIG, 0);
	gmac_write_reg(sc, GMAC_XIFCONFIG, 5);
	gmac_write_reg(sc, GMAC_MACCTRLCONFIG, 0);
	if (IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_FDX) {
		gmac_write_reg(sc, GMAC_TXMACCONFIG, 6);
		gmac_write_reg(sc, GMAC_XIFCONFIG, 1);
	} else {
		gmac_write_reg(sc, GMAC_TXMACCONFIG, 0);
		gmac_write_reg(sc, GMAC_XIFCONFIG, 5);
	}
	if (0)	/* g-bit? */ 
		gmac_write_reg(sc, GMAC_MACCTRLCONFIG, 3);
	else
		gmac_write_reg(sc, GMAC_MACCTRLCONFIG, 0);
}

void
gmac_setladrf(sc)
	struct gmac_softc *sc;
{
	struct ifnet *ifp = &sc->sc_if;
	struct ether_multi *enm;
	struct ether_multistep step;
#if defined(__OpenBSD__)
	struct arpcom *ec = &sc->arpcom;
#else
	struct ethercom *ec = &sc->sc_ethercom;
#endif
	u_int32_t crc;
	u_int32_t hash[16];
	u_int v;
	int i;

	/* Clear hash table */
	for (i = 0; i < 16; i++)
		hash[i] = 0;

	/* Get current RX configuration */
	v = gmac_read_reg(sc, GMAC_RXMACCONFIG);

	if ((ifp->if_flags & IFF_PROMISC) != 0) {
		/* Turn on promiscuous mode; turn off the hash filter */
		v |= GMAC_RXMAC_PR;
		v &= ~GMAC_RXMAC_HEN;
		ifp->if_flags |= IFF_ALLMULTI;
		goto chipit;
	}

	/* Turn off promiscuous mode; turn on the hash filter */
	v &= ~GMAC_RXMAC_PR;
	v |= GMAC_RXMAC_HEN;

	/*
	 * Set up multicast address filter by passing all multicast addresses
	 * through a crc generator, and then using the high order 8 bits as an
	 * index into the 256 bit logical address filter.  The high order bit
	 * selects the word, while the rest of the bits select the bit within
	 * the word.
	 */

	ETHER_FIRST_MULTI(step, ec, enm);
	while (enm != NULL) {
		if (bcmp(enm->enm_addrlo, enm->enm_addrhi, 6)) {
			/*
			 * We must listen to a range of multicast addresses.
			 * For now, just accept all multicasts, rather than
			 * trying to set only those filter bits needed to match
			 * the range.  (At this time, the only use of address
			 * ranges is for IP multicast routing, for which the
			 * range is big enough to require all bits set.)
			 */
			for (i = 0; i < 16; i++)
				hash[i] = 0xffff;
			ifp->if_flags |= IFF_ALLMULTI;
			goto chipit;
		}

		crc = ether_crc32_le(enm->enm_addrlo, ETHER_ADDR_LEN);

		/* Just want the 8 most significant bits. */
		crc >>= 24;

		/* Set the corresponding bit in the filter. */
		hash[crc >> 4] |= 1 << (crc & 0xf);

		ETHER_NEXT_MULTI(step, enm);
	}

	ifp->if_flags &= ~IFF_ALLMULTI;

chipit:
	/* Now load the hash table into the chip */
	for (i = 0; i < 16; i++)
		gmac_write_reg(sc, GMAC_HASHTABLE0 + i * 4, hash[i]);

	gmac_write_reg(sc, GMAC_RXMACCONFIG, v);
}

void
gmac_init(sc)
	struct gmac_softc *sc;
{
	struct ifnet *ifp = &sc->sc_if;
	u_int x;

	gmac_stop_txdma(sc);
	gmac_stop_rxdma(sc);

	gmac_init_mac(sc);
	gmac_setladrf(sc);

	x = gmac_read_reg(sc, GMAC_RXMACCONFIG);
	if (ifp->if_flags & IFF_PROMISC)
		x |= GMAC_RXMAC_PR;
	else
		x &= ~GMAC_RXMAC_PR;
	gmac_write_reg(sc, GMAC_RXMACCONFIG, x);

	gmac_start_txdma(sc);
	gmac_start_rxdma(sc);

	gmac_write_reg(sc, GMAC_INTMASK, ~(GMAC_INT_TXEMPTY | GMAC_INT_RXDONE));

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;
	ifp->if_timer = 0;

	timeout_del(&sc->sc_tmo);
	timeout_add(&sc->sc_tmo, 1);

	gmac_start(ifp);
}

int
gmac_ioctl(ifp, cmd, data)
	struct ifnet *ifp;
	u_long cmd;
	caddr_t data;
{
	struct gmac_softc *sc = ifp->if_softc;
	struct ifaddr *ifa = (struct ifaddr *)data;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {

	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;

		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			gmac_init(sc);
#ifdef __OpenBSD__
			arp_ifinit(&sc->arpcom, ifa);
#else /* !__OpenBSD__ */
			arp_ifinit(ifp, ifa);
#endif /* !__OpenBSD__ */
			break;
#endif
#ifdef NS
		case AF_NS:
		    {
			struct ns_addr *ina = &IA_SNS(ifa)->sns_addr;

			if (ns_nullhost(*ina))
				ina->x_host =
				    *(union ns_host *)LLADDR(ifp->if_sadl);
			else {
				bcopy(ina->x_host.c_host,
				    LLADDR(ifp->if_sadl),
				    sizeof(sc->sc_enaddr));
			}
			/* Set new address. */
			gmac_init(sc);
			break;
		    }
#endif
		default:
			gmac_init(sc);
			break;
		}
		break;

	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_flags & IFF_RUNNING) != 0) {
			/*
			 * If interface is marked down and it is running, then
			 * stop it.
			 */
			gmac_stop(sc);
			ifp->if_flags &= ~IFF_RUNNING;
		} else if ((ifp->if_flags & IFF_UP) != 0 &&
		    (ifp->if_flags & IFF_RUNNING) == 0) {
			/*
			 * If interface is marked up and it is stopped, then
			 * start it.
			 */
			gmac_init(sc);
		} else {
			/*
			 * Reset the interface to pick up changes in any other
			 * flags that affect hardware registers.
			 */
			gmac_reset(sc);
			gmac_init(sc);
		}
#ifdef GMAC_DEBUG
		if (ifp->if_flags & IFF_DEBUG)
			sc->sc_flags |= GMAC_DEBUGFLAG;
#endif
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
#if defined(__OpenBSD__)
		error = (cmd == SIOCADDMULTI) ?
		    ether_addmulti(ifr, &sc->arpcom) :
		    ether_delmulti(ifr, &sc->arpcom);
#else
		error = (cmd == SIOCADDMULTI) ?
		    ether_addmulti(ifr, &sc->sc_ethercom) :
		    ether_delmulti(ifr, &sc->sc_ethercom);
#endif

		if (error == ENETRESET) {
			/*
			 * Multicast list has changed; set the hardware filter
			 * accordingly.
			 */
			gmac_init(sc);
			/* gmac_setladrf(sc); */
			error = 0;
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = EINVAL;
	}

	splx(s);
	return error;
}

void
gmac_watchdog(ifp)
	struct ifnet *ifp;
{
	struct gmac_softc *sc = ifp->if_softc;

	printf("%s: device timeout\n", ifp->if_xname);
	ifp->if_oerrors++;

	gmac_reset(sc);
	gmac_init(sc);
}

int
gmac_mediachange(ifp)
	struct ifnet *ifp;
{
	struct gmac_softc *sc = ifp->if_softc;

	return mii_mediachg(&sc->sc_mii);
}

void
gmac_mediastatus(ifp, ifmr)
	struct ifnet *ifp;
	struct ifmediareq *ifmr;
{
	struct gmac_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);

	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

int
gmac_mii_readreg(dev, phy, reg)
	struct device *dev;
	int phy, reg;
{
	struct gmac_softc *sc = (void *)dev;
	int i;

	gmac_write_reg(sc, GMAC_MIFFRAMEOUTPUT,
		0x60020000 | (phy << 23) | (reg << 18));

	for (i = 1000; i >= 0; i -= 10) {
		if (gmac_read_reg(sc, GMAC_MIFFRAMEOUTPUT) & 0x10000)
			break;
		delay(10);
	}
	if (i < 0) {
		printf("%s: gmac_mii_readreg: timeout\n", sc->sc_dev.dv_xname);
		return 0;
	}

	return gmac_read_reg(sc, GMAC_MIFFRAMEOUTPUT) & 0xffff;
}

void
gmac_mii_writereg(dev, phy, reg, val)
	struct device *dev;
	int phy, reg, val;
{
	struct gmac_softc *sc = (void *)dev;
	int i;

	gmac_write_reg(sc, GMAC_MIFFRAMEOUTPUT,
		0x50020000 | (phy << 23) | (reg << 18) | (val & 0xffff));

	for (i = 1000; i >= 0; i -= 10) {
		if (gmac_read_reg(sc, GMAC_MIFFRAMEOUTPUT) & 0x10000)
			break;
		delay(10);
	}
	if (i < 0)
		printf("%s: gmac_mii_writereg: timeout\n", sc->sc_dev.dv_xname);
}

void
gmac_mii_statchg(dev)
	struct device *dev;
{
	struct gmac_softc *sc = (void *)dev;

	gmac_stop_txdma(sc);
	gmac_stop_rxdma(sc);

	if (IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_FDX) {
		gmac_write_reg(sc, GMAC_TXMACCONFIG, 6);
		gmac_write_reg(sc, GMAC_XIFCONFIG, 1);
	} else {
		gmac_write_reg(sc, GMAC_TXMACCONFIG, 0);
		gmac_write_reg(sc, GMAC_XIFCONFIG, 5);
	}

	if (0)	/* g-bit? */
		gmac_write_reg(sc, GMAC_MACCTRLCONFIG, 3);
	else
		gmac_write_reg(sc, GMAC_MACCTRLCONFIG, 0);

	gmac_start_txdma(sc);
	gmac_start_rxdma(sc);
}

void
gmac_mii_tick(v)
	void *v;
{
	struct gmac_softc *sc = v;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add(&sc->sc_tmo, hz);
}

void
gmac_enable_hack()
{
	u_int32_t *paddr;
	u_int32_t value;

#if 1
	paddr = mapiodev(0xf8000020, 0x30);

	value = *paddr;
	value |= 0x2;
	*paddr = value;

	unmapiodev(paddr,0x30);
#endif

	printf("gmac enabled\n");
}

/* HACK, THIS SHOULD NOT BE IN THIS FILE */
u_int32_t
ether_crc32_le(const u_int8_t *buf, size_t len)
{
        static const u_int32_t crctab[] = {
                0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
                0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
                0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
        };
        u_int32_t crc;
        int i;

        crc = 0xffffffffU;      /* initial value */

        for (i = 0; i < len; i++) {
                crc ^= buf[i];
                crc = (crc >> 4) ^ crctab[crc & 0xf];
                crc = (crc >> 4) ^ crctab[crc & 0xf];
        }

        return (crc);
}
