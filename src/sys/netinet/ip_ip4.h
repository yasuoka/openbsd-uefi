/*	$OpenBSD: ip_ip4.h,v 1.7 1997/07/01 22:12:50 provos Exp $	*/

/*
 * The author of this code is John Ioannidis, ji@tla.org,
 * 	(except when noted otherwise).
 *
 * This code was written for BSD/OS in Athens, Greece, in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis, kermit@forthnet.gr.
 *
 * Copyright (C) 1995, 1996, 1997 by John Ioannidis and Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NEITHER AUTHOR MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

/*
 * IP-inside-IP processing.
 * Not quite all the functionality of RFC-1853, but the main idea is there.
 */

struct ip4stat
{
    u_int32_t	ip4s_ipackets;		/* total input packets */
    u_int32_t	ip4s_opackets;		/* total output packets */
    u_int32_t	ip4s_hdrops;		/* packet shorter than header shows */
    u_int32_t	ip4s_badlen;
    u_int32_t	ip4s_notip4;
    u_int32_t	ip4s_qfull;
};

#define IP4_DEFAULT_TTL    0
#define IP4_SAME_TTL	  -1

#ifdef _KERNEL
struct ip4stat ip4stat;
#endif
