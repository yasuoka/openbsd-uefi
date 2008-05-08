/*	$OpenBSD: mpls.h,v 1.9 2008/05/08 03:18:39 claudio Exp $	*/

/*
 * Copyright (C) 1999, 2000 and 2001 AYAME Project, WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULARPURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, ORCONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 */

#ifndef _NETMPLS_MPLS_H_
#define _NETMPLS_MPLS_H_

#include <sys/param.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_dl.h>

/*
 * Structure of a SHIM header.
 */
#define MPLS_LABEL_MAX		((1 << 20) - 1)

struct shim_hdr {
	u_int32_t shim_label;	/* 20 bit label, 4 bit exp & BoS, 8 bit TTL */
};

/*
 * By byte-swapping the constants, we avoid ever having to byte-swap IP
 * addresses inside the kernel.  Unfortunately, user-level programs rely
 * on these macros not doing byte-swapping.
 */

#ifdef _KERNEL
#define __MADDR(x)     ((u_int32_t)htonl((u_int32_t)(x)))
#else
#define __MADDR(x)     ((u_int32_t)(x))
#endif

#define MPLS_LABEL_MASK		__MADDR(0xfffff000U)
#define MPLS_LABEL_OFFSET	12
#define MPLS_EXP_MASK		__MADDR(0x00000e00U)
#define MPLS_EXP_OFFSET		9
#define MPLS_BOS_MASK		__MADDR(0x00000100U)
#define MPLS_BOS_OFFSET		8
#define MPLS_TTL_MASK		__MADDR(0x000000ffU)

#define MPLS_BOS_ISSET(l)	(((l) & MPLS_BOS_MASK) == MPLS_BOS_MASK)

/* Reserved lavel values (RFC3032) */
#define MPLS_LABEL_IPV4NULL	0               /* IPv4 Explicit NULL Label */
#define MPLS_LABEL_RTALERT	1               /* Router Alert Label       */
#define MPLS_LABEL_IPV6NULL	2               /* IPv6 Explicit NULL Label */
#define MPLS_LABEL_IMPLNULL	3               /* Implicit NULL Label      */
/*      MPLS_LABEL_RESERVED	4-15 */		/* Values 4-15 are reserved */
#define MPLS_LABEL_RESERVED_MAX 15

/*
 * Socket address
 */

struct sockaddr_mpls {
	u_int8_t	smpls_len;		/* length */
	u_int8_t	smpls_family;		/* AF_MPLS */
	u_int8_t	smpls_operation;
	u_int8_t	smpls_out_exp;		/* outgoing exp value */
	u_int32_t	smpls_out_label;	/* outgoing MPLS label */
	u_int16_t	smpls_out_ifindex;
	u_int16_t	smpls_in_ifindex;
	u_int32_t	smpls_in_label;		/* MPLS label 20 bits*/
#if MPLS_MCAST
	u_int8_t smpls_mcexp;
	u_int8_t smpls_pad2[2];
	u_int32_t smpls_mclabel;
#endif
};

#define MPLS_OP_POP		1
#define MPLS_OP_PUSH		2
#define MPLS_OP_SWAP		3

#define MPLS_INKERNEL_LOOP_MAX	16

#define satosmpls(sa)		((struct sockaddr_mpls *)(sa))
#define smplstosa(smpls)	((struct sockaddr *)(smpls))
#define satosdl(sa)		((struct sockaddr_dl *)(sa))
#define sdltosa(sdl)		((struct sockaddr *)(sdl))

/*
 * Names for MPLS sysctl objects
 */
#define MPLSCTL_ENABLE			1
#define	MPLSCTL_DEFTTL			2
#define MPLSCTL_IFQUEUE			3
#define	MPLSCTL_MAXINKLOOP		4
#define MPLSCTL_MAXID			5

#define MPLSCTL_NAMES { \
	{ 0, 0 }, \
	{ "enable", CTLTYPE_INT }, \
	{ "ttl", CTLTYPE_INT }, \
	{ "ifq", CTLTYPE_NODE },\
	{ "maxloop_inkernel", CTLTYPE_INT }, \
}

#define MPLSCTL_VARS { \
	0, \
	&mpls_enable, \
	&mpls_defttl, \
	0, \
	&mpls_inkloop, \
}

#endif

#ifdef _KERNEL

struct mpe_softc {
	struct ifnet		sc_if;		/* the interface */
	int			sc_unit;
	struct shim_hdr		sc_shim;
	LIST_ENTRY(mpe_softc)	sc_list;
};

#define MPE_HDRLEN	sizeof(struct shim_hdr)
#define MPE_MTU		1500
#define MPE_MTU_MIN	256
#define MPE_MTU_MAX	8192

void	mpe_input(struct mbuf *, struct ifnet *, struct sockaddr_mpls *,
	    u_int32_t);

extern int mpls_raw_usrreq(struct socket *, int, struct mbuf *,
			struct mbuf *, struct mbuf *);

extern struct ifqueue	mplsintrq;	/* MPLS input queue */
extern int		mplsqmaxlen;	/* MPLS input queue length */
extern int		mpls_enable;
extern int		mpls_defttl;

void	mpls_init(void);
void	mplsintr(void);

struct mbuf	*mpls_shim_pop(struct mbuf *);
struct mbuf	*mpls_shim_swap(struct mbuf *, struct sockaddr_mpls *);
struct mbuf	*mpls_shim_push(struct mbuf *, struct sockaddr_mpls *);

int	mpls_sysctl(int *, u_int, void *, size_t *, void *, size_t);
void	mpls_input(struct mbuf *);

#endif /* _KERNEL */
