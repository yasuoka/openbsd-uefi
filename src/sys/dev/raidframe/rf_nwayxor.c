/*	$OpenBSD: rf_nwayxor.c,v 1.1 1999/01/11 14:29:31 niklas Exp $	*/
/*	$NetBSD: rf_nwayxor.c,v 1.1 1998/11/13 04:20:31 oster Exp $	*/
/*
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Mark Holland, Daniel Stodolsky
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/************************************************************
 *
 * nwayxor.c -- code to do N-way xors for reconstruction
 *
 * nWayXorN xors N input buffers into the destination buffer.
 * adapted from danner's longword_bxor code.
 *
 ************************************************************/

/* :  
 * Log: rf_nwayxor.c,v 
 * Revision 1.6  1996/06/12 03:31:18  jimz
 * only print call counts if rf_showXorCallCounts != 0
 *
 * Revision 1.5  1996/06/10  11:55:47  jimz
 * Straightened out some per-array/not-per-array distinctions, fixed
 * a couple bugs related to confusion. Added shutdown lists. Removed
 * layout shutdown function (now subsumed by shutdown lists).
 *
 * Revision 1.4  1996/06/02  17:31:48  jimz
 * Moved a lot of global stuff into array structure, where it belongs.
 * Fixed up paritylogging, pss modules in this manner. Some general
 * code cleanup. Removed lots of dead code, some dead files.
 *
 * Revision 1.3  1996/05/18  19:51:34  jimz
 * major code cleanup- fix syntax, make some types consistent,
 * add prototypes, clean out dead code, et cetera
 *
 * Revision 1.2  1995/12/01  19:29:14  root
 * added copyright info
 *
 */

#include "rf_nwayxor.h"
#include "rf_shutdown.h"

static int callcount[10];
static void rf_ShutdownNWayXor(void *);

static void rf_ShutdownNWayXor(ignored)
  void  *ignored;
{
  int i;

  if (rf_showXorCallCounts == 0)
    return;
  printf("Call counts for n-way xor routines:  ");
  for (i=0; i<10; i++)
    printf("%d ",callcount[i]);
  printf("\n");
}

int rf_ConfigureNWayXor(listp)
  RF_ShutdownList_t  **listp;
{
  int i, rc;

  for (i=0; i<10; i++)
    callcount[i] = 0;
  rc = rf_ShutdownCreate(listp, rf_ShutdownNWayXor, NULL);
  return(rc);
}

void rf_nWayXor1(src_rbs, dest_rb, len)
  RF_ReconBuffer_t  **src_rbs;
  RF_ReconBuffer_t   *dest_rb;
  int                 len;
{
  register unsigned long *src = (unsigned long *) src_rbs[0]->buffer;
  register unsigned long *dest= (unsigned long *) dest_rb->buffer;
  register unsigned long *end = src+len;
  register unsigned long d0, d1, d2, d3, s0, s1, s2, s3;

  callcount[1]++;
  while (len >= 4 )
    {
      d0 = dest[0];
      d1 = dest[1];
      d2 = dest[2];
      d3 = dest[3];
      s0 = src[0];
      s1 = src[1];
      s2 = src[2];
      s3 = src[3];
      dest[0] = d0 ^ s0;
      dest[1] = d1 ^ s1;
      dest[2] = d2 ^ s2;
      dest[3] = d3 ^ s3;
      src += 4;
      dest += 4;
      len -= 4;
    }
  while (src < end) {*dest++ ^=  *src++;}
}

