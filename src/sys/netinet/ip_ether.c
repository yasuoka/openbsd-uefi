/*	$OpenBSD: ip_ether.c,v 1.18 2001/02/01 00:14:14 jason Exp $  */

/*
 * The author of this code is Angelos D. Keromytis (kermit@adk.gr)
 *
 * This code was written by Angelos D. Keromytis for OpenBSD in October 1999.
 *
 * Copyright (C) 1999 by Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software. 
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

/*
 * Ethernet-inside-IP processing
 */

#include "bridge.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/route.h>
#include <net/netisr.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#endif /* INET */

#include <netinet/ip_ipsp.h>
#include <netinet/ip_ether.h>
#include <netinet/if_ether.h>
#include <dev/rndvar.h>
#include <net/if_bridge.h>
#include <net/if_gif.h>

#include "gif.h"

#ifdef ENCDEBUG
#define DPRINTF(x)	if (encdebug) printf x
#else
#define DPRINTF(x)
#endif

#ifndef offsetof
#define offsetof(s, e) ((int)&((s *)0)->e)
#endif

/*
 * We can control the acceptance of EtherIP packets by altering the sysctl
 * net.inet.etherip.allow value. Zero means drop them, all else is acceptance.
 */
int etherip_allow = 0;

struct etheripstat etheripstat;

/*
 * etherip_input gets called when we receive an encapsulated packet,
 * either because we got it at a real interface, or because AH or ESP
 * were being used in tunnel mode (in which case the rcvif element will 
 * contain the address of the encX interface associated with the tunnel.
 */

