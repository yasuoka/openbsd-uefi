/*	$OpenBSD: route.c,v 1.225 2015/08/24 22:11:33 mpi Exp $	*/
/*	$NetBSD: route.c,v 1.14 1996/02/13 22:00:46 christos Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1980, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)route.c	8.2 (Berkeley) 11/15/93
 */

/*
 *	@(#)COPYRIGHT	1.1 (NRL) 17 January 1995
 * 
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 * 	This product includes software developed by the University of
 * 	California, Berkeley and its contributors.
 * 	This product includes software developed at the Information
 * 	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/timeout.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/pool.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#endif

#ifdef MPLS
#include <netmpls/mpls.h>
#endif

#ifdef IPSEC
#include <netinet/ip_ipsp.h>
#include <net/if_enc.h>
#endif

struct	rtstat		   rtstat;
void			***rt_tables;
u_int8_t		   af2rtafidx[AF_MAX+1];
u_int8_t		   rtafidx_max;
u_int			   rtbl_id_max = 0;
u_int			  *rt_tab2dom;	/* rt table to domain lookup table */

int			rttrash;	/* routes not in table but not freed */

struct pool		rtentry_pool;	/* pool for rtentry structures */
struct pool		rttimer_pool;	/* pool for rttimer structures */

void	rt_timer_init(void);
int	rtable_alloc(void ***, u_int);
int	rtflushclone1(struct rtentry *, void *, u_int);
void	rtflushclone(unsigned int, struct rtentry *);
int	rt_if_remove_rtdelete(struct rtentry *, void *, u_int);

struct	ifaddr *ifa_ifwithroute(int, struct sockaddr *, struct sockaddr *,
		    u_int);

#define	LABELID_MAX	50000

struct rt_label {
	TAILQ_ENTRY(rt_label)	rtl_entry;
	char			rtl_name[RTLABEL_LEN];
	u_int16_t		rtl_id;
	int			rtl_ref;
};

TAILQ_HEAD(rt_labels, rt_label)	rt_labels = TAILQ_HEAD_INITIALIZER(rt_labels);

int
rtable_alloc(void ***table, u_int id)
{
	void		**p;
	struct domain	 *dom;
	u_int8_t	  i;

	if ((p = mallocarray(rtafidx_max + 1, sizeof(void *), M_RTABLE,
	    M_NOWAIT|M_ZERO)) == NULL)
		return (ENOMEM);

	/* 2nd pass: attach */
	for (dom = domains; dom != NULL; dom = dom->dom_next)
		if (dom->dom_rtattach)
			dom->dom_rtattach(&p[af2rtafidx[dom->dom_family]],
			    dom->dom_rtoffset);

	for (i = 0; i < rtafidx_max; i++)
		rtable_setid(p, id, i);

	*table = (void **)p;

	return (0);
}

void
route_init(void)
{
	struct domain	 *dom;

	pool_init(&rtentry_pool, sizeof(struct rtentry), 0, 0, 0, "rtentry",
	    NULL);
	rtable_init();	/* initialize all zeroes, all ones, mask table */

	bzero(af2rtafidx, sizeof(af2rtafidx));
	rtafidx_max = 1;	/* must have NULL at index 0, so start at 1 */

	/* find out how many tables to allocate */
	for (dom = domains; dom != NULL; dom = dom->dom_next)
		if (dom->dom_rtattach)
			af2rtafidx[dom->dom_family] = rtafidx_max++;

	if (rtable_add(0) != 0)
		panic("route_init rtable_add");
}

int
rtable_add(u_int id)
{
	void	*p, *q;

	splsoftassert(IPL_SOFTNET);

	if (id > RT_TABLEID_MAX)
		return (EINVAL);

	if (id == 0 || id > rtbl_id_max) {
		size_t	newlen;
		size_t	newlen2;

		if ((p = mallocarray(id + 1, sizeof(void *), M_RTABLE,
		    M_NOWAIT|M_ZERO)) == NULL)
			return (ENOMEM);
		newlen = sizeof(void *) * (id+1);
		if ((q = mallocarray(id + 1, sizeof(u_int), M_RTABLE,
		    M_NOWAIT|M_ZERO)) == NULL) {
			free(p, M_RTABLE, newlen);
			return (ENOMEM);
		}
		newlen2 = sizeof(u_int) * (id+1);
		if (rt_tables) {
			bcopy(rt_tables, p, sizeof(void *) * (rtbl_id_max+1));
			bcopy(rt_tab2dom, q, sizeof(u_int) * (rtbl_id_max+1));
			free(rt_tables, M_RTABLE, 0);
			free(rt_tab2dom, M_RTABLE, 0);
		}
		rt_tables = p;
		rt_tab2dom = q;
		rtbl_id_max = id;
	}

	if (rt_tables[id] != NULL)	/* already exists */
		return (EEXIST);

	rt_tab2dom[id] = 0;	/* use main table/domain by default */
	return (rtable_alloc(&rt_tables[id], id));
}

void *
rtable_get(u_int id, sa_family_t af)
{
	if (id > rtbl_id_max)
		return (NULL);
	return (rt_tables[id] ? rt_tables[id][af2rtafidx[af]] : NULL);
}

u_int
rtable_l2(u_int id)
{
	if (id > rtbl_id_max)
		return (0);
	return (rt_tab2dom[id]);
}

void
rtable_l2set(u_int id, u_int parent)
{
	splsoftassert(IPL_SOFTNET);

	if (!rtable_exists(id) || !rtable_exists(parent))
		return;
	rt_tab2dom[id] = parent;
}

int
rtable_exists(u_int id)	/* verify table with that ID exists */
{
	if (id > RT_TABLEID_MAX)
		return (0);

	if (id > rtbl_id_max)
		return (0);

	if (rt_tables[id] == NULL)
		return (0);

	return (1);
}

struct rtentry *
rtalloc(struct sockaddr *dst, int flags, unsigned int tableid)
{
	struct rtentry		*rt;
	struct rtentry		*newrt = NULL;
	struct rt_addrinfo	 info;
	int			 s, error = 0, msgtype = RTM_MISS;

	s = splsoftnet();

	bzero(&info, sizeof(info));
	info.rti_info[RTAX_DST] = dst;

	rt = rtable_match(tableid, dst);
	if (rt != NULL) {
		newrt = rt;
		if ((rt->rt_flags & RTF_CLONING) && ISSET(flags, RT_RESOLVE)) {
			error = rtrequest1(RTM_RESOLVE, &info, RTP_DEFAULT,
			    &newrt, tableid);
			if (error) {
				newrt = rt;
				rt->rt_refcnt++;
				goto miss;
			}
			rt = newrt;
			if (rt->rt_flags & RTF_XRESOLVE) {
				msgtype = RTM_RESOLVE;
				goto miss;
			}
			/* Inform listeners of the new route */
			rt_sendmsg(rt, RTM_ADD, tableid);
		} else
			rt->rt_refcnt++;
	} else {
		rtstat.rts_unreach++;
miss:
		if (ISSET(flags, RT_REPORT)) {
			bzero((caddr_t)&info, sizeof(info));
			info.rti_info[RTAX_DST] = dst;
			rt_missmsg(msgtype, &info, 0, NULL, error, tableid);
		}
	}
	splx(s);
	return (newrt);
}

