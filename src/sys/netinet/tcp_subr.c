/*	$OpenBSD: tcp_subr.c,v 1.77 2004/03/02 12:51:12 markus Exp $	*/
/*	$NetBSD: tcp_subr.c,v 1.22 1996/02/13 23:44:00 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
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
#include <sys/proc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/kernel.h>

#include <net/route.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#include <dev/rndvar.h>

#ifdef INET6
#include <netinet6/in6_var.h>
#include <netinet6/ip6protosw.h>
#endif /* INET6 */

#ifdef TCP_SIGNATURE
#include <sys/md5k.h>
#endif /* TCP_SIGNATURE */

/* patchable/settable parameters for tcp */
int	tcp_mssdflt = TCP_MSS;
int	tcp_rttdflt = TCPTV_SRTTDFLT / PR_SLOWHZ;

/*
 * Configure kernel with options "TCP_DO_RFC1323=0" to disable RFC1323 stuff.
 * This is a good idea over slow SLIP/PPP links, because the timestamp
 * pretty well destroys the VJ compression (any packet with a timestamp
 * different from the previous one can't be compressed), as well as adding
 * more overhead.
 * XXX And it should be a settable per route characteristic (with this just
 * used as the default).
 */
#ifndef TCP_DO_RFC1323
#define TCP_DO_RFC1323	1
#endif
int	tcp_do_rfc1323 = TCP_DO_RFC1323;

#ifndef TCP_DO_SACK
#ifdef TCP_SACK
#define TCP_DO_SACK	1
#else
#define TCP_DO_SACK	0
#endif
#endif
int	tcp_do_sack = TCP_DO_SACK;		/* RFC 2018 selective ACKs */
int	tcp_ack_on_push = 0;	/* set to enable immediate ACK-on-PUSH */
int	tcp_do_ecn = 0;		/* RFC3168 ECN enabled/disabled? */
int	tcp_do_rfc3390 = 0;	/* RFC3390 Increasing TCP's Initial Window */

u_int32_t	tcp_now;

#ifndef TCBHASHSIZE
#define	TCBHASHSIZE	128
#endif
int	tcbhashsize = TCBHASHSIZE;

/* syn hash parameters */
#define	TCP_SYN_HASH_SIZE	293
#define	TCP_SYN_BUCKET_SIZE	35
int	tcp_syn_cache_size = TCP_SYN_HASH_SIZE;
int	tcp_syn_cache_limit = TCP_SYN_HASH_SIZE*TCP_SYN_BUCKET_SIZE;
int	tcp_syn_bucket_limit = 3*TCP_SYN_BUCKET_SIZE;
struct	syn_cache_head tcp_syn_cache[TCP_SYN_HASH_SIZE];

int tcp_reass_limit = NMBCLUSTERS / 2; /* hardlimit for tcpqe_pool */

#ifdef INET6
extern int ip6_defhlim;
#endif /* INET6 */

struct pool tcpcb_pool;
struct pool tcpqe_pool;
#ifdef TCP_SACK
struct pool sackhl_pool;
#endif

struct tcpstat tcpstat;		/* tcp statistics */
tcp_seq  tcp_iss;

/*
 * Tcp initialization
 */
void
tcp_init()
{
#ifdef TCP_COMPAT_42
	tcp_iss = 1;		/* wrong */
#endif /* TCP_COMPAT_42 */
	pool_init(&tcpcb_pool, sizeof(struct tcpcb), 0, 0, 0, "tcpcbpl",
	    NULL);
	pool_init(&tcpqe_pool, sizeof(struct ipqent), 0, 0, 0, "tcpqepl",
	    NULL);
	pool_sethardlimit(&tcpqe_pool, tcp_reass_limit, NULL, 0);
#ifdef TCP_SACK
	pool_init(&sackhl_pool, sizeof(struct sackhole), 0, 0, 0, "sackhlpl",
	    NULL);
#endif /* TCP_SACK */
	in_pcbinit(&tcbtable, tcbhashsize);
	tcp_now = arc4random() / 2;

#ifdef INET6
	/*
	 * Since sizeof(struct ip6_hdr) > sizeof(struct ip), we
	 * do max length checks/computations only on the former.
	 */
	if (max_protohdr < (sizeof(struct ip6_hdr) + sizeof(struct tcphdr)))
		max_protohdr = (sizeof(struct ip6_hdr) + sizeof(struct tcphdr));
	if ((max_linkhdr + sizeof(struct ip6_hdr) + sizeof(struct tcphdr)) >
	    MHLEN)
		panic("tcp_init");

	icmp6_mtudisc_callback_register(tcp6_mtudisc_callback);
#endif /* INET6 */

	/* Initialize the compressed state engine. */
	syn_cache_init();

	/* Initialize timer state. */
	tcp_timer_init();
}

