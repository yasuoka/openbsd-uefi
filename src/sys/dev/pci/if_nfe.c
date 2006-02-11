/*	$OpenBSD: if_nfe.c,v 1.29 2006/02/11 20:25:21 brad Exp $	*/

/*-
 * Copyright (c) 2006 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2005, 2006 Jonathan Gray <jsg@openbsd.org>
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

/* Driver for nvidia nForce Ethernet */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/socket.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#endif

#if NVLAN > 0
#include <net/if_types.h>
#include <net/if_vlan_var.h>
#endif

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_nfereg.h>
#include <dev/pci/if_nfevar.h>

int	nfe_match(struct device *, void *, void *);
void	nfe_attach(struct device *, struct device *, void *);
void	nfe_power(int, void *);
void	nfe_miibus_statchg(struct device *);
int	nfe_miibus_readreg(struct device *, int, int);
void	nfe_miibus_writereg(struct device *, int, int, int);
int	nfe_intr(void *);
int	nfe_ioctl(struct ifnet *, u_long, caddr_t);
void	nfe_txdesc32_sync(struct nfe_softc *, struct nfe_desc32 *, int);
void	nfe_txdesc64_sync(struct nfe_softc *, struct nfe_desc64 *, int);
void	nfe_rxdesc32_sync(struct nfe_softc *, struct nfe_desc32 *, int);
void	nfe_rxdesc64_sync(struct nfe_softc *, struct nfe_desc64 *, int);
void	nfe_rxeof(struct nfe_softc *);
void	nfe_txeof(struct nfe_softc *);
int	nfe_encap(struct nfe_softc *, struct mbuf *);
void	nfe_start(struct ifnet *);
void	nfe_watchdog(struct ifnet *);
int	nfe_init(struct ifnet *);
void	nfe_stop(struct ifnet *, int);
int	nfe_alloc_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
void	nfe_reset_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
void	nfe_free_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
int	nfe_alloc_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
void	nfe_reset_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
void	nfe_free_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
int	nfe_ifmedia_upd(struct ifnet *);
void	nfe_ifmedia_sts(struct ifnet *, struct ifmediareq *);
void	nfe_setmulti(struct nfe_softc *);
void	nfe_get_macaddr(struct nfe_softc *, uint8_t *);
void	nfe_set_macaddr(struct nfe_softc *, const uint8_t *);
void	nfe_tick(void *);

struct cfattach nfe_ca = {
	sizeof (struct nfe_softc), nfe_match, nfe_attach
};

struct cfdriver nfe_cd = {
	NULL, "nfe", DV_IFNET
};

#define NFE_DEBUG

#ifdef NFE_DEBUG
int nfedebug = 1;
#define DPRINTF(x)	do { if (nfedebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (nfedebug >= (n)) printf x; } while (0)
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

const struct pci_matchid nfe_devices[] = {
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE_LAN },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE2_LAN },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN3 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN4 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN5 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_CK804_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_CK804_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP04_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP04_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP51_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP51_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP55_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP55_LAN2 }
};

int
nfe_match(struct device *dev, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, nfe_devices,
	    sizeof (nfe_devices) / sizeof (nfe_devices[0]));
}

