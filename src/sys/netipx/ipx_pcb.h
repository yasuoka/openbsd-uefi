/*	$OpenBSD: ipx_pcb.h,v 1.6 2003/06/02 23:28:16 millert Exp $	*/

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
 *	@(#)ipx_pcb.h
 *
 * from FreeBSD Id: ipx_pcb.h,v 1.5 1995/11/24 12:25:10 bde Exp
 */

#ifndef _NETIPX_IPX_PCB_H_
#define	_NETIPX_IPX_PCB_H_

/*
 * IPX protocol interface control block.
 */
struct ipxpcb {
	LIST_ENTRY(ipxpcb)	ipxp_hash;
	CIRCLEQ_ENTRY(ipxpcb)	ipxp_queue;
	struct	ipxpcbtable	*ipxp_table;	/* back pointer to the table */
	struct	socket		*ipxp_socket;	/* back pointer to socket */
	struct	ipx_addr	ipxp_faddr;	/* destination address */
	struct	ipx_addr	ipxp_laddr;	/* socket's address */
#define ipxp_lport ipxp_laddr.ipx_port
#define ipxp_fport ipxp_faddr.ipx_port
	caddr_t	ipxp_ppcb;		/* protocol specific stuff */
	struct	route ipxp_route;	/* routing information */
	struct	ipx_addr ipxp_lastdst;	/* validate cached route for dg socks*/
	u_long	ipxp_notify_param;	/* extra info passed via ipx_pcbnotify*/
	u_short	ipxp_flags;
	u_char	ipxp_dpt;		/* default packet type for ipx_output */
	u_char	ipxp_rpt;		/* last received packet type by ipx_input() */
};

struct ipxpcbtable {
	CIRCLEQ_HEAD(, ipxpcb)	ipxpt_queue;
	LIST_HEAD(ipxppcbhead, ipxpcb) *ipxpt_hashtbl;
	u_long		ipxpt_hash;
	u_int16_t	ipxpt_lport;
};

/* possible flags */

#define IPXP_IN_ABORT	0x1	/* calling abort through socket */
#define IPXP_RAWIN	0x2	/* show headers on input */
#define IPXP_RAWOUT	0x4	/* show header on output */
#define IPXP_ALL_PACKETS 0x8	/* Turn off higher proto processing */

#define	IPX_WILDCARD	1

#define	sotoipxpcb(so)		((struct ipxpcb *)((so)->so_pcb))

/*
 * Nominal space allocated to a IPX socket.
 */
#define	IPXSNDQ		16384
#define	IPXRCVQ		40960

#ifdef _KERNEL
extern struct ipxpcbtable ipxcbtable, ipxrawcbtable;	/* head of list */

void	ipx_pcbinit(struct ipxpcbtable *, int);
int	ipx_pcballoc(struct socket *so, struct ipxpcbtable *head);
int	ipx_pcbbind(struct ipxpcb *ipxp, struct mbuf *nam);
int	ipx_pcbconnect(struct ipxpcb *ipxp, struct mbuf *nam);
void	ipx_pcbdetach(struct ipxpcb *ipxp);
void	ipx_pcbdisconnect(struct ipxpcb *ipxp);
struct ipxpcb *ipx_pcblookup(struct ipx_addr *faddr, int lport, int wildp);
void	ipx_pcbnotify(struct ipx_addr *dst, int errno,
	    void (*notify)(struct ipxpcb *), long param);
void	ipx_setpeeraddr(struct ipxpcb *ipxp, struct mbuf *nam);
void	ipx_setsockaddr(struct ipxpcb *ipxp, struct mbuf *nam);
#endif

#endif /* !_NETIPX_IPX_PCB_H_ */
