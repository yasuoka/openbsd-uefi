/* $OpenBSD: ip_spd.c,v 1.2 2000/09/20 19:13:18 angelos Exp $ */

/*
 * The author of this code is Angelos D. Keromytis (angelos@cis.upenn.edu)
 *
 * Copyright (c) 2000 Angelos D. Keromytis.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/route.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#endif /* INET */

#ifdef INET6
#ifndef INET
#include <netinet/in.h>
#endif
#include <netinet6/in6_var.h>
#endif /* INET6 */

#include <net/pfkeyv2.h>

#include <netinet/ip_ipsp.h>

#ifdef ENCDEBUG
#define DPRINTF(x)	if (encdebug) printf x
#else
#define DPRINTF(x)
#endif

#ifndef offsetof
#define offsetof(s, e) ((int)&((s *)0)->e)
#endif

/*
 * Lookup at the SPD based on the headers contained on the mbuf. The second
 * argument indicates what protocol family the header at the beginning of
 * the mbuf is. hlen is the the offset of the transport protocol header
 * in the mbuf.
 *
 * Return combinations (of return value and in *error):
 * - NULL/0 -> no IPsec required on packet
 * - NULL/-EINVAL -> silently drop the packet
 * - NULL/errno -> drop packet and return error
 * or a pointer to a TDB (and 0 in *error).
 *
 * In the case of incoming flows, only the first three combinations are
 * returned.
 */
struct tdb *
ipsp_spd_lookup(struct mbuf *m, int af, int hlen, int *error, int direction,
		struct tdb *tdbp, struct inpcb *inp)
{
    struct route_enc re0, *re = &re0;
    union sockaddr_union sdst, ssrc;
    struct sockaddr_encap *ddst;
    struct ipsec_policy *ipo;
    int ignore = 0;

    /*
     * If there are no flows in place, there's no point
     * continuing with the SPD lookup.
     */
    if (!ipsec_in_use)
    {
	*error = 0;
	return NULL;
    }

    /* If an input packet is destined to a BYPASS socket, just accept it */
    if ((inp != NULL) && (direction == IPSP_DIRECTION_IN) &&
	(inp->inp_seclevel[SL_ESP_TRANS] == IPSEC_LEVEL_BYPASS) &&
	(inp->inp_seclevel[SL_ESP_NETWORK] == IPSEC_LEVEL_BYPASS) &&
	(inp->inp_seclevel[SL_AUTH] == IPSEC_LEVEL_BYPASS))
    {
	*error = 0;
	return NULL;
    }

    bzero((caddr_t) re, sizeof(struct route_enc));
    bzero((caddr_t) &sdst, sizeof(union sockaddr_union));
    bzero((caddr_t) &ssrc, sizeof(union sockaddr_union));
    ddst = (struct sockaddr_encap *) &re->re_dst;
    ddst->sen_family = PF_KEY;
    ddst->sen_len = SENT_LEN;

