/*	$OpenBSD: libsa.h,v 1.2 1998/07/08 19:31:13 mickey Exp $	*/
/*	$NOWHERE: libsa.h,v 2.2 1998/06/22 19:34:47 mickey Exp $	*/

/*
 * Copyright (c) 1998 Michael Shalayeff
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
 *	This product includes software developed by Michael Shalayeff.
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

#include <lib/libsa/stand.h>

#define	DEFAULT_KERNEL_ADDRESS	0x12000

void pdc_init __P((void));
void getbinfo __P((void));

int ctstrategy __P((void *, int, daddr_t, size_t, void *, size_t *));
int ctopen __P((struct open_file *, ...));
int ctclose __P((struct open_file *));
int ctioctl __P((struct open_file *, u_long, void *));

int dkstrategy __P((void *, int, daddr_t, size_t, void *, size_t *));
int dkopen __P((struct open_file *, ...));
int dkclose __P((struct open_file *));
int dkioctl __P((struct open_file *, u_long, void *));

void ite_probe __P((struct consdev *));
void ite_init __P((struct consdev *));
int ite_getc __P((dev_t));
void ite_putc __P((dev_t, int));
void ite_pollc __P((dev_t, int));

void machdep __P((void));
void devboot __P((dev_t, char *));
void fcacheall __P((void));
void sync_caches __P((void));

extern int debug;