/*
 * Create template to be used to send tcp packets on a connection.
 * Call after host entry created, allocates an mbuf and fills
 * in a skeletal tcp/ip header, minimizing the amount of work
 * necessary when the connection is used.
 *
 * To support IPv6 in addition to IPv4 and considering that the sizes of
 * the IPv4 and IPv6 headers are not the same, we now use a separate pointer
 * for the TCP header.  Also, we made the former tcpiphdr header pointer
 * into just an IP overlay pointer, with casting as appropriate for v6. rja
 */
struct mbuf *
tcp_template(tp)
	struct tcpcb *tp;
{
	struct inpcb *inp = tp->t_inpcb;
	struct mbuf *m;
	struct tcphdr *th;

	if ((m = tp->t_template) == 0) {
		m = m_get(M_DONTWAIT, MT_HEADER);
		if (m == NULL)
			return (0);

		switch (tp->pf) {
		case 0:	/*default to PF_INET*/
#ifdef INET
		case AF_INET:
			m->m_len = sizeof(struct ip);
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			m->m_len = sizeof(struct ip6_hdr);
			break;
#endif /* INET6 */
		}
		m->m_len += sizeof (struct tcphdr);

		/*
		 * The link header, network header, TCP header, and TCP options
		 * all must fit in this mbuf. For now, assume the worst case of
		 * TCP options size. Eventually, compute this from tp flags.
		 */
		if (m->m_len + MAX_TCPOPTLEN + max_linkhdr >= MHLEN) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_free(m);
				return (0);
			}
		}
	}

	switch(tp->pf) {
#ifdef INET
	case AF_INET:
		{
			struct ipovly *ipovly;

			ipovly = mtod(m, struct ipovly *);

			bzero(ipovly->ih_x1, sizeof ipovly->ih_x1);
			ipovly->ih_pr = IPPROTO_TCP;
			ipovly->ih_len = htons(sizeof (struct tcphdr));
			ipovly->ih_src = inp->inp_laddr;
			ipovly->ih_dst = inp->inp_faddr;

			th = (struct tcphdr *)(mtod(m, caddr_t) +
				sizeof(struct ip));
			th->th_sum = in_cksum_phdr(ipovly->ih_src.s_addr,
			    ipovly->ih_dst.s_addr,
			    htons(sizeof (struct tcphdr) + IPPROTO_TCP));
		}
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		{
			struct ip6_hdr *ip6;

			ip6 = mtod(m, struct ip6_hdr *);

			ip6->ip6_src = inp->inp_laddr6;
			ip6->ip6_dst = inp->inp_faddr6;
			ip6->ip6_flow = htonl(0x60000000) |
			    (inp->inp_flowinfo & IPV6_FLOWLABEL_MASK);

			ip6->ip6_nxt = IPPROTO_TCP;
			ip6->ip6_plen = htons(sizeof(struct tcphdr)); /*XXX*/
			ip6->ip6_hlim = in6_selecthlim(inp, NULL);	/*XXX*/

			th = (struct tcphdr *)(mtod(m, caddr_t) +
				sizeof(struct ip6_hdr));
			th->th_sum = 0;
		}
		break;
#endif /* INET6 */
	}

	th->th_sport = inp->inp_lport;
	th->th_dport = inp->inp_fport;
	th->th_seq = 0;
	th->th_ack = 0;
	th->th_x2  = 0;
	th->th_off = 5;
	th->th_flags = 0;
	th->th_win = 0;
	th->th_urp = 0;
	return (m);
}

/*
 * Send a single message to the TCP at address specified by
 * the given TCP/IP header.  If m == 0, then we make a copy
 * of the tcpiphdr at ti and send directly to the addressed host.
 * This is used to force keep alive messages out using the TCP
 * template for a connection tp->t_template.  If flags are given
 * then we send a message back to the TCP which originated the
 * segment ti, and discard the mbuf containing it and any other
 * attached mbufs.
 *
 * In any case the ack and sequence number of the transmitted
 * segment are as specified by the parameters.
 */