    switch (af)
    {
#ifdef INET
	case AF_INET:
	    ddst->sen_direction = direction;
	    ddst->sen_type = SENT_IP4;

	    m_copydata(m, offsetof(struct ip, ip_src),
		       sizeof(struct in_addr), (caddr_t) &(ddst->sen_ip_src));
	    m_copydata(m, offsetof(struct ip, ip_dst),
		       sizeof(struct in_addr), (caddr_t) &(ddst->sen_ip_dst));
	    m_copydata(m, offsetof(struct ip, ip_p), sizeof(u_int8_t),
		       (caddr_t) &(ddst->sen_proto));

	    sdst.sin.sin_family = ssrc.sin.sin_family = AF_INET;
	    sdst.sin.sin_len = ssrc.sin.sin_len = sizeof(struct sockaddr_in);
	    ssrc.sin.sin_addr = ddst->sen_ip_src;
	    sdst.sin.sin_addr = ddst->sen_ip_dst;

	    /* If TCP/UDP, extract the port numbers to use in the lookup */
	    switch (ddst->sen_proto)
	    {
		case IPPROTO_UDP:
		case IPPROTO_TCP:
		    /*
		     * Luckily, the offset of the src/dst ports in both the UDP
		     * and TCP headers is the same (first two 16-bit values
		     * in the respective headers), so we can just copy them.
		     */
		    m_copydata(m, hlen, sizeof(u_int16_t),
			       (caddr_t) &(ddst->sen_sport));
		    m_copydata(m, hlen + sizeof(u_int16_t), sizeof(u_int16_t),
			       (caddr_t) &(ddst->sen_dport));
		    break;

		default:
		    ddst->sen_sport = 0;
		    ddst->sen_dport = 0;
	    }

	    break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:
	    ddst->sen_type = SENT_IP6;
	    ddst->sen_ip6_direction = direction;

	    m_copydata(m, offsetof(struct ip6_hdr, ip6_src),
		       sizeof(struct in6_addr),
		       (caddr_t) &(ddst->sen_ip6_src));
	    m_copydata(m, offsetof(struct ip6_hdr, ip6_dst),
		       sizeof(struct in6_addr),
		       (caddr_t) &(ddst->sen_ip6_dst));
	    m_copydata(m, offsetof(struct ip6_hdr, ip6_nxt), sizeof(u_int8_t),
		       (caddr_t) &(ddst->sen_ip6_proto));

	    sdst.sin6.sin6_family = ssrc.sin6.sin6_family = AF_INET6;
	    sdst.sin6.sin6_len = ssrc.sin6.sin6_family =
				sizeof(struct sockaddr_in6);
	    ssrc.sin6.sin6_addr = ddst->sen_ip6_src;
	    sdst.sin6.sin6_addr = ddst->sen_ip6_dst;

	    /* If TCP/UDP, extract the port numbers to use in the lookup */
	    switch (ddst->sen_ip6_proto)
	    {
		case IPPROTO_UDP:
		case IPPROTO_TCP:
		    /*
		     * Luckily, the offset of the src/dst ports in both the UDP
		     * and TCP headers is the same (first two 16-bit values
		     * in the respective headers), so we can just copy them.
		     */
		    m_copydata(m, hlen, sizeof(u_int16_t),
			       (caddr_t) &(ddst->sen_ip6_sport));
		    m_copydata(m, hlen + sizeof(u_int16_t), sizeof(u_int16_t),
			       (caddr_t) &(ddst->sen_ip6_dport));
		    break;

		default:
		    ddst->sen_ip6_sport = 0;
		    ddst->sen_ip6_dport = 0;
	    }

	    break;
#endif /* INET6 */

	default:
	    *error = EAFNOSUPPORT;
	    return NULL;
    }

    /* Actual SPD lookup */
    rtalloc((struct route *) re);
    if (re->re_rt == NULL)
    {
	*error = 0;
	return NULL; /* Nothing found -- means no IPsec needed */
    }

    /* Sanity check */
    if ((re->re_rt->rt_gateway == NULL) ||
	(((struct sockaddr_encap *) re->re_rt->rt_gateway)->sen_type !=
	 SENT_IPSP))
    {
	DPRINTF(("ipsp_spd_lookup(): no gw, or gw data not IPSP\n"));
	RTFREE(re->re_rt);
	*error = EHOSTUNREACH;
	return NULL;
    }

    ipo = ((struct sockaddr_encap *) (re->re_rt->rt_gateway))->sen_ipsp;
    RTFREE(re->re_rt);
    if (ipo == NULL)
    {
	DPRINTF(("ipsp_spd_lookup(): no policy present\n"));
	*error = EHOSTUNREACH;
	return NULL;
    }

    switch (ipo->ipo_type)
    {
	case IPSP_PERMIT:
	    *error = 0;
	    return NULL;

	case IPSP_DENY:
	    *error = EHOSTUNREACH;
	    return NULL;

	case IPSP_IPSEC_USE:
	case IPSP_IPSEC_ACQUIRE:
	case IPSP_IPSEC_REQUIRE:
	case IPSP_IPSEC_DONTACQ:
	    /* Nothing more needed here */
	    break;

	default:
	    *error = EINVAL;
	    return NULL;
    }