#ifndef SMALL_KERNEL
/*
 * Allocate a route, potentially using multipath to select the peer.
 */
struct rtentry *
rtalloc_mpath(struct sockaddr *dst, uint32_t *src, unsigned int rtableid)
{
	struct rtentry *rt;

	rt = rtalloc(dst, RT_REPORT|RT_RESOLVE, rtableid);

	/* if the route does not exist or it is not multipath, don't care */
	if (rt == NULL || !ISSET(rt->rt_flags, RTF_MPATH))
		return (rt);

	/* check if multipath routing is enabled for the specified protocol */
	if (!(0
	    || (ipmultipath && dst->sa_family == AF_INET)
#ifdef INET6
	    || (ip6_multipath && dst->sa_family == AF_INET6)
#endif
	    ))
		return (rt);

	return (rtable_mpath_select(rt, src));
}
#endif /* SMALL_KERNEL */

void
rtfree(struct rtentry *rt)
{
	struct ifaddr	*ifa;

	if (rt == NULL)
		return;

	rt->rt_refcnt--;

	if (rt->rt_refcnt <= 0 && (rt->rt_flags & RTF_UP) == 0) {
		if (rt->rt_refcnt == 0 && RT_ACTIVE(rt))
			return; /* route still active but currently down */
		if (RT_ACTIVE(rt) || RT_ROOT(rt))
			panic("rtfree 2");
		rttrash--;
		if (rt->rt_refcnt < 0) {
			printf("rtfree: %p not freed (neg refs)\n", rt);
			return;
		}
		rt_timer_remove_all(rt);
		ifa = rt->rt_ifa;
		if (ifa)
			ifafree(ifa);
		rtlabel_unref(rt->rt_labelid);
#ifdef MPLS
		if (rt->rt_flags & RTF_MPLS)
			free(rt->rt_llinfo, M_TEMP, 0);
#endif
		if (rt->rt_gateway)
			free(rt->rt_gateway, M_RTABLE, 0);
		free(rt_key(rt), M_RTABLE, 0);
		pool_put(&rtentry_pool, rt);
	}
}

void
rt_sendmsg(struct rtentry *rt, int cmd, u_int rtableid)
{
	struct rt_addrinfo info;
	struct sockaddr_rtlabel sa_rl;

	memset(&info, 0, sizeof(info));
	info.rti_info[RTAX_DST] = rt_key(rt);
	info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
	info.rti_info[RTAX_NETMASK] = rt_mask(rt);
	info.rti_info[RTAX_LABEL] = rtlabel_id2sa(rt->rt_labelid, &sa_rl);
	if (rt->rt_ifp != NULL) {
		info.rti_info[RTAX_IFP] =(struct sockaddr *)rt->rt_ifp->if_sadl;
		info.rti_info[RTAX_IFA] = rt->rt_ifa->ifa_addr;
	}

	rt_missmsg(cmd, &info, rt->rt_flags, rt->rt_ifp, 0, rtableid);
}

void
ifafree(struct ifaddr *ifa)
{
	if (ifa == NULL)
		panic("ifafree");
	if (ifa->ifa_refcnt == 0)
		free(ifa, M_IFADDR, 0);
	else
		ifa->ifa_refcnt--;
}

/*
 * Force a routing table entry to the specified
 * destination to go through the given gateway.
 * Normally called as a result of a routing redirect
 * message from the network layer.
 *
 * N.B.: must be called at splsoftnet
 */
void
rtredirect(struct sockaddr *dst, struct sockaddr *gateway,
    struct sockaddr *netmask, int flags, struct sockaddr *src,
    struct rtentry **rtp, u_int rdomain)
{
	struct rtentry		*rt;
	int			 error = 0;
	u_int32_t		*stat = NULL;
	struct rt_addrinfo	 info;
	struct ifaddr		*ifa;
	struct ifnet		*ifp = NULL;

	splsoftassert(IPL_SOFTNET);

	/* verify the gateway is directly reachable */
	if ((ifa = ifa_ifwithnet(gateway, rdomain)) == NULL) {
		error = ENETUNREACH;
		goto out;
	}
	ifp = ifa->ifa_ifp;
	rt = rtalloc(dst, 0, rdomain);
	/*
	 * If the redirect isn't from our current router for this dst,
	 * it's either old or wrong.  If it redirects us to ourselves,
	 * we have a routing loop, perhaps as a result of an interface
	 * going down recently.
	 */
#define	equal(a1, a2) \
	((a1)->sa_len == (a2)->sa_len && \
	 bcmp((caddr_t)(a1), (caddr_t)(a2), (a1)->sa_len) == 0)
	if (!(flags & RTF_DONE) && rt &&
	     (!equal(src, rt->rt_gateway) || rt->rt_ifa != ifa))
		error = EINVAL;
	else if (ifa_ifwithaddr(gateway, rdomain) != NULL)
		error = EHOSTUNREACH;
	if (error)
		goto done;
	/*
	 * Create a new entry if we just got back a wildcard entry
	 * or the lookup failed.  This is necessary for hosts
	 * which use routing redirects generated by smart gateways
	 * to dynamically build the routing tables.
	 */
	if ((rt == NULL) || (rt_mask(rt) && rt_mask(rt)->sa_len < 2))
		goto create;
	/*
	 * Don't listen to the redirect if it's
	 * for a route to an interface. 
	 */
	if (rt->rt_flags & RTF_GATEWAY) {
		if (((rt->rt_flags & RTF_HOST) == 0) && (flags & RTF_HOST)) {
			/*
			 * Changing from route to net => route to host.
			 * Create new route, rather than smashing route to net.
			 */
create:
			if (rt)
				rtfree(rt);
			flags |= RTF_GATEWAY | RTF_DYNAMIC;
			bzero(&info, sizeof(info));
			info.rti_info[RTAX_DST] = dst;
			info.rti_info[RTAX_GATEWAY] = gateway;
			info.rti_info[RTAX_NETMASK] = netmask;
			info.rti_ifa = ifa;
			info.rti_flags = flags;
			rt = NULL;
			error = rtrequest1(RTM_ADD, &info, RTP_DEFAULT, &rt,
			    rdomain);
			if (error == 0)
				flags = rt->rt_flags;
			stat = &rtstat.rts_dynamic;
		} else {
			/*
			 * Smash the current notion of the gateway to
			 * this destination.  Should check about netmask!!!
			 */
			rt->rt_flags |= RTF_MODIFIED;
			flags |= RTF_MODIFIED;
			stat = &rtstat.rts_newgateway;
			rt_setgate(rt, gateway, rdomain);
		}
	} else
		error = EHOSTUNREACH;
done:
	if (rt) {
		if (rtp && !error)
			*rtp = rt;
		else
			rtfree(rt);
	}
out:
	if (error)
		rtstat.rts_badredirect++;
	else if (stat != NULL)
		(*stat)++;
	bzero((caddr_t)&info, sizeof(info));
	info.rti_info[RTAX_DST] = dst;
	info.rti_info[RTAX_GATEWAY] = gateway;
	info.rti_info[RTAX_NETMASK] = netmask;
	info.rti_info[RTAX_AUTHOR] = src;
	rt_missmsg(RTM_REDIRECT, &info, flags, ifp, error, rdomain);
}

