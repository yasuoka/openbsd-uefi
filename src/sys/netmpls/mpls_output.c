/* $OpenBSD: mpls_output.c,v 1.4 2009/01/08 12:47:45 michele Exp $ */

/*
 * Copyright (c) 2008 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2008 Michele Marchetto <michele@openbsd.org>
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

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/route.h>

#include <netmpls/mpls.h>

extern int	mpls_inkloop;

#ifdef MPLS_DEBUG
#define MPLS_LABEL_GET(l)	((ntohl((l) & MPLS_LABEL_MASK)) >> MPLS_LABEL_OFFSET)
#endif

void
mpls_output(struct mbuf *m)
{
	struct ifnet		*ifp = m->m_pkthdr.rcvif;
	struct sockaddr_mpls	*smpls;
	struct sockaddr_mpls	*newsmpls;
	struct sockaddr_mpls	 sa_mpls, sa_outmpls;
	struct shim_hdr		*shim;
	struct rtentry		*rt = NULL;
	u_int32_t		 ttl;
	int			 i;

	if (!mpls_enable) {
		m_freem(m);
		return;
	}

	/* reset broadcast and multicast flags, this is a P2P tunnel */
	m->m_flags &= ~(M_BCAST | M_MCAST);

	if (m->m_len < sizeof(*shim))
		if ((m = m_pullup(m, sizeof(*shim))) == NULL)
			return;

	bzero(&sa_outmpls, sizeof(sa_outmpls));
	newsmpls = &sa_outmpls;
	newsmpls->smpls_family = AF_MPLS;
	newsmpls->smpls_len = sizeof(*smpls);

	shim = mtod(m, struct shim_hdr *);

	/* extract TTL */
	ttl = shim->shim_label & MPLS_TTL_MASK;

	for (i = 0; i < mpls_inkloop; i++) {
		bzero(&sa_mpls, sizeof(sa_mpls));
		smpls = &sa_mpls;
		smpls->smpls_family = AF_MPLS;
		smpls->smpls_len = sizeof(*smpls);
		smpls->smpls_label = shim->shim_label & MPLS_LABEL_MASK;

#ifdef MPLS_DEBUG
		printf("smpls af %d len %d in_label %d in_ifindex %d\n",
		    smpls->smpls_family, smpls->smpls_len,
		    MPLS_LABEL_GET(smpls->smpls_label),
		    ifp->if_index);
#endif

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
		newsmpls->smpls_label = rt->rt_mpls;

#ifdef MPLS_DEBUG
		printf("route af %d len %d in_label %d in_ifindex %d\n",
		    smpls->smpls_family, smpls->smpls_len,
		    MPLS_LABEL_GET(smpls->smpls_label),
		    ifp->if_index);
#endif

		switch (rt->rt_flags & (MPLS_OP_PUSH | MPLS_OP_POP |
		    MPLS_OP_SWAP)) {

		case MPLS_OP_POP:
			if (MPLS_BOS_ISSET(shim->shim_label)) {
				/* drop to avoid loops */
				m_freem(m);
				goto done;
			}

			m = mpls_shim_pop(m);
			break;
		case MPLS_OP_PUSH:
			m = mpls_shim_push(m, newsmpls);
			break;
		case MPLS_OP_SWAP:
			m = mpls_shim_swap(m, newsmpls);
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

		if (ifp != NULL)
			break;

		RTFREE(rt);
		rt = NULL;
	}

	/* write back TTL */
	shim->shim_label = (shim->shim_label & ~MPLS_TTL_MASK) | ttl;

#ifdef MPLS_DEBUG
	printf("MPLS: sending on %s outlabel %x dst af %d in %d out %d\n",
	    ifp->if_xname, ntohl(shim->shim_label), smpls->smpls_family,
	    MPLS_LABEL_GET(smpls->smpls_label),
	    MPLS_LABEL_GET(smpls->smpls_label));
#endif

	(*ifp->if_output)(ifp, m, smplstosa(smpls), rt);
done:
	if (rt)
		RTFREE(rt);
}