#ifdef INET6
/* This function looks hairy, because it was so IPv4-dependent. */
#endif /* INET6 */
void
tcp_respond(tp, template, m, ack, seq, flags)
	struct tcpcb *tp;
	caddr_t template;
	struct mbuf *m;
	tcp_seq ack, seq;
	int flags;
{
	int tlen;
	int win = 0;
	struct route *ro = 0;
	struct tcphdr *th;
	struct tcpiphdr *ti = (struct tcpiphdr *)template;
	int af;		/* af on wire */

	if (tp) {
		win = sbspace(&tp->t_inpcb->inp_socket->so_rcv);
		/*
		 * If this is called with an unconnected
		 * socket/tp/pcb (tp->pf is 0), we lose.
		 */
		af = tp->pf;

		/*
		 * The route/route6 distinction is meaningless
		 * unless you're allocating space or passing parameters.
		 */
		ro = &tp->t_inpcb->inp_route;
	} else
		af = (((struct ip *)ti)->ip_v == 6) ? AF_INET6 : AF_INET;
	if (m == 0) {
		m = m_gethdr(M_DONTWAIT, MT_HEADER);
		if (m == NULL)
			return;
#ifdef TCP_COMPAT_42
		tlen = 1;
#else
		tlen = 0;
#endif
		m->m_data += max_linkhdr;
		switch (af) {
#ifdef INET6
		case AF_INET6:
			bcopy(ti, mtod(m, caddr_t), sizeof(struct tcphdr) +
			    sizeof(struct ip6_hdr));
			break;
#endif /* INET6 */
		case AF_INET:
			bcopy(ti, mtod(m, caddr_t), sizeof(struct tcphdr) +
			    sizeof(struct ip));
			break;
		}

		ti = mtod(m, struct tcpiphdr *);
		flags = TH_ACK;
	} else {
		m_freem(m->m_next);
		m->m_next = 0;
		m->m_data = (caddr_t)ti;
		tlen = 0;
#define xchg(a,b,type) do { type t; t=a; a=b; b=t; } while (0)
		switch (af) {
#ifdef INET6
		case AF_INET6:
			m->m_len = sizeof(struct tcphdr) + sizeof(struct ip6_hdr);
			xchg(((struct ip6_hdr *)ti)->ip6_dst,
			    ((struct ip6_hdr *)ti)->ip6_src, struct in6_addr);
			th = (void *)((caddr_t)ti + sizeof(struct ip6_hdr));
			break;
#endif /* INET6 */
		case AF_INET:
			m->m_len = sizeof (struct tcpiphdr);
			xchg(ti->ti_dst.s_addr, ti->ti_src.s_addr, u_int32_t);
			th = (void *)((caddr_t)ti + sizeof(struct ip));
			break;
		}
		xchg(th->th_dport, th->th_sport, u_int16_t);
#undef xchg
	}
	switch (af) {
#ifdef INET6
	case AF_INET6:
		tlen += sizeof(struct tcphdr) + sizeof(struct ip6_hdr);
		th = (struct tcphdr *)((caddr_t)ti + sizeof(struct ip6_hdr));
		break;
#endif /* INET6 */
	case AF_INET:
		ti->ti_len = htons((u_int16_t)(sizeof (struct tcphdr) + tlen));
		tlen += sizeof (struct tcpiphdr);
		th = (struct tcphdr *)((caddr_t)ti + sizeof(struct ip));
		break;
	}

	m->m_len = tlen;
	m->m_pkthdr.len = tlen;
	m->m_pkthdr.rcvif = (struct ifnet *) 0;
	th->th_seq = htonl(seq);
	th->th_ack = htonl(ack);
	th->th_x2 = 0;
	th->th_off = sizeof (struct tcphdr) >> 2;
	th->th_flags = flags;
	if (tp)
		win >>= tp->rcv_scale;
	if (win > TCP_MAXWIN)
		win = TCP_MAXWIN;
	th->th_win = htons((u_int16_t)win);
	th->th_urp = 0;

	switch (af) {
#ifdef INET6
	case AF_INET6:
		((struct ip6_hdr *)ti)->ip6_flow   = htonl(0x60000000);
		((struct ip6_hdr *)ti)->ip6_nxt  = IPPROTO_TCP;
		((struct ip6_hdr *)ti)->ip6_hlim =
			in6_selecthlim(tp ? tp->t_inpcb : NULL, NULL);	/*XXX*/
		((struct ip6_hdr *)ti)->ip6_plen = tlen - sizeof(struct ip6_hdr);
		th->th_sum = 0;
		th->th_sum = in6_cksum(m, IPPROTO_TCP,
		   sizeof(struct ip6_hdr), ((struct ip6_hdr *)ti)->ip6_plen);
		HTONS(((struct ip6_hdr *)ti)->ip6_plen);
		ip6_output(m, tp ? tp->t_inpcb->inp_outputopts6 : NULL,
		    (struct route_in6 *)ro, 0, NULL, NULL);
		break;
#endif /* INET6 */
	case AF_INET:
		bzero(ti->ti_x1, sizeof ti->ti_x1);
		ti->ti_len = htons((u_short)tlen - sizeof(struct ip));

		/*
		 * There's no point deferring to hardware checksum processing
		 * here, as we only send a minimal TCP packet whose checksum
		 * we need to compute in any case.
		 */
		th->th_sum = 0;
		th->th_sum = in_cksum(m, tlen);
		((struct ip *)ti)->ip_len = htons(tlen);
		((struct ip *)ti)->ip_ttl = ip_defttl;
		ip_output(m, (void *)NULL, ro, ip_mtudisc ? IP_MTUDISC : 0,
			(void *)NULL, tp ? tp->t_inpcb : (void *)NULL);
	}
}