void
nfe_attach(struct device *parent, struct device *self, void *aux)
{
	struct nfe_softc *sc = (struct nfe_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr;
	struct ifnet *ifp;
	bus_size_t memsize;

	if (pci_mapreg_map(pa, NFE_PCI_BA, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_memt, &sc->sc_memh, NULL, &memsize, 0) != 0) {
		printf(": can't map mem space\n");
		return;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, nfe_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	sc->sc_dmat = pa->pa_dmat;

	nfe_get_macaddr(sc, sc->sc_arpcom.ac_enaddr);
	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	sc->sc_flags = 0;

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN2:
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN3:
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN4:
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN5:
		sc->sc_flags |= NFE_JUMBO_SUP | NFE_HW_CSUM;
		break;
	case PCI_PRODUCT_NVIDIA_MCP51_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP51_LAN2:
		sc->sc_flags |= NFE_40BIT_ADDR;
		break;
	case PCI_PRODUCT_NVIDIA_CK804_LAN1:
	case PCI_PRODUCT_NVIDIA_CK804_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP04_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP04_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP55_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP55_LAN2:
		sc->sc_flags |= NFE_JUMBO_SUP | NFE_40BIT_ADDR | NFE_HW_CSUM;
		break;
	}

	/*
	 * Allocate Tx and Rx rings.
	 */
	if (nfe_alloc_tx_ring(sc, &sc->txq) != 0) {
		printf("%s: could not allocate Tx ring\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	if (nfe_alloc_rx_ring(sc, &sc->rxq) != 0) {
		printf("%s: could not allocate Rx ring\n",
		    sc->sc_dev.dv_xname);
		nfe_free_tx_ring(sc, &sc->txq);
		return;
	}

	ifp = &sc->sc_arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = nfe_ioctl;
	ifp->if_start = nfe_start;
	ifp->if_watchdog = nfe_watchdog;
	ifp->if_init = nfe_init;
	ifp->if_baudrate = IF_Gbps(1);
	IFQ_SET_MAXLEN(&ifp->if_snd, NFE_IFQ_MAXLEN);
	IFQ_SET_READY(&ifp->if_snd);
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#ifdef NFE_CSUM
	if (sc->sc_flags & NFE_HW_CSUM) {
		ifp->if_capabilities |= IFCAP_CSUM_IPv4 | IFCAP_CSUM_TCPv4 |
		    IFCAP_CSUM_UDPv4;
	}
#endif

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = nfe_miibus_readreg;
	sc->sc_mii.mii_writereg = nfe_miibus_writereg;
	sc->sc_mii.mii_statchg = nfe_miibus_statchg;

	ifmedia_init(&sc->sc_mii.mii_media, 0, nfe_ifmedia_upd,
	    nfe_ifmedia_sts);
	mii_attach(self, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER | IFM_MANUAL,
		    0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->sc_tick_ch, nfe_tick, sc);

	sc->sc_powerhook = powerhook_establish(nfe_power, sc);
}

void
nfe_power(int why, void *arg)
{
	struct nfe_softc *sc = arg;
	struct ifnet *ifp;

	if (why == PWR_RESUME) {
		ifp = &sc->sc_arpcom.ac_if;
		if (ifp->if_flags & IFF_UP) {
			ifp->if_flags &= ~IFF_RUNNING;
			nfe_init(ifp);
			if (ifp->if_flags & IFF_RUNNING)
				nfe_start(ifp);
		}
	}
}

void
nfe_miibus_statchg(struct device *dev)
{
	struct nfe_softc *sc = (struct nfe_softc *)dev;
	struct mii_data *mii = &sc->sc_mii;
	uint32_t phy, seed, misc = NFE_MISC1_MAGIC, link = NFE_MEDIA_SET;

	phy = NFE_READ(sc, NFE_PHY_IFACE);
	phy &= ~(NFE_PHY_HDX | NFE_PHY_100TX | NFE_PHY_1000T);

	seed = NFE_READ(sc, NFE_RNDSEED);
	seed &= ~NFE_SEED_MASK;

	if ((mii->mii_media_active & IFM_GMASK) == IFM_HDX) {
		phy  |= NFE_PHY_HDX;	/* half-duplex */
		misc |= NFE_MISC1_HDX;
	}

	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_1000_T:	/* full-duplex only */
		link |= NFE_MEDIA_1000T;
		seed |= NFE_SEED_1000T;
		phy  |= NFE_PHY_1000T;
		break;
	case IFM_100_TX:
		link |= NFE_MEDIA_100TX;
		seed |= NFE_SEED_100TX;
		phy  |= NFE_PHY_100TX;
		break;
	case IFM_10_T:
		link |= NFE_MEDIA_10T;
		seed |= NFE_SEED_10T;
		break;
	}

	NFE_WRITE(sc, NFE_RNDSEED, seed);	/* XXX: gigabit NICs only? */

	NFE_WRITE(sc, NFE_PHY_IFACE, phy);
	NFE_WRITE(sc, NFE_MISC1, misc);
	NFE_WRITE(sc, NFE_LINKSPEED, link);
}

int
nfe_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct nfe_softc *sc = (struct nfe_softc *)dev;
	uint32_t val;
	int ntries;

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	if (NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY) {
		NFE_WRITE(sc, NFE_PHY_CTL, NFE_PHY_BUSY);
		DELAY(100);
	}

	NFE_WRITE(sc, NFE_PHY_CTL, (phy << NFE_PHYADD_SHIFT) | reg);

	for (ntries = 0; ntries < 1000; ntries++) {
		DELAY(100);
		if (!(NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY))
			break;
	}
	if (ntries == 1000) {
		DPRINTFN(2, ("timeout waiting for PHY\n"));
		return 0;
	}

	if (NFE_READ(sc, NFE_PHY_STATUS) & NFE_PHY_ERROR) {
		DPRINTFN(2, ("could not read PHY\n"));
		return 0;
	}

	val = NFE_READ(sc, NFE_PHY_DATA);
	if (val != 0xffffffff && val != 0)
		sc->phyaddr = phy;

	DPRINTFN(2, ("mii read phy %d reg 0x%x ret 0x%x\n", phy, reg, val));

	return val;
}