/*
 * Delete a route and generate a message
 */
int
rtdeletemsg(struct rtentry *rt, u_int tableid)
{
	int			error;
	struct rt_addrinfo	info;
	struct ifnet		*ifp;

	/*
	 * Request the new route so that the entry is not actually
	 * deleted.  That will allow the information being reported to
	 * be accurate (and consistent with route_output()).
	 */
	bzero((caddr_t)&info, sizeof(info));
	info.rti_info[RTAX_DST] = rt_key(rt);
	info.rti_info[RTAX_NETMASK] = rt_mask(rt);
	info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
	info.rti_flags = rt->rt_flags;
	ifp = rt->rt_ifp;
	error = rtrequest1(RTM_DELETE, &info, rt->rt_priority, &rt, tableid);
	rt_missmsg(RTM_DELETE, &info, info.rti_flags, ifp, error, tableid);
	if (error == 0)
		rtfree(rt);
	return (error);
}

static inline int
rtequal(struct rtentry *a, struct rtentry *b)
{
	if (memcmp(rt_key(a), rt_key(b), rt_key(a)->sa_len) == 0 &&
	    memcmp(rt_mask(a), rt_mask(b), rt_mask(a)->sa_len) == 0)
		return 1;
	else
		return 0;
}

int
rtflushclone1(struct rtentry *rt, void *arg, u_int id)
{
	struct rtentry *parent = arg;

	if ((rt->rt_flags & RTF_CLONED) != 0 && (rt->rt_parent == parent ||
	    rtequal(rt->rt_parent, parent)))
		rtdeletemsg(rt, id);
	return 0;
}

void
rtflushclone(unsigned int rtableid, struct rtentry *parent)
{

#ifdef DIAGNOSTIC
	if (!parent || (parent->rt_flags & RTF_CLONING) == 0)
		panic("rtflushclone: called with a non-cloning route");
#endif
	rtable_walk(rtableid, rt_key(parent)->sa_family, rtflushclone1, parent);
}

int
rtioctl(u_long req, caddr_t data, struct proc *p)
{
	return (EOPNOTSUPP);
}

struct ifaddr *
ifa_ifwithroute(int flags, struct sockaddr *dst, struct sockaddr *gateway,
    u_int rtableid)
{
	struct ifaddr	*ifa;

	if ((flags & RTF_GATEWAY) == 0) {
		/*
		 * If we are adding a route to an interface,
		 * and the interface is a pt to pt link
		 * we should search for the destination
		 * as our clue to the interface.  Otherwise
		 * we can use the local address.
		 */
		ifa = NULL;
		if (flags & RTF_HOST)
			ifa = ifa_ifwithdstaddr(dst, rtableid);
		if (ifa == NULL)
			ifa = ifa_ifwithaddr(gateway, rtableid);
	} else {
		/*
		 * If we are adding a route to a remote net
		 * or host, the gateway may still be on the
		 * other end of a pt to pt link.
		 */
		ifa = ifa_ifwithdstaddr(gateway, rtableid);
	}
	if (ifa == NULL) {
		if (gateway->sa_family == AF_LINK) {
			struct sockaddr_dl *sdl = (struct sockaddr_dl *)gateway;
			struct ifnet *ifp = if_get(sdl->sdl_index);

			if (ifp == NULL)
				ifp = ifunit(sdl->sdl_data);
			if (ifp != NULL)
				ifa = ifaof_ifpforaddr(dst, ifp);
		} else {
			ifa = ifa_ifwithnet(gateway, rtableid);
		}
	}
	if (ifa == NULL) {
		struct rtentry	*rt = rtalloc(gateway, 0, rtableid);
		if (rt == NULL)
			return (NULL);
		/* The gateway must be local if the same address family. */
		if ((rt->rt_flags & RTF_GATEWAY) &&
		    rt_key(rt)->sa_family == dst->sa_family) {
			rtfree(rt);
			return (NULL);
		}
		ifa = rt->rt_ifa;
		if (ifa == NULL || ifa->ifa_ifp == NULL) {
			rtfree(rt);
			return (NULL);
		}
		rtfree(rt);
	}
	if (ifa->ifa_addr->sa_family != dst->sa_family) {
		struct ifaddr	*oifa = ifa;
		ifa = ifaof_ifpforaddr(dst, ifa->ifa_ifp);
		if (ifa == NULL)
			ifa = oifa;
	}
	return (ifa);
}

#define ROUNDUP(a) (a>0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

int
rt_getifa(struct rt_addrinfo *info, u_int rtid)
{
	struct ifnet	*ifp = NULL;

	/*
	 * ifp may be specified by sockaddr_dl when protocol address
	 * is ambiguous
	 */
	if (info->rti_info[RTAX_IFP] != NULL) {
		struct sockaddr_dl *sdl;

		sdl = (struct sockaddr_dl *)info->rti_info[RTAX_IFP];
		ifp = if_get(sdl->sdl_index);
		if (ifp == NULL)
			ifp = ifunit(sdl->sdl_data);
	}

#ifdef IPSEC
	/*
	 * If the destination is a PF_KEY address, we'll look
	 * for the existence of a encap interface number or address
	 * in the options list of the gateway. By default, we'll return
	 * enc0.
	 */
	if (info->rti_info[RTAX_DST] &&
	    info->rti_info[RTAX_DST]->sa_family == PF_KEY)
		info->rti_ifa = enc_getifa(rtid, 0);
#endif

	if (info->rti_ifa == NULL && info->rti_info[RTAX_IFA] != NULL)
		info->rti_ifa = ifa_ifwithaddr(info->rti_info[RTAX_IFA], rtid);

	if (info->rti_ifa == NULL) {
		struct sockaddr	*sa;

		if ((sa = info->rti_info[RTAX_IFA]) == NULL)
			if ((sa = info->rti_info[RTAX_GATEWAY]) == NULL)
				sa = info->rti_info[RTAX_DST];

		if (sa != NULL && ifp != NULL)
			info->rti_ifa = ifaof_ifpforaddr(sa, ifp);
		else if (info->rti_info[RTAX_DST] != NULL &&
		    info->rti_info[RTAX_GATEWAY] != NULL)
			info->rti_ifa = ifa_ifwithroute(info->rti_flags,
			    info->rti_info[RTAX_DST],
			    info->rti_info[RTAX_GATEWAY],
			    rtid);
		else if (sa != NULL)
			info->rti_ifa = ifa_ifwithroute(info->rti_flags,
			    sa, sa, rtid);
	}

	if (info->rti_ifa == NULL)
		return (ENETUNREACH);

	return (0);
}

int
rtrequest1(int req, struct rt_addrinfo *info, u_int8_t prio,
    struct rtentry **ret_nrt, u_int tableid)
{
	struct rtentry		*rt, *crt;
	struct ifaddr		*ifa;
	struct sockaddr		*ndst;
	struct sockaddr_rtlabel	*sa_rl, sa_rl2;
	struct sockaddr_dl	 sa_dl = { sizeof(sa_dl), AF_LINK };
	int			 dlen, error;
#ifdef MPLS
	struct sockaddr_mpls	*sa_mpls;
#endif

