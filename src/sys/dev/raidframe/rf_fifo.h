/*	$OpenBSD: rf_fifo.h,v 1.3 2002/12/16 07:01:04 tdeval Exp $	*/
/*	$NetBSD: rf_fifo.h,v 1.3 1999/02/05 00:06:11 oster Exp $	*/

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

/*
 * rf_fifo.h --  prioritized FIFO queue code.
 *
 * 4-9-93 Created (MCH)
 */


#ifndef	_RF__RF_FIFO_H_
#define	_RF__RF_FIFO_H_

#include "rf_archs.h"
#include "rf_types.h"
#include "rf_diskqueue.h"

typedef struct RF_FifoHeader_s {
	RF_DiskQueueData_t *hq_head, *hq_tail;	/* high priority requests */
	RF_DiskQueueData_t *lq_head, *lq_tail;	/* low priority requests */
	int		    hq_count, lq_count;	/* debug only */
}	RF_FifoHeader_t;

extern void *rf_FifoCreate(RF_SectorCount_t, RF_AllocListElem_t *,
	RF_ShutdownList_t **);
extern void rf_FifoEnqueue(void *, RF_DiskQueueData_t *, int);
extern RF_DiskQueueData_t *rf_FifoDequeue(void *);
extern RF_DiskQueueData_t *rf_FifoPeek(void *);
extern int rf_FifoPromote(void *, RF_StripeNum_t, RF_ReconUnitNum_t);

#endif	/* !_RF__RF_FIFO_H_ */
