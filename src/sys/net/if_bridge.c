/*	$OpenBSD: if_bridge.c,v 1.45 2001/01/17 04:47:18 fgsch Exp $	*/

/*
 * Copyright (c) 1999, 2000 Jason L. Wright (jason@thought.net)
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "bridge.h"
#include "bpfilter.h"
#include "gif.h"

#if NBRIDGE > 0

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <machine/cpu.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_llc.h>
#include <net/route.h>
#include <net/netisr.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/ip_ipsp.h>

#include <net/if_enc.h>
#ifdef IPFILTER
#include <netinet/ip_compat.h>
#include <netinet/ip_fil.h>
#endif
#endif

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <net/if_bridge.h>

#ifndef	BRIDGE_RTABLE_SIZE
#define	BRIDGE_RTABLE_SIZE	1024
#endif
#define	BRIDGE_RTABLE_MASK	(BRIDGE_RTABLE_SIZE - 1)

/*
 * Maximum number of addresses to cache
 */
#ifndef	BRIDGE_RTABLE_MAX
#define	BRIDGE_RTABLE_MAX	100
#endif

/* spanning tree defaults */
#define	BSTP_DEFAULT_MAX_AGE		(20 * 256)
#define	BSTP_DEFAULT_HELLO_TIME		(2 * 256)
#define	BSTP_DEFAULT_FORWARD_DELAY	(15 * 256)
#define	BSTP_DEFAULT_HOLD_TIME		(1 * 256)
#define	BSTP_DEFAULT_BRIDGE_PRIORITY	0x8000
#define	BSTP_DEFAULT_PORT_PRIORITY	0x80
#define	BSTP_DEFAULT_PATH_COST		55

/*
 * Timeout (in seconds) for entries learned dynamically
 */
#ifndef	BRIDGE_RTABLE_TIMEOUT
#define	BRIDGE_RTABLE_TIMEOUT	240
#endif

extern int ifqmaxlen;

struct bridge_softc bridgectl[NBRIDGE];

void	bridgeattach __P((int));
int	bridge_ioctl __P((struct ifnet *, u_long, caddr_t));
void	bridge_start __P((struct ifnet *));
void	bridgeintr_frame __P((struct bridge_softc *, struct mbuf *));
void	bridge_broadcast __P((struct bridge_softc *, struct ifnet *,
    struct ether_header *, struct mbuf *));
void	bridge_stop __P((struct bridge_softc *));
void	bridge_init __P((struct bridge_softc *));
int	bridge_bifconf __P((struct bridge_softc *, struct ifbifconf *));

void	bridge_timer __P((void *));
int	bridge_rtfind __P((struct bridge_softc *, struct ifbaconf *));
void	bridge_rtage __P((struct bridge_softc *));
void	bridge_rttrim __P((struct bridge_softc *));
void	bridge_rtdelete __P((struct bridge_softc *, struct ifnet *));
int	bridge_rtdaddr __P((struct bridge_softc *, struct ether_addr *));
int	bridge_rtflush __P((struct bridge_softc *, int));
struct ifnet *	bridge_rtupdate __P((struct bridge_softc *,
    struct ether_addr *, struct ifnet *ifp, int, u_int8_t));
struct ifnet *	bridge_rtlookup __P((struct bridge_softc *,
    struct ether_addr *));
u_int32_t	bridge_hash __P((struct bridge_softc *, struct ether_addr *));
int bridge_blocknonip __P((struct ether_header *, struct mbuf *));
int		bridge_addrule __P((struct bridge_iflist *,
    struct ifbrlreq *, int out));
int		bridge_flushrule __P((struct bridge_iflist *));
int	bridge_brlconf __P((struct bridge_softc *, struct ifbrlconf *));
u_int8_t bridge_filterrule __P((struct brl_head *, struct ether_header *));

#define	ETHERADDR_IS_IP_MCAST(a) \
	/* struct etheraddr *a;	*/				\
	((a)->ether_addr_octet[0] == 0x01 &&			\
	 (a)->ether_addr_octet[1] == 0x00 &&			\
	 (a)->ether_addr_octet[2] == 0x5e)


#if defined(INET) && (defined(IPFILTER) || defined(IPFILTER_LKM))
/*
 * Filter hooks
 */
struct mbuf *bridge_filter __P((struct bridge_softc *, struct ifnet *,
    struct ether_header *, struct mbuf *m));
#endif

void
bridgeattach(unused)
	int unused;
{
	int i;
	struct ifnet *ifp;

	for (i = 0; i < NBRIDGE; i++) {
		struct bridge_softc *sc;

		sc = &bridgectl[i];

		sc->sc_brtmax = BRIDGE_RTABLE_MAX;
		sc->sc_brttimeout = BRIDGE_RTABLE_TIMEOUT;
		sc->sc_bridge_max_age = BSTP_DEFAULT_MAX_AGE;
		sc->sc_bridge_hello_time = BSTP_DEFAULT_HELLO_TIME;
		sc->sc_bridge_forward_delay= BSTP_DEFAULT_FORWARD_DELAY;
		sc->sc_bridge_priority = BSTP_DEFAULT_BRIDGE_PRIORITY;
		sc->sc_hold_time = BSTP_DEFAULT_HOLD_TIME;
		timeout_set(&sc->sc_brtimeout, bridge_timer, sc);
		LIST_INIT(&sc->sc_iflist);
		ifp = &sc->sc_if;
		sprintf(ifp->if_xname, "bridge%d", i);
		ifp->if_softc = sc;
		ifp->if_mtu = ETHERMTU;
		ifp->if_ioctl = bridge_ioctl;
		ifp->if_output = bridge_output;
		ifp->if_start = bridge_start;
		ifp->if_type = IFT_BRIDGE;
		ifp->if_snd.ifq_maxlen = ifqmaxlen;
		ifp->if_hdrlen = sizeof(struct ether_header);
		if_attach(ifp);
#if NBPFILTER > 0
		bpfattach(&sc->sc_if.if_bpf, ifp,
		    DLT_EN10MB, sizeof(struct ether_header));
#endif
	}
}

