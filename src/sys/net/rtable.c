/*	$OpenBSD: rtable.c,v 1.3 2015/08/20 12:51:10 mpi Exp $ */

/*
 * Copyright (c) 2014-2015 Martin Pieuchot
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
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/queue.h>

#include <net/rtable.h>
#include <net/route.h>

#ifndef ART

void
rtable_init(void)
{
	rn_init();
}

int
rtable_attach(void **head, int off)
{
	int rv;

#ifndef SMALL_KERNEL
	rv = rn_mpath_inithead(head, off);
#else
	rv = rn_inithead(head, off);
#endif

	return (rv);
}

struct rtentry *
rtable_lookup(unsigned int rtableid, struct sockaddr *dst,
    struct sockaddr *mask)
{
	struct radix_node_head	*rnh;
	struct radix_node	*rn;

	rnh = rtable_get(rtableid, dst->sa_family);
	if (rnh == NULL)
		return (NULL);

	rn = rnh->rnh_lookup(dst, mask, rnh);
	if (rn == NULL || (rn->rn_flags & RNF_ROOT) != 0)
		return (NULL);

	return ((struct rtentry *)rn);
}

struct rtentry *
rtable_match(unsigned int rtableid, struct sockaddr *dst)
{
	struct radix_node_head	*rnh;
	struct radix_node	*rn;

	rnh = rtable_get(rtableid, dst->sa_family);
	if (rnh == NULL)
		return (NULL);

	rn = rnh->rnh_matchaddr(dst, rnh);
	if (rn == NULL || (rn->rn_flags & RNF_ROOT) != 0)
		return (NULL);

	return ((struct rtentry *)rn);
}

int
rtable_insert(unsigned int rtableid, struct sockaddr *dst,
    struct sockaddr *mask, uint8_t prio, struct rtentry *rt)
{
	struct radix_node_head	*rnh;
	struct radix_node	*rn = (struct radix_node *)rt;

	rnh = rtable_get(rtableid, dst->sa_family);
	if (rnh == NULL)
		return (EAFNOSUPPORT);

	rn = rnh->rnh_addaddr(dst, mask, rnh, rn, prio);
	if (rn == NULL)
		return (ESRCH);

	return (0);
}

int
rtable_delete(unsigned int rtableid, struct sockaddr *dst,
    struct sockaddr *mask, uint8_t prio, struct rtentry *rt)
{
	struct radix_node_head	*rnh;
	struct radix_node	*rn = (struct radix_node *)rt;

	rnh = rtable_get(rtableid, dst->sa_family);
	if (rnh == NULL)
		return (EAFNOSUPPORT);

	rn = rnh->rnh_deladdr(dst, mask, rnh, rn);
	if (rn == NULL)
		return (ESRCH);

	if (rn->rn_flags & (RNF_ACTIVE | RNF_ROOT))
		panic("active node flags=%x", rn->rn_flags);

	return (0);
}

int
rtable_setid(void **p, unsigned int rtableid, sa_family_t af)
{
	struct radix_node_head **rnh = (struct radix_node_head **)p;

	if (rnh == NULL || rnh[af] == NULL)
		return (EINVAL);

	rnh[af]->rnh_rtableid = rtableid;

	return (0);
}

int
rtable_walk(unsigned int rtableid, sa_family_t af,
    int (*func)(struct rtentry *, void *, unsigned int), void *arg)
{
	struct radix_node_head	*rnh;
	int (*f)(struct radix_node *, void *, u_int) = (void *)func;

	rnh = rtable_get(rtableid, af);
	if (rnh == NULL)
		return (EAFNOSUPPORT);

	return (*rnh->rnh_walktree)(rnh, f, arg);
}

#ifndef SMALL_KERNEL
int
rtable_mpath_capable(unsigned int rtableid, sa_family_t af)
{
	struct radix_node_head	*rnh;

	rnh = rtable_get(rtableid, af);
	if (rnh == NULL)
		return (0);

	return (rnh->rnh_multipath);
}

struct rtentry *
rtable_mpath_match(unsigned int rtableid, struct rtentry *rt,
    struct sockaddr *gateway, uint8_t prio)
{
	struct radix_node_head	*rnh;

	rnh = rtable_get(rtableid, rt_key(rt)->sa_family);
	if (rnh == NULL || rnh->rnh_multipath == 0)
		return (rt);

	rt = rt_mpath_matchgate(rt, gateway, prio);

	return (rt);
}

int
rtable_mpath_conflict(unsigned int rtableid, struct sockaddr *dst,
    struct sockaddr *mask, struct sockaddr *gateway, uint8_t prio, int mpathok)
{
	struct radix_node_head	*rnh;

	rnh = rtable_get(rtableid, dst->sa_family);
	if (rnh == NULL)
		return (EAFNOSUPPORT);

	if (rnh->rnh_multipath == 0)
		return (0);

	return (rt_mpath_conflict(rnh, dst, mask, gateway, prio, mpathok));
}

struct rtentry *
rtable_mpath_select(struct rtentry *rt, uint32_t *src)
{
	return (rn_mpath_select(rt, src));
}

void
rtable_mpath_reprio(struct rtentry *rt, uint8_t newprio)
{
	struct radix_node	*rn = (struct radix_node *)rt;

	rn_mpath_reprio(rn, newprio);
}
#endif /* SMALL_KERNEL */