void
nfe_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct nfe_softc *sc = (struct nfe_softc *)dev;
	uint32_t ctl;
	int ntries;

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	if (NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY) {
		NFE_WRITE(sc, NFE_PHY_CTL, NFE_PHY_BUSY);
		DELAY(100);
	}

	NFE_WRITE(sc, NFE_PHY_DATA, val);
	ctl = NFE_PHY_WRITE | (phy << NFE_PHYADD_SHIFT) | reg;
	NFE_WRITE(sc, NFE_PHY_CTL, ctl);

	for (ntries = 0; ntries < 1000; ntries++) {
		DELAY(100);
		if (!(NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY))
			break;
	}
#ifdef NFE_DEBUG
	if (nfedebug >= 2 && ntries == 1000)
		printf("could not write to PHY\n");
#endif
}

int
nfe_intr(void *arg)
{
	struct nfe_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t r;

	/* disable interrupts */
	NFE_WRITE(sc, NFE_IRQ_MASK, 0);

	r = NFE_READ(sc, NFE_IRQ_STATUS);
	NFE_WRITE(sc, NFE_IRQ_STATUS, r);

	DPRINTFN(5, ("nfe_intr: interrupt register %x\n", r));

	if (r == 0) {
		/* re-enable interrupts */
		NFE_WRITE(sc, NFE_IRQ_MASK, NFE_IRQ_WANTED);
		return 0;
	}

	if (r & NFE_IRQ_LINK) {
		NFE_READ(sc, NFE_PHY_STATUS);
		NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);
		DPRINTF(("link state changed\n"));
	}

	if (ifp->if_flags & IFF_RUNNING) {
		/* check Rx ring */
		nfe_rxeof(sc);

		/* check Tx ring */
		nfe_txeof(sc);
	}

	/* re-enable interrupts */
	NFE_WRITE(sc, NFE_IRQ_MASK, NFE_IRQ_WANTED);

	return 1;
}

int
nfe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct ifaddr *ifa = (struct ifaddr *)data;
	int s, error = 0;

	s = splnet();

	if ((error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data)) > 0) {
		splx(s);
		return error;
	}

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		nfe_init(ifp);
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			arp_ifinit(&sc->sc_arpcom, ifa);
			break;
#endif
		default:
			break;
		}
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > ETHERMTU)
			error = EINVAL;
		else if (ifp->if_mtu != ifr->ifr_mtu)
			ifp->if_mtu = ifr->ifr_mtu;
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			/*
			 * If only the PROMISC or ALLMULTI flag changes, then
			 * don't do a full re-init of the chip, just update
			 * the Rx filter.
			 */
			if ((ifp->if_flags & IFF_RUNNING) &&
			    ((ifp->if_flags ^ sc->sc_if_flags) &
			     (IFF_ALLMULTI | IFF_PROMISC)) != 0)
				nfe_setmulti(sc);
			else
				nfe_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				nfe_stop(ifp, 1);
		}
		sc->sc_if_flags = ifp->if_flags;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = (cmd == SIOCADDMULTI) ?
		    ether_addmulti(ifr, &sc->sc_arpcom) :
		    ether_delmulti(ifr, &sc->sc_arpcom);

		if (error == ENETRESET) {
			if (ifp->if_flags & IFF_RUNNING)
				nfe_setmulti(sc);
			error = 0;
		}
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;
	default:
		error = EINVAL;
	}

	splx(s);

	return error;
}

void
nfe_txdesc32_sync(struct nfe_softc *sc, struct nfe_desc32 *desc32, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
	    (caddr_t)desc32 - (caddr_t)sc->txq.desc32,
	    sizeof (struct nfe_desc32), ops);
}

void
nfe_txdesc64_sync(struct nfe_softc *sc, struct nfe_desc64 *desc64, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
	    (caddr_t)desc64 - (caddr_t)sc->txq.desc64,
	    sizeof (struct nfe_desc64), ops);
}

void
nfe_rxdesc32_sync(struct nfe_softc *sc, struct nfe_desc32 *desc32, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
	    (caddr_t)desc32 - (caddr_t)sc->rxq.desc32,
	    sizeof (struct nfe_desc32), ops);
}

void
nfe_rxdesc64_sync(struct nfe_softc *sc, struct nfe_desc64 *desc64, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
	    (caddr_t)desc64 - (caddr_t)sc->rxq.desc64,
	    sizeof (struct nfe_desc64), ops);
}

