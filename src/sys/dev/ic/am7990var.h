/*	$NetBSD: am7990var.h,v 1.1 1995/06/28 02:24:56 cgd Exp $	*/

/*
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
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

#ifdef DDB
#define	integrate
#else
#define	integrate	static inline
#endif

void leconfig __P((struct le_softc *));
void leinit __P((struct le_softc *));
int leioctl __P((struct ifnet *, u_long, caddr_t));
void lememinit __P((struct le_softc *));
void lereset __P((struct le_softc *));
void lesetladrf __P((struct arpcom *, u_int16_t *));
void lestart __P((struct ifnet *));
void lestop __P((struct le_softc *));
void lewatchdog __P((/* short */));

integrate u_int16_t lerdcsr __P((/* struct le_softc *, u_int16_t */));
integrate void lewrcsr __P((/* struct le_softc *, u_int16_t, u_int16_t */));

integrate void lerint __P((struct le_softc *));
integrate void letint __P((struct le_softc *));

integrate int leput __P((struct le_softc *, int, struct mbuf *));
integrate struct mbuf *leget __P((struct le_softc *, int, int));
integrate void leread __P((struct le_softc *, int, int));

void copytodesc_contig(), copyfromdesc_contig();
void copytobuf_contig(), copyfrombuf_contig(), zerobuf_contig();
#ifdef 0
void copytobuf_gap2(), copyfrombuf_gap2(), zerobuf_gap2();
void copytobuf_gap16(), copyfrombuf_gap16(), zerobuf_gap16();
#endif
