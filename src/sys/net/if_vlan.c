/*	$OpenBSD: if_vlan.c,v 1.132 2015/06/29 10:32:29 dlg Exp $	*/

/*
 * Copyright 1998 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/net/if_vlan.c,v 1.16 2000/03/26 15:21:40 charnier Exp $
 */

/*
 * if_vlan.c - pseudo-device driver for IEEE 802.1Q virtual LANs.
 * This is sort of sneaky in the implementation, since
 * we need to pretend to be enough of an Ethernet implementation
 * to make arp work.  The way we do this is by telling everyone
 * that we are an Ethernet, and then catch the packets that
 * ether_output() left on our output queue when it calls
 * if_start(), rewrite them for use by the real outgoing interface,
 * and ask it to send them.
 *
 * Some devices support 802.1Q tag insertion in firmware.  The
 * vlan interface behavior changes when the IFCAP_VLAN_HWTAGGING
 * capability is set on the parent.  In this case, vlan_start()
 * will not modify the ethernet header.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_vlan_var.h>

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

u_long vlan_tagmask, svlan_tagmask;

#define TAG_HASH_SIZE		32
#define TAG_HASH(tag)		(tag & vlan_tagmask)
LIST_HEAD(vlan_taghash, ifvlan)	*vlan_tagh, *svlan_tagh;


int	vlan_input(struct mbuf *);
void	vlan_start(struct ifnet *ifp);
int	vlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr);
int	vlan_unconfig(struct ifnet *ifp, struct ifnet *newp);
int	vlan_config(struct ifvlan *, struct ifnet *, u_int16_t);
void	vlan_vlandev_state(void *);
void	vlanattach(int count);
int	vlan_set_promisc(struct ifnet *ifp);
int	vlan_ether_addmulti(struct ifvlan *, struct ifreq *);
int	vlan_ether_delmulti(struct ifvlan *, struct ifreq *);
void	vlan_ether_purgemulti(struct ifvlan *);
void	vlan_ether_resetmulti(struct ifvlan *, struct ifnet *);
int	vlan_clone_create(struct if_clone *, int);
int	vlan_clone_destroy(struct ifnet *);
void	vlan_ifdetach(void *);

struct if_clone vlan_cloner =
    IF_CLONE_INITIALIZER("vlan", vlan_clone_create, vlan_clone_destroy);
struct if_clone svlan_cloner =
    IF_CLONE_INITIALIZER("svlan", vlan_clone_create, vlan_clone_destroy);

/* ARGSUSED */
void
vlanattach(int count)
{
	/* Normal VLAN */
	vlan_tagh = hashinit(TAG_HASH_SIZE, M_DEVBUF, M_NOWAIT,
	    &vlan_tagmask);
	if (vlan_tagh == NULL)
		panic("vlanattach: hashinit");
	if_clone_attach(&vlan_cloner);

	/* Service-VLAN for QinQ/802.1ad provider bridges */
	svlan_tagh = hashinit(TAG_HASH_SIZE, M_DEVBUF, M_NOWAIT,
	    &svlan_tagmask);
	if (svlan_tagh == NULL)
		panic("vlanattach: hashinit");
	if_clone_attach(&svlan_cloner);
}

int
vlan_clone_create(struct if_clone *ifc, int unit)
{
	struct ifvlan	*ifv;
	struct ifnet	*ifp;

	if ((ifv = malloc(sizeof(*ifv), M_DEVBUF, M_NOWAIT|M_ZERO)) == NULL)
		return (ENOMEM);

	LIST_INIT(&ifv->vlan_mc_listhead);
	ifp = &ifv->ifv_if;
	ifp->if_softc = ifv;
	snprintf(ifp->if_xname, sizeof ifp->if_xname, "%s%d", ifc->ifc_name,
	    unit);
	/* NB: flags are not set here */
	/* NB: mtu is not set here */

	/* Special handling for the IEEE 802.1ad QinQ variant */
	if (strcmp("svlan", ifc->ifc_name) == 0)
		ifv->ifv_type = ETHERTYPE_QINQ;
	else
		ifv->ifv_type = ETHERTYPE_VLAN;

	ifp->if_start = vlan_start;
	ifp->if_ioctl = vlan_ioctl;
	IFQ_SET_MAXLEN(&ifp->if_snd, 1);
	IFQ_SET_READY(&ifp->if_snd);
	if_attach(ifp);
	ether_ifattach(ifp);
	ifp->if_hdrlen = EVL_ENCAPLEN;

	return (0);
}

