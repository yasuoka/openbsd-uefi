/*	$OpenBSD: vaxstand.h,v 1.4 1997/05/29 00:04:29 niklas Exp $ */
/*	$NetBSD: vaxstand.h,v 1.5 1996/08/02 11:22:56 ragge Exp $ */
/*
 * Copyright (c) 1994 Ludd, University of Lule}, Sweden.
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
 *     This product includes software developed at Ludd, University of Lule}.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

 /* All bugs are subject to removal without further notice */
		

#define MAXNMBA 8 /* Massbussadapters */
#define MAXNUBA 8 /* Unibusadapters */
#define	MAXMBAU	8 /* Units on an mba */

/* Variables used in autoconf */
extern int nmba, nuba, nbi, nsbi, nuda;
extern int *ubaaddr, *mbaaddr, *udaaddr, *uioaddr, *biaddr;
extern int cpunumber;

/* devsw type definitions, used in bootxx and conf */
#define SADEV(name,strategy,open,close,ioctl) \
        { (char *)name, \
         (int(*)(void *, int ,daddr_t , size_t, void *, size_t *))strategy, \
         (int(*)(struct open_file *, ...))open, \
         (int(*)(struct open_file *))close, \
         (int(*)(struct open_file *,u_long, void *))ioctl}

/*
 * Easy-to-use definitions
 */
#define	min(x,y) (x < y ? x : y)

/*
 * Device numbers gotten from boot prom.
 */
#define	BDEV_MBA		0
#define	BDEV_RK06		1
#define	BDEV_RL02		2
#define	BDEV_UDA		17
#define	BDEV_TK50		18
#define	BDEV_CONSOLE		64