void
#if __STDC__
etherip_input(struct mbuf *m, ...)
#else
etherip_input(m, va_alist)
struct mbuf *m;
va_dcl
#endif
{
    union sockaddr_union ssrc, sdst;
    struct ether_header eh;
    struct mbuf *mrest, *m0;
    int iphlen, clen;
    u_int8_t v;
    va_list ap;

#if NGIF > 0
    int i;
#endif /* NGIF */

    va_start(ap, m);
    iphlen = va_arg(ap, int);
    va_end(ap);

    etheripstat.etherip_ipackets++;

    /* If we do not accept EtherIP explicitly, drop. */
    if (!etherip_allow && (m->m_flags & (M_AUTH|M_CONF)) == 0)
    {
	DPRINTF(("etherip_input(): dropped due to policy\n"));
	etheripstat.etherip_pdrops++;
	m_freem(m);
	return;
    }

    /*
     * Make sure there's at least an ethernet header's plus one byte
     * (EtherIP header) worth of data after the outer IP header.
     */
    if (m->m_pkthdr.len < iphlen + sizeof(struct ether_header) +
	sizeof(u_int8_t))
    {
	DPRINTF(("etherip_input(): encapsulated packet too short\n"));
	etheripstat.etherip_hdrops++;
	m_freem(m);
	return;
    }

    /* Verify EtherIP version number */
    m_copydata(m, iphlen, sizeof(u_int8_t), &v);
    if (v != (ETHERIP_VERSION << 4))
    {
	/*
	 * Note that the other potential failure of the above check is that the
	 * second nibble of the EtherIP header (the reserved part) is not
	 * zero; this is also invalid protocol behaviour.
	 */
	if (v & 0x0f)
	{
	    DPRINTF(("etherip_input(): received invalid EtherIP header (reserved field non-zero\n"));
	}
	else
	{
	    DPRINTF(("etherip_input(): received EtherIP version number %d not suppoorted\n", (v >> 4) & 0xff));
	}
	etheripstat.etherip_adrops++;
	m_freem(m);
	return;
    }

    /* Make sure the ethernet header at least is in the first mbuf. */
    if (m->m_len < iphlen + sizeof(struct ether_header) + sizeof(u_int8_t))
    {
	if ((m = m_pullup(m, iphlen + sizeof(struct ether_header) +
			  sizeof(u_int8_t))) == 0)
	{
	    DPRINTF(("etherip_input(): m_pullup() failed\n"));
	    etheripstat.etherip_adrops++;
	    m_freem(m);
	    return;
	}
    }

    /* Copy the addresses for use later */
    bzero(&ssrc, sizeof(ssrc));
    bzero(&sdst, sizeof(sdst));

    m_copydata(m, 0, sizeof(u_int8_t), &v);
    switch (v >> 4)
    {
#ifdef INET
	case 4:
	    ssrc.sa.sa_len = sdst.sa.sa_len = sizeof(struct sockaddr_in);
	    ssrc.sa.sa_family = sdst.sa.sa_family = AF_INET;
	    m_copydata(m, offsetof(struct ip, ip_src), sizeof(struct in_addr),
		       (caddr_t) &ssrc.sin.sin_addr);
	    m_copydata(m, offsetof(struct ip, ip_dst), sizeof(struct in_addr),
		       (caddr_t) &sdst.sin.sin_addr);
	    break;
#endif /* INET */
#ifdef INET6
	case 6:
	    ssrc.sa.sa_len = sdst.sa.sa_len = sizeof(struct sockaddr_in6);
	    ssrc.sa.sa_family = sdst.sa.sa_family = AF_INET6;
	    m_copydata(m, offsetof(struct ip6_hdr, ip6_src),
		       sizeof(struct in6_addr),
		       (caddr_t) &ssrc.sin6.sin6_addr);
	    m_copydata(m, offsetof(struct ip6_hdr, ip6_dst),
		       sizeof(struct in6_addr),
		       (caddr_t) &sdst.sin6.sin6_addr);
	    break;
#endif /* INET6 */
	default:
	    m_freem(m);
	    etheripstat.etherip_hdrops++;
	    return /* EAFNOSUPPORT */;
    }

    /* Chop off the `outer' IP and EtherIP headers and reschedule. */
    m->m_len -= (iphlen + sizeof(u_int8_t));
    m->m_pkthdr.len -= (iphlen + sizeof(u_int8_t));
    m->m_data += (iphlen + sizeof(u_int8_t));

    /* Statistics */
    etheripstat.etherip_ibytes += m->m_pkthdr.len;

    /* Copy ethernet header */
    m_copydata(m, 0, sizeof(eh), (void *) &eh);

    m->m_flags &= ~(M_BCAST|M_MCAST);
    if (eh.ether_dhost[0] & 1)
    {
	if (bcmp((caddr_t) etherbroadcastaddr, (caddr_t) eh.ether_dhost,
	  sizeof(etherbroadcastaddr)) == 0)
	    m->m_flags |= M_BCAST;
	else
	    m->m_flags |= M_BCAST;
    }

    if (m->m_flags & (M_BCAST|M_MCAST))
      m->m_pkthdr.rcvif->if_imcasts++;

    /* Trim the beginning of the mbuf, to remove the ethernet header */
    m_adj(m, sizeof(struct ether_header));

    /* Copy out the first MHLEN bytes of data to ensure correct alignment */
    MGETHDR(m0, M_DONTWAIT, MT_DATA);
    if (m0 == NULL) {
	    m_freem(m);
	    etheripstat.etherip_adrops++;
	    return;
    }
    M_COPY_PKTHDR(m0, m);
    clen = min(MHLEN, m->m_pkthdr.len);
    if (m->m_pkthdr.len == clen)
      mrest = NULL;
    else {
	mrest = m_split(m, clen, M_DONTWAIT);
	if (mrest == NULL) {
	    m_freem(m);
	    m_freem(m0);
	    etheripstat.etherip_adrops++;
	    return;
	}
    }
    m0->m_next = mrest;
    m0->m_len = clen;
    m_copydata(m, 0, clen, mtod(m0, caddr_t));
    m_freem(m);
    m = m0;

#if NGIF > 0
    /* Find appropriate gif(4) interface */
    for (i = 0; i < ngif; i++)
    {
	if ((gif[i].gif_psrc == NULL) || (gif[i].gif_pdst == NULL) ||
	    !(gif[i].gif_if.if_flags & (IFF_UP|IFF_RUNNING)))
	  continue;

	if (!bcmp(gif[i].gif_psrc, &sdst, gif[i].gif_psrc->sa_len) &&
	    !bcmp(gif[i].gif_pdst, &ssrc, gif[i].gif_pdst->sa_len) &&
	    gif[i].gif_if.if_bridge != NULL)
	  break;
    }

    /* None found */
    if (i >= ngif)
    {
	DPRINTF(("etherip_input(): no interface found\n"));
        etheripstat.etherip_noifdrops++;
	m_freem(m);
	return;
    }

#if NBRIDGE > 0
    /*
     * Tap the packet off here for a bridge. bridge_input() returns
     * NULL if it has consumed the packet, otherwise, it
     * gets processed as normal.
     */
    m->m_pkthdr.rcvif = &gif[i].gif_if;
    m = bridge_input(&gif[i].gif_if, &eh, m);
    if (m == NULL)
      return;
#endif /* NBRIDGE */
#endif /* NGIF */

    etheripstat.etherip_noifdrops++;
    m_freem(m);
    return;
}

