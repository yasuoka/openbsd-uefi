/*	$OpenBSD: if_sppp.h,v 1.6 2002/09/26 20:43:54 chris Exp $	*/
/*	$NetBSD: if_sppp.h,v 1.2.2.1 1999/04/04 06:57:39 explorer Exp $	*/

/*
 * Defines for synchronous PPP/Cisco link level subroutines.
 *
 * Copyright (C) 1994 Cronyx Ltd.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * Heavily revamped to conform to RFC 1661.
 * Copyright (C) 1997, Joerg Wunsch.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE FREEBSD PROJECT ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE FREEBSD PROJECT OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * From: Version 2.0, Fri Oct  6 20:39:21 MSK 1995
 *
 * From: if_sppp.h,v 1.8 1997/10/11 11:25:20 joerg Exp
 *
 * From: Id: if_sppp.h,v 1.7 1998/12/01 20:20:19 hm Exp
 */

#ifndef _NET_IF_SPPP_H_
#define _NET_IF_SPPP_H_

#include <sys/timeout.h>

#define IDX_LCP 0		/* idx into state table */

struct slcp {
	u_long	opts;		/* LCP options to send (bitfield) */
	u_long  magic;          /* local magic number */
	u_long	mru;		/* our max receive unit */
	u_long	their_mru;	/* their max receive unit */
	u_long	protos;		/* bitmask of protos that are started */
	u_char  echoid;         /* id of last keepalive echo request */
	/* restart max values, see RFC 1661 */
	int	timeout;
	int	max_terminate;
	int	max_configure;
	int	max_failure;
};

#define IDX_IPCP 1		/* idx into state table */

struct sipcp {
	u_long	opts;		/* IPCP options to send (bitfield) */
	u_int	flags;
#define IPCP_HISADDR_SEEN 1	/* have seen his address already */
#define IPCP_MYADDR_DYN   2	/* my address is dynamically assigned */
#define IPCP_MYADDR_SEEN  4	/* have seen his address already */
};

#define AUTHNAMELEN	32
#define AUTHKEYLEN	16

struct sauth {
	u_short	proto;			/* authentication protocol to use */
	u_short	flags;
#define AUTHFLAG_NOCALLOUT	1	/* do not require authentication on */
					/* callouts */
#define AUTHFLAG_NORECHALLENGE	2	/* do not re-challenge CHAP */
	u_char	name[AUTHNAMELEN];	/* system identification name */
	u_char	secret[AUTHKEYLEN];	/* secret password */
	u_char	challenge[AUTHKEYLEN];	/* random challenge */
};

#define IDX_PAP		2
#define IDX_CHAP	3

#define IDX_COUNT (IDX_CHAP + 1) /* bump this when adding cp's! */

/*
 * Don't change the order of this.  Ordering the phases this way allows
 * for a comparision of ``pp_phase >= PHASE_AUTHENTICATE'' in order to
 * know whether LCP is up.
 */
enum ppp_phase {
	PHASE_DEAD, PHASE_ESTABLISH, PHASE_TERMINATE,
	PHASE_AUTHENTICATE, PHASE_NETWORK
};

struct sppp {
	/* NB: pp_if _must_ be first */
	struct  ifnet pp_if;    /* network interface data */
	struct  ifqueue pp_fastq; /* fast output queue */
	struct	ifqueue pp_cpq;	/* PPP control protocol queue */
	struct  sppp *pp_next;  /* next interface in keepalive list */
	u_int   pp_flags;       /* use Cisco protocol instead of PPP */
	u_short pp_alivecnt;    /* keepalive packets counter */
	u_short pp_loopcnt;     /* loopback detection counter */
	u_long  pp_seq;         /* local sequence number */
	u_long  pp_rseq;        /* remote sequence number */
	enum ppp_phase pp_phase;	/* phase we're currently in */
	int	state[IDX_COUNT];	/* state machine */
	u_char  confid[IDX_COUNT];	/* id of last configuration request */
	int	rst_counter[IDX_COUNT];	/* restart counter */
	int	fail_counter[IDX_COUNT]; /* negotiation failure counter */
	struct timeout ch[IDX_COUNT];
	struct timeout pap_my_to_ch;
	struct slcp lcp;		/* LCP params */
	struct sipcp ipcp;		/* IPCP params */
	struct sauth myauth;		/* auth params, i'm peer */
	struct sauth hisauth;		/* auth params, i'm authenticator */
	/*
	 * These functions are filled in by sppp_attach(), and are
	 * expected to be used by the lower layer (hardware) drivers
	 * in order to communicate the (un)availability of the
	 * communication link.  Lower layer drivers that are always
	 * ready to communicate (like hardware HDLC) can shortcut
	 * pp_up from pp_tls, and pp_down from pp_tlf.
	 */
	void	(*pp_up)(struct sppp *sp);
	void	(*pp_down)(struct sppp *sp);
	/*
	 * These functions need to be filled in by the lower layer
	 * (hardware) drivers if they request notification from the
	 * PPP layer whether the link is actually required.  They
	 * correspond to the tls and tlf actions.
	 */
	void	(*pp_tls)(struct sppp *sp);
	void	(*pp_tlf)(struct sppp *sp);
	/*
	 * These (optional) functions may be filled by the hardware
	 * driver if any notification of established connections
	 * (currently: IPCP up) is desired (pp_con) or any internal
	 * state change of the interface state machine should be
	 * signaled for monitoring purposes (pp_chg).
	 */
	void	(*pp_con)(struct sppp *sp);
	void	(*pp_chg)(struct sppp *sp, int new_state);
};

#define PP_KEEPALIVE    0x01    /* use keepalive protocol */
#define PP_CISCO        0x02    /* use Cisco protocol instead of PPP */
				/* 0x04 was PP_TIMO */
#define PP_CALLIN	0x08	/* we are being called */
#define PP_NEEDAUTH	0x10	/* remote requested authentication */


#define PP_MTU          1500    /* default/minimal MRU */
#define PP_MAX_MRU	2048	/* maximal MRU we want to negotiate */

/*
 * Definitions to pass struct sppp data down into the kernel using the
 * SIOC[SG]IFGENERIC ioctl interface.
 *
 * In order to use this, create a struct spppreq, fill in the cmd
 * field with SPPPIOGDEFS, and put the address of this structure into
 * the ifr_data portion of a struct ifreq.  Pass this struct to a
 * SIOCGIFGENERIC ioctl.  Then replace the cmd field by SPPPIOCDEFS,
 * modify the defs field as desired, and pass the struct ifreq now
 * to a SIOCSIFGENERIC ioctl.
 */

#define SPPPIOGDEFS  ((caddr_t)(('S' << 24) + (1 << 16) + sizeof(struct sppp)))
#define SPPPIOSDEFS  ((caddr_t)(('S' << 24) + (2 << 16) + sizeof(struct sppp)))

struct spppreq {
	int	cmd;
	struct sppp defs;
};

#if defined(_KERNEL)
void sppp_attach (struct ifnet *ifp);
void sppp_detach (struct ifnet *ifp);
void sppp_input (struct ifnet *ifp, struct mbuf *m);

/* Workaround */
void spppattach (struct ifnet *ifp);
int sppp_ioctl(struct ifnet *ifp, u_long cmd, void *data);

struct mbuf *sppp_dequeue (struct ifnet *ifp);
struct mbuf *sppp_pick(struct ifnet *ifp);
int sppp_isempty (struct ifnet *ifp);
void sppp_flush (struct ifnet *ifp);
#endif
#endif /* _NET_IF_SPPP_H_ */