	splsoftassert(IPL_SOFTNET);

	if (!rtable_exists(tableid))
		return (EAFNOSUPPORT);
	if (info->rti_flags & RTF_HOST)
		info->rti_info[RTAX_NETMASK] = NULL;
	switch (req) {
	case RTM_DELETE:
		rt = rtable_lookup(tableid, info->rti_info[RTAX_DST],
		    info->rti_info[RTAX_NETMASK]);
		if (rt == NULL)
			return (ESRCH);
#ifndef SMALL_KERNEL
		rt = rtable_mpath_match(tableid, rt,
		    info->rti_info[RTAX_GATEWAY], prio);
		if (rt == NULL)
			return (ESRCH);

		/*
		 * If we got multipath routes, we require users to specify
		 * a matching gateway.
		 */
		if ((rt->rt_flags & RTF_MPATH) &&
		    info->rti_info[RTAX_GATEWAY] == NULL)
			return (ESRCH);
#endif

		/*
		 * Since RTP_LOCAL cannot be set by userland, make
		 * sure that local routes are only modified by the
		 * kernel.
		 */
		if ((rt->rt_flags & (RTF_LOCAL|RTF_BROADCAST)) &&
		    prio != RTP_LOCAL)
			return (EINVAL);

		error = rtable_delete(tableid, info->rti_info[RTAX_DST],
		    info->rti_info[RTAX_NETMASK], prio, rt);
		if (error != 0)
			return (ESRCH);

		/* clean up any cloned children */
		if ((rt->rt_flags & RTF_CLONING) != 0)
			rtflushclone(tableid, rt);

		if (rt->rt_gwroute) {
			rtfree(rt->rt_gwroute);
			rt->rt_gwroute = NULL;
		}

		if (rt->rt_parent) {
			rt->rt_parent->rt_refcnt--;
			rt->rt_parent = NULL;
		}

		rt->rt_flags &= ~RTF_UP;
		if ((ifa = rt->rt_ifa) && ifa->ifa_rtrequest)
			ifa->ifa_rtrequest(RTM_DELETE, rt);
		rttrash++;

		rt->rt_refcnt++;
		if (ret_nrt != NULL)
			*ret_nrt = rt;
		else
			rtfree(rt);
		break;

	case RTM_RESOLVE:
		if (ret_nrt == NULL || (rt = *ret_nrt) == NULL)
			return (EINVAL);
		if ((rt->rt_flags & RTF_CLONING) == 0)
			return (EINVAL);
		if (rt->rt_ifa->ifa_ifp) {
			info->rti_ifa = rt->rt_ifa;
		} else {
			/*
			 * The address of the cloning route is not longer
			 * configured on an interface, but its descriptor
			 * is still there because of reference counting.
			 *
			 * Try to find a similar active address and use
			 * it for the cloned route.  The cloning route
			 * will get the new address and interface later.
			 */
			info->rti_ifa = NULL;
			info->rti_info[RTAX_IFA] = rt->rt_ifa->ifa_addr;
		}

		info->rti_flags = rt->rt_flags & ~(RTF_CLONING | RTF_STATIC);
		info->rti_flags |= RTF_CLONED;
		info->rti_info[RTAX_GATEWAY] = (struct sockaddr *)&sa_dl;
		info->rti_flags |= RTF_HOST;
		info->rti_info[RTAX_LABEL] =
		    rtlabel_id2sa(rt->rt_labelid, &sa_rl2);
		/* FALLTHROUGH */

	case RTM_ADD:
		if (info->rti_ifa == NULL && (error = rt_getifa(info, tableid)))
			return (error);
		ifa = info->rti_ifa;
		if (prio == 0)
			prio = ifa->ifa_ifp->if_priority + RTP_STATIC;

		dlen = info->rti_info[RTAX_DST]->sa_len;
		ndst = malloc(dlen, M_RTABLE, M_NOWAIT);
		if (ndst == NULL)
			return (ENOBUFS);

		if (info->rti_info[RTAX_NETMASK] != NULL)
			rt_maskedcopy(info->rti_info[RTAX_DST], ndst,
			    info->rti_info[RTAX_NETMASK]);
		else
			memcpy(ndst, info->rti_info[RTAX_DST], dlen);

#ifndef SMALL_KERNEL
		/* Do not permit exactly the same dst/mask/gw pair. */
		if (rtable_mpath_conflict(tableid, ndst,
		    info->rti_info[RTAX_NETMASK], info->rti_info[RTAX_GATEWAY],
		    prio, info->rti_flags & RTF_MPATH)) {
			free(ndst, M_RTABLE, dlen);
			return (EEXIST);
		}
#endif
		rt = pool_get(&rtentry_pool, PR_NOWAIT | PR_ZERO);
		if (rt == NULL) {
			free(ndst, M_RTABLE, dlen);
			return (ENOBUFS);
		}

		rt->rt_flags = info->rti_flags;
		rt->rt_tableid = tableid;
		rt->rt_priority = prio;	/* init routing priority */
		LIST_INIT(&rt->rt_timer);

#ifndef SMALL_KERNEL
		if (rtable_mpath_capable(tableid, ndst->sa_family)) {
			/* check the link state since the table supports it */
			if (LINK_STATE_IS_UP(ifa->ifa_ifp->if_link_state) &&
			    ifa->ifa_ifp->if_flags & IFF_UP)
				rt->rt_flags |= RTF_UP;
			else {
				rt->rt_flags &= ~RTF_UP;
				rt->rt_priority |= RTP_DOWN;
			}
		}
#endif

		if (info->rti_info[RTAX_LABEL] != NULL) {
			sa_rl = (struct sockaddr_rtlabel *)
			    info->rti_info[RTAX_LABEL];
			rt->rt_labelid = rtlabel_name2id(sa_rl->sr_label);
		}

#ifdef MPLS
		/* We have to allocate additional space for MPLS infos */ 
		if (info->rti_flags & RTF_MPLS &&
		    (info->rti_info[RTAX_SRC] != NULL ||
		    info->rti_info[RTAX_DST]->sa_family == AF_MPLS)) {
			struct rt_mpls *rt_mpls;

			sa_mpls = (struct sockaddr_mpls *)
			    info->rti_info[RTAX_SRC];

			rt->rt_llinfo = malloc(sizeof(struct rt_mpls),
			    M_TEMP, M_NOWAIT|M_ZERO);

			if (rt->rt_llinfo == NULL) {
				free(ndst, M_RTABLE, dlen);
				pool_put(&rtentry_pool, rt);
				return (ENOMEM);
			}

			rt_mpls = (struct rt_mpls *)rt->rt_llinfo;

			if (sa_mpls != NULL)
				rt_mpls->mpls_label = sa_mpls->smpls_label;

			rt_mpls->mpls_operation = info->rti_mpls;

			/* XXX: set experimental bits */

			rt->rt_flags |= RTF_MPLS;
		} else
			rt->rt_flags &= ~RTF_MPLS;
#endif

		ifa->ifa_refcnt++;
		rt->rt_ifa = ifa;
		rt->rt_ifp = ifa->ifa_ifp;
		if (req == RTM_RESOLVE) {
			/*
			 * If the ifa of the cloning route was stale, a
			 * successful lookup for an ifa with the same address
			 * has been made.  Use this ifa also for the cloning
			 * route.
			 */
			if ((*ret_nrt)->rt_ifa->ifa_ifp == NULL) {
				printf("rtrequest1 RTM_RESOLVE: wrong ifa (%p) "
				    "was (%p)\n", ifa, (*ret_nrt)->rt_ifa);
				if ((*ret_nrt)->rt_ifa->ifa_rtrequest)
					(*ret_nrt)->rt_ifa->ifa_rtrequest(
					    RTM_DELETE, *ret_nrt);
				ifafree((*ret_nrt)->rt_ifa);
				(*ret_nrt)->rt_ifa = ifa;
				(*ret_nrt)->rt_ifp = ifa->ifa_ifp;
				ifa->ifa_refcnt++;
				if (ifa->ifa_rtrequest)
					ifa->ifa_rtrequest(RTM_ADD, *ret_nrt);
			}
			/*
			 * Copy both metrics and a back pointer to the cloned
			 * route's parent.
			 */
			rt->rt_rmx = (*ret_nrt)->rt_rmx; /* copy metrics */
			rt->rt_priority = (*ret_nrt)->rt_priority;
			rt->rt_parent = *ret_nrt;	 /* Back ptr. to parent. */
			rt->rt_parent->rt_refcnt++;
		}

		/*
		 * We must set rt->rt_gateway before adding ``rt'' to
		 * the routing table because the radix MPATH code use
		 * it to (re)order routes.
		 */
		if ((error = rt_setgate(rt, info->rti_info[RTAX_GATEWAY],
		    tableid))) {
			free(ndst, M_RTABLE, dlen);
			pool_put(&rtentry_pool, rt);
			return (error);
		}

		error = rtable_insert(tableid, ndst,
		    info->rti_info[RTAX_NETMASK], rt->rt_priority, rt);
		if (error != 0 && (crt = rtalloc(ndst, 0, tableid)) != NULL) {
			/* overwrite cloned route */
			if ((crt->rt_flags & RTF_CLONED) != 0) {
				rtdeletemsg(crt, tableid);
				error = rtable_insert(tableid, ndst,
				    info->rti_info[RTAX_NETMASK],
				    rt->rt_priority, rt);
				}
			rtfree(crt);
		}
		if (error != 0) {
			ifafree(ifa);
			if ((rt->rt_flags & RTF_CLONED) != 0 && rt->rt_parent)
				rtfree(rt->rt_parent);
			if (rt->rt_gwroute)
				rtfree(rt->rt_gwroute);
			if (rt->rt_gateway)
				free(rt->rt_gateway, M_RTABLE, 0);
			free(ndst, M_RTABLE, dlen);
			pool_put(&rtentry_pool, rt);
			return (EEXIST);
		}

		if (ifa->ifa_rtrequest)
			ifa->ifa_rtrequest(req, rt);
		if (ret_nrt) {
			*ret_nrt = rt;
			rt->rt_refcnt++;
		}
		if ((rt->rt_flags & RTF_CLONING) != 0) {
			/* clean up any cloned children */
			rtflushclone(tableid, rt);
		}

		if_group_routechange(info->rti_info[RTAX_DST],
			info->rti_info[RTAX_NETMASK]);
		break;
	}