void rf_nWayXor2(src_rbs, dest_rb, len)
  RF_ReconBuffer_t  **src_rbs;
  RF_ReconBuffer_t   *dest_rb;
  int                 len;
{
  register unsigned long *dst = (unsigned long *) dest_rb->buffer;
  register unsigned long *a   = dst;
  register unsigned long *b   = (unsigned long *) src_rbs[0]->buffer;
  register unsigned long *c   = (unsigned long *) src_rbs[1]->buffer;
  unsigned long a0,a1,a2,a3, b0,b1,b2,b3;
  
  callcount[2]++;
  /* align dest to cache line */
  while ((((unsigned long) dst) & 0x1f))
    {
      *dst++ = *a++ ^ *b++ ^ *c++;
      len--;
    }
  while (len > 4 )
    {
      a0 = a[0]; len -= 4;

      a1 = a[1];
      a2 = a[2];

      a3 = a[3];  a += 4;

      b0 = b[0];
      b1 = b[1];

      b2 = b[2];
      b3 = b[3];
      /* start dual issue */
      a0 ^= b0; b0 =  c[0];

      b += 4;  a1 ^= b1;

      a2 ^= b2; a3 ^= b3;

      b1 =  c[1]; a0 ^= b0;

      b2 =  c[2]; a1 ^= b1;

      b3 =  c[3]; a2 ^= b2;

      dst[0] = a0; a3 ^= b3;
      dst[1] = a1; c += 4;
      dst[2] = a2;
      dst[3] = a3; dst += 4;
    }
  while (len)
    {
      *dst++ = *a++ ^ *b++ ^ *c++;
      len--;
    }
}

/* note that first arg is not incremented but 2nd arg is */
#define LOAD_FIRST(_dst,_b) \
  a0 = _dst[0]; len -= 4;   \
  a1 = _dst[1];             \
  a2 = _dst[2];             \
  a3 = _dst[3];             \
  b0 = _b[0];               \
  b1 = _b[1];               \
  b2 = _b[2];               \
  b3 = _b[3];  _b += 4;

/* note: arg is incremented */
#define XOR_AND_LOAD_NEXT(_n) \
  a0 ^= b0; b0 = _n[0];       \
  a1 ^= b1; b1 = _n[1];       \
  a2 ^= b2; b2 = _n[2];       \
  a3 ^= b3; b3 = _n[3];       \
  _n += 4;

/* arg is incremented */
#define XOR_AND_STORE(_dst)       \
  a0 ^= b0; _dst[0] = a0;         \
  a1 ^= b1; _dst[1] = a1;         \
  a2 ^= b2; _dst[2] = a2;         \
  a3 ^= b3; _dst[3] = a3;         \
  _dst += 4;


void rf_nWayXor3(src_rbs, dest_rb, len)
  RF_ReconBuffer_t  **src_rbs;
  RF_ReconBuffer_t   *dest_rb;
  int                 len;
{
  register unsigned long *dst = (unsigned long *) dest_rb->buffer;
  register unsigned long *b   = (unsigned long *) src_rbs[0]->buffer;
  register unsigned long *c   = (unsigned long *) src_rbs[1]->buffer;
  register unsigned long *d   = (unsigned long *) src_rbs[2]->buffer;
  unsigned long a0,a1,a2,a3, b0,b1,b2,b3;
  
  callcount[3]++;
  /* align dest to cache line */
  while ((((unsigned long) dst) & 0x1f)) {
    *dst++ ^= *b++ ^ *c++ ^ *d++;
    len--;
  }
  while (len > 4 ) {
    LOAD_FIRST(dst,b);
    XOR_AND_LOAD_NEXT(c);
    XOR_AND_LOAD_NEXT(d);
    XOR_AND_STORE(dst);
  }
  while (len) {
    *dst++ ^=  *b++ ^ *c++ ^ *d++;
    len--;
  }
}

void rf_nWayXor4(src_rbs, dest_rb, len)
  RF_ReconBuffer_t  **src_rbs;
  RF_ReconBuffer_t   *dest_rb;
  int                 len;
{
  register unsigned long *dst = (unsigned long *) dest_rb->buffer;
  register unsigned long *b   = (unsigned long *) src_rbs[0]->buffer;
  register unsigned long *c   = (unsigned long *) src_rbs[1]->buffer;
  register unsigned long *d   = (unsigned long *) src_rbs[2]->buffer;
  register unsigned long *e   = (unsigned long *) src_rbs[3]->buffer;
  unsigned long a0,a1,a2,a3, b0,b1,b2,b3;
  
  callcount[4]++;
  /* align dest to cache line */
  while ((((unsigned long) dst) & 0x1f)) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++;
    len--;
  }
  while (len > 4 ) {
    LOAD_FIRST(dst,b);
    XOR_AND_LOAD_NEXT(c);
    XOR_AND_LOAD_NEXT(d);
    XOR_AND_LOAD_NEXT(e);
    XOR_AND_STORE(dst);
  }
  while (len) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++;
    len--;
  }
}