int
bridge_ioctl(ifp, cmd, data)
	struct ifnet *ifp;
	u_long cmd;
	caddr_t	data;
{
	struct proc *prc = curproc;		/* XXX */
	struct ifnet *ifs;
	struct bridge_softc *sc = (struct bridge_softc *)ifp->if_softc;
	struct ifbreq *req = (struct ifbreq *)data;
	struct ifbaconf *baconf = (struct ifbaconf *)data;
	struct ifbareq *bareq = (struct ifbareq *)data;
	struct ifbrparam *bparam = (struct ifbrparam *)data;
	struct ifbifconf *bifconf = (struct ifbifconf *)data;
	struct ifbrlreq *brlreq = (struct ifbrlreq *)data;
	struct ifbrlconf *brlconf = (struct ifbrlconf *)data;
	struct ifreq ifreq;
	int error = 0, s;
	struct bridge_iflist *p;

	s = splnet();
	switch (cmd) {
	case SIOCBRDGADD:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;

		ifs = ifunit(req->ifbr_ifsname);
		if (ifs == NULL) {			/* no such interface */
			error = ENOENT;
			break;
		}
		if (ifs->if_bridge == (caddr_t)sc) {
			error = EEXIST;
			break;
		}
		if (ifs->if_bridge != NULL) {
			error = EBUSY;
			break;
		}

		if (ifs->if_type == IFT_ETHER) {
			if ((ifs->if_flags & IFF_UP) == 0) {
				/*
				 * Bring interface up long enough to set
				 * promiscuous flag, then shut it down again.
				 */
				strncpy(ifreq.ifr_name, req->ifbr_ifsname,
				    sizeof(ifreq.ifr_name) - 1);
				ifreq.ifr_name[sizeof(ifreq.ifr_name) - 1] = '\0';
				ifs->if_flags |= IFF_UP;
				ifreq.ifr_flags = ifs->if_flags;
				error = (*ifs->if_ioctl)(ifs, SIOCSIFFLAGS,
				    (caddr_t)&ifreq);
				if (error != 0)
					break;

				error = ifpromisc(ifs, 1);
				if (error != 0)
					break;

				strncpy(ifreq.ifr_name, req->ifbr_ifsname,
				    sizeof(ifreq.ifr_name) - 1);
				ifreq.ifr_name[sizeof(ifreq.ifr_name) - 1] = '\0';
				ifs->if_flags &= ~IFF_UP;
				ifreq.ifr_flags = ifs->if_flags;
				error = (*ifs->if_ioctl)(ifs, SIOCSIFFLAGS,
				    (caddr_t)&ifreq);
				if (error != 0) {
					ifpromisc(ifs, 0);
					break;
				}
			} else {
				error = ifpromisc(ifs, 1);
				if (error != 0)
					break;
			}
		}
#if NGIF > 0
		else if (ifs->if_type == IFT_GIF) {
		        /* Nothing needed */
		}
#endif /* NGIF */
		else {
			error = EINVAL;
			break;
		}

		p = (struct bridge_iflist *) malloc(
		    sizeof(struct bridge_iflist), M_DEVBUF, M_NOWAIT);
		if (p == NULL) {
			if (ifs->if_type == IFT_ETHER)
				ifpromisc(ifs, 0);
			error = ENOMEM;
			break;
		}
		bzero(p, sizeof(struct bridge_iflist));

		p->ifp = ifs;
		p->bif_flags = IFBIF_LEARNING | IFBIF_DISCOVER;
		p->bif_priority = BSTP_DEFAULT_PORT_PRIORITY;
		p->bif_path_cost = BSTP_DEFAULT_PATH_COST;
		SIMPLEQ_INIT(&p->bif_brlin);
		SIMPLEQ_INIT(&p->bif_brlout);
		LIST_INSERT_HEAD(&sc->sc_iflist, p, next);
		ifs->if_bridge = (caddr_t)sc;
		break;
	case SIOCBRDGDEL:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;

		LIST_FOREACH(p, &sc->sc_iflist, next) {
			if (strncmp(p->ifp->if_xname, req->ifbr_ifsname,
			    sizeof(p->ifp->if_xname)) == 0) {
				p->ifp->if_bridge = NULL;

				error = ifpromisc(p->ifp, 0);

				LIST_REMOVE(p, next);
				bridge_rtdelete(sc, p->ifp);
				bridge_flushrule(p);
				free(p, M_DEVBUF);
				break;
			}
		}
		if (p == LIST_END(&sc->sc_iflist)) {
			error = ENOENT;
			break;
		}
		break;
	case SIOCBRDGIFS:
		error = bridge_bifconf(sc, bifconf);
		break;
	case SIOCBRDGGIFFLGS:
		ifs = ifunit(req->ifbr_ifsname);
		if (ifs == NULL) {
			error = ENOENT;
			break;
		}
		if ((caddr_t)sc != ifs->if_bridge) {
			error = ESRCH;
			break;
		}
		LIST_FOREACH(p, &sc->sc_iflist, next) {
			if (p->ifp == ifs)
				break;
		}
		if (p == LIST_END(&sc->sc_iflist)) {
			error = ESRCH;
			break;
		}
		req->ifbr_ifsflags = p->bif_flags;
		req->ifbr_state = p->bif_state;
		req->ifbr_priority = p->bif_priority;
		req->ifbr_portno = p->ifp->if_index & 0xff;
		break;
	case SIOCBRDGSIFFLGS:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;
		ifs = ifunit(req->ifbr_ifsname);
		if (ifs == NULL) {
			error = ENOENT;
			break;
		}
		if ((caddr_t)sc != ifs->if_bridge) {
			error = ESRCH;
			break;
		}
		LIST_FOREACH(p, &sc->sc_iflist, next) {
			if (p->ifp == ifs)
				break;
		}
		if (p == LIST_END(&sc->sc_iflist)) {
			error = ESRCH;
			break;
		}
		if ((req->ifbr_ifsflags & IFBIF_STP) &&
		    (ifs->if_type != IFT_ETHER)) {
			error = EINVAL;
			break;
		}
		p->bif_flags = req->ifbr_ifsflags;
		break;
	case SIOCBRDGSIFPRIO:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;
		ifs = ifunit(req->ifbr_ifsname);
		if (ifs == NULL) {
			error = ENOENT;
			break;
		}
		if ((caddr_t)sc != ifs->if_bridge) {
			error = ESRCH;
			break;
		}
		LIST_FOREACH(p, &sc->sc_iflist, next) {
			if (p->ifp == ifs)
				break;
		}
		if (p == LIST_END(&sc->sc_iflist)) {
			error = ESRCH;
			break;
		}
		p->bif_priority = req->ifbr_priority;
		break;
	case SIOCBRDGRTS:
		error = bridge_rtfind(sc, baconf);
		break;
	case SIOCBRDGFLUSH:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;

		error = bridge_rtflush(sc, req->ifbr_ifsflags);
		break;
	case SIOCBRDGSADDR:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;

		ifs = ifunit(bareq->ifba_ifsname);
		if (ifs == NULL) {			/* no such interface */
			error = ENOENT;
			break;
		}

		if (ifs->if_bridge == NULL ||
		    ifs->if_bridge != (caddr_t)sc) {
			error = ESRCH;
			break;
		}

		ifs = bridge_rtupdate(sc, &bareq->ifba_dst, ifs, 1,
		    bareq->ifba_flags);
		if (ifs == NULL)
			error = ENOMEM;
		break;
	case SIOCBRDGDADDR:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;
		error = bridge_rtdaddr(sc, &bareq->ifba_dst);
		break;
	case SIOCBRDGGCACHE:
		bparam->ifbrp_csize = sc->sc_brtmax;
		break;
	case SIOCBRDGSCACHE:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;
		sc->sc_brtmax = bparam->ifbrp_csize;
		bridge_rttrim(sc);
		break;
	case SIOCBRDGSTO:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;
		sc->sc_brttimeout = bparam->ifbrp_ctime;
		timeout_del(&sc->sc_brtimeout);
		if (bparam->ifbrp_ctime != 0)
			timeout_add(&sc->sc_brtimeout, sc->sc_brttimeout * hz);
		break;
	case SIOCBRDGGTO:
		bparam->ifbrp_ctime = sc->sc_brttimeout;
		break;
	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == IFF_UP)
			bridge_init(sc);

		if ((ifp->if_flags & IFF_UP) == 0)
			bridge_stop(sc);

		break;
	case SIOCBRDGARL:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;
		ifs = ifunit(brlreq->ifbr_ifsname);
		if (ifs == NULL) {
			error = ENOENT;
			break;
		}
		if (ifs->if_bridge == NULL ||
		    ifs->if_bridge != (caddr_t)sc) {
			error = ESRCH;
			break;
		}
		LIST_FOREACH(p, &sc->sc_iflist, next) {
			if (p->ifp == ifs)
				break;
		}
		if (p == LIST_END(&sc->sc_iflist)) {
			error = ESRCH;
			break;
		}
		if ((brlreq->ifbr_action != BRL_ACTION_BLOCK &&
		    brlreq->ifbr_action != BRL_ACTION_PASS) ||
		    (brlreq->ifbr_flags & (BRL_FLAG_IN|BRL_FLAG_OUT)) == 0) {
			error = EINVAL;
			break;
		}
		if (brlreq->ifbr_flags & BRL_FLAG_IN) {
			error = bridge_addrule(p, brlreq, 0);
			if (error)
				break;
		}
		if (brlreq->ifbr_flags & BRL_FLAG_OUT) {
			error = bridge_addrule(p, brlreq, 1);
			if (error)
				break;
		}
		break;
	case SIOCBRDGFRL:
		if ((error = suser(prc->p_ucred, &prc->p_acflag)) != 0)
			break;
		ifs = ifunit(brlreq->ifbr_ifsname);
		if (ifs == NULL) {
			error = ENOENT;
			break;
		}
		if (ifs->if_bridge == NULL ||
		    ifs->if_bridge != (caddr_t)sc) {
			error = ESRCH;
			break;
		}
		LIST_FOREACH(p, &sc->sc_iflist, next) {
			if (p->ifp == ifs)
				break;
		}
		if (p == LIST_END(&sc->sc_iflist)) {
			error = ESRCH;
			break;
		}
		error = bridge_flushrule(p);
		break;
	case SIOCBRDGGRL:
		error = bridge_brlconf(sc, brlconf);
		break;
	case SIOCBRDGGPRI:
	case SIOCBRDGGMA:
	case SIOCBRDGGHT:
	case SIOCBRDGGFD:
		break;
	case SIOCBRDGSPRI:
	case SIOCBRDGSFD:
	case SIOCBRDGSMA:
	case SIOCBRDGSHT:
		error = suser(prc->p_ucred, &prc->p_acflag);
		break;
	default:
		error = EINVAL;
	}

	if (!error)
		error = bstp_ioctl(ifp, cmd, data);

	splx(s);
	return (error);
}

