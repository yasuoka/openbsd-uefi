/*	$OpenBSD: ip_state.c,v 1.7 1997/02/11 22:23:28 kstailey Exp $	*/
/*
 * (C)opyright 1995 by Darren Reed.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and due credit is given
 * to the original author and the contributors.
 */
#if 0
#if !defined(lint) && defined(LIBC_SCCS)
static	char	sccsid[] = "@(#)ip_state.c	1.8 6/5/96 (C) 1993-1995 Darren Reed";
static	char	rcsid[] = "Id: ip_state.c,v 2.0.1.2 1997/01/09 15:22:45 darrenr Exp ";
#endif
#endif

#if !defined(_KERNEL) && !defined(KERNEL)
# include <stdlib.h>
# include <string.h>
#endif
#include <sys/errno.h>
#include <sys/types.h>
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
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/udp.h>
#include <netinet/tcpip.h>
#include <netinet/ip_icmp.h>
#include "ip_fil_compat.h"
#include "ip_fil.h"
#include "ip_state.h"
#ifndef	MIN
#define	MIN(a,b)	(((a)<(b))?(a):(b))
#endif

void set_tcp_age __P((int *, u_char *, ip_t *, fr_info_t *, int));
#ifndef _KERNEL
int fr_tcpstate __P((register ipstate_t *, fr_info_t *, ip_t *, tcphdr_t *,
		     u_short, ipstate_t **));
#else
int fr_tcpstate __P((register ipstate_t *,  fr_info_t *, ip_t *, tcphdr_t *,
		     u_short));
#endif

#define	TCP_CLOSE	(TH_FIN|TH_RST)

ipstate_t *ips_table[IPSTATE_SIZE];
int	ips_num = 0;
ips_stat_t ips_stats;
#if	SOLARIS
extern	kmutex_t	ipf_state;
# if	!defined(_KERNEL)
#define	bcopy(a,b,c)	memmove(b,a,c)
# endif
#endif


#define	FIVE_DAYS	(2 * 5 * 86400)	/* 5 days: half closed session */

u_long	fr_tcpidletimeout = FIVE_DAYS,
	fr_tcpclosewait = 60,
	fr_tcplastack = 20,
	fr_tcptimeout = 120,
	fr_tcpclosed = 1,
	fr_udptimeout = 120,
	fr_icmptimeout = 120;


ips_stat_t *
fr_statetstats()
{
	ips_stats.iss_active = ips_num;
	ips_stats.iss_table = ips_table;
	return &ips_stats;
}


#define	PAIRS(s1,d1,s2,d2)	((((s1) == (s2)) && ((d1) == (d2))) ||\
				 (((s1) == (d2)) && ((d1) == (s2))))
#define	IPPAIR(s1,d1,s2,d2)	PAIRS((s1).s_addr, (d1).s_addr, \
				      (s2).s_addr, (d2).s_addr)

/*
 * Create a new ipstate structure and hang it off the hash table.
 */
