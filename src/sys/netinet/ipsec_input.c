/*	$OpenBSD: ipsec_input.c,v 1.5 2000/01/02 11:12:03 angelos Exp $	*/

/*
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr) and 
 * Niels Provos (provos@physnet.uni-hamburg.de).
 *
 * This code was written by John Ioannidis for BSD/OS in Athens, Greece, 
 * in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D. Keromytis
 * and Niels Provos.
 *
 * Additional features in 1999 by Angelos D. Keromytis.
 *
 * Copyright (C) 1995, 1996, 1997, 1998, 1999 by John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
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
 * Authentication Header Processing
 * Per RFC1826 (Atkinson, 1995)
 *
 * Encapsulation Security Payload Processing
 * Per RFC1827 (Atkinson, 1995)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/kernel.h>

#include <net/if.h>
#include <net/route.h>
#include <net/netisr.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_var.h>

#ifdef INET6
#include <netinet6/in6.h>
#include <netinet6/ip6.h>
#endif/ * INET6 */

#include <netinet/ip_ipsp.h>
#include <netinet/ip_esp.h>
#include <netinet/ip_ah.h>

#include <net/if_enc.h>

#include "bpfilter.h"

extern struct enc_softc encif[];

#ifdef ENCDEBUG
#define DPRINTF(x)	if (encdebug) printf x
#else
#define DPRINTF(x)
#endif

#ifndef offsetof
#define offsetof(s, e) ((int)&((s *)0)->e)
#endif

int esp_enable = 0;
int ah_enable = 0;

/*
 * ipsec_common_input() gets called when we receive an IPsec-protected packet
 * in IPv4 or IPv6.
 */

