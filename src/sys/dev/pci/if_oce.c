/*	$OpenBSD: if_oce.c,v 1.48 2012/11/08 18:56:54 mikeb Exp $	*/

/*
 * Copyright (c) 2012 Mike Belopuhov
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

/*-
 * Copyright (C) 2012 Emulex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Emulex Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * freebsd-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/timeout.h>
#include <sys/pool.h>

#include <uvm/uvm_extern.h>

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

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NVLAN > 0
#include <net/if_types.h>
#include <net/if_vlan_var.h>
#endif

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_ocereg.h>
#include <dev/pci/if_ocevar.h>

int  oce_probe(struct device *parent, void *match, void *aux);
void oce_attach(struct device *parent, struct device *self, void *aux);
void oce_attachhook(void *arg);
void oce_attach_ifp(struct oce_softc *sc);
int  oce_ioctl(struct ifnet *ifp, u_long command, caddr_t data);
void oce_init(void *xsc);
void oce_stop(struct oce_softc *sc);
void oce_iff(struct oce_softc *sc);

int  oce_pci_alloc(struct oce_softc *sc, struct pci_attach_args *pa);
int  oce_intr(void *arg);
int  oce_alloc_intr(struct oce_softc *sc, struct pci_attach_args *pa);
void oce_intr_enable(struct oce_softc *sc);
void oce_intr_disable(struct oce_softc *sc);

void oce_media_status(struct ifnet *ifp, struct ifmediareq *ifmr);
int  oce_media_change(struct ifnet *ifp);
void oce_update_link_status(struct oce_softc *sc);
void oce_link_event(struct oce_softc *sc,
    struct oce_async_cqe_link_state *acqe);

int  oce_get_buf(struct oce_rq *rq);
int  oce_alloc_rx_bufs(struct oce_rq *rq);
void oce_refill_rx(void *arg);

void oce_watchdog(struct ifnet *ifp);
void oce_start(struct ifnet *ifp);
int  oce_encap(struct oce_softc *sc, struct mbuf **mpp, int wq_index);
void oce_txeof(struct oce_wq *wq);

void oce_discard_rx_comp(struct oce_rq *rq, struct oce_nic_rx_cqe *cqe);
int  oce_cqe_vtp_valid(struct oce_softc *sc, struct oce_nic_rx_cqe *cqe);
int  oce_cqe_portid_valid(struct oce_softc *sc, struct oce_nic_rx_cqe *cqe);
void oce_rxeof(struct oce_rq *rq, struct oce_nic_rx_cqe *cqe);
int  oce_start_rq(struct oce_rq *rq);
void oce_stop_rq(struct oce_rq *rq);
void oce_free_posted_rxbuf(struct oce_rq *rq);

int  oce_vid_config(struct oce_softc *sc);
void oce_set_macaddr(struct oce_softc *sc);
void oce_tick(void *arg);

#if defined(INET6) || defined(INET)
#ifdef OCE_LRO
int  oce_init_lro(struct oce_softc *sc);
void oce_free_lro(struct oce_softc *sc);
void oce_rx_flush_lro(struct oce_rq *rq);
#endif
#ifdef OCE_TSO
struct mbuf *oce_tso_setup(struct oce_softc *sc, struct mbuf **mpp);
#endif
#endif

void oce_intr_mq(void *arg);
void oce_intr_wq(void *arg);
void oce_intr_rq(void *arg);

int  oce_init_queues(struct oce_softc *sc);
void oce_release_queues(struct oce_softc *sc);

struct oce_wq *oce_create_wq(struct oce_softc *sc, struct oce_eq *eq);
void oce_drain_wq(struct oce_wq *wq);
void oce_destroy_wq(struct oce_wq *wq);

struct oce_rq *oce_create_rq(struct oce_softc *sc, struct oce_eq *eq,
    uint32_t if_id, uint32_t rss);
void oce_drain_rq(struct oce_rq *rq);
void oce_destroy_rq(struct oce_rq *rq);

struct oce_eq *oce_create_eq(struct oce_softc *sc);
static inline void oce_arm_eq(struct oce_eq *eq, int neqe, int rearm,
    int clearint);
void oce_drain_eq(struct oce_eq *eq);
void oce_destroy_eq(struct oce_eq *eq);

struct oce_mq *oce_create_mq(struct oce_softc *sc, struct oce_eq *eq);
void oce_drain_mq(struct oce_mq *mq);
void oce_destroy_mq(struct oce_mq *mq);

struct oce_cq *oce_create_cq(struct oce_softc *sc, struct oce_eq *eq,
    uint32_t q_len, uint32_t item_size, uint32_t is_eventable,
    uint32_t nodelay, uint32_t ncoalesce);
static inline void oce_arm_cq(struct oce_cq *cq, int ncqe, int rearm);
void oce_destroy_cq(struct oce_cq *cq);

int oce_dma_alloc(struct oce_softc *sc, bus_size_t size,
    struct oce_dma_mem *dma);
void oce_dma_free(struct oce_softc *sc, struct oce_dma_mem *dma);

struct oce_ring *oce_create_ring(struct oce_softc *sc, int nitems,
    int isize, int maxsegs);
void oce_destroy_ring(struct oce_softc *sc, struct oce_ring *ring);
int oce_load_ring(struct oce_softc *sc, struct oce_ring *ring,
    struct phys_addr *pa_list, int max_segs);
static inline void *oce_ring_get(struct oce_ring *ring);
static inline void *oce_ring_first(struct oce_ring *ring);
static inline void *oce_ring_next(struct oce_ring *ring);

struct oce_pkt *oce_pkt_alloc(struct oce_softc *sc, size_t size, int nsegs,
    int maxsegs);
void oce_pkt_free(struct oce_softc *sc, struct oce_pkt *pkt);
static inline struct oce_pkt *oce_pkt_get(struct oce_pkt_list *lst);
static inline void oce_pkt_put(struct oce_pkt_list *lst, struct oce_pkt *pkt);

int oce_init_fw(struct oce_softc *sc);
int oce_mbox_init(struct oce_softc *sc);
int oce_mbox_dispatch(struct oce_softc *sc);
int oce_cmd(struct oce_softc *sc, int subsys, int opcode, int version,
    void *payload, int length);
void oce_first_mcc(struct oce_softc *sc);

int oce_get_fw_config(struct oce_softc *sc);
int oce_check_native_mode(struct oce_softc *sc);
int oce_create_iface(struct oce_softc *sc, uint8_t *macaddr);
int oce_config_vlan(struct oce_softc *sc, uint32_t if_id,
    struct normal_vlan *vtag_arr, int vtag_cnt, int untagged, int promisc);
int oce_set_flow_control(struct oce_softc *sc, uint32_t flow_control);
int oce_config_rss(struct oce_softc *sc, uint32_t if_id, int enable);
int oce_update_mcast(struct oce_softc *sc,
    uint8_t multi[][ETHER_ADDR_LEN], int naddr);
int oce_set_promisc(struct oce_softc *sc, int enable);
int oce_get_link_status(struct oce_softc *sc);

int oce_macaddr_get(struct oce_softc *sc, uint8_t *macaddr);
int oce_macaddr_add(struct oce_softc *sc, uint8_t *macaddr,
    uint32_t if_id, uint32_t *pmac_id);
int oce_macaddr_del(struct oce_softc *sc, uint32_t if_id,
    uint32_t pmac_id);

int oce_new_rq(struct oce_softc *sc, struct oce_rq *rq);
int oce_new_wq(struct oce_softc *sc, struct oce_wq *wq);
int oce_new_mq(struct oce_softc *sc, struct oce_mq *mq);
int oce_new_eq(struct oce_softc *sc, struct oce_eq *eq);
int oce_new_cq(struct oce_softc *sc, struct oce_cq *cq);
int oce_destroy_queue(struct oce_softc *sc, enum qtype qtype, uint32_t qid);

static inline int oce_update_stats(struct oce_softc *sc);
int oce_stats_be2(struct oce_softc *sc, uint64_t *rxe, uint64_t *txe);
int oce_stats_be3(struct oce_softc *sc, uint64_t *rxe, uint64_t *txe);
int oce_stats_xe(struct oce_softc *sc, uint64_t *rxe, uint64_t *txe);

struct pool *oce_pkt_pool;
extern struct uvm_constraint_range no_constraint;
const struct kmem_pa_mode kp_contig = {
	.kp_constraint = &no_constraint,
	.kp_maxseg = 1,
	.kp_zero = 1
};

struct cfdriver oce_cd = {
	NULL, "oce", DV_IFNET
};

struct cfattach oce_ca = {
	sizeof(struct oce_softc), oce_probe, oce_attach, NULL, NULL
};

const struct pci_matchid oce_devices[] = {
	{ PCI_VENDOR_SERVERENGINES, PCI_PRODUCT_SERVERENGINES_BE2 },
	{ PCI_VENDOR_SERVERENGINES, PCI_PRODUCT_SERVERENGINES_BE3 },
	{ PCI_VENDOR_SERVERENGINES, PCI_PRODUCT_SERVERENGINES_OCBE2 },
	{ PCI_VENDOR_SERVERENGINES, PCI_PRODUCT_SERVERENGINES_OCBE3 },
	{ PCI_VENDOR_EMULEX, PCI_PRODUCT_EMULEX_XE201 },
};

int
oce_probe(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, oce_devices, nitems(oce_devices)));
}

void
oce_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	struct oce_softc *sc = (struct oce_softc *)self;

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_SERVERENGINES_BE2:
	case PCI_PRODUCT_SERVERENGINES_OCBE2:
		SET(sc->flags, OCE_F_BE2);
		break;
	case PCI_PRODUCT_SERVERENGINES_BE3:
	case PCI_PRODUCT_SERVERENGINES_OCBE3:
		SET(sc->flags, OCE_F_BE3);
		break;
	case PCI_PRODUCT_EMULEX_XE201:
		SET(sc->flags, OCE_F_XE201);
		break;
	}

	sc->dmat = pa->pa_dmat;
	if (oce_pci_alloc(sc, pa))
		return;

	sc->rss_enable 	 = 0;
	sc->tx_ring_size = OCE_TX_RING_SIZE;
	sc->rx_ring_size = OCE_RX_RING_SIZE;
	sc->rx_frag_size = OCE_RQ_BUF_SIZE;
	sc->flow_control = OCE_FC_TX | OCE_FC_RX;

	/* create the bootstrap mailbox */
	if (oce_dma_alloc(sc, sizeof(struct oce_bmbx), &sc->bsmbx)) {
		printf(": failed to allocate mailbox memory\n");
		return;
	}

	if (oce_init_fw(sc))
		goto fail_1;

	if (oce_mbox_init(sc)) {
		printf(": failed to initialize mailbox\n");
		goto fail_1;
	}

	if (oce_get_fw_config(sc)) {
		printf(": failed to get firmware configuration\n");
		goto fail_1;
	}

	if (ISSET(sc->flags, OCE_F_BE3)) {
		if (oce_check_native_mode(sc))
			goto fail_1;
	}

	if (oce_macaddr_get(sc, sc->macaddr)) {
		printf(": failed to fetch MAC address\n");
		goto fail_1;
	}
	bcopy(sc->macaddr, sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	if (oce_pkt_pool == NULL) {
		oce_pkt_pool = malloc(sizeof(struct pool), M_DEVBUF, M_NOWAIT);
		if (oce_pkt_pool == NULL) {
			printf(": unable to allocate descriptor pool\n");
			goto fail_1;
		}
		pool_init(oce_pkt_pool, sizeof(struct oce_pkt), 0, 0, 0,
		    "ocepkts", NULL);
		pool_set_constraints(oce_pkt_pool, &kp_contig);
	}

	if (oce_alloc_intr(sc, pa))
		goto fail_1;

	if (oce_init_queues(sc))
		goto fail_1;

	oce_attach_ifp(sc);

#ifdef OCE_LRO
	if (oce_init_lro(sc))
		goto fail_2;
#endif

	timeout_set(&sc->timer, oce_tick, sc);
	timeout_set(&sc->rxrefill, oce_refill_rx, sc);

	mountroothook_establish(oce_attachhook, sc);

	printf(", address %s\n", ether_sprintf(sc->arpcom.ac_enaddr));

	return;

#ifdef OCE_LRO
fail_2:
	oce_free_lro(sc);
	ether_ifdetach(&sc->arpcom.ac_if);
	if_detach(&sc->arpcom.ac_if);
	oce_release_queues(sc);
#endif
fail_1:
	oce_dma_free(sc, &sc->bsmbx);
}

void
oce_attachhook(void *arg)
{
	struct oce_softc *sc = arg;

	oce_get_link_status(sc);

	oce_arm_cq(sc->mq->cq, 0, TRUE);

	/*
	 * We need to get MCC async events. So enable intrs and arm
	 * first EQ, Other EQs will be armed after interface is UP
	 */
	oce_intr_enable(sc);
	oce_arm_eq(sc->eq[0], 0, TRUE, FALSE);

	/*
	 * Send first mcc cmd and after that we get gracious
	 * MCC notifications from FW
	 */
	oce_first_mcc(sc);
}