void
nfe_rxeof(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_rx_data *data;
	struct mbuf *m, *mnew;
	uint16_t flags;
	int error, len;

	for (;;) {
		data = &sc->rxq.data[sc->rxq.cur];

		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->rxq.desc64[sc->rxq.cur];
			nfe_rxdesc64_sync(sc, desc64, BUS_DMASYNC_POSTREAD);

			flags = letoh16(desc64->flags);
			len = letoh16(desc64->length) & 0x3fff;
		} else {
			desc32 = &sc->rxq.desc32[sc->rxq.cur];
			nfe_rxdesc32_sync(sc, desc32, BUS_DMASYNC_POSTREAD);

			flags = letoh16(desc32->flags);
			len = letoh16(desc32->length) & 0x3fff;
		}

		if (flags & NFE_RX_READY)
			break;

		if ((sc->sc_flags & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
			if (!(flags & NFE_RX_VALID_V1))
				goto skip;

			if ((flags & NFE_RX_FIXME_V1) == NFE_RX_FIXME_V1) {
				flags &= ~NFE_RX_ERROR;
				len--;	/* fix buffer length */
			}
		} else {
			if (!(flags & NFE_RX_VALID_V2))
				goto skip;

			if ((flags & NFE_RX_FIXME_V2) == NFE_RX_FIXME_V2) {
				flags &= ~NFE_RX_ERROR;
				len--;	/* fix buffer length */
			}
		}

		if (flags & NFE_RX_ERROR) {
			ifp->if_ierrors++;
			goto skip;
		}

		/*
		 * Try to allocate a new mbuf for this ring element and load
		 * it before processing the current mbuf. If the ring element
		 * cannot be loaded, drop the received packet and reuse the
		 * old mbuf. In the unlikely case that the old mbuf can't be
		 * reloaded either, explicitly panic.
		 */
		MGETHDR(mnew, M_DONTWAIT, MT_DATA);
		if (mnew == NULL) {
			ifp->if_ierrors++;
			goto skip;
		}

		MCLGET(mnew, M_DONTWAIT);
		if (!(mnew->m_flags & M_EXT)) {
			m_freem(mnew);
			ifp->if_ierrors++;
			goto skip;
		}

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, data->map);

		error = bus_dmamap_load(sc->sc_dmat, data->map,
		    mtod(mnew, void *), MCLBYTES, NULL, BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(mnew);

			/* try to reload the old mbuf */
			error = bus_dmamap_load(sc->sc_dmat, data->map,
			    mtod(data->m, void *), MCLBYTES, NULL,
			    BUS_DMA_NOWAIT);
			if (error != 0) {
				/* very unlikely that it will fail... */
				panic("%s: could not load old rx mbuf",
				    sc->sc_dev.dv_xname);
			}
			ifp->if_ierrors++;
			goto skip;
		}

		/*
		 * New mbuf successfully loaded, update Rx ring and continue
		 * processing.
		 */
		m = data->m;
		data->m = mnew;

		/* finalize mbuf */
		m->m_pkthdr.len = m->m_len = len;
		m->m_pkthdr.rcvif = ifp;

#ifdef notyet
		if (sc->sc_flags & NFE_HW_CSUM) {
			if (flags & NFE_RX_IP_CSUMOK)
				m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;
			if (flags & NFE_RX_UDP_CSUMOK)
				m->m_pkthdr.csum_flags |= M_UDP_CSUM_IN_OK;
			if (flags & NFE_RX_TCP_CSUMOK)
				m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK;
		}
#elif defined(NFE_CSUM)
		if ((sc->sc_flags & NFE_HW_CSUM) && (flags & NFE_RX_CSUMOK))
			m->m_pkthdr.csum_flags = M_IPV4_CSUM_IN_OK;
#endif

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m);
#endif
		ifp->if_ipackets++;
		ether_input_mbuf(ifp, m);

skip:		if (sc->sc_flags & NFE_40BIT_ADDR) {
#if defined(__LP64__)
			desc64->physaddr[0] =
			    htole32(data->map->dm_segs->ds_addr >> 32);
#endif
			desc64->physaddr[1] =
			    htole32(data->map->dm_segs->ds_addr & 0xffffffff);
			desc64->flags = htole16(NFE_RX_READY);
			desc64->length = htole16(MCLBYTES);

			nfe_rxdesc64_sync(sc, desc64, BUS_DMASYNC_PREWRITE);
		} else {
			desc32->physaddr =
			    htole32(data->map->dm_segs->ds_addr);
			desc32->flags = htole16(NFE_RX_READY);
			desc32->length = htole16(MCLBYTES);

			nfe_rxdesc32_sync(sc, desc32, BUS_DMASYNC_PREWRITE);
		}

		sc->rxq.cur = (sc->rxq.cur + 1) % NFE_RX_RING_COUNT;
	}
}

