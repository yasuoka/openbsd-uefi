/*	$OpenBSD: rf_paritylogging.c,v 1.1 1999/01/11 14:29:35 niklas Exp $	*/
/*	$NetBSD: rf_paritylogging.c,v 1.1 1998/11/13 04:20:32 oster Exp $	*/
/*
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: William V. Courtright II
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

/* :  
 * Log: rf_paritylogging.c,v 
 * Revision 1.42  1996/11/05 21:10:40  jimz
 * failed pda generalization
 *
 * Revision 1.41  1996/07/31  16:56:18  jimz
 * dataBytesPerStripe, sectorsPerDisk init arch-indep.
 *
 * Revision 1.40  1996/07/28  20:31:39  jimz
 * i386netbsd port
 * true/false fixup
 *
 * Revision 1.39  1996/07/18  22:57:14  jimz
 * port simulator to AIX
 *
 * Revision 1.38  1996/07/13  00:00:59  jimz
 * sanitized generalized reconstruction architecture
 * cleaned up head sep, rbuf problems
 *
 * Revision 1.37  1996/06/17  03:24:14  jimz
 * switch to new shutdown function typing
 *
 * Revision 1.36  1996/06/14  23:15:38  jimz
 * attempt to deal with thread GC problem
 *
 * Revision 1.35  1996/06/11  13:48:30  jimz
 * get it to compile in-kernel
 *
 * Revision 1.34  1996/06/11  10:16:35  jimz
 * Check return values on array configuration- back out if failed.
 * Reorder shutdown to avoid using deallocated resources.
 * Get rid of bogus join op in shutdown.
 *
 * Revision 1.33  1996/06/10  18:29:17  wvcii
 * fixed bug in rf_IdentifyStripeParityLogging
 * - added array initialization
 *
 * Revision 1.32  1996/06/10  11:55:47  jimz
 * Straightened out some per-array/not-per-array distinctions, fixed
 * a couple bugs related to confusion. Added shutdown lists. Removed
 * layout shutdown function (now subsumed by shutdown lists).
 *
 * Revision 1.31  1996/06/07  22:26:27  jimz
 * type-ify which_ru (RF_ReconUnitNum_t)
 *
 * Revision 1.30  1996/06/07  21:33:04  jimz
 * begin using consistent types for sector numbers,
 * stripe numbers, row+col numbers, recon unit numbers
 *
 * Revision 1.29  1996/06/05  18:06:02  jimz
 * Major code cleanup. The Great Renaming is now done.
 * Better modularity. Better typing. Fixed a bunch of
 * synchronization bugs. Made a lot of global stuff
 * per-desc or per-array. Removed dead code.
 *
 * Revision 1.28  1996/06/03  23:28:26  jimz
 * more bugfixes
 * check in tree to sync for IPDS runs with current bugfixes
 * there still may be a problem with threads in the script test
 * getting I/Os stuck- not trivially reproducible (runs ~50 times
 * in a row without getting stuck)
 *
 * Revision 1.27  1996/06/02  17:31:48  jimz
 * Moved a lot of global stuff into array structure, where it belongs.
 * Fixed up paritylogging, pss modules in this manner. Some general
 * code cleanup. Removed lots of dead code, some dead files.
 *
 * Revision 1.26  1996/05/31  22:26:54  jimz
 * fix a lot of mapping problems, memory allocation problems
 * found some weird lock issues, fixed 'em
 * more code cleanup
 *
 * Revision 1.25  1996/05/30  23:22:16  jimz
 * bugfixes of serialization, timing problems
 * more cleanup
 *
 * Revision 1.24  1996/05/27  18:56:37  jimz
 * more code cleanup
 * better typing
 * compiles in all 3 environments
 *
 * Revision 1.23  1996/05/24  22:17:04  jimz
 * continue code + namespace cleanup
 * typed a bunch of flags
 *
 * Revision 1.22  1996/05/24  01:59:45  jimz
 * another checkpoint in code cleanup for release
 * time to sync kernel tree
 *
 * Revision 1.21  1996/05/23  21:46:35  jimz
 * checkpoint in code cleanup (release prep)
 * lots of types, function names have been fixed
 *
 * Revision 1.20  1996/05/23  00:33:23  jimz
 * code cleanup: move all debug decls to rf_options.c, all extern
 * debug decls to rf_options.h, all debug vars preceded by rf_
 *
 * Revision 1.19  1996/05/20  16:16:30  jimz
 * switch to rf_{mutex,cond}_{init,destroy}
 *
 * Revision 1.18  1996/05/18  19:51:34  jimz
 * major code cleanup- fix syntax, make some types consistent,
 * add prototypes, clean out dead code, et cetera
 *
 * Revision 1.17  1996/05/03  19:47:11  wvcii
 * added includes of new dag library
 *
 * Revision 1.16  1995/12/12  18:10:06  jimz
 * MIN -> RF_MIN, MAX -> RF_MAX, ASSERT -> RF_ASSERT
 * fix 80-column brain damage in comments
 *
 * Revision 1.15  1995/12/06  20:57:43  wvcii
 * added prototypes
 * reintegration of logs on shutdown now conditional on forceParityLogReint
 *
 * Revision 1.14  1995/11/30  16:06:42  wvcii
 * added copyright info
 *
 * Revision 1.13  1995/11/17  19:01:29  wvcii
 * added prototyping to MapParity
 *
 * Revision 1.12  1995/11/07  15:36:03  wvcii
 * changed ParityLoggingDagSelect prototype
 * function no longer returns numHdrSucc, numTermAnt
 *
 * Revision 1.11  1995/10/08  20:42:54  wvcii
 * lots of random debugging - debugging incomplete
 *
 * Revision 1.10  1995/09/07  01:26:55  jimz
 * Achive basic compilation in kernel. Kernel functionality
 * is not guaranteed at all, but it'll compile. Mostly. I hope.
 *
 * Revision 1.9  1995/09/06  19:21:17  wvcii
 * explicit shutdown (forced reintegration) for simulator version
 *
 * Revision 1.8  1995/07/08  18:19:16  rachad
 * Parity verifies can not be done in the simulator.
 *
 * Revision 1.7  1995/07/07  00:17:20  wvcii
 * this version free from deadlock, fails parity verification
 *
 * Revision 1.6  1995/06/23  13:39:59  robby
 * updeated to prototypes in rf_layout.h
 *
 * Revision 1.5  1995/06/09  13:14:56  wvcii
 * code is now nonblocking
 *
 * Revision 1.4  95/06/01  17:02:23  wvcii
 * code debug
 * 
 * Revision 1.3  95/05/31  13:08:57  wvcii
 * code debug
 * 
 * Revision 1.2  95/05/21  15:35:00  wvcii
 * code debug
 * 
 *
 *
 */

