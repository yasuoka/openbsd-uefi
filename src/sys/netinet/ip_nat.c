/*	$OpenBSD: ip_nat.c,v 1.11 1997/04/18 06:10:07 niklas Exp $	*/
/*
 * (C)opyright 1995-1996 by Darren Reed.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and due credit is given
 * to the original author and the contributors.
 *
 * Added redirect stuff and a LOT of bug fixes. (mcn@EnGarde.com)
 *
 */
#if 0
#if !defined(lint) && defined(LIBC_SCCS)
static	char	sccsid[] = "@(#)ip_nat.c	1.11 6/5/96 (C) 1995 Darren Reed";
static	char	rcsid[] = "Id: ip_nat.c,v 2.0.1.10 1997/02/08 06:38:49 darrenr Exp";
#endif
#endif

#if !defined(_KERNEL) && !defined(KERNEL)
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
#endif
#include <sys/errno.h>
#ifndef __OpenBSD__
# include <sys/types.h>
#endif
#include <sys/param.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#ifdef	_KERNEL
# include <sys/systm.h>
#endif
#if !defined(__SVR4) && !defined(__svr4__)
# include <sys/mbuf.h>
#else
# include <sys/byteorder.h>
# include <sys/dditypes.h>
# include <sys/stream.h>
# include <sys/kmem.h>
#endif

#include <net/if.h>
#ifdef sun
#include <net/af.h>
#endif
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#ifdef RFC1825
#include <vpn/md5.h>
#include <vpn/ipsec.h>
extern struct ifnet vpnif;
#endif

#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/tcpip.h>
#include <netinet/ip_icmp.h>
#include "ip_fil_compat.h"
#include "ip_fil.h"
#include "ip_nat.h"
#include "ip_state.h"
#ifndef	MIN
#define	MIN(a,b)	(((a)<(b))?(a):(b))
#endif

nat_t	*nat_table[2][NAT_SIZE], *nat_instances = NULL;
ipnat_t	*nat_list = NULL;
u_long	nat_inuse = 0,
	fr_defnatage = 1200;
natstat_t nat_stats;
#if	SOLARIS
# ifndef	_KERNEL
#define	bzero(a,b)	memset(a,0,b)
#define	bcmp(a,b,c)	memcpy(a,b,c)
#define	bcopy(a,b,c)	memmove(b,a,c)
# else
extern	kmutex_t	ipf_nat;
# endif
#endif

static	int	flush_nattable __P((void)), clear_natlist __P((void));
static	void	nattable_sync __P((void)), nat_delete __P((struct nat *));
void		fix_incksum __P((u_short *, u_long));
void		fix_outcksum __P((u_short *, u_long));
nat_t		*nat_new __P((ipnat_t *, ip_t *, fr_info_t *, u_short, int));
nat_t		*nat_lookupmapip __P((register int, struct in_addr, u_short,
			struct in_addr, u_short));

void
fix_outcksum(sp, n)
	u_short *sp;
	u_long n;
{
	register u_short sumshort;
	register u_long sum1;

#ifdef sparc
	sum1 = (~(*sp)) & 0xffff;
#else
	sum1 = (~ntohs(*sp)) & 0xffff;
#endif
	sum1 += (n);
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	/* Again */
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	sumshort = ~(u_short)sum1;
	*(sp) = htons(sumshort);
}


void
fix_incksum(sp, n)
	u_short *sp;
	u_long n;
{
	register u_short sumshort;
	register u_long sum1;

#ifdef sparc
	sum1 = (~(*sp)) & 0xffff;
#else
	sum1 = (~ntohs(*sp)) & 0xffff;
#endif
	sum1 += ~(n) & 0xffff;
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	/* Again */
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	sumshort = ~(u_short)sum1;
	*(sp) = htons(sumshort);
}