/* Detach an interface from a bridge.  */
void
bridge_ifdetach(ifp)
	struct ifnet *ifp;
{
	struct bridge_softc *sc = (struct bridge_softc *)ifp->if_bridge;
	struct bridge_iflist *bif;

	LIST_FOREACH(bif, &sc->sc_iflist, next) {
		if (bif->ifp == ifp) {
			LIST_REMOVE(bif, next);
			bridge_rtdelete(sc, ifp);
			bridge_flushrule(bif);
			free(bif, M_DEVBUF);
			ifp->if_bridge = NULL;
			break;
		}
	}
}

int
bridge_bifconf(sc, bifc)
	struct bridge_softc *sc;
	struct ifbifconf *bifc;
{
	struct bridge_iflist *p;
	u_int32_t total = 0, i = 0;
	int error = 0;
	struct ifbreq breq;

	LIST_FOREACH(p, &sc->sc_iflist, next) {
		total++;
	}
	if (bifc->ifbic_len == 0) {
		i = total;
		goto done;
	}

	LIST_FOREACH(p, &sc->sc_iflist, next) {
		if (bifc->ifbic_len < sizeof(breq))
			break;
		strncpy(breq.ifbr_name, sc->sc_if.if_xname,
		    sizeof(breq.ifbr_name)-1);
		breq.ifbr_name[sizeof(breq.ifbr_name) - 1] = '\0';
		strncpy(breq.ifbr_ifsname, p->ifp->if_xname,
		    sizeof(breq.ifbr_ifsname)-1);
		breq.ifbr_ifsname[sizeof(breq.ifbr_ifsname) - 1] = '\0';
		breq.ifbr_ifsflags = p->bif_flags;
		breq.ifbr_state = p->bif_state;
		breq.ifbr_priority = p->bif_priority;
		breq.ifbr_portno = p->ifp->if_index & 0xff;
		error = copyout((caddr_t)&breq,
		    (caddr_t)(bifc->ifbic_req + i), sizeof(breq));
		if (error)
			goto done;
		i++;
		bifc->ifbic_len -= sizeof(breq);
	}

done:
	bifc->ifbic_len = i * sizeof(breq);
	return (error);
}

int
bridge_brlconf(sc, bc)
	struct bridge_softc *sc;
	struct ifbrlconf *bc;
{
	struct ifnet *ifp;
	struct bridge_iflist *ifl;
	struct brl_node *n;
	struct ifbrlreq req;
	int error = 0;
	u_int32_t i = 0, total = 0;

	ifp = ifunit(bc->ifbrl_ifsname);
	if (ifp == NULL)
		return (ENOENT);
	if (ifp->if_bridge == NULL || ifp->if_bridge != (caddr_t)sc)
		return (ESRCH);
	LIST_FOREACH(ifl, &sc->sc_iflist, next) {
		if (ifl->ifp == ifp)
			break;
	}
	if (ifl == LIST_END(&sc->sc_iflist))
		return (ESRCH);

	SIMPLEQ_FOREACH(n, &ifl->bif_brlin, brl_next) {
		total++;
	}
	SIMPLEQ_FOREACH(n, &ifl->bif_brlout, brl_next) {
		total++;
	}

	if (bc->ifbrl_len == 0) {
		i = total;
		goto done;
	}

	SIMPLEQ_FOREACH(n, &ifl->bif_brlin, brl_next) {
		if (bc->ifbrl_len < sizeof(req))
			goto done;
		strncpy(req.ifbr_name, sc->sc_if.if_xname,
		    sizeof(req.ifbr_name) - 1);
		req.ifbr_name[sizeof(req.ifbr_name) - 1] = '\0';
		strncpy(req.ifbr_ifsname, ifl->ifp->if_xname,
		    sizeof(req.ifbr_ifsname) - 1);
		req.ifbr_ifsname[sizeof(req.ifbr_ifsname) - 1] = '\0';
		req.ifbr_action = n->brl_action;
		req.ifbr_flags = n->brl_flags;
		req.ifbr_src = n->brl_src;
		req.ifbr_dst = n->brl_dst;
		error = copyout((caddr_t)&req,
		    (caddr_t)(bc->ifbrl_buf + (i * sizeof(req))), sizeof(req));
		if (error)
			goto done;
		i++;
		bc->ifbrl_len -= sizeof(req);
	}

	SIMPLEQ_FOREACH(n, &ifl->bif_brlout, brl_next) {
		if (bc->ifbrl_len < sizeof(req))
			goto done;
		strncpy(req.ifbr_name, sc->sc_if.if_xname,
		    sizeof(req.ifbr_name) - 1);
		req.ifbr_name[sizeof(req.ifbr_name) - 1] = '\0';
		strncpy(req.ifbr_ifsname, ifl->ifp->if_xname,
		    sizeof(req.ifbr_ifsname) - 1);
		req.ifbr_ifsname[sizeof(req.ifbr_ifsname) - 1] = '\0';
		req.ifbr_action = n->brl_action;
		req.ifbr_flags = n->brl_flags;
		req.ifbr_src = n->brl_src;
		req.ifbr_dst = n->brl_dst;
		error = copyout((caddr_t)&req,
		    (caddr_t)(bc->ifbrl_buf + (i * sizeof(req))), sizeof(req));
		if (error)
			goto done;
		i++;
		bc->ifbrl_len -= sizeof(req);
	}

done:
	bc->ifbrl_len = i * sizeof(req);
	return (error);
}

void
bridge_init(sc)
	struct bridge_softc *sc;
{
	struct ifnet *ifp = &sc->sc_if;
	int i;

	if ((ifp->if_flags & IFF_RUNNING) == IFF_RUNNING)
		return;

	if (sc->sc_rts == NULL) {
		sc->sc_rts = (struct bridge_rthead *)malloc(
		    BRIDGE_RTABLE_SIZE * (sizeof(struct bridge_rthead)),
		    M_DEVBUF, M_NOWAIT);
		if (sc->sc_rts == NULL)
			return;
		for (i = 0; i < BRIDGE_RTABLE_SIZE; i++) {
			LIST_INIT(&sc->sc_rts[i]);
		}
		get_random_bytes(&sc->sc_hashkey, sizeof(sc->sc_hashkey));
	}
	ifp->if_flags |= IFF_RUNNING;

	if (sc->sc_brttimeout != 0)
		timeout_add(&sc->sc_brtimeout, sc->sc_brttimeout * hz);
}

/*
 * Stop the bridge and deallocate the routing table.
 */
void
bridge_stop(sc)
	struct bridge_softc *sc;
{
	struct ifnet *ifp = &sc->sc_if;

	/*
	 * If we're not running, there's nothing to do.
	 */
	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	timeout_del(&sc->sc_brtimeout);

	bridge_rtflush(sc, IFBF_FLUSHDYN);

	ifp->if_flags &= ~IFF_RUNNING;
}

/*
 * Send output from the bridge.  The mbuf has the ethernet header
 * already attached.  We must enqueue or free the mbuf before exiting.
 */
