/*	$OpenBSD: rf_shutdown.c,v 1.1 1999/01/11 14:29:49 niklas Exp $	*/
/*	$NetBSD: rf_shutdown.c,v 1.1 1998/11/13 04:20:34 oster Exp $	*/
/*
 * rf_shutdown.c
 */
/*
 * Copyright (c) 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Jim Zelenka
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
 * Maintain lists of cleanup functions. Also, mechanisms for coordinating
 * thread startup and shutdown.
 */

#include "rf_types.h"
#include "rf_threadstuff.h"
#include "rf_shutdown.h"
#include "rf_debugMem.h"
#include "rf_freelist.h"
#include "rf_threadid.h"

static void rf_FreeShutdownEnt(RF_ShutdownList_t *ent)
{
#ifdef KERNEL
  FREE(ent, M_DEVBUF);
#else /* KERNEL */
  free(ent);
#endif /* KERNEL */
}

int _rf_ShutdownCreate(
  RF_ShutdownList_t  **listp,
  void               (*cleanup)(void *arg),
  void                *arg,
  char                *file,
  int                  line)
{
  RF_ShutdownList_t *ent;

  /*
   * Have to directly allocate memory here, since we start up before
   * and shutdown after RAIDframe internal allocation system.
   */
#ifdef KERNEL
  ent = (RF_ShutdownList_t *)malloc( sizeof(RF_ShutdownList_t), M_DEVBUF, M_WAITOK);
#if 0
  MALLOC(ent, RF_ShutdownList_t *, sizeof(RF_ShutdownList_t), M_DEVBUF, M_WAITOK);
#endif
#else /* KERNEL */
  ent = (RF_ShutdownList_t *)malloc(sizeof(RF_ShutdownList_t));
#endif /* KERNEL */
  if (ent == NULL)
    return(ENOMEM);
  ent->cleanup = cleanup;
  ent->arg = arg;
  ent->file = file;
  ent->line = line;
  ent->next = *listp;
  *listp = ent;
  return(0);
}

int rf_ShutdownList(RF_ShutdownList_t **list)
{
  RF_ShutdownList_t *r, *next;
  char *file;
  int line;

  for(r=*list;r;r=next) {
    next = r->next;
    file = r->file;
    line = r->line;

    if (rf_shutdownDebug) {
      int tid;
      rf_get_threadid(tid);
      printf("[%d] call shutdown, created %s:%d\n", tid, file, line);
    }

    r->cleanup(r->arg);

    if (rf_shutdownDebug) {
      int tid;
      rf_get_threadid(tid);
      printf("[%d] completed shutdown, created %s:%d\n", tid, file, line);
    }

    rf_FreeShutdownEnt(r);
  }
  *list = NULL;
  return(0);
}