/*
 * How the NAT is organised and works.
 *
 * Inside (interface y) NAT       Outside (interface x)
 * -------------------- -+- -------------------------------------
 * Packet going          |   out, processsed by ip_natout() for x
 * ------------>         |   ------------>
 * src=10.1.1.1          |   src=192.1.1.1
 *                       |
 *                       |   in, processed by ip_natin() for x
 * <------------         |   <------------
 * dst=10.1.1.1          |   dst=192.1.1.1
 * -------------------- -+- -------------------------------------
 * ip_natout() - changes ip_src and if required, sport
 *             - creates a new mapping, if required.
 * ip_natin()  - changes ip_dst and if required, dport
 *
 * In the NAT table, internal source is recorded as "in" and externally
 * seen as "out".
 */

/*
 * Handle ioctls which manipulate the NAT.
 */
int
nat_ioctl(data, cmd, mode)
	caddr_t data;
	u_long cmd;
	int mode;
{
	register ipnat_t *nat, *n = NULL, **np = NULL;
	ipnat_t natd;
	int error = 0, ret;

	/*
	 * For add/delete, look to see if the NAT entry is already present
	 */
	MUTEX_ENTER(&ipf_nat);
	if ((cmd == SIOCADNAT) || (cmd == SIOCRMNAT)) {
		IRCOPY(data, (char *)&natd, sizeof(natd));
		nat = &natd;
		for (np = &nat_list; (n = *np); np = &n->in_next)
			if (!bcmp((char *)&nat->in_flags, (char *)&n->in_flags,
					IPN_CMPSIZ))
				break;
	}

	switch (cmd)
	{
	case SIOCADNAT :
		if (!(mode & FWRITE)) {
			error = EPERM;
			break;
		}
		if (n) {
			error = EEXIST;
			break;
		}
		if (!(n = (ipnat_t *)KMALLOC(sizeof(*n)))) {
			error = ENOMEM;
			break;
		}
		IRCOPY((char *)data, (char *)n, sizeof(*n));
		n->in_ifp = (void *)GETUNIT(n->in_ifname);
		n->in_next = *np;
		n->in_use = 0;
		n->in_space = ~(0xffffffff & ntohl(n->in_outmsk));
		if (n->in_space) /* lose 2: broadcast + network address */
			n->in_space -= 2;
		else
			n->in_space = 1;	/* single IP# mapping */
		if (n->in_outmsk != 0xffffffff)
			n->in_nip = ntohl(n->in_outip) + 1;
		else
			n->in_nip = ntohl(n->in_outip);
		if (n->in_redir == NAT_MAP) {
			n->in_pnext = ntohs(n->in_pmin);
			/*
			 * Multiply by the number of ports made available.
			 */
			if (ntohs(n->in_pmax) > ntohs(n->in_pmin))
				n->in_space *= (ntohs(n->in_pmax) -
						ntohs(n->in_pmin));
		}
		/* Otherwise, these fields are preset */
		*np = n;
		break;
	case SIOCRMNAT :
		if (!(mode & FWRITE)) {
			error = EPERM;
			break;
		}
		if (!n) {
			error = ESRCH;
			break;
		}
		*np = n->in_next;

		KFREE(n);
		nattable_sync();
		break;
	case SIOCGNATS :
		nat_stats.ns_table[0] = nat_table[0];
		nat_stats.ns_table[1] = nat_table[1];
		nat_stats.ns_list = nat_list;
		nat_stats.ns_inuse = nat_inuse;
		IWCOPY((char *)&nat_stats, (char *)data, sizeof(nat_stats));
		break;
	case SIOCGNATL :
	    {
		natlookup_t nl;

		IRCOPY((char *)data, (char *)&nl, sizeof(nl));

		if (nat_lookupredir(&nl))
			IWCOPY((char *)&nl, (char *)data, sizeof(nl));
		else
			error = ESRCH;
		break;
	    }
	case SIOCFLNAT :
		if (!(mode & FWRITE)) {
			error = EPERM;
			break;
		}
		ret = flush_nattable();
		IWCOPY((caddr_t)&ret, data, sizeof(ret));
		break;
	case SIOCCNATL :
		if (!(mode & FWRITE)) {
			error = EPERM;
			break;
		}
		ret = clear_natlist();
		IWCOPY((caddr_t)&ret, data, sizeof(ret));
		break;
	}
	MUTEX_EXIT(&ipf_nat);
	return error;
}


