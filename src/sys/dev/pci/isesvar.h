/*	$OpenBSD: isesvar.h,v 1.1 2001/01/29 08:45:58 ho Exp $	*/

/*
 * Copyright (c) 2000 H�kan Olsson (ho@crt.se)
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

struct ises_softc {
	struct	device		sc_dv;		/* generic device */
	void			*sc_ih;		/* interrupt handler cookie */
	bus_space_handle_t	sc_memh;	/* memory handle */
	bus_space_tag_t		sc_memt;	/* memory tag */
	bus_dma_tag_t		sc_dmat;	/* dma tag */
	bus_dmamap_t		sc_dmamap_xfer; /* dma xfer map */
	struct ises_databuf     sc_dmamap;      /* data area */
	bus_addr_t		sc_dmamap_phys; /* bus address of data area */
	int32_t			sc_cid;		/* crypto tag */
	u_int32_t		sc_intrmask;	/* interrupt mask */
	u_int32_t		sc_omr;		/* OMR */
	SIMPLEQ_HEAD(,ises_q)	sc_queue;	/* packet queue */
	int			sc_nqueue;	/* count enqueued */
	SIMPLEQ_HEAD(,ises_q)	sc_qchip;	/* on chip */
	struct timeout		sc_timeout;	/* init + hrng timeout */
	int			sc_nsessions;	/* nr of sessions */
	struct ises_session	*sc_sessions;	/* sessions */
	int			sc_initstate;	/* card initialization state */
};

struct ises_q {
	SIMPLEQ_ENTRY(ises_q)	q_next;
	struct cryptop		*q_crp;
	struct ises_pktbuf	q_srcpkt;
	struct ises_pktbuf	q_dstpkt;
	struct ises_pktctx	q_ctx;

	struct ises_softc	*q_sc;
	struct mbuf 		*q_src_m, *q_dst_m;

	int			q_sesn;
	long			q_src_packp;
	int			q_src_packl;
	int			q_src_npa, q_src_l;

	long			q_dst_packp;
	int			q_dst_packl;
	int			q_dst_npa, q_dst_l;
	u_int32_t		q_macbuf[5];
};

struct ises_session {
       u_int32_t    ses_used;
       u_int32_t    ses_deskey[6];		/* 3DES key */
       u_int32_t    ses_hminner[5];		/* hmac inner state */
       u_int32_t    ses_hmouter[5];		/* hmac outer state */
       u_int32_t    ses_iv[2];			/* DES/3DES iv */
};

/* Maximum queue length */
#ifndef ISES_MAX_NQUEUE
#define ISES_MAX_NQUEUE		24
#endif