int
bridge_output(ifp, m, sa, rt)
	struct ifnet *ifp;
	struct mbuf *m;
	struct sockaddr *sa;
	struct rtentry *rt;
{
	struct ether_header *eh;
	struct ifnet *dst_if;
	struct ether_addr *src, *dst;
	struct bridge_softc *sc;
	int s;

	if (m->m_len < sizeof(*eh)) {
		m = m_pullup(m, sizeof(*eh));
		if (m == NULL)
			return (0);
	}
	eh = mtod(m, struct ether_header *);
	dst = (struct ether_addr *)&eh->ether_dhost[0];
	src = (struct ether_addr *)&eh->ether_shost[0];
	sc = (struct bridge_softc *)ifp->if_bridge;

	s = splimp();

	/*
	 * If bridge is down, but original output interface is up,
	 * go ahead and send out that interface.  Otherwise the packet
	 * is dropped below.
	 */
	if ((sc->sc_if.if_flags & IFF_RUNNING) == 0) {
		dst_if = ifp;
		goto sendunicast;
	}

	/*
	 * If the packet is a broadcast or we don't know a better way to
	 * get there, send to all interfaces.
	 */
	dst_if = bridge_rtlookup(sc, dst);
	if (dst_if == NULL || eh->ether_dhost[0] & 1) {
		struct bridge_iflist *p;
		struct mbuf *mc;
		int used = 0;

		LIST_FOREACH(p, &sc->sc_iflist, next) {
			dst_if = p->ifp;
			if ((dst_if->if_flags & IFF_RUNNING) == 0)
				continue;
			if (IF_QFULL(&dst_if->if_snd)) {
				IF_DROP(&dst_if->if_snd);
				sc->sc_if.if_oerrors++;
				continue;
			}
			if (LIST_NEXT(p, next) == LIST_END(&sc->sc_iflist)) {
				used = 1;
				mc = m;
			} else {
				mc = m_copym(m, 0, M_COPYALL, M_NOWAIT);
				if (mc == NULL) {
					sc->sc_if.if_oerrors++;
					continue;
				}
			}

			sc->sc_if.if_opackets++;
			sc->sc_if.if_obytes += mc->m_pkthdr.len;
			dst_if->if_lastchange = time;
			dst_if->if_obytes += mc->m_pkthdr.len;
			IF_ENQUEUE(&dst_if->if_snd, mc);
			if (mc->m_flags & M_MCAST)
				ifp->if_omcasts++;
			if ((dst_if->if_flags & IFF_OACTIVE) == 0)
				(*dst_if->if_start)(dst_if);
		}
		if (!used)
			m_freem(m);
		splx(s);
		return (0);
	}

sendunicast:
	if ((dst_if->if_flags & IFF_RUNNING) == 0) {
		m_freem(m);
		splx(s);
		return (0);
	}
	if (IF_QFULL(&dst_if->if_snd)) {
		IF_DROP(&dst_if->if_snd);
		sc->sc_if.if_oerrors++;
		m_freem(m);
		splx(s);
		return (0);
	}
	sc->sc_if.if_opackets++;
	sc->sc_if.if_obytes += m->m_pkthdr.len;
	dst_if->if_lastchange = time;
	dst_if->if_obytes += m->m_pkthdr.len;
	IF_ENQUEUE(&dst_if->if_snd, m);
	if (m->m_flags & M_MCAST)
		dst_if->if_omcasts++;
	if ((dst_if->if_flags & IFF_OACTIVE) == 0)
		(*dst_if->if_start)(dst_if);
	splx(s);
	return (0);
}

/*
 * Start output on the bridge.  This function should never be called.
 */
void
bridge_start(ifp)
	struct ifnet *ifp;
{
}

/*
 * Loop through each bridge interface and process their input queues.
 */
void
bridgeintr(void)
{
	struct bridge_softc *sc;
	struct mbuf *m;
	int i, s;

	for (i = 0; i < NBRIDGE; i++) {
		sc = &bridgectl[i];
		for (;;) {
			s = splimp();
			IF_DEQUEUE(&sc->sc_if.if_snd, m);
			splx(s);
			if (m == NULL)
				break;
			bridgeintr_frame(sc, m);
		}
	}
}

/*
 * Process a single frame.  Frame must be freed or queued before returning.
 */
void
bridgeintr_frame(sc, m)
	struct bridge_softc *sc;
	struct mbuf *m;
{
	int s;
	struct ifnet *src_if, *dst_if;
	struct bridge_iflist *ifl;
	struct ether_addr *dst, *src;
	struct ether_header eh;

	if ((sc->sc_if.if_flags & IFF_RUNNING) == 0) {
		m_freem(m);
		return;
	}

	src_if = m->m_pkthdr.rcvif;

#if NBPFILTER > 0
	if (sc->sc_if.if_bpf)
		bpf_mtap(sc->sc_if.if_bpf, m);
#endif

	sc->sc_if.if_lastchange = time;
	sc->sc_if.if_ipackets++;
	sc->sc_if.if_ibytes += m->m_pkthdr.len;

	LIST_FOREACH(ifl, &sc->sc_iflist, next) {
		if (ifl->ifp == src_if)
			break;
	}
	if (ifl == LIST_END(&sc->sc_iflist)) {
		m_freem(m);
		return;
	}

	if ((ifl->bif_flags & IFBIF_STP) &&
	    ((ifl->bif_state == BSTP_IFSTATE_BLOCKING) ||
	     (ifl->bif_state == BSTP_IFSTATE_LISTENING) ||
	     (ifl->bif_state == BSTP_IFSTATE_DISABLED))) {
		m_freem(m);
		return;
	}

	if (m->m_pkthdr.len < sizeof(eh)) {
		m_freem(m);
		return;
	}
	m_copydata(m, 0, sizeof(struct ether_header), (caddr_t)&eh);
	dst = (struct ether_addr *)&eh.ether_dhost[0];
	src = (struct ether_addr *)&eh.ether_shost[0];

	/*
	 * If interface is learning, and if source address
	 * is not broadcast or multicast, record it's address.
	 */
	if ((ifl->bif_flags & IFBIF_LEARNING) &&
	    (eh.ether_shost[0] & 1) == 0 &&
	    !(eh.ether_shost[0] == 0 &&
	      eh.ether_shost[1] == 0 &&
	      eh.ether_shost[2] == 0 &&
	      eh.ether_shost[3] == 0 &&
	      eh.ether_shost[4] == 0 &&
	      eh.ether_shost[5] == 0))
		bridge_rtupdate(sc, src, src_if, 0, IFBAF_DYNAMIC);

	if ((ifl->bif_flags & IFBIF_STP) &&
	    (ifl->bif_state == BSTP_IFSTATE_LEARNING)) {
		m_freem(m);
		return;
	}

	/*
	 * At this point, the port either doesn't participate in stp or
	 * it's in the forwarding state
	 */

	/*
	 * If packet is unicast, destined for someone on "this"
	 * side of the bridge, drop it.
	 */
	if ((m->m_flags & (M_BCAST | M_MCAST)) == 0) {
		dst_if = bridge_rtlookup(sc, dst);
		if (dst_if == src_if) {
			m_freem(m);
			return;
		}
	} else
		dst_if = NULL;

	/*
	 * Multicast packets get handled a little differently:
	 * If interface is:
	 *	-link0,-link1	(default) Forward all multicast
	 *			as broadcast.
	 *	-link0,link1	Drop non-IP multicast, forward
	 *			as broadcast IP multicast.
	 *	link0,-link1	Drop IP multicast, forward as
	 *			broadcast non-IP multicast.
	 *	link0,link1	Drop all multicast.
	 */
	if (m->m_flags & M_MCAST) {
		if ((sc->sc_if.if_flags &
		    (IFF_LINK0 | IFF_LINK1)) ==
		    (IFF_LINK0 | IFF_LINK1)) {
			m_freem(m);
			return;
		}
		if (sc->sc_if.if_flags & IFF_LINK0 &&
		    ETHERADDR_IS_IP_MCAST(dst)) {
			m_freem(m);
			return;
		}
		if (sc->sc_if.if_flags & IFF_LINK1 &&
		    !ETHERADDR_IS_IP_MCAST(dst)) {
			m_freem(m);
			return;
		}
	}

