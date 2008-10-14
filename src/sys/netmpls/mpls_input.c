/*	$OpenBSD: mpls_input.c,v 1.14 2008/10/14 20:43:33 michele Exp $	*/

/*
 * Copyright (c) 2008 Claudio Jeker <claudio@openbsd.org>
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

#include "mpe.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netmpls/mpls.h>

struct ifqueue	mplsintrq;
int		mplsqmaxlen = IFQ_MAXLEN;
extern int	mpls_inkloop;

#ifdef MPLS_DEBUG
#define MPLS_LABEL_GET(l)	((ntohl((l) & MPLS_LABEL_MASK)) >> MPLS_LABEL_OFFSET)
#define MPLS_TTL_GET(l)		(ntohl((l) & MPLS_TTL_MASK))
#endif

void
mpls_init(void)
{
	mplsintrq.ifq_maxlen = mplsqmaxlen;
}

void
mplsintr(void)
{
	struct mbuf *m;
	int s;

	for (;;) {
		/* Get next datagram of input queue */
		s = splnet();
		IF_DEQUEUE(&mplsintrq, m);
		splx(s);
		if (m == NULL)
			return;
#ifdef DIAGNOSTIC
		if ((m->m_flags & M_PKTHDR) == 0)
			panic("ipintr no HDR");
#endif
		mpls_input(m);
	}
}

void
mpls_input(struct mbuf *m)
{
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct sockaddr_mpls *smpls;
	struct sockaddr_mpls sa_mpls;
	struct shim_hdr *shim;
	struct rtentry *rt = NULL;
	u_int32_t ttl;
	int i, hasbos;

	if (!mpls_enable) {
		m_freem(m);
		return;
	}

	/* drop all broadcast and multicast packets */
	if (m->m_flags & (M_BCAST | M_MCAST)) {
		m_freem(m);
		return;
	}

	if (m->m_len < sizeof(*shim))
		if ((m = m_pullup(m, sizeof(*shim))) == NULL)
			return;

	shim = mtod(m, struct shim_hdr *);

#ifdef MPLS_DEBUG
	printf("mpls_input: iface %s label=%d, ttl=%d BoS %d\n",
	    ifp->if_xname, MPLS_LABEL_GET(shim->shim_label),
	    MPLS_TTL_GET(shim->shim_label),
	    MPLS_BOS_ISSET(shim->shim_label));
#endif	/* MPLS_DEBUG */

	/* check and decrement TTL */
	ttl = ntohl(shim->shim_label & MPLS_TTL_MASK);
	if (ttl <= 1) {
		/* TTL exceeded */
		/*
		 * XXX if possible hand packet up to network layer so that an
		 * ICMP TTL exceeded can be sent back.
		 */
		m_freem(m);
		return;
	}
	ttl = htonl(ttl - 1);

	for (i = 0; i < mpls_inkloop; i++) {
		bzero(&sa_mpls, sizeof(sa_mpls));
		smpls = &sa_mpls;
		smpls->smpls_family = AF_MPLS;
		smpls->smpls_len = sizeof(*smpls);
		smpls->smpls_in_ifindex = ifp->if_index;
		smpls->smpls_in_label = shim->shim_label & MPLS_LABEL_MASK;

#ifdef MPLS_DEBUG
		printf("smpls af %d len %d in_label %d in_ifindex %d\n",
		    smpls->smpls_family, smpls->smpls_len,
		    MPLS_LABEL_GET(smpls->smpls_in_label),
		    smpls->smpls_in_ifindex);
#endif

		if (ntohl(smpls->smpls_in_label) < MPLS_LABEL_RESERVED_MAX) {

			hasbos = MPLS_BOS_ISSET(shim->shim_label);
			m = mpls_shim_pop(m);
			shim = mtod(m, struct shim_hdr *);

			switch (ntohl(smpls->smpls_in_label)) { 

			case MPLS_LABEL_IPV4NULL:
				if (hasbos) {
					mpe_input(m, NULL, smpls, ttl);
					goto done;
				} else
					continue;

			case MPLS_LABEL_IPV6NULL:
				if (hasbos) {
					mpe_input6(m, NULL, smpls, ttl);
					goto done;
				} else
					continue;
			}
			/* Other cases are not handled for now */
		}

		rt = rtalloc1(smplstosa(smpls), 1, 0);

		if (rt == NULL) {
			/* no entry for this label */
#ifdef MPLS_DEBUG
			printf("MPLS_DEBUG: label not found\n");
#endif
			m_freem(m);
			goto done;
		}

		rt->rt_use++;
		smpls = satosmpls(rt_key(rt));
#ifdef MPLS_DEBUG
		printf("route af %d len %d in_label %d in_ifindex %d\n",
		    smpls->smpls_family, smpls->smpls_len,
		    MPLS_LABEL_GET(smpls->smpls_in_label),
		    smpls->smpls_in_ifindex);
		printf("\top %d out_label %d out_ifindex %d\n",
		    smpls->smpls_operation, 
		    MPLS_LABEL_GET(smpls->smpls_out_label), 
		    smpls->smpls_out_ifindex);
#endif

		switch (smpls->smpls_operation) {
		case MPLS_OP_POP:
			hasbos = MPLS_BOS_ISSET(shim->shim_label);
			m = mpls_shim_pop(m);
			if (hasbos) {
#if NMPE > 0
				if (rt->rt_ifp->if_type == IFT_MPLS) {
					mpe_input(m, rt->rt_ifp, smpls, ttl);
					goto done;
				}
#endif
				/* last label but we have no clue so drop */
				m_freem(m);
				goto done;
			}
			break;
		case MPLS_OP_PUSH:
			m = mpls_shim_push(m, smpls);
			break;
		case MPLS_OP_SWAP:
			m = mpls_shim_swap(m, smpls);
			break;
		default:
			m_freem(m);
			goto done;
		}

		if (m == NULL)
			goto done;

		/* refetch label */
		shim = mtod(m, struct shim_hdr *);
		ifp = rt->rt_ifp;

		if (smpls->smpls_out_ifindex)
			break;

		RTFREE(rt);
		rt = NULL;
	}

	/* write back TTL */
	shim->shim_label = (shim->shim_label & ~MPLS_TTL_MASK) | ttl;

#ifdef MPLS_DEBUG
	printf("MPLS: sending on %s outlabel %x dst af %d in %d out %d\n",
    	    ifp->if_xname, ntohl(shim->shim_label), smpls->smpls_family,
	    MPLS_LABEL_GET(smpls->smpls_in_label),
	    MPLS_LABEL_GET(smpls->smpls_out_label));
#endif

	(*ifp->if_output)(ifp, m, smplstosa(smpls), rt);
done:
	if (rt)
		RTFREE(rt);
}
