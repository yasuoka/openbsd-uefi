/*	$OpenBSD: ip_ip4.c,v 1.11 1997/07/11 23:37:58 provos Exp $	*/

/*
 * The author of this code is John Ioannidis, ji@tla.org,
 * 	(except when noted otherwise).
 *
 * This code was written for BSD/OS in Athens, Greece, in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis, kermit@forthnet.gr.
 *
 * Copyright (C) 1995, 1996, 1997 by John Ioannidis and Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NEITHER AUTHOR MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

/*
 * IP-inside-IP processing
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <machine/cpu.h>

#include <net/if.h>
#include <net/route.h>
#include <net/netisr.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>

#include <sys/socketvar.h>
#include <net/raw_cb.h>
#include <net/encap.h>

#include <netinet/ip_ipsp.h>
#include <netinet/ip_ip4.h>
#include <dev/rndvar.h>
#include <sys/syslog.h>

void	ip4_input __P((struct mbuf *, int));

/*
 * ip4_input gets called when we receive an encapsulated packet,
 * either because we got it at a real interface, or because AH or ESP
 * were being used in tunnel mode (in which case the rcvif element will 
 * contain the address of the encapX interface associated with the tunnel.
 */

void
ip4_input(register struct mbuf *m, int iphlen)
{
    struct ip *ipo, *ipi;
    struct ifqueue *ifq = NULL;
    int s;

    ip4stat.ip4s_ipackets++;

    /*
     * Strip IP options, if any.
     */
    if (iphlen > sizeof(struct ip))
    {
#ifdef ENCDEBUG
	if (encdebug)
	  printf("ip4_input(): stripping options\n");
#endif /* ENCDEBUG */

	ip_stripoptions(m, (struct mbuf *) 0);
	iphlen = sizeof(struct ip);
    }
	
    /*
     * Make sure next IP header is in the first mbuf.
     *
     * Careful here! we are receiving the packet from ipintr;
     * this means that the ip_len field has been adjusted to
     * not count the ip header, and is also in host order.
     */

    ipo = mtod(m, struct ip *);

    if (m->m_len < iphlen + sizeof(struct ip))
    {
	if ((m = m_pullup(m, iphlen + sizeof(struct ip))) == 0)
	{
#ifdef ENCDEBUG
	    if (encdebug)
	      printf("ip4_input(): m_pullup() failed\n");
#endif /* ENCDEBUG */

	    ip4stat.ip4s_hdrops++;
	    return;
	}

	ipo = mtod(m, struct ip *);
    }

    ipi = (struct ip *) ((caddr_t) ipo + iphlen);
	
    /*
     * RFC 1853 specifies that the inner TTL should not be touched on
     * decapsulation. There's no reason this comment should be here, but
     * this is as good as any a position.
     */

    if (ipi->ip_v != IPVERSION)
    {
	log(LOG_WARNING,
	    "ip4_input(): wrong version %d on IP packet from %x to %x (%x->%x)",
	    ipi->ip_v, ipo->ip_src, ipo->ip_dst, ipi->ip_src, ipi->ip_dst);
	ip4stat.ip4s_notip4++;
	return;
    }
	
    /*
     * Interface pointer is already in first mbuf; chop off the 
     * `outer' header and reschedule.
     */

    m->m_len -= iphlen;
    m->m_pkthdr.len -= iphlen;
    m->m_data += iphlen;
	
    /*
     * Interface pointer stays the same; if no IPsec processing has
     * been done (or will be done), this will point to a normal 
     * interface. Otherwise, it'll point to an encap interface, which
     * will allow a packet filter to distinguish between secure and
     * untrusted packets.
     */

    ifq = &ipintrq;

    s = splimp();			/* isn't it already? */
    if (IF_QFULL(ifq))
    {
	IF_DROP(ifq);
	m_freem(m);
	ip4stat.ip4s_qfull++;
	splx(s);
#ifdef ENCDEBUG
	if (encdebug)
	  printf("ip4_input(): packet dropped because of full queue\n");
#endif /* ENCDEBUG */
	return;
    }

    IF_ENQUEUE(ifq, m);
    schednetisr(NETISR_IP);
    splx(s);
	
    return;
}