void
oce_attach_ifp(struct oce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ifmedia_init(&sc->media, IFM_IMASK, oce_media_change, oce_media_status);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

	strlcpy(ifp->if_xname, sc->dev.dv_xname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = oce_ioctl;
	ifp->if_start = oce_start;
	ifp->if_watchdog = oce_watchdog;
	ifp->if_hardmtu = OCE_MAX_MTU;
	ifp->if_softc = sc;
	IFQ_SET_MAXLEN(&ifp->if_snd, sc->tx_ring_size - 1);
	IFQ_SET_READY(&ifp->if_snd);

	/* oce splits jumbos into 2k chunks... */
	m_clsetwms(ifp, MCLBYTES, 8, sc->rx_ring_size);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

#if defined(INET6) || defined(INET)
#ifdef OCE_TSO
	ifp->if_capabilities |= IFCAP_TSO;
	ifp->if_capabilities |= IFCAP_VLAN_HWTSO;
#endif
#ifdef OCE_LRO
	ifp->if_capabilities |= IFCAP_LRO;
#endif
#endif

	if_attach(ifp);
	ether_ifattach(ifp);
}

int
oce_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct oce_softc *sc = ifp->if_softc;
	struct ifaddr *ifa = (struct ifaddr *)data;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			oce_init(sc);
#ifdef INET
		if (ifa->ifa_addr->sa_family == AF_INET)
			arp_ifinit(&sc->arpcom, ifa);
#endif
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				oce_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				oce_stop(sc);
		}
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < OCE_MIN_MTU || ifr->ifr_mtu > OCE_MAX_MTU)
			error = EINVAL;
		else if (ifp->if_mtu != ifr->ifr_mtu) {
			ifp->if_mtu = ifr->ifr_mtu;
			oce_init(sc);
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;
	default:
		error = ether_ioctl(ifp, &sc->arpcom, command, data);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			oce_iff(sc);
		error = 0;
	}

	splx(s);

	return error;
}

void
oce_iff(struct oce_softc *sc)
{
	uint8_t multi[OCE_MAX_MC_FILTER_SIZE][ETHER_ADDR_LEN];
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct arpcom *ac = &sc->arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	int naddr = 0, promisc = 0;

	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0 ||
	    ac->ac_multicnt >= OCE_MAX_MC_FILTER_SIZE) {
		ifp->if_flags |= IFF_ALLMULTI;
		promisc = 1;
	} else {
		ETHER_FIRST_MULTI(step, &sc->arpcom, enm);
		while (enm != NULL) {
			bcopy(enm->enm_addrlo, multi[naddr++], ETHER_ADDR_LEN);
			ETHER_NEXT_MULTI(step, enm);
		}
		oce_update_mcast(sc, multi, naddr);
	}

	oce_set_promisc(sc, promisc);
}

int
oce_intr(void *arg)
{
	struct oce_softc *sc = arg;
	struct oce_eq *eq = sc->eq[0];
	struct oce_eqe *eqe;
	struct oce_cq *cq = NULL;
	int i, claimed = 0, neqe = 0;

	oce_dma_sync(&eq->ring->dma, BUS_DMASYNC_POSTREAD);

	OCE_RING_FOREACH(eq->ring, eqe, eqe->evnt != 0) {
		eqe->evnt = 0;
		oce_dma_sync(&eq->ring->dma, BUS_DMASYNC_PREWRITE);
		neqe++;
	}

	if (!neqe)
		goto eq_arm; /* Spurious */

	claimed = 1;

 	/* Clear EQ entries, but dont arm */
	oce_arm_eq(eq, neqe, FALSE, TRUE);

	/* Process TX, RX and MCC completion queues */
	for (i = 0; i < eq->cq_valid; i++) {
		cq = eq->cq[i];
		(*cq->cq_intr)(cq->cb_arg);
	}

	/* Arm all CQs connected to this EQ */
	for (i = 0; i < eq->cq_valid; i++) {
		cq = eq->cq[i];
		oce_arm_cq(cq, 0, TRUE);
	}

eq_arm:
	oce_arm_eq(eq, 0, TRUE, FALSE);
	return (claimed);
}

int
oce_pci_alloc(struct oce_softc *sc, struct pci_attach_args *pa)
{
	pcireg_t memtype, reg;

	/* setup the device config region */
	if (ISSET(sc->flags, OCE_F_BE2))
		reg = OCE_BAR_CFG_BE2;
	else
		reg = OCE_BAR_CFG;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, reg);
	if (pci_mapreg_map(pa, reg, memtype, 0, &sc->cfg_iot,
	    &sc->cfg_ioh, NULL, &sc->cfg_size,
	    IS_BE(sc) ? 0 : 32768)) {
		printf(": can't find cfg mem space\n");
		return (ENXIO);
	}

	/*
	 * Read the SLI_INTF register and determine whether we
	 * can use this port and its features
	 */
	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, OCE_INTF_REG_OFFSET);
	if (OCE_SLI_SIGNATURE(reg) != OCE_INTF_VALID_SIG) {
		printf(": invalid signature\n");
		goto fail_1;
	}
	if (OCE_SLI_REVISION(reg) != OCE_INTF_SLI_REV4) {
		printf(": unsupported SLI revision\n");
		goto fail_1;
	}
	if (OCE_SLI_IFTYPE(reg) == OCE_INTF_IF_TYPE_1)
		SET(sc->flags, OCE_F_MBOX_ENDIAN_RQD);
	if (OCE_SLI_HINT1(reg) == OCE_INTF_FUNC_RESET_REQD)
		SET(sc->flags, OCE_F_RESET_RQD);

	/* Lancer has one BAR (CFG) but BE3 has three (CFG, CSR, DB) */
	if (IS_BE(sc)) {
		/* set up CSR region */
		reg = OCE_BAR_CSR;
		memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, reg);
		if (pci_mapreg_map(pa, reg, memtype, 0, &sc->csr_iot,
		    &sc->csr_ioh, NULL, &sc->csr_size, 0)) {
			printf(": can't find csr mem space\n");
			goto fail_1;
		}

		/* set up DB doorbell region */
		reg = OCE_BAR_DB;
		memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, reg);
		if (pci_mapreg_map(pa, reg, memtype, 0, &sc->db_iot,
		    &sc->db_ioh, NULL, &sc->db_size, 0)) {
			printf(": can't find csr mem space\n");
			goto fail_2;
		}
	} else {
		sc->csr_iot = sc->db_iot = sc->cfg_iot;
		sc->csr_ioh = sc->db_ioh = sc->cfg_ioh;
	}

	return (0);

fail_2:
	bus_space_unmap(sc->csr_iot, sc->csr_ioh, sc->csr_size);
fail_1:
	bus_space_unmap(sc->cfg_iot, sc->cfg_ioh, sc->cfg_size);
	return (ENXIO);
}

static inline uint32_t
oce_read_cfg(struct oce_softc *sc, bus_size_t off)
{
	bus_space_barrier(sc->cfg_iot, sc->cfg_ioh, off, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->cfg_iot, sc->cfg_ioh, off));
}

static inline uint32_t
oce_read_csr(struct oce_softc *sc, bus_size_t off)
{
	bus_space_barrier(sc->csr_iot, sc->csr_ioh, off, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->csr_iot, sc->csr_ioh, off));
}

static inline uint32_t
oce_read_db(struct oce_softc *sc, bus_size_t off)
{
	bus_space_barrier(sc->db_iot, sc->db_ioh, off, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->db_iot, sc->db_ioh, off));
}

static inline void
oce_write_cfg(struct oce_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->cfg_iot, sc->cfg_ioh, off, val);
	bus_space_barrier(sc->cfg_iot, sc->cfg_ioh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

static inline void
oce_write_csr(struct oce_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->csr_iot, sc->csr_ioh, off, val);
	bus_space_barrier(sc->csr_iot, sc->csr_ioh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

static inline void
oce_write_db(struct oce_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->db_iot, sc->db_ioh, off, val);
	bus_space_barrier(sc->db_iot, sc->db_ioh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

int
oce_alloc_intr(struct oce_softc *sc, struct pci_attach_args *pa)
{
	const char *intrstr = NULL;
	pci_intr_handle_t ih;

	sc->intr_count = 1;

	/* We allocate a single interrupt resource */
	if (pci_intr_map_msi(pa, &ih) != 0 &&
	    pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		return (ENXIO);
	}

	intrstr = pci_intr_string(pa->pa_pc, ih);
	if (pci_intr_establish(pa->pa_pc, ih, IPL_NET, oce_intr, sc,
	    sc->dev.dv_xname) == NULL) {
		printf(": couldn't establish interrupt\n");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return (ENXIO);
	}
	printf(": %s", intrstr);

	return (0);
}

void
oce_intr_enable(struct oce_softc *sc)
{
	uint32_t reg;

	reg = oce_read_cfg(sc, PCI_INTR_CTRL);
	oce_write_cfg(sc, PCI_INTR_CTRL, reg | HOSTINTR_MASK);
}

void
oce_intr_disable(struct oce_softc *sc)
{
	uint32_t reg;

	reg = oce_read_cfg(sc, PCI_INTR_CTRL);
	oce_write_cfg(sc, PCI_INTR_CTRL, reg & ~HOSTINTR_MASK);
}

void
oce_update_link_status(struct oce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int speed = 0;

	if (sc->link_status) {
		if (sc->link_active == 0) {
			switch (sc->link_speed) {
			case 1: /* 10 Mbps */
				speed = 10;
				break;
			case 2: /* 100 Mbps */
				speed = 100;
				break;
			case 3: /* 1 Gbps */
				speed = 1000;
				break;
			case 4: /* 10 Gbps */
				speed = 10000;
				break;
			}
			sc->link_active = 1;
			ifp->if_baudrate = speed * 1000000ULL;
		}
		if (!LINK_STATE_IS_UP(ifp->if_link_state)) {
			ifp->if_link_state = LINK_STATE_FULL_DUPLEX;
			if_link_state_change(ifp);
		}
	} else {
		if (sc->link_active == 1) {
			ifp->if_baudrate = 0;
			sc->link_active = 0;
		}
		if (ifp->if_link_state != LINK_STATE_DOWN) {
			ifp->if_link_state = LINK_STATE_DOWN;
			if_link_state_change(ifp);
		}
	}
}

void
oce_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct oce_softc *sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (oce_get_link_status(sc) == 0)
		oce_update_link_status(sc);

	if (!sc->link_status) {
		ifmr->ifm_active |= IFM_NONE;
		return;
	}

	ifmr->ifm_status |= IFM_ACTIVE;

	switch (sc->link_speed) {
	case 1: /* 10 Mbps */
		ifmr->ifm_active |= IFM_10_T | IFM_FDX;
		break;
	case 2: /* 100 Mbps */
		ifmr->ifm_active |= IFM_100_TX | IFM_FDX;
		break;
	case 3: /* 1 Gbps */
		ifmr->ifm_active |= IFM_1000_T | IFM_FDX;
		break;
	case 4: /* 10 Gbps */
		ifmr->ifm_active |= IFM_10G_SR | IFM_FDX;
		break;
	}

	if (sc->flow_control & OCE_FC_TX)
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_TXPAUSE;
	if (sc->flow_control & OCE_FC_RX)
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_RXPAUSE;
}

int
oce_media_change(struct ifnet *ifp)
{
	return (0);
}

int
oce_encap(struct oce_softc *sc, struct mbuf **mpp, int wq_index)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m = *mpp;
	struct oce_wq *wq = sc->wq[wq_index];
	struct oce_pkt *pkt = NULL;
	struct oce_nic_hdr_wqe *nichdr;
	struct oce_nic_frag_wqe *nicfrag;
	int i, nwqe, rc;

#ifdef OCE_TSO
	if (m->m_pkthdr.csum_flags & CSUM_TSO) {
		/* consolidate packet buffers for TSO/LSO segment offload */
#if defined(INET6) || defined(INET)
		m = oce_tso_setup(sc, mpp);
#else
		m = NULL;	/* Huh? */
#endif
		if (m == NULL)
			goto error;
	}
#endif

	if ((pkt = oce_pkt_get(&wq->pkt_free)) == NULL)
		goto error;

	rc = bus_dmamap_load_mbuf(sc->dmat, pkt->map, m, BUS_DMA_NOWAIT);
	if (rc == EFBIG) {
		if (m_defrag(m, M_DONTWAIT) ||
		    bus_dmamap_load_mbuf(sc->dmat, pkt->map, m, BUS_DMA_NOWAIT))
			goto error;
		*mpp = m;
	} else if (rc != 0)
		goto error;

	pkt->nsegs = pkt->map->dm_nsegs;

	nwqe = pkt->nsegs + 1;
	if (IS_BE(sc)) {
		/*Dummy required only for BE3.*/
		if (nwqe & 1)
			nwqe++;
	}
	if (nwqe >= RING_NUM_FREE(wq->ring)) {
		bus_dmamap_unload(sc->dmat, pkt->map);
		goto error;
	}

	bus_dmamap_sync(sc->dmat, pkt->map, 0, pkt->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	pkt->mbuf = m;

	nichdr = oce_ring_get(wq->ring);
	nichdr->u0.dw[0] = 0;
	nichdr->u0.dw[1] = 0;
	nichdr->u0.dw[2] = 0;
	nichdr->u0.dw[3] = 0;

	nichdr->u0.s.complete = 1;
	nichdr->u0.s.event = 1;
	nichdr->u0.s.crc = 1;
	nichdr->u0.s.forward = 0;
	nichdr->u0.s.ipcs = (m->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT) ? 1 : 0;
	nichdr->u0.s.udpcs = (m->m_pkthdr.csum_flags & M_UDP_CSUM_OUT) ? 1 : 0;
	nichdr->u0.s.tcpcs = (m->m_pkthdr.csum_flags & M_TCP_CSUM_OUT) ? 1 : 0;
	nichdr->u0.s.num_wqe = nwqe;
	nichdr->u0.s.total_length = m->m_pkthdr.len;

#if NVLAN > 0
	if (m->m_flags & M_VLANTAG) {
		nichdr->u0.s.vlan = 1; /* Vlan present */
		nichdr->u0.s.vlan_tag = m->m_pkthdr.ether_vtag;
	}
#endif

#ifdef OCE_TSO
	if (m->m_pkthdr.csum_flags & CSUM_TSO) {
		if (m->m_pkthdr.tso_segsz) {
			nichdr->u0.s.lso = 1;
			nichdr->u0.s.lso_mss  = m->m_pkthdr.tso_segsz;
		}
		if (!IS_BE(sc))
			nichdr->u0.s.ipcs = 1;
	}
#endif

	oce_dma_sync(&wq->ring->dma, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	wq->ring->nused++;

	for (i = 0; i < pkt->nsegs; i++) {
		nicfrag = oce_ring_get(wq->ring);
		nicfrag->u0.s.rsvd0 = 0;
		nicfrag->u0.s.frag_pa_hi = ADDR_HI(pkt->map->dm_segs[i].ds_addr);
		nicfrag->u0.s.frag_pa_lo = ADDR_LO(pkt->map->dm_segs[i].ds_addr);
		nicfrag->u0.s.frag_len = pkt->map->dm_segs[i].ds_len;
		wq->ring->nused++;
	}
	if (nwqe > (pkt->nsegs + 1)) {
		nicfrag = oce_ring_get(wq->ring);
		nicfrag->u0.dw[0] = 0;
		nicfrag->u0.dw[1] = 0;
		nicfrag->u0.dw[2] = 0;
		nicfrag->u0.dw[3] = 0;
		wq->ring->nused++;
		pkt->nsegs++;
	}

	oce_pkt_put(&wq->pkt_list, pkt);

	ifp->if_opackets++;

	oce_dma_sync(&wq->ring->dma, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);

	oce_write_db(sc, PD_TXULP_DB, wq->id | (nwqe << 16));

	return (0);

error:
	if (pkt)
		oce_pkt_put(&wq->pkt_free, pkt);
	m_freem(*mpp);
	*mpp = NULL;
	return (1);
}

void
oce_txeof(struct oce_wq *wq)
{
	struct oce_softc *sc = wq->sc;
	struct oce_pkt *pkt;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m;

	if ((pkt = oce_pkt_get(&wq->pkt_list)) == NULL) {
		printf("%s: missing descriptor in txeof\n", sc->dev.dv_xname);
		return;
	}

	wq->ring->nused -= pkt->nsegs + 1;
	bus_dmamap_sync(sc->dmat, pkt->map, 0, pkt->map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->dmat, pkt->map);

	m = pkt->mbuf;
	m_freem(m);
	pkt->mbuf = NULL;
	oce_pkt_put(&wq->pkt_free, pkt);

	if (ifp->if_flags & IFF_OACTIVE) {
		if (wq->ring->nused < (wq->ring->nitems / 2)) {
			ifp->if_flags &= ~IFF_OACTIVE;
			oce_start(ifp);
		}
	}
	if (wq->ring->nused == 0)
		ifp->if_timer = 0;
}

#if OCE_TSO
#if defined(INET6) || defined(INET)
struct mbuf *
oce_tso_setup(struct oce_softc *sc, struct mbuf **mpp)
{
	struct mbuf *m;
#ifdef INET
	struct ip *ip;
#endif
#ifdef INET6
	struct ip6_hdr *ip6;
#endif
	struct ether_vlan_header *eh;
	struct tcphdr *th;
	uint16_t etype;
	int total_len = 0, ehdrlen = 0;

	m = *mpp;

	if (M_WRITABLE(m) == 0) {
		m = m_dup(*mpp, M_DONTWAIT);
		if (!m)
			return NULL;
		m_freem(*mpp);
		*mpp = m;
	}

	eh = mtod(m, struct ether_vlan_header *);
	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		etype = ntohs(eh->evl_proto);
		ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
	} else {
		etype = ntohs(eh->evl_encap_proto);
		ehdrlen = ETHER_HDR_LEN;
	}

	switch (etype) {
#ifdef INET
	case ETHERTYPE_IP:
		ip = (struct ip *)(m->m_data + ehdrlen);
		if (ip->ip_p != IPPROTO_TCP)
			return NULL;
		th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));

		total_len = ehdrlen + (ip->ip_hl << 2) + (th->th_off << 2);
		break;
#endif
#ifdef INET6
	case ETHERTYPE_IPV6:
		ip6 = (struct ip6_hdr *)(m->m_data + ehdrlen);
		if (ip6->ip6_nxt != IPPROTO_TCP)
			return NULL;
		th = (struct tcphdr *)((caddr_t)ip6 + sizeof(struct ip6_hdr));

		total_len = ehdrlen + sizeof(struct ip6_hdr) + (th->th_off << 2);
		break;
#endif
	default:
		return NULL;
	}

	m = m_pullup(m, total_len);
	if (!m)
		return NULL;
	*mpp = m;
	return m;

}
#endif /* INET6 || INET */
#endif