/*
 * Create a new TCP control block, making an
 * empty reassembly queue and hooking it to the argument
 * protocol control block.
 */
struct tcpcb *
tcp_newtcpcb(struct inpcb *inp)
{
	struct tcpcb *tp;
	int i;

	tp = pool_get(&tcpcb_pool, PR_NOWAIT);
	if (tp == NULL)
		return ((struct tcpcb *)0);
	bzero((char *) tp, sizeof(struct tcpcb));
	LIST_INIT(&tp->segq);
	tp->t_maxseg = tcp_mssdflt;
	tp->t_maxopd = 0;

	TCP_INIT_DELACK(tp);
	for (i = 0; i < TCPT_NTIMERS; i++)
		TCP_TIMER_INIT(tp, i);

#ifdef TCP_SACK
	tp->sack_enable = tcp_do_sack;
#endif
	tp->t_flags = tcp_do_rfc1323 ? (TF_REQ_SCALE|TF_REQ_TSTMP) : 0;
	tp->t_inpcb = inp;
	/*
	 * Init srtt to TCPTV_SRTTBASE (0), so we can tell that we have no
	 * rtt estimate.  Set rttvar so that srtt + 2 * rttvar gives
	 * reasonable initial retransmit time.
	 */
	tp->t_srtt = TCPTV_SRTTBASE;
	tp->t_rttvar = tcp_rttdflt * PR_SLOWHZ << (TCP_RTTVAR_SHIFT + 2 - 1);
	tp->t_rttmin = TCPTV_MIN;
	TCPT_RANGESET(tp->t_rxtcur, TCP_REXMTVAL(tp),
	    TCPTV_MIN, TCPTV_REXMTMAX);
	tp->snd_cwnd = TCP_MAXWIN << TCP_MAX_WINSHIFT;
	tp->snd_ssthresh = TCP_MAXWIN << TCP_MAX_WINSHIFT;
#ifdef INET6
	/* we disallow IPv4 mapped address completely. */
	if ((inp->inp_flags & INP_IPV6) == 0)
		tp->pf = PF_INET;
	else
		tp->pf = PF_INET6;
#else
	tp->pf = PF_INET;
#endif

#ifdef INET6
	if (inp->inp_flags & INP_IPV6)
		inp->inp_ipv6.ip6_hlim = ip6_defhlim;
	else
#endif /* INET6 */
		inp->inp_ip.ip_ttl = ip_defttl;

	inp->inp_ppcb = (caddr_t)tp;
	return (tp);
}

/*
 * Drop a TCP connection, reporting
 * the specified error.  If connection is synchronized,
 * then send a RST to peer.
 */
struct tcpcb *
tcp_drop(tp, errno)
	struct tcpcb *tp;
	int errno;
{
	struct socket *so = tp->t_inpcb->inp_socket;

	if (TCPS_HAVERCVDSYN(tp->t_state)) {
		tp->t_state = TCPS_CLOSED;
		(void) tcp_output(tp);
		tcpstat.tcps_drops++;
	} else
		tcpstat.tcps_conndrops++;
	if (errno == ETIMEDOUT && tp->t_softerror)
		errno = tp->t_softerror;
	so->so_error = errno;
	return (tcp_close(tp));
}

/*
 * Close a TCP control block:
 *	discard all space held by the tcp
 *	discard internet protocol block
 *	wake up any sleepers
 */