static int
ipsec_common_input(struct mbuf *m, int skip, int protoff, int af, int sproto)
{
#define IPSEC_ISTAT(y,z) (sproto == IPPROTO_ESP ? (y)++ : (z)++)
#define IPSEC_NAME (sproto == IPPROTO_ESP ? "esp_input()" : "ah_input()")

    union sockaddr_union sunion;
    struct tdb *tdbp;
    u_int32_t spi;
    u_int8_t prot;
    int s;

#ifdef INET
    struct ip *ip, ipn;
#endif /* INET */

#ifdef INET6
    struct ip6_hdr *ip6, ip6n;
#endif /* INET6 */

    IPSEC_ISTAT(espstat.esps_input, ahstat.ahs_input);

    if ((sproto == IPPROTO_ESP && !esp_enable) ||
	(sproto == IPPROTO_AH && !ah_enable))
    {
        m_freem(m);
        IPSEC_ISTAT(espstat.esps_pdrops, ahstat.ahs_pdrops);
        return EOPNOTSUPP;
    }
    
    /* Retrieve the SPI from the relevant IPsec header */
    if (sproto == IPPROTO_ESP)
      m_copydata(m, skip, sizeof(u_int32_t), (caddr_t) &spi);
    else
      m_copydata(m, skip + sizeof(u_int32_t), sizeof(u_int32_t),
		 (caddr_t) &spi);

    /*
     * Find tunnel control block and (indirectly) call the appropriate
     * kernel crypto routine. The resulting mbuf chain is a valid
     * IP packet ready to go through input processing.
     */

    bzero(&sunion, sizeof(sunion));
    sunion.sin.sin_family = af;

#ifdef INET
    if (af == AF_INET)
    {
	sunion.sin.sin_len = sizeof(struct sockaddr_in);
	m_copydata(m, offsetof(struct ip, ip_dst), sizeof(struct in_addr),
		   (unsigned char *) &(sunion.sin.sin_addr));
    }
#endif /* INET */

#ifdef INET6
    if (af == AF_INET6)
    {
	sunion.sin6.sin6_len = sizeof(struct sockaddr_in6);
	m_copydata(m, offsetof(struct ip6_hdr, ip6_dst),
		   sizeof(struct in6_addr),
		   (unsigned char *) &(sunion.sin6.sin6_addr));
    }
#endif /* INET6 */

    s = spltdb();
    tdbp = gettdb(spi, &sunion, sproto);
    if (tdbp == NULL)
    {
	DPRINTF(("%s: could not find SA for packet to %s, spi %08x\n",
		 IPSEC_NAME, ipsp_address(sunion), ntohl(spi)));
	m_freem(m);
	IPSEC_ISTAT(espstat.esps_notdb, ahstat.ahs_notdb);
	return ENOENT;
    }
	
    if (tdbp->tdb_flags & TDBF_INVALID)
    {
	DPRINTF(("%s: attempted to use invalid SA %s/%08x\n",
		 IPSEC_NAME, ipsp_address(sunion), ntohl(spi)));
	m_freem(m);
	IPSEC_ISTAT(espstat.esps_invalid, ahstat.ahs_invalid);
	return EINVAL;
    }

    if (tdbp->tdb_xform == NULL)
    {
	DPRINTF(("%s: attempted to use uninitialized SA %s/%08x\n",
		 IPSEC_NAME, ipsp_address(sunion), ntohl(spi)));
	m_freem(m);
	IPSEC_ISTAT(espstat.esps_noxform, ahstat.ahs_noxform);
	return ENXIO;
    }

    if (tdbp->tdb_interface)
      m->m_pkthdr.rcvif = (struct ifnet *) tdbp->tdb_interface;
    else
      m->m_pkthdr.rcvif = &encif[0].sc_if;

    /* Register first use, setup expiration timer */
    if (tdbp->tdb_first_use == 0)
    {
	tdbp->tdb_first_use = time.tv_sec;
	tdb_expiration(tdbp, TDBEXP_TIMEOUT);
    }

    m = (*(tdbp->tdb_xform->xf_input))(m, tdbp, skip, protoff);
    if (m == NULL)
    {
	/* The called routine will print a message if necessary */
	IPSEC_ISTAT(espstat.esps_badkcr, ahstat.ahs_badkcr);
	return EINVAL;
    }

#ifdef INET
    /* Fix IPv4 header */
    if (af == AF_INET)
    {
        if ((m = m_pullup(m, skip)) == 0)
        {
	    DPRINTF(("%s: processing failed for SA %s/%08x\n",
		     IPSEC_NAME, ipsp_address(tdbp->tdb_dst), ntohl(spi)));
            IPSEC_ISTAT(espstat.esps_hdrops, ahstat.ahs_hdrops);
            return ENOMEM;
        }

	ip = mtod(m, struct ip *);
	ip->ip_len = htons(m->m_pkthdr.len);
	ip->ip_sum = 0;
	ip->ip_sum = in_cksum(m, ip->ip_hl << 2);
	prot = ip->ip_p;

	/* IP-in-IP encapsulation */
	if (prot == IPPROTO_IPIP)
	{
	    /* ipn will now contain the inner IPv4 header */
	    m_copydata(m, ip->ip_hl << 2, sizeof(struct ip), (caddr_t) &ipn);

	    /*
	     * Check that the inner source address is the same as
	     * the proxy address, if available.
	     */
	    if (((tdbp->tdb_proxy.sa.sa_family == AF_INET) &&
		 (tdbp->tdb_proxy.sin.sin_addr.s_addr != INADDR_ANY) &&
		 (ipn.ip_src.s_addr != tdbp->tdb_proxy.sin.sin_addr.s_addr)) ||
		((tdbp->tdb_proxy.sa.sa_family != AF_INET6) &&
		 (tdbp->tdb_proxy.sa.sa_family != 0)))
	    {
		DPRINTF(("%s: inner source address %s doesn't correspond to expected proxy source %s, SA %s/%08x\n", IPSEC_NAME, inet_ntoa4(ipn.ip_src), ipsp_address(tdbp->tdb_proxy), ipsp_address(tdbp->tdb_dst), ntohl(spi)));
		m_free(m);
                IPSEC_ISTAT(espstat.esps_hdrops, ahstat.ahs_hdrops);
		return EACCES;
	    }
	}

#if INET6
	/* IPv6-in-IP encapsulation */
	if (prot == IPPROTO_IPV6)
	{
	    /* ip6n will now contain the inner IPv6 header */
	    m_copydata(m, ip->ip_hl << 2, sizeof(struct ip6_hdr),
		       (caddr_t) &ip6n);

	    /*
	     * Check that the inner source address is the same as
	     * the proxy address, if available.
	     */
	    if (((tdbp->tdb_proxy.sa.sa_family == AF_INET6) &&
		 !IN6_IS_ADDR_UNSPECIFIED(&tdbp->tdb_proxy.sin6.sin6_addr) &&
		 !IN6_ARE_ADDR_EQUAL(&ip6n.ip6_src,
				     &tdbp->tdb_proxy.sin6.sin6_addr)) ||
		((tdbp->tdb_proxy.sa.sa_family != AF_INET6) &&
		 (tdbp->tdb_proxy.sa.sa_family != 0)))
	    {
		DPRINTF(("%s: inner source address %s doesn't correspond to expected proxy source %s, SA %s/%08x\n", IPSEC_NAME, inet6_ntoa4(ip6n.ip6_src), ipsp_address(tdbp->tdb_proxy), ipsp_address(tdbp->tdb_dst), ntohl(spi)));
		m_free(m);
		IPSEC_ISTAT(espstat.esps_hdrops, ahstat.ahs_hdrops);
		return EACCES;
	    }
	}
#endif /* INET6 */

	/* 
	 * Check that the source address is an expected one, if we know what
	 * it's supposed to be. This avoids source address spoofing.
	 */
	if (((tdbp->tdb_src.sa.sa_family == AF_INET) &&
	     (tdbp->tdb_src.sin.sin_addr.s_addr != INADDR_ANY) &&
	     (ip->ip_src.s_addr != tdbp->tdb_src.sin.sin_addr.s_addr)) ||
	    ((tdbp->tdb_src.sa.sa_family != AF_INET) &&
	     (tdbp->tdb_src.sa.sa_family != 0)))
	{
	    DPRINTF(("%s: source address %s doesn't correspond to expected source %s, SA %s/%08x\n", IPSEC_NAME, inet_ntoa4(ip->ip_src), ipsp_address(tdbp->tdb_src), ipsp_address(tdbp->tdb_dst), ntohl(spi)));
	    m_free(m);
	    IPSEC_ISTAT(espstat.esps_hdrops, ahstat.ahs_hdrops);
	    return EACCES;
	}
    }
#endif /* INET */

#ifdef INET6
    /* Fix IPv6 header */
    if (af == INET6)
    {
        if ((m = m_pullup(m, sizeof(struct ip6_hdr))) == 0)
        {
	    DPRINTF(("%s: processing failed for SA %s/%08x\n",
		     IPSEC_NAME, ipsp_address(tdbp->tdb_dst), ntohl(spi)));
            IPSEC_ISTAT(espstat.esps_hdrops, ahstat.ahs_hdrops);
            return ENOMEM;
        }

	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_plen = htons(m->m_pkthdr.len);

	/* Save protocol */
	m_copydata(m, protoff, 1, (unsigned char *) &prot);

#ifdef INET
	/* IP-in-IP encapsulation */
	if (prot == IPPROTO_IPIP)
	{
	    /* ipn will now contain the inner IPv4 header */
	    m_copydata(m, skip, sizeof(struct ip), (caddr_t) &ipn);

	    /*
	     * Check that the inner source address is the same as
	     * the proxy address, if available.
	     */
	    if (((tdbp->tdb_proxy.sa.sa_family == AF_INET) &&
		 (tdbp->tdb_proxy.sin.sin_addr.s_addr != INADDR_ANY) &&
		 (ipn.ip_src.s_addr != tdbp->tdb_proxy.sin.sin_addr.s_addr)) ||
		((tdbp->tdb_proxy.sa.sa_family != AF_INET) &&
		 (tdbp->tdb_proxy.sa.sa_family != 0)))
	    {
		DPRINTF(("%s: inner source address %s doesn't correspond to expected proxy source %s, SA %s/%08x\n", IPSEC_NAME, inet_ntoa4(ipn.ip_src), ipsp_address(tdbp->tdb_proxy), ipsp_address(tdbp->tdb_dst), ntohl(spi)));
		m_free(m);
		IPSEC_ISTAT(espstat.esps_hdrops, ahstat.ahs_hdrops);
		return EACCES;
	    }
	}
#endif /* INET */

	/* IPv6-in-IP encapsulation */
	if (prot == IPPROTO_IPV6)
	{
	    /* ip6n will now contain the inner IPv6 header */
	    m_copydata(m, skip, sizeof(struct ip6_hdr), (caddr_t) &ip6n);

	    /*
	     * Check that the inner source address is the same as
	     * the proxy address, if available.
	     */
	    if (((tdbp->tdb_proxy.sa.sa_family == AF_INET6) &&
		 !IN6_IS_ADDR_UNSPECIFIED(&tdbp->tdb_proxy.sin6.sin6_addr) &&
		 !IN6_ARE_ADDR_EQUAL(&ip6n.ip6_src,
				     &tdbp->tdb_proxy.sin6.sin6_addr)) ||
		((tdbp->tdb_proxy.sa.sa_family != AF_INET6) &&
		 (tdbp->tdb_proxy.sa.sa_family != 0)))
	    {
		DPRINTF(("%s: inner source address %s doesn't correspond to expected proxy source %s, SA %s/%08x\n", IPSEC_NAME, inet6_ntoa4(ip6n.ip6_src), ipsp_address(tdbp->tdb_proxy), ipsp_address(tdbp->tdb_dst), ntohl(spi)));
		m_free(m);
		IPSEC_ISTAT(espstat.esps_hdrops, ahstat.ahs_hdrops);
		return EACCES;
	    }
	}

	/* 
	 * Check that the source address is an expected one, if we know what
	 * it's supposed to be. This avoids source address spoofing.
	 */
	if (((tdbp->tdb_src.sa.sa_family == AF_INET6) &&
	     !IN6_IS_ADDR_UNSPECIFIED(&tdbp->tdb_src.sin6.sin6_addr) &&
	     !IN6_ARE_ADDR_EQUAL(&ip6->ip6_src,
				 &tdbp->tdb_src.sin6.sin6_addr)) ||
	    ((tdbp->tdb_src.sa.sa_family != AF_INET6) &&
	     (tdbp->tdb_src.sa.sa_family != 0)))
	{
	    DPRINTF(("%s: source address %s doesn't correspond to expected source %s, SA %s/%08x\n", IPSEC_NAME, inet6_ntoa4(ip6->ip6_src), ipsp_address(tdbp->tdb_src), ipsp_address(tdbp->tdb_dst), ntohl(spi)));
	    m_free(m);
	    IPSEC_ISTAT(espstat.esps_hdrops, ahstat.ahs_hdrops);
	    return EACCES;
	}
    }
#endif /* INET6 */

    if (prot == IPPROTO_TCP || prot == IPPROTO_UDP)
    {
	struct tdb_ident *tdbi = NULL;

	if (tdbp->tdb_bind_out)
	{
	    tdbi = m->m_pkthdr.tdbi;
	    if (!(m->m_flags & M_PKTHDR))
	      DPRINTF(("%s: mbuf is not a packet header!\n", IPSEC_NAME));

	    MALLOC(tdbi, struct tdb_ident *, sizeof(struct tdb_ident),
	           M_TEMP, M_NOWAIT);

	    if (tdbi == NULL)
	      m->m_pkthdr.tdbi = NULL;
	    else
	    {
		tdbi->spi = tdbp->tdb_bind_out->tdb_spi;
		tdbi->dst = tdbp->tdb_bind_out->tdb_dst;
		tdbi->proto = tdbp->tdb_bind_out->tdb_sproto;
	    }
	}
    }
    else
      m->m_pkthdr.tdbi = NULL;

    if (sproto == IPPROTO_ESP)
    {
	/* Packet is confidental */
	m->m_flags |= M_CONF;

	/* Check if we had authenticated ESP */
	if (tdbp->tdb_authalgxform)
	  m->m_flags |= M_AUTH;
    }
    else
      m->m_flags |= M_AUTH;

#if NBPFILTER > 0
    if (m->m_pkthdr.rcvif->if_bpf) 
    {
        /*
         * We need to prepend the address family as
         * a four byte field.  Cons up a dummy header
         * to pacify bpf.  This is safe because bpf
         * will only read from the mbuf (i.e., it won't
         * try to free it or keep a pointer a to it).
         */
        struct mbuf m0;
        struct enchdr hdr;

	hdr.af = af;
	hdr.spi = tdbp->tdb_spi;
	hdr.flags = m->m_flags & (M_AUTH|M_CONF);

        m0.m_next = m;
        m0.m_len = ENC_HDRLEN;
        m0.m_data = (char *) &hdr;
        
        bpf_mtap(m->m_pkthdr.rcvif->if_bpf, &m0);
    }
#endif
    splx(s);

    return 0;
#undef IPSEC_NAME
#undef IPSEC_ISTAT
}