static void
nat_delete(natd)
	struct nat *natd;
{
	register struct nat **natp, *nat;

	for (natp = natd->nat_hstart[0]; (nat = *natp);
	     natp = &nat->nat_hnext[0])
		if (nat == natd) {
			*natp = nat->nat_hnext[0];
			break;
		}

	for (natp = natd->nat_hstart[1]; (nat = *natp);
	     natp = &nat->nat_hnext[1])
		if (nat == natd) {
			*natp = nat->nat_hnext[1];
			break;
		}

	if (natd->nat_ptr) {
		natd->nat_ptr->in_space++;
		natd->nat_ptr->in_use--;
	}
	KFREE(natd);
	nat_inuse--;
}


/*
 * flush_nattable - clear the NAT table of all mapping entries.
 */
static int
flush_nattable()
{
	register nat_t *nat, **natp;
	register int j = 0;
  
	/*
	 * Everything will be deleted, so lets just make it the deletions
	 * quicker.
	 */
	bzero((char *)nat_table[0], sizeof(nat_table[0]));
	bzero((char *)nat_table[1], sizeof(nat_table[1]));

	for (natp = &nat_instances; (nat = *natp); ) {
		*natp = nat->nat_next;
		nat_delete(nat);
		j++;
	}

	return j;
}


/*
 * I know this is O(N*M), but it can't be avoided.
 */
static void
nattable_sync()
{
	register nat_t *nat;
	register ipnat_t *np;
	int i;

	for (i = NAT_SIZE - 1; i >= 0; i--)
		for (nat = nat_instances; nat; nat = nat->nat_next) {
			for (np = nat_list; np; np = np->in_next)
				if (nat->nat_ptr == np)
					break;
			/*
			 * XXX - is it better to remove this if ? works the
			 * same if it is just "nat->nat_ptr = np".
			 */
			if (!np)
				nat->nat_ptr = NULL;
		}
}


/*
 * clear_natlist - delete all entries in the active NAT mapping list.
 */
static int
clear_natlist()
{
	register ipnat_t *n, **np;
	int i = 0;

	for (np = &nat_list; (n = *np); i++) {
		*np = n->in_next;
		KFREE(n);
	}

	nattable_sync();
	return i;
}


/*
 * Create a new NAT table entry.
 */