int
vlan_clone_destroy(struct ifnet *ifp)
{
	struct ifvlan	*ifv = ifp->if_softc;

	vlan_unconfig(ifp, NULL);
	ether_ifdetach(ifp);
	if_detach(ifp);
	free(ifv, M_DEVBUF, sizeof(*ifv));
	return (0);
}

void
vlan_ifdetach(void *ptr)
{
	struct ifvlan	*ifv = ptr;
	vlan_clone_destroy(&ifv->ifv_if);
}

void
vlan_start(struct ifnet *ifp)
{
	struct ifvlan   *ifv;
	struct ifnet	*p;
	struct mbuf	*m;
	uint8_t		 prio;

	ifv = ifp->if_softc;
	p = ifv->ifv_p;

	for (;;) {
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif /* NBPFILTER > 0 */

		if ((p->if_flags & (IFF_UP|IFF_RUNNING)) !=
		    (IFF_UP|IFF_RUNNING)) {
			IF_DROP(&p->if_snd);
			ifp->if_oerrors++;
			m_freem(m);
			continue;
		}

		/* IEEE 802.1p has prio 0 and 1 swapped */
		prio = m->m_pkthdr.pf.prio;
		if (prio <= 1)
			prio = !prio;

		/*
		 * If the underlying interface cannot do VLAN tag insertion
		 * itself, create an encapsulation header.
		 */
		if ((p->if_capabilities & IFCAP_VLAN_HWTAGGING) &&
		    (ifv->ifv_type == ETHERTYPE_VLAN)) {
			m->m_pkthdr.ether_vtag = ifv->ifv_tag +
			    (prio << EVL_PRIO_BITS);
			m->m_flags |= M_VLANTAG;
		} else {
			struct ether_vlan_header evh;

			m_copydata(m, 0, ETHER_HDR_LEN, (caddr_t)&evh);
			evh.evl_proto = evh.evl_encap_proto;
			evh.evl_encap_proto = htons(ifv->ifv_type);
			evh.evl_tag = htons(ifv->ifv_tag +
			    (prio << EVL_PRIO_BITS));
			m_adj(m, ETHER_HDR_LEN);
			M_PREPEND(m, sizeof(evh), M_DONTWAIT);
			if (m == NULL) {
				ifp->if_oerrors++;
				continue;
			}
			m_copyback(m, 0, sizeof(evh), &evh, M_NOWAIT);
			m->m_flags &= ~M_VLANTAG;
		}

		if (if_output(p, m)) {
			ifp->if_oerrors++;
			continue;
		}
		ifp->if_opackets++;
	}
}

/*
 * vlan_input() returns 1 if it has consumed the packet, 0 otherwise.
 */