void
oce_watchdog(struct ifnet *ifp)
{
	printf("%s: watchdog timeout -- resetting\n", ifp->if_xname);

	oce_init(ifp->if_softc);

	ifp->if_oerrors++;
}

void
oce_start(struct ifnet *ifp)
{
	struct oce_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int pkts = 0;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;

	for (;;) {
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;

		if (oce_encap(sc, &m, 0)) {
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
		pkts++;
	}

	/* Set a timeout in case the chip goes out to lunch */
	if (pkts)
		ifp->if_timer = 5;
}

/* Handle the Completion Queue for transmit */
void
oce_intr_wq(void *arg)
{
	struct oce_wq *wq = (struct oce_wq *)arg;
	struct oce_cq *cq = wq->cq;
	struct oce_nic_tx_cqe *cqe;
	int ncqe = 0;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);
	OCE_RING_FOREACH(cq->ring, cqe, WQ_CQE_VALID(cqe)) {
		DW_SWAP((uint32_t *) cqe, sizeof(oce_wq_cqe));

		oce_txeof(wq);

		WQ_CQE_INVALIDATE(cqe);
		oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_PREWRITE);
		ncqe++;
	}
	if (ncqe)
		oce_arm_cq(cq, ncqe, FALSE);
}

void
oce_rxeof(struct oce_rq *rq, struct oce_nic_rx_cqe *cqe)
{
	struct oce_softc *sc = rq->sc;
	struct oce_pkt *pkt = NULL;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m = NULL, *tail = NULL;
	int i, len, frag_len;
	uint16_t vtag;

	len = cqe->u0.s.pkt_size;
	if (!len) {
		/* partial DMA workaround for Lancer */
		oce_discard_rx_comp(rq, cqe);
		goto exit;
	}

	 /* Get vlan_tag value */
	if (IS_BE(sc))
		vtag = BSWAP_16(cqe->u0.s.vlan_tag);
	else
		vtag = cqe->u0.s.vlan_tag;

	for (i = 0; i < cqe->u0.s.num_fragments; i++) {
		if ((pkt = oce_pkt_get(&rq->pkt_list)) == NULL) {
			printf("%s: missing descriptor in rxeof\n",
			    sc->dev.dv_xname);
			goto exit;
		}

		bus_dmamap_sync(sc->dmat, pkt->map, 0, pkt->map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->dmat, pkt->map);
		rq->pending--;

		frag_len = (len > rq->fragsize) ? rq->fragsize : len;
		pkt->mbuf->m_len = frag_len;

		if (tail != NULL) {
			/* additional fragments */
			pkt->mbuf->m_flags &= ~M_PKTHDR;
			tail->m_next = pkt->mbuf;
			tail = pkt->mbuf;
		} else {
			/* first fragment, fill out much of the packet header */
			pkt->mbuf->m_pkthdr.len = len;
			pkt->mbuf->m_pkthdr.csum_flags = 0;
			if (cqe->u0.s.ip_cksum_pass) {
				if (!cqe->u0.s.ip_ver) { /* IPV4 */
					pkt->mbuf->m_pkthdr.csum_flags =
					    M_IPV4_CSUM_IN_OK;
				}
			}
			if (cqe->u0.s.l4_cksum_pass) {
				pkt->mbuf->m_pkthdr.csum_flags |=
				    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
			}
			m = tail = pkt->mbuf;
		}
		pkt->mbuf = NULL;
		oce_pkt_put(&rq->pkt_free, pkt);
		len -= frag_len;
	}

	if (m) {
		if (!oce_cqe_portid_valid(sc, cqe)) {
			 m_freem(m);
			 goto exit;
		}

		/* Account for jumbo chains */
		m_cluncount(m, 1);

		m->m_pkthdr.rcvif = ifp;

#if NVLAN > 0
		/* This determines if vlan tag is valid */
		if (oce_cqe_vtp_valid(sc, cqe)) {
			if (sc->function_mode & FNM_FLEX10_MODE) {
				/* FLEX10. If QnQ is not set, neglect VLAN */
				if (cqe->u0.s.qnq) {
					m->m_pkthdr.ether_vtag = vtag;
					m->m_flags |= M_VLANTAG;
				}
			} else if (sc->pvid != (vtag & VLAN_VID_MASK))  {
				/*
				 * In UMC mode generally pvid will be striped by
				 * hw. But in some cases we have seen it comes
				 * with pvid. So if pvid == vlan, neglect vlan.
				 */
				m->m_pkthdr.ether_vtag = vtag;
				m->m_flags |= M_VLANTAG;
			}
		}
#endif

		ifp->if_ipackets++;

#ifdef OCE_LRO
#if defined(INET6) || defined(INET)
		/* Try to queue to LRO */
		if (IF_LRO_ENABLED(ifp) && !(m->m_flags & M_VLANTAG) &&
		    cqe->u0.s.ip_cksum_pass && cqe->u0.s.l4_cksum_pass &&
		    !cqe->u0.s.ip_ver && rq->lro.lro_cnt != 0) {

			if (tcp_lro_rx(&rq->lro, m, 0) == 0) {
				rq->lro_pkts_queued ++;
				goto exit;
			}
			/* If LRO posting fails then try to post to STACK */
		}
#endif
#endif /* OCE_LRO */

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_IN);
#endif

		ether_input_mbuf(ifp, m);
	}
exit:
	return;
}

void
oce_discard_rx_comp(struct oce_rq *rq, struct oce_nic_rx_cqe *cqe)
{
	struct oce_softc *sc = rq->sc;
	struct oce_pkt *pkt;
	int i, num_frags = cqe->u0.s.num_fragments;

	if (IS_XE201(sc) && cqe->u0.s.error) {
		/*
		 * Lancer A0 workaround:
		 * num_frags will be 1 more than actual in case of error
		 */
		if (num_frags)
			num_frags--;
	}
	for (i = 0; i < num_frags; i++) {
		if ((pkt = oce_pkt_get(&rq->pkt_list)) == NULL) {
			printf("%s: missing descriptor in discard_rx_comp\n",
			    sc->dev.dv_xname);
			return;
		}
		bus_dmamap_sync(sc->dmat, pkt->map, 0, pkt->map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->dmat, pkt->map);
		rq->pending--;
		m_freem(pkt->mbuf);
		oce_pkt_put(&rq->pkt_free, pkt);
	}
}

int
oce_cqe_vtp_valid(struct oce_softc *sc, struct oce_nic_rx_cqe *cqe)
{
	struct oce_nic_rx_cqe_v1 *cqe_v1;

	if (IS_BE(sc) && ISSET(sc->flags, OCE_F_BE3_NATIVE)) {
		cqe_v1 = (struct oce_nic_rx_cqe_v1 *)cqe;
		return (cqe_v1->u0.s.vlan_tag_present);
	}
	return (cqe->u0.s.vlan_tag_present);
}

int
oce_cqe_portid_valid(struct oce_softc *sc, struct oce_nic_rx_cqe *cqe)
{
	struct oce_nic_rx_cqe_v1 *cqe_v1;

	if (IS_BE(sc) && ISSET(sc->flags, OCE_F_BE3_NATIVE)) {
		cqe_v1 = (struct oce_nic_rx_cqe_v1 *)cqe;
		if (sc->port_id != cqe_v1->u0.s.port)
			return (0);
	}
	return (1);
}