    /*
     * Check for non-specific destination in the policy. If a specific
     * destination was specified, use that -- otherwise, use the relevant
     * information from the packet.
     */
    switch (ipo->ipo_dst.sa.sa_family)
    {
#ifdef INET
	case AF_INET:
	    if (ipo->ipo_dst.sin.sin_addr.s_addr != INADDR_ANY)
	    {
		if (direction == IPSP_DIRECTION_OUT)
		  bcopy(&ipo->ipo_dst, &sdst, sizeof(union sockaddr_union));
		else
		  bcopy(&ipo->ipo_dst, &ssrc, sizeof(union sockaddr_union));
	    }
	    break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:
	    if (!IN6_IS_ADDR_UNSPECIFIED(&ipo->ipo_dst.sin6.sin6_addr))
	    {
		if (direction == IPSP_DIRECTION_OUT)
		  bcopy(&ipo->ipo_dst, &sdst, sizeof(union sockaddr_union));
		else
		  bcopy(&ipo->ipo_dst, &ssrc, sizeof(union sockaddr_union));
	    }
	    break;
#endif /* INET6 */
    }

    switch (ipo->ipo_src.sa.sa_family)
    {
#ifdef INET
	case AF_INET:
	    if (ipo->ipo_src.sin.sin_addr.s_addr != INADDR_ANY)
	    {
		if (direction == IPSP_DIRECTION_OUT)
		  bcopy(&ipo->ipo_src, &ssrc, sizeof(union sockaddr_union));
		else
		  bcopy(&ipo->ipo_src, &sdst, sizeof(union sockaddr_union));
	    }
            else
              ignore = 1;
	    break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:
	    if (!IN6_IS_ADDR_UNSPECIFIED(&ipo->ipo_src.sin6.sin6_addr))
	    {
		if (direction == IPSP_DIRECTION_OUT)
		  bcopy(&ipo->ipo_src, &ssrc, sizeof(union sockaddr_union));
		else
		  bcopy(&ipo->ipo_src, &sdst, sizeof(union sockaddr_union));
	    }
            else
              ignore = 1;
	    break;
#endif /* INET6 */
    }

