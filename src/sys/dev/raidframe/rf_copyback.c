/*	$OpenBSD: rf_copyback.c,v 1.8 2007/06/05 00:38:22 deraadt Exp $	*/
/*	$NetBSD: rf_copyback.c,v 1.14 2000/03/07 02:59:50 oster Exp $	*/

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


/*****************************************************************************
 *
 * copyback.c -- Code to copy reconstructed data back from spare space to
 *		 the replaced disk.
 *
 * The code operates using callbacks on the I/Os to continue with the next
 * unit to be copied back. We do this because a simple loop containing
 * blocking I/Os will not work in the simulator.
 *
 *****************************************************************************/

#include "rf_types.h"

#include <sys/time.h>
#include <sys/buf.h>
#include "rf_raid.h"
#include "rf_mcpair.h"
#include "rf_acctrace.h"
#include "rf_etimer.h"
#include "rf_general.h"
#include "rf_utils.h"
#include "rf_copyback.h"
#include "rf_decluster.h"
#include "rf_driver.h"
#include "rf_shutdown.h"
#include "rf_kintf.h"

#define	RF_COPYBACK_DATA	0
#define	RF_COPYBACK_PARITY	1

int	rf_copyback_in_progress;

int  rf_CopybackReadDoneProc(RF_CopybackDesc_t *, int);
int  rf_CopybackWriteDoneProc(RF_CopybackDesc_t *, int);
void rf_CopybackOne(RF_CopybackDesc_t *, int, RF_RaidAddr_t,
	RF_RowCol_t, RF_RowCol_t, RF_SectorNum_t);
void rf_CopybackComplete(RF_CopybackDesc_t *, int);

int
rf_ConfigureCopyback(RF_ShutdownList_t **listp)
{
	rf_copyback_in_progress = 0;
	return (0);
}

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#ifdef	__NETBSD__
#include <sys/vnode.h>
#endif