#ifdef IPSEC
int
etherip_output(struct mbuf *m, struct tdb *tdb, struct mbuf **mp, int skip,
	       int protoff)
{
#ifdef INET
    struct ip *ipo;
#endif /* INET */

#ifdef INET6
    struct ip6_hdr *ip6;
#endif /* INET6 */

    struct mbuf *m0;
    ushort hlen;
    u_int8_t v;

    /* Some address family sanity checks */
    if ((tdb->tdb_src.sa.sa_family != 0) &&
        (tdb->tdb_src.sa.sa_family != AF_INET) &&
	(tdb->tdb_src.sa.sa_family != AF_INET6))
    {
        DPRINTF(("etherip_output(): IP in protocol-family <%d> attempted, aborting", tdb->tdb_src.sa.sa_family));
	etheripstat.etherip_adrops++;
        m_freem(m);
        return EINVAL;
    }

    if ((tdb->tdb_dst.sa.sa_family != AF_INET) &&
	(tdb->tdb_dst.sa.sa_family != AF_INET6))
    {
	DPRINTF(("etherip_output(): IP in protocol-family <%d> attempted, aborting", tdb->tdb_dst.sa.sa_family));
	etheripstat.etherip_adrops++;
	m_freem(m);
	return EINVAL;
    }

    if (tdb->tdb_dst.sa.sa_family != tdb->tdb_src.sa.sa_family)
    {
	DPRINTF(("etherip_output(): mismatch in tunnel source and destination address protocol families (%d/%d), aborting", tdb->tdb_src.sa.sa_family, tdb->tdb_dst.sa.sa_family));
	etheripstat.etherip_adrops++;
	m_freem(m);
	return EINVAL;
    }

    switch (tdb->tdb_dst.sa.sa_family)
    {
#ifdef INET
	case AF_INET:
	    hlen = sizeof(struct ip);
	    break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
	    hlen = sizeof(struct ip6_hdr);
	    break;
#endif /* INET6 */
	default:
	    DPRINTF(("etherip_output(): unsupported tunnel protocol family <%d>, aborting", tdb->tdb_dst.sa.sa_family));
	    etheripstat.etherip_adrops++;
	    m_freem(m);
	    return EINVAL;
    }

    /* Don't forget the EtherIP header */
    hlen += sizeof(u_int8_t);

    MGETHDR(m0, M_DONTWAIT, MT_DATA);
    if (m0 == NULL) {
	DPRINTF(("etherip_output(): M_GETHDR failed\n"));
	etheripstat.etherip_adrops++;
	m_freem(m);
      	return ENOBUFS;
    }
    M_COPY_PKTHDR(m0, m);
    m0->m_next = m;
    m0->m_len = hlen;
    m0->m_pkthdr.len += hlen;
    m = m0;

    /* Statistics */
    etheripstat.etherip_opackets++;
    etheripstat.etherip_obytes += m->m_pkthdr.len - hlen;

    switch (tdb->tdb_dst.sa.sa_family)
    {
#ifdef INET
	case AF_INET:
	    ipo = mtod(m, struct ip *);

	    ipo->ip_v = IPVERSION;
	    ipo->ip_hl = 5;
	    ipo->ip_len = htons(m->m_pkthdr.len);
	    ipo->ip_ttl = ip_defttl;
	    ipo->ip_p = IPPROTO_ETHERIP;
	    ipo->ip_tos = 0;
	    ipo->ip_off = 0;
	    ipo->ip_sum = 0;
	    ipo->ip_id = ip_randomid();
	    HTONS(ipo->ip_id);

	    /* 
	     * We should be keeping tunnel soft-state and send back
	     * ICMPs as needed.
	     */

	    ipo->ip_src = tdb->tdb_src.sin.sin_addr;
	    ipo->ip_dst = tdb->tdb_dst.sin.sin_addr;
	    break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
	    ip6 = mtod(m, struct ip6_hdr *);

	    ip6->ip6_flow = 0;
            ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
            ip6->ip6_vfc |= IPV6_VERSION;
            ip6->ip6_plen = htons(m->m_pkthdr.len);
            ip6->ip6_hlim = ip_defttl;
            ip6->ip6_dst = tdb->tdb_dst.sin6.sin6_addr;
            ip6->ip6_src = tdb->tdb_src.sin6.sin6_addr;
	    break;
#endif /* INET6 */
    }

    /* Set the version number */
    v = (ETHERIP_VERSION << 4) & 0xf0;
    m_copyback(m, hlen - sizeof(u_int8_t), sizeof(u_int8_t), &v);

    *mp = m;

    return 0;
}
#endif /* IPSEC */

int
etherip_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
	       void *newp, size_t newlen)
{
    /* All sysctl names at this level are terminal. */
    if (namelen != 1)
      return (ENOTDIR);

    switch (name[0]) 
    {
	case ETHERIPCTL_ALLOW:
	    return (sysctl_int(oldp, oldlenp, newp, newlen, &etherip_allow));
	default:
	    return (ENOPROTOOPT);
    }
    /* NOTREACHED */
}