nat_t *
nat_new(np, ip, fin, flags, direction)
	ipnat_t *np;
	ip_t *ip;
	fr_info_t *fin;
	u_short flags;
	int direction;
{
	register u_long sum1, sum2, sumd;
	u_short port = 0, sport = 0, dport = 0, nport = 0;
	struct in_addr in;
	tcphdr_t *tcp = NULL;
	nat_t *nat, **natp;
	u_short nflags;

	nflags = flags & np->in_flags;
	if (flags & IPN_TCPUDP) {
		tcp = (tcphdr_t *)fin->fin_dp;
		sport = tcp->th_sport;
		dport = tcp->th_dport;
	}

	/* Give me a new nat */
	if (!(nat = (nat_t *)KMALLOC(sizeof(*nat))))
		return NULL;

	bzero((char *)nat, sizeof(*nat));

	/*
	 * Search the current table for a match.
	 */
	if (direction == NAT_OUTBOUND) {
		/*
		 * If it's an outbound packet which doesn't match any existing
		 * record, then create a new port
		 */
		do {
			port = 0;
			in.s_addr = np->in_nip;
			if (nflags & IPN_TCPUDP) {
				port = htons(np->in_pnext++);
				if (np->in_pnext >= ntohs(np->in_pmax)) {
					np->in_pnext = ntohs(np->in_pmin);
					np->in_space--;
					if (np->in_outmsk != 0xffffffff)
						np->in_nip++;
				}
			} else if (np->in_outmsk != 0xffffffff) {
				np->in_space--;
				np->in_nip++;
			}

			if (!port && (flags & IPN_TCPUDP))
				port = sport;
			if ((np->in_nip & ntohl(np->in_outmsk)) >
			    ntohl(np->in_outip))
				np->in_nip = ntohl(np->in_outip) + 1;
		} while (nat_inlookup(flags, ip->ip_dst, dport, in, port));

		/* Setup the NAT table */
		nat->nat_inip = ip->ip_src;
		nat->nat_outip.s_addr = htonl(in.s_addr);
		nat->nat_oip = ip->ip_dst;

		sum1 = (ntohl(ip->ip_src.s_addr) & 0xffff) +
			(ntohl(ip->ip_src.s_addr) >> 16) + ntohs(sport);

		sum2 = (in.s_addr & 0xffff) + (in.s_addr >> 16) + ntohs(port);

		if (flags & IPN_TCPUDP) {
			nat->nat_inport = sport;
			nat->nat_outport = port;
			nat->nat_oport = dport;
		}
	} else {

		/*
		 * Otherwise, it's an inbound packet. Most likely, we don't
		 * want to rewrite source ports and source addresses. Instead,
		 * we want to rewrite to a fixed internal address and fixed
		 * internal port.
		 */
		in.s_addr = ntohl(np->in_inip);
		if (!(nport = np->in_pnext))
			nport = dport;

		nat->nat_inip.s_addr = htonl(in.s_addr);
		nat->nat_outip = ip->ip_dst;
		nat->nat_oip = ip->ip_src;

		sum1 = (ntohl(ip->ip_dst.s_addr) & 0xffff) +
			(ntohl(ip->ip_dst.s_addr) >> 16) + ntohs(dport);

		sum2 = (in.s_addr & 0xffff) + (in.s_addr >> 16) + ntohs(nport);

		if (flags & IPN_TCPUDP) {
			nat->nat_inport = nport;
			nat->nat_outport = dport;
			nat->nat_oport = sport;
		}
	}

	/* Do it twice */
	sum1 = (sum1 & 0xffff) + (sum1 >> 16);
	sum1 = (sum1 & 0xffff) + (sum1 >> 16);

	/* Do it twice */
	sum2 = (sum2 & 0xffff) + (sum2 >> 16);
	sum2 = (sum2 & 0xffff) + (sum2 >> 16);

	if (sum1 > sum2)
		sum2--; /* Because ~1 == -2, We really need ~1 == -1 */
	sumd = sum2 - sum1;
	sumd = (sumd & 0xffff) + (sumd >> 16);
	nat->nat_sumd = (sumd & 0xffff) + (sumd >> 16);

	if ((flags & IPN_TCPUDP) && ((sport != port) || (dport != nport))) {
		if (direction == NAT_OUTBOUND)
			sum1 = (ntohl(ip->ip_src.s_addr) & 0xffff) +
				(ntohl(ip->ip_src.s_addr) >> 16);
		else
			sum1 = (ntohl(ip->ip_dst.s_addr) & 0xffff) +
				(ntohl(ip->ip_dst.s_addr) >> 16);

		sum2 = (in.s_addr & 0xffff) + (in.s_addr >> 16);

		/* Do it twice */
		sum1 = (sum1 & 0xffff) + (sum1 >> 16);
		sum1 = (sum1 & 0xffff) + (sum1 >> 16);

		/* Do it twice */
		sum2 = (sum2 & 0xffff) + (sum2 >> 16);
		sum2 = (sum2 & 0xffff) + (sum2 >> 16);

		if (sum1 > sum2)
			sum2--; /* Because ~1 == -2, We really need ~1 == -1 */
		sumd = sum2 - sum1;
		sumd = (sumd & 0xffff) + (sumd >> 16);
		nat->nat_ipsumd = (sumd & 0xffff) + (sumd >> 16);
	} else
		nat->nat_ipsumd = nat->nat_sumd;

	in.s_addr = htonl(in.s_addr);
	nat->nat_next = nat_instances;
	nat_instances = nat;
	natp = &nat_table[0][nat->nat_inip.s_addr % NAT_SIZE];
	nat->nat_hstart[0] = natp;
	nat->nat_hnext[0] = *natp;
	*natp = nat;
	natp = &nat_table[1][nat->nat_outip.s_addr % NAT_SIZE];
	nat->nat_hstart[1] = natp;
	nat->nat_hnext[1] = *natp;
	*natp = nat;
	nat->nat_ptr = np;
	np->in_use++;
	if (direction == NAT_OUTBOUND) {
		if (flags & IPN_TCPUDP)
			tcp->th_sport = htons(port);
	} else {
		if (flags & IPN_TCPUDP)
			tcp->th_dport = htons(nport);
	}
	nat_stats.ns_added++;
	nat_inuse++;
	return nat;
}


