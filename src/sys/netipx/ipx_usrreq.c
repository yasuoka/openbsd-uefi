/*	$OpenBSD: ipx_usrreq.c,v 1.14 2004/01/03 14:08:54 espie Exp $	*/

/*-
 *
 * Copyright (c) 1996 Michael Shalayeff
 * Copyright (c) 1995, Mike Mitchell
 * Copyright (c) 1984, 1985, 1986, 1987, 1993
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
 *	@(#)ipx_usrreq.c
 *
 * from FreeBSD Id: ipx_usrreq.c,v 1.5 1996/03/11 15:13:57 davidg Exp
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <uvm/uvm_extern.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>

#include <netipx/ipx.h>
#include <netipx/ipx_pcb.h>
#include <netipx/ipx_if.h>
#include <netipx/ipx_var.h>
#include <netipx/ipx_ip.h>

#include <sys/stdarg.h>

/*
 * IPX protocol implementation.
 */

int noipxRoute;

int ipx_sendspace = IPXSNDQ;
int ipx_recvspace = IPXRCVQ;

/*
 *  This may also be called for raw listeners.
 */
void
ipx_input(struct mbuf *m, ...)
{
	struct ipxpcb *ipxp;
	struct ipx *ipx = mtod(m, struct ipx *);
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct sockaddr_ipx ipx_ipx;
	va_list	ap;

	va_start(ap, m);
	ipxp = va_arg(ap, struct ipxpcb *);
	va_end(ap);

	if (ipxp == NULL)
		panic("No ipxpcb");
	/*
	 * Construct sockaddr format source address.
	 * Stuff source address and datagram in user buffer.
	 */
	bzero(&ipx_ipx, sizeof(ipx_ipx));
	ipx_ipx.sipx_len = sizeof(ipx_ipx);
	ipx_ipx.sipx_family = AF_IPX;
	ipx_ipx.sipx_addr = ipx->ipx_sna;
	if (ipx_neteqnn(ipx->ipx_sna.ipx_net, ipx_zeronet) && ifp) {
		struct ifaddr *ifa;

		for (ifa = ifp->if_addrlist.tqh_first;
		     ifa; ifa = ifa->ifa_list.tqe_next) {
			if (ifa->ifa_addr->sa_family == AF_IPX) {
				ipx_ipx.sipx_addr.ipx_net =
					IA_SIPX(ifa)->sipx_addr.ipx_net;
				break;
			}
		}
	}
	ipxp->ipxp_rpt = ipx->ipx_pt;
	if ( ! (ipxp->ipxp_flags & IPXP_RAWIN) ) {
		m->m_len -= sizeof(struct ipx);
		m->m_pkthdr.len -= sizeof(struct ipx);
		m->m_data += sizeof(struct ipx);
	}
	if (sbappendaddr(&ipxp->ipxp_socket->so_rcv, sipxtosa(&ipx_ipx), m,
	    (struct mbuf *)0) == 0)
		goto bad;
	sorwakeup(ipxp->ipxp_socket);
	return;
bad:
	m_freem(m);
}

void
ipx_abort(ipxp)
	struct ipxpcb *ipxp;
{
	struct socket *so = ipxp->ipxp_socket;

	ipx_pcbdisconnect(ipxp);
	soisdisconnected(so);
}
/*
 * Drop connection, reporting
 * the specified error.
 */
/* struct ipxpcb * DELETE THIS */
void
ipx_drop(ipxp, errno)
	struct ipxpcb *ipxp;
	int errno;
{
	struct socket *so = ipxp->ipxp_socket;

	/*
	 * someday, in the xerox world
	 * we will generate error protocol packets
	 * announcing that the socket has gone away.
	 */
	/*if (TCPS_HAVERCVDSYN(tp->t_state)) {
		tp->t_state = TCPS_CLOSED;
		(void) tcp_output(tp);
	}*/
	so->so_error = errno;
	ipx_pcbdisconnect(ipxp);
	soisdisconnected(so);
}