void
nfe_txeof(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_tx_data *data;
	uint16_t flags;

	while (sc->txq.next != sc->txq.cur) {
		data = &sc->txq.data[sc->txq.next];

		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->txq.desc64[sc->txq.next];
			nfe_txdesc64_sync(sc, desc64, BUS_DMASYNC_POSTREAD);

			flags = letoh16(desc64->flags);
		} else {
			desc32 = &sc->txq.desc32[sc->txq.next];
			nfe_txdesc32_sync(sc, desc32, BUS_DMASYNC_POSTREAD);

			flags = letoh16(desc32->flags);
		}

		if (flags & NFE_TX_VALID)
			break;

		if ((sc->sc_flags & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
			if (!(flags & NFE_TX_LASTFRAG_V1))
				goto skip;

			if ((flags & NFE_TX_ERROR_V1) != 0) {
				DPRINTF(("tx error 0x%04x\n", flags));
				ifp->if_oerrors++;
			} else
				ifp->if_opackets++;
		} else {
			if (!(flags & NFE_TX_LASTFRAG_V2))
				goto skip;

			if ((flags & NFE_TX_ERROR_V2) != 0) {
				DPRINTF(("tx error 0x%04x\n", flags));
				ifp->if_oerrors++;
			} else
				ifp->if_opackets++;
		}

		if (data->m == NULL) {	/* should not get there */
			DPRINTF(("last fragment bit w/o associated mbuf!\n"));
			goto skip;
		}

		/* last fragment of the mbuf chain transmitted */
		bus_dmamap_sync(sc->sc_dmat, data->active, 0,
		    data->active->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, data->active);
		m_freem(data->m);
		data->m = NULL;

skip:		sc->txq.queued--;
		sc->txq.next = (sc->txq.next + 1) % NFE_TX_RING_COUNT;
	}

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_OACTIVE;
	nfe_start(ifp);
}

int
nfe_encap(struct nfe_softc *sc, struct mbuf *m0)
{
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_tx_data *data;
	struct mbuf *mnew;
	bus_dmamap_t map;
	uint16_t flags = NFE_TX_VALID;
	int error, i;

	map = sc->txq.data[sc->txq.cur].map;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m0, BUS_DMA_NOWAIT);
	if (error != 0 && error != EFBIG) {
		printf("%s: could not map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		return error;
	}
	if (error != 0) {
		/* too many fragments, linearize */

		MGETHDR(mnew, M_DONTWAIT, MT_DATA);
		if (mnew == NULL)
			return ENOBUFS;

		M_DUP_PKTHDR(mnew, m0);
		if (m0->m_pkthdr.len > MHLEN) {
			MCLGET(mnew, M_DONTWAIT);
			if (!(mnew->m_flags & M_EXT)) {
				m_freem(mnew);
				return ENOBUFS;
			}
		}

		m_copydata(m0, 0, m0->m_pkthdr.len, mtod(mnew, caddr_t));
		m_freem(m0);
		mnew->m_len = mnew->m_pkthdr.len;
		m0 = mnew;

		error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m0,
		    BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: could not map mbuf (error %d)\n",
			    sc->sc_dev.dv_xname, error);
			m_freem(m0);
			return error;
		}
	}

	if (sc->txq.queued + map->dm_nsegs >= NFE_TX_RING_COUNT - 1) {
		bus_dmamap_unload(sc->sc_dmat, map);
		return ENOBUFS;
	}

#ifdef NFE_CSUM
	if (m0->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
		flags |= NFE_TX_IP_CSUM;
	if (m0->m_pkthdr.csum_flags & (M_TCPV4_CSUM_OUT | M_UDPV4_CSUM_OUT))
		flags |= NFE_TX_TCP_CSUM;
#endif

	for (i = 0; i < map->dm_nsegs; i++) {
		data = &sc->txq.data[sc->txq.cur];

		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->txq.desc64[sc->txq.cur];
#if defined(__LP64__)
			desc64->physaddr[0] =
			    htole32(map->dm_segs[i].ds_addr >> 32);
#endif
			desc64->physaddr[1] =
			    htole32(map->dm_segs[i].ds_addr & 0xffffffff);
			desc64->length = htole16(map->dm_segs[i].ds_len - 1);
			desc64->flags = htole16(flags);

			nfe_txdesc64_sync(sc, desc64, BUS_DMASYNC_PREWRITE);
		} else {
			desc32 = &sc->txq.desc32[sc->txq.cur];

			desc32->physaddr = htole32(map->dm_segs[i].ds_addr);
			desc32->length = htole16(map->dm_segs[i].ds_len - 1);
			desc32->flags = htole16(flags);

			nfe_txdesc32_sync(sc, desc32, BUS_DMASYNC_PREWRITE);
		}

		/* csum flags belong to the first fragment only */
		if (map->dm_nsegs > 1)
			flags &= ~(NFE_TX_IP_CSUM | NFE_TX_TCP_CSUM);

		sc->txq.queued++;
		sc->txq.cur = (sc->txq.cur + 1) % NFE_TX_RING_COUNT;
	}

	/* the whole mbuf chain has been DMA mapped, fix last descriptor */
	if (sc->sc_flags & NFE_40BIT_ADDR) {
		flags |= NFE_TX_LASTFRAG_V2;

		desc64->flags = htole16(flags);
		nfe_txdesc64_sync(sc, desc64, BUS_DMASYNC_PREWRITE);
	} else {
		if (sc->sc_flags & NFE_JUMBO_SUP)
			flags |= NFE_TX_LASTFRAG_V2;
		else
			flags |= NFE_TX_LASTFRAG_V1;

		desc32->flags = htole16(flags);
		nfe_txdesc32_sync(sc, desc32, BUS_DMASYNC_PREWRITE);
	}

	data->m = m0;
	data->active = map;

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	return 0;
}