	return (0);
}

int
rt_setgate(struct rtentry *rt, struct sockaddr *gate, unsigned int tableid)
{
	int glen = ROUNDUP(gate->sa_len);
	struct sockaddr *sa;

	if (rt->rt_gateway == NULL || glen > ROUNDUP(rt->rt_gateway->sa_len)) {
		sa = malloc(glen, M_RTABLE, M_NOWAIT);
		if (sa == NULL)
			return (ENOBUFS);
		free(rt->rt_gateway, M_RTABLE, 0);
		rt->rt_gateway = sa;
	}
	memmove(rt->rt_gateway, gate, glen);

	if (rt->rt_gwroute != NULL) {
		rtfree(rt->rt_gwroute);
		rt->rt_gwroute = NULL;
	}
	if (rt->rt_flags & RTF_GATEWAY) {
		/* XXX is this actually valid to cross tables here? */
		rt->rt_gwroute = rtalloc(gate, RT_REPORT|RT_RESOLVE, tableid);
		/*
		 * If we switched gateways, grab the MTU from the new
		 * gateway route if the current MTU is 0 or greater
		 * than the MTU of gateway.
		 * Note that, if the MTU of gateway is 0, we will reset the
		 * MTU of the route to run PMTUD again from scratch. XXX
		 */
		if (rt->rt_gwroute && !(rt->rt_rmx.rmx_locks & RTV_MTU) &&
		    rt->rt_rmx.rmx_mtu &&
		    rt->rt_rmx.rmx_mtu > rt->rt_gwroute->rt_rmx.rmx_mtu) {
			rt->rt_rmx.rmx_mtu = rt->rt_gwroute->rt_rmx.rmx_mtu;
		}
	}
	return (0);
}

int
rt_checkgate(struct ifnet *ifp, struct rtentry *rt, struct sockaddr *dst,
    unsigned int rtableid, struct rtentry **rtp)
{
	struct rtentry *rt0;

	KASSERT(rt != NULL);

	if ((rt->rt_flags & RTF_UP) == 0) {
		rt = rtalloc(dst, RT_REPORT|RT_RESOLVE, rtableid);
		if (rt == NULL)
			return (EHOSTUNREACH);
		rt->rt_refcnt--;
		if (rt->rt_ifp != ifp)
			return (EHOSTUNREACH);
	}

	rt0 = rt;

	if (rt->rt_flags & RTF_GATEWAY) {
		if (rt->rt_gwroute && !(rt->rt_gwroute->rt_flags & RTF_UP)) {
			rtfree(rt->rt_gwroute);
			rt->rt_gwroute = NULL;
		}
		if (rt->rt_gwroute == NULL) {
			rt->rt_gwroute = rtalloc(rt->rt_gateway,
			    RT_REPORT|RT_RESOLVE, rtableid);
			if (rt->rt_gwroute == NULL)
				return (EHOSTUNREACH);
		}
		/*
		 * Next hop must be reachable, this also prevents rtentry
		 * loops, for example when rt->rt_gwroute points to rt.
		 */
		if (((rt->rt_gwroute->rt_flags & (RTF_UP|RTF_GATEWAY)) !=
		    RTF_UP) || (rt->rt_gwroute->rt_ifp != ifp)) {
			rtfree(rt->rt_gwroute);
			rt->rt_gwroute = NULL;
			return (EHOSTUNREACH);
		}
		rt = rt->rt_gwroute;
	}

	if (rt->rt_flags & RTF_REJECT)
		if (rt->rt_expire == 0 || time_second < rt->rt_expire)
			return (rt == rt0 ? EHOSTDOWN : EHOSTUNREACH);

	*rtp = rt;
	return (0);
}