int
fr_addstate(ip, fin, pass)
	ip_t *ip;
	fr_info_t *fin;
	u_int pass;
{
	ipstate_t ips;
	register ipstate_t *is = &ips;
	register u_int hv;

	if ((ip->ip_off & 0x1fff) || (fin->fin_fi.fi_fl & FI_SHORT))
		return -1;
	if (ips_num == IPSTATE_MAX) {
		ips_stats.iss_max++;
		return -1;
	}
	ips.is_age = 1;
	ips.is_state[0] = 0;
	ips.is_state[1] = 0;
	/*
	 * Copy and calculate...
	 */
	hv = (is->is_p = ip->ip_p);
	hv += (is->is_src.s_addr = ip->ip_src.s_addr);
	hv += (is->is_dst.s_addr = ip->ip_dst.s_addr);

	switch (ip->ip_p)
	{
	case IPPROTO_ICMP :
	    {
		struct icmp *ic = (struct icmp *)fin->fin_dp;

		switch (ic->icmp_type)
		{
		case ICMP_ECHO :
			is->is_icmp.ics_type = 0;
			hv += (is->is_icmp.ics_id = ic->icmp_id);
			hv += (is->is_icmp.ics_seq = ic->icmp_seq);
			break;
		case ICMP_TSTAMP :
		case ICMP_IREQ :
		case ICMP_MASKREQ :
			is->is_icmp.ics_type = ic->icmp_type + 1;
			break;
		default :
			return -1;
		}
		ips_stats.iss_icmp++;
		is->is_age = fr_icmptimeout;
		break;
	    }
	case IPPROTO_TCP :
	    {
		register tcphdr_t *tcp = (tcphdr_t *)fin->fin_dp;

		/*
		 * The endian of the ports doesn't matter, but the ack and
		 * sequence numbers do as we do mathematics on them later.
		 */
		hv += (is->is_dport = tcp->th_dport);
		hv += (is->is_sport = tcp->th_sport);
		is->is_seq = ntohl(tcp->th_seq);
		is->is_ack = ntohl(tcp->th_ack);
		is->is_swin = ntohs(tcp->th_win);
		is->is_dwin = is->is_swin;	/* start them the same */
		ips_stats.iss_tcp++;
		/*
		 * If we're creating state for a starting connectoin, start the
		 * timer on it as we'll never see an error if it fails to
		 * connect.
		 */
		if ((tcp->th_flags & (TH_SYN|TH_ACK)) == TH_SYN)
			is->is_ack = 0;	/* Trumpet WinSock 'ism */
		set_tcp_age(&is->is_age, is->is_state, ip, fin,
			    tcp->th_sport == is->is_sport);
		break;
	    }
	case IPPROTO_UDP :
	    {
		register tcphdr_t *tcp = (tcphdr_t *)fin->fin_dp;

		hv += (is->is_dport = tcp->th_dport);
		hv += (is->is_sport = tcp->th_sport);
		ips_stats.iss_udp++;
		is->is_age = fr_udptimeout;
		break;
	    }
	default :
		return -1;
	}

	if (!(is = (ipstate_t *)KMALLOC(sizeof(*is)))) {
		ips_stats.iss_nomem++;
		return -1;
	}
	bcopy((char *)&ips, (char *)is, sizeof(*is));
	hv %= IPSTATE_SIZE;
	MUTEX_ENTER(&ipf_state);
	is->is_next = ips_table[hv];
	ips_table[hv] = is;
	is->is_pass = pass;
	if (pass & FR_LOGFIRST)
		is->is_pass &= ~(FR_LOGFIRST|FR_LOG);
	ips_num++;
	MUTEX_EXIT(&ipf_state);
	return 0;
}


/*
 * check to see if a packet with TCP headers fits within the TCP window.
 * change timeout depending on whether new packet is a SYN-ACK returning for a
 * SYN or a RST or FIN which indicate time to close up shop.
 */
int
fr_tcpstate(is, fin, ip, tcp, sport
#ifndef	_KERNEL
     ,isp)
	ipstate_t **isp;
#else
	)
#endif
	register ipstate_t *is;
	fr_info_t *fin;
	ip_t *ip;
	tcphdr_t *tcp;
	u_short sport;
{
	register int seqskew, ackskew;
	register u_short swin, dwin;
	register tcp_seq seq, ack;
	int source;

	/*
	 * Find difference between last checked packet and this packet.
	 */
	seq = ntohl(tcp->th_seq);
	ack = ntohl(tcp->th_ack);
	if (sport == is->is_sport) {
		seqskew = seq - is->is_seq;
		ackskew = ack - is->is_ack;
	} else {
		seqskew = ack - is->is_seq;
		if (!is->is_ack)
			/*
			 * Must be a SYN-ACK in reply to a SYN.
			 */
			is->is_ack = seq;
		ackskew = seq - is->is_ack;
	}

	/*
	 * Make skew values absolute
	 */
	if (seqskew < 0)
		seqskew = -seqskew;
	if (ackskew < 0)
		ackskew = -ackskew;

	/*
	 * If the difference in sequence and ack numbers is within the
	 * window size of the connection, store these values and match
	 * the packet.
	 */
	if ((source = (sport == is->is_sport))) {
		swin = is->is_swin;
		dwin = is->is_dwin;
	} else {
		dwin = is->is_swin;
		swin = is->is_dwin;
	}

	if ((seqskew <= swin) && (ackskew <= dwin)) {
		if (source) {
			is->is_seq = seq;
			is->is_ack = ack;
			is->is_swin = ntohs(tcp->th_win);
		} else {
			is->is_seq = ack;
			is->is_ack = seq;
			is->is_dwin = ntohs(tcp->th_win);
		}
		ips_stats.iss_hits++;
		/*
		 * Nearing end of connection, start timeout.
		 */
		set_tcp_age(&is->is_age, is->is_state, ip, fin,
			    tcp->th_sport == is->is_sport);
		return 1;
	}
	return 0;
}