void
nfe_start(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct mbuf *m0;
	uint32_t txctl;
	int pkts = 0;

	for (;;) {
		IFQ_POLL(&ifp->if_snd, m0);
		if (m0 == NULL)
			break;

		if (nfe_encap(sc, m0) != 0) {
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}

		/* packet put in h/w queue, remove from s/w queue */
		IFQ_DEQUEUE(&ifp->if_snd, m0);
		pkts++;

#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m0);
#endif
	}
	if (pkts == 0)
		return;

	txctl = NFE_RXTX_KICKTX;
	if (sc->sc_flags & NFE_40BIT_ADDR)
		txctl |= NFE_RXTX_V3MAGIC;
	else if (sc->sc_flags & NFE_JUMBO_SUP)
		txctl |= NFE_RXTX_V2MAGIC;
#ifdef NFE_CSUM
	if (sc->sc_flags & NFE_HW_CSUM)
		txctl |= NFE_RXTX_RXCHECK;
#endif

	/* kick Tx */
	NFE_WRITE(sc, NFE_RXTX_CTL, txctl);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

void
nfe_watchdog(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;

	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

	ifp->if_flags &= ~IFF_RUNNING;
	nfe_init(ifp);

	ifp->if_oerrors++;
}

int
nfe_init(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;
	uint32_t tmp, rxtxctl;

	if (ifp->if_flags & IFF_RUNNING)
		return 0;

	nfe_stop(ifp, 0);

	nfe_ifmedia_upd(ifp);

	NFE_WRITE(sc, NFE_TX_UNK, 0);

	rxtxctl = NFE_RXTX_BIT2;
	if (sc->sc_flags & NFE_40BIT_ADDR)
		rxtxctl |= NFE_RXTX_V3MAGIC;
	else if (sc->sc_flags & NFE_JUMBO_SUP)
		rxtxctl |= NFE_RXTX_V2MAGIC;
#ifdef NFE_CSUM
	if (sc->sc_flags & NFE_HW_CSUM)
		rxtxctl |= NFE_RXTX_RXCHECK;
#endif

	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_RESET | rxtxctl);
	DELAY(10);
	NFE_WRITE(sc, NFE_RXTX_CTL, rxtxctl);

	NFE_WRITE(sc, NFE_SETUP_R6, 0);

	/* set MAC address */
	nfe_set_macaddr(sc, sc->sc_arpcom.ac_enaddr);

	/* tell MAC where rings are in memory */
	NFE_WRITE(sc, NFE_RX_RING_ADDR, sc->rxq.physaddr);
	NFE_WRITE(sc, NFE_TX_RING_ADDR, sc->txq.physaddr);

	NFE_WRITE(sc, NFE_RING_SIZE,
	    (NFE_RX_RING_COUNT - 1) << 16 |
	    (NFE_TX_RING_COUNT - 1));

	NFE_WRITE(sc, NFE_RXBUFSZ, MCLBYTES);

	/* force MAC to wakeup */
	tmp = NFE_READ(sc, NFE_PWR_STATE);
	NFE_WRITE(sc, NFE_PWR_STATE, tmp | NFE_PWR_WAKEUP);
	DELAY(10);
	tmp = NFE_READ(sc, NFE_PWR_STATE);
	NFE_WRITE(sc, NFE_PWR_STATE, tmp | NFE_PWR_VALID);

	NFE_WRITE(sc, NFE_SETUP_R1, NFE_R1_MAGIC);
	NFE_WRITE(sc, NFE_SETUP_R2, NFE_R2_MAGIC);
	NFE_WRITE(sc, NFE_TIMER_INT, 970);	/* XXX Magic */

	NFE_WRITE(sc, NFE_SETUP_R4, NFE_R4_MAGIC);
	NFE_WRITE(sc, NFE_WOL_CTL, NFE_WOL_MAGIC);

	rxtxctl &= ~NFE_RXTX_BIT2;
	NFE_WRITE(sc, NFE_RXTX_CTL, rxtxctl);
	DELAY(10);
	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_BIT1 | rxtxctl);

	/* set Rx filter */
	nfe_setmulti(sc);

	/* enable Rx */
	NFE_WRITE(sc, NFE_RX_CTL, NFE_RX_START);

	/* enable Tx */
	NFE_WRITE(sc, NFE_TX_CTL, NFE_TX_START);

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	/* enable interrupts */
	NFE_WRITE(sc, NFE_IRQ_MASK, NFE_IRQ_WANTED);

	timeout_add(&sc->sc_tick_ch, hz);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	return 0;
}