/* Do a complete copyback. */
void
rf_CopybackReconstructedData(RF_Raid_t *raidPtr)
{
	RF_ComponentLabel_t c_label;
	int done, retcode;
	RF_CopybackDesc_t *desc;
	RF_RowCol_t frow, fcol;
	RF_RaidDisk_t *badDisk;
	char *databuf;

	struct partinfo dpart;
	struct vnode *vp;
	struct vattr va;
	struct proc *proc;

	int ac;

	done = 0;
	fcol = 0;
	for (frow = 0; frow < raidPtr->numRow; frow++) {
		for (fcol = 0; fcol < raidPtr->numCol; fcol++) {
			if (raidPtr->Disks[frow][fcol].status ==
			     rf_ds_dist_spared ||
			    raidPtr->Disks[frow][fcol].status ==
			     rf_ds_spared) {
				done = 1;
				break;
			}
		}
		if (done)
			break;
	}

	if (frow == raidPtr->numRow) {
		printf("COPYBACK: No disks need copyback.\n");
		return;
	}
	badDisk = &raidPtr->Disks[frow][fcol];

	proc = raidPtr->engine_thread;

	/*
	 * This device may have been opened successfully the first time.
	 * Close it before trying to open it again.
	 */

	if (raidPtr->raid_cinfo[frow][fcol].ci_vp != NULL) {
		printf("Close the opened device: %s.\n",
		    raidPtr->Disks[frow][fcol].devname);
 		vp = raidPtr->raid_cinfo[frow][fcol].ci_vp;
 		ac = raidPtr->Disks[frow][fcol].auto_configured;
 		rf_close_component(raidPtr, vp, ac);
		raidPtr->raid_cinfo[frow][fcol].ci_vp = NULL;

	}
 	/* Note that this disk was *not* auto_configured (any longer). */
 	raidPtr->Disks[frow][fcol].auto_configured = 0;

	printf("About to (re-)open the device: %s.\n",
	    raidPtr->Disks[frow][fcol].devname);

	retcode = raidlookup(raidPtr->Disks[frow][fcol].devname, proc, &vp);

	if (retcode) {
		printf("COPYBACK: raidlookup on device: %s failed: %d !\n",
		    raidPtr->Disks[frow][fcol].devname, retcode);

		/*
		 * XXX The component isn't responding properly... Must be
		 * still dead :-(
		 */
		return;

	} else {

		/*
		 * Ok, so we can at least do a lookup...
		 * How about actually getting a vp for it ?
		 */

		if ((retcode = VOP_GETATTR(vp, &va, proc->p_ucred, proc)) != 0)
		{
			return;
		}
		retcode = VOP_IOCTL(vp, DIOCGPART, (caddr_t) &dpart, FREAD,
		    proc->p_ucred, proc);
		if (retcode) {
			return;
		}
		raidPtr->Disks[frow][fcol].blockSize = dpart.disklab->d_secsize;

		raidPtr->Disks[frow][fcol].numBlocks = DL_GETPSIZE(dpart.part) -
		    rf_protectedSectors;

		raidPtr->raid_cinfo[frow][fcol].ci_vp = vp;
		raidPtr->raid_cinfo[frow][fcol].ci_dev = va.va_rdev;

		/* XXX Or the above ? */
		raidPtr->Disks[frow][fcol].dev = va.va_rdev;

		/*
		 * We allow the user to specify that only a fraction of the
		 * disks should be used this is just for debug: it speeds up
		 * the parity scan.
		 */
		raidPtr->Disks[frow][fcol].numBlocks =
		    raidPtr->Disks[frow][fcol].numBlocks *
		    rf_sizePercentage / 100;
	}
#if 0
	/* This is the way it was done before the CAM stuff was removed. */

	if (rf_extract_ids(badDisk->devname, &bus, &targ, &lun)) {
		printf("COPYBACK: unable to extract bus, target, lun from"
		    " devname %s.\n", badDisk->devname);
		return;
	}
	/*
	 * TUR the disk that's marked as bad to be sure that it's actually
	 * alive.
	 */
	rf_SCSI_AllocTUR(&tur_op);
	retcode = rf_SCSI_DoTUR(tur_op, bus, targ, lun, badDisk->dev);
	rf_SCSI_FreeDiskOp(tur_op, 0);
#endif

	if (retcode) {
		printf("COPYBACK: target disk failed TUR.\n");
		return;
	}
	/* Get a buffer to hold one SU. */
	RF_Malloc(databuf, rf_RaidAddressToByte(raidPtr,
	    raidPtr->Layout.sectorsPerStripeUnit), (char *));

	/* Create a descriptor. */
	RF_Malloc(desc, sizeof(*desc), (RF_CopybackDesc_t *));
	desc->raidPtr = raidPtr;
	desc->status = 0;
	desc->frow = frow;
	desc->fcol = fcol;
	desc->spRow = badDisk->spareRow;
	desc->spCol = badDisk->spareCol;
	desc->stripeAddr = 0;
	desc->sectPerSU = raidPtr->Layout.sectorsPerStripeUnit;
	desc->sectPerStripe = raidPtr->Layout.sectorsPerStripeUnit *
	    raidPtr->Layout.numDataCol;
	desc->databuf = databuf;
	desc->mcpair = rf_AllocMCPair();

	printf("COPYBACK: Quiescing the array.\n");
	/*
	 * Quiesce the array, since we don't want to code support for user
	 * accs here.
	 */
	rf_SuspendNewRequestsAndWait(raidPtr);

	/* Adjust state of the array and of the disks. */
	RF_LOCK_MUTEX(raidPtr->mutex);
	raidPtr->Disks[desc->frow][desc->fcol].status = rf_ds_optimal;
	raidPtr->status[desc->frow] = rf_rs_optimal;
	rf_copyback_in_progress = 1;	/* Debug only. */
	RF_UNLOCK_MUTEX(raidPtr->mutex);

	printf("COPYBACK: Beginning\n");
	RF_GETTIME(desc->starttime);
	rf_ContinueCopyback(desc);

	/*
	 * Data has been restored.
	 * Fix up the component label.
	 * Don't actually need the read here.
	 */
	raidread_component_label(raidPtr->raid_cinfo[frow][fcol].ci_dev,
				 raidPtr->raid_cinfo[frow][fcol].ci_vp,
				 &c_label);

	raid_init_component_label(raidPtr, &c_label);

	c_label.row = frow;
	c_label.column = fcol;

	raidwrite_component_label(raidPtr->raid_cinfo[frow][fcol].ci_dev,
				  raidPtr->raid_cinfo[frow][fcol].ci_vp,
				  &c_label);
}


