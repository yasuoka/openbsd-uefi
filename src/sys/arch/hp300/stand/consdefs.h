/*	$OpenBSD: consdefs.h,v 1.2 1997/01/17 08:32:41 downsj Exp $	*/
/*	$NetBSD: consdefs.h,v 1.3 1995/10/04 06:54:43 thorpej Exp $	*/

/*
 * Copyright (c) 1982, 1990, 1993
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
 */

/*
 * Glue for determining console select code.
 */
extern	int curcons_scode;
extern	int cons_scode;

/*
 * Console routine prototypes.
 */
#ifdef ITECONSOLE
void	iteprobe __P((struct consdev *));
void	iteinit __P((struct consdev *));
int	itegetchar __P((dev_t));
void	iteputchar __P((dev_t, int));
#endif
#ifdef DCACONSOLE
void	dcaprobe __P((struct consdev *));
void	dcainit __P((struct consdev *));
int	dcagetchar __P((dev_t));
void	dcaputchar __P((dev_t, int));
#endif
#ifdef DCMCONSOLE
void	dcmprobe __P((struct consdev *));
void	dcminit __P((struct consdev *));
int	dcmgetchar __P((dev_t));
void	dcmputchar __P((dev_t, int));
#endif