void
rt_maskedcopy(struct sockaddr *src, struct sockaddr *dst,
    struct sockaddr *netmask)
{
	u_char	*cp1 = (u_char *)src;
	u_char	*cp2 = (u_char *)dst;
	u_char	*cp3 = (u_char *)netmask;
	u_char	*cplim = cp2 + *cp3;
	u_char	*cplim2 = cp2 + *cp1;

	*cp2++ = *cp1++; *cp2++ = *cp1++; /* copies sa_len & sa_family */
	cp3 += 2;
	if (cplim > cplim2)
		cplim = cplim2;
	while (cp2 < cplim)
		*cp2++ = *cp1++ & *cp3++;
	if (cp2 < cplim2)
		bzero((caddr_t)cp2, (unsigned)(cplim2 - cp2));
}

int
rt_ifa_add(struct ifaddr *ifa, int flags, struct sockaddr *dst)
{
	struct ifnet		*ifp = ifa->ifa_ifp;
	struct rtentry		*rt;
	struct sockaddr_rtlabel	 sa_rl;
	struct rt_addrinfo	 info;
	u_short			 rtableid = ifp->if_rdomain;
	u_int8_t		 prio = ifp->if_priority + RTP_STATIC;
	int			 error;

	memset(&info, 0, sizeof(info));
	info.rti_ifa = ifa;
	info.rti_flags = flags | RTF_MPATH;
	info.rti_info[RTAX_DST] = dst;
	if (flags & RTF_LLINFO)
		info.rti_info[RTAX_GATEWAY] = (struct sockaddr *)ifp->if_sadl;
	else
		info.rti_info[RTAX_GATEWAY] = ifa->ifa_addr;
	info.rti_info[RTAX_LABEL] = rtlabel_id2sa(ifp->if_rtlabelid, &sa_rl);

#ifdef MPLS
	if ((flags & RTF_MPLS) == RTF_MPLS) {
		info.rti_mpls = MPLS_OP_POP;

		/* MPLS routes only exist in rdomain 0 */
		rtableid = 0;
	}
#endif /* MPLS */

	if ((flags & RTF_HOST) == 0)
		info.rti_info[RTAX_NETMASK] = ifa->ifa_netmask;

	if (flags & (RTF_LOCAL|RTF_BROADCAST))
		prio = RTP_LOCAL;

	error = rtrequest1(RTM_ADD, &info, prio, &rt, rtableid);
	if (error == 0) {
		if (rt->rt_ifa != ifa) {
			printf("%s: wrong ifa (%p) was (%p)\n", __func__,
			    ifa, rt->rt_ifa);
			if (rt->rt_ifa->ifa_rtrequest)
				rt->rt_ifa->ifa_rtrequest(RTM_DELETE, rt);
			ifafree(rt->rt_ifa);
			rt->rt_ifa = ifa;
			rt->rt_ifp = ifa->ifa_ifp;
			ifa->ifa_refcnt++;
			if (ifa->ifa_rtrequest)
				ifa->ifa_rtrequest(RTM_ADD, rt);
		}

		/*
		 * A local route is created for every address configured
		 * on an interface, so use this information to notify
		 * userland that a new address has been added.
		 */
		if (flags & RTF_LOCAL)
			rt_sendaddrmsg(rt, RTM_NEWADDR);
		rt_sendmsg(rt, RTM_ADD, rtableid);
		rtfree(rt);
	}
	return (error);
}

int
rt_ifa_del(struct ifaddr *ifa, int flags, struct sockaddr *dst)
{
	struct ifnet		*ifp = ifa->ifa_ifp;
	struct rtentry		*rt;
	struct mbuf		*m = NULL;
	struct sockaddr		*deldst;
	struct rt_addrinfo	 info;
	struct sockaddr_rtlabel	 sa_rl;
	u_short			 rtableid = ifp->if_rdomain;
	u_int8_t		 prio = ifp->if_priority + RTP_STATIC;
	int			 error;

#ifdef MPLS
	if ((flags & RTF_MPLS) == RTF_MPLS)
		/* MPLS routes only exist in rdomain 0 */
		rtableid = 0;
#endif /* MPLS */

	if ((flags & RTF_HOST) == 0 && ifa->ifa_netmask) {
		m = m_get(M_DONTWAIT, MT_SONAME);
		if (m == NULL)
			return (ENOBUFS);
		deldst = mtod(m, struct sockaddr *);
		rt_maskedcopy(dst, deldst, ifa->ifa_netmask);
		dst = deldst;
	}
	if ((rt = rtalloc(dst, 0, rtableid)) != NULL) {
		rt->rt_refcnt--;
#ifndef ART
		/* try to find the right route */
		while (rt && rt->rt_ifa != ifa)
			rt = (struct rtentry *)
			    ((struct radix_node *)rt)->rn_dupedkey;
		if (!rt) {
			if (m != NULL)
				(void) m_free(m);
			return (flags & RTF_HOST ? EHOSTUNREACH
						: ENETUNREACH);
		}
#endif
	}

	memset(&info, 0, sizeof(info));
	info.rti_ifa = ifa;
	info.rti_flags = flags;
	info.rti_info[RTAX_DST] = dst;
	if ((flags & RTF_LLINFO) == 0)
		info.rti_info[RTAX_GATEWAY] = ifa->ifa_addr;
	info.rti_info[RTAX_LABEL] = rtlabel_id2sa(ifp->if_rtlabelid, &sa_rl);

	if ((flags & RTF_HOST) == 0)
		info.rti_info[RTAX_NETMASK] = ifa->ifa_netmask;

	if (flags & (RTF_LOCAL|RTF_BROADCAST))
		prio = RTP_LOCAL;

	error = rtrequest1(RTM_DELETE, &info, prio, &rt, rtableid);
	if (error == 0) {
		rt_sendmsg(rt, RTM_DELETE, rtableid);
		if (flags & RTF_LOCAL)
			rt_sendaddrmsg(rt, RTM_DELADDR);
		rtfree(rt);
	}
	if (m != NULL)
		m_free(m);

	return (error);
}

/*
 * Add ifa's address as a local rtentry.
 */
int
rt_ifa_addlocal(struct ifaddr *ifa)
{
	struct rtentry *rt;
	u_int flags = RTF_HOST|RTF_LOCAL;
	int error = 0;

	/*
	 * If the configured address correspond to the magical "any"
	 * address do not add a local route entry because that might
	 * corrupt the routing tree which uses this value for the
	 * default routes.
	 */
	switch (ifa->ifa_addr->sa_family) {
	case AF_INET:
		if (satosin(ifa->ifa_addr)->sin_addr.s_addr == INADDR_ANY)
			return (0);
		break;
#ifdef INET6
	case AF_INET6:
		if (IN6_ARE_ADDR_EQUAL(&satosin6(ifa->ifa_addr)->sin6_addr,
		    &in6addr_any))
			return (0);
		break;
#endif
	default:
		break;
	}

	if (!ISSET(ifa->ifa_ifp->if_flags, (IFF_LOOPBACK|IFF_POINTOPOINT)))
		flags |= RTF_LLINFO;

	/* If there is no loopback entry, allocate one. */
	rt = rtalloc(ifa->ifa_addr, 0, ifa->ifa_ifp->if_rdomain);
	if (rt == NULL || !ISSET(rt->rt_flags, flags))
		error = rt_ifa_add(ifa, RTF_UP | flags, ifa->ifa_addr);
	if (rt)
		rtfree(rt);

	return (error);
}