void
nfe_stop(struct ifnet *ifp, int disable)
{
	struct nfe_softc *sc = ifp->if_softc;

	timeout_del(&sc->sc_tick_ch);

	ifp->if_timer = 0;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	mii_down(&sc->sc_mii);

	/* abort Tx */
	NFE_WRITE(sc, NFE_TX_CTL, 0);

	/* disable Rx */
	NFE_WRITE(sc, NFE_RX_CTL, 0);

	/* disable interrupts */
	NFE_WRITE(sc, NFE_IRQ_MASK, 0);

	/* reset Tx and Rx rings */
	nfe_reset_tx_ring(sc, &sc->txq);
	nfe_reset_rx_ring(sc, &sc->rxq);
}

int
nfe_alloc_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	struct nfe_rx_data *data;
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	void **desc;
	int i, nsegs, error, descsize;

	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc = (void **)&ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = (void **)&ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	ring->cur = ring->next = 0;

	error = bus_dmamap_create(sc->sc_dmat, NFE_RX_RING_COUNT * descsize, 1,
	    NFE_RX_RING_COUNT * descsize, 0, BUS_DMA_NOWAIT, &ring->map);
	if (error != 0) {
		printf("%s: could not create desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, NFE_RX_RING_COUNT * descsize,
	    PAGE_SIZE, 0, &ring->seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs,
	    NFE_RX_RING_COUNT * descsize, (caddr_t *)desc, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not map desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, *desc,
	    NFE_RX_RING_COUNT * descsize, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	bzero(*desc, NFE_RX_RING_COUNT * descsize);
	ring->physaddr = ring->map->dm_segs->ds_addr;

	/*
	 * Pre-allocate Rx buffers and populate Rx ring.
	 */
	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		data = &sc->rxq.data[i];

		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &data->map);
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		MGETHDR(data->m, M_DONTWAIT, MT_DATA);
		if (data->m == NULL) {
			printf("%s: could not allocate rx mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		MCLGET(data->m, M_DONTWAIT);
		if (!(data->m->m_flags & M_EXT)) {
			printf("%s: could not allocate rx mbuf cluster\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		error = bus_dmamap_load(sc->sc_dmat, data->map,
		    mtod(data->m, void *), MCLBYTES, NULL, BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: could not load rx buf DMA map",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->rxq.desc64[i];
#if defined(__LP64__)
			desc64->physaddr[0] =
			    htole32(data->map->dm_segs->ds_addr >> 32);
#endif
			desc64->physaddr[1] =
			    htole32(data->map->dm_segs->ds_addr & 0xffffffff);
			desc64->length = htole16(MCLBYTES);
			desc64->flags = htole16(NFE_RX_READY);
		} else {
			desc32 = &sc->rxq.desc32[i];
			desc32->physaddr =
			    htole32(data->map->dm_segs->ds_addr);
			desc32->length = htole16(MCLBYTES);
			desc32->flags = htole16(NFE_RX_READY);
		}
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	return 0;

fail:	nfe_free_rx_ring(sc, ring);
	return error;
}

void
nfe_reset_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	int i;

	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		if (sc->sc_flags & NFE_40BIT_ADDR) {
			ring->desc64[i].length = htole16(MCLBYTES);
			ring->desc64[i].flags = htole16(NFE_RX_READY);
		} else {
			ring->desc32[i].length = htole16(MCLBYTES);
			ring->desc32[i].flags = htole16(NFE_RX_READY);
		}
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ring->cur = ring->next = 0;
}

void
nfe_free_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	struct nfe_rx_data *data;
	void *desc;
	int i, descsize;

	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc = ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	if (desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)desc,
		    NFE_RX_RING_COUNT * descsize);
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}

	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
		}

		if (data->map != NULL)
			bus_dmamap_destroy(sc->sc_dmat, data->map);
	}
}