/*
  parity logging configuration, dag selection, and mapping is implemented here
 */

#include "rf_archs.h"

#if RF_INCLUDE_PARITYLOGGING > 0

#include "rf_types.h"
#include "rf_raid.h"
#include "rf_dag.h"
#include "rf_dagutils.h"
#include "rf_dagfuncs.h"
#include "rf_dagffrd.h"
#include "rf_dagffwr.h"
#include "rf_dagdegrd.h"
#include "rf_dagdegwr.h"
#include "rf_threadid.h"
#include "rf_paritylog.h"
#include "rf_paritylogDiskMgr.h"
#include "rf_paritylogging.h"
#include "rf_parityloggingdags.h"
#include "rf_general.h"
#include "rf_map.h"
#include "rf_utils.h"
#include "rf_shutdown.h"

typedef struct RF_ParityLoggingConfigInfo_s {
  RF_RowCol_t  **stripeIdentifier;  /* filled in at config time & used by IdentifyStripe */
} RF_ParityLoggingConfigInfo_t;

static void FreeRegionInfo(RF_Raid_t *raidPtr, RF_RegionId_t regionID);
static void rf_ShutdownParityLogging(RF_ThreadArg_t arg);
static void rf_ShutdownParityLoggingRegionInfo(RF_ThreadArg_t arg);
static void rf_ShutdownParityLoggingPool(RF_ThreadArg_t arg);
static void rf_ShutdownParityLoggingRegionBufferPool(RF_ThreadArg_t arg);
static void rf_ShutdownParityLoggingParityBufferPool(RF_ThreadArg_t arg);
static void rf_ShutdownParityLoggingDiskQueue(RF_ThreadArg_t arg);