int
ipe4_output(struct mbuf *m, struct sockaddr_encap *gw, struct tdb *tdb, 
	    struct mbuf **mp)
{
    struct ip *ipo, *ipi;
    ushort ilen;

    ip4stat.ip4s_opackets++;
    ipi = mtod(m, struct ip *);
    ilen = ntohs(ipi->ip_len);

    M_PREPEND(m, sizeof(struct ip), M_DONTWAIT);
    if (m == 0)
    {
#ifdef ENCDEBUG
	if (encdebug)
	  printf("ipe4_output(): M_PREPEND failed\n");
#endif /* ENCDEBUG */
      	return ENOBUFS;
    }

    ipo = mtod(m, struct ip *);
	
    ipo->ip_v = IPVERSION;
    ipo->ip_hl = 5;
    ipo->ip_tos = ipi->ip_tos;
    ipo->ip_len = htons(ilen + sizeof(struct ip));
/*  ipo->ip_id = htons(ip_id++); */
    get_random_bytes((void *) &(ipo->ip_id), sizeof(ipo->ip_id));
    ipo->ip_off = ipi->ip_off & ~(IP_MF | IP_OFFMASK); /* keep C and DF */

    if (tdb->tdb_flags & TDBF_SAME_TTL)
      ipo->ip_ttl = ipi->ip_ttl;
    else
      if (tdb->tdb_ttl == 0)
        ipo->ip_ttl = ip_defttl;
      else
        ipi->ip_ttl = tdb->tdb_ttl;
	
    ipo->ip_p = IPPROTO_IPIP;
    ipo->ip_sum = 0;
    ipo->ip_src = tdb->tdb_osrc;
    ipo->ip_dst = tdb->tdb_odst;
	
/* 
 *  printf("ip4_output: [%x->%x](l=%d, p=%d)", 
 *  	   ntohl(ipi->ip_src.s_addr), ntohl(ipi->ip_dst.s_addr),
 *	   ilen, ipi->ip_p);
 *  printf(" through [%x->%x](l=%d, p=%d)\n", 
 *	   ntohl(ipo->ip_src.s_addr), ntohl(ipo->ip_dst.s_addr),
 *	   ipo->ip_len, ipo->ip_p);
 */

    *mp = m;

    /* Update the counters */
    if (tdb->tdb_xform->xf_type == XF_IP4)
    {
	tdb->tdb_cur_packets++;
	tdb->tdb_cur_bytes += ntohs(ipo->ip_len) - (ipo->ip_hl << 2);
    }

    return 0;

/*  return ip_output(m, NULL, NULL, IP_ENCAPSULATED, NULL); */
}

int
ipe4_attach()
{
#ifdef ENCDEBUG
    if (encdebug)
      printf("ipe4_attach(): setting up\n");
#endif /* ENCDEBUG */
    return 0;
}

int
ipe4_init(struct tdb *tdbp, struct xformsw *xsp, struct mbuf *m)
{
#ifdef ENCDEBUG
    if (encdebug)
      printf("ipe4_init(): setting up\n");
#endif /* ENCDEBUG */
    tdbp->tdb_xform = xsp;
    return 0;
}

int
ipe4_zeroize(struct tdb *tdbp)
{
#ifdef ENCDEBUG
    if (encdebug)
      printf("ipe4_zeroize(): nothing to do really...\n");
#endif /* ENCDEBUG */
    return 0;
}

void
ipe4_input(struct mbuf *m, ...)
{
    log(LOG_ALERT, "ipe4_input(): should never be called");
    if (m)
      m_freem(m);
}