void rf_nWayXor5(src_rbs, dest_rb, len)
  RF_ReconBuffer_t  **src_rbs;
  RF_ReconBuffer_t   *dest_rb;
  int                 len;
{
  register unsigned long *dst = (unsigned long *) dest_rb->buffer;
  register unsigned long *b   = (unsigned long *) src_rbs[0]->buffer;
  register unsigned long *c   = (unsigned long *) src_rbs[1]->buffer;
  register unsigned long *d   = (unsigned long *) src_rbs[2]->buffer;
  register unsigned long *e   = (unsigned long *) src_rbs[3]->buffer;
  register unsigned long *f   = (unsigned long *) src_rbs[4]->buffer;
  unsigned long a0,a1,a2,a3, b0,b1,b2,b3;
  
  callcount[5]++;
  /* align dest to cache line */
  while ((((unsigned long) dst) & 0x1f)) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++ ^ *f++;
    len--;
  }
  while (len > 4 ) {
    LOAD_FIRST(dst,b);
    XOR_AND_LOAD_NEXT(c);
    XOR_AND_LOAD_NEXT(d);
    XOR_AND_LOAD_NEXT(e);
    XOR_AND_LOAD_NEXT(f);
    XOR_AND_STORE(dst);
  }
  while (len) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++ ^ *f++;
    len--;
  }
}

void rf_nWayXor6(src_rbs, dest_rb, len)
  RF_ReconBuffer_t  **src_rbs;
  RF_ReconBuffer_t   *dest_rb;
  int                 len;
{
  register unsigned long *dst = (unsigned long *) dest_rb->buffer;
  register unsigned long *b   = (unsigned long *) src_rbs[0]->buffer;
  register unsigned long *c   = (unsigned long *) src_rbs[1]->buffer;
  register unsigned long *d   = (unsigned long *) src_rbs[2]->buffer;
  register unsigned long *e   = (unsigned long *) src_rbs[3]->buffer;
  register unsigned long *f   = (unsigned long *) src_rbs[4]->buffer;
  register unsigned long *g   = (unsigned long *) src_rbs[5]->buffer;
  unsigned long a0,a1,a2,a3, b0,b1,b2,b3;
  
  callcount[6]++;
  /* align dest to cache line */
  while ((((unsigned long) dst) & 0x1f)) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++ ^ *f++ ^ *g++;
    len--;
  }
  while (len > 4 ) {
    LOAD_FIRST(dst,b);
    XOR_AND_LOAD_NEXT(c);
    XOR_AND_LOAD_NEXT(d);
    XOR_AND_LOAD_NEXT(e);
    XOR_AND_LOAD_NEXT(f);
    XOR_AND_LOAD_NEXT(g);
    XOR_AND_STORE(dst);
  }
  while (len) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++ ^ *f++ ^ *g++;
    len--;
  }
}

void rf_nWayXor7(src_rbs, dest_rb, len)
  RF_ReconBuffer_t  **src_rbs;
  RF_ReconBuffer_t   *dest_rb;
  int                 len;
{
  register unsigned long *dst = (unsigned long *) dest_rb->buffer;
  register unsigned long *b   = (unsigned long *) src_rbs[0]->buffer;
  register unsigned long *c   = (unsigned long *) src_rbs[1]->buffer;
  register unsigned long *d   = (unsigned long *) src_rbs[2]->buffer;
  register unsigned long *e   = (unsigned long *) src_rbs[3]->buffer;
  register unsigned long *f   = (unsigned long *) src_rbs[4]->buffer;
  register unsigned long *g   = (unsigned long *) src_rbs[5]->buffer;
  register unsigned long *h   = (unsigned long *) src_rbs[6]->buffer;
  unsigned long a0,a1,a2,a3, b0,b1,b2,b3;
  
  callcount[7]++;
  /* align dest to cache line */
  while ((((unsigned long) dst) & 0x1f)) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++ ^ *f++ ^ *g++ ^ *h++;
    len--;
  }
  while (len > 4 ) {
    LOAD_FIRST(dst,b);
    XOR_AND_LOAD_NEXT(c);
    XOR_AND_LOAD_NEXT(d);
    XOR_AND_LOAD_NEXT(e);
    XOR_AND_LOAD_NEXT(f);
    XOR_AND_LOAD_NEXT(g);
    XOR_AND_LOAD_NEXT(h);
    XOR_AND_STORE(dst);
  }
  while (len) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++ ^ *f++ ^ *g++ ^ *h++;
    len--;
  }
}

