/*	$OpenBSD: spx_debug.c,v 1.7 2003/06/02 23:28:16 millert Exp $	*/

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
 *	@(#)spx_debug.c
 *
 * from FreeBSD Id: spx_debug.c,v 1.5 1996/03/11 15:13:58 davidg Exp
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#define	PRUREQUESTS
#include <sys/protosw.h>
#include <sys/errno.h>

#include <net/route.h>
#include <net/if.h>
#include <netinet/in_systm.h>
#define	TCPSTATES
#include <netinet/tcp_fsm.h>

#include <netipx/ipx.h>
#include <netipx/ipx_pcb.h>
#include <netipx/ipx.h>
#include <netipx/ipx_var.h>
#include <netipx/spx.h>
#define SPXTIMERS
#include <netipx/spx_timer.h>
#include <netipx/spx_var.h>
#define	SANAMES
#include <netipx/spx_debug.h>

int	spxconsdebug = 0;

/*
 * spx debug routines
 */
void
spx_trace(act, ostate, sp, si, req)
	short act;
	u_char ostate;
	struct spxpcb *sp;
	struct spx *si;
	int req;
{
#ifdef SPXDEBUG
	u_short seq, ack, len, alo;
	int flags;
	struct spx_debug *sd = &spx_debug[spx_debx++];

	if (spx_debx == SPX_NDEBUG)
		spx_debx = 0;
	sd->sd_time = iptime();
	sd->sd_act = act;
	sd->sd_ostate = ostate;
	sd->sd_cb = (caddr_t)sp;
	if (sp)
		sd->sd_sp = *sp;
	else
		bzero((caddr_t)&sd->sd_sp, sizeof(*sp));
	if (si)
		sd->sd_si = *si;
	else
		bzero((caddr_t)&sd->sd_si, sizeof(*si));
	sd->sd_req = req;
	if (spxconsdebug == 0)
		return;
	if (ostate >= TCP_NSTATES) ostate = 0;
	if (act >= SA_DROP) act = SA_DROP;
	if (sp)
		printf("%x %s:", sp, tcpstates[ostate]);
	else
		printf("???????? ");
	printf("%s ", spxnames[act]);
	switch (act) {

	case SA_RESPOND:
	case SA_INPUT:
	case SA_OUTPUT:
	case SA_DROP:
		if (si == 0)
			break;
		seq = si->si_seq;
		ack = si->si_ack;
		alo = si->si_alo;
		len = si->si_len;
		if (act == SA_OUTPUT) {
			seq = ntohs(seq);
			ack = ntohs(ack);
			alo = ntohs(alo);
			len = ntohs(len);
		}
#ifndef lint
#define p1(f)  { printf("%s = %x, ", "f", f); }
		p1(seq); p1(ack); p1(alo); p1(len);
#endif
		flags = si->si_cc;
		if (flags) {
			char *cp = "<";
#ifndef lint
#define pf(f) { if (flags & SPX_ ## f) { printf("%s%s", cp, "f"); cp = ","; } }
			pf(SP); pf(SA); pf(OB); pf(EM);
#else
			cp = cp;
#endif
			printf(">");
		}
#ifndef lint
#define p2(f)  { printf("%s = %x, ", "f", si->si_ ## f); }
		p2(sid);p2(did);p2(dt);p2(pt);
#endif
		ipx_printhost(&si->si_sna);
		ipx_printhost(&si->si_dna);

		if (act==SA_RESPOND) {
			printf("ipx_len = %x, ",
				((struct ipx *)si)->ipx_len);
		}
		break;

	case SA_USER:
		printf("%s", prurequests[req&0xff]);
		if ((req & 0xff) == PRU_SLOWTIMO)
			printf("<%s>", spxtimers[req>>8]);
		break;
	}
	if (sp)
		printf(" -> %s", tcpstates[sp->s_state]);
	/* print out internal state of sp !?! */
	printf("\n");
	if (sp == 0)
		return;
#ifndef lint
#define p3(f)  { printf("%s = %x, ", "f", sp->s_ ## f); }
	printf("\t"); p3(rack);p3(ralo);p3(smax);p3(flags); printf("\n");
#endif
#endif
}