int
esp_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlen, void *newp,
	   size_t newlen)
{
    /* All sysctl names at this level are terminal. */
    if (namelen != 1)
      return ENOTDIR;

    switch (name[0])
    {
	case ESPCTL_ENABLE:
	    return sysctl_int(oldp, oldlen, newp, newlen, &esp_enable);
	default:
	    return ENOPROTOOPT;
    }
    /* NOTREACHED */
}

int
ah_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlen, void *newp,
	  size_t newlen)
{
    /* All sysctl names at this level are terminal. */
    if (namelen != 1)
      return ENOTDIR;

    switch (name[0])
    {
	case AHCTL_ENABLE:
	    return sysctl_int(oldp, oldlen, newp, newlen, &ah_enable);
	default:
	    return ENOPROTOOPT;
    }
    /* NOTREACHED */
}

#ifdef INET
/* IPv4 AH wrapper */
void
ah_input(struct mbuf *m, ...)
{
    struct ifqueue *ifq = &ipintrq;
    int skip, s;

    va_list ap;
    va_start(ap, m);
    skip = va_arg(ap, int);
    va_end(ap);

    if (ipsec_common_input(m, skip, offsetof(struct ip, ip_p), AF_INET,
			   IPPROTO_AH) != 0)
      return;

    /*
     * Interface pointer is already in first mbuf; chop off the 
     * `outer' header and reschedule.
     */

    s = splimp();			/* isn't it already? */
    if (IF_QFULL(ifq))
    {
	IF_DROP(ifq);
	if (m->m_pkthdr.tdbi)
	  free(m->m_pkthdr.tdbi, M_TEMP);
	m_freem(m);
	ahstat.ahs_qfull++;
	splx(s);
	DPRINTF(("ah_input(): dropped packet because of full IP queue\n"));
	return;
    }

    IF_ENQUEUE(ifq, m);
    schednetisr(NETISR_IP);
    splx(s);
}

