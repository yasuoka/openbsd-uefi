/*	$OpenBSD: ip_ah.c,v 1.13 1997/11/04 09:10:58 provos Exp $	*/

/*
 * The author of this code is John Ioannidis, ji@tla.org,
 * 	(except when noted otherwise).
 *
 * This code was written for BSD/OS in Athens, Greece, in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis, kermit@forthnet.gr.
 *
 * Additional transforms and features in 1997 by Angelos D. Keromytis and
 * Niels Provos.
 *
 * Copyright (C) 1995, 1996, 1997 by John Ioannidis, Angelos D. Keromytis
 * and Niels Provos.
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
 * Authentication Header Processing
 * Per RFC1826 (Atkinson, 1995)
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
#include <netinet/ip_ah.h>

#include <sys/syslog.h>

void	ah_input __P((struct mbuf *, int));

/*
 * ah_input gets called when we receive an packet with an AH.
 */

void
ah_input(register struct mbuf *m, int iphlen)
{
    struct ifqueue *ifq = NULL;
    struct ah_old *ahp, ahn;
    struct expiration *exp;
    struct ip *ipo, ipn;
    struct tdb *tdbp;
    int s;
	
    ahstat.ahs_input++;

    /*
     * Make sure that at least the fixed part of the AH header is
     * in the first mbuf.
     */

    ipo = mtod(m, struct ip *);
    if (m->m_len < iphlen + AH_OLD_FLENGTH)
    {
	if ((m = m_pullup(m, iphlen + AH_OLD_FLENGTH)) == 0)
	{
#ifdef ENCDEBUG
	    if (encdebug)
	      printf("ah_input(): (possibly too short) packet from %x to %x dropped\n", ipo->ip_src, ipo->ip_dst);
#endif /* ENCDEBUG */
	    ahstat.ahs_hdrops++;
	    return;
	}
	ipo = mtod(m, struct ip *);
    }

    ahp = (struct ah_old *) ((caddr_t) ipo + iphlen);

    /*
     * Find tunnel control block and (indirectly) call the appropriate
     * tranform routine. The resulting mbuf chain is a valid
     * IP packet ready to go through input processing.
     */

    tdbp = gettdb(ahp->ah_spi, ipo->ip_dst, IPPROTO_AH);
    if (tdbp == NULL)
    {
	if (encdebug)
	  log(LOG_ERR, "ah_input(): could not find SA for AH packet from %x to %x, spi %08x\n", ipo->ip_src, ipo->ip_dst, ntohl(ahp->ah_spi));
	m_freem(m);
	ahstat.ahs_notdb++;
	return;
    }

    if (tdbp->tdb_flags & TDBF_INVALID)
    {
	if (encdebug)
	  log(LOG_ALERT, "ah_input(): attempted to use invalid AH SA %08x, packet %x->%x\n", ntohl(ahp->ah_spi), ipo->ip_src, ipo->ip_dst);
	m_freem(m);
	ahstat.ahs_invalid++;
	return;
    }

    if (tdbp->tdb_xform == NULL)
    {
	if (encdebug)
	  log(LOG_ALERT, "ah_input(): attempted to use uninitialized AH SA %08x, packet from %x to %x\n", ntohl(ahp->ah_spi), ipo->ip_src, ipo->ip_dst);
	m_freem(m);
	ahstat.ahs_noxform++;
	return;
    }

    m->m_pkthdr.rcvif = &enc_softc;

    /* Register first use, setup expiration timer */
    if (tdbp->tdb_first_use == 0)
    {
	tdbp->tdb_first_use = time.tv_sec;

	if (tdbp->tdb_flags & TDBF_FIRSTUSE)
	{
	    exp = get_expiration();
	    if (exp == (struct expiration *) NULL)
	    {
		if (encdebug)
		  log(LOG_WARNING,
		      "ah_input(): out of memory for expiration timer\n");
		ahstat.ahs_hdrops++;
		m_freem(m);
		return;
	    }

	    exp->exp_dst.s_addr = tdbp->tdb_dst.s_addr;
	    exp->exp_spi = tdbp->tdb_spi;
	    exp->exp_sproto = tdbp->tdb_sproto;
	    exp->exp_timeout = tdbp->tdb_first_use + tdbp->tdb_exp_first_use;

	    put_expiration(exp);
	}

	if ((tdbp->tdb_flags & TDBF_SOFT_FIRSTUSE) &&
	    (tdbp->tdb_soft_first_use <= tdbp->tdb_exp_first_use))
	{
	    exp = get_expiration();
	    if (exp == (struct expiration *) NULL)
	    {
		if (encdebug)
		  log(LOG_WARNING,
		      "ah_input(): out of memory for expiration timer\n");
		ahstat.ahs_hdrops++;
		m_freem(m);
		return;
	    }

	    exp->exp_dst.s_addr = tdbp->tdb_dst.s_addr;
	    exp->exp_spi = tdbp->tdb_spi;
	    exp->exp_sproto = tdbp->tdb_sproto;
	    exp->exp_timeout = tdbp->tdb_first_use + tdbp->tdb_soft_first_use;

	    put_expiration(exp);
	}
    }
    
    ipn = *ipo;
    ahn = *ahp;

    m = (*(tdbp->tdb_xform->xf_input))(m, tdbp);
    if (m == NULL)
    {
	if (encdebug)
	  log(LOG_ALERT, "ah_input(): authentication failed for AH packet from %x to %x, spi %08x\n", ipn.ip_src, ipn.ip_dst, ntohl(ahn.ah_spi));
	ahstat.ahs_badkcr++;
	return;
    }

    ipo = mtod(m, struct ip *);
    if (ipo->ip_p == IPPROTO_IPIP)	/* IP-in-IP encapsulation */
    {
	/* Encapsulating SPI */
	if (tdbp->tdb_osrc.s_addr && tdbp->tdb_odst.s_addr)
	{
	    if (tdbp->tdb_flags & TDBF_UNIQUE)
	      if ((ipn.ip_src.s_addr != ipo->ip_src.s_addr) ||
		  (ipn.ip_dst.s_addr != ipo->ip_dst.s_addr))
	      {
		  if (encdebug)
		    log(LOG_ALERT, "ah_input(): AH-tunnel with different internal addresses %x/%x, SA %08x/%x\n", ipo->ip_src, ipo->ip_dst, tdbp->tdb_spi, tdbp->tdb_dst);
		  m_freem(m);
		  ahstat.ahs_hdrops++;
		  return;
	      }
	}
	else				/* So we're paranoid */
	{
	    if (encdebug)
	      log(LOG_ALERT, "ah_input(): AH-tunnel used when expecting AH-transport, SA %08x/%x\n", tdbp->tdb_spi, tdbp->tdb_dst);
	    m_freem(m);
	    ahstat.ahs_hdrops++;
	    return;
	}
    }

    /*
     * Interface pointer is already in first mbuf; chop off the 
     * `outer' header and reschedule.
     */

    ifq = &ipintrq;

    s = splimp();			/* isn't it already? */
    if (IF_QFULL(ifq))
    {
	IF_DROP(ifq);
	m_freem(m);
	ahstat.ahs_qfull++;
	splx(s);
#ifdef ENCDEBUG
	if (encdebug)
	  printf("ah_input(): dropped packet because of full IP queue\n");
#endif /* ENCDEBUG */
	return;
    }

    IF_ENQUEUE(ifq, m);
    schednetisr(NETISR_IP);
    splx(s);
    return;
}