int
vlan_input(struct mbuf *m)
{
	struct ifvlan			*ifv;
	struct ifnet			*ifp;
	struct ether_vlan_header	*evl;
	struct ether_header		*eh;
	struct vlan_taghash		*tagh;
	u_int				 tag;
	struct mbuf_list		 ml = MBUF_LIST_INITIALIZER();
	u_int16_t			 etype;

	ifp = if_get(m->m_pkthdr.ph_ifidx);
	KASSERT(ifp != NULL);
	if ((ifp->if_flags & IFF_UP) == 0) {
		m_freem(m);
		return (1);
	}

	eh = mtod(m, struct ether_header *);
	etype = ntohs(eh->ether_type);

	if (m->m_flags & M_VLANTAG) {
		etype = ETHERTYPE_VLAN;
		tagh = vlan_tagh;
	} else if ((etype == ETHERTYPE_VLAN) || (etype == ETHERTYPE_QINQ)) {
		if (m->m_len < EVL_ENCAPLEN &&
		    (m = m_pullup(m, EVL_ENCAPLEN)) == NULL) {
			ifp->if_ierrors++;
			return (1);
		}

		evl = mtod(m, struct ether_vlan_header *);
		m->m_pkthdr.ether_vtag = ntohs(evl->evl_tag);
		tagh = etype == ETHERTYPE_QINQ ? svlan_tagh : vlan_tagh;
	} else {
		/* Skip non-VLAN packets. */
		return (0);
	}

	/* From now on ether_vtag is fine */
	tag = EVL_VLANOFTAG(m->m_pkthdr.ether_vtag);
	m->m_pkthdr.pf.prio = EVL_PRIOFTAG(m->m_pkthdr.ether_vtag);

	/* IEEE 802.1p has prio 0 and 1 swapped */
	if (m->m_pkthdr.pf.prio <= 1)
		m->m_pkthdr.pf.prio = !m->m_pkthdr.pf.prio;

	LIST_FOREACH(ifv, &tagh[TAG_HASH(tag)], ifv_list) {
		if (ifp == ifv->ifv_p && tag == ifv->ifv_tag &&
		    etype == ifv->ifv_type)
			break;
	}

	if (ifv == NULL) {
		ifp->if_noproto++;
		m_freem(m);
		return (1);
	}

	if ((ifv->ifv_if.if_flags & (IFF_UP|IFF_RUNNING)) !=
	    (IFF_UP|IFF_RUNNING)) {
		m_freem(m);
		return (1);
	}

	/*
	 * Drop promiscuously received packets if we are not in
	 * promiscuous mode.
	 */
	if (!ETHER_IS_MULTICAST(eh->ether_dhost) &&
	    (ifp->if_flags & IFF_PROMISC) &&
	    (ifv->ifv_if.if_flags & IFF_PROMISC) == 0) {
		if (bcmp(&ifv->ifv_ac.ac_enaddr, eh->ether_dhost,
		    ETHER_ADDR_LEN)) {
			m_freem(m);
			return (1);
		}
	}

	/*
	 * Having found a valid vlan interface corresponding to
	 * the given source interface and vlan tag, remove the
	 * encapsulation.
	 */
	if (m->m_flags & M_VLANTAG) {
		m->m_flags &= ~M_VLANTAG;
	} else {
		eh->ether_type = evl->evl_proto;
		memmove((char *)eh + EVL_ENCAPLEN, eh, sizeof(*eh));
		m_adj(m, EVL_ENCAPLEN);
	}

	ml_enqueue(&ml, m);
	if_input(&ifv->ifv_if, &ml);
	return (1);
}