	if (ifl->bif_flags & IFBIF_BLOCKNONIP && bridge_blocknonip(&eh, m)) {
		m_freem(m);
		return;
	}

	if (bridge_filterrule(&ifl->bif_brlin, &eh) == BRL_ACTION_BLOCK) {
		m_freem(m);
		return;
	}

#if defined(INET) && (defined(IPFILTER) || defined(IPFILTER_LKM))
	m = bridge_filter(sc, src_if, &eh, m);
	if (m == NULL)
		return;
#endif

	/*
	 * If the packet is a multicast or broadcast OR if we don't
	 * know any better, forward it to all interfaces.
	 */
	if ((m->m_flags & (M_BCAST | M_MCAST)) || dst_if == NULL) {
		sc->sc_if.if_imcasts++;
		s = splimp();
		bridge_broadcast(sc, src_if, &eh, m);
		splx(s);
		return;
	}

	/*
	 * At this point, we're dealing with a unicast frame going to a
	 * different interface
	 */
	if ((dst_if->if_flags & IFF_RUNNING) == 0) {
		m_freem(m);
		return;
	}
	LIST_FOREACH(ifl, &sc->sc_iflist, next) {
		if (ifl->ifp == dst_if)
			break;
	}
	if (ifl == LIST_END(&sc->sc_iflist)) {
		m_freem(m);
		return;
	}
	if ((ifl->bif_flags & IFBIF_STP) &&
	    (ifl->bif_state == BSTP_IFSTATE_DISABLED ||
	     ifl->bif_state == BSTP_IFSTATE_BLOCKING)) {
		m_free(m);
		return;
	}
	if (bridge_filterrule(&ifl->bif_brlout, &eh) == BRL_ACTION_BLOCK) {
		m_freem(m);
		return;
	}
	s = splimp();
	if (IF_QFULL(&dst_if->if_snd)) {
		IF_DROP(&dst_if->if_snd);
		sc->sc_if.if_oerrors++;
		m_freem(m);
		splx(s);
		return;
	}
	sc->sc_if.if_opackets++;
	sc->sc_if.if_obytes += m->m_pkthdr.len;
	dst_if->if_lastchange = time;
	dst_if->if_obytes += m->m_pkthdr.len;
	IF_ENQUEUE(&dst_if->if_snd, m);
	if (m->m_flags & M_MCAST)
		dst_if->if_omcasts++;
	if ((dst_if->if_flags & IFF_OACTIVE) == 0)
		(*dst_if->if_start)(dst_if);
	splx(s);
}

/*
 * Receive input from an interface.  Queue the packet for bridging if its
 * not for us, and schedule an interrupt.
 */
struct mbuf *
bridge_input(ifp, eh, m)
	struct ifnet *ifp;
	struct ether_header *eh;
	struct mbuf *m;
{
	struct bridge_softc *sc;
	int s;
	struct bridge_iflist *ifl;
	struct arpcom *ac;
	struct mbuf *mc;

	/*
	 * Make sure this interface is a bridge member.
	 */
	if (ifp == NULL || ifp->if_bridge == NULL || m == NULL)
		return (m);

	if ((m->m_flags & M_PKTHDR) == 0)
		panic("bridge_input(): no HDR");

	sc = (struct bridge_softc *)ifp->if_bridge;
	if ((sc->sc_if.if_flags & IFF_RUNNING) == 0)
		return (m);

	LIST_FOREACH(ifl, &sc->sc_iflist, next) {
		if (ifl->ifp == ifp)
			break;
	}
	if (ifl == LIST_END(&sc->sc_iflist))
		return (m);

	if (m->m_flags & (M_BCAST | M_MCAST)) {
		/* Tap off 802.1D packets, they do not get forwarded */
		if (bcmp(eh->ether_dhost, bstp_etheraddr, ETHER_ADDR_LEN) == 0) {
			m = bstp_input(sc, ifp, eh, m);
			if (m == NULL)
				return (NULL);
		}

		/*
		 * No need to queue frames for ifs in the blocking, disabled,
		 *  or listening state
		 */
		if ((ifl->bif_flags & IFBIF_STP) &&
		    ((ifl->bif_state == BSTP_IFSTATE_BLOCKING) ||
		     (ifl->bif_state == BSTP_IFSTATE_LISTENING) ||
		     (ifl->bif_state == BSTP_IFSTATE_DISABLED)))
			return (m);

		/*
		 * make a copy of 'm' with 'eh' tacked on to the
		 * beginning.  Return 'm' for local processing
		 * and enqueue the copy.  Schedule netisr.
		 */
		mc = m_copym2(m, 0, M_COPYALL, M_NOWAIT);
		if (mc == NULL)
			return (m);
		M_PREPEND(mc, sizeof(struct ether_header), M_DONTWAIT);
		if (mc == NULL)
			return (m);
		bcopy(eh, mtod(mc, caddr_t), sizeof(struct ether_header));
		s = splimp();
		if (IF_QFULL(&sc->sc_if.if_snd)) {
			m_freem(mc);
			splx(s);
			return (m);
		}
		IF_ENQUEUE(&sc->sc_if.if_snd, mc);
		splx(s);
		schednetisr(NETISR_BRIDGE);
		return (m);
	}

	/*
	 * No need to queue frames for ifs in the blocking, disabled, or
	 * listening state
	 */
	if ((ifl->bif_flags & IFBIF_STP) &&
	    ((ifl->bif_state == BSTP_IFSTATE_BLOCKING) ||
	     (ifl->bif_state == BSTP_IFSTATE_LISTENING) ||
	     (ifl->bif_state == BSTP_IFSTATE_DISABLED)))
		return (m);


	/*
	 * Unicast, make sure it's not for us.
	 */
	LIST_FOREACH(ifl, &sc->sc_iflist, next) {
		if (ifl->ifp->if_type != IFT_ETHER)
			continue;
		ac = (struct arpcom *)ifl->ifp;
		if (bcmp(ac->ac_enaddr, eh->ether_dhost, ETHER_ADDR_LEN) == 0) {
			if (ifl->bif_flags & IFBIF_LEARNING)
				bridge_rtupdate(sc,
				    (struct ether_addr *)&eh->ether_dhost,
				    ifp, 0, IFBAF_DYNAMIC);
			m->m_pkthdr.rcvif = ifl->ifp;
			return (m);
		}
		if (bcmp(ac->ac_enaddr, eh->ether_shost, ETHER_ADDR_LEN) == 0) {
			m_freem(m);
			return (NULL);
		}
	}
	M_PREPEND(m, sizeof(struct ether_header), M_DONTWAIT);
	if (m == NULL)
		return (NULL);
	bcopy(eh, mtod(m, caddr_t), sizeof(struct ether_header));
	s = splimp();
	if (IF_QFULL(&sc->sc_if.if_snd)) {
		m_freem(m);
		splx(s);
		return (NULL);
	}
	IF_ENQUEUE(&sc->sc_if.if_snd, m);
	splx(s);
	schednetisr(NETISR_BRIDGE);
	return (NULL);
}

/*
 * Send a frame to all interfaces that are members of the bridge
 * (except the one it came in on).  This code assumes that it is
 * running at splnet or higher.
 */
void
bridge_broadcast(sc, ifp, eh, m)
	struct bridge_softc *sc;
	struct ifnet *ifp;
	struct ether_header *eh;
	struct mbuf *m;
{
	struct bridge_iflist *p;
	struct mbuf *mc;
	struct ifnet *dst_if;
	int used = 0;