int
ipx_output(struct mbuf *m0, ...)
{
	struct mbuf *m;
	struct ipx *ipx;
	struct ipxpcb *ipxp;
	struct socket *so;
	int len = 0;
	struct route *ro;
	struct mbuf *mprev = NULL;
	va_list	ap;

	va_start(ap, m0);
	ipxp = va_arg(ap, struct ipxpcb *);
	va_end(ap);

	/*
	 * Calculate data length.
	 */
	for (m = m0; m; m = m->m_next) {
		mprev = m;
		len += m->m_len;
	}
	/*
	 * Make sure packet is actually of even length.
	 */
	
	if (len & 1) {
		m = mprev;
		if ((m->m_flags & M_EXT) == 0 &&
			(m->m_len + m->m_data < &m->m_dat[MLEN])) {
			m->m_len++;
		} else {
			struct mbuf *m1 = m_get(M_DONTWAIT, MT_DATA);

			if (m1 == 0) {
				m_freem(m0);
				return (ENOBUFS);
			}
			m1->m_len = 1;
			* mtod(m1, char *) = 0;
			m->m_next = m1;
		}
		m0->m_pkthdr.len++;
	}

	/*
	 * Fill in mbuf with extended IPX header
	 * and addresses and length put into network format.
	 */
	m = m0;
	if (ipxp->ipxp_flags & IPXP_RAWOUT) {
		ipx = mtod(m, struct ipx *);
	} else {
		M_PREPEND(m, sizeof(struct ipx), M_DONTWAIT);
		if (m == 0)
			return (ENOBUFS);
		ipx = mtod(m, struct ipx *);
		ipx->ipx_tc = 0;
		ipx->ipx_pt = ipxp->ipxp_dpt;
		ipx->ipx_sna = ipxp->ipxp_laddr;
		ipx->ipx_dna = ipxp->ipxp_faddr;
		len += sizeof(struct ipx);
	}

	ipx->ipx_len = htons((u_short)len);

	if (ipxcksum) {
		ipx->ipx_sum = 0;
		len = ((len - 1) | 1) + 1;
		ipx->ipx_sum = ipx_cksum(m, len);
	} else
		ipx->ipx_sum = 0xffff;

	/*
	 * Output datagram.
	 */
	so = ipxp->ipxp_socket;
	if (so->so_options & SO_DONTROUTE)
		return (ipx_outputfl(m, (struct route *)0,
		    (so->so_options & SO_BROADCAST) | IPX_ROUTETOIF));
	/*
	 * Use cached route for previous datagram if
	 * possible.  If the previous net was the same
	 * and the interface was a broadcast medium, or
	 * if the previous destination was identical,
	 * then we are ok.
	 *
	 * NB: We don't handle broadcasts because that
	 *     would require 3 subroutine calls.
	 */
	ro = &ipxp->ipxp_route;
	if (noipxRoute)
		ro = 0;
	return (ipx_outputfl(m, ro, so->so_options & SO_BROADCAST));
}

/* ARGSUSED */
int
ipx_ctloutput(req, so, level, name, value)
	int req, level;
	struct socket *so;
	int name;
	struct mbuf **value;
{
	struct mbuf *m;
	struct ipxpcb *ipxp = sotoipxpcb(so);
	int mask, error = 0;
	/*extern long ipx_pexseq;*/ /*XXX*//*JRE*/

	if (ipxp == NULL)
		return (EINVAL);

