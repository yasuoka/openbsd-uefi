/*	$OpenBSD: sboot.h,v 1.5 1997/01/29 07:58:39 deraadt Exp $ */

/*
 * Copyright (c) 1995 Charles D. Cranor and Seth Widoff
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
 *      This product includes software developed by Charles D. Cranor
 *	and Seth Widoff.
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

/*
 * sboot.h: stuff for MVME147's serial line boot
 */

extern caddr_t end;

/* console */
void puts __P((char *));
char cngetc __P((void));
char *ngets __P((char *, int));

/* sboot */
void callrom __P((void));
void do_cmd __P((char *, char*));

/* le */
#define LANCE_ADDR 0xfffe0778
#define ERAM_ADDR  0xfffe0774
#define LANCE_REG_ADDR 0xfffe1800
void le_end __P((void));
void le_init __P((void));
int le_get __P((u_char *, size_t, u_long));
int le_put __P((u_char *, size_t));

/* etherfun */
#define READ 0
#define ACKN 1 
void do_rev_arp __P((void));
int get_rev_arp __P((void));
int rev_arp __P((void));
void do_send_tftp __P((int));
int do_get_file __P((void)); 
void tftp_file __P((char *, u_long));

/* clock */
u_long ttime __P((void));

/* checksum */
u_long oc_cksum __P((void *, u_long, u_long));

#define CONS_ZS_ADDR (0xfffe3002)
#define CLOCK_ADDR (0xfffe07f8)
#define LOAD_ADDR 0x7000 

unsigned char myea[6];                /* my ether addr */
unsigned char myip[4];
unsigned char servip[4];
unsigned char servea[6];
u_short myport;
u_short servport;
unsigned char reboot;