	LIST_FOREACH(p, &sc->sc_iflist, next) {
		/*
		 * Don't retransmit out of the same interface where
		 * the packet was received from.
		 */
		dst_if = p->ifp;
		if (dst_if->if_index == ifp->if_index)
			continue;

		if ((p->bif_flags & IFBIF_STP) &&
		    (p->bif_state == BSTP_IFSTATE_BLOCKING ||
		     p->bif_state == BSTP_IFSTATE_DISABLED))
			continue;

		if ((p->bif_flags & IFBIF_DISCOVER) == 0 &&
		    (m->m_flags & (M_BCAST | M_MCAST)) == 0)
			continue;

		if ((dst_if->if_flags & IFF_RUNNING) == 0)
			continue;

		if (IF_QFULL(&dst_if->if_snd)) {
			IF_DROP(&dst_if->if_snd);
			sc->sc_if.if_oerrors++;
			continue;
		}

		if (bridge_filterrule(&p->bif_brlout, eh) == BRL_ACTION_BLOCK)
			continue;

		/* If last one, reuse the passed-in mbuf */
		if (LIST_NEXT(p, next) == LIST_END(&sc->sc_iflist)) {
			mc = m;
			used = 1;
		} else {
			mc = m_copym(m, 0, M_COPYALL, M_DONTWAIT);
			if (mc == NULL) {
				sc->sc_if.if_oerrors++;
				continue;
			}
		}

		if (p->bif_flags & IFBIF_BLOCKNONIP &&
		    bridge_blocknonip(eh, mc)) {
			m_freem(mc);
			continue;
		}

		sc->sc_if.if_opackets++;
		sc->sc_if.if_obytes += mc->m_pkthdr.len;
		dst_if->if_obytes += m->m_pkthdr.len;
		dst_if->if_lastchange = time;
		IF_ENQUEUE(&dst_if->if_snd, mc);
		if (mc->m_flags & M_MCAST)
			dst_if->if_omcasts++;
		if ((dst_if->if_flags & IFF_OACTIVE) == 0)
			(*dst_if->if_start)(dst_if);
	}

	if (!used)
		m_freem(m);
}

struct ifnet *
bridge_rtupdate(sc, ea, ifp, setflags, flags)
	struct bridge_softc *sc;
	struct ether_addr *ea;
	struct ifnet *ifp;
	int setflags;
	u_int8_t flags;
{
	struct bridge_rtnode *p, *q;
	u_int32_t h;
	int dir;

	if (sc->sc_rts == NULL) {
		if (setflags && flags == IFBAF_STATIC) {
			sc->sc_rts = (struct bridge_rthead *)malloc(
			    BRIDGE_RTABLE_SIZE *
			    (sizeof(struct bridge_rthead)),M_DEVBUF,M_NOWAIT);
			if (sc->sc_rts == NULL)
				goto done;

			for (h = 0; h < BRIDGE_RTABLE_SIZE; h++) {
				LIST_INIT(&sc->sc_rts[h]);
			}
			get_random_bytes(&sc->sc_hashkey, sizeof(sc->sc_hashkey));
		} else
			goto done;
	}

	h = bridge_hash(sc, ea);
	p = LIST_FIRST(&sc->sc_rts[h]);
	if (p == LIST_END(&sc->sc_rts[h])) {
		if (sc->sc_brtcnt >= sc->sc_brtmax)
			goto done;
		p = (struct bridge_rtnode *)malloc(
		    sizeof(struct bridge_rtnode), M_DEVBUF, M_NOWAIT);
		if (p == NULL)
			goto done;

		bcopy(ea, &p->brt_addr, sizeof(p->brt_addr));
		p->brt_if = ifp;
		p->brt_age = 1;

		if (setflags)
			p->brt_flags = flags;
		else
			p->brt_flags = IFBAF_DYNAMIC;

		LIST_INSERT_HEAD(&sc->sc_rts[h], p, brt_next);
		sc->sc_brtcnt++;
		goto want;
	}

	do {
		q = p;
		p = LIST_NEXT(p, brt_next);

		dir = memcmp(ea, &q->brt_addr, sizeof(q->brt_addr));
		if (dir == 0) {
			if (setflags) {
				q->brt_if = ifp;
				q->brt_flags = flags;
			}

			if (q->brt_if == ifp)
				q->brt_age = 1;
			ifp = q->brt_if;
			goto want;
		}

		if (dir > 0) {
			if (sc->sc_brtcnt >= sc->sc_brtmax)
				goto done;
			p = (struct bridge_rtnode *)malloc(
			    sizeof(struct bridge_rtnode), M_DEVBUF, M_NOWAIT);
			if (p == NULL)
				goto done;

			bcopy(ea, &p->brt_addr, sizeof(p->brt_addr));
			p->brt_if = ifp;
			p->brt_age = 1;

			if (setflags)
				p->brt_flags = flags;
			else
				p->brt_flags = IFBAF_DYNAMIC;

			LIST_INSERT_BEFORE(q, p, brt_next);
			sc->sc_brtcnt++;
			goto want;
		}

		if (p == LIST_END(&sc->sc_rts[h])) {
			if (sc->sc_brtcnt >= sc->sc_brtmax)
				goto done;
			p = (struct bridge_rtnode *)malloc(
			    sizeof(struct bridge_rtnode), M_DEVBUF, M_NOWAIT);
			if (p == NULL)
				goto done;

			bcopy(ea, &p->brt_addr, sizeof(p->brt_addr));
			p->brt_if = ifp;
			p->brt_age = 1;

			if (setflags)
				p->brt_flags = flags;
			else
				p->brt_flags = IFBAF_DYNAMIC;
			LIST_INSERT_AFTER(q, p, brt_next);
			sc->sc_brtcnt++;
			goto want;
		}
	} while (p != LIST_END(&sc->sc_rts[h]));

done:
	ifp = NULL;
want:
	return (ifp);
}

struct ifnet *
bridge_rtlookup(sc, ea)
	struct bridge_softc *sc;
	struct ether_addr *ea;
{
	struct bridge_rtnode *p;
	u_int32_t h;
	int dir;

	if (sc->sc_rts == NULL)
		goto fail;

	h = bridge_hash(sc, ea);
	LIST_FOREACH(p, &sc->sc_rts[h], brt_next) {
		dir = memcmp(ea, &p->brt_addr, sizeof(p->brt_addr));
		if (dir == 0)
			return (p->brt_if);
		if (dir > 0)
			goto fail;
	}
fail:
	return (NULL);
}

/*
 * The following hash function is adapted from 'Hash Functions' by Bob Jenkins
 * ("Algorithm Alley", Dr. Dobbs Journal, September 1997).
 * "You may use this code any way you wish, private, educational, or
 *  commercial.  It's free."
 */
#define	mix(a,b,c) \
	do {						\
		a -= b; a -= c; a ^= (c >> 13);		\
		b -= c; b -= a; b ^= (a << 8);		\
		c -= a; c -= b; c ^= (b >> 13);		\
		a -= b; a -= c; a ^= (c >> 12);		\
		b -= c; b -= a; b ^= (a << 16);		\
		c -= a; c -= b; c ^= (b >> 5);		\
		a -= b; a -= c; a ^= (c >> 3);		\
		b -= c; b -= a; b ^= (a << 10);		\
		c -= a; c -= b; c ^= (b >> 15);		\
	} while(0)

u_int32_t
bridge_hash(sc, addr)
	struct bridge_softc *sc;
	struct ether_addr *addr;
{
	u_int32_t a = 0x9e3779b9, b = 0x9e3779b9, c = sc->sc_hashkey;

	b += addr->ether_addr_octet[5] << 8;
	b += addr->ether_addr_octet[4];
	a += addr->ether_addr_octet[3] << 24;
	a += addr->ether_addr_octet[2] << 16;
	a += addr->ether_addr_octet[1] << 8;
	a += addr->ether_addr_octet[0];

	mix(a, b, c);
	return (c & BRIDGE_RTABLE_MASK);
}

/*
 * Trim the routing table so that we've got a number of routes
 * less than or equal to the maximum.
 */
void
bridge_rttrim(sc)
	struct bridge_softc *sc;
{
	struct bridge_rtnode *n, *p;
	int i;

	if (sc->sc_rts == NULL)
		goto done;