/*
 * Remove local rtentry of ifa's addresss if it exists.
 */
int
rt_ifa_dellocal(struct ifaddr *ifa)
{
	struct rtentry *rt;
	u_int flags = RTF_HOST|RTF_LOCAL;
	int error = 0;

	/*
	 * We do not add local routes for such address, so do not bother
	 * removing them.
	 */
	switch (ifa->ifa_addr->sa_family) {
	case AF_INET:
		if (satosin(ifa->ifa_addr)->sin_addr.s_addr == INADDR_ANY)
			return (0);
		break;
#ifdef INET6
	case AF_INET6:
		if (IN6_ARE_ADDR_EQUAL(&satosin6(ifa->ifa_addr)->sin6_addr,
		    &in6addr_any))
			return (0);
		break;
#endif
	default:
		break;
	}

	if (!ISSET(ifa->ifa_ifp->if_flags, (IFF_LOOPBACK|IFF_POINTOPOINT)))
		flags |= RTF_LLINFO;

	/*
	 * Before deleting, check if a corresponding local host
	 * route surely exists.  With this check, we can avoid to
	 * delete an interface direct route whose destination is same
	 * as the address being removed.  This can happen when removing
	 * a subnet-router anycast address on an interface attached
	 * to a shared medium.
	 */
	rt = rtalloc(ifa->ifa_addr, 0, ifa->ifa_ifp->if_rdomain);
	if (rt != NULL && ISSET(rt->rt_flags, flags))
		error = rt_ifa_del(ifa, flags, ifa->ifa_addr);
	if (rt)
		rtfree(rt);

	return (error);
}

/*
 * Route timer routines.  These routes allow functions to be called
 * for various routes at any time.  This is useful in supporting
 * path MTU discovery and redirect route deletion.
 *
 * This is similar to some BSDI internal functions, but it provides
 * for multiple queues for efficiency's sake...
 */

LIST_HEAD(, rttimer_queue)	rttimer_queue_head;
static int			rt_init_done = 0;

#define RTTIMER_CALLOUT(r)	{				\
	if (r->rtt_func != NULL) {				\
		(*r->rtt_func)(r->rtt_rt, r);			\
	} else {						\
		struct rt_addrinfo info;			\
		bzero(&info, sizeof(info));			\
		info.rti_info[RTAX_DST] = rt_key(r->rtt_rt);	\
		rtrequest1(RTM_DELETE, &info,			\
		    r->rtt_rt->rt_priority, NULL, r->rtt_tableid);	\
	}							\
}

/* 
 * Some subtle order problems with domain initialization mean that
 * we cannot count on this being run from rt_init before various
 * protocol initializations are done.  Therefore, we make sure
 * that this is run when the first queue is added...
 */

void
rt_timer_init()
{
	static struct timeout	rt_timer_timeout;

	if (rt_init_done)
		panic("rt_timer_init: already initialized");

	pool_init(&rttimer_pool, sizeof(struct rttimer), 0, 0, 0, "rttmr",
	    NULL);

	LIST_INIT(&rttimer_queue_head);
	timeout_set(&rt_timer_timeout, rt_timer_timer, &rt_timer_timeout);
	timeout_add_sec(&rt_timer_timeout, 1);
	rt_init_done = 1;
}

struct rttimer_queue *
rt_timer_queue_create(u_int timeout)
{
	struct rttimer_queue	*rtq;

	if (rt_init_done == 0)
		rt_timer_init();

	if ((rtq = malloc(sizeof(*rtq), M_RTABLE, M_NOWAIT|M_ZERO)) == NULL)
		return (NULL);

	rtq->rtq_timeout = timeout;
	rtq->rtq_count = 0;
	TAILQ_INIT(&rtq->rtq_head);
	LIST_INSERT_HEAD(&rttimer_queue_head, rtq, rtq_link);

	return (rtq);
}

void
rt_timer_queue_change(struct rttimer_queue *rtq, long timeout)
{
	rtq->rtq_timeout = timeout;
}

void
rt_timer_queue_destroy(struct rttimer_queue *rtq)
{
	struct rttimer	*r;

	while ((r = TAILQ_FIRST(&rtq->rtq_head)) != NULL) {
		LIST_REMOVE(r, rtt_link);
		TAILQ_REMOVE(&rtq->rtq_head, r, rtt_next);
		RTTIMER_CALLOUT(r);
		pool_put(&rttimer_pool, r);
		if (rtq->rtq_count > 0)
			rtq->rtq_count--;
		else
			printf("rt_timer_queue_destroy: rtq_count reached 0\n");
	}

	LIST_REMOVE(rtq, rtq_link);
	free(rtq, M_RTABLE, 0);
}

unsigned long
rt_timer_queue_count(struct rttimer_queue *rtq)
{
	return (rtq->rtq_count);
}

void
rt_timer_remove_all(struct rtentry *rt)
{
	struct rttimer	*r;

	while ((r = LIST_FIRST(&rt->rt_timer)) != NULL) {
		LIST_REMOVE(r, rtt_link);
		TAILQ_REMOVE(&r->rtt_queue->rtq_head, r, rtt_next);
		if (r->rtt_queue->rtq_count > 0)
			r->rtt_queue->rtq_count--;
		else
			printf("rt_timer_remove_all: rtq_count reached 0\n");
		pool_put(&rttimer_pool, r);
	}
}

int
rt_timer_add(struct rtentry *rt, void (*func)(struct rtentry *,
    struct rttimer *), struct rttimer_queue *queue, u_int rtableid)
{
	struct rttimer	*r;
	long		 current_time;

	current_time = time_uptime;
	rt->rt_rmx.rmx_expire = time_second + queue->rtq_timeout;

	/*
	 * If there's already a timer with this action, destroy it before
	 * we add a new one.
	 */
	for (r = LIST_FIRST(&rt->rt_timer); r != NULL;
	     r = LIST_NEXT(r, rtt_link)) {
		if (r->rtt_func == func) {
			LIST_REMOVE(r, rtt_link);
			TAILQ_REMOVE(&r->rtt_queue->rtq_head, r, rtt_next);
			if (r->rtt_queue->rtq_count > 0)
				r->rtt_queue->rtq_count--;
			else
				printf("rt_timer_add: rtq_count reached 0\n");
			pool_put(&rttimer_pool, r);
			break;  /* only one per list, so we can quit... */
		}
	}

	r = pool_get(&rttimer_pool, PR_NOWAIT | PR_ZERO);
	if (r == NULL)
		return (ENOBUFS);

	r->rtt_rt = rt;
	r->rtt_time = current_time;
	r->rtt_func = func;
	r->rtt_queue = queue;
	r->rtt_tableid = rtableid;
	LIST_INSERT_HEAD(&rt->rt_timer, r, rtt_link);
	TAILQ_INSERT_TAIL(&queue->rtq_head, r, rtt_next);
	r->rtt_queue->rtq_count++;