/*
 * Check if a packet has a registered state.
 */
int
fr_checkstate(ip, fin)
	ip_t *ip;
	fr_info_t *fin;
{
	register struct in_addr dst, src;
	register ipstate_t *is, **isp;
	register u_char pr;
	struct icmp *ic;
	tcphdr_t *tcp;
	u_int hv, hlen;

	if ((ip->ip_off & 0x1fff) || (fin->fin_fi.fi_fl & FI_SHORT))
		return 0;

	hlen = fin->fin_hlen;
	tcp = (tcphdr_t *)((char *)ip + hlen);
	ic = (struct icmp *)tcp;
	hv = (pr = ip->ip_p);
	hv += (src.s_addr = ip->ip_src.s_addr);
	hv += (dst.s_addr = ip->ip_dst.s_addr);

	/*
	 * Search the hash table for matching packet header info.
	 */
	switch (ip->ip_p)
	{
	case IPPROTO_ICMP :
		hv += ic->icmp_id;
		hv += ic->icmp_seq;
		hv %= IPSTATE_SIZE;
		MUTEX_ENTER(&ipf_state);
		for (isp = &ips_table[hv]; (is = *isp); isp = &is->is_next)
			if ((is->is_p == pr) &&
			    (ic->icmp_id == is->is_icmp.ics_id) &&
			    (ic->icmp_seq == is->is_icmp.ics_seq) &&
			    IPPAIR(src, dst, is->is_src, is->is_dst)) {
				/*
				 * If we have type 0 stored, allow any icmp
				 * replies through.
				 */
				if (is->is_icmp.ics_type &&
				    is->is_icmp.ics_type != ic->icmp_type)
					continue;
				is->is_age = fr_icmptimeout;
				ips_stats.iss_hits++;
				MUTEX_EXIT(&ipf_state);
				return is->is_pass;
			}
		MUTEX_EXIT(&ipf_state);
		break;
	case IPPROTO_TCP :
	    {
		register u_short dport = tcp->th_dport, sport = tcp->th_sport;

		hv += dport;
		hv += sport;
		hv %= IPSTATE_SIZE;
		MUTEX_ENTER(&ipf_state);
		for (isp = &ips_table[hv]; (is = *isp); isp = &is->is_next) {
			if ((is->is_p == pr) &&
			    PAIRS(sport, dport, is->is_sport, is->is_dport) &&
			    IPPAIR(src, dst, is->is_src, is->is_dst))
				if (fr_tcpstate(is, fin, ip, tcp, sport
#ifndef _KERNEL
						, NULL
#endif
						)) {
#ifdef	_KERNEL
					MUTEX_EXIT(&ipf_state);
					return is->is_pass;
#else
					int pass = is->is_pass;

					if (tcp->th_flags & TCP_CLOSE) {
						*isp = is->is_next;
						isp = &ips_table[hv];
						KFREE(is);
					}
					return pass;
#endif
				}
		}
		MUTEX_EXIT(&ipf_state);
		break;
	    }
	case IPPROTO_UDP :
	    {
		register u_short dport = tcp->th_dport, sport = tcp->th_sport;

		hv += dport;
		hv += sport;
		hv %= IPSTATE_SIZE;
		/*
		 * Nothing else to match on but ports. and IP#'s
		 */
		MUTEX_ENTER(&ipf_state);
		for (is = ips_table[hv]; is; is = is->is_next)
			if ((is->is_p == pr) &&
			    PAIRS(sport, dport, is->is_sport, is->is_dport) &&
			    IPPAIR(src, dst, is->is_src, is->is_dst)) {
				ips_stats.iss_hits++;
				is->is_age = fr_udptimeout;
				MUTEX_EXIT(&ipf_state);
				return is->is_pass;
			}
		MUTEX_EXIT(&ipf_state);
		break;
	    }
	default :
		break;
	}
	ips_stats.iss_miss++;
	return 0;
}


