/*	$OpenBSD: smpprobe.c,v 1.1 1998/02/24 22:07:46 weingart Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
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
 *	This product includes software developed by Tobias Weingartner.
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

#include <sys/param.h>
#include <machine/biosvar.h>
#include "libsa.h"

extern u_int cnvmem, extmem;
#define MP_FLOAT_SIG 0x5F504D5F		/* _MP_ */
#define MP_CONF_SIG 0x504D4350		/* PCMP */

typedef struct _mp_float {
	u_int32_t signature;
	u_int32_t conf_addr;
	u_int8_t length;
	u_int8_t spec_rev;
	u_int8_t checksum;
	u_int8_t feature[5];
} mp_float_t;


static int
mp_checksum(ptr, len)
	u_int8_t *ptr;
	int len;
{
	int i, sum = 0;

#ifdef DEBUG
	printf("Checksum %p for %d\n", ptr, len);
#endif

	for(i = 0; i < len; i++){
		sum += *(ptr + i);
	}
	sum &= 0xFF;

	return(!sum);
}


static mp_float_t *
mp_probefloat(ptr, len)
	u_int8_t *ptr;
	int len;
{
	mp_float_t *mpp = NULL;
	int i;

#ifdef DEBUG
	printf("Checking %p for %d\n", ptr, len);
#endif
	for(i = 0; i < 1024; i++){
		mp_float_t *tmp = (mp_float_t*)(ptr + i);
		if(tmp->signature == MP_FLOAT_SIG){
			printf("Found possible MP signature at: %p\n", ptr);

			mpp = tmp;
			break;
		}
		if((tmp->signature == MP_FLOAT_SIG) &&
			mp_checksum(tmp, tmp->length*16)){

#ifdef DEBUG
			printf("Found valid MP signature at: %p\n", ptr);
#endif
			mpp = tmp;
			break;
		}
	}

	return(mpp);
}


void
smpprobe()
{
	u_int32_t *ptr;
	u_int16_t *seg;
	mp_float_t *mp = NULL;

	/* Check EBDA */
	seg = (void *)((0x40 * 16) + 0x0E);		/* 40:0E */
	ptr = (void *)((*seg) * 16);			/* Segment => Linear */
	mp = mp_probefloat(ptr, 1024);

	/* Check BIOS ROM 0xE0000 - 0xFFFFF */
	ptr = (void *)(0xE0000);
	mp = mp_probefloat(ptr, 0x1FFFF);
	if(mp) goto end;

	/* Check last 1K of base RAM */
	ptr = (void *)(cnvmem * 1024);
	mp = mp_probefloat(ptr, 1024);
	if(mp) goto end;

	/* Check last 1K of extended RAM */
	ptr = (void *)(extmem * 1024 - 1024);		/* XXX */
	mp = mp_probefloat(ptr, 1024);
	if(mp) goto end;

	/* No valid MP signature found */
#if DEBUG
	printf("No valid MP signature found.\n");
#endif
	return;

	/* Valid MP signature found */
end:
	printf(" smp");
#if DEBUG
	printf("Floating Structure:\n");
	printf("\tSignature: %x\n", mp->signature);
	printf("\tConfig at: %x\n", mp->conf_addr);
	printf("\tLength: %d\n", mp->length);
	printf("\tRev: 1.%d\n", mp->spec_rev);
	printf("\tFeature: %x %x %x %x %x\n",
		mp->feature[0], mp->feature[1], mp->feature[2],
		mp->feature[3], mp->feature[4]);
#endif
}