	/*
	 * Make sure we have to trim the address table
	 */
	if (sc->sc_brtcnt <= sc->sc_brtmax)
		goto done;

	/*
	 * Force an aging cycle, this might trim enough addresses.
	 */
	bridge_rtage(sc);

	if (sc->sc_brtcnt <= sc->sc_brtmax)
		goto done;

	for (i = 0; i < BRIDGE_RTABLE_SIZE; i++) {
		n = LIST_FIRST(&sc->sc_rts[i]);
		while (n != LIST_END(&sc->sc_rts[i])) {
			p = LIST_NEXT(n, brt_next);
			if ((n->brt_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC) {
				LIST_REMOVE(n, brt_next);
				sc->sc_brtcnt--;
				free(n, M_DEVBUF);
				n = p;
				if (sc->sc_brtcnt <= sc->sc_brtmax)
					goto done;
			}
		}
	}

done:
	if (sc->sc_rts != NULL && sc->sc_brtcnt == 0 &&
	    (sc->sc_if.if_flags & IFF_UP) == 0) {
		free(sc->sc_rts, M_DEVBUF);
		sc->sc_rts = NULL;
	}
}

void
bridge_timer(vsc)
	void *vsc;
{
	struct bridge_softc *sc = vsc;
	int s;

	s = splsoftnet();
	bridge_rtage(sc);
	splx(s);
}

/*
 * Perform an aging cycle
 */
void
bridge_rtage(sc)
	struct bridge_softc *sc;
{
	struct bridge_rtnode *n, *p;
	int i;

	if (sc->sc_rts == NULL)
		return;

	for (i = 0; i < BRIDGE_RTABLE_SIZE; i++) {
		n = LIST_FIRST(&sc->sc_rts[i]);
		while (n != LIST_END(&sc->sc_rts[i])) {
			if ((n->brt_flags & IFBAF_TYPEMASK) == IFBAF_STATIC) {
				n->brt_age = !n->brt_age;
				if (n->brt_age)
					n->brt_age = 0;
				n = LIST_NEXT(n, brt_next);
			} else if (n->brt_age) {
				n->brt_age = 0;
				n = LIST_NEXT(n, brt_next);
			} else {
				p = LIST_NEXT(n, brt_next);
				LIST_REMOVE(n, brt_next);
				sc->sc_brtcnt--;
				free(n, M_DEVBUF);
				n = p;
			}
		}
	}