int
vlan_config(struct ifvlan *ifv, struct ifnet *p, u_int16_t tag)
{
	struct sockaddr_dl	*sdl1, *sdl2;
	struct vlan_taghash	*tagh;
	u_int			 flags;
	int			 s;

	if (p->if_type != IFT_ETHER)
		return EPROTONOSUPPORT;
	if (ifv->ifv_p == p && ifv->ifv_tag == tag) /* noop */
		return (0);

	/* Can we share an ifih between multiple vlan(4) instances? */
	ifv->ifv_ifih = SLIST_FIRST(&p->if_inputs);
	if (ifv->ifv_ifih->ifih_input != vlan_input) {
		ifv->ifv_ifih = malloc(sizeof(*ifv->ifv_ifih), M_DEVBUF,
		    M_NOWAIT);
		if (ifv->ifv_ifih == NULL)
			return (ENOMEM);
		ifv->ifv_ifih->ifih_input = vlan_input;
		ifv->ifv_ifih->ifih_refcnt = 0;
	}

	/* Remember existing interface flags and reset the interface */
	flags = ifv->ifv_flags;
	vlan_unconfig(&ifv->ifv_if, p);
	ifv->ifv_p = p;
	ifv->ifv_if.if_baudrate = p->if_baudrate;

	if (p->if_capabilities & IFCAP_VLAN_MTU) {
		ifv->ifv_if.if_mtu = p->if_mtu;
		ifv->ifv_if.if_hardmtu = p->if_hardmtu;
	} else {
		ifv->ifv_if.if_mtu = p->if_mtu - EVL_ENCAPLEN;
		ifv->ifv_if.if_hardmtu = p->if_hardmtu - EVL_ENCAPLEN;
	}

	ifv->ifv_if.if_flags = p->if_flags &
	    (IFF_UP | IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);

	/* Reset promisc mode on the interface and its parent */
	if (flags & IFVF_PROMISC) {
		ifv->ifv_if.if_flags |= IFF_PROMISC;
		vlan_set_promisc(&ifv->ifv_if);
	}

	/*
	 * If the parent interface can do hardware-assisted
	 * VLAN encapsulation, then propagate its hardware-
	 * assisted checksumming flags.
	 *
	 * If the card cannot handle hardware tagging, it cannot
	 * possibly compute the correct checksums for tagged packets.
	 */
	if (p->if_capabilities & IFCAP_VLAN_HWTAGGING)
		ifv->ifv_if.if_capabilities = p->if_capabilities &
		    IFCAP_CSUM_MASK;

	/*
	 * Hardware VLAN tagging only works with the default VLAN
	 * ethernet type (0x8100).
	 */
	if (ifv->ifv_type != ETHERTYPE_VLAN)
		ifv->ifv_if.if_capabilities &= ~IFCAP_VLAN_HWTAGGING;

	/*
	 * Set up our ``Ethernet address'' to reflect the underlying
	 * physical interface's.
	 */
	sdl1 = ifv->ifv_if.if_sadl;
	sdl2 = p->if_sadl;
	sdl1->sdl_type = IFT_ETHER;
	sdl1->sdl_alen = ETHER_ADDR_LEN;
	bcopy(LLADDR(sdl2), LLADDR(sdl1), ETHER_ADDR_LEN);
	bcopy(LLADDR(sdl2), ifv->ifv_ac.ac_enaddr, ETHER_ADDR_LEN);

	ifv->ifv_tag = tag;

	/* Register callback for physical link state changes */
	ifv->lh_cookie = hook_establish(p->if_linkstatehooks, 1,
	    vlan_vlandev_state, ifv);

	/* Register callback if parent wants to unregister */
	ifv->dh_cookie = hook_establish(p->if_detachhooks, 0,
	    vlan_ifdetach, ifv);

	vlan_vlandev_state(ifv);

	tagh = ifv->ifv_type == ETHERTYPE_QINQ ? svlan_tagh : vlan_tagh;

	s = splnet();
	/* Change input handler of the physical interface. */
	if (++ifv->ifv_ifih->ifih_refcnt == 1)
		SLIST_INSERT_HEAD(&p->if_inputs, ifv->ifv_ifih, ifih_next);

	LIST_INSERT_HEAD(&tagh[TAG_HASH(tag)], ifv, ifv_list);
	splx(s);

	return (0);
}

