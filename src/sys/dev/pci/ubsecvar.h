/*	$OpenBSD: ubsecvar.h,v 1.21 2001/06/29 21:52:42 jason Exp $	*/

/*
 * Copyright (c) 2000 Theo de Raadt
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

/* Maximum queue length */
#ifndef UBS_MAX_NQUEUE
#define UBS_MAX_NQUEUE		60
#endif

#define	UBS_MAX_SCATTER		64	/* Maximum scatter/gather depth */
#define	UBS_MAX_AGGR		5	/* Maximum aggregation count */

#define	UBSEC_CARD(sid)		(((sid) & 0xf0000000) >> 28)
#define	UBSEC_SESSION(sid)	( (sid) & 0x0fffffff)
#define	UBSEC_SID(crd, sesn)	(((crd) << 28) | ((sesn) & 0x0fffffff))

struct ubsec_dma_alloc {
	u_int32_t		dma_paddr;
	caddr_t			dma_vaddr;
	bus_dmamap_t		dma_map;
	bus_dma_segment_t	dma_seg;
	bus_size_t		dma_size;
	int			dma_nseg;
};

struct ubsec_q2 {
	SIMPLEQ_ENTRY(ubsec_q2)		q_next;
	struct ubsec_dma_alloc		q_mcr;
	struct ubsec_dma_alloc		q_ctx;
};

struct ubsec_q2_rng {
	struct ubsec_q2			rng_q;
	struct ubsec_dma_alloc		rng_buf;
	int				rng_used;
};
#define	UBSEC_RNG_BUFSIZ	16		/* measured in 32bit words */

struct ubsec_dma {
	SIMPLEQ_ENTRY(ubsec_dma)	d_next;
	struct ubsec_dma_alloc		d_ctx;
};

struct ubsec_softc {
	struct	device		sc_dv;		/* generic device */
	void			*sc_ih;		/* interrupt handler cookie */
	bus_space_handle_t	sc_sh;		/* memory handle */
	bus_space_tag_t		sc_st;		/* memory tag */
	bus_dma_tag_t		sc_dmat;	/* dma tag */
	int			sc_flags;	/* device specific flags */
	u_int32_t		sc_statmask;	/* interrupt status mask */
	int32_t			sc_cid;		/* crypto tag */
	SIMPLEQ_HEAD(,ubsec_q)	sc_queue;	/* packet queue, mcr1 */
	int			sc_nqueue;	/* count enqueued, mcr1 */
	SIMPLEQ_HEAD(,ubsec_q)	sc_qchip;	/* on chip, mcr1 */
	SIMPLEQ_HEAD(,ubsec_q2)	sc_queue2;	/* packet queue, mcr2 */
	int			sc_nqueue2;	/* count enqueued, mcr2 */
	SIMPLEQ_HEAD(,ubsec_q2)	sc_qchip2;	/* on chip, mcr2 */
	int			sc_nsessions;	/* # of sessions */
	struct ubsec_session	*sc_sessions;	/* sessions */
	struct timeout		sc_rngto;	/* rng timeout */
	int			sc_rnghz;	/* rng poll time */
	struct ubsec_q2_rng	sc_rng;
	struct ubsec_dma	sc_dmaa[UBS_MAX_NQUEUE];
	SIMPLEQ_HEAD(,ubsec_dma) sc_dma;
};

#define	UBS_FLAGS_KEY		0x01		/* has key accelerator */
#define	UBS_FLAGS_LONGCTX	0x02		/* uses long ipsec ctx */

struct ubsec_q {
	SIMPLEQ_ENTRY(ubsec_q)		q_next;
	struct cryptop			*q_crp;
	struct ubsec_mcr		*q_mcr;
	struct ubsec_pktbuf		q_srcpkt[UBS_MAX_SCATTER-1];
	struct ubsec_pktbuf		q_dstpkt[UBS_MAX_SCATTER-1];
	struct ubsec_dma		*q_dma;

	struct mbuf 		      	*q_src_m, *q_dst_m;
	struct uio			*q_src_io, *q_dst_io;

	long				q_src_packp[UBS_MAX_SCATTER];
	int				q_src_packl[UBS_MAX_SCATTER];
	int				q_src_npa, q_src_l;

	long				q_dst_packp[UBS_MAX_SCATTER];
	int				q_dst_packl[UBS_MAX_SCATTER];
	int				q_dst_npa, q_dst_l;
	u_int32_t			q_macbuf[5];
	int				q_sesn;
	int				q_flags;
};
#define	UBSEC_QFLAGS_COPYOUTIV		0x1

struct ubsec_session {
	u_int32_t	ses_used;
	u_int32_t	ses_deskey[6];		/* 3DES key */
	u_int32_t       ses_hminner[5];		/* hmac inner state */
	u_int32_t       ses_hmouter[5];		/* hmac outer state */
	u_int32_t       ses_iv[2];		/* [3]DES iv */
};