#else /* ART */

struct pool		an_pool;	/* pool for ART node structures */

static inline int	 satoplen(struct art_root *, struct sockaddr *);
static inline uint8_t	*satoaddr(struct art_root *, struct sockaddr *);

void
rtable_init(void)
{
	pool_init(&an_pool, sizeof(struct art_node), 0, 0, 0, "art node", NULL);
}

int
rtable_attach(void **head, int off)
{
	return (art_attach(head, off));
}

struct rtentry *
rtable_lookup(unsigned int rtableid, struct sockaddr *dst,
    struct sockaddr *mask)
{
	struct art_root			*ar;
	struct art_node			*an;
	uint8_t				*addr;
	int				 plen;

	ar = rtable_get(rtableid, dst->sa_family);
	if (ar == NULL)
		return (NULL);

	addr = satoaddr(ar, dst);

	/* No need for a perfect match. */
	if (mask == NULL) {
		an = art_match(ar, addr);
	} else {
		plen = satoplen(ar, mask);
		if (plen == -1)
			return (NULL);

		an = art_lookup(ar, addr, plen);
		/* Make sure we've got a perfect match. */
		if (an == NULL || an->an_plen != plen ||
		    memcmp(an->an_dst, dst, dst->sa_len))
			return (NULL);
	}

	if (an == NULL)
		return (NULL);

	return (LIST_FIRST(&an->an_rtlist));
}

struct rtentry *
rtable_match(unsigned int rtableid, struct sockaddr *dst)
{
	struct art_root			*ar;
	struct art_node			*an;
	uint8_t				*addr;

	ar = rtable_get(rtableid, dst->sa_family);
	if (ar == NULL)
		return (NULL);

	addr = satoaddr(ar, dst);
	an = art_match(ar, addr);
	if (an == NULL)
		return (NULL);

	return (LIST_FIRST(&an->an_rtlist));
}

int
rtable_insert(unsigned int rtableid, struct sockaddr *dst,
    struct sockaddr *mask, uint8_t prio, struct rtentry *rt)
{
#ifndef SMALL_KERNEL
	struct rtentry			*mrt;
#endif
	struct art_root			*ar;
	struct art_node			*an, *prev;
	uint8_t				*addr;
	int				 plen;

	ar = rtable_get(rtableid, dst->sa_family);
	if (ar == NULL)
		return (EAFNOSUPPORT);

	addr = satoaddr(ar, dst);
	plen = satoplen(ar, mask);
	if (plen == -1)
		return (EINVAL);

	an = pool_get(&an_pool, PR_NOWAIT | PR_ZERO);
	if (an == NULL)
		return (ENOBUFS);

	an->an_dst = dst;
	an->an_plen = plen;

	prev = art_insert(ar, an, addr, plen);
	if (prev == NULL) {
		pool_put(&an_pool, an);
		return (ESRCH);
	}

	if (prev == an) {
		rt->rt_flags &= ~RTF_MPATH;
	} else {
		pool_put(&an_pool, an);
#ifndef SMALL_KERNEL
		an = prev;

		mrt = LIST_FIRST(&an->an_rtlist);

		KASSERT(mrt != NULL);
		KASSERT((rt->rt_flags & RTF_MPATH) || mrt->rt_priority != prio);

		/*
		 * An ART node with the same destination/netmask already
		 * exists, MPATH conflict must have been already checked.
		 */
		if (rt->rt_flags & RTF_MPATH) {
			/*
			 * Only keep the RTF_MPATH flag if two routes have
			 * the same gateway.
			 */
			rt->rt_flags &= ~RTF_MPATH;
			LIST_FOREACH(mrt, &an->an_rtlist, rt_next) {
				if (mrt->rt_priority == prio) {
					mrt->rt_flags |= RTF_MPATH;
					rt->rt_flags |= RTF_MPATH;
				}
			}
		}
#else
		return (EEXIST);
#endif /* SMALL_KERNEL */
	}

