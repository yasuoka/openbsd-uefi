/*	$OpenBSD: rf_memchunk.h,v 1.3 2002/12/16 07:01:04 tdeval Exp $	*/
/*	$NetBSD: rf_memchunk.h,v 1.3 1999/02/05 00:06:13 oster Exp $	*/

/*
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Mark Holland
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

/* Header file for rf_memchunk.c. See comments there. */

#ifndef	_RF__RF_MEMCHUNK_H_
#define	_RF__RF_MEMCHUNK_H_

#include "rf_types.h"

struct RF_ChunkDesc_s {
	int		 size;
	int		 reuse_count;
	char		*buf;
	RF_ChunkDesc_t	*next;
};

int  rf_ConfigureMemChunk(RF_ShutdownList_t **);
RF_ChunkDesc_t *rf_GetMemChunk(int);
void rf_ReleaseMemChunk(RF_ChunkDesc_t *);

#endif	/* !_RF__RF_MEMCHUNK_H_ */