#ifdef OCE_LRO
#if defined(INET6) || defined(INET)
void
oce_rx_flush_lro(struct oce_rq *rq)
{
	struct oce_softc *sc = rq->sc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct lro_ctrl	*lro = &rq->lro;
	struct lro_entry *queued;

	if (!IF_LRO_ENABLED(ifp))
		return;

	while ((queued = SLIST_FIRST(&lro->lro_active)) != NULL) {
		SLIST_REMOVE_HEAD(&lro->lro_active, next);
		tcp_lro_flush(lro, queued);
	}
	rq->lro_pkts_queued = 0;

	return;
}

int
oce_init_lro(struct oce_softc *sc)
{
	struct lro_ctrl *lro = NULL;
	int i = 0, rc = 0;

	for (i = 0; i < sc->nrqs; i++) {
		lro = &sc->rq[i]->lro;
		rc = tcp_lro_init(lro);
		if (rc != 0) {
			printf("%s: LRO init failed\n");
			return rc;
		}
		lro->ifp = &sc->arpcom.ac_if;
	}

	return rc;
}

void
oce_free_lro(struct oce_softc *sc)
{
	struct lro_ctrl *lro = NULL;
	int i = 0;

	for (i = 0; i < sc->nrqs; i++) {
		lro = &sc->rq[i]->lro;
		if (lro)
			tcp_lro_free(lro);
	}
}
#endif /* INET6 || INET */
#endif /* OCE_LRO */

int
oce_get_buf(struct oce_rq *rq)
{
	struct oce_softc *sc = rq->sc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct oce_pkt *pkt;
	struct oce_nic_rqe *rqe;

	if ((pkt = oce_pkt_get(&rq->pkt_free)) == NULL)
		return (0);

	pkt->mbuf = MCLGETI(NULL, M_DONTWAIT, ifp, MCLBYTES);
	if (pkt->mbuf == NULL) {
		oce_pkt_put(&rq->pkt_free, pkt);
		return (0);
	}

	pkt->mbuf->m_len = pkt->mbuf->m_pkthdr.len = MCLBYTES;

	if (bus_dmamap_load_mbuf(sc->dmat, pkt->map, pkt->mbuf,
	    BUS_DMA_NOWAIT)) {
		m_freem(pkt->mbuf);
		pkt->mbuf = NULL;
		oce_pkt_put(&rq->pkt_free, pkt);
		return (0);
	}

	bus_dmamap_sync(sc->dmat, pkt->map, 0, pkt->map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	oce_dma_sync(&rq->ring->dma, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	rqe = oce_ring_get(rq->ring);
	rqe->u0.s.frag_pa_hi = ADDR_HI(pkt->map->dm_segs[0].ds_addr);
	rqe->u0.s.frag_pa_lo = ADDR_LO(pkt->map->dm_segs[0].ds_addr);
	DW_SWAP(u32ptr(rqe), sizeof(struct oce_nic_rqe));
	rq->pending++;

	oce_dma_sync(&rq->ring->dma, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);

	oce_pkt_put(&rq->pkt_list, pkt);

	return (1);
}

int
oce_alloc_rx_bufs(struct oce_rq *rq)
{
	struct oce_softc *sc = rq->sc;
	int i, nbufs = 0;

	while (oce_get_buf(rq))
		nbufs++;
	if (!nbufs)
		return (0);
	for (i = nbufs / OCE_MAX_RQ_POSTS; i > 0; i--) {
		oce_write_db(sc, PD_RXULP_DB, rq->id |
		    (OCE_MAX_RQ_POSTS << 24));
		nbufs -= OCE_MAX_RQ_POSTS;
	}
	if (nbufs > 0)
		oce_write_db(sc, PD_RXULP_DB, rq->id | (nbufs << 24));
	return (1);
}

void
oce_refill_rx(void *arg)
{
	struct oce_softc *sc = arg;
	struct oce_rq *rq;
	int i, s;

	s = splnet();
	for_all_rq_queues(sc, rq, i) {
		oce_alloc_rx_bufs(rq);
		if (!rq->pending)
			timeout_add(&sc->rxrefill, 1);
	}
	splx(s);
}

/* Handle the Completion Queue for receive */
void
oce_intr_rq(void *arg)
{
	struct oce_rq *rq = (struct oce_rq *)arg;
	struct oce_cq *cq = rq->cq;
	struct oce_softc *sc = rq->sc;
	struct oce_nic_rx_cqe *cqe;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int max_rsp, ncqe = 0;

	max_rsp = IS_XE201(sc) ? 8 : OCE_MAX_RSP_HANDLED;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);

	OCE_RING_FOREACH(cq->ring, cqe, RQ_CQE_VALID(cqe) && ncqe <= max_rsp) {
		DW_SWAP((uint32_t *)cqe, sizeof(oce_rq_cqe));

		if (cqe->u0.s.error == 0) {
			oce_rxeof(rq, cqe);
		} else {
			ifp->if_ierrors++;
			if (IS_XE201(sc))
				/* Lancer A0 no buffer workaround */
				oce_discard_rx_comp(rq, cqe);
			else
				/* Post L3/L4 errors to stack.*/
				oce_rxeof(rq, cqe);
		}

#ifdef OCE_LRO
#if defined(INET6) || defined(INET)
		if (IF_LRO_ENABLED(ifp) && rq->lro_pkts_queued >= 16)
			oce_rx_flush_lro(rq);
#endif
#endif

		RQ_CQE_INVALIDATE(cqe);
		oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_PREWRITE);
		ncqe++;
	}

#ifdef OCE_LRO
#if defined(INET6) || defined(INET)
	if (IF_LRO_ENABLED(ifp))
		oce_rx_flush_lro(rq);
#endif
#endif

	if (ncqe) {
		oce_arm_cq(cq, ncqe, FALSE);
		if (rq->nitems - rq->pending > 1 && !oce_alloc_rx_bufs(rq))
			timeout_add(&sc->rxrefill, 1);
	}
}

void
oce_set_macaddr(struct oce_softc *sc)
{
	uint32_t old_pmac_id = sc->pmac_id;
	int status = 0;

	if (!bcmp(sc->macaddr, sc->arpcom.ac_enaddr, ETHER_ADDR_LEN))
		return;

	status = oce_macaddr_add(sc, sc->arpcom.ac_enaddr, sc->if_id,
	    &sc->pmac_id);
	if (!status)
		status = oce_macaddr_del(sc, sc->if_id, old_pmac_id);
	else
		printf("%s: failed to set MAC address\n", sc->dev.dv_xname);
}

void
oce_tick(void *arg)
{
	struct oce_softc *sc = arg;
	int s;

	s = splnet();

	if (oce_update_stats(sc) == 0)
		timeout_add_sec(&sc->timer, 1);

	splx(s);
}

void
oce_stop(struct oce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct oce_rq *rq;
	struct oce_wq *wq;
	struct oce_eq *eq;
	int i;

	timeout_del(&sc->timer);

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	/* Stop intrs and finish any bottom halves pending */
	oce_intr_disable(sc);

	/* Invalidate any pending cq and eq entries */
	for_all_eq_queues(sc, eq, i)
		oce_drain_eq(eq);
	for_all_rq_queues(sc, rq, i) {
		oce_destroy_queue(sc, QTYPE_RQ, rq->id);
		DELAY(1000);
		oce_drain_rq(rq);
		oce_free_posted_rxbuf(rq);
	}
	for_all_wq_queues(sc, wq, i)
		oce_drain_wq(wq);
}

void
oce_init(void *arg)
{
	struct oce_softc *sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct oce_eq *eq;
	struct oce_rq *rq;
	struct oce_wq *wq;
	int i;

	oce_stop(sc);

	DELAY(10);

	oce_set_macaddr(sc);

	oce_iff(sc);

	for_all_rq_queues(sc, rq, i) {
		rq->mtu = ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN +
		    ETHER_VLAN_ENCAP_LEN;
		if (oce_new_rq(sc, rq)) {
			printf("%s: failed to create rq\n", sc->dev.dv_xname);
			goto error;
		}
		rq->pending	 = 0;
		rq->ring->index	 = 0;

		if (!oce_alloc_rx_bufs(rq)) {
			printf("%s: failed to allocate rx buffers\n",
			    sc->dev.dv_xname);
			goto error;
		}
	}

#ifdef OCE_RSS
	/* RSS config */
	if (sc->rss_enable) {
		if (oce_config_rss(sc, (uint8_t)sc->if_id, 1)) {
			printf("%s: failed to configure RSS\n",
			    sc->dev.dv_xname);
			goto error;
		}
	}
#endif

	for_all_rq_queues(sc, rq, i)
		oce_arm_cq(rq->cq, 0, TRUE);

	for_all_wq_queues(sc, wq, i)
		oce_arm_cq(wq->cq, 0, TRUE);

	oce_arm_cq(sc->mq->cq, 0, TRUE);

	for_all_eq_queues(sc, eq, i)
		oce_arm_eq(eq, 0, TRUE, FALSE);

	if (oce_get_link_status(sc) == 0)
		oce_update_link_status(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	timeout_add_sec(&sc->timer, 1);

	oce_intr_enable(sc);

	return;
error:
	oce_stop(sc);
}

void
oce_link_event(struct oce_softc *sc, struct oce_async_cqe_link_state *acqe)
{
	/* Update Link status */
	if ((acqe->u0.s.link_status & ~ASYNC_EVENT_LOGICAL) ==
	     ASYNC_EVENT_LINK_UP)
		sc->link_status = ASYNC_EVENT_LINK_UP;
	else
		sc->link_status = ASYNC_EVENT_LINK_DOWN;

	/* Update speed */
	sc->link_speed = acqe->u0.s.speed;
	sc->qos_link_speed = (uint32_t) acqe->u0.s.qos_link_speed * 10;

	oce_update_link_status(sc);
}

/* Handle the Completion Queue for the Mailbox/Async notifications */
void
oce_intr_mq(void *arg)
{
	struct oce_mq *mq = (struct oce_mq *)arg;
	struct oce_softc *sc = mq->sc;
	struct oce_cq *cq = mq->cq;
	struct oce_mq_cqe *cqe;
	struct oce_async_cqe_link_state *acqe;
	struct oce_async_event_grp5_pvid_state *gcqe;
	int evtype, optype, ncqe = 0;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);

	OCE_RING_FOREACH(cq->ring, cqe, MQ_CQE_VALID(cqe)) {
		DW_SWAP((uint32_t *) cqe, sizeof(oce_mq_cqe));
		if (cqe->u0.s.async_event) {
			evtype = cqe->u0.s.event_type;
			optype = cqe->u0.s.async_type;
			if (evtype  == ASYNC_EVENT_CODE_LINK_STATE) {
				/* Link status evt */
				acqe = (struct oce_async_cqe_link_state *)cqe;
				oce_link_event(sc, acqe);
			} else if ((evtype == ASYNC_EVENT_GRP5) &&
				   (optype == ASYNC_EVENT_PVID_STATE)) {
				/* GRP5 PVID */
				gcqe =
				(struct oce_async_event_grp5_pvid_state *)cqe;
				if (gcqe->enabled)
					sc->pvid = gcqe->tag & VLAN_VID_MASK;
				else
					sc->pvid = 0;
			}
		}
		MQ_CQE_INVALIDATE(cqe);
		oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_PREWRITE);
		ncqe++;
	}

	if (ncqe)
		oce_arm_cq(cq, ncqe, FALSE /* TRUE */);
}

int
oce_init_queues(struct oce_softc *sc)
{
	struct oce_wq *wq;
	struct oce_rq *rq;
	int i;

	sc->nrqs = 1;
	sc->nwqs = 1;

	/* Create network interface on card */
	if (oce_create_iface(sc, sc->macaddr))
		goto error;

	/* create all of the event queues */
	for (i = 0; i < sc->intr_count; i++) {
		sc->eq[i] = oce_create_eq(sc);
		if (!sc->eq[i])
			goto error;
	}

	/* alloc tx queues */
	for_all_wq_queues(sc, wq, i) {
		sc->wq[i] = oce_create_wq(sc, sc->eq[i]);
		if (!sc->wq[i])
			goto error;
	}

	/* alloc rx queues */
	for_all_rq_queues(sc, rq, i) {
		sc->rq[i] = oce_create_rq(sc, sc->eq[i == 0 ? 0 : i - 1],
		    sc->if_id, (i == 0) ? 0 : sc->rss_enable);
		if (!sc->rq[i])
			goto error;
	}

	/* alloc mailbox queue */
	sc->mq = oce_create_mq(sc, sc->eq[0]);
	if (!sc->mq)
		goto error;

	return (0);
error:
	oce_release_queues(sc);
	return (1);
}

void
oce_release_queues(struct oce_softc *sc)
{
	struct oce_wq *wq;
	struct oce_rq *rq;
	struct oce_eq *eq;
	int i;

	for_all_rq_queues(sc, rq, i) {
		if (rq)
			oce_destroy_rq(sc->rq[i]);
	}

	for_all_wq_queues(sc, wq, i) {
		if (wq)
			oce_destroy_wq(sc->wq[i]);
	}

	if (sc->mq)
		oce_destroy_mq(sc->mq);

	for_all_eq_queues(sc, eq, i) {
		if (eq)
			oce_destroy_eq(sc->eq[i]);
	}
}