	switch (req) {

	case PRCO_GETOPT:
		if (value==NULL)
			return (EINVAL);
		m = m_get(M_DONTWAIT, MT_DATA);
		if (m==NULL)
			return (ENOBUFS);
		switch (name) {

		case SO_ALL_PACKETS:
			mask = IPXP_ALL_PACKETS;
			goto get_flags;

		case SO_HEADERS_ON_INPUT:
			mask = IPXP_RAWIN;
			goto get_flags;
			
		case SO_HEADERS_ON_OUTPUT:
			mask = IPXP_RAWOUT;
		get_flags:
			m->m_len = sizeof(short);
			*mtod(m, short *) = ipxp->ipxp_flags & mask;
			break;

		case SO_DEFAULT_HEADERS:
			m->m_len = sizeof(struct ipx);
			{
				struct ipx *ipx = mtod(m, struct ipx *);
				ipx->ipx_len = 0;
				ipx->ipx_sum = 0;
				ipx->ipx_tc = 0;
				ipx->ipx_pt = ipxp->ipxp_dpt;
				ipx->ipx_dna = ipxp->ipxp_faddr;
				ipx->ipx_sna = ipxp->ipxp_laddr;
			}
			break;

		case SO_SEQNO:
			m->m_len = sizeof(long);
			*mtod(m, long *) = ipx_pexseq++;
			break;

		default:
			error = EINVAL;
		}
		*value = m;
		break;

	case PRCO_SETOPT:
		switch (name) {
			int *ok;

		case SO_ALL_PACKETS:
			mask = IPXP_ALL_PACKETS;
			goto set_head;

		case SO_HEADERS_ON_INPUT:
			mask = IPXP_RAWIN;
			goto set_head;

		case SO_HEADERS_ON_OUTPUT:
			mask = IPXP_RAWOUT;
		set_head:
			if (value && *value) {
				ok = mtod(*value, int *);
				if (*ok)
					ipxp->ipxp_flags |= mask;
				else
					ipxp->ipxp_flags &= ~mask;
			} else error = EINVAL;
			break;

		case SO_DEFAULT_HEADERS:
			{
				struct ipx *ipx
				    = mtod(*value, struct ipx *);
				ipxp->ipxp_dpt = ipx->ipx_pt;
			}
			break;
#ifdef IPXIP
		case SO_IPXIP_ROUTE:
			error = ipxip_route(*value);
			break;
#endif /* IPXIP */
#ifdef IPXTUNNEL
		case SO_IPXTUNNEL_ROUTE:
			error = ipxtun_route(*value);
			break;
#endif
		default:
			error = EINVAL;
		}
		if (value && *value)
			m_freem(*value);
		break;
	}
	return (error);
}

/*ARGSUSED*/
int
ipx_usrreq(so, req, m, nam, control)
	struct socket *so;
	int req;
	struct mbuf *m, *nam, *control;
{
	struct ipxpcb *ipxp = sotoipxpcb(so);
	int error = 0;