/*
 * Invoked via callback after a copyback I/O has completed to
 * continue on with the next one.
 */
void
rf_ContinueCopyback(RF_CopybackDesc_t *desc)
{
	RF_SectorNum_t testOffs, stripeAddr;
	RF_Raid_t *raidPtr = desc->raidPtr;
	RF_RaidAddr_t addr;
	RF_RowCol_t testRow, testCol;
	int old_pctg, new_pctg, done;
	struct timeval t, diff;

	old_pctg = (-1);
	while (1) {
		stripeAddr = desc->stripeAddr;
		desc->raidPtr->copyback_stripes_done = stripeAddr /
		    desc->sectPerStripe;
		if (rf_prReconSched) {
			old_pctg = 100 * desc->stripeAddr /
			    raidPtr->totalSectors;
		}
		desc->stripeAddr += desc->sectPerStripe;
		if (rf_prReconSched) {
			new_pctg = 100 * desc->stripeAddr /
			    raidPtr->totalSectors;
			if (new_pctg != old_pctg) {
				RF_GETTIME(t);
				RF_TIMEVAL_DIFF(&desc->starttime, &t, &diff);
				printf("%d %d.%06d\n", new_pctg,
				    (int) diff.tv_sec, (int) diff.tv_usec);
			}
		}
		if (stripeAddr >= raidPtr->totalSectors) {
			rf_CopybackComplete(desc, 0);
			return;
		}
		/* Walk through the current stripe, su-by-su. */
		for (done = 0, addr = stripeAddr;
		     addr < stripeAddr + desc->sectPerStripe;
		     addr += desc->sectPerSU) {

			/* Map the SU, disallowing remap to spare space. */
			(raidPtr->Layout.map->MapSector) (raidPtr, addr,
			    &testRow, &testCol, &testOffs, RF_DONT_REMAP);

			if (testRow == desc->frow && testCol == desc->fcol) {
				rf_CopybackOne(desc, RF_COPYBACK_DATA, addr,
				    testRow, testCol, testOffs);
				done = 1;
				break;
			}
		}

		if (!done) {
			/*
			 * We didn't find the failed disk in the data part,
			 * check parity.
			 */

			/*
			 * Map the parity for this stripe, disallowing remap
			 * to spare space.
			 */
			(raidPtr->Layout.map->MapParity) (raidPtr, stripeAddr,
			    &testRow, &testCol, &testOffs, RF_DONT_REMAP);

			if (testRow == desc->frow && testCol == desc->fcol) {
				rf_CopybackOne(desc, RF_COPYBACK_PARITY,
				    stripeAddr, testRow, testCol, testOffs);
			}
		}
		/* Check to see if the last read/write pair failed. */
		if (desc->status) {
			rf_CopybackComplete(desc, 1);
			return;
		}
		/*
		 * We didn't find any units to copy back in this stripe.
		 * Continue with the next one.
		 */
	}
}