/**
 * @brief 		Function to create a WQ for NIC Tx
 * @param sc 		software handle to the device
 * @returns		the pointer to the WQ created or NULL on failure
 */
struct oce_wq *
oce_create_wq(struct oce_softc *sc, struct oce_eq *eq)
{
	struct oce_wq *wq;
	struct oce_cq *cq;
	struct oce_pkt *pkt;
	int i;

	if (sc->tx_ring_size < 256 || sc->tx_ring_size > 2048)
		return (NULL);

	wq = malloc(sizeof(struct oce_wq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!wq)
		return (NULL);

	wq->ring = oce_create_ring(sc, sc->tx_ring_size, NIC_WQE_SIZE, 8);
	if (!wq->ring) {
		free(wq, M_DEVBUF);
		return (NULL);
	}

	cq = oce_create_cq(sc, eq, CQ_LEN_512, sizeof(struct oce_nic_tx_cqe),
	    1, 0, 3);
	if (!cq) {
		oce_destroy_ring(sc, wq->ring);
		free(wq, M_DEVBUF);
		return (NULL);
	}

	wq->id = -1;
	wq->sc = sc;

	wq->cq = cq;
	wq->nitems = sc->tx_ring_size;

	SIMPLEQ_INIT(&wq->pkt_free);
	SIMPLEQ_INIT(&wq->pkt_list);

	for (i = 0; i < sc->tx_ring_size / 2; i++) {
		pkt = oce_pkt_alloc(sc, OCE_MAX_TX_SIZE, OCE_MAX_TX_ELEMENTS,
		    PAGE_SIZE);
		if (pkt == NULL) {
			oce_destroy_wq(wq);
			return (NULL);
		}
		oce_pkt_put(&wq->pkt_free, pkt);
	}

	if (oce_new_wq(sc, wq)) {
		oce_destroy_wq(wq);
		return (NULL);
	}

	eq->cq[eq->cq_valid] = cq;
	eq->cq_valid++;
	cq->cb_arg = wq;
	cq->cq_intr = oce_intr_wq;

	return (wq);
}

void
oce_destroy_wq(struct oce_wq *wq)
{
	struct oce_softc *sc = wq->sc;
	struct oce_pkt *pkt;

	if (wq->id >= 0)
		oce_destroy_queue(sc, QTYPE_WQ, wq->id);
	if (wq->cq != NULL)
		oce_destroy_cq(wq->cq);
	if (wq->ring != NULL)
		oce_destroy_ring(sc, wq->ring);
	while ((pkt = oce_pkt_get(&wq->pkt_free)) != NULL)
		oce_pkt_free(sc, pkt);
	free(wq, M_DEVBUF);
}

/**
 * @brief 		function to allocate receive queue resources
 * @param sc		software handle to the device
 * @param eq		pointer to associated event queue
 * @param if_id		interface identifier index
 * @param rss		is-rss-queue flag
 * @returns		the pointer to the RQ created or NULL on failure
 */
struct oce_rq *
oce_create_rq(struct oce_softc *sc, struct oce_eq *eq, uint32_t if_id,
    uint32_t rss)
{
	struct oce_rq *rq;
	struct oce_cq *cq;
	struct oce_pkt *pkt;
	int i;

	if (ilog2(sc->rx_frag_size) <= 0)
		return (NULL);

	/* Hardware doesn't support any other value */
	if (sc->rx_ring_size != 1024)
		return (NULL);

	rq = malloc(sizeof(struct oce_rq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!rq)
		return (NULL);

	rq->ring = oce_create_ring(sc, sc->rx_ring_size,
	    sizeof(struct oce_nic_rqe), 2);
	if (!rq->ring) {
		free(rq, M_DEVBUF);
		return (NULL);
	}

	cq = oce_create_cq(sc, eq, CQ_LEN_1024, sizeof(struct oce_nic_rx_cqe),
	    1, 0, 3);
	if (!cq) {
		oce_destroy_ring(sc, rq->ring);
		free(rq, M_DEVBUF);
		return (NULL);
	}

	rq->id = -1;
	rq->sc = sc;

	rq->nitems = sc->rx_ring_size;
	rq->fragsize = sc->rx_frag_size;
	rq->rss = rss;

	SIMPLEQ_INIT(&rq->pkt_free);
	SIMPLEQ_INIT(&rq->pkt_list);

	for (i = 0; i < sc->rx_ring_size; i++) {
		pkt = oce_pkt_alloc(sc, sc->rx_frag_size, 1, sc->rx_frag_size);
		if (pkt == NULL) {
			oce_destroy_rq(rq);
			return (NULL);
		}
		oce_pkt_put(&rq->pkt_free, pkt);
	}

	rq->cq = cq;
	eq->cq[eq->cq_valid] = cq;
	eq->cq_valid++;
	cq->cb_arg = rq;
	cq->cq_intr = oce_intr_rq;

	/* RX queue is created in oce_init */

	return (rq);
}

void
oce_destroy_rq(struct oce_rq *rq)
{
	struct oce_softc *sc = rq->sc;
	struct oce_pkt *pkt;

	if (rq->id >= 0)
		oce_destroy_queue(sc, QTYPE_RQ, rq->id);
	if (rq->cq != NULL)
		oce_destroy_cq(rq->cq);
	if (rq->ring != NULL)
		oce_destroy_ring(sc, rq->ring);
	while ((pkt = oce_pkt_get(&rq->pkt_free)) != NULL)
		oce_pkt_free(sc, pkt);
	free(rq, M_DEVBUF);
}

struct oce_eq *
oce_create_eq(struct oce_softc *sc)
{
	struct oce_eq *eq;

	/* allocate an eq */
	eq = malloc(sizeof(struct oce_eq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (eq == NULL)
		return (NULL);

	eq->ring = oce_create_ring(sc, EQ_LEN_1024, EQE_SIZE_4, 8);
	if (!eq->ring) {
		free(eq, M_DEVBUF);
		return (NULL);
	}

	eq->id = -1;
	eq->sc = sc;
	eq->nitems = EQ_LEN_1024;	/* length of event queue */
	eq->isize = EQE_SIZE_4; 	/* size of a queue item */
	eq->delay = OCE_DEFAULT_EQD;	/* event queue delay */

	if (oce_new_eq(sc, eq)) {
		oce_destroy_ring(sc, eq->ring);
		free(eq, M_DEVBUF);
		return (NULL);
	}

	return (eq);
}

void
oce_destroy_eq(struct oce_eq *eq)
{
	struct oce_softc *sc = eq->sc;

	if (eq->id >= 0)
		oce_destroy_queue(sc, QTYPE_EQ, eq->id);
	if (eq->ring != NULL)
		oce_destroy_ring(sc, eq->ring);
	free(eq, M_DEVBUF);
}

struct oce_mq *
oce_create_mq(struct oce_softc *sc, struct oce_eq *eq)
{
	struct oce_mq *mq = NULL;
	struct oce_cq *cq;

	/* allocate the mq */
	mq = malloc(sizeof(struct oce_mq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!mq)
		return (NULL);

	mq->ring = oce_create_ring(sc, 128, sizeof(struct oce_mbx), 8);
	if (!mq->ring) {
		free(mq, M_DEVBUF);
		return (NULL);
	}

	cq = oce_create_cq(sc, eq, CQ_LEN_256, sizeof(struct oce_mq_cqe),
	    1, 0, 0);
	if (!cq) {
		oce_destroy_ring(sc, mq->ring);
		free(mq, M_DEVBUF);
		return (NULL);
	}

	mq->id = -1;
	mq->sc = sc;
	mq->cq = cq;

	mq->nitems = 128;

	if (oce_new_mq(sc, mq)) {
		oce_destroy_cq(mq->cq);
		oce_destroy_ring(sc, mq->ring);
		free(mq, M_DEVBUF);
		return (NULL);
	}

	eq->cq[eq->cq_valid] = cq;
	eq->cq_valid++;
	mq->cq->eq = eq;
	mq->cq->cb_arg = mq;
	mq->cq->cq_intr = oce_intr_mq;

	return (mq);
}

void
oce_destroy_mq(struct oce_mq *mq)
{
	struct oce_softc *sc = mq->sc;

	if (mq->id >= 0)
		oce_destroy_queue(sc, QTYPE_MQ, mq->id);
	if (mq->ring != NULL)
		oce_destroy_ring(sc, mq->ring);
	if (mq->cq != NULL)
		oce_destroy_cq(mq->cq);
	free(mq, M_DEVBUF);
}

/**
 * @brief		Function to create a completion queue
 * @param sc		software handle to the device
 * @param eq		optional eq to be associated with to the cq
 * @param q_len		length of completion queue
 * @param item_size	size of completion queue items
 * @param is_eventable	event table
 * @param nodelay	no delay flag
 * @param ncoalesce	no coalescence flag
 * @returns 		pointer to the cq created, NULL on failure
 */
struct oce_cq *
oce_create_cq(struct oce_softc *sc, struct oce_eq *eq, uint32_t q_len,
    uint32_t item_size, uint32_t eventable, uint32_t nodelay,
    uint32_t ncoalesce)
{
	struct oce_cq *cq = NULL;

	cq = malloc(sizeof(struct oce_cq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!cq)
		return (NULL);

	cq->ring = oce_create_ring(sc, q_len, item_size, 4);
	if (!cq->ring) {
		free(cq, M_DEVBUF);
		return (NULL);
	}

	cq->sc = sc;
	cq->eq = eq;
	cq->nitems = q_len;
	cq->nodelay = (uint8_t) nodelay;
	cq->ncoalesce = ncoalesce;
	cq->eventable = eventable;

	if (oce_new_cq(sc, cq)) {
		oce_destroy_ring(sc, cq->ring);
		free(cq, M_DEVBUF);
		return (NULL);
	}

	sc->cq[sc->ncqs++] = cq;

	return (cq);
}

void
oce_destroy_cq(struct oce_cq *cq)
{
	struct oce_softc *sc = cq->sc;

	if (cq->ring != NULL) {
		oce_destroy_queue(sc, QTYPE_CQ, cq->id);
		oce_destroy_ring(sc, cq->ring);
	}
	free(cq, M_DEVBUF);
}

/**
 * @brief		Function to arm an EQ so that it can generate events
 * @param eq		pointer to event queue structure
 * @param neqe		number of EQEs to arm
 * @param rearm		rearm bit enable/disable
 * @param clearint	bit to clear the interrupt condition because of which
 *			EQEs are generated
 */
static inline void
oce_arm_eq(struct oce_eq *eq, int neqe, int rearm, int clearint)
{
	oce_write_db(eq->sc, PD_EQ_DB, eq->id | PD_EQ_DB_EVENT |
	    (clearint << 9) | (neqe << 16) | (rearm << 29));
}

/**
 * @brief		Function to arm a CQ with CQEs
 * @param cq		pointer to the completion queue structure
 * @param ncqe		number of CQEs to arm
 * @param rearm		rearm bit enable/disable
 */
static inline void
oce_arm_cq(struct oce_cq *cq, int ncqe, int rearm)
{
	oce_write_db(cq->sc, PD_CQ_DB, cq->id | (ncqe << 16) | (rearm << 29));
}

/**
 * @brief		function to cleanup the eqs used during stop
 * @param eq		pointer to event queue structure
 * @returns		the number of EQs processed
 */
void
oce_drain_eq(struct oce_eq *eq)
{
	struct oce_eqe *eqe;
	int neqe = 0;

	oce_dma_sync(&eq->ring->dma, BUS_DMASYNC_POSTREAD);
	OCE_RING_FOREACH(eq->ring, eqe, eqe->evnt != 0) {
		eqe->evnt = 0;
		oce_dma_sync(&eq->ring->dma, BUS_DMASYNC_POSTWRITE);
		neqe++;
	}
	oce_arm_eq(eq, neqe, FALSE, TRUE);
}

void
oce_drain_wq(struct oce_wq *wq)
{
	struct oce_cq *cq = wq->cq;
	struct oce_nic_tx_cqe *cqe;
	int ncqe = 0;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);
	OCE_RING_FOREACH(cq->ring, cqe, WQ_CQE_VALID(cqe)) {
		WQ_CQE_INVALIDATE(cqe);
		oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTWRITE);
		ncqe++;
	}
	oce_arm_cq(cq, ncqe, FALSE);
}

void
oce_drain_mq(struct oce_mq *mq)
{
	struct oce_cq *cq = mq->cq;
	struct oce_mq_cqe *cqe;
	int ncqe = 0;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);
	OCE_RING_FOREACH(cq->ring, cqe, MQ_CQE_VALID(cqe)) {
		MQ_CQE_INVALIDATE(cqe);
		oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTWRITE);
		ncqe++;
	}
	oce_arm_cq(cq, ncqe, FALSE);
}

void
oce_drain_rq(struct oce_rq *rq)
{
	struct oce_nic_rx_cqe *cqe;
	struct oce_cq *cq = rq->cq;
	int ncqe = 0;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);
	OCE_RING_FOREACH(cq->ring, cqe, RQ_CQE_VALID(cqe)) {
		RQ_CQE_INVALIDATE(cqe);
		oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTWRITE);
		ncqe++;
	}
	oce_arm_cq(cq, ncqe, FALSE);
}