int
vlan_unconfig(struct ifnet *ifp, struct ifnet *newp)
{
	struct sockaddr_dl	*sdl;
	struct ifvlan		*ifv;
	struct ifnet		*p;
	int			 s;

	ifv = ifp->if_softc;
	if ((p = ifv->ifv_p) == NULL)
		return 0;

	/* Unset promisc mode on the interface and its parent */
	if (ifv->ifv_flags & IFVF_PROMISC) {
		ifp->if_flags &= ~IFF_PROMISC;
		vlan_set_promisc(ifp);
	}

	s = splnet();
	LIST_REMOVE(ifv, ifv_list);

	/* Restore previous input handler. */
	if (--ifv->ifv_ifih->ifih_refcnt == 0) {
		SLIST_REMOVE(&p->if_inputs, ifv->ifv_ifih, ifih, ifih_next);
		free(ifv->ifv_ifih, M_DEVBUF, sizeof(*ifv->ifv_ifih));
	}
	splx(s);

	hook_disestablish(p->if_linkstatehooks, ifv->lh_cookie);
	hook_disestablish(p->if_detachhooks, ifv->dh_cookie);
	/* Reset link state */
	if (newp != NULL) {
		ifp->if_link_state = LINK_STATE_INVALID;
		if_link_state_change(ifp);
	}

	/*
 	 * Since the interface is being unconfigured, we need to
	 * empty the list of multicast groups that we may have joined
	 * while we were alive and remove them from the parent's list
	 * as well.
	 */
	vlan_ether_resetmulti(ifv, newp);

	/* Disconnect from parent. */
	ifv->ifv_p = NULL;
	ifv->ifv_if.if_mtu = ETHERMTU;
	ifv->ifv_if.if_hardmtu = ETHERMTU;
	ifv->ifv_flags = 0;

	/* Clear our MAC address. */
	sdl = ifv->ifv_if.if_sadl;
	sdl->sdl_type = IFT_ETHER;
	sdl->sdl_alen = ETHER_ADDR_LEN;
	bzero(LLADDR(sdl), ETHER_ADDR_LEN);
	bzero(ifv->ifv_ac.ac_enaddr, ETHER_ADDR_LEN);

	return (0);
}

void
vlan_vlandev_state(void *v)
{
	struct ifvlan	*ifv = v;

	if (ifv->ifv_if.if_link_state == ifv->ifv_p->if_link_state)
		return;

	ifv->ifv_if.if_link_state = ifv->ifv_p->if_link_state;
	ifv->ifv_if.if_baudrate = ifv->ifv_p->if_baudrate;
	if_link_state_change(&ifv->ifv_if);
}

int
vlan_set_promisc(struct ifnet *ifp)
{
	struct ifvlan	*ifv = ifp->if_softc;
	int		 error = 0;

	if ((ifp->if_flags & IFF_PROMISC) != 0) {
		if ((ifv->ifv_flags & IFVF_PROMISC) == 0)
			if ((error = ifpromisc(ifv->ifv_p, 1)) == 0)
				ifv->ifv_flags |= IFVF_PROMISC;
	} else {
		if ((ifv->ifv_flags & IFVF_PROMISC) != 0)
			if ((error = ifpromisc(ifv->ifv_p, 0)) == 0)
				ifv->ifv_flags &= ~IFVF_PROMISC;
	}
	return (0);
}

