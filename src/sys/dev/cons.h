/*	$OpenBSD: cons.h,v 1.13 2003/09/23 16:51:12 millert Exp $	*/
/*	$NetBSD: cons.h,v 1.14 1996/03/14 19:08:35 christos Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 * from: Utah $Hdr: cons.h 1.6 92/01/21$
 *
 *	@(#)cons.h	8.1 (Berkeley) 6/10/93
 */

struct consdev {
				/* probe hardware and fill in consdev info */
	void	(*cn_probe)(struct consdev *);
				/* turn on as console */
	void	(*cn_init)(struct consdev *);
				/* kernel getchar interface */
	int	(*cn_getc)(dev_t);
				/* kernel putchar interface */
	void	(*cn_putc)(dev_t, int);
				/* turn on and off polling */
	void	(*cn_pollc)(dev_t, int);
				/* ring bell */
	void	(*cn_bell)(dev_t, u_int, u_int, u_int);
	dev_t	cn_dev;		/* major/minor of device */
	int	cn_pri;		/* pecking order; the higher the better */
};

/* values for cn_pri - reflect our policy for console selection */
#define	CN_DEAD		0	/* device doesn't exist */
#define CN_NORMAL	1	/* device exists but is nothing special */
#define CN_INTERNAL	2	/* "internal" bit-mapped display */
#define CN_REMOTE	3	/* serial interface with remote bit set */

/* XXX */
#define	CONSMAJOR	0

#ifdef _KERNEL

extern	struct consdev constab[];
extern	struct consdev *cn_tab;

struct knote;

void	cninit(void);
int	cnset(dev_t);
int	cnopen(dev_t, int, int, struct proc *);
int	cnclose(dev_t, int, int, struct proc *);
int	cnread(dev_t, struct uio *, int);
int	cnwrite(dev_t, struct uio *, int);
int	cnioctl(dev_t, u_long, caddr_t, int, struct proc *);
int	cnpoll(dev_t, int, struct proc *);
int	cnkqfilter(dev_t, struct knote *);
int	cngetc(void);
void	cnputc(int);
void	cnpollc(int);
void	cnbell(u_int, u_int, u_int);
void	cnrint(void);
void	nullcnpollc(dev_t, int);

/* console-specific types */
#define	dev_type_cnprobe(n)	void n(struct consdev *)
#define	dev_type_cninit(n)	void n(struct consdev *)
#define	dev_type_cngetc(n)	int n(dev_t)
#define	dev_type_cnputc(n)	void n(dev_t, int)
#define	dev_type_cnpollc(n)	void n(dev_t, int)
#define	dev_type_cnbell(n)	void n(dev_t, u_int, u_int, u_int)

#define	cons_decl(n) \
	dev_decl(n,cnprobe); dev_decl(n,cninit); dev_decl(n,cngetc); \
	dev_decl(n,cnputc); dev_decl(n,cnpollc); dev_decl(n,cnbell);

#define	cons_init(n) { \
	dev_init(1,n,cnprobe), dev_init(1,n,cninit), dev_init(1,n,cngetc), \
	dev_init(1,n,cnputc), dev_init(1,n,cnpollc) }

#define cons_init_bell(n) { \
	dev_init(1,n,cnprobe), dev_init(1,n,cninit), dev_init(1,n,cngetc), \
	dev_init(1,n,cnputc), dev_init(1,n,cnpollc), dev_init(1,n,cnbell) }

#endif