/*
 * NB: these lookups don't lock access to the list, it assume it has already
 * been done!
 */
/*
 * Lookup a nat entry based on the mapped destination ip address/port and
 * real source address/port.  We use this lookup when receiving a packet,
 * we're looking for a table entry, based on the destination address.
 * NOTE: THE PACKET BEING CHECKED (IF FOUND) HAS A MAPPING ALREADY.
 */
nat_t *
nat_inlookup(flags, src, sport, mapdst, mapdport)
	register int flags;
	struct in_addr src, mapdst;
	u_short sport, mapdport;
{
	register nat_t *nat;

	flags &= IPN_TCPUDP;

	nat = nat_table[1][mapdst.s_addr % NAT_SIZE];
	for (; nat; nat = nat->nat_hnext[1])
		if (nat->nat_oip.s_addr == src.s_addr &&
		    nat->nat_outip.s_addr == mapdst.s_addr &&
		    (!flags || (nat->nat_oport == sport &&
		     nat->nat_outport == mapdport)))
			return nat;
	return NULL;
}


/*
 * Lookup a nat entry based on the source 'real' ip address/port and
 * destination address/port.  We use this lookup when sending a packet out,
 * we're looking for a table entry, based on the source address.
 * NOTE: THE PACKET BEING CHECKED (IF FOUND) HAS A MAPPING ALREADY.
 */
nat_t *
nat_outlookup(flags, src, sport, dst, dport)
	register int flags;
	struct in_addr src, dst;
	u_short sport, dport;
{
	register nat_t *nat;

	flags &= IPN_TCPUDP;

	nat = nat_table[0][src.s_addr % NAT_SIZE];
	for (; nat; nat = nat->nat_hnext[0])
		if (nat->nat_inip.s_addr == src.s_addr &&
		    nat->nat_oip.s_addr == dst.s_addr &&
		    (!flags || (nat->nat_inport == sport &&
		     nat->nat_oport == dport)))
			return nat;
	return NULL;
}


/*
 * Lookup a nat entry based on the mapped source ip address/port and
 * real destination address/port.  We use this lookup when sending a packet
 * out, we're looking for a table entry, based on the source address.
 */
nat_t *
nat_lookupmapip(flags, mapsrc, mapsport, dst, dport)
	register int flags;
	struct in_addr mapsrc, dst;
	u_short mapsport, dport;
{
	register nat_t *nat;

	flags &= IPN_TCPUDP;

	nat = nat_table[1][mapsrc.s_addr % NAT_SIZE];
	for (; nat; nat = nat->nat_hnext[0])
		if (nat->nat_outip.s_addr == mapsrc.s_addr &&
		    nat->nat_oip.s_addr == dst.s_addr &&
		    (!flags || (nat->nat_outport == mapsport &&
		     nat->nat_oport == dport)))
			return nat;
	return NULL;
}


/*
 * Lookup the NAT tables to search for a matching redirect
 */
nat_t *
nat_lookupredir(np)
	register natlookup_t *np;
{
	nat_t *nat;

	/*
	 * If nl_inip is non null, this is a lookup based on the real
	 * ip address. Else, we use the fake.
	 */
	if ((nat = nat_outlookup(IPN_TCPUDP, np->nl_inip, np->nl_inport,
				 np->nl_outip, np->nl_outport))) {
		np->nl_inip = nat->nat_outip;
		np->nl_inport = nat->nat_outport;
	}
	return nat;
}


/*
 * Packets going out on the external interface go through this.
 * Here, the source address requires alteration, if anything.
 */