int
vlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct proc	*p = curproc;	/* XXX */
	struct ifaddr	*ifa;
	struct ifnet	*pr;
	struct ifreq	*ifr;
	struct ifvlan	*ifv;
	struct vlanreq	 vlr;
	int		 error = 0, s;

	ifr = (struct ifreq *)data;
	ifa = (struct ifaddr *)data;
	ifv = ifp->if_softc;

	switch (cmd) {
	case SIOCSIFADDR:
		if (ifv->ifv_p != NULL) {
			ifp->if_flags |= IFF_UP;
			if (ifa->ifa_addr->sa_family == AF_INET)
				arp_ifinit(&ifv->ifv_ac, ifa);
		} else
			error = EINVAL;
		break;

	case SIOCGIFADDR:
		{
			struct sockaddr	*sa;

			sa = (struct sockaddr *)&ifr->ifr_data;
			bcopy(((struct arpcom *)ifp->if_softc)->ac_enaddr,
			    (caddr_t) sa->sa_data, ETHER_ADDR_LEN);
		}
		break;

	case SIOCSIFMTU:
		if (ifv->ifv_p != NULL) {
			if (ifr->ifr_mtu < ETHERMIN ||
			    ifr->ifr_mtu > ifv->ifv_if.if_hardmtu)
				error = EINVAL;
			else
				ifp->if_mtu = ifr->ifr_mtu;
		} else
			error = EINVAL;

		break;

	case SIOCSETVLAN:
		if ((error = suser(p, 0)) != 0)
			break;
		if ((error = copyin(ifr->ifr_data, &vlr, sizeof vlr)))
			break;
		if (vlr.vlr_parent[0] == '\0') {
			s = splnet();
			vlan_unconfig(ifp, NULL);
			if (ifp->if_flags & IFF_UP)
				if_down(ifp);
			ifp->if_flags &= ~IFF_RUNNING;
			splx(s);
			break;
		}
		pr = ifunit(vlr.vlr_parent);
		if (pr == NULL) {
			error = ENOENT;
			break;
		}
		/*
		 * Don't let the caller set up a VLAN tag with
		 * anything except VLID bits.
		 */
		if (vlr.vlr_tag & ~EVL_VLID_MASK) {
			error = EINVAL;
			break;
		}
		error = vlan_config(ifv, pr, vlr.vlr_tag);
		if (error)
			break;
		ifp->if_flags |= IFF_RUNNING;

		/* Update promiscuous mode, if necessary. */
		vlan_set_promisc(ifp);
		break;
		
	case SIOCGETVLAN:
		bzero(&vlr, sizeof vlr);
		if (ifv->ifv_p) {
			snprintf(vlr.vlr_parent, sizeof(vlr.vlr_parent),
			    "%s", ifv->ifv_p->if_xname);
			vlr.vlr_tag = ifv->ifv_tag;
		}
		error = copyout(&vlr, ifr->ifr_data, sizeof vlr);
		break;
	case SIOCSIFFLAGS:
		/*
		 * For promiscuous mode, we enable promiscuous mode on
		 * the parent if we need promiscuous on the VLAN interface.
		 */
		if (ifv->ifv_p != NULL)
			error = vlan_set_promisc(ifp);
		break;

	case SIOCADDMULTI:
		error = (ifv->ifv_p != NULL) ?
		    vlan_ether_addmulti(ifv, ifr) : EINVAL;
		break;

	case SIOCDELMULTI:
		error = (ifv->ifv_p != NULL) ?
		    vlan_ether_delmulti(ifv, ifr) : EINVAL;
		break;
	default:
		error = ENOTTY;
	}
	return error;
}


int
vlan_ether_addmulti(struct ifvlan *ifv, struct ifreq *ifr)
{
	struct ifnet		*ifp = ifv->ifv_p;
	struct vlan_mc_entry	*mc;
	u_int8_t		 addrlo[ETHER_ADDR_LEN], addrhi[ETHER_ADDR_LEN];
	int			 error;

	error = ether_addmulti(ifr, (struct arpcom *)&ifv->ifv_ac);
	if (error != ENETRESET)
		return (error);

	/*
	 * This is new multicast address.  We have to tell parent
	 * about it.  Also, remember this multicast address so that
	 * we can delete them on unconfigure.
	 */
	if ((mc = malloc(sizeof(*mc), M_DEVBUF, M_NOWAIT)) == NULL) {
		error = ENOMEM;
		goto alloc_failed;
	}

	/*
	 * As ether_addmulti() returns ENETRESET, following two
	 * statement shouldn't fail.
	 */
	(void)ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
	ETHER_LOOKUP_MULTI(addrlo, addrhi, &ifv->ifv_ac, mc->mc_enm);
	memcpy(&mc->mc_addr, &ifr->ifr_addr, ifr->ifr_addr.sa_len);
	LIST_INSERT_HEAD(&ifv->vlan_mc_listhead, mc, mc_entries);

	if ((error = (*ifp->if_ioctl)(ifp, SIOCADDMULTI, (caddr_t)ifr)) != 0)
		goto ioctl_failed;

	return (error);

 ioctl_failed:
	LIST_REMOVE(mc, mc_entries);
	free(mc, M_DEVBUF, 0);
 alloc_failed:
	(void)ether_delmulti(ifr, (struct arpcom *)&ifv->ifv_ac);

	return (error);
}

