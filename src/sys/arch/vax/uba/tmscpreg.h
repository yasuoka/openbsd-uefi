/*	$NetBSD: tmscpreg.h,v 1.1 1995/02/23 17:53:19 ragge Exp $ */

/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)tmscpreg.h	7.2 (Berkeley) 5/9/91
 */

/*	@(#)tmscpreg.h	1.1	11/2/84	84/09/25	*/

/****************************************************************
 *								*
 *        Licensed from Digital Equipment Corporation 		*
 *                       Copyright (c) 				*
 *               Digital Equipment Corporation			*
 *                   Maynard, Massachusetts 			*
 *                         1985, 1986 				*
 *                    All rights reserved. 			*
 *								*
 *        The Information in this software is subject to change *
 *   without notice and should not be construed as a commitment *
 *   by  Digital  Equipment  Corporation.   Digital   makes  no *
 *   representations about the suitability of this software for *
 *   any purpose.  It is supplied "As Is" without expressed  or *
 *   implied  warranty. 					*
 *								*
 *        If the Regents of the University of California or its *
 *   licensees modify the software in a manner creating  	*
 *   diriviative copyright rights, appropriate copyright  	*
 *   legends may be placed on  the drivative work in addition   *
 *   to that set forth above. 					*
 *								*
 ****************************************************************/
/*
 * TMSCP registers and structures
 */
 
#ifndef _UBA_TMSCPREG_
#define _UBA_TMSCPREG_
struct tmscpdevice {
	short	tmscpip;	/* initialization and polling */
	short	tmscpsa;	/* status and address */
};
 
#define	TMSCP_ERR		0100000	/* error bit */
#define	TMSCP_STEP4	0040000	/* step 4 has started */
#define	TMSCP_STEP3	0020000	/* step 3 has started */
#define	TMSCP_STEP2	0010000	/* step 2 has started */
#define	TMSCP_STEP1	0004000	/* step 1 has started */
#define	TMSCP_NV		0002000	/* no host settable interrupt vector */
#define	TMSCP_QB		0001000	/* controller supports Q22 bus */
#define	TMSCP_DI		0000400	/* controller implements diagnostics */
#define	TMSCP_OD		0000200	/* port allows odd host addr's in the buffer descriptor */
#define	TMSCP_IE		0000200	/* interrupt enable */
#define	TMSCP_MP		0000100	/* port supports address mapping */
#define	TMSCP_LF		0000002	/* host requests last fail response packet */
#define	TMSCP_PI		0000001	/* host requests adapter purge interrupts */
#define	TMSCP_GO		0000001	/* start operation, after init */
 
 
/*
 * TMSCP Communications Area
 */
 
struct tmscpca {
	short	ca_xxx1;	/* unused */
	char	ca_xxx2;	/* unused */
	char	ca_bdp;		/* BDP to purge */
	short	ca_cmdint;	/* command queue transition interrupt flag */
	short	ca_rspint;	/* response queue transition interrupt flag */
	long	ca_rspdsc[NRSP];/* response descriptors */
	long	ca_cmddsc[NCMD];/* command descriptors */
};
 
#define	ca_ringbase	ca_rspdsc[0]
 
#define	TMSCP_OWN	0x80000000	/* port owns this descriptor (else host
 owns it) */
#define	TMSCP_INT	0x40000000	/* allow interrupt on ring transition */
 
#define	TMSCP_MAP	0x80000000	/* modifier for mapped buffer descriptors */
 
/*
 * TMSCP packet info (same as MSCP)
 */
struct mscp_header {
	short	tmscp_msglen;	/* length of MSCP packet */
	char	tmscp_credits;	/* low 4 bits: credits, high 4 bits: msgtype */
	char	tmscp_vcid;	/* virtual circuit id (connection id) */
};
#endif