struct tcpcb *
tcp_close(struct tcpcb *tp)
{
	struct inpcb *inp = tp->t_inpcb;
	struct socket *so = inp->inp_socket;
#ifdef TCP_SACK
	struct sackhole *p, *q;
#endif
#ifdef RTV_RTT
	struct rtentry *rt;
#ifdef INET6
	int bound_to_specific = 0;  /* I.e. non-default */

	/*
	 * This code checks the nature of the route for this connection.
	 * Normally this is done by two simple checks in the next
	 * INET/INET6 ifdef block, but because of two possible lower layers,
	 * that check is done here.
	 *
	 * Perhaps should be doing this only for a RTF_HOST route.
	 */
	rt = inp->inp_route.ro_rt;  /* Same for route or route6. */
	if (tp->pf == PF_INET6) {
		if (rt)
			bound_to_specific =
			    !(IN6_IS_ADDR_UNSPECIFIED(&
			    ((struct sockaddr_in6 *)rt_key(rt))->sin6_addr));
	} else {
		if (rt)
			bound_to_specific =
			    (((struct sockaddr_in *)rt_key(rt))->
			    sin_addr.s_addr != INADDR_ANY);
	}
#endif /* INET6 */

	/*
	 * If we sent enough data to get some meaningful characteristics,
	 * save them in the routing entry.  'Enough' is arbitrarily
	 * defined as the sendpipesize (default 4K) * 16.  This would
	 * give us 16 rtt samples assuming we only get one sample per
	 * window (the usual case on a long haul net).  16 samples is
	 * enough for the srtt filter to converge to within 5% of the correct
	 * value; fewer samples and we could save a very bogus rtt.
	 *
	 * Don't update the default route's characteristics and don't
	 * update anything that the user "locked".
	 */
#ifdef INET6
	/*
	 * Note that rt and bound_to_specific are set above.
	 */
	if (SEQ_LT(tp->iss + so->so_snd.sb_hiwat * 16, tp->snd_max) &&
	    rt && bound_to_specific) {
#else /* INET6 */
	if (SEQ_LT(tp->iss + so->so_snd.sb_hiwat * 16, tp->snd_max) &&
	    (rt = inp->inp_route.ro_rt) &&
	    satosin(rt_key(rt))->sin_addr.s_addr != INADDR_ANY) {
#endif /* INET6 */
		u_long i = 0;

		if ((rt->rt_rmx.rmx_locks & RTV_RTT) == 0) {
			i = tp->t_srtt *
			    (RTM_RTTUNIT / (PR_SLOWHZ * TCP_RTT_SCALE));
			if (rt->rt_rmx.rmx_rtt && i)
				/*
				 * filter this update to half the old & half
				 * the new values, converting scale.
				 * See route.h and tcp_var.h for a
				 * description of the scaling constants.
				 */
				rt->rt_rmx.rmx_rtt =
				    (rt->rt_rmx.rmx_rtt + i) / 2;
			else
				rt->rt_rmx.rmx_rtt = i;
		}
		if ((rt->rt_rmx.rmx_locks & RTV_RTTVAR) == 0) {
			i = tp->t_rttvar *
			    (RTM_RTTUNIT / (PR_SLOWHZ * TCP_RTTVAR_SCALE));
			if (rt->rt_rmx.rmx_rttvar && i)
				rt->rt_rmx.rmx_rttvar =
				    (rt->rt_rmx.rmx_rttvar + i) / 2;
			else
				rt->rt_rmx.rmx_rttvar = i;
		}
		/*
		 * update the pipelimit (ssthresh) if it has been updated
		 * already or if a pipesize was specified & the threshhold
		 * got below half the pipesize.  I.e., wait for bad news
		 * before we start updating, then update on both good
		 * and bad news.
		 */
		if (((rt->rt_rmx.rmx_locks & RTV_SSTHRESH) == 0 &&
		    (i = tp->snd_ssthresh) && rt->rt_rmx.rmx_ssthresh) ||
		    i < (rt->rt_rmx.rmx_sendpipe / 2)) {
			/*
			 * convert the limit from user data bytes to
			 * packets then to packet data bytes.
			 */
			i = (i + tp->t_maxseg / 2) / tp->t_maxseg;
			if (i < 2)
				i = 2;
#ifdef INET6
			if (tp->pf == PF_INET6)
				i *= (u_long)(tp->t_maxseg + sizeof (struct tcphdr)
				    + sizeof(struct ip6_hdr));
			else
#endif /* INET6 */
				i *= (u_long)(tp->t_maxseg +
				    sizeof (struct tcpiphdr));

			if (rt->rt_rmx.rmx_ssthresh)
				rt->rt_rmx.rmx_ssthresh =
				    (rt->rt_rmx.rmx_ssthresh + i) / 2;
			else
				rt->rt_rmx.rmx_ssthresh = i;
		}
	}
#endif /* RTV_RTT */

	/* free the reassembly queue, if any */
	tcp_reass_lock(tp);
	tcp_freeq(tp);
	tcp_reass_unlock(tp);

	tcp_canceltimers(tp);
	TCP_CLEAR_DELACK(tp);
	syn_cache_cleanup(tp);

#ifdef TCP_SACK
	/* Free SACK holes. */
	q = p = tp->snd_holes;
	while (p != 0) {
		q = p->next;
		pool_put(&sackhl_pool, p);
		p = q;
	}
#endif
	if (tp->t_template)
		(void) m_free(tp->t_template);
	pool_put(&tcpcb_pool, tp);
	inp->inp_ppcb = 0;
	soisdisconnected(so);
	in_pcbdetach(inp);
	tcpstat.tcps_closed++;
	return ((struct tcpcb *)0);
}

int
tcp_freeq(struct tcpcb *tp)
{
	struct ipqent *qe;
	int rv = 0;

	while ((qe = LIST_FIRST(&tp->segq)) != NULL) {
		LIST_REMOVE(qe, ipqe_q);
		m_freem(qe->ipqe_m);
		pool_put(&tcpqe_pool, qe);
		rv = 1;
	}
	return (rv);
}

void
tcp_drain()
{
	struct inpcb *inp;

	/* called at splimp() */
	CIRCLEQ_FOREACH(inp, &tcbtable.inpt_queue, inp_queue) {
		struct tcpcb *tp = (struct tcpcb *)inp->inp_ppcb;

		if (tp != NULL) {
			if (tcp_reass_lock_try(tp) == 0)
				continue;
			if (tcp_freeq(tp))
				tcpstat.tcps_conndrained++;
			tcp_reass_unlock(tp);
		}
	}
}

/*
 * Compute proper scaling value for receiver window from buffer space
 */

void
tcp_rscale(struct tcpcb *tp, u_long hiwat)
{
	tp->request_r_scale = 0;
	while (tp->request_r_scale < TCP_MAX_WINSHIFT &&
	       TCP_MAXWIN << tp->request_r_scale < hiwat)
		tp->request_r_scale++;
}

/*
 * Notify a tcp user of an asynchronous error;
 * store error as soft error, but wake up user
 * (for now, won't do anything until can select for soft error).
 */
void
tcp_notify(inp, error)
	struct inpcb *inp;
	int error;
{
	struct tcpcb *tp = (struct tcpcb *)inp->inp_ppcb;
	struct socket *so = inp->inp_socket;

	/*
	 * Ignore some errors if we are hooked up.
	 * If connection hasn't completed, has retransmitted several times,
	 * and receives a second error, give up now.  This is better
	 * than waiting a long time to establish a connection that
	 * can never complete.
	 */
	if (tp->t_state == TCPS_ESTABLISHED &&
	     (error == EHOSTUNREACH || error == ENETUNREACH ||
	      error == EHOSTDOWN)) {
		return;
	} else if (TCPS_HAVEESTABLISHED(tp->t_state) == 0 &&
	    tp->t_rxtshift > 3 && tp->t_softerror)
		so->so_error = error;
	else
		tp->t_softerror = error;
	wakeup((caddr_t) &so->so_timeo);
	sorwakeup(so);
	sowwakeup(so);
}

#ifdef INET6
void
tcp6_ctlinput(cmd, sa, d)
	int cmd;
	struct sockaddr *sa;
	void *d;
{
	struct tcphdr th;
	void (*notify)(struct inpcb *, int) = tcp_notify;
	struct ip6_hdr *ip6;
	const struct sockaddr_in6 *sa6_src = NULL;
	struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)sa;
	struct mbuf *m;
	int off;
	struct {
		u_int16_t th_sport;
		u_int16_t th_dport;
	} *thp;

	if (sa->sa_family != AF_INET6 ||
	    sa->sa_len != sizeof(struct sockaddr_in6))
		return;
	if ((unsigned)cmd >= PRC_NCMDS)
		return;
	else if (cmd == PRC_QUENCH) {
		/* XXX there's no PRC_QUENCH in IPv6 */
		notify = tcp_quench;
	} else if (PRC_IS_REDIRECT(cmd))
		notify = in_rtchange, d = NULL;
	else if (cmd == PRC_MSGSIZE)
		; /* special code is present, see below */
	else if (cmd == PRC_HOSTDEAD)
		d = NULL;
	else if (inet6ctlerrmap[cmd] == 0)
		return;

	/* if the parameter is from icmp6, decode it. */
	if (d != NULL) {
		struct ip6ctlparam *ip6cp = (struct ip6ctlparam *)d;
		m = ip6cp->ip6c_m;
		ip6 = ip6cp->ip6c_ip6;
		off = ip6cp->ip6c_off;
		sa6_src = ip6cp->ip6c_src;
	} else {
		m = NULL;
		ip6 = NULL;
		sa6_src = &sa6_any;
	}

	if (ip6) {
		/*
		 * XXX: We assume that when ip6 is non NULL,
		 * M and OFF are valid.
		 */

		/* check if we can safely examine src and dst ports */
		if (m->m_pkthdr.len < off + sizeof(*thp))
			return;

		bzero(&th, sizeof(th));
#ifdef DIAGNOSTIC
		if (sizeof(*thp) > sizeof(th))
			panic("assumption failed in tcp6_ctlinput");
#endif
		m_copydata(m, off, sizeof(*thp), (caddr_t)&th);

		if (cmd == PRC_MSGSIZE) {
			int valid = 0;

			/*
			 * Check to see if we have a valid TCP connection
			 * corresponding to the address in the ICMPv6 message
			 * payload.
			 */
			if (in6_pcbhashlookup(&tcbtable, &sa6->sin6_addr,
			    th.th_dport, (struct in6_addr *)&sa6_src->sin6_addr,
			    th.th_sport))
				valid++;

			/*
			 * Depending on the value of "valid" and routing table
			 * size (mtudisc_{hi,lo}wat), we will:
			 * - recalcurate the new MTU and create the
			 *   corresponding routing entry, or
			 * - ignore the MTU change notification.
			 */
			icmp6_mtudisc_update((struct ip6ctlparam *)d, valid);

			return;
		}

		if (in6_pcbnotify(&tcbtable, sa, th.th_dport,
		    (struct sockaddr *)sa6_src, th.th_sport, cmd, NULL, notify) == 0 &&
		    syn_cache_count &&
		    (inet6ctlerrmap[cmd] == EHOSTUNREACH ||
		     inet6ctlerrmap[cmd] == ENETUNREACH ||
		     inet6ctlerrmap[cmd] == EHOSTDOWN))
			syn_cache_unreach((struct sockaddr *)sa6_src,
			    sa, &th);
	} else {
		(void) in6_pcbnotify(&tcbtable, sa, 0,
		    (struct sockaddr *)sa6_src, 0, cmd, NULL, notify);
	}
}
#endif