int rf_ConfigureParityLogging(
  RF_ShutdownList_t  **listp,
  RF_Raid_t           *raidPtr,
  RF_Config_t         *cfgPtr)
{
  int i, j, startdisk, rc;
  RF_SectorCount_t totalLogCapacity, fragmentation, lastRegionCapacity;
  RF_SectorCount_t parityBufferCapacity, maxRegionParityRange;
  RF_RaidLayout_t *layoutPtr = &raidPtr->Layout;
  RF_ParityLoggingConfigInfo_t *info;
  RF_ParityLog_t *l=NULL, *next;
  caddr_t lHeapPtr;

  /*
   * We create multiple entries on the shutdown list here, since
   * this configuration routine is fairly complicated in and of
   * itself, and this makes backing out of a failed configuration
   * much simpler.
   */

  raidPtr->numSectorsPerLog = RF_DEFAULT_NUM_SECTORS_PER_LOG;

  /* create a parity logging configuration structure */
  RF_MallocAndAdd(info, sizeof(RF_ParityLoggingConfigInfo_t), (RF_ParityLoggingConfigInfo_t *), raidPtr->cleanupList);
  if (info == NULL)
    return(ENOMEM);
  layoutPtr->layoutSpecificInfo = (void *) info;

  RF_ASSERT(raidPtr->numRow == 1);
  
  /* the stripe identifier must identify the disks in each stripe,
   * IN THE ORDER THAT THEY APPEAR IN THE STRIPE.
   */
  info->stripeIdentifier = rf_make_2d_array((raidPtr->numCol), (raidPtr->numCol), raidPtr->cleanupList);
  if (info->stripeIdentifier == NULL)
    return(ENOMEM);

  startdisk = 0;
  for (i=0; i<(raidPtr->numCol); i++)
    {
      for (j=0; j<(raidPtr->numCol); j++)
	{
	  info->stripeIdentifier[i][j] = (startdisk + j) % (raidPtr->numCol - 1);
	}
      if ((--startdisk) < 0)
	startdisk = raidPtr->numCol-1-1;
    }

  /* fill in the remaining layout parameters */
  layoutPtr->numStripe = layoutPtr->stripeUnitsPerDisk;
  layoutPtr->bytesPerStripeUnit = layoutPtr->sectorsPerStripeUnit << raidPtr->logBytesPerSector;
  layoutPtr->numParityCol = 1;
  layoutPtr->numParityLogCol = 1;
  layoutPtr->numDataCol = raidPtr->numCol - layoutPtr->numParityCol - layoutPtr->numParityLogCol;
  layoutPtr->dataSectorsPerStripe = layoutPtr->numDataCol * layoutPtr->sectorsPerStripeUnit;
  layoutPtr->dataStripeUnitsPerDisk = layoutPtr->stripeUnitsPerDisk;
  raidPtr->sectorsPerDisk = layoutPtr->stripeUnitsPerDisk * layoutPtr->sectorsPerStripeUnit;

  raidPtr->totalSectors = layoutPtr->stripeUnitsPerDisk * layoutPtr->numDataCol * layoutPtr->sectorsPerStripeUnit;

  /* configure parity log parameters

     parameter               comment/constraints
     ----------------        -------------------
   * numParityRegions        all regions (except possibly last) of equal size
   * totalInCoreLogCapacity  amount of memory in bytes available for in-core logs (default 1 MB)
   # numSectorsPerLog        capacity of an in-core log in sectors (1 disk track)
     numParityLogs           total number of in-core logs, should be at least numParityRegions
     regionLogCapacity       size of a region log (except possibly last one) in sectors
     totalLogCapacity        total amount of log space in sectors

   * denotes a user settable parameter.
   # logs are fixed to be the size of a disk track, value #defined in rf_paritylog.h

  */

  totalLogCapacity = layoutPtr->stripeUnitsPerDisk * layoutPtr->sectorsPerStripeUnit * layoutPtr->numParityLogCol;
  raidPtr->regionLogCapacity = totalLogCapacity / rf_numParityRegions;
  if (rf_parityLogDebug)
    printf("bytes per sector %d\n", raidPtr->bytesPerSector);

  /* reduce fragmentation within a disk region by adjusting the number of regions
     in an attempt to allow an integral number of logs to fit into a disk region */
  fragmentation = raidPtr->regionLogCapacity % raidPtr->numSectorsPerLog;
  if (fragmentation > 0)
    for (i = 1; i < (raidPtr->numSectorsPerLog / 2); i++)
      {
	if (((totalLogCapacity / (rf_numParityRegions + i)) % raidPtr->numSectorsPerLog) < fragmentation)
	  {
	    rf_numParityRegions++;
	    raidPtr->regionLogCapacity = totalLogCapacity / rf_numParityRegions;
	    fragmentation = raidPtr->regionLogCapacity % raidPtr->numSectorsPerLog;
	  }
	if (((totalLogCapacity / (rf_numParityRegions - i)) % raidPtr->numSectorsPerLog) < fragmentation)
	  {
	    rf_numParityRegions--;
	    raidPtr->regionLogCapacity = totalLogCapacity / rf_numParityRegions;
	    fragmentation = raidPtr->regionLogCapacity % raidPtr->numSectorsPerLog;
	  }
      }
  /* ensure integral number of regions per log */
  raidPtr->regionLogCapacity = (raidPtr->regionLogCapacity / raidPtr->numSectorsPerLog) * raidPtr->numSectorsPerLog;

  raidPtr->numParityLogs = rf_totalInCoreLogCapacity / (raidPtr->bytesPerSector * raidPtr->numSectorsPerLog);
  /* to avoid deadlock, must ensure that enough logs exist for each region to have one simultaneously */
  if (raidPtr->numParityLogs < rf_numParityRegions)
    raidPtr->numParityLogs = rf_numParityRegions;

  /* create region information structs */
  RF_Malloc(raidPtr->regionInfo, (rf_numParityRegions * sizeof(RF_RegionInfo_t)), (RF_RegionInfo_t *));
  if (raidPtr->regionInfo == NULL)
    return(ENOMEM);

  /* last region may not be full capacity */
  lastRegionCapacity = raidPtr->regionLogCapacity;
  while ((rf_numParityRegions - 1) * raidPtr->regionLogCapacity + lastRegionCapacity > totalLogCapacity)
    lastRegionCapacity = lastRegionCapacity - raidPtr->numSectorsPerLog;

  raidPtr->regionParityRange = raidPtr->sectorsPerDisk / rf_numParityRegions;
  maxRegionParityRange = raidPtr->regionParityRange;

/* i can't remember why this line is in the code -wvcii 6/30/95 */
/*  if (raidPtr->sectorsPerDisk % rf_numParityRegions > 0)
    regionParityRange++; */

  /* build pool of unused parity logs */
  RF_Malloc(raidPtr->parityLogBufferHeap, raidPtr->numParityLogs * raidPtr->numSectorsPerLog * raidPtr->bytesPerSector, (caddr_t));
  if (raidPtr->parityLogBufferHeap == NULL)
    return(ENOMEM);
  lHeapPtr = raidPtr->parityLogBufferHeap;
  rc = rf_mutex_init(&raidPtr->parityLogPool.mutex);
  if (rc) {
    RF_ERRORMSG3("Unable to init mutex file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    RF_Free(raidPtr->parityLogBufferHeap, raidPtr->numParityLogs * raidPtr->numSectorsPerLog * raidPtr->bytesPerSector);
    return(ENOMEM);
  }
  for (i = 0; i < raidPtr->numParityLogs; i++)
    {
      if (i == 0)
	{
	  RF_Calloc(raidPtr->parityLogPool.parityLogs, 1, sizeof(RF_ParityLog_t), (RF_ParityLog_t *));
          if (raidPtr->parityLogPool.parityLogs == NULL) {
            RF_Free(raidPtr->parityLogBufferHeap, raidPtr->numParityLogs * raidPtr->numSectorsPerLog * raidPtr->bytesPerSector);
            return(ENOMEM);
          }
	  l = raidPtr->parityLogPool.parityLogs;
	}
      else
	{
	  RF_Calloc(l->next, 1, sizeof(RF_ParityLog_t), (RF_ParityLog_t *));
          if (l->next == NULL) {
            RF_Free(raidPtr->parityLogBufferHeap, raidPtr->numParityLogs * raidPtr->numSectorsPerLog * raidPtr->bytesPerSector);
            for(l=raidPtr->parityLogPool.parityLogs;l;l=next) {
              next = l->next;
              if (l->records)
                RF_Free(l->records, (raidPtr->numSectorsPerLog * sizeof(RF_ParityLogRecord_t)));
              RF_Free(l, sizeof(RF_ParityLog_t));
            }
            return(ENOMEM);
          }
	  l = l->next;
	}
      l->bufPtr = lHeapPtr;
      lHeapPtr += raidPtr->numSectorsPerLog * raidPtr->bytesPerSector;
      RF_Malloc(l->records, (raidPtr->numSectorsPerLog * sizeof(RF_ParityLogRecord_t)), (RF_ParityLogRecord_t *));
      if (l->records == NULL) {
        RF_Free(raidPtr->parityLogBufferHeap, raidPtr->numParityLogs * raidPtr->numSectorsPerLog * raidPtr->bytesPerSector);
        for(l=raidPtr->parityLogPool.parityLogs;l;l=next) {
          next = l->next;
          if (l->records)
            RF_Free(l->records, (raidPtr->numSectorsPerLog * sizeof(RF_ParityLogRecord_t)));
          RF_Free(l, sizeof(RF_ParityLog_t));
        }
        return(ENOMEM);
      }
    }
  rc = rf_ShutdownCreate(listp, rf_ShutdownParityLoggingPool, raidPtr);
  if (rc) {
    RF_ERRORMSG3("Unable to create shutdown entry file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    rf_ShutdownParityLoggingPool(raidPtr);
    return(rc);
  }

  /* build pool of region buffers */
  rc = rf_mutex_init(&raidPtr->regionBufferPool.mutex);
  if (rc) {
    RF_ERRORMSG3("Unable to init mutex file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    return(ENOMEM);
  }
  rc = rf_cond_init(&raidPtr->regionBufferPool.cond);
  if (rc) {
    RF_ERRORMSG3("Unable to init cond file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    rf_mutex_destroy(&raidPtr->regionBufferPool.mutex);
    return(ENOMEM);
  }
  raidPtr->regionBufferPool.bufferSize = raidPtr->regionLogCapacity * raidPtr->bytesPerSector;
  printf("regionBufferPool.bufferSize %d\n",raidPtr->regionBufferPool.bufferSize);
  raidPtr->regionBufferPool.totalBuffers = 1;  /* for now, only one region at a time may be reintegrated */
  raidPtr->regionBufferPool.availableBuffers = raidPtr->regionBufferPool.totalBuffers;
  raidPtr->regionBufferPool.availBuffersIndex = 0;
  raidPtr->regionBufferPool.emptyBuffersIndex = 0;
  RF_Malloc(raidPtr->regionBufferPool.buffers, raidPtr->regionBufferPool.totalBuffers * sizeof(caddr_t), (caddr_t *));
  if (raidPtr->regionBufferPool.buffers == NULL) {
    rf_mutex_destroy(&raidPtr->regionBufferPool.mutex);
    rf_cond_destroy(&raidPtr->regionBufferPool.cond);
    return(ENOMEM);
  }
  for (i = 0; i < raidPtr->regionBufferPool.totalBuffers; i++) {
    RF_Malloc(raidPtr->regionBufferPool.buffers[i], raidPtr->regionBufferPool.bufferSize * sizeof(char), (caddr_t));
    if (raidPtr->regionBufferPool.buffers == NULL) {
      rf_mutex_destroy(&raidPtr->regionBufferPool.mutex);
      rf_cond_destroy(&raidPtr->regionBufferPool.cond);
      for(j=0;j<i;j++) {
        RF_Free(raidPtr->regionBufferPool.buffers[i], raidPtr->regionBufferPool.bufferSize * sizeof(char));
      }
      RF_Free(raidPtr->regionBufferPool.buffers, raidPtr->regionBufferPool.totalBuffers * sizeof(caddr_t));
      return(ENOMEM);
    }
    printf("raidPtr->regionBufferPool.buffers[%d] = %lx\n", i, 
	   (long)raidPtr->regionBufferPool.buffers[i]);
  }
  rc = rf_ShutdownCreate(listp, rf_ShutdownParityLoggingRegionBufferPool, raidPtr);
  if (rc) {
    RF_ERRORMSG3("Unable to create shutdown entry file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    rf_ShutdownParityLoggingRegionBufferPool(raidPtr);
    return(rc);
  }

  /* build pool of parity buffers */
  parityBufferCapacity = maxRegionParityRange;
  rc = rf_mutex_init(&raidPtr->parityBufferPool.mutex);
  if (rc) {
    RF_ERRORMSG3("Unable to init mutex file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    return(rc);
  }
  rc = rf_cond_init(&raidPtr->parityBufferPool.cond);
  if (rc) {
    RF_ERRORMSG3("Unable to init cond file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    rf_mutex_destroy(&raidPtr->parityBufferPool.mutex);
    return(ENOMEM);
  }
  raidPtr->parityBufferPool.bufferSize = parityBufferCapacity * raidPtr->bytesPerSector;
  printf("parityBufferPool.bufferSize %d\n",raidPtr->parityBufferPool.bufferSize);
  raidPtr->parityBufferPool.totalBuffers = 1;  /* for now, only one region at a time may be reintegrated */
  raidPtr->parityBufferPool.availableBuffers = raidPtr->parityBufferPool.totalBuffers;
  raidPtr->parityBufferPool.availBuffersIndex = 0;
  raidPtr->parityBufferPool.emptyBuffersIndex = 0;
  RF_Malloc(raidPtr->parityBufferPool.buffers, raidPtr->parityBufferPool.totalBuffers * sizeof(caddr_t), (caddr_t *));
  if (raidPtr->parityBufferPool.buffers == NULL) {
    rf_mutex_destroy(&raidPtr->parityBufferPool.mutex);
    rf_cond_destroy(&raidPtr->parityBufferPool.cond);
    return(ENOMEM);
  }
  for (i = 0; i < raidPtr->parityBufferPool.totalBuffers; i++) {
    RF_Malloc(raidPtr->parityBufferPool.buffers[i], raidPtr->parityBufferPool.bufferSize * sizeof(char), (caddr_t));
    if (raidPtr->parityBufferPool.buffers == NULL) {
      rf_mutex_destroy(&raidPtr->parityBufferPool.mutex);
      rf_cond_destroy(&raidPtr->parityBufferPool.cond);
      for(j=0;j<i;j++) {
        RF_Free(raidPtr->parityBufferPool.buffers[i], raidPtr->regionBufferPool.bufferSize * sizeof(char));
      }
      RF_Free(raidPtr->parityBufferPool.buffers, raidPtr->regionBufferPool.totalBuffers * sizeof(caddr_t));
      return(ENOMEM);
    }
    printf("parityBufferPool.buffers[%d] = %lx\n", i, 
	   (long)raidPtr->parityBufferPool.buffers[i]);
  }
  rc = rf_ShutdownCreate(listp, rf_ShutdownParityLoggingParityBufferPool, raidPtr);
  if (rc) {
    RF_ERRORMSG3("Unable to create shutdown entry file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    rf_ShutdownParityLoggingParityBufferPool(raidPtr);
    return(rc);
  }

  /* initialize parityLogDiskQueue */
  rc = rf_create_managed_mutex(listp, &raidPtr->parityLogDiskQueue.mutex);
  if (rc) {
    RF_ERRORMSG3("Unable to init mutex file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    return(rc);
  }
  rc = rf_create_managed_cond(listp, &raidPtr->parityLogDiskQueue.cond);
  if (rc) {
    RF_ERRORMSG3("Unable to init cond file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    return(rc);
  }
  raidPtr->parityLogDiskQueue.flushQueue = NULL;
  raidPtr->parityLogDiskQueue.reintQueue = NULL;
  raidPtr->parityLogDiskQueue.bufHead = NULL;
  raidPtr->parityLogDiskQueue.bufTail = NULL;
  raidPtr->parityLogDiskQueue.reintHead = NULL;
  raidPtr->parityLogDiskQueue.reintTail = NULL;
  raidPtr->parityLogDiskQueue.logBlockHead = NULL;
  raidPtr->parityLogDiskQueue.logBlockTail = NULL;
  raidPtr->parityLogDiskQueue.reintBlockHead = NULL;
  raidPtr->parityLogDiskQueue.reintBlockTail = NULL;
  raidPtr->parityLogDiskQueue.freeDataList = NULL;
  raidPtr->parityLogDiskQueue.freeCommonList = NULL;

  rc = rf_ShutdownCreate(listp, rf_ShutdownParityLoggingDiskQueue, raidPtr);
  if (rc) {
    RF_ERRORMSG3("Unable to create shutdown entry file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    return(rc);
  }

  for (i = 0; i < rf_numParityRegions; i++)
    {
      rc = rf_mutex_init(&raidPtr->regionInfo[i].mutex);
      if (rc) {
        RF_ERRORMSG3("Unable to init mutex file %s line %d rc=%d\n", __FILE__,
          __LINE__, rc);
        for(j=0;j<i;j++)
          FreeRegionInfo(raidPtr, j);
        RF_Free(raidPtr->regionInfo, (rf_numParityRegions * sizeof(RF_RegionInfo_t)));
        return(ENOMEM);
      }
      rc = rf_mutex_init(&raidPtr->regionInfo[i].reintMutex);
      if (rc) {
        RF_ERRORMSG3("Unable to init mutex file %s line %d rc=%d\n", __FILE__,
          __LINE__, rc);
        rf_mutex_destroy(&raidPtr->regionInfo[i].mutex);
        for(j=0;j<i;j++)
          FreeRegionInfo(raidPtr, j);
        RF_Free(raidPtr->regionInfo, (rf_numParityRegions * sizeof(RF_RegionInfo_t)));
        return(ENOMEM);
      }
      raidPtr->regionInfo[i].reintInProgress = RF_FALSE;
      raidPtr->regionInfo[i].regionStartAddr = raidPtr->regionLogCapacity * i;
      raidPtr->regionInfo[i].parityStartAddr = raidPtr->regionParityRange * i;
      if (i < rf_numParityRegions - 1)
	{
	  raidPtr->regionInfo[i].capacity = raidPtr->regionLogCapacity;
	  raidPtr->regionInfo[i].numSectorsParity = raidPtr->regionParityRange;
	}
      else
	{
	  raidPtr->regionInfo[i].capacity = lastRegionCapacity;
	  raidPtr->regionInfo[i].numSectorsParity = raidPtr->sectorsPerDisk - raidPtr->regionParityRange * i;
	  if (raidPtr->regionInfo[i].numSectorsParity > maxRegionParityRange)
	    maxRegionParityRange = raidPtr->regionInfo[i].numSectorsParity;
	}
      raidPtr->regionInfo[i].diskCount = 0;
      RF_ASSERT(raidPtr->regionInfo[i].capacity + raidPtr->regionInfo[i].regionStartAddr <= totalLogCapacity);
      RF_ASSERT(raidPtr->regionInfo[i].parityStartAddr + raidPtr->regionInfo[i].numSectorsParity <= raidPtr->sectorsPerDisk);
      RF_Malloc(raidPtr->regionInfo[i].diskMap, (raidPtr->regionInfo[i].capacity * sizeof(RF_DiskMap_t)), (RF_DiskMap_t *));
      if (raidPtr->regionInfo[i].diskMap == NULL) {
        rf_mutex_destroy(&raidPtr->regionInfo[i].mutex);
        rf_mutex_destroy(&raidPtr->regionInfo[i].reintMutex);
        for(j=0;j<i;j++)
          FreeRegionInfo(raidPtr, j);
        RF_Free(raidPtr->regionInfo, (rf_numParityRegions * sizeof(RF_RegionInfo_t)));
        return(ENOMEM);
      }
      raidPtr->regionInfo[i].loggingEnabled = RF_FALSE;
      raidPtr->regionInfo[i].coreLog = NULL;
    }
  rc = rf_ShutdownCreate(listp, rf_ShutdownParityLoggingRegionInfo, raidPtr);
  if (rc) {
    RF_ERRORMSG3("Unable to create shutdown entry file %s line %d rc=%d\n", __FILE__,
      __LINE__, rc);
    rf_ShutdownParityLoggingRegionInfo(raidPtr);
    return(rc);
  }

  RF_ASSERT(raidPtr->parityLogDiskQueue.threadState == 0);
  raidPtr->parityLogDiskQueue.threadState = RF_PLOG_CREATED;
  rc = RF_CREATE_THREAD(raidPtr->pLogDiskThreadHandle, rf_ParityLoggingDiskManager, raidPtr);
  if (rc) {
    raidPtr->parityLogDiskQueue.threadState = 0;
    RF_ERRORMSG3("Unable to create parity logging disk thread file %s line %d rc=%d\n",
      __FILE__, __LINE__, rc);
    return(ENOMEM);
  }
  /* wait for thread to start */
  RF_LOCK_MUTEX(raidPtr->parityLogDiskQueue.mutex);
  while(!(raidPtr->parityLogDiskQueue.threadState&RF_PLOG_RUNNING)) {
    RF_WAIT_COND(raidPtr->parityLogDiskQueue.cond, raidPtr->parityLogDiskQueue.mutex);
  }
  RF_UNLOCK_MUTEX(raidPtr->parityLogDiskQueue.mutex);

  rc = rf_ShutdownCreate(listp, rf_ShutdownParityLogging, raidPtr);
  if (rc) {
    RF_ERRORMSG1("Got rc=%d adding parity logging shutdown event\n", rc);
    rf_ShutdownParityLogging(raidPtr);
    return(rc);
  }

  if (rf_parityLogDebug)
    {
      printf("                            size of disk log in sectors: %d\n", 
	     (int)totalLogCapacity);
      printf("                            total number of parity regions is %d\n", (int)rf_numParityRegions);
      printf("                            nominal sectors of log per parity region is %d\n", (int)raidPtr->regionLogCapacity);
      printf("                            nominal region fragmentation is %d sectors\n",(int)fragmentation);
      printf("                            total number of parity logs is %d\n", raidPtr->numParityLogs);
      printf("                            parity log size is %d sectors\n", raidPtr->numSectorsPerLog);
      printf("                            total in-core log space is %d bytes\n", (int) rf_totalInCoreLogCapacity);
    }

  rf_EnableParityLogging(raidPtr);

  return(0);
}

static void FreeRegionInfo(
  RF_Raid_t      *raidPtr,
  RF_RegionId_t   regionID)
{
  RF_LOCK_MUTEX(raidPtr->regionInfo[regionID].mutex);
  RF_Free(raidPtr->regionInfo[regionID].diskMap, (raidPtr->regionInfo[regionID].capacity * sizeof(RF_DiskMap_t)));
  if (!rf_forceParityLogReint && raidPtr->regionInfo[regionID].coreLog) {
    rf_ReleaseParityLogs(raidPtr, raidPtr->regionInfo[regionID].coreLog);
    raidPtr->regionInfo[regionID].coreLog = NULL;
  }
  else {
    RF_ASSERT(raidPtr->regionInfo[regionID].coreLog == NULL);
    RF_ASSERT(raidPtr->regionInfo[regionID].diskCount == 0);
  }
  RF_UNLOCK_MUTEX(raidPtr->regionInfo[regionID].mutex);
  rf_mutex_destroy(&raidPtr->regionInfo[regionID].mutex);
  rf_mutex_destroy(&raidPtr->regionInfo[regionID].reintMutex);
}


static void FreeParityLogQueue(
  RF_Raid_t            *raidPtr,
  RF_ParityLogQueue_t  *queue)
{
  RF_ParityLog_t *l1, *l2;

  RF_LOCK_MUTEX(queue->mutex);
  l1 = queue->parityLogs;
  while (l1)
    {
      l2 = l1;
      l1 = l2->next;
      RF_Free(l2->records, (raidPtr->numSectorsPerLog * sizeof(RF_ParityLogRecord_t)));
      RF_Free(l2, sizeof(RF_ParityLog_t));
    }
  RF_UNLOCK_MUTEX(queue->mutex);
  rf_mutex_destroy(&queue->mutex);
}


static void FreeRegionBufferQueue(RF_RegionBufferQueue_t *queue)
{
  int i;

  RF_LOCK_MUTEX(queue->mutex);
  if (queue->availableBuffers != queue->totalBuffers)
    {
      printf("Attempt to free region queue which is still in use!\n");
      RF_ASSERT(0);
    }
  for (i = 0; i < queue->totalBuffers; i++)
    RF_Free(queue->buffers[i], queue->bufferSize);
  RF_Free(queue->buffers, queue->totalBuffers * sizeof(caddr_t));
  RF_UNLOCK_MUTEX(queue->mutex);
  rf_mutex_destroy(&queue->mutex);
}

static void rf_ShutdownParityLoggingRegionInfo(RF_ThreadArg_t arg)
{
  RF_Raid_t *raidPtr;
  RF_RegionId_t i;

  raidPtr = (RF_Raid_t *)arg;
  if (rf_parityLogDebug) {
    int tid;
    rf_get_threadid(tid);
    printf("[%d] ShutdownParityLoggingRegionInfo\n", tid);
  }
  /* free region information structs */
  for (i = 0; i < rf_numParityRegions; i++)
    FreeRegionInfo(raidPtr, i);
  RF_Free(raidPtr->regionInfo, (rf_numParityRegions * sizeof(raidPtr->regionInfo)));
  raidPtr->regionInfo = NULL;
}

static void rf_ShutdownParityLoggingPool(RF_ThreadArg_t arg)
{
  RF_Raid_t *raidPtr;

  raidPtr = (RF_Raid_t *)arg;
  if (rf_parityLogDebug) {
    int tid;
    rf_get_threadid(tid);
    printf("[%d] ShutdownParityLoggingPool\n", tid);
  }
  /* free contents of parityLogPool */
  FreeParityLogQueue(raidPtr, &raidPtr->parityLogPool);
  RF_Free(raidPtr->parityLogBufferHeap, raidPtr->numParityLogs * raidPtr->numSectorsPerLog * raidPtr->bytesPerSector);
}

static void rf_ShutdownParityLoggingRegionBufferPool(RF_ThreadArg_t arg)
{
  RF_Raid_t *raidPtr;

  raidPtr = (RF_Raid_t *)arg;
  if (rf_parityLogDebug) {
    int tid;
    rf_get_threadid(tid);
    printf("[%d] ShutdownParityLoggingRegionBufferPool\n", tid);
  }
  FreeRegionBufferQueue(&raidPtr->regionBufferPool);
}

static void rf_ShutdownParityLoggingParityBufferPool(RF_ThreadArg_t arg)
{
  RF_Raid_t *raidPtr;

  raidPtr = (RF_Raid_t *)arg;
  if (rf_parityLogDebug) {
    int tid;
    rf_get_threadid(tid);
    printf("[%d] ShutdownParityLoggingParityBufferPool\n", tid);
  }
  FreeRegionBufferQueue(&raidPtr->parityBufferPool);
}

static void rf_ShutdownParityLoggingDiskQueue(RF_ThreadArg_t arg)
{
  RF_ParityLogData_t *d;
  RF_CommonLogData_t *c;
  RF_Raid_t *raidPtr;

  raidPtr = (RF_Raid_t *)arg;
  if (rf_parityLogDebug) {
    int tid;
    rf_get_threadid(tid);
    printf("[%d] ShutdownParityLoggingDiskQueue\n", tid);
  }
  /* free disk manager stuff */
  RF_ASSERT(raidPtr->parityLogDiskQueue.bufHead == NULL);
  RF_ASSERT(raidPtr->parityLogDiskQueue.bufTail == NULL);
  RF_ASSERT(raidPtr->parityLogDiskQueue.reintHead == NULL);
  RF_ASSERT(raidPtr->parityLogDiskQueue.reintTail == NULL);
  while (raidPtr->parityLogDiskQueue.freeDataList)
    {
      d = raidPtr->parityLogDiskQueue.freeDataList;
      raidPtr->parityLogDiskQueue.freeDataList = raidPtr->parityLogDiskQueue.freeDataList->next;
      RF_Free(d, sizeof(RF_ParityLogData_t));
    }
  while (raidPtr->parityLogDiskQueue.freeCommonList)
    {
      c = raidPtr->parityLogDiskQueue.freeCommonList;
      rf_mutex_destroy(&c->mutex);
      raidPtr->parityLogDiskQueue.freeCommonList = raidPtr->parityLogDiskQueue.freeCommonList->next;
      RF_Free(c, sizeof(RF_CommonLogData_t));
    }
}

static void rf_ShutdownParityLogging(RF_ThreadArg_t arg)
{
  RF_Raid_t *raidPtr;

  raidPtr = (RF_Raid_t *)arg;
  if (rf_parityLogDebug) {
    int tid;
    rf_get_threadid(tid);
    printf("[%d] ShutdownParityLogging\n", tid);
  }
#ifndef SIMULATE
  /* shutdown disk thread */
  /* This has the desirable side-effect of forcing all regions to be
     reintegrated.  This is necessary since all parity log maps are
     currently held in volatile memory. */

  RF_LOCK_MUTEX(raidPtr->parityLogDiskQueue.mutex);
  raidPtr->parityLogDiskQueue.threadState |= RF_PLOG_TERMINATE;
  RF_UNLOCK_MUTEX(raidPtr->parityLogDiskQueue.mutex);
  RF_SIGNAL_COND(raidPtr->parityLogDiskQueue.cond);
  /*
   * pLogDiskThread will now terminate when queues are cleared
   * now wait for it to be done
   */
  RF_LOCK_MUTEX(raidPtr->parityLogDiskQueue.mutex);
  while(!(raidPtr->parityLogDiskQueue.threadState&RF_PLOG_SHUTDOWN)) {
    RF_WAIT_COND(raidPtr->parityLogDiskQueue.cond, raidPtr->parityLogDiskQueue.mutex);
  }
  RF_UNLOCK_MUTEX(raidPtr->parityLogDiskQueue.mutex);
#else /* !SIMULATE */
  /* explicitly call shutdown routines which force reintegration */
  rf_ShutdownLogging(raidPtr);
#endif /* !SIMULATE */
  if (rf_parityLogDebug) {
    int tid;
    rf_get_threadid(tid);
    printf("[%d] ShutdownParityLogging done (thread completed)\n", tid);
  }
}

int rf_GetDefaultNumFloatingReconBuffersParityLogging(RF_Raid_t *raidPtr)
{
  return(20);
}

RF_HeadSepLimit_t rf_GetDefaultHeadSepLimitParityLogging(RF_Raid_t *raidPtr)
{
  return(10);
}

/* return the region ID for a given RAID address */
RF_RegionId_t rf_MapRegionIDParityLogging(
  RF_Raid_t       *raidPtr,
  RF_SectorNum_t   address)
{
  RF_RegionId_t regionID;

/*  regionID = address / (raidPtr->regionParityRange * raidPtr->Layout.numDataCol); */
  regionID = address / raidPtr->regionParityRange;
  if (regionID == rf_numParityRegions)
    {
      /* last region may be larger than other regions */
      regionID--;
    }
  RF_ASSERT(address >= raidPtr->regionInfo[regionID].parityStartAddr);
  RF_ASSERT(address < raidPtr->regionInfo[regionID].parityStartAddr + raidPtr->regionInfo[regionID].numSectorsParity);
  RF_ASSERT(regionID < rf_numParityRegions);
  return(regionID);
}


/* given a logical RAID sector, determine physical disk address of data */
void rf_MapSectorParityLogging(
  RF_Raid_t         *raidPtr,
  RF_RaidAddr_t      raidSector,
  RF_RowCol_t       *row,
  RF_RowCol_t       *col,
  RF_SectorNum_t    *diskSector,
  int                remap)
{
  RF_StripeNum_t SUID = raidSector / raidPtr->Layout.sectorsPerStripeUnit;
  *row = 0;
  /* *col = (SUID % (raidPtr->numCol - raidPtr->Layout.numParityLogCol)); */
  *col = SUID % raidPtr->Layout.numDataCol;
  *diskSector = (SUID / (raidPtr->Layout.numDataCol)) * raidPtr->Layout.sectorsPerStripeUnit +
    (raidSector % raidPtr->Layout.sectorsPerStripeUnit);
}


/* given a logical RAID sector, determine physical disk address of parity  */
void rf_MapParityParityLogging(
  RF_Raid_t       *raidPtr,
  RF_RaidAddr_t    raidSector,
  RF_RowCol_t     *row,
  RF_RowCol_t     *col,
  RF_SectorNum_t  *diskSector,
  int              remap)
{
  RF_StripeNum_t SUID = raidSector / raidPtr->Layout.sectorsPerStripeUnit;

  *row = 0;
  /* *col = raidPtr->Layout.numDataCol-(SUID/raidPtr->Layout.numDataCol)%(raidPtr->numCol - raidPtr->Layout.numParityLogCol); */
  *col = raidPtr->Layout.numDataCol;
  *diskSector =(SUID / (raidPtr->Layout.numDataCol)) * raidPtr->Layout.sectorsPerStripeUnit +
    (raidSector % raidPtr->Layout.sectorsPerStripeUnit);
}


/* given a regionID and sector offset, determine the physical disk address of the parity log */
void rf_MapLogParityLogging(
  RF_Raid_t       *raidPtr,
  RF_RegionId_t    regionID,
  RF_SectorNum_t   regionOffset,
  RF_RowCol_t     *row,
  RF_RowCol_t     *col,
  RF_SectorNum_t  *startSector)
{
  *row = 0;
  *col = raidPtr->numCol - 1;
  *startSector = raidPtr->regionInfo[regionID].regionStartAddr + regionOffset;
}


/* given a regionID, determine the physical disk address of the logged parity for that region */
void rf_MapRegionParity(
  RF_Raid_t         *raidPtr,
  RF_RegionId_t      regionID,
  RF_RowCol_t       *row,
  RF_RowCol_t       *col,
  RF_SectorNum_t    *startSector,
  RF_SectorCount_t  *numSector)
{
  *row = 0;
  *col = raidPtr->numCol - 2;
  *startSector = raidPtr->regionInfo[regionID].parityStartAddr;
  *numSector = raidPtr->regionInfo[regionID].numSectorsParity;
}


/* given a logical RAID address, determine the participating disks in the stripe */
void rf_IdentifyStripeParityLogging(
  RF_Raid_t        *raidPtr,
  RF_RaidAddr_t     addr,
  RF_RowCol_t     **diskids,
  RF_RowCol_t      *outRow)
{
  RF_StripeNum_t stripeID = rf_RaidAddressToStripeID(&raidPtr->Layout, addr);
  RF_ParityLoggingConfigInfo_t *info = (RF_ParityLoggingConfigInfo_t *) raidPtr->Layout.layoutSpecificInfo;
  *outRow = 0;
  *diskids = info->stripeIdentifier[ stripeID % raidPtr->numCol ];
}


void rf_MapSIDToPSIDParityLogging(
  RF_RaidLayout_t    *layoutPtr,
  RF_StripeNum_t      stripeID,
  RF_StripeNum_t     *psID,
  RF_ReconUnitNum_t  *which_ru)
{
  *which_ru = 0;
  *psID = stripeID;
}


/* select an algorithm for performing an access.  Returns two pointers,
 * one to a function that will return information about the DAG, and
 * another to a function that will create the dag.
 */
void rf_ParityLoggingDagSelect(
  RF_Raid_t             *raidPtr,
  RF_IoType_t            type,
  RF_AccessStripeMap_t  *asmp,
  RF_VoidFuncPtr        *createFunc)
{
  RF_RaidLayout_t *layoutPtr = &(raidPtr->Layout);
  RF_PhysDiskAddr_t *failedPDA=NULL;
  RF_RowCol_t frow, fcol;
  RF_RowStatus_t rstat;
  int prior_recon;
  int tid;
  
  RF_ASSERT(RF_IO_IS_R_OR_W(type));

  if (asmp->numDataFailed + asmp->numParityFailed > 1) {
    RF_ERRORMSG("Multiple disks failed in a single group!  Aborting I/O operation.\n");
    /* *infoFunc = */ *createFunc = NULL;
    return;
  } else if (asmp->numDataFailed + asmp->numParityFailed == 1) {
    
    /* if under recon & already reconstructed, redirect the access to the spare drive 
     * and eliminate the failure indication 
     */
    failedPDA = asmp->failedPDAs[0];
    frow = failedPDA->row; fcol = failedPDA->col;
    rstat = raidPtr->status[failedPDA->row];
    prior_recon = (rstat == rf_rs_reconfigured) || (
      (rstat == rf_rs_reconstructing) ?
      rf_CheckRUReconstructed(raidPtr->reconControl[frow]->reconMap, failedPDA->startSector) : 0
      );
    if (prior_recon) {
      RF_RowCol_t or = failedPDA->row,oc=failedPDA->col;
      RF_SectorNum_t oo=failedPDA->startSector;
      if (layoutPtr->map->flags & RF_DISTRIBUTE_SPARE) {         /* redirect to dist spare space */

        if (failedPDA == asmp->parityInfo) {

          /* parity has failed */
          (layoutPtr->map->MapParity)(raidPtr, failedPDA->raidAddress, &failedPDA->row, 
                                      &failedPDA->col, &failedPDA->startSector, RF_REMAP);

          if (asmp->parityInfo->next) {                          /* redir 2nd component, if any */
            RF_PhysDiskAddr_t *p = asmp->parityInfo->next;
            RF_SectorNum_t SUoffs = p->startSector % layoutPtr->sectorsPerStripeUnit;
            p->row = failedPDA->row;
            p->col = failedPDA->col;
            p->startSector = rf_RaidAddressOfPrevStripeUnitBoundary(layoutPtr, failedPDA->startSector) +
                             SUoffs;    /* cheating:  startSector is not really a RAID address */
          }

        } else if (asmp->parityInfo->next && failedPDA == asmp->parityInfo->next) {
          RF_ASSERT(0);            /* should not ever happen */
        } else {

          /* data has failed */
          (layoutPtr->map->MapSector)(raidPtr, failedPDA->raidAddress, &failedPDA->row, 
                                      &failedPDA->col, &failedPDA->startSector, RF_REMAP);

        }

      } else {                                                 /* redirect to dedicated spare space */

        failedPDA->row = raidPtr->Disks[frow][fcol].spareRow;
        failedPDA->col = raidPtr->Disks[frow][fcol].spareCol;

        /* the parity may have two distinct components, both of which may need to be redirected */
        if (asmp->parityInfo->next) {
          if (failedPDA == asmp->parityInfo) {
            failedPDA->next->row = failedPDA->row;
            failedPDA->next->col = failedPDA->col;
          } else if (failedPDA == asmp->parityInfo->next) {    /* paranoid:  should never occur */
            asmp->parityInfo->row = failedPDA->row;
            asmp->parityInfo->col = failedPDA->col;
          }
        }
      }

      RF_ASSERT(failedPDA->col != -1);
       
      if (rf_dagDebug || rf_mapDebug) {
        rf_get_threadid(tid);
        printf("[%d] Redirected type '%c' r %d c %d o %ld -> r %d c %d o %ld\n",
             tid,type,or,oc,(long)oo,failedPDA->row,failedPDA->col,(long)failedPDA->startSector);
      }
      
      asmp->numDataFailed = asmp->numParityFailed = 0;
    }

  }

  
  if (type == RF_IO_TYPE_READ) {

    if (asmp->numDataFailed == 0)
      *createFunc = (RF_VoidFuncPtr)rf_CreateFaultFreeReadDAG;
    else
      *createFunc = (RF_VoidFuncPtr)rf_CreateRaidFiveDegradedReadDAG;
    
  }
  else {

    
    /* if mirroring, always use large writes.  If the access requires two distinct parity updates,
     * always do a small write.  If the stripe contains a failure but the access does not, do a
     * small write.
     * The first conditional (numStripeUnitsAccessed <= numDataCol/2) uses a less-than-or-equal
     * rather than just a less-than because when G is 3 or 4, numDataCol/2 is 1, and I want
     * single-stripe-unit updates to use just one disk.
     */
    if ( (asmp->numDataFailed + asmp->numParityFailed) == 0) {
      if (((asmp->numStripeUnitsAccessed <= (layoutPtr->numDataCol / 2)) && (layoutPtr->numDataCol!=1)) ||
          (asmp->parityInfo->next!=NULL) || rf_CheckStripeForFailures(raidPtr, asmp)) {
	*createFunc = (RF_VoidFuncPtr)rf_CreateParityLoggingSmallWriteDAG;
      }
      else
        *createFunc = (RF_VoidFuncPtr)rf_CreateParityLoggingLargeWriteDAG;
    } 
    else
      if (asmp->numParityFailed == 1)
        *createFunc = (RF_VoidFuncPtr)rf_CreateNonRedundantWriteDAG;
      else
        if (asmp->numStripeUnitsAccessed != 1 && failedPDA->numSector != layoutPtr->sectorsPerStripeUnit)
          *createFunc = NULL;
        else
          *createFunc = (RF_VoidFuncPtr)rf_CreateDegradedWriteDAG;
  }
}

#endif /* RF_INCLUDE_PARITYLOGGING > 0 */
