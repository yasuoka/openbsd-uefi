/*	$OpenBSD: biosdev.h,v 1.7 1997/04/23 14:49:23 weingart Exp $	*/

/*
 * Copyright (c) 1996 Michael Shalayeff
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */


#define	BIOSNHEADS(d)	(((d)>>8)+1)
#define	BIOSNSECTS(d)	((d)&0xff)	/* sectors are 1-based */

#ifdef _LOCORE
#define	BIOSINT(n)	int	$0x20+(n)
#else
#define	BIOSINT(n)	__asm ((int $0x20+(n)))

/* biosdev.c */
extern const char *biosdevs[];
int biosstrategy __P((void *, int, daddr_t, size_t, void *, size_t *));
int biosopen __P((struct open_file *, ...));
int biosclose __P((struct open_file *));
int biosioctl __P((struct open_file *, u_long, void *));

/* biosdisk.S */
u_int16_t biosdinfo __P((int dev));
int		biosdreset __P((int dev));
int     biosread  __P((int dev, int cyl, int hd, int sect, int nsect, void *));
int     bioswrite __P((int dev, int cyl, int hd, int sect, int nsect, void *));

/* bioskbd.S */
int	kbd_probe __P((void));
void	kbd_putc __P((int c));
int	kbd_getc __P((void));
int	kbd_ischar __P((void));

/* bioscom.S */
int	com_probe __P((void));
void	com_putc __P((int c));
int	com_getc __P((void));
int	com_ischar __P((void));

/* biosmem.S */
u_int	biosmem __P((void));

/* biostime.S */
int	usleep __P((u_long));
#endif