void *
tcp_ctlinput(cmd, sa, v)
	int cmd;
	struct sockaddr *sa;
	void *v;
{
	struct ip *ip = v;
	struct tcphdr *th;
	extern int inetctlerrmap[];
	void (*notify)(struct inpcb *, int) = tcp_notify;
	int errno;

	if (sa->sa_family != AF_INET)
		return NULL;

	if ((unsigned)cmd >= PRC_NCMDS)
		return NULL;
	errno = inetctlerrmap[cmd];
	if (cmd == PRC_QUENCH)
		notify = tcp_quench;
	else if (PRC_IS_REDIRECT(cmd))
		notify = in_rtchange, ip = 0;
	else if (cmd == PRC_MSGSIZE && ip_mtudisc) {
		th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		/*
		 * Verify that the packet in the icmp payload refers
		 * to an existing TCP connection.
		 */
		/*
		 * XXX is it possible to get a valid PRC_MSGSIZE error for
		 * a non-established connection?
		 */
		if (in_pcbhashlookup(&tcbtable,
		    ip->ip_dst, th->th_dport, ip->ip_src, th->th_sport)) {
			struct icmp *icp;
			icp = (struct icmp *)((caddr_t)ip -
					      offsetof(struct icmp, icmp_ip));

			/* Calculate new mtu and create corresponding route */
			icmp_mtudisc(icp);
		}
		notify = tcp_mtudisc, ip = 0;
	} else if (cmd == PRC_MTUINC)
		notify = tcp_mtudisc_increase, ip = 0;
	else if (cmd == PRC_HOSTDEAD)
		ip = 0;
	else if (errno == 0)
		return NULL;

	if (ip) {
		th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		if (in_pcbnotify(&tcbtable, sa, th->th_dport, ip->ip_src,
		    th->th_sport, errno, notify) == 0 &&
		    syn_cache_count &&
		    (inetctlerrmap[cmd] == EHOSTUNREACH ||
		     inetctlerrmap[cmd] == ENETUNREACH ||
		     inetctlerrmap[cmd] == EHOSTDOWN)) {
			struct sockaddr_in sin;

			bzero(&sin, sizeof(sin));
			sin.sin_len = sizeof(sin);
			sin.sin_family = AF_INET;
			sin.sin_port = th->th_sport;
			sin.sin_addr = ip->ip_src;
			syn_cache_unreach((struct sockaddr *)&sin,
			    sa, th);
		}
	} else
		in_pcbnotifyall(&tcbtable, sa, errno, notify);

	return NULL;
}