/*
 * Free memory in use by all state info. kept.
 */
void
fr_stateunload()
{
	register int i;
	register ipstate_t *is, **isp;

	MUTEX_ENTER(&ipf_state);
	for (i = 0; i < IPSTATE_SIZE; i++)
		for (isp = &ips_table[i]; (is = *isp); ) {
			*isp = is->is_next;
			KFREE(is);
		}
	MUTEX_EXIT(&ipf_state);
}


/*
 * Slowly expire held state for things like UDP and ICMP.  Timeouts are set
 * in expectation of this being called twice per second.
 */
void
fr_timeoutstate()
{
	register int i;
	register ipstate_t *is, **isp;

	MUTEX_ENTER(&ipf_state);
	for (i = 0; i < IPSTATE_SIZE; i++)
		for (isp = &ips_table[i]; (is = *isp); )
			if (is->is_age && !--is->is_age) {
				*isp = is->is_next;
				if (is->is_p == IPPROTO_TCP)
					ips_stats.iss_fin++;
				else
					ips_stats.iss_expire++;
				KFREE(is);
				ips_num--;
			} else
				isp = &is->is_next;
	MUTEX_EXIT(&ipf_state);
}


/*
 * Original idea freom Pradeep Krishnan for use primarily with NAT code.
 * (pkrishna@netcom.com)
 */
void
set_tcp_age(age, state, ip, fin, dir)
	int *age;
	u_char *state;
	ip_t *ip;
	fr_info_t *fin;
	int dir;
{
	tcphdr_t *tcp = (tcphdr_t *)fin->fin_dp;
	u_char flags = tcp->th_flags;
	int dlen, ostate;

	ostate = state[1 - dir];

	dlen = ip->ip_len - fin->fin_hlen - (tcp->th_off << 2);

	if (flags & TH_RST) {
		if (!(tcp->th_flags & TH_PUSH) && !dlen) {
			*age = fr_tcpclosed;
			state[dir] = TCPS_CLOSED;
		} else {
			*age = fr_tcpclosewait;
			state[dir] = TCPS_CLOSE_WAIT;
		}
		return;
	}

	*age = fr_tcptimeout; /* 1 min */

	switch(state[dir])
	{
	case TCPS_FIN_WAIT_2:
	case TCPS_CLOSED:
		if ((flags & TH_OPENING) == TH_OPENING)
			state[dir] = TCPS_SYN_RECEIVED;
		else if (flags & TH_SYN)
			state[dir] = TCPS_SYN_SENT;
		break;
	case TCPS_SYN_RECEIVED:
		if ((flags & (TH_FIN|TH_ACK)) == TH_ACK) {
			state[dir] = TCPS_ESTABLISHED;
			*age = fr_tcpidletimeout;
		}
		break;
	case TCPS_SYN_SENT:
		if ((flags & (TH_FIN|TH_ACK)) == TH_ACK) {
			state[dir] = TCPS_ESTABLISHED;
			*age = fr_tcpidletimeout;
		}
		break;
	case TCPS_ESTABLISHED:
		if (flags & TH_FIN) {
			state[dir] = TCPS_CLOSE_WAIT;
			if (!(flags & TH_PUSH) && !dlen &&
			    ostate > TCPS_ESTABLISHED)
				*age  = fr_tcplastack;
			else
				*age  = fr_tcpclosewait;
		} else
			*age = fr_tcpidletimeout;
		break;
	case TCPS_CLOSE_WAIT:
		if ((flags & TH_FIN) && !(flags & TH_PUSH) && !dlen &&
		    ostate > TCPS_ESTABLISHED) {
			*age  = fr_tcplastack;
			state[dir] = TCPS_LAST_ACK;
		} else
			*age  = fr_tcpclosewait;
		break;
	case TCPS_LAST_ACK:
		if (flags & TH_ACK) {
			state[dir] = TCPS_FIN_WAIT_2;
			if (!(flags & TH_PUSH) && !dlen &&
			    ostate > TCPS_ESTABLISHED)
				*age  = fr_tcplastack;
			else {
				*age  = fr_tcpclosewait;
				state[dir] = TCPS_CLOSE_WAIT;
			}
		}
		break;
	}
}