	rt->rt_node = an;
	rt->rt_dest = dst;

	/*
	 * XXX Allocating a sockaddr for the mask per node wastes a lot
	 * of memory, thankfully we'll get rid of that when rt_mask()
	 * will be no more.
	 */
	if (mask != NULL) {
		struct sockaddr		*msk;

		msk = malloc(dst->sa_len, M_RTABLE, M_NOWAIT | M_ZERO);
		if (msk == NULL) {
			pool_put(&an_pool, an);
			return (ENOMEM);
		}
		memcpy(msk, mask, dst->sa_len);
		rt->rt_mask = msk;
	}

#ifndef SMALL_KERNEL
	if ((mrt = LIST_FIRST(&an->an_rtlist)) != NULL) {
		/*
		 * Select the order of the MPATH routes.
		 */
		while (LIST_NEXT(mrt, rt_next) != NULL) {
			if (mrt->rt_priority > prio)
				break;
			mrt = LIST_NEXT(mrt, rt_next);
		}

		if (mrt->rt_priority > prio)
			LIST_INSERT_BEFORE(mrt, rt, rt_next);
		else
			LIST_INSERT_AFTER(mrt, rt, rt_next);

		return (0);
	}
#endif /* SMALL_KERNEL */

	LIST_INSERT_HEAD(&an->an_rtlist, rt, rt_next);
	return (0);
}

int
rtable_delete(unsigned int rtableid, struct sockaddr *dst,
    struct sockaddr *mask, uint8_t prio, struct rtentry *rt)
{
	struct art_root			*ar;
	struct art_node			*an = rt->rt_node;
	uint8_t				*addr;
	int				 plen;

	ar = rtable_get(rtableid, dst->sa_family);
	if (ar == NULL)
		return (EAFNOSUPPORT);

#ifdef DIAGNOSTIC
	if (memcmp(dst, an->an_dst, dst->sa_len)) {
		printf("%s: destination do not match\n", __func__);
		return (EINVAL);
	}
	if (mask != NULL && an->an_plen != satoplen(ar, mask)) {
		printf("%s: mask do not match\n", __func__);
		return (EINVAL);
	}
#endif

	/*
	 * XXX Is it safe to free the mask now?  Are we sure rt_mask()
	 * is only used when entries are in the table?
	 */
	free(rt->rt_mask, M_RTABLE, 0);

	/* Remove rt <-> ART glue. */
	rt->rt_node = NULL;
	rt->rt_mask = NULL;
	LIST_REMOVE(rt, rt_next);
	KASSERT(rt->rt_refcnt >= 0);

#ifndef SMALL_KERNEL
	if ((rt = LIST_FIRST(&an->an_rtlist)) != NULL) {
		an->an_dst = rt->rt_dest;
		if (LIST_NEXT(rt, rt_next) == NULL)
			rt->rt_flags &= ~RTF_MPATH;
		return (0);
	}
#endif /* SMALL_KERNEL */

	addr = satoaddr(ar, an->an_dst);
	plen = an->an_plen;

	if (art_delete(ar, an, addr, plen) == NULL)
		return (ESRCH);

	pool_put(&an_pool, an);

	return (0);
}

int
rtable_setid(void **p, unsigned int rtableid, sa_family_t af)
{
	struct art_root			**ar = (struct art_root **)p;

	if (ar == NULL || ar[af] == NULL)
		return (EINVAL);

	ar[af]->ar_rtableid = rtableid;

	return (0);
}

struct rtable_walk_cookie {
	int		(*rwc_func)(struct rtentry *, void *, unsigned int);
	void		 *rwc_arg;
	unsigned int	  rwc_rid;
};

/*
 * Helper for rtable_walk to keep the ART code free from any "struct rtentry".
 */
int
rtable_walk_helper(struct art_node *an, void *xrwc)
{
	struct rtable_walk_cookie	*rwc = xrwc;
	struct rtentry			*rt, *nrt;
	int				 error = 0;

	LIST_FOREACH_SAFE(rt, &an->an_rtlist, rt_next, nrt) {
		if ((error = (*rwc->rwc_func)(rt, rwc->rwc_arg, rwc->rwc_rid)))
			break;
	}

	return (error);
}

int
rtable_walk(unsigned int rtableid, sa_family_t af,
    int (*func)(struct rtentry *, void *, unsigned int), void *arg)
{
	struct art_root			*ar;
	struct rtable_walk_cookie	 rwc;

	ar = rtable_get(rtableid, af);
	if (ar == NULL)
		return (EAFNOSUPPORT);

	rwc.rwc_func = func;
	rwc.rwc_arg = arg;
	rwc.rwc_rid = rtableid;

	return art_walk(ar, rtable_walk_helper, &rwc);
}