/* Copyback one unit. */
void
rf_CopybackOne(RF_CopybackDesc_t *desc, int typ, RF_RaidAddr_t addr,
    RF_RowCol_t testRow, RF_RowCol_t testCol, RF_SectorNum_t testOffs)
{
	RF_SectorCount_t sectPerSU = desc->sectPerSU;
	RF_Raid_t *raidPtr = desc->raidPtr;
	RF_RowCol_t spRow = desc->spRow;
	RF_RowCol_t spCol = desc->spCol;
	RF_SectorNum_t spOffs;

	/* Find the spare location for this SU. */
	if (raidPtr->Layout.map->flags & RF_DISTRIBUTE_SPARE) {
		if (typ == RF_COPYBACK_DATA)
			raidPtr->Layout.map->MapSector(raidPtr, addr, &spRow,
			    &spCol, &spOffs, RF_REMAP);
		else
			raidPtr->Layout.map->MapParity(raidPtr, addr, &spRow,
			    &spCol, &spOffs, RF_REMAP);
	} else {
		spOffs = testOffs;
	}

	/* Create reqs to read the old location & write the new. */
	desc->readreq = rf_CreateDiskQueueData(RF_IO_TYPE_READ, spOffs,
	    sectPerSU, desc->databuf, 0L, 0, (int (*) (void *, int))
	    rf_CopybackReadDoneProc, desc, NULL, NULL, (void *) raidPtr,
	    RF_DISKQUEUE_DATA_FLAGS_NONE, NULL);
	desc->writereq = rf_CreateDiskQueueData(RF_IO_TYPE_WRITE, testOffs,
	    sectPerSU, desc->databuf, 0L, 0, (int (*) (void *, int))
	    rf_CopybackWriteDoneProc, desc, NULL, NULL, (void *) raidPtr,
	    RF_DISKQUEUE_DATA_FLAGS_NONE, NULL);
	desc->frow = testRow;
	desc->fcol = testCol;

	/*
	 * Enqueue the read. The write will go out as part of the callback on
	 * the read. At user-level & in the kernel, wait for the read-write
	 * pair to complete. In the simulator, just return, since everything
	 * will happen as callbacks.
	 */

	RF_LOCK_MUTEX(desc->mcpair->mutex);
	desc->mcpair->flag = 0;

	rf_DiskIOEnqueue(&raidPtr->Queues[spRow][spCol], desc->readreq,
	    RF_IO_NORMAL_PRIORITY);

	while (!desc->mcpair->flag) {
		RF_WAIT_MCPAIR(desc->mcpair);
	}
	RF_UNLOCK_MUTEX(desc->mcpair->mutex);
	rf_FreeDiskQueueData(desc->readreq);
	rf_FreeDiskQueueData(desc->writereq);

}


/*
 * Called at interrupt context when the read has completed.
 * Just send out the write.
 */
int
rf_CopybackReadDoneProc(RF_CopybackDesc_t *desc, int status)
{
	if (status) {		/* Invoke the callback with bad status. */
		printf("COPYBACK: copyback read failed. Aborting.\n");
		(desc->writereq->CompleteFunc) (desc, -100);
	} else {
		rf_DiskIOEnqueue(&(desc->raidPtr
		    ->Queues[desc->frow][desc->fcol]),
		    desc->writereq, RF_IO_NORMAL_PRIORITY);
	}
	return (0);
}


/*
 * Called at interrupt context when the write has completed.
 * At user level & in the kernel, wake up the copyback thread.
 * In the simulator, invoke the next copyback directly.
 * Can't free diskqueuedata structs in the kernel because we're at
 * interrupt context.
 */
int
rf_CopybackWriteDoneProc(RF_CopybackDesc_t *desc, int status)
{
	if (status && status != -100) {
		printf("COPYBACK: copyback write failed. Aborting.\n");
	}
	desc->status = status;
	rf_MCPairWakeupFunc(desc->mcpair);
	return (0);
}


/* Invoked when the copyback has completed. */
void
rf_CopybackComplete(RF_CopybackDesc_t *desc, int status)
{
	RF_Raid_t *raidPtr = desc->raidPtr;
	struct timeval t, diff;

	if (!status) {
		RF_LOCK_MUTEX(raidPtr->mutex);
		if (raidPtr->Layout.map->flags & RF_DISTRIBUTE_SPARE) {
			RF_ASSERT(raidPtr->Layout.map->parityConfig == 'D');
			rf_FreeSpareTable(raidPtr);
		} else {
			raidPtr->Disks[desc->spRow][desc->spCol].status =
			    rf_ds_spare;
		}
		RF_UNLOCK_MUTEX(raidPtr->mutex);

		RF_GETTIME(t);
		RF_TIMEVAL_DIFF(&desc->starttime, &t, &diff);
		printf("Copyback time was %d.%06d seconds.\n",
		    (int) diff.tv_sec, (int) diff.tv_usec);
	} else
		printf("COPYBACK: Failure.\n");

	RF_Free(desc->databuf, rf_RaidAddressToByte(raidPtr, desc->sectPerSU));
	rf_FreeMCPair(desc->mcpair);
	RF_Free(desc, sizeof(*desc));

	rf_copyback_in_progress = 0;
	rf_ResumeNewRequests(raidPtr);
}