    /* Do we have a cached entry ? If so, check if it's still valid. */
    if ((ipo->ipo_tdb) && (ipo->ipo_tdb->tdb_flags & TDBF_INVALID))
    {
	TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head, ipo, ipo_tdb_next);
	ipo->ipo_tdb = NULL;
    }

    /* Outgoing packet SPD lookup */
    if (direction == IPSP_DIRECTION_OUT)
    {
	/*
	 * If the packet is destined for the policy-specified gateway/endhost,
	 * and the socket has the BYPASS option set, skip IPsec processing.
	 */
	if ((inp != NULL) &&
	    (inp->inp_seclevel[SL_ESP_TRANS] == IPSEC_LEVEL_BYPASS) &&
	    (inp->inp_seclevel[SL_ESP_NETWORK] == IPSEC_LEVEL_BYPASS) &&
	    (inp->inp_seclevel[SL_AUTH] == IPSEC_LEVEL_BYPASS))
	{
	    /* Direct match */
	    if (bcmp(&sdst, &ipo->ipo_dst, sizeof(union sockaddr_union)) == 0)
	    {
		*error = 0;
		return NULL;
	    }

	    /* Same-host */
	    switch (ipo->ipo_dst.sa.sa_family)
	    {
#ifdef INET
		case AF_INET:
		    if (ipo->ipo_dst.sin.sin_addr.s_addr == INADDR_ANY)
		    {
			*error = 0;
			return NULL;
		    }
		    break;
#endif /* INET */

#ifdef INET6
		case AF_INET6:
		    if (IN6_IS_ADDR_UNSPECIFIED(&ipo->ipo_dst.sin6.sin6_addr))
		    {
			*error = 0;
			return NULL;
		    }
		    break;
#endif /* INET6 */
	    }
	}

	/* Check that the cached TDB (if present), is appropriate */
	if (ipo->ipo_tdb)
	{
	    if (bcmp(&sdst, &ipo->ipo_tdb->tdb_dst,
		     sizeof(union sockaddr_union)))
	    {
		TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head, ipo,
			     ipo_tdb_next);
		ipo->ipo_tdb = NULL;

		/* Fall through to acquisition of TDB */
	    }
	    else
	      return ipo->ipo_tdb; /* Cached entry is good, we're done */
	}

	/*
	 * If no SA has been added since the last time we did a lookup,
	 * there's no point searching for one.
	 */
	if (ipo->ipo_last_searched <= ipsec_last_added)
	{
	    ipo->ipo_last_searched = time.tv_sec; /* "touch" the entry */

	    /* Find an appropriate SA from among the existing SAs */
	    ipo->ipo_tdb = gettdbbyaddr(&sdst, ipo->ipo_sproto, m, af);
	    if (ipo->ipo_tdb)
	    {
		TAILQ_INSERT_TAIL(&ipo->ipo_tdb->tdb_policy_head, ipo,
				  ipo_tdb_next);
		*error = 0;
		ipo->ipo_last_searched = 0;
		return ipo->ipo_tdb;
	    }
	}

	/* So, we don't have an SA -- just a policy */
	switch (ipo->ipo_type)
	{
	    case IPSP_IPSEC_REQUIRE:
		/* Acquire SA through key management */
		if (ipsp_acquire_sa(ipo, &sdst, ignore ? NULL : &ssrc) != 0)
                {
                    *error = EACCES;
		    return NULL;
                }

		/* Fall through */

	    case IPSP_IPSEC_DONTACQ:
		*error = -EINVAL; /* Silently drop packet */
		return NULL;

	    case IPSP_IPSEC_ACQUIRE:
		/* Acquire SA through key management */
		if (ipsp_acquire_sa(ipo, &sdst, ignore ? NULL : &ssrc) != 0)
                {
                    *error = EACCES;
		    return NULL;
                }

		/* Fall through */

	    case IPSP_IPSEC_USE:
		*error = 0;  /* Let packet through */
		return NULL;
	}
    }
    else /* IPSP_DIRECTION_IN */
    {
	/* Check the cached entry */
	if ((ipo->ipo_tdb) &&
	    bcmp(&ssrc, &ipo->ipo_tdb->tdb_src, sizeof(union sockaddr_union)))
	{
	    TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head, ipo, ipo_tdb_next);
	    ipo->ipo_tdb = NULL;
	}

	switch (ipo->ipo_type)
	{
	    case IPSP_IPSEC_DONTACQ:
		/* Does protection match stated policy ? */
		if (tdbp && ipsp_match_policy(tdbp, ipo, m, af))
		{
		    /* Accept packet */
		    *error = 0;
		    return NULL;
		}

		/* Silently drop packet */
		*error = EHOSTUNREACH;
		return NULL;

	    case IPSP_IPSEC_REQUIRE:
		if (tdbp && ipsp_match_policy(tdbp, ipo, m, af))
		{
		    /* Accept packet */
		    *error = 0;
		    return NULL;
		}

		/* If we have a cached entry, just discard the packet */
		if (ipo->ipo_tdb)
		{
		    *error = EHOSTUNREACH;
		    return NULL;
		}

		/*
		 * Find whether there exists an appropriate SA. If so, drop
		 * the packet. Otherwise, try to acquire one (from below).
		 *
		 * If no SA has been added since the last time we did a lookup,
		 * there's no point searching for one.
		 */
		if (ipo->ipo_last_searched <= ipsec_last_added)
		{
		    ipo->ipo_last_searched = time.tv_sec; /* "touch" */

		    if ((ipo->ipo_tdb = gettdbbysrc(&ssrc, ipo->ipo_sproto,
						   m, af)) != NULL)
		    {
			TAILQ_INSERT_TAIL(&ipo->ipo_tdb->tdb_policy_head, ipo,
					  ipo_tdb_next);
			ipo->ipo_last_searched = 0;
			*error = EHOSTUNREACH;
			return NULL;
		    }
		}

		/* Acquire SA through key management */
		if ((*error = ipsp_acquire_sa(ipo, &ssrc,
                                              ignore ? NULL : &sdst)) != 0)
		  return NULL;

		*error = -EINVAL;
		return NULL;

	    case IPSP_IPSEC_USE:
		/*
		 * It doesn't matter what protection it had (if any),
		 * just accept it -- equivalent to PERMIT for input.
		 * This means we can't say that we want in incoming
		 * packet to be unprotected -- at least not directly;
		 * we can always have a DENY policy for ESP/AH packets.
		 */
		*error = 0;
		return NULL;

	    case IPSP_IPSEC_ACQUIRE:
                /*
                 * We don't check for policy match, since we would
                 * accept clear-text packets as well.
                 */

		/* If we have a cached entry, just accept the packet */
		if (ipo->ipo_tdb)
		{
		    *error = 0;
		    return NULL;
		}

		/*
		 * Find whether there exists an appropriate SA. If so, accept
		 * the packet. Otherwise, try to acquire one (from below).
		 *
		 * If no SA has been added since the last time we did a lookup,
		 * there's no point searching for one.
		 */
		if (ipo->ipo_last_searched <= ipsec_last_added)
		{
		    ipo->ipo_last_searched = time.tv_sec; /* "touch" */

		    if ((ipo->ipo_tdb = gettdbbysrc(&ssrc, ipo->ipo_sproto,
						   m, af)) != NULL)
		    {
			TAILQ_INSERT_TAIL(&ipo->ipo_tdb->tdb_policy_head, ipo,
					  ipo_tdb_next);
			ipo->ipo_last_searched = 0;
			*error = 0;
			return NULL;
		    }
		}

		/* Acquire SA through key management */
		if ((*error = ipsp_acquire_sa(ipo, &ssrc,
                                              ignore ? NULL : &sdst)) != 0)
		  return NULL;

		/* Just accept the packet */
		*error = 0;
		return NULL;
	}
    }

    /* Shouldn't ever get this far */
    *error = EINVAL;
    return NULL;
}