int
vlan_ether_delmulti(struct ifvlan *ifv, struct ifreq *ifr)
{
	struct ifnet		*ifp = ifv->ifv_p;
	struct ether_multi	*enm;
	struct vlan_mc_entry	*mc;
	u_int8_t		 addrlo[ETHER_ADDR_LEN], addrhi[ETHER_ADDR_LEN];
	int			 error;

	/*
	 * Find a key to lookup vlan_mc_entry.  We have to do this
	 * before calling ether_delmulti for obvious reason.
	 */
	if ((error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi)) != 0)
		return (error);
	ETHER_LOOKUP_MULTI(addrlo, addrhi, &ifv->ifv_ac, enm);
	if (enm == NULL)
		return (EINVAL);

	LIST_FOREACH(mc, &ifv->vlan_mc_listhead, mc_entries)
		if (mc->mc_enm == enm)
			break;

	/* We won't delete entries we didn't add */
	if (mc == NULL)
		return (EINVAL);

	if ((error = ether_delmulti(ifr, (struct arpcom *)&ifv->ifv_ac)) != 0)
		return (error);

	/* We no longer use this multicast address.  Tell parent so. */
	if ((error = (*ifp->if_ioctl)(ifp, SIOCDELMULTI, (caddr_t)ifr)) != 0) {
		/* And forget about this address. */
		LIST_REMOVE(mc, mc_entries);
		free(mc, M_DEVBUF, 0);
	} else
		(void)ether_addmulti(ifr, (struct arpcom *)&ifv->ifv_ac);
	return (error);
}

/*
 * Delete any multicast address we have asked to add from parent
 * interface.  Called when the vlan is being unconfigured.
 */
void
vlan_ether_purgemulti(struct ifvlan *ifv)
{
	struct ifnet		*ifp = ifv->ifv_p;
	struct vlan_mc_entry	*mc;
	union {
		struct ifreq ifreq;
		struct {
			char			ifr_name[IFNAMSIZ];
			struct sockaddr_storage	ifr_ss;
		} ifreq_storage;
	} ifreq;
	struct ifreq	*ifr = &ifreq.ifreq;

	memcpy(ifr->ifr_name, ifp->if_xname, IFNAMSIZ);
	while ((mc = LIST_FIRST(&ifv->vlan_mc_listhead)) != NULL) {
		memcpy(&ifr->ifr_addr, &mc->mc_addr, mc->mc_addr.ss_len);
		(void)(*ifp->if_ioctl)(ifp, SIOCDELMULTI, (caddr_t)ifr);
		LIST_REMOVE(mc, mc_entries);
		free(mc, M_DEVBUF, 0);
	}
}

void
vlan_ether_resetmulti(struct ifvlan *ifv, struct ifnet *p)
{
	struct ifnet		*ifp = ifv->ifv_p;
	struct vlan_mc_entry	*mc;
	union {
		struct ifreq ifreq;
		struct {
			char			ifr_name[IFNAMSIZ];
			struct sockaddr_storage	ifr_ss;
		} ifreq_storage;
	} ifreq;
	struct ifreq	*ifr = &ifreq.ifreq;

	if (p == NULL) {
		vlan_ether_purgemulti(ifv);
		return;
	} else if (ifp == p)
		return;

	LIST_FOREACH(mc, &ifv->vlan_mc_listhead, mc_entries) {
		memcpy(&ifr->ifr_addr, &mc->mc_addr, mc->mc_addr.ss_len);
	
		/* Remove from the old parent */
		memcpy(ifr->ifr_name, ifp->if_xname, IFNAMSIZ);
		(void)(*ifp->if_ioctl)(ifp, SIOCDELMULTI, (caddr_t)ifr);

		/* Try to add to the new parent */
		memcpy(ifr->ifr_name, p->if_xname, IFNAMSIZ);
		(void)(*p->if_ioctl)(p, SIOCADDMULTI, (caddr_t)ifr);
	}
}