int
nfe_alloc_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	int i, nsegs, error;
	void **desc;
	int descsize;

	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc = (void **)&ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = (void **)&ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	ring->queued = 0;
	ring->cur = ring->next = 0;

	error = bus_dmamap_create(sc->sc_dmat, NFE_TX_RING_COUNT * descsize, 1,
	    NFE_TX_RING_COUNT * descsize, 0, BUS_DMA_NOWAIT, &ring->map);

	if (error != 0) {
		printf("%s: could not create desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, NFE_TX_RING_COUNT * descsize,
	    PAGE_SIZE, 0, &ring->seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs,
	    NFE_TX_RING_COUNT * descsize, (caddr_t *)desc, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not map desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, *desc,
	    NFE_TX_RING_COUNT * descsize, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	bzero(*desc, NFE_TX_RING_COUNT * descsize);
	ring->physaddr = ring->map->dm_segs->ds_addr;

	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    NFE_MAX_SCATTER, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &ring->data[i].map);
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}

	return 0;

fail:	nfe_free_tx_ring(sc, ring);
	return error;
}

void
nfe_reset_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	struct nfe_tx_data *data;
	int i;

	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		if (sc->sc_flags & NFE_40BIT_ADDR)
			ring->desc64[i].flags = 0;
		else
			ring->desc32[i].flags = 0;

		data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->active, 0,
			    data->active->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->active);
			m_freem(data->m);
			data->m = NULL;
		}
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ring->queued = 0;
	ring->cur = ring->next = 0;
}

void
nfe_free_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	struct nfe_tx_data *data;
	void *desc;
	int i, descsize;

	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc = ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	if (desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)desc,
		    NFE_TX_RING_COUNT * descsize);
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}

	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->active, 0,
			    data->active->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->active);
			m_freem(data->m);
		}
	}

	/* ..and now actually destroy the DMA mappings */
	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		data = &ring->data[i];
		if (data->map == NULL)
			continue;
		bus_dmamap_destroy(sc->sc_dmat, data->map);
	}
}

int
nfe_ifmedia_upd(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;
	struct mii_softc *miisc;

	if (mii->mii_instance != 0) {
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	return mii_mediachg(mii);
}

void
nfe_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

void
nfe_setmulti(struct nfe_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint8_t addr[ETHER_ADDR_LEN], mask[ETHER_ADDR_LEN];
	uint32_t filter = NFE_RXFILTER_MAGIC;
	int i;

	if ((ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC)) != 0) {
		bzero(addr, ETHER_ADDR_LEN);
		bzero(mask, ETHER_ADDR_LEN);
		goto done;
	}

	bcopy(etherbroadcastaddr, addr, ETHER_ADDR_LEN);
	bcopy(etherbroadcastaddr, mask, ETHER_ADDR_LEN);

	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		if (bcmp(enm->enm_addrlo, enm->enm_addrhi, ETHER_ADDR_LEN)) {
			ifp->if_flags |= IFF_ALLMULTI;
			bzero(addr, ETHER_ADDR_LEN);
			bzero(mask, ETHER_ADDR_LEN);
			goto done;
		}
		for (i = 0; i < ETHER_ADDR_LEN; i++) {
			addr[i] &=  enm->enm_addrlo[i];
			mask[i] &= ~enm->enm_addrlo[i];
		}
		ETHER_NEXT_MULTI(step, enm);
	}
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		mask[i] |= addr[i];

done:
	addr[0] |= 0x01;	/* make sure multicast bit is set */

	NFE_WRITE(sc, NFE_MULTIADDR_HI,
	    addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0]);
	NFE_WRITE(sc, NFE_MULTIADDR_LO,
	    addr[5] <<  8 | addr[4]);
	NFE_WRITE(sc, NFE_MULTIMASK_HI,
	    mask[3] << 24 | mask[2] << 16 | mask[1] << 8 | mask[0]);
	NFE_WRITE(sc, NFE_MULTIMASK_LO,
	    mask[5] <<  8 | mask[4]);

	filter |= (ifp->if_flags & IFF_PROMISC) ? NFE_PROMISC : NFE_U2M;
	NFE_WRITE(sc, NFE_RXFILTER, filter);
}

void
nfe_get_macaddr(struct nfe_softc *sc, uint8_t *addr)
{
	uint32_t tmp;

	tmp = NFE_READ(sc, NFE_MACADDR_LO);
	addr[0] = (tmp >> 8) & 0xff;
	addr[1] = (tmp & 0xff);

	tmp = NFE_READ(sc, NFE_MACADDR_HI);
	addr[2] = (tmp >> 24) & 0xff;
	addr[3] = (tmp >> 16) & 0xff;
	addr[4] = (tmp >>  8) & 0xff;
	addr[5] = (tmp & 0xff);
}

void
nfe_set_macaddr(struct nfe_softc *sc, const uint8_t *addr)
{
	NFE_WRITE(sc, NFE_MACADDR_LO,
	    addr[5] <<  8 | addr[4]);
	NFE_WRITE(sc, NFE_MACADDR_HI,
	    addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0]);
}

void
nfe_tick(void *arg)
{
	struct nfe_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add(&sc->sc_tick_ch, hz);
}