int
ip_natout(ip, hlen, fin)
	ip_t *ip;
	int hlen;
	fr_info_t *fin;
{
	register ipnat_t *np;
	register u_long ipa;
	tcphdr_t *tcp = NULL;
	nat_t *nat;
	u_short nflags = 0, sport = 0, dport = 0, *csump = NULL;
	struct ifnet *ifp;
	frentry_t *fr;

	if ((fr = fin->fin_fr) && !(fr->fr_flags & FR_DUP) &&
	    fr->fr_tif.fd_ifp && fr->fr_tif.fd_ifp != (void *)-1)
		ifp = fr->fr_tif.fd_ifp;
	else
		ifp = fin->fin_ifp;

	if (!(ip->ip_off & 0x1fff) && !(fin->fin_fi.fi_fl & FI_SHORT)) {
		if (ip->ip_p == IPPROTO_TCP)
			nflags = IPN_TCP;
		else if (ip->ip_p == IPPROTO_UDP)
			nflags = IPN_UDP;
		if (nflags) {
			tcp = (tcphdr_t *)fin->fin_dp;
			sport = tcp->th_sport;
			dport = tcp->th_dport;
		}
	}

	ipa = ip->ip_src.s_addr;

	MUTEX_ENTER(&ipf_nat);
	for (np = nat_list; np; np = np->in_next)
		if ((np->in_ifp == ifp) && np->in_space &&
		    (!np->in_flags || (np->in_flags & nflags)) &&
		    ((ipa & np->in_inmsk) == np->in_inip) &&
		    ((np->in_redir == NAT_MAP) ||
		     (np->in_pnext == sport))) {
			/*
			 * If there is no current entry in the nat table for
			 * this IP#, create one for it.
			 */
			if (!(nat = nat_outlookup(nflags, ip->ip_src, sport,
						  ip->ip_dst, dport))) {
				if (np->in_redir == NAT_REDIRECT)
					continue;
				/*
				 * if it's a redirection, then we don't want
				 * to create new outgoing port stuff.
				 * Redirections are only for incoming
				 * connections.
				 */
				if (!(nat = nat_new(np, ip, fin, nflags,
						    NAT_OUTBOUND)))
					break;
			}
			ip->ip_src = nat->nat_outip;

			nat->nat_age = fr_defnatage;	/* 5 mins */

			/*
			 * Fix up checksums, not by recalculating them, but
			 * simply computing adjustments.
			 */
#if SOLARIS
			if (np->in_redir == NAT_MAP)
				fix_outcksum(&ip->ip_sum, nat->nat_ipsumd);
			else
				fix_incksum(&ip->ip_sum, nat->nat_ipsumd);
#endif

			if (nflags && !(ip->ip_off & 0x1fff) &&
			    !(fin->fin_fi.fi_fl & FI_SHORT)) {

				if (nat->nat_outport)
					tcp->th_sport = nat->nat_outport;

				if (ip->ip_p == IPPROTO_TCP) {
					csump = &tcp->th_sum;
					set_tcp_age(&nat->nat_age,
						    nat->nat_state, ip, fin,1);
				} else if (ip->ip_p == IPPROTO_UDP) {
					udphdr_t *udp = (udphdr_t *)tcp;

					if (udp->uh_sum)
						csump = &udp->uh_sum;
				} else if (ip->ip_p == IPPROTO_ICMP) {
					icmphdr_t *ic = (icmphdr_t *)tcp;

					csump = &ic->icmp_cksum;
				}
				if (csump) {
					if (np->in_redir == NAT_MAP)
						fix_outcksum(csump,
							     nat->nat_sumd);
					else
						fix_incksum(csump,
							    nat->nat_sumd);
				}
			}
			nat_stats.ns_mapped[1]++;
			MUTEX_EXIT(&ipf_nat);
			return 1;
		}
	MUTEX_EXIT(&ipf_nat);
	return 0;
}


/*
 * Packets coming in from the external interface go through this.
 * Here, the destination address requires alteration, if anything.
 */