	if (req == PRU_CONTROL)
                return (ipx_control(so, (long)m, (caddr_t)nam,
			(struct ifnet *)control));
	if (control && control->m_len) {
		error = EINVAL;
		goto release;
	}
	if (ipxp == NULL && req != PRU_ATTACH) {
		error = EINVAL;
		goto release;
	}
	switch (req) {

	case PRU_ATTACH:
		if (ipxp != NULL) {
			error = EINVAL;
			break;
		}
		error = ipx_pcballoc(so, &ipxcbtable);
		if (error)
			break;
		error = soreserve(so, ipx_sendspace, ipx_recvspace);
		if (error)
			break;
		break;

	case PRU_DETACH:
		if (ipxp == NULL) {
			error = ENOTCONN;
			break;
		}
		ipx_pcbdetach(ipxp);
		break;

	case PRU_BIND:
		error = ipx_pcbbind(ipxp, nam);
		break;

	case PRU_LISTEN:
		error = EOPNOTSUPP;
		break;

	case PRU_CONNECT:
		if (!ipx_nullhost(ipxp->ipxp_faddr)) {
			error = EISCONN;
			break;
		}
		error = ipx_pcbconnect(ipxp, nam);
		if (error == 0)
			soisconnected(so);
		break;

	case PRU_CONNECT2:
		error = EOPNOTSUPP;
		break;

	case PRU_ACCEPT:
		error = EOPNOTSUPP;
		break;

	case PRU_DISCONNECT:
		if (ipx_nullhost(ipxp->ipxp_faddr)) {
			error = ENOTCONN;
			break;
		}
		ipx_pcbdisconnect(ipxp);
		soisdisconnected(so);
		break;

	case PRU_SHUTDOWN:
		socantsendmore(so);
		break;

	case PRU_SEND:
	{
		struct ipx_addr laddr;
		int s = 0;

		if (nam) {
			laddr = ipxp->ipxp_laddr;
			if (!ipx_nullhost(ipxp->ipxp_faddr)) {
				error = EISCONN;
				break;
			}
			/*
			 * Must block input while temporarily connected.
			 */
			s = splnet();
			error = ipx_pcbconnect(ipxp, nam);
			if (error) {
				splx(s);
				break;
			}
		} else {
			if (ipx_nullhost(ipxp->ipxp_faddr)) {
				error = ENOTCONN;
				break;
			}
		}
		error = ipx_output(m, ipxp);
		m = NULL;
		if (nam) {
			ipx_pcbdisconnect(ipxp);
			splx(s);
			ipxp->ipxp_laddr.ipx_host = laddr.ipx_host;
			ipxp->ipxp_laddr.ipx_port = laddr.ipx_port;
		}
	}
		break;

	case PRU_ABORT:
		ipx_pcbdetach(ipxp);
		sofree(so);
		soisdisconnected(so);
		break;

	case PRU_SOCKADDR:
		ipx_setsockaddr(ipxp, nam);
		break;

	case PRU_PEERADDR:
		ipx_setpeeraddr(ipxp, nam);
		break;

	case PRU_SENSE:
		/*
		 * stat: don't bother with a blocksize.
		 */
		return (0);

	case PRU_SENDOOB:
	case PRU_FASTTIMO:
	case PRU_SLOWTIMO:
	case PRU_PROTORCV:
	case PRU_PROTOSEND:
		error =  EOPNOTSUPP;
		break;

	case PRU_CONTROL:
	case PRU_RCVD:
	case PRU_RCVOOB:
		return (EOPNOTSUPP);	/* do not free mbuf's */

	default:
		panic("ipx_usrreq");
	}
release:
	if (control != NULL)
		m_freem(control);
	if (m != NULL)
		m_freem(m);
	return (error);
}

/*ARGSUSED*/
int
ipx_raw_usrreq(so, req, m, nam, control)
	struct socket *so;
	int req;
	struct mbuf *m, *nam, *control;
{
	int error = 0;
	struct ipxpcb *ipxp = sotoipxpcb(so);
	/*extern struct ipxpcb ipxrawpcb;*//*XXX*//*JRE*/

	switch (req) {

	case PRU_ATTACH:

		if (!(so->so_state & SS_PRIV) || (ipxp != NULL)) {
			error = EINVAL;
			break;
		}
		error = ipx_pcballoc(so, &ipxrawcbtable);
		if (error)
			break;
		error = soreserve(so, ipx_sendspace, ipx_recvspace);
		if (error)
			break;
		ipxp = sotoipxpcb(so);
		ipxp->ipxp_faddr.ipx_host = ipx_broadhost;
		ipxp->ipxp_flags = IPXP_RAWIN | IPXP_RAWOUT;
		break;
	default:
		error = ipx_usrreq(so, req, m, nam, control);
	}
	return (error);
}

int
ipx_sysctl(name, namelen, oldp, oldlenp, newp, newlen)
	int *name;
	u_int namelen;
	void *oldp;
	size_t *oldlenp;
	void *newp;
	size_t newlen;
{
	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case IPXCTL_CHECKSUM:
		return (sysctl_int(oldp, oldlenp, newp, newlen,
		    &ipxcksum));
	case IPXCTL_FORWARDING:	
		return (sysctl_int(oldp, oldlenp, newp, newlen,
		    &ipxforwarding));
	case IPXCTL_NETBIOS:
		return (sysctl_int(oldp, oldlenp, newp, newlen,
		    &ipxnetbios));
	case IPXCTL_RECVSPACE:
		return (sysctl_int(oldp, oldlenp, newp, newlen,
		    &ipx_recvspace));
	case IPXCTL_SENDSPACE:
		return (sysctl_int(oldp, oldlenp, newp, newlen,
		    &ipx_sendspace));
	default:
		return (ENOPROTOOPT);
	}
	/* NOTREACHED */
}