/*
 * See if a specific SA satisfies stated policy. Return 0 if false, 1 (or
 * non-zero) otherwise.
 */
int
ipsp_match_policy(struct tdb *tdb, struct ipsec_policy *ipo,
		  struct mbuf *m, int af)
{
    union sockaddr_union peer;
    int pflag = 0;

    switch (ipo->ipo_dst.sa.sa_family)
    {
#ifdef INET
	case AF_INET:
	    if (ipo->ipo_dst.sin.sin_addr.s_addr == INADDR_ANY)
	      pflag = 1;
	    break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:
	    if (IN6_IS_ADDR_UNSPECIFIED(&ipo->ipo_dst.sin6.sin6_addr))
	      pflag = 1;
	    break;
#endif /* INET6 */

	case 0: /* Just in case */
	    pflag = 1;
	    break;

	default:
	    return 0; /* Unknown/unsupported network protocol */
    }

    if (pflag == 0)
      bcopy(&ipo->ipo_dst, &peer, sizeof(union sockaddr_union));
    else
    {
	bzero(&peer, sizeof(union sockaddr_union));

	/* Need to copy the source address from the packet */
	switch (af)
	{
#ifdef INET
	    case AF_INET:
		peer.sin.sin_family = AF_INET;
		peer.sin.sin_len = sizeof(struct sockaddr_in);
		m_copydata(m, offsetof(struct ip, ip_src),
			   sizeof(struct in_addr),
			   (caddr_t) &peer.sin.sin_addr);
		break;
#endif /* INET */

#ifdef INET6
	    case AF_INET6:
		peer.sin6.sin6_family = AF_INET6;
		peer.sin6.sin6_len = sizeof(struct sockaddr_in6);
		m_copydata(m, offsetof(struct ip6_hdr, ip6_src),
			   sizeof(struct in6_addr),
			   (caddr_t) &peer.sin6.sin6_addr);
		break;
#endif /* INET6 */

	    default:
	        return 0; /* Unknown/unsupported network protocol */
	}
    }

    /*
     * Does the packet use the right security protocol and is coming from
     * the right peer ?
     */
    if ((tdb->tdb_sproto == ipo->ipo_sproto) &&
	(!bcmp(&tdb->tdb_src, &peer, sizeof(union sockaddr_union))))
      return 1;

    return 0;
}

/*
 * Delete a policy from the SPD.
 */
int
ipsec_delete_policy(struct ipsec_policy *ipo)
{
    int err;

    /* Delete */
    err = rtrequest(RTM_DELETE, (struct sockaddr *) &ipo->ipo_addr,
		    (struct sockaddr *) 0, (struct sockaddr *) &ipo->ipo_mask,
		    0, (struct rtentry **) 0);

    if (ipo->ipo_tdb)
      TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head, ipo, ipo_tdb_next);

    TAILQ_REMOVE(&ipsec_policy_head, ipo, ipo_list);
    if (ipo->ipo_srcid)
      FREE(ipo->ipo_srcid, M_TEMP);
    if (ipo->ipo_dstid)
      FREE(ipo->ipo_dstid, M_TEMP);
    FREE(ipo, M_TDB);
    ipsec_in_use--;

    return err;
}

