/*	$OpenBSD: biosdev.h,v 1.26 2002/03/14 01:26:34 millert Exp $	*/

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

struct consdev;
struct open_file;

/* biosdev.c */
extern const char *biosdevs[];
int biosstrategy(void *, int, daddr_t, size_t, void *, size_t *);
int biosopen(struct open_file *, ...);
int biosclose(struct open_file *);
int biosioctl(struct open_file *, u_long, void *);
int bios_getdiskinfo(int, bios_diskinfo_t *);
int biosd_io(int, int, int, int, int, int, void*);
const char * bios_getdisklabel(bios_diskinfo_t *, struct disklabel *);

/* diskprobe.c */
struct diskinfo *dklookup(int);
bios_diskinfo_t *bios_dklookup(int);

/* bioscons.c */
void pc_probe(struct consdev *);
void pc_init(struct consdev *);
int pc_getc(dev_t);
void pc_putc(dev_t, int);
void pc_pollc(dev_t, int);
void com_probe(struct consdev *);
void com_init(struct consdev *);
int comspeed(dev_t, int);
int com_getc(dev_t);
void com_putc(dev_t, int);
void com_pollc(dev_t, int);