	if (sc->sc_brttimeout != 0)
		timeout_add(&sc->sc_brtimeout, sc->sc_brttimeout * hz);
}

/*
 * Remove all dynamic addresses from the cache
 */
int
bridge_rtflush(sc, full)
	struct bridge_softc *sc;
	int full;
{
	int i;
	struct bridge_rtnode *p, *n;

	if (sc->sc_rts == NULL)
		return (0);

	for (i = 0; i < BRIDGE_RTABLE_SIZE; i++) {
		n = LIST_FIRST(&sc->sc_rts[i]);
		while (n != LIST_END(&sc->sc_rts[i])) {
			if (full ||
			    (n->brt_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC) {
				p = LIST_NEXT(n, brt_next);
				LIST_REMOVE(n, brt_next);
				sc->sc_brtcnt--;
				free(n, M_DEVBUF);
				n = p;
			} else
				n = LIST_NEXT(n, brt_next);
		}
	}

	if (sc->sc_brtcnt == 0 && (sc->sc_if.if_flags & IFF_UP) == 0) {
		free(sc->sc_rts, M_DEVBUF);
		sc->sc_rts = NULL;
	}

	return (0);
}

/*
 * Remove an address from the cache
 */
int
bridge_rtdaddr(sc, ea)
	struct bridge_softc *sc;
	struct ether_addr *ea;
{
	int h;
	struct bridge_rtnode *p;

	if (sc->sc_rts == NULL)
		return (ENOENT);

	h = bridge_hash(sc, ea);
	LIST_FOREACH(p, &sc->sc_rts[h], brt_next) {
		if (bcmp(ea, &p->brt_addr, sizeof(p->brt_addr)) == 0) {
			LIST_REMOVE(p, brt_next);
			sc->sc_brtcnt--;
			free(p, M_DEVBUF);
			if (sc->sc_brtcnt == 0 &&
			    (sc->sc_if.if_flags & IFF_UP) == 0) {
				free(sc->sc_rts, M_DEVBUF);
				sc->sc_rts = NULL;
			}
			return (0);
		}
	}

	return (ENOENT);
}
/*
 * Delete routes to a specific interface member.
 */
void
bridge_rtdelete(sc, ifp)
	struct bridge_softc *sc;
	struct ifnet *ifp;
{
	int i;
	struct bridge_rtnode *n, *p;

	if (sc->sc_rts == NULL)
		return;

	/*
	 * Loop through all of the hash buckets and traverse each
	 * chain looking for routes to this interface.
	 */
	for (i = 0; i < BRIDGE_RTABLE_SIZE; i++) {
		n = LIST_FIRST(&sc->sc_rts[i]);
		while (n != LIST_END(&sc->sc_rts[i])) {
			if (n->brt_if == ifp) {		/* found one */
				p = LIST_NEXT(n, brt_next);
				LIST_REMOVE(n, brt_next);
				sc->sc_brtcnt--;
				free(n, M_DEVBUF);
				n = p;
			} else
				n = LIST_NEXT(n, brt_next);
		}
	}
	if (sc->sc_brtcnt == 0 && (sc->sc_if.if_flags & IFF_UP) == 0) {
		free(sc->sc_rts, M_DEVBUF);
		sc->sc_rts = NULL;
	}
}

/*
 * Gather all of the routes for this interface.
 */
int
bridge_rtfind(sc, baconf)
	struct bridge_softc *sc;
	struct ifbaconf *baconf;
{
	int i, error = 0;
	u_int32_t cnt = 0;
	struct bridge_rtnode *n;
	struct ifbareq bareq;

	if (sc->sc_rts == NULL || baconf->ifbac_len == 0)
		goto done;

	for (i = 0, cnt = 0; i < BRIDGE_RTABLE_SIZE; i++) {
		LIST_FOREACH(n, &sc->sc_rts[i], brt_next) {
			if (baconf->ifbac_len < sizeof(struct ifbareq))
				goto done;
			bcopy(sc->sc_if.if_xname, bareq.ifba_name,
			    sizeof(bareq.ifba_name));
			bcopy(n->brt_if->if_xname, bareq.ifba_ifsname,
			    sizeof(bareq.ifba_ifsname));
			bcopy(&n->brt_addr, &bareq.ifba_dst,
			    sizeof(bareq.ifba_dst));
			bareq.ifba_age = n->brt_age;
			bareq.ifba_flags = n->brt_flags;
			error = copyout((caddr_t)&bareq,
		    	    (caddr_t)(baconf->ifbac_req + cnt), sizeof(bareq));
			if (error)
				goto done;
			cnt++;
			baconf->ifbac_len -= sizeof(struct ifbareq);
		}
	}
done:
	baconf->ifbac_len = cnt * sizeof(struct ifbareq);
	return (error);
}

/*
 * Block non-ip frames:
 * Returns 0 if frame is ip, and 1 if it should be dropped.
 */
int
bridge_blocknonip(eh, m)
	struct ether_header *eh;
	struct mbuf *m;
{
	struct llc llc;
	u_int16_t etype;

	if (m->m_pkthdr.len < sizeof(struct ether_header))
		return (1);

	etype = ntohs(eh->ether_type);
	switch (etype) {
	case ETHERTYPE_ARP:
	case ETHERTYPE_REVARP:
	case ETHERTYPE_IP:
	case ETHERTYPE_IPV6:
		return (0);
	}

	if (etype > ETHERMTU)
		return (1);

	if (m->m_pkthdr.len <
	    (sizeof(struct ether_header) + LLC_SNAPFRAMELEN))
		return (1);

	m_copydata(m, sizeof(struct ether_header), LLC_SNAPFRAMELEN,
	    (caddr_t)&llc);

	etype = ntohs(llc.llc_snap.ether_type);
	if (llc.llc_dsap == LLC_SNAP_LSAP &&
	    llc.llc_ssap == LLC_SNAP_LSAP &&
	    llc.llc_control == LLC_UI &&
	    llc.llc_snap.org_code[0] == 0 &&
	    llc.llc_snap.org_code[1] == 0 &&
	    llc.llc_snap.org_code[2] == 0 &&
	    (etype == ETHERTYPE_ARP ||
	     etype == ETHERTYPE_REVARP ||
	     etype == ETHERTYPE_IP ||
	     etype == ETHERTYPE_IPV6)) {
		return (0);
	}

	return (1);
}

u_int8_t
bridge_filterrule(h, eh)
	struct brl_head *h;
	struct ether_header *eh;
{
	struct brl_node *n;
	u_int8_t flags;

	SIMPLEQ_FOREACH(n, h, brl_next) {
		flags = n->brl_flags & (BRL_FLAG_SRCVALID|BRL_FLAG_DSTVALID);
		if (flags == 0)
			return (n->brl_action);
		if (flags == (BRL_FLAG_SRCVALID|BRL_FLAG_DSTVALID)) {
			if (bcmp(eh->ether_shost, &n->brl_src, ETHER_ADDR_LEN))
				continue;
			if (bcmp(eh->ether_dhost, &n->brl_src, ETHER_ADDR_LEN))
				continue;
			return (n->brl_action);
		}
		if (flags == BRL_FLAG_SRCVALID) {
			if (bcmp(eh->ether_shost, &n->brl_src, ETHER_ADDR_LEN))
				continue;
			return (n->brl_action);
		}
		if (flags == BRL_FLAG_DSTVALID) {
			if (bcmp(eh->ether_dhost, &n->brl_dst, ETHER_ADDR_LEN))
				continue;
			return (n->brl_action);
		}
	}
	return (BRL_ACTION_PASS);
}

int
bridge_addrule(bif, req, out)
	struct bridge_iflist *bif;
	struct ifbrlreq *req;
	int out;
{
	struct brl_node *n;

	n = (struct brl_node *)malloc(sizeof(struct brl_node), M_DEVBUF, M_NOWAIT);
	if (n == NULL)
		return (ENOMEM);
	bcopy(&req->ifbr_src, &n->brl_src, sizeof(struct ether_addr));
	bcopy(&req->ifbr_dst, &n->brl_dst, sizeof(struct ether_addr));
	n->brl_action = req->ifbr_action;
	n->brl_flags = req->ifbr_flags;
	if (out) {
		n->brl_flags &= ~BRL_FLAG_IN;
		n->brl_flags |= BRL_FLAG_OUT;
		SIMPLEQ_INSERT_TAIL(&bif->bif_brlout, n, brl_next);
	} else {
		n->brl_flags &= ~BRL_FLAG_OUT;
		n->brl_flags |= BRL_FLAG_IN;
		SIMPLEQ_INSERT_TAIL(&bif->bif_brlin, n, brl_next);
	}
	return (0);
}

int
bridge_flushrule(bif)
	struct bridge_iflist *bif;
{
	struct brl_node *p;

	while (!SIMPLEQ_EMPTY(&bif->bif_brlin)) {
		p = SIMPLEQ_FIRST(&bif->bif_brlin);
		SIMPLEQ_REMOVE_HEAD(&bif->bif_brlin, p, brl_next);
		free(p, M_DEVBUF);
	}
	while (!SIMPLEQ_EMPTY(&bif->bif_brlout)) {
		p = SIMPLEQ_FIRST(&bif->bif_brlout);
		SIMPLEQ_REMOVE_HEAD(&bif->bif_brlin, p, brl_next);
		free(p, M_DEVBUF);
	}
	return (0);
}

#if defined(INET) && (defined(IPFILTER) || defined(IPFILTER_LKM))

/*
 * Filter IP packets by peeking into the ethernet frame.  This violates
 * the ISO model, but allows us to act as a IP filter at the data link
 * layer.  As a result, most of this code will look familiar to those
 * who've read net/if_ethersubr.c and netinet/ip_input.c
 */
struct mbuf *
bridge_filter(sc, ifp, eh, m)
	struct bridge_softc *sc;
	struct ifnet *ifp;
	struct ether_header *eh;
	struct mbuf *m;
{
	struct llc llc;
	int hassnap = 0;
	struct ip *ip;
	int hlen;

	if (fr_checkp == NULL)
		return (m);

	if (eh->ether_type != htons(ETHERTYPE_IP)) {
		if (eh->ether_type > ETHERMTU ||
		    m->m_pkthdr.len < (LLC_SNAPFRAMELEN +
		    sizeof(struct ether_header)))
			return (m);

		m_copydata(m, sizeof(struct ether_header),
		    LLC_SNAPFRAMELEN, (caddr_t)&llc);

		if (llc.llc_dsap != LLC_SNAP_LSAP ||
		    llc.llc_ssap != LLC_SNAP_LSAP ||
		    llc.llc_control != LLC_UI ||
		    llc.llc_snap.org_code[0] ||
		    llc.llc_snap.org_code[1] ||
		    llc.llc_snap.org_code[2] ||
		    llc.llc_snap.ether_type != htons(ETHERTYPE_IP))
			return (m);
		hassnap = 1;
	}

	m_adj(m, sizeof(struct ether_header));
	if (hassnap)
		m_adj(m, LLC_SNAPFRAMELEN);

	if (m->m_pkthdr.len < sizeof(struct ip))
		goto dropit;

	/* Copy minimal header, and drop invalids */
	if (m->m_len < sizeof(struct ip) &&
	    (m = m_pullup(m, sizeof(struct ip))) == NULL)
		return (NULL);
	ip = mtod(m, struct ip *);

	if (ip->ip_v != IPVERSION)
		goto dropit;

	hlen = ip->ip_hl << 2;	/* get whole header length */
	if (hlen < sizeof(struct ip))
		goto dropit;
	if (hlen > m->m_len) {
		if ((m = m_pullup(m, sizeof(struct ip))) == NULL)
			return (NULL);
		ip = mtod(m, struct ip *);
	}

	if ((ip->ip_sum = in_cksum(m, hlen)) != 0)
		goto dropit;

	NTOHS(ip->ip_len);
	if (ip->ip_len < hlen)
		goto dropit;
	NTOHS(ip->ip_id);
	NTOHS(ip->ip_off);

	if (m->m_pkthdr.len < ip->ip_len)
		goto dropit;
	if (m->m_pkthdr.len > ip->ip_len) {
		if (m->m_len == m->m_pkthdr.len) {
			m->m_len = ip->ip_len;
			m->m_pkthdr.len = ip->ip_len;
		} else
			m_adj(m, ip->ip_len - m->m_pkthdr.len);
	}

	/* Finally, we get to filter the packet! */
	if (fr_checkp && (*fr_checkp)(ip, hlen, ifp, 0, &m))
		return (NULL);

	/* Rebuild the IP header */
	if (m->m_len < hlen && ((m = m_pullup(m, hlen)) == NULL))
		return (NULL);
	if (m->m_len < sizeof(struct ip))
		goto dropit;
	ip = mtod(m, struct ip *);
	HTONS(ip->ip_len);
	HTONS(ip->ip_id);
	HTONS(ip->ip_off);
	ip->ip_sum = in_cksum(m, hlen);

	/* Reattach SNAP header */
	if (hassnap) {
		M_PREPEND(m, LLC_SNAPFRAMELEN, M_DONTWAIT);
		if (m == NULL)
			goto dropit;
		bcopy(&llc, mtod(m, caddr_t), LLC_SNAPFRAMELEN);
	}

	/* Reattach ethernet header */
	M_PREPEND(m, sizeof(*eh), M_DONTWAIT);
	if (m == NULL)
		goto dropit;
	bcopy(eh, mtod(m, caddr_t), sizeof(*eh));

	return (m);

dropit:
	if (m != NULL)
		m_freem(m);
	return (NULL);
}
#endif

#endif  /* NBRIDGE */
