/*	$OpenBSD: rf_revent.c,v 1.5 1999/08/02 15:42:48 peter Exp $	*/
/*	$NetBSD: rf_revent.c,v 1.4 1999/03/14 21:53:31 oster Exp $	*/
/*
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author:
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
 * revent.c -- reconstruction event handling code
 */

#include <sys/errno.h>

#include "rf_raid.h"
#include "rf_revent.h"
#include "rf_etimer.h"
#include "rf_general.h"
#include "rf_freelist.h"
#include "rf_desc.h"
#include "rf_shutdown.h"

#include <sys/kernel.h>

static RF_FreeList_t *rf_revent_freelist;
#define RF_MAX_FREE_REVENT 128
#define RF_REVENT_INC        8
#define RF_REVENT_INITIAL    8



#include <sys/proc.h>

extern int hz;

#define DO_WAIT(_rc)   tsleep(&(_rc)->eventQueue, PRIBIO, "raidframe eventq", 0)

#define DO_SIGNAL(_rc)     wakeup(&(_rc)->eventQueue)


static void rf_ShutdownReconEvent(void *);

static RF_ReconEvent_t *
GetReconEventDesc(RF_RowCol_t row, RF_RowCol_t col,
    void *arg, RF_Revent_t type);
RF_ReconEvent_t *
rf_GetNextReconEvent(RF_RaidReconDesc_t *,
    RF_RowCol_t, void (*continueFunc) (void *),
    void *);

static void
rf_ShutdownReconEvent(ignored)
void   *ignored;
{
	RF_FREELIST_DESTROY(rf_revent_freelist, next, (RF_ReconEvent_t *));
}

int 
rf_ConfigureReconEvent(listp)
	RF_ShutdownList_t **listp;
{
	int     rc;

	RF_FREELIST_CREATE(rf_revent_freelist, RF_MAX_FREE_REVENT,
	    RF_REVENT_INC, sizeof(RF_ReconEvent_t));
	if (rf_revent_freelist == NULL)
		return (ENOMEM);
	rc = rf_ShutdownCreate(listp, rf_ShutdownReconEvent, NULL);
	if (rc) {
		RF_ERRORMSG3("Unable to add to shutdown list file %s line %d rc=%d\n", __FILE__,
		    __LINE__, rc);
		rf_ShutdownReconEvent(NULL);
		return (rc);
	}
	RF_FREELIST_PRIME(rf_revent_freelist, RF_REVENT_INITIAL, next,
	    (RF_ReconEvent_t *));
	return (0);
}
/* returns the next reconstruction event, blocking the calling thread until
 * one becomes available
 */

/* will now return null if it is blocked or will return an event if it is not */

RF_ReconEvent_t *
rf_GetNextReconEvent(reconDesc, row, continueFunc, continueArg)
	RF_RaidReconDesc_t *reconDesc;
	RF_RowCol_t row;
	void    (*continueFunc) (void *);
	void   *continueArg;
{
	int	s;

	RF_Raid_t *raidPtr = reconDesc->raidPtr;
	RF_ReconCtrl_t *rctrl = raidPtr->reconControl[row];
	RF_ReconEvent_t *event;

	RF_ASSERT(row >= 0 && row <= raidPtr->numRow);
	RF_LOCK_MUTEX(rctrl->eq_mutex);
	RF_ASSERT((rctrl->eventQueue == NULL) == (rctrl->eq_count == 0));	/* q null and count==0
										 * must be equivalent
										 * conditions */


	rctrl->continueFunc = continueFunc;
	rctrl->continueArg = continueArg;

/* start with a simple premise. Allow 100 ms for recon, and then
 * sleep for the 2 ms to allow use of the CPU. This resulted in a CPU
 * utilisation of about 25% locally, and a very responsive system - PMG
 */
#define	RECON_TIME	((100 * hz) / 1000)

	if (reconDesc->reconExecTimerRunning) {
		int     status;
		RF_int64 ticks;

		s = splclock();
		ticks = (mono_time.tv_sec * 1000000 + mono_time.tv_usec) -
			reconDesc->reconExecTimerRunning;
		splx(s);

		if (ticks >= (1000000 / RECON_TIME)) {
			/* we've been running too long.  delay for RECON_TIME */
#if RF_RECON_STATS > 0
			reconDesc->numReconExecDelays++;
#endif /* RF_RECON_STATS > 0 */
			status = tsleep(&reconDesc->reconExecTicks, PRIBIO, "recon delay", RECON_TIME / 5);
			RF_ASSERT(status == EWOULDBLOCK);
		}
	}

	while (!rctrl->eventQueue) {
#if RF_RECON_STATS > 0
		reconDesc->numReconEventWaits++;
#endif				/* RF_RECON_STATS > 0 */
		DO_WAIT(rctrl);
	}
	s = splclock();
	/* set time to now */
	reconDesc->reconExecTimerRunning = mono_time.tv_sec * 1000000 +
					   mono_time.tv_usec;
	splx(s);

	event = rctrl->eventQueue;
	rctrl->eventQueue = event->next;
	event->next = NULL;
	rctrl->eq_count--;
	RF_ASSERT((rctrl->eventQueue == NULL) == (rctrl->eq_count == 0));	/* q null and count==0
										 * must be equivalent
										 * conditions */
	RF_UNLOCK_MUTEX(rctrl->eq_mutex);
	return (event);
}
/* enqueues a reconstruction event on the indicated queue */
void 
rf_CauseReconEvent(raidPtr, row, col, arg, type)
	RF_Raid_t *raidPtr;
	RF_RowCol_t row;
	RF_RowCol_t col;
	void   *arg;
	RF_Revent_t type;
{
	RF_ReconCtrl_t *rctrl = raidPtr->reconControl[row];
	RF_ReconEvent_t *event = GetReconEventDesc(row, col, arg, type);

	if (type == RF_REVENT_BUFCLEAR) {
		RF_ASSERT(col != rctrl->fcol);
	}
	RF_ASSERT(row >= 0 && row <= raidPtr->numRow && col >= 0 && col <= raidPtr->numCol);
	RF_LOCK_MUTEX(rctrl->eq_mutex);
	RF_ASSERT((rctrl->eventQueue == NULL) == (rctrl->eq_count == 0));	/* q null and count==0
										 * must be equivalent
										 * conditions */
	event->next = rctrl->eventQueue;
	rctrl->eventQueue = event;
	rctrl->eq_count++;
	RF_UNLOCK_MUTEX(rctrl->eq_mutex);

	DO_SIGNAL(rctrl);
}
/* allocates and initializes a recon event descriptor */
static RF_ReconEvent_t *
GetReconEventDesc(row, col, arg, type)
	RF_RowCol_t row;
	RF_RowCol_t col;
	void   *arg;
	RF_Revent_t type;
{
	RF_ReconEvent_t *t;

	RF_FREELIST_GET(rf_revent_freelist, t, next, (RF_ReconEvent_t *));
	if (t == NULL)
		return (NULL);
	t->col = col;
	t->arg = arg;
	t->type = type;
	return (t);
}

void 
rf_FreeReconEventDesc(event)
	RF_ReconEvent_t *event;
{
	RF_FREELIST_FREE(rf_revent_freelist, event, next);
}