void rf_nWayXor8(src_rbs, dest_rb, len)
  RF_ReconBuffer_t  **src_rbs;
  RF_ReconBuffer_t   *dest_rb;
  int                 len;
{
  register unsigned long *dst = (unsigned long *) dest_rb->buffer;
  register unsigned long *b   = (unsigned long *) src_rbs[0]->buffer;
  register unsigned long *c   = (unsigned long *) src_rbs[1]->buffer;
  register unsigned long *d   = (unsigned long *) src_rbs[2]->buffer;
  register unsigned long *e   = (unsigned long *) src_rbs[3]->buffer;
  register unsigned long *f   = (unsigned long *) src_rbs[4]->buffer;
  register unsigned long *g   = (unsigned long *) src_rbs[5]->buffer;
  register unsigned long *h   = (unsigned long *) src_rbs[6]->buffer;
  register unsigned long *i   = (unsigned long *) src_rbs[7]->buffer;
  unsigned long a0,a1,a2,a3, b0,b1,b2,b3;
  
  callcount[8]++;
  /* align dest to cache line */
  while ((((unsigned long) dst) & 0x1f)) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++ ^ *f++ ^ *g++ ^ *h++ ^ *i++;
    len--;
  }
  while (len > 4 ) {
    LOAD_FIRST(dst,b);
    XOR_AND_LOAD_NEXT(c);
    XOR_AND_LOAD_NEXT(d);
    XOR_AND_LOAD_NEXT(e);
    XOR_AND_LOAD_NEXT(f);
    XOR_AND_LOAD_NEXT(g);
    XOR_AND_LOAD_NEXT(h);
    XOR_AND_LOAD_NEXT(i);
    XOR_AND_STORE(dst);
  }
  while (len) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++ ^ *f++ ^ *g++ ^ *h++ ^ *i++;
    len--;
  }
}


void rf_nWayXor9(src_rbs, dest_rb, len)
  RF_ReconBuffer_t  **src_rbs;
  RF_ReconBuffer_t   *dest_rb;
  int                 len;
{
  register unsigned long *dst = (unsigned long *) dest_rb->buffer;
  register unsigned long *b   = (unsigned long *) src_rbs[0]->buffer;
  register unsigned long *c   = (unsigned long *) src_rbs[1]->buffer;
  register unsigned long *d   = (unsigned long *) src_rbs[2]->buffer;
  register unsigned long *e   = (unsigned long *) src_rbs[3]->buffer;
  register unsigned long *f   = (unsigned long *) src_rbs[4]->buffer;
  register unsigned long *g   = (unsigned long *) src_rbs[5]->buffer;
  register unsigned long *h   = (unsigned long *) src_rbs[6]->buffer;
  register unsigned long *i   = (unsigned long *) src_rbs[7]->buffer;
  register unsigned long *j   = (unsigned long *) src_rbs[8]->buffer;
  unsigned long a0,a1,a2,a3, b0,b1,b2,b3;
  
  callcount[9]++;
  /* align dest to cache line */
  while ((((unsigned long) dst) & 0x1f)) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++ ^ *f++ ^ *g++ ^ *h++ ^ *i++ ^ *j++;
    len--;
  }
  while (len > 4 ) {
    LOAD_FIRST(dst,b);
    XOR_AND_LOAD_NEXT(c);
    XOR_AND_LOAD_NEXT(d);
    XOR_AND_LOAD_NEXT(e);
    XOR_AND_LOAD_NEXT(f);
    XOR_AND_LOAD_NEXT(g);
    XOR_AND_LOAD_NEXT(h);
    XOR_AND_LOAD_NEXT(i);
    XOR_AND_LOAD_NEXT(j);
    XOR_AND_STORE(dst);
  }
  while (len) {
    *dst++ ^= *b++ ^ *c++ ^ *d++ ^ *e++ ^ *f++ ^ *g++ ^ *h++ ^ *i++ ^ *j++;
    len--;
  }
}
