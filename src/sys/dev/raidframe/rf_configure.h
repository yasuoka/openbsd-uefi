/*	$OpenBSD: rf_configure.h,v 1.1 1999/01/11 14:29:02 niklas Exp $	*/
/*	$NetBSD: rf_configure.h,v 1.1 1998/11/13 04:20:26 oster Exp $	*/
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

/********************************
 *
 * rf_configure.h 
 *
 * header file for raidframe configuration in the kernel version only.
 * configuration is invoked via ioctl rather than at boot time
 *
 *******************************/

/* :  
 * Log: rf_configure.h,v 
 * Revision 1.16  1996/06/19 14:57:53  jimz
 * move layout-specific config parsing hooks into RF_LayoutSW_t
 * table in rf_layout.c
 *
 * Revision 1.15  1996/06/07  21:33:04  jimz
 * begin using consistent types for sector numbers,
 * stripe numbers, row+col numbers, recon unit numbers
 *
 * Revision 1.14  1996/05/31  22:26:54  jimz
 * fix a lot of mapping problems, memory allocation problems
 * found some weird lock issues, fixed 'em
 * more code cleanup
 *
 * Revision 1.13  1996/05/30  11:29:41  jimz
 * Numerous bug fixes. Stripe lock release code disagreed with the taking code
 * about when stripes should be locked (I made it consistent: no parity, no lock)
 * There was a lot of extra serialization of I/Os which I've removed- a lot of
 * it was to calculate values for the cache code, which is no longer with us.
 * More types, function, macro cleanup. Added code to properly quiesce the array
 * on shutdown. Made a lot of stuff array-specific which was (bogusly) general
 * before. Fixed memory allocation, freeing bugs.
 *
 * Revision 1.12  1996/05/27  18:56:37  jimz
 * more code cleanup
 * better typing
 * compiles in all 3 environments
 *
 * Revision 1.11  1996/05/24  01:59:45  jimz
 * another checkpoint in code cleanup for release
 * time to sync kernel tree
 *
 * Revision 1.10  1996/05/23  00:33:23  jimz
 * code cleanup: move all debug decls to rf_options.c, all extern
 * debug decls to rf_options.h, all debug vars preceded by rf_
 *
 * Revision 1.9  1996/05/18  20:09:51  jimz
 * bit of cleanup to compile cleanly in kernel, once again
 *
 * Revision 1.8  1996/05/18  19:51:34  jimz
 * major code cleanup- fix syntax, make some types consistent,
 * add prototypes, clean out dead code, et cetera
 *
 * Revision 1.7  1995/12/01  15:16:26  root
 * added copyright info
 *
 */

#ifndef _RF__RF_CONFIGURE_H_
#define _RF__RF_CONFIGURE_H_

#include "rf_archs.h"
#include "rf_types.h"

#include <sys/param.h>
#include <sys/proc.h>

#include <sys/ioctl.h>

/* the raidframe configuration, passed down through an ioctl.  
 * the driver can be reconfigured (with total loss of data) at any time,
 * but it must be shut down first.
 */
struct RF_Config_s {
  RF_RowCol_t         numRow, numCol, numSpare;     /* number of rows, columns, and spare disks */
  dev_t               devs[RF_MAXROW][RF_MAXCOL];   /* device numbers for disks comprising array */
  char                devnames[RF_MAXROW][RF_MAXCOL][50]; /* device names */
  dev_t               spare_devs[RF_MAXSPARE];      /* device numbers for spare disks */
  char                spare_names[RF_MAXSPARE][50]; /* device names */
  RF_SectorNum_t      sectPerSU;                    /* sectors per stripe unit */
  RF_StripeNum_t      SUsPerPU;                     /* stripe units per parity unit */
  RF_StripeNum_t      SUsPerRU;                     /* stripe units per reconstruction unit */
  RF_ParityConfig_t   parityConfig;                 /* identifies the RAID architecture to be used */
  RF_DiskQueueType_t  diskQueueType;                /* 'f' = fifo, 'c' = cvscan, not used in kernel */
  char                maxOutstandingDiskReqs;       /* # concurrent reqs to be sent to a disk.  not used in kernel. */
  char                debugVars[RF_MAXDBGV][50];    /* space for specifying debug variables & their values */
  unsigned int        layoutSpecificSize;           /* size in bytes of layout-specific info */
  void               *layoutSpecific;               /* a pointer to a layout-specific structure to be copied in */
};

#ifndef KERNEL
int   rf_MakeConfig(char *configname, RF_Config_t *cfgPtr);
int   rf_MakeLayoutSpecificNULL(FILE *fp, RF_Config_t *cfgPtr, void *arg);
int   rf_MakeLayoutSpecificDeclustered(FILE *configfp, RF_Config_t *cfgPtr, void *arg);
void *rf_ReadSpareTable(RF_SparetWait_t *req, char *fname);
#endif /* !KERNEL */

#endif /* !_RF__RF_CONFIGURE_H_ */
