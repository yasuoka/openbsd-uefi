/*	$OpenBSD: radix_mpath.c,v 1.1 2004/04/25 02:48:03 itojun Exp $	*/
/*	$KAME: radix_mpath.c,v 1.13 2002/10/28 21:05:59 itojun Exp $	*/

/*
 * Copyright (C) 2001 WIDE Project.
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
 * THE AUTHORS DO NOT GUARANTEE THAT THIS SOFTWARE DOES NOT INFRINGE
 * ANY OTHERS' INTELLECTUAL PROPERTIES. IN NO EVENT SHALL THE AUTHORS
 * BE LIABLE FOR ANY INFRINGEMENT OF ANY OTHERS' INTELLECTUAL
 * PROPERTIES.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#define	M_DONTWAIT M_NOWAIT
#include <sys/domain.h>
#include <sys/syslog.h>
#include <net/radix.h>
#include <net/radix_mpath.h>
#include <net/route.h>
#include <dev/rndvar.h>

/*
 * give some jitter to hash, to avoid synchronization between routers
 */
static u_int32_t hashjitter;

int
rn_mpath_capable(rnh)
	struct radix_node_head *rnh;
{

	return rnh->rnh_multipath;
}

struct radix_node *
rn_mpath_next(rn)
	struct radix_node *rn;
{
	struct radix_node *next;

	if (!rn->rn_dupedkey)
		return NULL;
	next = rn->rn_dupedkey;
	if (rn->rn_mask == next->rn_mask)
		return next;
	else
		return NULL;
}

int
rn_mpath_count(rn)
	struct radix_node *rn;
{
	int i;

	i = 1;
	while ((rn = rn_mpath_next(rn)) != NULL)
		i++;
	return i;
}

struct rtentry *
rt_mpath_matchgate(rt, gate)
	struct rtentry *rt;
	struct sockaddr *gate;
{
	struct radix_node *rn;

	if (!rn_mpath_next((struct radix_node *)rt))
		return rt;

	if (!gate)
		return NULL;
	/* beyond here, we use rn as the master copy */
	rn = (struct radix_node *)rt;
	do {
		rt = (struct rtentry *)rn;
		if (rt->rt_gateway->sa_len == gate->sa_len &&
		    !memcmp(rt->rt_gateway, gate, gate->sa_len))
			break;
	} while ((rn = rn_mpath_next(rn)) != NULL);
	if (!rn)
		return NULL;

	return (struct rtentry *)rn;
}

/*
 * check if we have the same key/mask/gateway on the table already.
 */
int
rt_mpath_conflict(rnh, rt, netmask)
	struct radix_node_head *rnh;
	struct rtentry *rt;
	struct sockaddr *netmask;
{
	struct radix_node *rn, *rn1;
	struct rtentry *rt1;
	char *p, *q, *eq;
	int same, l, skip;

	rn = (struct radix_node *)rt;
	rn1 = rnh->rnh_lookup(rt_key(rt), netmask, rnh);
	if (!rn1 || rn1->rn_flags & RNF_ROOT)
		return 0;

	/*
	 * unlike other functions we have in this file, we have to check
	 * all key/mask/gateway as rnh_lookup can match less specific entry.
	 */
	rt1 = (struct rtentry *)rn1;

	/* compare key. */
	if (rt_key(rt1)->sa_len != rt_key(rt)->sa_len ||
	    bcmp(rt_key(rt1), rt_key(rt), rt_key(rt1)->sa_len))
		goto different;

	/* key was the same.  compare netmask.  hairy... */
	if (rt_mask(rt1) && netmask) {
		skip = rnh->rnh_treetop->rn_off;
		if (rt_mask(rt1)->sa_len > netmask->sa_len) {
			/*
			 * as rt_mask(rt1) is made optimal by radix.c,
			 * there must be some 1-bits on rt_mask(rt1)
			 * after netmask->sa_len.  therefore, in
			 * this case, the entries are different.
			 */
			if (rt_mask(rt1)->sa_len > skip)
				goto different;
			else {
				/* no bits to compare, i.e. same*/
				goto maskmatched;
			}
		}

		l = rt_mask(rt1)->sa_len;
		if (skip > l) {
			/* no bits to compare, i.e. same */
			goto maskmatched;
		}
		p = (char *)rt_mask(rt1);
		q = (char *)netmask;
		if (bcmp(p + skip, q + skip, l - skip))
			goto different;
		/*
		 * need to go through all the bit, as netmask is not
		 * optimal and can contain trailing 0s
		 */
		eq = (char *)netmask + netmask->sa_len;
		q += l;
		same = 1;
		while (eq > q)
			if (*q++) {
				same = 0;
				break;
			}
		if (!same)
			goto different;
	} else if (!rt_mask(rt1) && !netmask)
		; /* no mask to compare, i.e. same */
	else {
		/* one has mask and the other does not, different */
		goto different;
	}

 maskmatched:;

	/* key/mask were the same.  compare gateway for all multipaths */
	do {
		rt1 = (struct rtentry *)rn1;

		/* sanity: no use in comparing the same thing */
		if (rn1 == rn)
			continue;

		if (rt1->rt_gateway->sa_len != rt->rt_gateway->sa_len ||
		    bcmp(rt1->rt_gateway, rt->rt_gateway,
		    rt1->rt_gateway->sa_len))
			continue;

		/* all key/mask/gateway are the same.  conflicting entry. */
		return EEXIST;
	} while ((rn1 = rn_mpath_next(rn1)) != NULL);

 different:
	return 0;
}

void
rtalloc_mpath(ro, hash)
	struct route *ro;
	int hash;
{
	struct radix_node *rn0, *rn;
	int n;

	/*
	 * XXX we don't attempt to lookup cached route again; what should
	 * be done for sendto(3) case?
	 */
	if (ro->ro_rt && ro->ro_rt->rt_ifp && (ro->ro_rt->rt_flags & RTF_UP))
		return;				 /* XXX */
	ro->ro_rt = rtalloc1(&ro->ro_dst, 1);
	/* if the route does not exist or it is not multipath, don't care */
	if (!ro->ro_rt || !rn_mpath_next((struct radix_node *)ro->ro_rt))
		return;

	/* beyond here, we use rn as the master copy */
	rn0 = rn = (struct radix_node *)ro->ro_rt;
	n = rn_mpath_count(rn0);

	/* gw selection by Modulo-N Hash (RFC2991) XXX need improvement? */
	hash += hashjitter;
	hash %= n;
	while (hash-- > 0 && rn) {
		/* stay within the multipath routes */
		if (rn->rn_dupedkey && rn->rn_mask != rn->rn_dupedkey->rn_mask)
			break;
		rn = rn->rn_dupedkey;
	}

	/* XXX try filling rt_gwroute and avoid unreachable gw  */

	/* if gw selection fails, use the first match (default) */
	if (!rn)
		return;

	rtfree(ro->ro_rt);
	ro->ro_rt = (struct rtentry *)rn;
	ro->ro_rt->rt_refcnt++;
}

int
rn_mpath_inithead(head, off)
	void **head;
	int off;
{
	struct radix_node_head *rnh;

	hashjitter = arc4random();
	if (rn_inithead(head, off) == 1) {
		rnh = (struct radix_node_head *)*head;
		rnh->rnh_multipath = 1;
		return 1;
	} else
		return 0;
}