#ifndef SMALL_KERNEL
int
rtable_mpath_capable(unsigned int rtableid, sa_family_t af)
{
	return (1);
}

struct rtentry *
rtable_mpath_match(unsigned int rtableid, struct rtentry *rt0,
    struct sockaddr *gateway, uint8_t prio)
{
	struct art_node			*an = rt0->rt_node;
	struct rtentry			*rt;

	LIST_FOREACH(rt, &an->an_rtlist, rt_next) {
		if (prio != RTP_ANY &&
		    (rt->rt_priority & RTP_MASK) != (prio & RTP_MASK))
			continue;

		if (gateway == NULL)
			return (rt);

		if (rt->rt_gateway->sa_len == gateway->sa_len &&
		    memcmp(rt->rt_gateway, gateway, gateway->sa_len) == 0)
			break;
	}

	return (rt);
}

int
rtable_mpath_conflict(unsigned int rtableid, struct sockaddr *dst,
    struct sockaddr *mask, struct sockaddr *gateway, uint8_t prio, int mpathok)
{
	struct art_root			*ar;
	struct art_node			*an;
	struct rtentry			*rt;
	uint8_t				*addr;
	int				 plen;

	ar = rtable_get(rtableid, dst->sa_family);
	if (ar == NULL)
		return (EAFNOSUPPORT);

	addr = satoaddr(ar, dst);
	plen = satoplen(ar, mask);
	if (plen == -1)
		return (EINVAL);

	an = art_lookup(ar, addr, plen);
	/* Make sure we've got a perfect match. */
	if (an == NULL || an->an_plen != plen ||
	    memcmp(an->an_dst, dst, dst->sa_len))
		return (0);

	LIST_FOREACH(rt, &an->an_rtlist, rt_next) {
		if (prio != RTP_ANY &&
		    (rt->rt_priority & RTP_MASK) != (prio & RTP_MASK))
			continue;

		if (!mpathok)
			return (EEXIST);

		if (rt->rt_gateway->sa_len == gateway->sa_len &&
		    memcmp(rt->rt_gateway, gateway, gateway->sa_len) == 0)
			return (EEXIST);
	}


	return (0);
}

struct rtentry *
rtable_mpath_select(struct rtentry *rt, uint32_t *src)
{
	struct art_node			*an = rt->rt_node;

	/*
	 * XXX consider using ``src'' (8
	 */
	return (LIST_FIRST(&an->an_rtlist));
}

void
rtable_mpath_reprio(struct rtentry *rt, uint8_t newprio)
{
	/* XXX */
}
#endif /* SMALL_KERNEL */

/*
 * Return a pointer to the address (key).  This is an heritage from the
 * BSD radix tree needed to skip the non-address fields from the flavor
 * of "struct sockaddr" used by this routing table.
 */
static inline uint8_t *
satoaddr(struct art_root *at, struct sockaddr *sa)
{
	return (((uint8_t *)sa) + at->ar_off);
}

/*
 * Return the prefix length of a mask.
 */
static inline int
satoplen(struct art_root *ar, struct sockaddr *mask)
{
	uint8_t				*ap, *ep;
	int				 skip, mlen, plen = 0;

	/* Host route */
	if (mask == NULL)
		return (ar->ar_alen);

	mlen = mask->sa_len;

	/* Default route */
	if (mlen == 0)
		return (0);

	skip = ar->ar_off;

	ap = (uint8_t *)((uint8_t *)mask) + skip;
	ep = (uint8_t *)((uint8_t *)mask) + mlen;
	if (ap > ep)
		return (-1);

	if (ap == ep)
		return (0);

	/* "Beauty" adapted from sbin/route/show.c ... */
	while (ap < ep) {
		switch (*ap) {
		case 0xff:
			plen += 8;
			ap++;
			break;
		case 0xfe:
			plen += 7;
			ap++;
			goto out;
		case 0xfc:
			plen += 6;
			ap++;
			goto out;
		case 0xf8:
			plen += 5;
			ap++;
			goto out;
		case 0xf0:
			plen += 4;
			ap++;
			goto out;
		case 0xe0:
			plen += 3;
			ap++;
			goto out;
		case 0xc0:
			plen += 2;
			ap++;
			goto out;
		case 0x80:
			plen += 1;
			ap++;
			goto out;
		case 0x00:
			goto out;
		default:
			/* Non contiguous mask. */
			return (-1);
		}

	}

out:
#ifdef DIAGNOSTIC
	for (; ap < ep; ap++) {
		if (*ap != 0x00)
			return (-1);
	}
#endif

	return (plen);
}
#endif /* ART */