	return (0);
}

/* ARGSUSED */
void
rt_timer_timer(void *arg)
{
	struct timeout		*to = (struct timeout *)arg;
	struct rttimer_queue	*rtq;
	struct rttimer		*r;
	long			 current_time;
	int			 s;

	current_time = time_uptime;

	s = splsoftnet();
	for (rtq = LIST_FIRST(&rttimer_queue_head); rtq != NULL;
	     rtq = LIST_NEXT(rtq, rtq_link)) {
		while ((r = TAILQ_FIRST(&rtq->rtq_head)) != NULL &&
		    (r->rtt_time + rtq->rtq_timeout) < current_time) {
			LIST_REMOVE(r, rtt_link);
			TAILQ_REMOVE(&rtq->rtq_head, r, rtt_next);
			RTTIMER_CALLOUT(r);
			pool_put(&rttimer_pool, r);
			if (rtq->rtq_count > 0)
				rtq->rtq_count--;
			else
				printf("rt_timer_timer: rtq_count reached 0\n");
		}
	}
	splx(s);

	timeout_add_sec(to, 1);
}

u_int16_t
rtlabel_name2id(char *name)
{
	struct rt_label		*label, *p = NULL;
	u_int16_t		 new_id = 1;

	if (!name[0])
		return (0);

	TAILQ_FOREACH(label, &rt_labels, rtl_entry)
		if (strcmp(name, label->rtl_name) == 0) {
			label->rtl_ref++;
			return (label->rtl_id);
		}

	/*
	 * to avoid fragmentation, we do a linear search from the beginning
	 * and take the first free slot we find. if there is none or the list
	 * is empty, append a new entry at the end.
	 */

	if (!TAILQ_EMPTY(&rt_labels))
		for (p = TAILQ_FIRST(&rt_labels); p != NULL &&
		    p->rtl_id == new_id; p = TAILQ_NEXT(p, rtl_entry))
			new_id = p->rtl_id + 1;

	if (new_id > LABELID_MAX)
		return (0);

	label = malloc(sizeof(*label), M_TEMP, M_NOWAIT|M_ZERO);
	if (label == NULL)
		return (0);
	strlcpy(label->rtl_name, name, sizeof(label->rtl_name));
	label->rtl_id = new_id;
	label->rtl_ref++;

	if (p != NULL)	/* insert new entry before p */
		TAILQ_INSERT_BEFORE(p, label, rtl_entry);
	else		/* either list empty or no free slot in between */
		TAILQ_INSERT_TAIL(&rt_labels, label, rtl_entry);

	return (label->rtl_id);
}

const char *
rtlabel_id2name(u_int16_t id)
{
	struct rt_label	*label;

	TAILQ_FOREACH(label, &rt_labels, rtl_entry)
		if (label->rtl_id == id)
			return (label->rtl_name);

	return (NULL);
}

struct sockaddr *
rtlabel_id2sa(u_int16_t labelid, struct sockaddr_rtlabel *sa_rl)
{
	const char	*label;

	if (labelid == 0 || (label = rtlabel_id2name(labelid)) == NULL)
		return (NULL);

	bzero(sa_rl, sizeof(*sa_rl));
	sa_rl->sr_len = sizeof(*sa_rl);
	sa_rl->sr_family = AF_UNSPEC;
	strlcpy(sa_rl->sr_label, label, sizeof(sa_rl->sr_label));

	return ((struct sockaddr *)sa_rl);
}

void
rtlabel_unref(u_int16_t id)
{
	struct rt_label	*p, *next;

	if (id == 0)
		return;

	for (p = TAILQ_FIRST(&rt_labels); p != NULL; p = next) {
		next = TAILQ_NEXT(p, rtl_entry);
		if (id == p->rtl_id) {
			if (--p->rtl_ref == 0) {
				TAILQ_REMOVE(&rt_labels, p, rtl_entry);
				free(p, M_TEMP, 0);
			}
			break;
		}
	}
}

void
rt_if_remove(struct ifnet *ifp)
{
	int			 i;
	u_int			 tid;

	for (tid = 0; tid <= rtbl_id_max; tid++) {
		/* skip rtables that are not in the rdomain of the ifp */
		if (rtable_l2(tid) != ifp->if_rdomain)
			continue;
		for (i = 1; i <= AF_MAX; i++) {
			while (rtable_walk(tid, i, rt_if_remove_rtdelete,
				ifp) == EAGAIN)
					;	/* nothing */
		}
	}
}

/*
 * Note that deleting a RTF_CLONING route can trigger the
 * deletion of more entries, so we need to cancel the walk
 * and return EAGAIN.  The caller should restart the walk
 * as long as EAGAIN is returned.
 */
int
rt_if_remove_rtdelete(struct rtentry *rt, void *vifp, u_int id)
{
	struct ifnet	*ifp = vifp;

	if (rt->rt_ifp == ifp) {
		int	cloning = (rt->rt_flags & RTF_CLONING);

		if (rtdeletemsg(rt, id) == 0 && cloning)
			return (EAGAIN);
	}

	/*
	 * XXX There should be no need to check for rt_ifa belonging to this
	 * interface, because then rt_ifp is set, right?
	 */

	return (0);
}

#ifndef SMALL_KERNEL
void
rt_if_track(struct ifnet *ifp)
{
	int i;
	u_int tid;

	if (rt_tables == NULL)
		return;

	for (tid = 0; tid <= rtbl_id_max; tid++) {
		/* skip rtables that are not in the rdomain of the ifp */
		if (rtable_l2(tid) != ifp->if_rdomain)
			continue;
		for (i = 1; i <= AF_MAX; i++) {
			if (!rtable_mpath_capable(tid, i))
				continue;

			while (rtable_walk(tid, i,
			    rt_if_linkstate_change, ifp) == EAGAIN)
				;	/* nothing */
		}
	}
}

int
rt_if_linkstate_change(struct rtentry *rt, void *arg, u_int id)
{
	struct ifnet *ifp = arg;

	if (rt->rt_ifp != ifp)
		return (0);

	if (LINK_STATE_IS_UP(ifp->if_link_state) && ifp->if_flags & IFF_UP) {
		if (!(rt->rt_flags & RTF_UP)) {
			/* bring route up */
			rt->rt_flags |= RTF_UP;
			rtable_mpath_reprio(rt, rt->rt_priority & RTP_MASK);
		}
	} else {
		if (rt->rt_flags & RTF_UP) {
			/*
			 * Remove cloned routes (mainly arp) to
			 * down interfaces so we have a chance to
			 * clone a new route from a better source.
			 */
			if (rt->rt_flags & RTF_CLONED) {
				rtdeletemsg(rt, id);
				return (0);
			}
			/* take route down */
			rt->rt_flags &= ~RTF_UP;
			rtable_mpath_reprio(rt, rt->rt_priority | RTP_DOWN);
		}
	}
	if_group_routechange(rt_key(rt), rt_mask(rt));

	return (0);
}
#endif