void
oce_free_posted_rxbuf(struct oce_rq *rq)
{
	struct oce_softc *sc = rq->sc;
	struct oce_pkt *pkt;

	while ((pkt = oce_pkt_get(&rq->pkt_list)) != NULL) {
		bus_dmamap_sync(sc->dmat, pkt->map, 0, pkt->map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->dmat, pkt->map);
		if (pkt->mbuf != NULL) {
			m_freem(pkt->mbuf);
			pkt->mbuf = NULL;
		}
		oce_pkt_put(&rq->pkt_free, pkt);
		rq->pending--;
	}
}

int
oce_dma_alloc(struct oce_softc *sc, bus_size_t size, struct oce_dma_mem *dma)
{
	int rc;

	bzero(dma, sizeof(struct oce_dma_mem));

	dma->tag = sc->dmat;
	rc = bus_dmamap_create(dma->tag, size, 1, size, 0, BUS_DMA_NOWAIT,
	    &dma->map);
	if (rc != 0) {
		printf("%s: failed to allocate DMA handle", sc->dev.dv_xname);
		goto fail_0;
	}

	rc = bus_dmamem_alloc(dma->tag, size, PAGE_SIZE, 0, &dma->segs, 1,
	    &dma->nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (rc != 0) {
		printf("%s: failed to allocate DMA memory", sc->dev.dv_xname);
		goto fail_1;
	}

	rc = bus_dmamem_map(dma->tag, &dma->segs, dma->nsegs, size,
	    &dma->vaddr, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: failed to map DMA memory", sc->dev.dv_xname);
		goto fail_2;
	}

	rc = bus_dmamap_load(dma->tag, dma->map, dma->vaddr, size, NULL,
	    BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: failed to load DMA memory", sc->dev.dv_xname);
		goto fail_3;
	}

	bus_dmamap_sync(dma->tag, dma->map, 0, dma->map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	dma->paddr = dma->map->dm_segs[0].ds_addr;
	dma->size = size;

	return (0);

fail_3:
	bus_dmamem_unmap(dma->tag, dma->vaddr, size);
fail_2:
	bus_dmamem_free(dma->tag, &dma->segs, dma->nsegs);
fail_1:
	bus_dmamap_destroy(dma->tag, dma->map);
fail_0:
	return (rc);
}

void
oce_dma_free(struct oce_softc *sc, struct oce_dma_mem *dma)
{
	if (dma->tag == NULL)
		return;

	if (dma->map != NULL) {
		oce_dma_sync(dma, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(dma->tag, dma->map);

		if (dma->vaddr != 0) {
			bus_dmamem_free(dma->tag, &dma->segs, dma->nsegs);
			dma->vaddr = 0;
		}

		bus_dmamap_destroy(dma->tag, dma->map);
		dma->map = NULL;
		dma->tag = NULL;
	}
}

struct oce_ring *
oce_create_ring(struct oce_softc *sc, int nitems, int isize, int maxsegs)
{
	struct oce_dma_mem *dma;
	struct oce_ring *ring;
	bus_size_t size = nitems * isize;
	int rc;

	if (size > maxsegs * PAGE_SIZE)
		return (NULL);

	ring = malloc(sizeof(struct oce_ring), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ring == NULL)
		return (NULL);

	ring->isize = isize;
	ring->nitems = nitems;

	dma = &ring->dma;
	dma->tag = sc->dmat;
	rc = bus_dmamap_create(dma->tag, size, maxsegs, PAGE_SIZE, 0,
	    BUS_DMA_NOWAIT, &dma->map);
	if (rc != 0) {
		printf("%s: failed to allocate DMA handle", sc->dev.dv_xname);
		goto fail_0;
	}

	rc = bus_dmamem_alloc(dma->tag, size, 0, 0, &dma->segs, maxsegs,
	    &dma->nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (rc != 0) {
		printf("%s: failed to allocate DMA memory", sc->dev.dv_xname);
		goto fail_1;
	}

	rc = bus_dmamem_map(dma->tag, &dma->segs, dma->nsegs, size,
	    &dma->vaddr, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: failed to map DMA memory", sc->dev.dv_xname);
		goto fail_2;
	}

	bus_dmamap_sync(dma->tag, dma->map, 0, dma->map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	dma->paddr = 0;
	dma->size = size;

	return (ring);

fail_2:
	bus_dmamem_free(dma->tag, &dma->segs, dma->nsegs);
fail_1:
	bus_dmamap_destroy(dma->tag, dma->map);
fail_0:
	free(ring, M_DEVBUF);
	return (NULL);
}

void
oce_destroy_ring(struct oce_softc *sc, struct oce_ring *ring)
{
	oce_dma_free(sc, &ring->dma);
	free(ring, M_DEVBUF);
}

int
oce_load_ring(struct oce_softc *sc, struct oce_ring *ring,
    struct phys_addr *pa_list, int maxsegs)
{
	struct oce_dma_mem *dma = &ring->dma;
	int i;

	if (bus_dmamap_load(dma->tag, dma->map, dma->vaddr,
	    ring->isize * ring->nitems, NULL, BUS_DMA_NOWAIT)) {
		printf("%s: failed to load a ring map\n", sc->dev.dv_xname);
		return (0);
	}

	if (dma->map->dm_nsegs > maxsegs) {
		printf("%s: too many segments", sc->dev.dv_xname);
		return (0);
	}

	bus_dmamap_sync(dma->tag, dma->map, 0, dma->map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	for (i = 0; i < dma->map->dm_nsegs; i++) {
		pa_list[i].lo = ADDR_LO(dma->map->dm_segs[i].ds_addr);
		pa_list[i].hi = ADDR_HI(dma->map->dm_segs[i].ds_addr);
	}

	return (dma->map->dm_nsegs);
}

static inline void *
oce_ring_get(struct oce_ring *ring)
{
	int index = ring->index;

	if (++ring->index == ring->nitems)
		ring->index = 0;
	return ((void *)(ring->dma.vaddr + index * ring->isize));
}

static inline void *
oce_ring_first(struct oce_ring *ring)
{
	return ((void *)(ring->dma.vaddr + ring->index * ring->isize));
}

static inline void *
oce_ring_next(struct oce_ring *ring)
{
	if (++ring->index == ring->nitems)
		ring->index = 0;
	return ((void *)(ring->dma.vaddr + ring->index * ring->isize));
}

struct oce_pkt *
oce_pkt_alloc(struct oce_softc *sc, size_t size, int nsegs, int maxsegs)
{
	struct oce_pkt *pkt;

	if ((pkt = pool_get(oce_pkt_pool, PR_NOWAIT)) == NULL)
		return (NULL);

	if (bus_dmamap_create(sc->dmat, size, nsegs, maxsegs, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &pkt->map)) {
		pool_put(oce_pkt_pool, pkt);
		return (NULL);
	}

	return (pkt);
}

void
oce_pkt_free(struct oce_softc *sc, struct oce_pkt *pkt)
{
	if (pkt->map) {
		bus_dmamap_unload(sc->dmat, pkt->map);
		bus_dmamap_destroy(sc->dmat, pkt->map);
	}
	pool_put(oce_pkt_pool, pkt);
}

static inline struct oce_pkt *
oce_pkt_get(struct oce_pkt_list *lst)
{
	struct oce_pkt *pkt;

	pkt = SIMPLEQ_FIRST(lst);
	if (pkt == NULL)
		return (NULL);

	SIMPLEQ_REMOVE_HEAD(lst, entry);

	return (pkt);
}

static inline void
oce_pkt_put(struct oce_pkt_list *lst, struct oce_pkt *pkt)
{
	SIMPLEQ_INSERT_TAIL(lst, pkt, entry);
}

/**
 * @brief Wait for FW to become ready and reset it
 * @param sc		software handle to the device
 */
int
oce_init_fw(struct oce_softc *sc)
{
	struct ioctl_common_function_reset cmd;
	uint32_t reg;
	int err = 0, tmo = 60000;

	/* read semaphore CSR */
	reg = oce_read_csr(sc, MPU_EP_SEMAPHORE(sc));

	/* if host is ready then wait for fw ready else send POST */
	if ((reg & MPU_EP_SEM_STAGE_MASK) <= POST_STAGE_AWAITING_HOST_RDY) {
		reg = (reg & ~MPU_EP_SEM_STAGE_MASK) | POST_STAGE_CHIP_RESET;
		oce_write_csr(sc, MPU_EP_SEMAPHORE(sc), reg);
	}

	/* wait for FW to become ready */
	for (;;) {
		if (--tmo == 0)
			break;

		DELAY(1000);

		reg = oce_read_csr(sc, MPU_EP_SEMAPHORE(sc));
		if (reg & MPU_EP_SEM_ERROR) {
			printf(": POST failed: %#x\n", reg);
			return (ENXIO);
		}
		if ((reg & MPU_EP_SEM_STAGE_MASK) == POST_STAGE_ARMFW_READY) {
			/* reset FW */
			if (ISSET(sc->flags, OCE_F_RESET_RQD)) {
				bzero(&cmd, sizeof(cmd));
				err = oce_cmd(sc, SUBSYS_COMMON,
				    OPCODE_COMMON_FUNCTION_RESET,
				    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
			}
			return (err);
		}
	}

	printf(": POST timed out: %#x\n", reg);

	return (ENXIO);
}

static inline int
oce_mbox_wait(struct oce_softc *sc)
{
	int i;

	for (i = 0; i < 20000; i++) {
		if (oce_read_db(sc, PD_MPU_MBOX_DB) & PD_MPU_MBOX_DB_READY)
			return (0);
		DELAY(100);
	}
	return (ETIMEDOUT);
}

/**
 * @brief Mailbox dispatch
 * @param sc		software handle to the device
 */
int
oce_mbox_dispatch(struct oce_softc *sc)
{
	uint32_t pa, reg;
	int err;

	pa = (uint32_t)((uint64_t)sc->bsmbx.paddr >> 34);
	reg = PD_MPU_MBOX_DB_HI | (pa << PD_MPU_MBOX_DB_ADDR_SHIFT);

	if ((err = oce_mbox_wait(sc)) != 0)
		goto out;

	oce_write_db(sc, PD_MPU_MBOX_DB, reg);

	pa = (uint32_t)((uint64_t)sc->bsmbx.paddr >> 4) & 0x3fffffff;
	reg = pa << PD_MPU_MBOX_DB_ADDR_SHIFT;

	if ((err = oce_mbox_wait(sc)) != 0)
		goto out;

	oce_write_db(sc, PD_MPU_MBOX_DB, reg);

	oce_dma_sync(&sc->bsmbx, BUS_DMASYNC_POSTWRITE);

	if ((err = oce_mbox_wait(sc)) != 0)
		goto out;

out:
	oce_dma_sync(&sc->bsmbx, BUS_DMASYNC_PREREAD);
	return (err);
}

/**
 * @brief Function to initialize the hw with host endian information
 * @param sc		software handle to the device
 * @returns		0 on success, ETIMEDOUT on failure
 */
int
oce_mbox_init(struct oce_softc *sc)
{
	struct oce_bmbx *bmbx = OCE_MEM_KVA(&sc->bsmbx);
	uint8_t *ptr = (uint8_t *)&bmbx->mbx;

	if (!ISSET(sc->flags, OCE_F_MBOX_ENDIAN_RQD))
		return (0);

	/* Endian Signature */
	*ptr++ = 0xff;
	*ptr++ = 0x12;
	*ptr++ = 0x34;
	*ptr++ = 0xff;
	*ptr++ = 0xff;
	*ptr++ = 0x56;
	*ptr++ = 0x78;
	*ptr = 0xff;

	return (oce_mbox_dispatch(sc));
}

int
oce_cmd(struct oce_softc *sc, int subsys, int opcode, int version,
    void *payload, int length)
{
	struct oce_bmbx *bmbx = OCE_MEM_KVA(&sc->bsmbx);
	struct oce_mbx *mbx = &bmbx->mbx;
	struct oce_dma_mem sgl;
	struct mbx_hdr *hdr;
	caddr_t epayload = NULL;
	int err;

	if (length > OCE_MBX_PAYLOAD) {
		if (oce_dma_alloc(sc, length, &sgl))
			return (-1);
		epayload = OCE_MEM_KVA(&sgl);
	}

	oce_dma_sync(&sc->bsmbx, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	bzero(mbx, sizeof(struct oce_mbx));

	mbx->payload_length = length;

	if (epayload) {
		mbx->u0.s.sge_count = 1;
		oce_dma_sync(&sgl, BUS_DMASYNC_PREWRITE);
		bcopy(payload, epayload, length);
		mbx->payload.u0.u1.sgl[0].paddr = sgl.paddr;
		mbx->payload.u0.u1.sgl[0].length = length;
		hdr = OCE_MEM_KVA(&sgl);
	} else {
		mbx->u0.s.embedded = 1;
		bcopy(payload, &mbx->payload, length);
		hdr = (struct mbx_hdr *)&mbx->payload;
	}

	hdr->u0.req.opcode = opcode;
	hdr->u0.req.subsystem = subsys;
	hdr->u0.req.request_length = length - sizeof(*hdr);
	hdr->u0.req.version = version;
	if (opcode == OPCODE_COMMON_FUNCTION_RESET)
		hdr->u0.req.timeout = 2 * MBX_TIMEOUT_SEC;
	else
		hdr->u0.req.timeout = MBX_TIMEOUT_SEC;

	err = oce_mbox_dispatch(sc);
	if (err == 0) {
		if (epayload) {
			oce_dma_sync(&sgl, BUS_DMASYNC_POSTWRITE);
			bcopy(epayload, payload, length);
		} else
			bcopy(&mbx->payload, payload, length);
	} else
		printf("%s: mailbox timeout, subsys %d op %d ver %d "
		    "%spayload lenght %d\n", sc->dev.dv_xname, subsys,
		    opcode, version, epayload ? "ext " : "",
		    length);
	if (epayload)
		oce_dma_free(sc, &sgl);
	return (err);
}

/**
 * @brief	Firmware will send gracious notifications during
 *		attach only after sending first mcc commnad. We
 *		use MCC queue only for getting async and mailbox
 *		for sending cmds. So to get gracious notifications
 *		atleast send one dummy command on mcc.
 */
void
oce_first_mcc(struct oce_softc *sc)
{
	struct oce_mbx *mbx;
	struct oce_mq *mq = sc->mq;
	struct mbx_hdr *hdr;
	struct mbx_get_common_fw_version *cmd;

	mbx = oce_ring_get(mq->ring);
	bzero(mbx, sizeof(struct oce_mbx));

	cmd = (struct mbx_get_common_fw_version *)&mbx->payload;

	hdr = &cmd->hdr;
	hdr->u0.req.subsystem = SUBSYS_COMMON;
	hdr->u0.req.opcode = OPCODE_COMMON_GET_FW_VERSION;
	hdr->u0.req.version = OCE_MBX_VER_V0;
	hdr->u0.req.timeout = MBX_TIMEOUT_SEC;
	hdr->u0.req.request_length = sizeof(*cmd) - sizeof(*hdr);

	mbx->u0.s.embedded = 1;
	mbx->payload_length = sizeof(*cmd);
	oce_dma_sync(&mq->ring->dma, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	oce_write_db(sc, PD_MQ_DB, mq->id | (1 << 16));
}

int
oce_get_fw_config(struct oce_softc *sc)
{
	struct mbx_common_query_fw_config cmd;
	int err;

	bzero(&cmd, sizeof(cmd));

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_QUERY_FIRMWARE_CONFIG,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	sc->port_id	  = cmd.params.rsp.port_id;
	sc->function_mode = cmd.params.rsp.function_mode;

	return (0);
}

int
oce_check_native_mode(struct oce_softc *sc)
{
	struct mbx_common_set_function_cap cmd;
	int err;

	bzero(&cmd, sizeof(cmd));

	cmd.params.req.valid_capability_flags = CAP_SW_TIMESTAMPS |
	    CAP_BE3_NATIVE_ERX_API;
	cmd.params.req.capability_flags = CAP_BE3_NATIVE_ERX_API;

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_SET_FUNCTIONAL_CAPS,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	if (cmd.params.rsp.capability_flags & CAP_BE3_NATIVE_ERX_API)
		SET(sc->flags, OCE_F_BE3_NATIVE);

	return (0);
}

/**
 * @brief Function for creating a network interface.
 * @param sc		software handle to the device
 * @returns		0 on success, error otherwise
 */
int
oce_create_iface(struct oce_softc *sc, uint8_t *macaddr)
{
	struct mbx_create_common_iface cmd;
	uint32_t capab_flags, capab_en_flags;
	int err = 0;

	/* interface capabilities to give device when creating interface */
	capab_flags = OCE_CAPAB_FLAGS;

	/* capabilities to enable by default (others set dynamically) */
	capab_en_flags = OCE_CAPAB_ENABLE;

	if (IS_XE201(sc)) {
		/* LANCER A0 workaround */
		capab_en_flags &= ~MBX_RX_IFACE_FLAGS_PASS_L3L4_ERR;
		capab_flags &= ~MBX_RX_IFACE_FLAGS_PASS_L3L4_ERR;
	}

	/* enable capabilities controlled via driver startup parameters */
	if (sc->rss_enable)
		capab_en_flags |= MBX_RX_IFACE_FLAGS_RSS;
	else {
		capab_en_flags &= ~MBX_RX_IFACE_FLAGS_RSS;
		capab_flags &= ~MBX_RX_IFACE_FLAGS_RSS;
	}

	bzero(&cmd, sizeof(cmd));

	cmd.params.req.version = 0;
	cmd.params.req.cap_flags = htole32(capab_flags);
	cmd.params.req.enable_flags = htole32(capab_en_flags);
	if (macaddr != NULL) {
		bcopy(macaddr, &cmd.params.req.mac_addr[0], ETHER_ADDR_LEN);
		cmd.params.req.mac_invalid = 0;
	} else
		cmd.params.req.mac_invalid = 1;

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_CREATE_IFACE,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	sc->if_id = letoh32(cmd.params.rsp.if_id);

	if (macaddr != NULL)
		sc->pmac_id = letoh32(cmd.params.rsp.pmac_id);

	sc->nifs++;

	sc->if_cap_flags = capab_en_flags;

	/* Enable VLAN Promisc on HW */
	err = oce_config_vlan(sc, (uint8_t)sc->if_id, NULL, 0, 1, 1);
	if (err)
		return (err);

	/* set default flow control */
	err = oce_set_flow_control(sc, sc->flow_control);
	if (err)
		return (err);

	return (0);
}

/**
 * @brief Function to send the mbx command to configure vlan
 * @param sc 		software handle to the device
 * @param if_id 	interface identifier index
 * @param vtag_arr	array of vlan tags
 * @param vtag_cnt	number of elements in array
 * @param untagged	boolean TRUE/FLASE
 * @param promisc	flag to enable/disable VLAN promiscuous mode
 * @returns		0 on success, EIO on failure
 */
int
oce_config_vlan(struct oce_softc *sc, uint32_t if_id,
    struct normal_vlan *vtag_arr, int vtag_cnt, int untagged, int promisc)
{
	struct mbx_common_config_vlan cmd;

	bzero(&cmd, sizeof(cmd));

	cmd.params.req.if_id = if_id;
	cmd.params.req.promisc = promisc;
	cmd.params.req.untagged = untagged;
	cmd.params.req.num_vlans = vtag_cnt;

	if (!promisc)
		bcopy(vtag_arr, cmd.params.req.tags.normal_vlans,
			vtag_cnt * sizeof(struct normal_vlan));

	return (oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_CONFIG_IFACE_VLAN,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd)));
}

/**
 * @brief Function to set flow control capability in the hardware
 * @param sc 		software handle to the device
 * @param flow_control	flow control flags to set
 * @returns		0 on success, EIO on failure
 */
int
oce_set_flow_control(struct oce_softc *sc, uint32_t flow_control)
{
	struct mbx_common_get_set_flow_control cmd;

	bzero(&cmd, sizeof(cmd));

	if (flow_control & OCE_FC_TX)
		cmd.tx_flow_control = 1;
	if (flow_control & OCE_FC_RX)
		cmd.rx_flow_control = 1;

	return (oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_SET_FLOW_CONTROL,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd)));
}

#ifdef OCE_RSS
/**
 * @brief Function to set flow control capability in the hardware
 * @param sc 		software handle to the device
 * @param if_id 	interface id to read the address from
 * @param enable	0=disable, OCE_RSS_xxx flags otherwise
 * @returns		0 on success, EIO on failure
 */
int
oce_config_rss(struct oce_softc *sc, uint32_t if_id, int enable)
{
	struct mbx_config_nic_rss cmd;
	uint8_t *tbl = &cmd.params.req.cputable;
	int i, j;

	bzero(&cmd, sizeof(cmd));

	if (enable)
		cmd.params.req.enable_rss = RSS_ENABLE_IPV4 | RSS_ENABLE_IPV6 |
		    RSS_ENABLE_TCP_IPV4 | RSS_ENABLE_TCP_IPV6);
	cmd.params.req.flush = OCE_FLUSH;
	cmd.params.req.if_id = htole32(if_id);

	arc4random_buf(cmd.params.req.hash, sizeof(cmd.params.req.hash));

	/*
	 * Initialize the RSS CPU indirection table.
	 *
	 * The table is used to choose the queue to place incomming packets.
	 * Incomming packets are hashed.  The lowest bits in the hash result
	 * are used as the index into the CPU indirection table.
	 * Each entry in the table contains the RSS CPU-ID returned by the NIC
	 * create.  Based on the CPU ID, the receive completion is routed to
	 * the corresponding RSS CQs.  (Non-RSS packets are always completed
	 * on the default (0) CQ).
	 */
	for (i = 0, j = 0; j < sc->nrqs; j++) {
		if (sc->rq[j]->cfg.is_rss_queue)
			tbl[i++] = sc->rq[j]->rss_cpuid;
	}
	if (i > 0)
		cmd->params.req.cpu_tbl_sz_log2 = htole16(ilog2(i));
	else
		return (ENXIO);

	return (oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_CONFIG_RSS, OCE_MBX_VER_V0,
	    &cmd, sizeof(cmd)));
}
#endif	/* OCE_RSS */

/**
 * @brief Function for hardware update multicast filter
 * @param sc		software handle to the device
 * @param multi		table of multicast addresses
 * @param naddr		number of multicast addresses in the table
 */
int
oce_update_mcast(struct oce_softc *sc,
    uint8_t multi[][ETHER_ADDR_LEN], int naddr)
{
	struct mbx_set_common_iface_multicast cmd;

	bzero(&cmd, sizeof(cmd));

	bcopy(&multi[0], &cmd.params.req.mac[0], naddr * ETHER_ADDR_LEN);
	cmd.params.req.num_mac = htole16(naddr);
	cmd.params.req.if_id = sc->if_id;

	return (oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_SET_IFACE_MULTICAST,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd)));
}

/**
 * @brief RXF function to enable/disable device promiscuous mode
 * @param sc		software handle to the device
 * @param enable	enable/disable flag
 * @returns		0 on success, EIO on failure
 * @note
 *	The OPCODE_NIC_CONFIG_PROMISCUOUS command deprecated for Lancer.
 *	This function uses the COMMON_SET_IFACE_RX_FILTER command instead.
 */
int
oce_set_promisc(struct oce_softc *sc, int enable)
{
	struct mbx_set_common_iface_rx_filter cmd;
	struct iface_rx_filter_ctx *req;

	bzero(&cmd, sizeof(cmd));

	req = &cmd.params.req;
	req->if_id = sc->if_id;
	req->iface_flags_mask = MBX_RX_IFACE_FLAGS_PROMISC |
				MBX_RX_IFACE_FLAGS_VLAN_PROMISC;
	if (enable)
		req->iface_flags = req->iface_flags_mask;

	return (oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_SET_IFACE_RX_FILTER,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd)));
}

