/*	$OpenBSD: nfs_aiod.c,v 1.1 2009/08/20 15:04:24 thib Exp $	*/
/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/kthread.h>
#include <sys/rwlock.h>
#include <sys/signalvar.h>
#include <sys/queue.h>
#include <sys/mutex.h>

#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsnode.h>
#include <nfs/nfs_var.h>
#include <nfs/nfsmount.h>

/* The nfs_aiodl_mtx mutex protects the two lists. */
struct mutex		nfs_aiodl_mtx;
struct nfs_aiodhead	nfs_aiods_all;
struct nfs_aiodhead	nfs_aiods_idle;

/* Current number of "running" aiods. Defaults to NFS_DEFASYNCDAEMON (4). */
int			nfs_numaiods = -1;

/* Maximum # of buf to queue on an aiod. */
int			nfs_aiodbufqmax;

/*
 * Asynchronous I/O threads for client nfs.
 * They do read-ahead and write-behind operations on the block I/O cache.
 * Never returns unless it fails or gets killed.
 */
void
nfs_aiod(void *arg)
{
	struct nfs_aiod	*aiod;
	struct nfsmount	*nmp;
	struct proc	*p;
	struct buf	*bp;
	int		 error;

	p = (struct proc *)arg;
	error = 0;

	aiod = malloc(sizeof(*aiod), M_TEMP, M_WAITOK|M_ZERO);
	mtx_enter(&nfs_aiodl_mtx);
	LIST_INSERT_HEAD(&nfs_aiods_all, aiod, nad_all);
	mtx_leave(&nfs_aiodl_mtx);
	nfs_numaiods++;

	/*
	 * Enforce an upper limit on how many bufs we'll queue up for
	 * a given aiod. This is arbitrarily chosen to be a quarter of
	 * the number of bufs in the system, divided evenly between
	 * the running aiods.
	 *
	 * Since the number of bufs in the system is dynamic, and the
	 * aiods are usually started up very early (during boot), the
	 * number of buffers available is pretty low, so the limit we
	 * enforce is way to low: So, always allow a minimum of 64 bufs.
	 * XXX: Footshooting.
	 */
	nfs_aiodbufqmax = max((bcstats.numbufs / 4) / nfs_numaiods, 64);


loop:	/* Loop around until SIGKILL */
	mtx_enter(&nfs_aiodl_mtx);
	LIST_INSERT_HEAD(&nfs_aiods_idle, aiod, nad_idle);
	mtx_leave(&nfs_aiodl_mtx);

	while (1) {
		nmp = aiod->nad_mnt;
		if (nmp) {
			aiod->nad_mnt = NULL;
			break;
		}

		error = tsleep(aiod, PWAIT, "nioidle", 0);
		if (error)
			goto out;

		/*
		 * Wakeup for this aiod happens in one of the following
		 * situations:
		 * - The thread is being asked to exit by nfs_set_naiod(), or
		 * - nfs_asyncio() has found work for this thread on a mount.
		 *
		 * In the former case, check to see if nfs_asyncio() has just
		 * found some work for this thread, and if so, ignore it until
		 * later.
		 */
		if (aiod->nad_exiting) {
			if (aiod->nad_mnt == NULL)
				goto out1;
			else
				break;
		}
	}

	while ((bp = TAILQ_FIRST(&nmp->nm_bufq)) != NULL) {
		/* Take one off the front of the list */
		TAILQ_REMOVE(&nmp->nm_bufq, bp, b_freelist);
		nmp->nm_bufqlen--;
		nfs_doio(bp, NULL);
	}

	KASSERT(nmp->nm_naiods > 0);
	nmp->nm_naiods--;
	if (aiod->nad_exiting)
		goto out1;

	goto loop;

out:
	mtx_enter(&nfs_aiodl_mtx);
	LIST_REMOVE(aiod, nad_idle);
	mtx_leave(&nfs_aiodl_mtx);
out1:
	free(aiod, M_TEMP);
	nfs_numaiods--;
	/* Rejust the limit of bufs to queue. See comment above. */
	nfs_aiodbufqmax = max((bcstats.numbufs / 4) / nfs_numaiods, 64);
	kthread_exit(error);
}

int
nfs_set_naiod(int howmany)
{
	struct nfs_aiod	*aiod;
	struct proc	*p;
	int		 want, error, dolock;

	KASSERT(howmany >= 0);

	error = 0;
	dolock = 1;

	if (nfs_numaiods == -1)
		nfs_numaiods = 0;

	want = howmany - nfs_numaiods;

	if (want > 0) {
		/* Add more. */	
		want = min(want, NFS_MAXASYNCDAEMON);
		while (want > 0) {
			error = kthread_create(nfs_aiod, p, &p, "nfsaio");
			if (error)
				return (error);
			want--;
		}
	} else if (want < 0) {
		/* Get rid of some. */
		want = -want;
		want = min(want, nfs_numaiods);

		/* Favour idle aiod's. */
		mtx_enter(&nfs_aiodl_mtx);
		while (!LIST_EMPTY(&nfs_aiods_idle) && want > 0) {
			aiod = LIST_FIRST(&nfs_aiods_idle);
			LIST_REMOVE(aiod, nad_idle);
			LIST_REMOVE(aiod, nad_all);	/* Yuck. */
			aiod->nad_exiting = 1;
			wakeup(aiod);
			want--;
		}

		while (!LIST_EMPTY(&nfs_aiods_all) && want > 0) {
			aiod = LIST_FIRST(&nfs_aiods_all);
			LIST_REMOVE(aiod, nad_all);
			aiod->nad_exiting = 1;
			wakeup(aiod);
			want--;
		}
		mtx_leave(&nfs_aiodl_mtx);
	}
	/* ignore the want == nfs_numaiods case, since it means no work */

	return (error);
}