/*
 * When a source quench is received, close congestion window
 * to one segment.  We will gradually open it again as we proceed.
 */
void
tcp_quench(inp, errno)
	struct inpcb *inp;
	int errno;
{
	struct tcpcb *tp = intotcpcb(inp);

	if (tp)
		tp->snd_cwnd = tp->t_maxseg;
}

#ifdef INET6
/*
 * Path MTU Discovery handlers.
 */
void
tcp6_mtudisc_callback(faddr)
	struct in6_addr *faddr;
{
	struct sockaddr_in6 sin6;

	bzero(&sin6, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	sin6.sin6_addr = *faddr;
	(void) in6_pcbnotify(&tcbtable, (struct sockaddr *)&sin6, 0,
	    (struct sockaddr *)&sa6_any, 0, PRC_MSGSIZE, NULL, tcp_mtudisc);
}
#endif /* INET6 */

/*
 * On receipt of path MTU corrections, flush old route and replace it
 * with the new one.  Retransmit all unacknowledged packets, to ensure
 * that all packets will be received.
 */
void
tcp_mtudisc(inp, errno)
	struct inpcb *inp;
	int errno;
{
	struct tcpcb *tp = intotcpcb(inp);
	struct rtentry *rt = in_pcbrtentry(inp);

	if (tp != 0) {
		if (rt != 0) {
			/*
			 * If this was not a host route, remove and realloc.
			 */
			if ((rt->rt_flags & RTF_HOST) == 0) {
				in_rtchange(inp, errno);
				if ((rt = in_pcbrtentry(inp)) == 0)
					return;
			}

			if (rt->rt_rmx.rmx_mtu != 0) {
				/* also takes care of congestion window */
				tcp_mss(tp, -1);
			}
		}

		/*
		 * Resend unacknowledged packets.
		 */
		tp->snd_nxt = tp->snd_una;
		tcp_output(tp);
	}
}

void
tcp_mtudisc_increase(inp, errno)
	struct inpcb *inp;
	int errno;
{
	struct tcpcb *tp = intotcpcb(inp);
	struct rtentry *rt = in_pcbrtentry(inp);

	if (tp != 0 && rt != 0) {
		/*
		 * If this was a host route, remove and realloc.
		 */
		if (rt->rt_flags & RTF_HOST)
			in_rtchange(inp, errno);

		/* also takes care of congestion window */
		tcp_mss(tp, -1);
	}
}

#ifdef TCP_SIGNATURE
int
tcp_signature_tdb_attach()
{
	return (0);
}

int
tcp_signature_tdb_init(tdbp, xsp, ii)
	struct tdb *tdbp;
	struct xformsw *xsp;
	struct ipsecinit *ii;
{
	if ((ii->ii_authkeylen < 1) || (ii->ii_authkeylen > 80))
		return (EINVAL);