/**
 * @brief Function to query the link status from the hardware
 * @param sc 		software handle to the device
 * @param[out] link	pointer to the structure returning link attributes
 * @returns		0 on success, EIO on failure
 */
int
oce_get_link_status(struct oce_softc *sc)
{
	struct mbx_query_common_link_config cmd;
	struct link_status link;
	int err;

	bzero(&cmd, sizeof(cmd));

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_QUERY_LINK_CONFIG,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	bcopy(&cmd.params.rsp, &link, sizeof(struct link_status));
	link.logical_link_status = letoh32(link.logical_link_status);
	link.qos_link_speed = letoh16(link.qos_link_speed);

	if (link.logical_link_status == NTWK_LOGICAL_LINK_UP)
		sc->link_status = NTWK_LOGICAL_LINK_UP;
	else
		sc->link_status = NTWK_LOGICAL_LINK_DOWN;

	if (link.mac_speed > 0 && link.mac_speed < 5)
		sc->link_speed = link.mac_speed;
	else
		sc->link_speed = 0;

	sc->duplex = link.mac_duplex;

	sc->qos_link_speed = (uint32_t )link.qos_link_speed * 10;

	return (0);
}

int
oce_macaddr_get(struct oce_softc *sc, uint8_t *macaddr)
{
	struct mbx_query_common_iface_mac cmd;
	int err;

	bzero(&cmd, sizeof(cmd));

	cmd.params.req.type = MAC_ADDRESS_TYPE_NETWORK;
	cmd.params.req.permanent = 1;

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_QUERY_IFACE_MAC,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err == 0)
		bcopy(&cmd.params.rsp.mac.mac_addr[0], macaddr,
		    ETHER_ADDR_LEN);
	return (err);
}

int
oce_macaddr_add(struct oce_softc *sc, uint8_t *enaddr, uint32_t if_id,
    uint32_t *pmac_id)
{
	struct mbx_add_common_iface_mac cmd;
	int err;

	bzero(&cmd, sizeof(cmd));

	cmd.params.req.if_id = htole16(if_id);
	bcopy(enaddr, cmd.params.req.mac_address, ETHER_ADDR_LEN);

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_ADD_IFACE_MAC,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err == 0)
		*pmac_id = letoh32(cmd.params.rsp.pmac_id);
	return (err);
}