/* IPv4 ESP wrapper */
void
esp_input(struct mbuf *m, ...)
{
    struct ifqueue *ifq = &ipintrq;
    int skip, s;

    va_list ap;
    va_start(ap, m);
    skip = va_arg(ap, int);
    va_end(ap);

    if (ipsec_common_input(m, skip, offsetof(struct ip, ip_p), AF_INET,
			   IPPROTO_ESP) != 0)
      return;

    /*
     * Interface pointer is already in first mbuf; chop off the 
     * `outer' header and reschedule.
     */

    s = splimp();			/* isn't it already? */
    if (IF_QFULL(ifq))
    {
	IF_DROP(ifq);
	if (m->m_pkthdr.tdbi)
	  free(m->m_pkthdr.tdbi, M_TEMP);
	m_freem(m);
	espstat.esps_qfull++;
	splx(s);
	DPRINTF(("esp_input(): dropped packet because of full IP queue\n"));
	return;
    }

    IF_ENQUEUE(ifq, m);
    schednetisr(NETISR_IP);
    splx(s);
}
#endif /* INET */

#ifdef INET6
/* IPv6 AH wrapper */
int
ah6_input(struct mbuf **mp, int *offp, int proto)
{
    struct mbuf *m = *mp;
    u_int8_t nxt;
    int protoff;

    /*
     * XXX assuming that it is first hdr, i.e.
     * offp == sizeof(struct ip6_hdr)
     */
    if (*offp != sizeof(struct ip6_hdr))
    {
	m_freem(m);
	return IPPROTO_DONE;	/* not quite */
    }

    protoff = offsetof(struct ip6_hdr, ip6_nxt);
    if (ipsec_common_input(m, *offp, protoff, AF_INET6, proto) != 0)
      return IPPROTO_DONE;

    /* Retrieve new protocol */
    m_copydata(m, protoff, sizeof(u_int8_t), (caddr_t) &nxt);
    return nxt;
}

/* IPv6 ESP wrapper */
int
esp6_input(struct mbuf **mp, int *offp, int proto)
{
    struct mbuf *m = *mp;
    u_int8_t nxt;
    int protoff;

    /*
     * XXX assuming that it is first hdr, i.e.
     * offp == sizeof(struct ip6_hdr)
     */
    if (*offp != sizeof(struct ip6_hdr))
    {
	m_freem(m);
	return IPPROTO_DONE;	/* not quite */
    }

    protoff = offsetof(struct ip6_hdr, ip6_nxt);
    if (ipsec_common_input(m, *offp, protoff, AF_INET6, proto) != 0)
      return IPPROTO_DONE;

    /* Retrieve new protocol */
    m_copydata(m, protoff, sizeof(u_int8_t), (caddr_t) &nxt);
    return nxt;
}
#endif /* INET6 */