	tdbp->tdb_amxkey = malloc(ii->ii_authkeylen, M_XDATA, M_DONTWAIT);
	if (tdbp->tdb_amxkey == NULL)
		return (ENOMEM);
	bcopy(ii->ii_authkey, tdbp->tdb_amxkey, ii->ii_authkeylen);
	tdbp->tdb_amxkeylen = ii->ii_authkeylen;

	return (0);
}

int
tcp_signature_tdb_zeroize(tdbp)
	struct tdb *tdbp;
{
	if (tdbp->tdb_amxkey) {
		bzero(tdbp->tdb_amxkey, tdbp->tdb_amxkeylen);
		free(tdbp->tdb_amxkey, M_XDATA);
		tdbp->tdb_amxkey = NULL;
	}

	return (0);
}

int
tcp_signature_tdb_input(m, tdbp, skip, protoff)
	struct mbuf *m;
	struct tdb *tdbp;
	int skip, protoff;
{
	return (0);
}

int
tcp_signature_tdb_output(m, tdbp, mp, skip, protoff)
	struct mbuf *m;
	struct tdb *tdbp;
	struct mbuf **mp;
	int skip, protoff;
{
	return (EINVAL);
}

int
tcp_signature_apply(fstate, data, len)
	caddr_t fstate;
	caddr_t data;
	unsigned int len;
{
	MD5Update((MD5_CTX *)fstate, (char *)data, len);
	return 0;
}
#endif /* TCP_SIGNATURE */

#define TCP_RNDISS_ROUNDS	16
#define TCP_RNDISS_OUT	7200
#define TCP_RNDISS_MAX	30000

u_int8_t tcp_rndiss_sbox[128];
u_int16_t tcp_rndiss_msb;
u_int16_t tcp_rndiss_cnt;
long tcp_rndiss_reseed;

u_int16_t
tcp_rndiss_encrypt(val)
	u_int16_t val;
{
	u_int16_t sum = 0, i;

	for (i = 0; i < TCP_RNDISS_ROUNDS; i++) {
		sum += 0x79b9;
		val ^= ((u_int16_t)tcp_rndiss_sbox[(val^sum) & 0x7f]) << 7;
		val = ((val & 0xff) << 7) | (val >> 8);
	}

	return val;
}

void
tcp_rndiss_init()
{
	get_random_bytes(tcp_rndiss_sbox, sizeof(tcp_rndiss_sbox));

	tcp_rndiss_reseed = time.tv_sec + TCP_RNDISS_OUT;
	tcp_rndiss_msb = tcp_rndiss_msb == 0x8000 ? 0 : 0x8000;
	tcp_rndiss_cnt = 0;
}

tcp_seq
tcp_rndiss_next()
{
        if (tcp_rndiss_cnt >= TCP_RNDISS_MAX ||
	    time.tv_sec > tcp_rndiss_reseed)
                tcp_rndiss_init();

	/* (arc4random() & 0x7fff) ensures a 32768 byte gap between ISS */
	return ((tcp_rndiss_encrypt(tcp_rndiss_cnt++) | tcp_rndiss_msb) <<16) |
		(arc4random() & 0x7fff);
}