int
oce_macaddr_del(struct oce_softc *sc, uint32_t if_id, uint32_t pmac_id)
{
	struct mbx_del_common_iface_mac cmd;

	bzero(&cmd, sizeof(cmd));

	cmd.params.req.if_id = htole16(if_id);
	cmd.params.req.pmac_id = htole32(pmac_id);

	return (oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_DEL_IFACE_MAC,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd)));
}

int
oce_new_rq(struct oce_softc *sc, struct oce_rq *rq)
{
	struct mbx_create_nic_rq cmd;
	int err, npages;

	bzero(&cmd, sizeof(cmd));

	npages = oce_load_ring(sc, rq->ring, &cmd.params.req.pages[0],
	    nitems(cmd.params.req.pages));
	if (!npages) {
		printf("%s: failed to load the rq ring\n", __func__);
		return (1);
	}

	if (IS_XE201(sc)) {
		cmd.params.req.frag_size = rq->fragsize / 2048;
		cmd.params.req.page_size = 1;
	} else
		cmd.params.req.frag_size = ilog2(rq->fragsize);
	cmd.params.req.num_pages = npages;
	cmd.params.req.cq_id = rq->cq->id;
	cmd.params.req.if_id = htole32(sc->if_id);
	cmd.params.req.max_frame_size = htole16(rq->mtu);
	cmd.params.req.is_rss_queue = htole32(rq->rss);

	err = oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_CREATE_RQ,
	    IS_XE201(sc) ? OCE_MBX_VER_V1 : OCE_MBX_VER_V0, &cmd,
	    sizeof(cmd));
	if (err)
		return (err);

	rq->id = letoh16(cmd.params.rsp.rq_id);
	rq->rss_cpuid = cmd.params.rsp.rss_cpuid;

	return (0);
}

int
oce_new_wq(struct oce_softc *sc, struct oce_wq *wq)
{
	struct mbx_create_nic_wq cmd;
	int err, npages;

	bzero(&cmd, sizeof(cmd));

	npages = oce_load_ring(sc, wq->ring, &cmd.params.req.pages[0],
	    nitems(cmd.params.req.pages));
	if (!npages) {
		printf("%s: failed to load the wq ring\n", __func__);
		return (1);
	}

	if (IS_XE201(sc))
		cmd.params.req.if_id = sc->if_id;
	cmd.params.req.nic_wq_type = NIC_WQ_TYPE_STANDARD;
	cmd.params.req.num_pages = npages;
	cmd.params.req.wq_size = ilog2(wq->nitems) + 1;
	cmd.params.req.cq_id = htole16(wq->cq->id);
	cmd.params.req.ulp_num = 1;

	err = oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_CREATE_WQ,
	    IS_XE201(sc) ? OCE_MBX_VER_V1 : OCE_MBX_VER_V0, &cmd,
	    sizeof(cmd));
	if (err)
		return (err);

	wq->id = letoh16(cmd.params.rsp.wq_id);

	return (0);
}

int
oce_new_mq(struct oce_softc *sc, struct oce_mq *mq)
{
	struct mbx_create_common_mq_ex cmd;
	union oce_mq_ext_ctx *ctx;
	int err, npages;

	bzero(&cmd, sizeof(cmd));

	npages = oce_load_ring(sc, mq->ring, &cmd.params.req.pages[0],
	    nitems(cmd.params.req.pages));
	if (!npages) {
		printf("%s: failed to load the mq ring\n", __func__);
		return (-1);
	}

	ctx = &cmd.params.req.context;
	ctx->v0.num_pages = npages;
	ctx->v0.cq_id = mq->cq->id;
	ctx->v0.ring_size = ilog2(mq->nitems) + 1;
	ctx->v0.valid = 1;
	/* Subscribe to Link State and Group 5 Events(bits 1 and 5 set) */
	ctx->v0.async_evt_bitmap = 0xffffffff;

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_CREATE_MQ_EXT,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	mq->id = letoh16(cmd.params.rsp.mq_id);

	return (0);
}

int
oce_new_eq(struct oce_softc *sc, struct oce_eq *eq)
{
	struct mbx_create_common_eq cmd;
	int err, npages;

	bzero(&cmd, sizeof(cmd));

	npages = oce_load_ring(sc, eq->ring, &cmd.params.req.pages[0],
	    nitems(cmd.params.req.pages));
	if (!npages) {
		printf("%s: failed to load the eq ring\n", __func__);
		return (-1);
	}

	cmd.params.req.ctx.num_pages = htole16(npages);
	cmd.params.req.ctx.valid = 1;
	cmd.params.req.ctx.size = (eq->isize == 4) ? 0 : 1;
	cmd.params.req.ctx.count = ilog2(eq->nitems / 256);
	cmd.params.req.ctx.armed = 0;
	cmd.params.req.ctx.delay_mult = htole32(eq->delay);

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_CREATE_EQ,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	eq->id = letoh16(cmd.params.rsp.eq_id);

	return (0);
}

int
oce_new_cq(struct oce_softc *sc, struct oce_cq *cq)
{
	struct mbx_create_common_cq cmd;
	union oce_cq_ctx *ctx;
	int err, npages;

	bzero(&cmd, sizeof(cmd));

	npages = oce_load_ring(sc, cq->ring, &cmd.params.req.pages[0],
	    nitems(cmd.params.req.pages));
	if (!npages) {
		printf("%s: failed to load the cq ring\n", __func__);
		return (-1);
	}

	ctx = &cmd.params.req.cq_ctx;

	if (IS_XE201(sc)) {
		ctx->v2.num_pages = htole16(npages);
		ctx->v2.page_size = 1; /* for 4K */
		ctx->v2.eventable = cq->eventable;
		ctx->v2.valid = 1;
		ctx->v2.count = ilog2(cq->nitems / 256);
		ctx->v2.nodelay = cq->nodelay;
		ctx->v2.coalesce_wm = cq->ncoalesce;
		ctx->v2.armed = 0;
		ctx->v2.eq_id = cq->eq->id;
		if (ctx->v2.count == 3) {
			if (cq->nitems > (4*1024)-1)
				ctx->v2.cqe_count = (4*1024)-1;
			else
				ctx->v2.cqe_count = cq->nitems;
		}
	} else {
		ctx->v0.num_pages = htole16(npages);
		ctx->v0.eventable = cq->eventable;
		ctx->v0.valid = 1;
		ctx->v0.count = ilog2(cq->nitems / 256);
		ctx->v0.nodelay = cq->nodelay;
		ctx->v0.coalesce_wm = cq->ncoalesce;
		ctx->v0.armed = 0;
		ctx->v0.eq_id = cq->eq->id;
	}

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_CREATE_CQ,
	    IS_XE201(sc) ? OCE_MBX_VER_V2 : OCE_MBX_VER_V0, &cmd,
	    sizeof(cmd));
	if (err)
		return (err);

	cq->id = letoh16(cmd.params.rsp.cq_id);

	return (0);
}

int
oce_destroy_queue(struct oce_softc *sc, enum qtype qtype, uint32_t qid)
{
	struct mbx_destroy_common_mq cmd;
	int opcode, subsys;

	switch (qtype) {
	case QTYPE_CQ:
		opcode = OPCODE_COMMON_DESTROY_CQ;
		subsys = SUBSYS_COMMON;
		break;
	case QTYPE_EQ:
		opcode = OPCODE_COMMON_DESTROY_EQ;
		subsys = SUBSYS_COMMON;
		break;
	case QTYPE_MQ:
		opcode = OPCODE_COMMON_DESTROY_MQ;
		subsys = SUBSYS_COMMON;
		break;
	case QTYPE_RQ:
		opcode = OPCODE_NIC_DELETE_RQ;
		subsys = SUBSYS_NIC;
		break;
	case QTYPE_WQ:
		opcode = OPCODE_NIC_DELETE_WQ;
		subsys = SUBSYS_NIC;
		break;
	default:
		return (EINVAL);
	}

	bzero(&cmd, sizeof(cmd));

	cmd.params.req.id = htole16(qid);

	return (oce_cmd(sc, subsys, opcode, OCE_MBX_VER_V0, &cmd,
	    sizeof(cmd)));
}

static inline int
oce_update_stats(struct oce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint64_t rxe, txe;
	int err;

	if (ISSET(sc->flags, OCE_F_BE2))
		err = oce_stats_be2(sc, &rxe, &txe);
	else if (ISSET(sc->flags, OCE_F_BE3))
		err = oce_stats_be3(sc, &rxe, &txe);
	else
		err = oce_stats_xe(sc, &rxe, &txe);
	if (err)
		return (err);

	ifp->if_ierrors += (rxe > sc->rx_errors) ?
	    rxe - sc->rx_errors : sc->rx_errors - rxe;
	sc->rx_errors = rxe;
	ifp->if_oerrors += (txe > sc->tx_errors) ?
	    txe - sc->tx_errors : sc->tx_errors - txe;
	sc->tx_errors = txe;

	return (0);
}

int
oce_stats_be2(struct oce_softc *sc, uint64_t *rxe, uint64_t *txe)
{
	struct mbx_get_nic_stats_v0 cmd;
	struct oce_pmem_stats *ms;
	struct oce_rxf_stats_v0 *rs;
	struct oce_port_rxf_stats_v0 *ps;
	int err;

	bzero(&cmd, sizeof(cmd));

	err = oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_GET_STATS, OCE_MBX_VER_V0,
	    &cmd, sizeof(cmd));
	if (err)
		return (err);

	ms = &cmd.params.rsp.stats.pmem;
	rs = &cmd.params.rsp.stats.rxf;
	ps = &rs->port[sc->port_id];

	*rxe = ps->rx_crc_errors + ps->rx_in_range_errors +
	    ps->rx_frame_too_long + ps->rx_dropped_runt +
	    ps->rx_ip_checksum_errs + ps->rx_tcp_checksum_errs +
	    ps->rx_udp_checksum_errs + ps->rxpp_fifo_overflow_drop +
	    ps->rx_dropped_tcp_length + ps->rx_dropped_too_small +
	    ps->rx_dropped_too_short + ps->rx_out_range_errors +
	    ps->rx_dropped_header_too_small + ps->rx_input_fifo_overflow_drop +
	    ps->rx_alignment_symbol_errors;
	if (sc->if_id)
		*rxe += rs->port1_jabber_events;
	else
		*rxe += rs->port0_jabber_events;
	*rxe += ms->eth_red_drops;

	*txe = 0; /* hardware doesn't provide any extra tx error statistics */

	return (0);
}

int
oce_stats_be3(struct oce_softc *sc, uint64_t *rxe, uint64_t *txe)
{
	struct mbx_get_nic_stats cmd;
	struct oce_pmem_stats *ms;
	struct oce_rxf_stats_v1 *rs;
	struct oce_port_rxf_stats_v1 *ps;
	int err;

	bzero(&cmd, sizeof(cmd));

	err = oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_GET_STATS, OCE_MBX_VER_V1,
	    &cmd, sizeof(cmd));
	if (err)
		return (err);

	ms = &cmd.params.rsp.stats.pmem;
	rs = &cmd.params.rsp.stats.rxf;
	ps = &rs->port[sc->port_id];

	*rxe = ps->rx_crc_errors + ps->rx_in_range_errors +
	    ps->rx_frame_too_long + ps->rx_dropped_runt +
	    ps->rx_ip_checksum_errs + ps->rx_tcp_checksum_errs +
	    ps->rx_udp_checksum_errs + ps->rxpp_fifo_overflow_drop +
	    ps->rx_dropped_tcp_length + ps->rx_dropped_too_small +
	    ps->rx_dropped_too_short + ps->rx_out_range_errors +
	    ps->rx_dropped_header_too_small + ps->rx_input_fifo_overflow_drop +
	    ps->rx_alignment_symbol_errors + ps->jabber_events;
	*rxe += ms->eth_red_drops;

	*txe = 0; /* hardware doesn't provide any extra tx error statistics */

	return (0);
}

int
oce_stats_xe(struct oce_softc *sc, uint64_t *rxe, uint64_t *txe)
{
	struct mbx_get_pport_stats cmd;
	struct oce_pport_stats *pps;
	int err;

	bzero(&cmd, sizeof(cmd));

	cmd.params.req.reset_stats = 0;
	cmd.params.req.port_number = sc->if_id;

	err = oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_GET_PPORT_STATS,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	pps = &cmd.params.rsp.pps;

	*rxe = pps->rx_discards + pps->rx_errors + pps->rx_crc_errors +
	    pps->rx_alignment_errors + pps->rx_symbol_errors +
	    pps->rx_frames_too_long + pps->rx_internal_mac_errors +
	    pps->rx_undersize_pkts + pps->rx_oversize_pkts + pps->rx_jabbers +
	    pps->rx_control_frames_unknown_opcode + pps->rx_in_range_errors +
	    pps->rx_out_of_range_errors + pps->rx_ip_checksum_errors +
	    pps->rx_tcp_checksum_errors + pps->rx_udp_checksum_errors +
	    pps->rx_fifo_overflow + pps->rx_input_fifo_overflow +
	    pps->rx_drops_too_many_frags + pps->rx_drops_mtu;

	*txe = pps->tx_discards + pps->tx_errors + pps->tx_internal_mac_errors;

	return (0);
}