/*
 * Add a policy to the SPD.
 */
struct ipsec_policy *
ipsec_add_policy(struct sockaddr_encap *dst, struct sockaddr_encap *mask,
		 union sockaddr_union *sdst, int type, int sproto)
{
    struct sockaddr_encap encapgw;
    struct ipsec_policy *ipon;

    MALLOC(ipon, struct ipsec_policy *, sizeof(struct ipsec_policy), M_TDB,
	   M_NOWAIT);
    if (ipon == NULL)
      return NULL;

    bzero(ipon, sizeof(struct ipsec_policy));
    bzero((caddr_t) &encapgw, sizeof(struct sockaddr_encap));

    encapgw.sen_len = SENT_LEN;
    encapgw.sen_family = PF_KEY;
    encapgw.sen_type = SENT_IPSP;
    encapgw.sen_ipsp = ipon;

    if (rtrequest(RTM_ADD, (struct sockaddr *) dst,
		  (struct sockaddr *) &encapgw, (struct sockaddr *) mask,
		  RTF_UP | RTF_GATEWAY | RTF_STATIC,
		  (struct rtentry **) 0) != 0)
    {
        DPRINTF(("ipsec_add_policy: failed to add policy\n"));
	FREE(ipon, M_TDB);
	return NULL;
    }

    ipsec_in_use++;

    bcopy(dst, &ipon->ipo_addr, sizeof(struct sockaddr_encap));
    bcopy(mask, &ipon->ipo_mask, sizeof(struct sockaddr_encap));
    bcopy(sdst, &ipon->ipo_dst, sizeof(union sockaddr_union));
    ipon->ipo_sproto = sproto;
    ipon->ipo_type = type;

    TAILQ_INSERT_HEAD(&ipsec_policy_head, ipon, ipo_list);

    return ipon;
}

/*
 * Expire old acquire requests to key management.
 */
void
ipsp_acquire_expirations(void *arg)
{
    struct ipsec_acquire *ipa;

    for (ipa = TAILQ_FIRST(&ipsec_acquire_head);
	 ipa;
	 ipa = TAILQ_FIRST(&ipsec_acquire_head))
    {
	if (ipa->ipa_expire <= time.tv_sec)
	{
	    /* Remove from the list and free */
	    TAILQ_REMOVE(&ipsec_acquire_head, ipa, ipa_next);
	    FREE(ipa, M_TDB);
	}
	else
	{
	    /* Schedule us for another expiration */
	    timeout(ipsp_acquire_expirations, (void *) NULL,
		    hz * (ipa->ipa_expire - time.tv_sec));
	    return;
	}
    }

    /* If there's no request pending, we don't need to schedule us */

    return;
}

/*
 * Signal key management that we need an SA.
 */
int
ipsp_acquire_sa(struct ipsec_policy *ipo, union sockaddr_union *gw,
		union sockaddr_union *laddr)
{
    struct ipsec_acquire *ipa;

    /*
     * Check whether request has been made already.
     * XXX We need a more scalable data structure instead of a list.
     */
    for (ipa = TAILQ_FIRST(&ipsec_acquire_head);
	 ipa;
	 ipa = TAILQ_NEXT(ipa, ipa_next))
    {
	/* Already in process */
	if (!bcmp(gw, &ipa->ipa_addr, sizeof(union sockaddr_union)))
	  return 0;
    }

    /* Add request in cache and proceed */
    MALLOC(ipa, struct ipsec_acquire *, sizeof(struct ipsec_acquire),
	   M_TDB, M_DONTWAIT);
    if (ipa == NULL)
      return ENOMEM;
    bcopy(gw, &ipa->ipa_addr, sizeof(union sockaddr_union));
    ipa->ipa_expire = time.tv_sec + ipsec_expire_acquire;
    TAILQ_INSERT_TAIL(&ipsec_acquire_head, ipa, ipa_next);

    if (TAILQ_FIRST(&ipsec_acquire_head) == ipa)
      timeout(ipsp_acquire_expirations, (void *) NULL,
	      hz * (ipa->ipa_expire - time.tv_sec));

    /* PF_KEYv2 notification message */
    return pfkeyv2_acquire(ipo, gw, laddr);
}
