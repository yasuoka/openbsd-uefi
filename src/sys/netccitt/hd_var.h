/*	$OpenBSD: hd_var.h,v 1.5 2003/06/02 23:28:13 millert Exp $	*/
/*	$NetBSD: hd_var.h,v 1.7 1996/02/13 22:04:34 christos Exp $	*/

/*
 * Copyright (c) University of British Columbia, 1984
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Laboratory for Computation Vision and the Computer Science Department
 * of the University of British Columbia.
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
 *	@(#)hd_var.h	8.1 (Berkeley) 6/10/93
 */

/*
 *
 * hdlc control block
 *
 */

struct	hdtxq {
	struct	mbuf *head;
	struct	mbuf *tail;
};

struct	hdcb {
	struct	hdcb *hd_next;	/* pointer to next hdlc control block */
	char	hd_state;	/* link state */
	char	hd_vs;		/* send state variable */
	char	hd_vr;		/* receive state variable */
	char	hd_lastrxnr;	/* last received N(R) */
	char	hd_lasttxnr;	/* last transmitted N(R) */
	char	hd_condition;
#define TIMER_RECOVERY_CONDITION        0x01
#define REJ_CONDITION                   0x02
#define REMOTE_RNR_CONDITION            0X04
	char	hd_retxcnt;
	char	hd_xx;
	struct	hdtxq hd_txq;
	struct	mbuf *hd_retxq[MODULUS];
	char	hd_retxqi;
	char	hd_rrtimer;
	char	hd_timer;
#define SET_TIMER(hdp)		hdp->hd_timer = hd_t1
#define KILL_TIMER(hdp)		hdp->hd_timer = 0
	char	hd_dontcopy;	/* if-driver doesn't free I-frames */
	struct	ifnet *hd_ifp;	/* device's network visible interface */
	struct	ifaddr *hd_ifa;	/* device's X.25 network address */
	struct	x25config *hd_xcp;
	caddr_t	hd_pkp;		/* Level III junk */
				/* separate entry for HDLC direct output */
	int	(*hd_output)(struct mbuf *, ...);

	/* link statistics */

	long	hd_iframes_in;
	long	hd_iframes_out;
	long	hd_rrs_in;
	long	hd_rrs_out;
	short	hd_rejs_in;
	short	hd_rejs_out;
	long	hd_window_condition;
	short	hd_invalid_ns;
	short	hd_invalid_nr;
	short	hd_timeouts;
	short	hd_resets;
	short	hd_unknown;
	short	hd_frmrs_in;
	short	hd_frmrs_out;
	short	hd_rnrs_in;
	short	hd_rnrs_out;
};

#ifdef _KERNEL
struct	hdcb *hdcbhead;		/* head of linked list of hdcb's */
struct	Frmr_frame hd_frmr;	/* rejected frame diagnostic info */
struct	ifqueue hdintrq;	/* hdlc packet input queue */
struct	Hdlc_frame;
struct	Hdlc_iframe;
struct	Hdlc_sframe;

int	hd_t1;			/* timer T1 value */
int	hd_t3;			/* RR send timer */
int	hd_n2;			/* frame retransmission limit */


/* hd_debug.c */
void hd_trace(struct hdcb *, int , struct Hdlc_frame *);
int hd_dumptrace(struct hdcb *);

/* hd_input.c */
void hdintr(void);
int process_rxframe(struct hdcb *, struct mbuf *);
int process_iframe(struct hdcb *, struct mbuf *, struct Hdlc_iframe *);
bool range_check(int, int , int );
void process_sframe(struct hdcb *, struct Hdlc_sframe *, int);
bool valid_nr(struct hdcb *, int , int);

/* hd_output.c */
int hd_output(struct mbuf *, ...);
void hd_start(struct hdcb *);
void hd_send_iframe(struct hdcb *, struct mbuf *, int);
int hd_ifoutput(struct mbuf *, ...);
void hd_resend_iframe(struct hdcb *);

/* hd_subr.c */
void hd_init(void);
void *hd_ctlinput(int , struct sockaddr *, void *);
void hd_initvars(struct hdcb *);
int hd_decode(struct hdcb *, struct Hdlc_frame *);
void hd_writeinternal(struct hdcb *, int, int );
void hd_append(struct hdtxq *, struct mbuf *);
void hd_flush(struct ifnet *);
void hd_message(struct hdcb *, char *);
int hd_status(struct hdcb *);
struct mbuf *hd_remove(struct hdtxq *);

/* hd_timer.c */
void hd_timer(void);

#endif