int
ip_natin(ip, hlen, fin)
	ip_t *ip;
	int hlen;
	fr_info_t *fin;
{
	register ipnat_t *np;
	register struct in_addr in;
	struct ifnet *ifp = fin->fin_ifp;
	tcphdr_t *tcp = NULL;
	u_short sport = 0, dport = 0, nflags = 0, *csump = NULL;
	nat_t *nat;

	if (!(ip->ip_off & 0x1fff) && !(fin->fin_fi.fi_fl & FI_SHORT)) {
		if (ip->ip_p == IPPROTO_TCP)
			nflags = IPN_TCP;
		else if (ip->ip_p == IPPROTO_UDP)
			nflags = IPN_UDP;
		if (nflags) {
			tcp = (tcphdr_t *)((char *)ip + hlen);
			dport = tcp->th_dport;
			sport = tcp->th_sport;
		}
	}

	in = ip->ip_dst;

	MUTEX_ENTER(&ipf_nat);
	for (np = nat_list; np; np = np->in_next)
		if ((np->in_ifp == ifp) &&
		    (!np->in_flags || (nflags & np->in_flags)) &&
		    ((in.s_addr & np->in_outmsk) == np->in_outip) &&
		    (np->in_redir == NAT_MAP || np->in_pmin == dport)) {
			if (!(nat = nat_inlookup(nflags, ip->ip_src, sport,
						 ip->ip_dst, dport))) {
				if (np->in_redir == NAT_MAP)
					continue;
				else {
					/*
					 * If this rule (np) is a redirection,
					 * rather than a mapping, then do a
					 * nat_new. Otherwise, if it's just a
					 * mapping, do a continue;
					 */
					if (!(nat = nat_new(np, ip, fin,
							    nflags,
							    NAT_INBOUND)))
						break;
				}
			}
			ip->ip_dst = nat->nat_inip;

			nat->nat_age = fr_defnatage;

			/*
			 * Fix up checksums, not by recalculating them, but
			 * simply computing adjustments.
			 */
#if SOLARIS
			if (np->in_redir == NAT_MAP)
				fix_incksum(&ip->ip_sum, nat->nat_ipsumd);
			else
				fix_outcksum(&ip->ip_sum, nat->nat_ipsumd);
#endif
			if (nflags && !(ip->ip_off & 0x1fff) &&
			    !(fin->fin_fi.fi_fl & FI_SHORT)) {

				if (nat->nat_inport)
					tcp->th_dport = nat->nat_inport;

				if (ip->ip_p == IPPROTO_TCP) {
					csump = &tcp->th_sum;
					set_tcp_age(&nat->nat_age,
						    nat->nat_state, ip, fin,0);
				} else if (ip->ip_p == IPPROTO_UDP) {
					udphdr_t *udp = (udphdr_t *)tcp;

					if (udp->uh_sum)
						csump = &udp->uh_sum;
				} else if (ip->ip_p == IPPROTO_ICMP) {
					icmphdr_t *ic = (icmphdr_t *)tcp;

					csump = &ic->icmp_cksum;
				}
				if (csump) {
					if (np->in_redir == NAT_MAP)
						fix_incksum(csump,
							    nat->nat_sumd);
					else
						fix_outcksum(csump,
							     nat->nat_sumd);
				}
			}
			nat_stats.ns_mapped[0]++;
			MUTEX_EXIT(&ipf_nat);
			return 1;
		}
	MUTEX_EXIT(&ipf_nat);
	return 0;
}


/*
 * Free all memory used by NAT structures allocated at runtime.
 */
void
ip_natunload()
{

	MUTEX_ENTER(&ipf_nat);

	(void) clear_natlist();
	(void) flush_nattable();

	MUTEX_EXIT(&ipf_nat);
}


/*
 * Slowly expire held state for NAT entries.  Timeouts are set in
 * expectation of this being called twice per second.
 */
void
ip_natexpire()
{
	register struct nat *nat, **natp;
	int s;

	MUTEX_ENTER(&ipf_nat);
	SPLNET(s);
	for (natp = &nat_instances; (nat = *natp); ) {
		if (--nat->nat_age) {
			natp = &nat->nat_next;
			continue;
		}
		*natp = nat->nat_next;
		nat_delete(nat);
		nat_stats.ns_expire++;
	}
	SPLX(s);
	MUTEX_EXIT(&ipf_nat);
}
