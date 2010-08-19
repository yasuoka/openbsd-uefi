/*	$OpenBSD: kern_sysctl.c,v 1.192 2010/08/19 18:14:14 kettenis Exp $	*/
/*	$NetBSD: kern_sysctl.c,v 1.17 1996/05/20 17:49:05 mrg Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Karels at Berkeley Software Design, Inc.
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
 *
 *	@(#)kern_sysctl.c	8.4 (Berkeley) 4/14/94
 */

/*
 * sysctl system call.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/vnode.h>
#include <sys/unistd.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <uvm/uvm_extern.h>
#include <sys/sysctl.h>
#include <sys/msgbuf.h>
#include <sys/dkstat.h>
#include <sys/vmmeter.h>
#include <sys/namei.h>
#include <sys/exec.h>
#include <sys/mbuf.h>
#include <sys/sensors.h>
#include <sys/pipe.h>
#include <sys/eventvar.h>
#include <sys/socketvar.h>
#include <sys/socket.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#ifdef __HAVE_TIMECOUNTER
#include <sys/timetc.h>
#endif
#include <sys/evcount.h>
#include <sys/unpcb.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>

#include <dev/cons.h>
#include <dev/rndvar.h>
#include <dev/systrace.h>

#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>

#ifdef DDB
#include <ddb/db_var.h>
#endif

#ifdef SYSVMSG
#include <sys/msg.h>
#endif
#ifdef SYSVSEM
#include <sys/sem.h>
#endif
#ifdef SYSVSHM
#include <sys/shm.h>
#endif

extern struct forkstat forkstat;
extern struct nchstats nchstats;
extern int nselcoll, fscale;
extern struct disklist_head disklist;
extern fixpt_t ccpu;
extern  long numvnodes;

extern void nmbclust_update(void);

int sysctl_diskinit(int, struct proc *);
int sysctl_proc_args(int *, u_int, void *, size_t *, struct proc *);
int sysctl_intrcnt(int *, u_int, void *, size_t *);
int sysctl_sensors(int *, u_int, void *, size_t *, void *, size_t);
int sysctl_emul(int *, u_int, void *, size_t *, void *, size_t);
int sysctl_cptime2(int *, u_int, void *, size_t *, void *, size_t);

int (*cpu_cpuspeed)(int *);
void (*cpu_setperf)(int);
int perflevel = 100;

int rthreads_enabled = 0;

/*
 * Lock to avoid too many processes vslocking a large amount of memory
 * at the same time.
 */
struct rwlock sysctl_lock = RWLOCK_INITIALIZER("sysctllk");
struct rwlock sysctl_disklock = RWLOCK_INITIALIZER("sysctldlk");

int
sys___sysctl(struct proc *p, void *v, register_t *retval)
{
	struct sys___sysctl_args /* {
		syscallarg(int *) name;
		syscallarg(u_int) namelen;
		syscallarg(void *) old;
		syscallarg(size_t *) oldlenp;
		syscallarg(void *) new;
		syscallarg(size_t) newlen;
	} */ *uap = v;
	int error, dolock = 1;
	size_t savelen = 0, oldlen = 0;
	sysctlfn *fn;
	int name[CTL_MAXNAME];

	if (SCARG(uap, new) != NULL &&
	    (error = suser(p, 0)))
		return (error);
	/*
	 * all top-level sysctl names are non-terminal
	 */
	if (SCARG(uap, namelen) > CTL_MAXNAME || SCARG(uap, namelen) < 2)
		return (EINVAL);
	error = copyin(SCARG(uap, name), name,
		       SCARG(uap, namelen) * sizeof(int));
	if (error)
		return (error);

	switch (name[0]) {
	case CTL_KERN:
		fn = kern_sysctl;
		if (name[1] == KERN_VNODE)	/* XXX */
			dolock = 0;
		break;
	case CTL_HW:
		fn = hw_sysctl;
		break;
	case CTL_VM:
		fn = uvm_sysctl;
		break;
	case CTL_NET:
		fn = net_sysctl;
		break;
	case CTL_FS:
		fn = fs_sysctl;
		break;
	case CTL_VFS:
		fn = vfs_sysctl;
		break;
	case CTL_MACHDEP:
		fn = cpu_sysctl;
		break;
#ifdef DEBUG
	case CTL_DEBUG:
		fn = debug_sysctl;
		break;
#endif
#ifdef DDB
	case CTL_DDB:
		fn = ddb_sysctl;
		break;
#endif
	default:
		return (EOPNOTSUPP);
	}

	if (SCARG(uap, oldlenp) &&
	    (error = copyin(SCARG(uap, oldlenp), &oldlen, sizeof(oldlen))))
		return (error);
	if (SCARG(uap, old) != NULL) {
		if ((error = rw_enter(&sysctl_lock, RW_WRITE|RW_INTR)) != 0)
			return (error);
		if (dolock) {
			if (atop(oldlen) > uvmexp.wiredmax - uvmexp.wired) {
				rw_exit_write(&sysctl_lock);
				return (ENOMEM);
			}
			error = uvm_vslock(p, SCARG(uap, old), oldlen,
			    VM_PROT_READ|VM_PROT_WRITE);
			if (error) {
				rw_exit_write(&sysctl_lock);
				return (error);
			}
		}
		savelen = oldlen;
	}
	error = (*fn)(&name[1], SCARG(uap, namelen) - 1, SCARG(uap, old),
	    &oldlen, SCARG(uap, new), SCARG(uap, newlen), p);
	if (SCARG(uap, old) != NULL) {
		if (dolock)
			uvm_vsunlock(p, SCARG(uap, old), savelen);
		rw_exit_write(&sysctl_lock);
	}
	if (error)
		return (error);
	if (SCARG(uap, oldlenp))
		error = copyout(&oldlen, SCARG(uap, oldlenp), sizeof(oldlen));
	return (error);
}

/*
 * Attributes stored in the kernel.
 */
char hostname[MAXHOSTNAMELEN];
int hostnamelen;
char domainname[MAXHOSTNAMELEN];
int domainnamelen;
long hostid;
char *disknames = NULL;
struct diskstats *diskstats = NULL;
#ifdef INSECURE
int securelevel = -1;
#else
int securelevel;
#endif

/*
 * kernel related system variables.
 */
int
kern_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	int error, level, inthostid, stackgap;
	dev_t dev;
	extern int somaxconn, sominconn;
	extern int usermount, nosuidcoredump;
	extern long cp_time[CPUSTATES];
	extern int stackgap_random;
#ifdef CRYPTO
	extern int usercrypto;
	extern int userasymcrypto;
	extern int cryptodevallowsoft;
#endif
	extern int maxlocksperuid;

	/* all sysctl names at this level are terminal except a ton of them */
	if (namelen != 1) {
		switch (name[0]) {
		case KERN_PROC:
		case KERN_PROC2:
		case KERN_PROF:
		case KERN_MALLOCSTATS:
		case KERN_TTY:
		case KERN_POOL:
		case KERN_PROC_ARGS:
		case KERN_SYSVIPC_INFO:
		case KERN_SEMINFO:
		case KERN_SHMINFO:
		case KERN_INTRCNT:
		case KERN_WATCHDOG:
		case KERN_EMUL:
		case KERN_EVCOUNT:
#ifdef __HAVE_TIMECOUNTER
		case KERN_TIMECOUNTER:
#endif
		case KERN_CPTIME2:
		case KERN_FILE2:
			break;
		default:
			return (ENOTDIR);	/* overloaded */
		}
	}

	switch (name[0]) {
	case KERN_OSTYPE:
		return (sysctl_rdstring(oldp, oldlenp, newp, ostype));
	case KERN_OSRELEASE:
		return (sysctl_rdstring(oldp, oldlenp, newp, osrelease));
	case KERN_OSREV:
		return (sysctl_rdint(oldp, oldlenp, newp, OpenBSD));
	case KERN_OSVERSION:
		return (sysctl_rdstring(oldp, oldlenp, newp, osversion));
	case KERN_VERSION:
		return (sysctl_rdstring(oldp, oldlenp, newp, version));
	case KERN_MAXVNODES:
		return(sysctl_int(oldp, oldlenp, newp, newlen, &maxvnodes));
	case KERN_MAXPROC:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &maxproc));
	case KERN_MAXFILES:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &maxfiles));
	case KERN_NFILES:
		return (sysctl_rdint(oldp, oldlenp, newp, nfiles));
	case KERN_TTYCOUNT:
		return (sysctl_rdint(oldp, oldlenp, newp, tty_count));
	case KERN_NUMVNODES:
		return (sysctl_rdint(oldp, oldlenp, newp, numvnodes));
	case KERN_ARGMAX:
		return (sysctl_rdint(oldp, oldlenp, newp, ARG_MAX));
	case KERN_NSELCOLL:
		return (sysctl_rdint(oldp, oldlenp, newp, nselcoll));
	case KERN_SECURELVL:
		level = securelevel;
		if ((error = sysctl_int(oldp, oldlenp, newp, newlen, &level)) ||
		    newp == NULL)
			return (error);
		if ((securelevel > 0 || level < -1) &&
		    level < securelevel && p->p_pid != 1)
			return (EPERM);
		securelevel = level;
		return (0);
	case KERN_HOSTNAME:
		error = sysctl_tstring(oldp, oldlenp, newp, newlen,
		    hostname, sizeof(hostname));
		if (newp && !error)
			hostnamelen = newlen;
		return (error);
	case KERN_DOMAINNAME:
		error = sysctl_tstring(oldp, oldlenp, newp, newlen,
		    domainname, sizeof(domainname));
		if (newp && !error)
			domainnamelen = newlen;
		return (error);
	case KERN_HOSTID:
		inthostid = hostid;  /* XXX assumes sizeof long <= sizeof int */
		error =  sysctl_int(oldp, oldlenp, newp, newlen, &inthostid);
		hostid = inthostid;
		return (error);
	case KERN_CLOCKRATE:
		return (sysctl_clockrate(oldp, oldlenp, newp));
	case KERN_BOOTTIME:
		return (sysctl_rdstruct(oldp, oldlenp, newp, &boottime,
		    sizeof(struct timeval)));
	case KERN_VNODE:
		return (sysctl_vnode(oldp, oldlenp, p));
#ifndef SMALL_KERNEL
	case KERN_PROC:
	case KERN_PROC2:
		return (sysctl_doproc(name, namelen, oldp, oldlenp));
	case KERN_PROC_ARGS:
		return (sysctl_proc_args(name + 1, namelen - 1, oldp, oldlenp,
		     p));
	case KERN_FILE2:
		return (sysctl_file2(name + 1, namelen - 1, oldp, oldlenp, p));
#endif
	case KERN_FILE:
		return (sysctl_file(oldp, oldlenp, p));
	case KERN_MBSTAT:
		return (sysctl_rdstruct(oldp, oldlenp, newp, &mbstat,
		    sizeof(mbstat)));
#ifdef GPROF
	case KERN_PROF:
		return (sysctl_doprof(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif
	case KERN_POSIX1:
		return (sysctl_rdint(oldp, oldlenp, newp, _POSIX_VERSION));
	case KERN_NGROUPS:
		return (sysctl_rdint(oldp, oldlenp, newp, NGROUPS_MAX));
	case KERN_JOB_CONTROL:
		return (sysctl_rdint(oldp, oldlenp, newp, 1));
	case KERN_SAVED_IDS:
#ifdef _POSIX_SAVED_IDS
		return (sysctl_rdint(oldp, oldlenp, newp, 1));
#else
		return (sysctl_rdint(oldp, oldlenp, newp, 0));
#endif
	case KERN_MAXPARTITIONS:
		return (sysctl_rdint(oldp, oldlenp, newp, MAXPARTITIONS));
	case KERN_RAWPARTITION:
		return (sysctl_rdint(oldp, oldlenp, newp, RAW_PART));
	case KERN_SOMAXCONN:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &somaxconn));
	case KERN_SOMINCONN:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &sominconn));
	case KERN_USERMOUNT:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &usermount));
	case KERN_RND:
		return (sysctl_rdstruct(oldp, oldlenp, newp, &rndstats,
		    sizeof(rndstats)));
	case KERN_ARND: {
		char buf[256];

		if (*oldlenp > sizeof(buf))
			*oldlenp = sizeof(buf);
		if (oldp) {
			arc4random_buf(buf, *oldlenp);
			if ((error = copyout(buf, oldp, *oldlenp)))
				return (error);
		}
		return (0);
	}
	case KERN_NOSUIDCOREDUMP:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &nosuidcoredump));
	case KERN_FSYNC:
		return (sysctl_rdint(oldp, oldlenp, newp, 1));
	case KERN_SYSVMSG:
#ifdef SYSVMSG
		return (sysctl_rdint(oldp, oldlenp, newp, 1));
#else
		return (sysctl_rdint(oldp, oldlenp, newp, 0));
#endif
	case KERN_SYSVSEM:
#ifdef SYSVSEM
		return (sysctl_rdint(oldp, oldlenp, newp, 1));
#else
		return (sysctl_rdint(oldp, oldlenp, newp, 0));
#endif
	case KERN_SYSVSHM:
#ifdef SYSVSHM
		return (sysctl_rdint(oldp, oldlenp, newp, 1));
#else
		return (sysctl_rdint(oldp, oldlenp, newp, 0));
#endif
	case KERN_MSGBUFSIZE:
		/*
		 * deal with cases where the message buffer has
		 * become corrupted.
		 */
		if (!msgbufp || msgbufp->msg_magic != MSG_MAGIC)
			return (ENXIO);
		return (sysctl_rdint(oldp, oldlenp, newp, msgbufp->msg_bufs));
	case KERN_MSGBUF:
		/* see note above */
		if (!msgbufp || msgbufp->msg_magic != MSG_MAGIC)
			return (ENXIO);
		return (sysctl_rdstruct(oldp, oldlenp, newp, msgbufp,
		    msgbufp->msg_bufs + offsetof(struct msgbuf, msg_bufc)));
	case KERN_MALLOCSTATS:
		return (sysctl_malloc(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen, p));
	case KERN_CPTIME:
	{
		CPU_INFO_ITERATOR cii;
		struct cpu_info *ci;
		int i;

		bzero(cp_time, sizeof(cp_time));

		CPU_INFO_FOREACH(cii, ci) {
			for (i = 0; i < CPUSTATES; i++)
				cp_time[i] += ci->ci_schedstate.spc_cp_time[i];
		}

		return (sysctl_rdstruct(oldp, oldlenp, newp, &cp_time,
		    sizeof(cp_time)));
	}
	case KERN_NCHSTATS:
		return (sysctl_rdstruct(oldp, oldlenp, newp, &nchstats,
		    sizeof(struct nchstats)));
	case KERN_FORKSTAT:
		return (sysctl_rdstruct(oldp, oldlenp, newp, &forkstat,
		    sizeof(struct forkstat)));
	case KERN_TTY:
		return (sysctl_tty(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
	case KERN_FSCALE:
		return (sysctl_rdint(oldp, oldlenp, newp, fscale));
	case KERN_CCPU:
		return (sysctl_rdint(oldp, oldlenp, newp, ccpu));
	case KERN_NPROCS:
		return (sysctl_rdint(oldp, oldlenp, newp, nprocs));
	case KERN_POOL:
		return (sysctl_dopool(name + 1, namelen - 1, oldp, oldlenp));
	case KERN_STACKGAPRANDOM:
		stackgap = stackgap_random;
		error = sysctl_int(oldp, oldlenp, newp, newlen, &stackgap);
		if (error)
			return (error);
		/*
		 * Safety harness.
		 */
		if ((stackgap < ALIGNBYTES && stackgap != 0) ||
		    !powerof2(stackgap) || stackgap >= MAXSSIZ)
			return (EINVAL);
		stackgap_random = stackgap;
		return (0);
#if defined(SYSVMSG) || defined(SYSVSEM) || defined(SYSVSHM)
	case KERN_SYSVIPC_INFO:
		return (sysctl_sysvipc(name + 1, namelen - 1, oldp, oldlenp));
#endif
#ifdef CRYPTO
	case KERN_USERCRYPTO:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &usercrypto));
	case KERN_USERASYMCRYPTO:
		return (sysctl_int(oldp, oldlenp, newp, newlen,
			    &userasymcrypto));
	case KERN_CRYPTODEVALLOWSOFT:
		return (sysctl_int(oldp, oldlenp, newp, newlen,
			    &cryptodevallowsoft));
#endif
	case KERN_SPLASSERT:
		return (sysctl_int(oldp, oldlenp, newp, newlen,
		    &splassert_ctl));
#ifdef SYSVSEM
	case KERN_SEMINFO:
		return (sysctl_sysvsem(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif
#ifdef SYSVSHM
	case KERN_SHMINFO:
		return (sysctl_sysvshm(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif
#ifndef SMALL_KERNEL
	case KERN_INTRCNT:
		return (sysctl_intrcnt(name + 1, namelen - 1, oldp, oldlenp));
	case KERN_WATCHDOG:
		return (sysctl_wdog(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
	case KERN_EMUL:
		return (sysctl_emul(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif
	case KERN_MAXCLUSTERS:
		error = sysctl_int(oldp, oldlenp, newp, newlen, &nmbclust);
		if (!error)
			nmbclust_update();
		return (error);
#ifndef SMALL_KERNEL
	case KERN_EVCOUNT:
		return (evcount_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif
#ifdef __HAVE_TIMECOUNTER
	case KERN_TIMECOUNTER:
		return (sysctl_tc(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif
	case KERN_MAXLOCKSPERUID:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &maxlocksperuid));
	case KERN_CPTIME2:
		return (sysctl_cptime2(name + 1, namelen -1, oldp, oldlenp,
		    newp, newlen));
	case KERN_RTHREADS:
		return (sysctl_int(oldp, oldlenp, newp, newlen,
		    &rthreads_enabled));
	case KERN_CACHEPCT: {
		int opct, pgs;
		opct = bufcachepercent;
		error = sysctl_int(oldp, oldlenp, newp, newlen,
		    &bufcachepercent);
		if (error)
			return(error);
		if (bufcachepercent > 90 || bufcachepercent < 5) {
			bufcachepercent = opct;
			return (EINVAL);
		}
		if (bufcachepercent != opct) {
			pgs = bufcachepercent * physmem / 100;
			bufadjust(pgs); /* adjust bufpages */
			bufhighpages = bufpages; /* set high water mark */
		}
		return(0);
	}
	case KERN_CONSDEV:
		if (cn_tab != NULL)
			dev = cn_tab->cn_dev;
		else
			dev = NODEV;
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

/*
 * hardware related system variables.
 */
char *hw_vendor, *hw_prod, *hw_uuid, *hw_serial, *hw_ver;

int
hw_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	extern char machine[], cpu_model[];
	int err, cpuspeed;

	/* all sysctl names at this level except sensors are terminal */
	if (name[0] != HW_SENSORS && namelen != 1)
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
	case HW_MACHINE:
		return (sysctl_rdstring(oldp, oldlenp, newp, machine));
	case HW_MODEL:
		return (sysctl_rdstring(oldp, oldlenp, newp, cpu_model));
	case HW_NCPU:
		return (sysctl_rdint(oldp, oldlenp, newp, ncpus));
	case HW_NCPUFOUND:
		return (sysctl_rdint(oldp, oldlenp, newp, ncpusfound));
	case HW_BYTEORDER:
		return (sysctl_rdint(oldp, oldlenp, newp, BYTE_ORDER));
	case HW_PHYSMEM:
		return (sysctl_rdint(oldp, oldlenp, newp, ptoa(physmem)));
	case HW_USERMEM:
		return (sysctl_rdint(oldp, oldlenp, newp,
		    ptoa(physmem - uvmexp.wired)));
	case HW_PAGESIZE:
		return (sysctl_rdint(oldp, oldlenp, newp, PAGE_SIZE));
	case HW_DISKNAMES:
		err = sysctl_diskinit(0, p);
		if (err)
			return err;
		if (disknames)
			return (sysctl_rdstring(oldp, oldlenp, newp,
			    disknames));
		else
			return (sysctl_rdstring(oldp, oldlenp, newp, ""));
	case HW_DISKSTATS:
		err = sysctl_diskinit(1, p);
		if (err)
			return err;
		return (sysctl_rdstruct(oldp, oldlenp, newp, diskstats,
		    disk_count * sizeof(struct diskstats)));
	case HW_DISKCOUNT:
		return (sysctl_rdint(oldp, oldlenp, newp, disk_count));
#ifndef	SMALL_KERNEL
	case HW_SENSORS:
		return (sysctl_sensors(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif
	case HW_CPUSPEED:
		if (!cpu_cpuspeed)
			return (EOPNOTSUPP);
		err = cpu_cpuspeed(&cpuspeed);
		if (err)
			return err;
		return (sysctl_rdint(oldp, oldlenp, newp, cpuspeed));
	case HW_SETPERF:
		if (!cpu_setperf)
			return (EOPNOTSUPP);
		err = sysctl_int(oldp, oldlenp, newp, newlen, &perflevel);
		if (err)
			return err;
		if (perflevel > 100)
			perflevel = 100;
		if (perflevel < 0)
			perflevel = 0;
		if (newp)
			cpu_setperf(perflevel);
		return (0);
	case HW_VENDOR:
		if (hw_vendor)
			return (sysctl_rdstring(oldp, oldlenp, newp,
			    hw_vendor));
		else
			return (EOPNOTSUPP);
	case HW_PRODUCT:
		if (hw_prod)
			return (sysctl_rdstring(oldp, oldlenp, newp, hw_prod));
		else
			return (EOPNOTSUPP);
	case HW_VERSION:
		if (hw_ver)
			return (sysctl_rdstring(oldp, oldlenp, newp, hw_ver));
		else
			return (EOPNOTSUPP);
	case HW_SERIALNO:
		if (hw_serial)
			return (sysctl_rdstring(oldp, oldlenp, newp,
			    hw_serial));
		else
			return (EOPNOTSUPP);
	case HW_UUID:
		if (hw_uuid)
			return (sysctl_rdstring(oldp, oldlenp, newp, hw_uuid));
		else
			return (EOPNOTSUPP);
	case HW_PHYSMEM64:
		return (sysctl_rdquad(oldp, oldlenp, newp,
		    ptoa((psize_t)physmem)));
	case HW_USERMEM64:
		return (sysctl_rdquad(oldp, oldlenp, newp,
		    ptoa((psize_t)physmem - uvmexp.wired)));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

#ifdef DEBUG
/*
 * Debugging related system variables.
 */
extern struct ctldebug debug0, debug1;
struct ctldebug debug2, debug3, debug4;
struct ctldebug debug5, debug6, debug7, debug8, debug9;
struct ctldebug debug10, debug11, debug12, debug13, debug14;
struct ctldebug debug15, debug16, debug17, debug18, debug19;
static struct ctldebug *debugvars[CTL_DEBUG_MAXID] = {
	&debug0, &debug1, &debug2, &debug3, &debug4,
	&debug5, &debug6, &debug7, &debug8, &debug9,
	&debug10, &debug11, &debug12, &debug13, &debug14,
	&debug15, &debug16, &debug17, &debug18, &debug19,
};
int
debug_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	struct ctldebug *cdp;

	/* all sysctl names at this level are name and field */
	if (namelen != 2)
		return (ENOTDIR);		/* overloaded */
	cdp = debugvars[name[0]];
	if (cdp->debugname == 0)
		return (EOPNOTSUPP);
	switch (name[1]) {
	case CTL_DEBUG_NAME:
		return (sysctl_rdstring(oldp, oldlenp, newp, cdp->debugname));
	case CTL_DEBUG_VALUE:
		return (sysctl_int(oldp, oldlenp, newp, newlen, cdp->debugvar));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}
#endif /* DEBUG */

/*
 * Reads, or writes that lower the value
 */
int
sysctl_int_lower(void *oldp, size_t *oldlenp, void *newp, size_t newlen, int *valp)
{
	unsigned int oval = *valp, val = *valp;
	int error;

	if (newp == NULL)
		return (sysctl_rdint(oldp, oldlenp, newp, *valp));

	if ((error = sysctl_int(oldp, oldlenp, newp, newlen, &val)))
		return (error);
	if (val > oval)
		return (EPERM);		/* do not allow raising */
	*(unsigned int *)valp = val;
	return (0);
}

/*
 * Validate parameters and get old / set new parameters
 * for an integer-valued sysctl function.
 */
int
sysctl_int(void *oldp, size_t *oldlenp, void *newp, size_t newlen, int *valp)
{
	int error = 0;

	if (oldp && *oldlenp < sizeof(int))
		return (ENOMEM);
	if (newp && newlen != sizeof(int))
		return (EINVAL);
	*oldlenp = sizeof(int);
	if (oldp)
		error = copyout(valp, oldp, sizeof(int));
	if (error == 0 && newp)
		error = copyin(newp, valp, sizeof(int));
	return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdint(void *oldp, size_t *oldlenp, void *newp, int val)
{
	int error = 0;

	if (oldp && *oldlenp < sizeof(int))
		return (ENOMEM);
	if (newp)
		return (EPERM);
	*oldlenp = sizeof(int);
	if (oldp)
		error = copyout((caddr_t)&val, oldp, sizeof(int));
	return (error);
}

/*
 * Array of integer values.
 */
int
sysctl_int_arr(int **valpp, int *name, u_int namelen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen)
{
	if (namelen > 1)
		return (ENOTDIR);
	if (name[0] < 0 || valpp[name[0]] == NULL)
		return (EOPNOTSUPP);
	return (sysctl_int(oldp, oldlenp, newp, newlen, valpp[name[0]]));
}

/*
 * Validate parameters and get old / set new parameters
 * for an integer-valued sysctl function.
 */
int
sysctl_quad(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    int64_t *valp)
{
	int error = 0;

	if (oldp && *oldlenp < sizeof(int64_t))
		return (ENOMEM);
	if (newp && newlen != sizeof(int64_t))
		return (EINVAL);
	*oldlenp = sizeof(int64_t);
	if (oldp)
		error = copyout(valp, oldp, sizeof(int64_t));
	if (error == 0 && newp)
		error = copyin(newp, valp, sizeof(int64_t));
	return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdquad(void *oldp, size_t *oldlenp, void *newp, int64_t val)
{
	int error = 0;

	if (oldp && *oldlenp < sizeof(int64_t))
		return (ENOMEM);
	if (newp)
		return (EPERM);
	*oldlenp = sizeof(int64_t);
	if (oldp)
		error = copyout((caddr_t)&val, oldp, sizeof(int64_t));
	return (error);
}

/*
 * Validate parameters and get old / set new parameters
 * for a string-valued sysctl function.
 */
int
sysctl_string(void *oldp, size_t *oldlenp, void *newp, size_t newlen, char *str,
    int maxlen)
{
	return sysctl__string(oldp, oldlenp, newp, newlen, str, maxlen, 0);
}

int
sysctl_tstring(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    char *str, int maxlen)
{
	return sysctl__string(oldp, oldlenp, newp, newlen, str, maxlen, 1);
}

int
sysctl__string(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    char *str, int maxlen, int trunc)
{
	int len, error = 0;
	char c;

	len = strlen(str) + 1;
	if (oldp && *oldlenp < len) {
		if (trunc == 0 || *oldlenp == 0)
			return (ENOMEM);
	}
	if (newp && newlen >= maxlen)
		return (EINVAL);
	if (oldp) {
		if (trunc && *oldlenp < len) {
			/* save & zap NUL terminator while copying */
			c = str[*oldlenp-1];
			str[*oldlenp-1] = '\0';
			error = copyout(str, oldp, *oldlenp);
			str[*oldlenp-1] = c;
		} else {
			*oldlenp = len;
			error = copyout(str, oldp, len);
		}
	}
	if (error == 0 && newp) {
		error = copyin(newp, str, newlen);
		str[newlen] = 0;
	}
	return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdstring(void *oldp, size_t *oldlenp, void *newp, const char *str)
{
	int len, error = 0;

	len = strlen(str) + 1;
	if (oldp && *oldlenp < len)
		return (ENOMEM);
	if (newp)
		return (EPERM);
	*oldlenp = len;
	if (oldp)
		error = copyout(str, oldp, len);
	return (error);
}

/*
 * Validate parameters and get old / set new parameters
 * for a structure oriented sysctl function.
 */
int
sysctl_struct(void *oldp, size_t *oldlenp, void *newp, size_t newlen, void *sp,
    int len)
{
	int error = 0;

	if (oldp && *oldlenp < len)
		return (ENOMEM);
	if (newp && newlen > len)
		return (EINVAL);
	if (oldp) {
		*oldlenp = len;
		error = copyout(sp, oldp, len);
	}
	if (error == 0 && newp)
		error = copyin(newp, sp, len);
	return (error);
}

/*
 * Validate parameters and get old parameters
 * for a structure oriented sysctl function.
 */
int
sysctl_rdstruct(void *oldp, size_t *oldlenp, void *newp, const void *sp,
    int len)
{
	int error = 0;

	if (oldp && *oldlenp < len)
		return (ENOMEM);
	if (newp)
		return (EPERM);
	*oldlenp = len;
	if (oldp)
		error = copyout(sp, oldp, len);
	return (error);
}

/*
 * Get file structures.
 */
int
sysctl_file(char *where, size_t *sizep, struct proc *p)
{
	int buflen, error;
	struct file *fp, cfile;
	char *start = where;
	struct ucred *cred = p->p_ucred;

	buflen = *sizep;
	if (where == NULL) {
		/*
		 * overestimate by KERN_FILESLOP files
		 */
		*sizep = sizeof(filehead) +
		    (nfiles + KERN_FILESLOP) * sizeof(struct file);
		return (0);
	}

	/*
	 * first copyout filehead
	 */
	if (buflen < sizeof(filehead)) {
		*sizep = 0;
		return (0);
	}
	error = copyout((caddr_t)&filehead, where, sizeof(filehead));
	if (error)
		return (error);
	buflen -= sizeof(filehead);
	where += sizeof(filehead);

	/*
	 * followed by an array of file structures
	 */
	LIST_FOREACH(fp, &filehead, f_list) {
		if (buflen < sizeof(struct file)) {
			*sizep = where - start;
			return (ENOMEM);
		}

		/* Only let the superuser or the owner see some information */
		bcopy(fp, &cfile, sizeof (struct file));
		if (suser(p, 0) != 0 && cred->cr_uid != fp->f_cred->cr_uid) {
			cfile.f_offset = (off_t)-1;
			cfile.f_rxfer = 0;
			cfile.f_wxfer = 0;
			cfile.f_rbytes = 0;
			cfile.f_wbytes = 0;
		}
		error = copyout(&cfile, where, sizeof (struct file));
		if (error)
			return (error);
		buflen -= sizeof(struct file);
		where += sizeof(struct file);
	}
	*sizep = where - start;
	return (0);
}

#ifndef SMALL_KERNEL
void
fill_file2(struct kinfo_file2 *kf, struct file *fp, struct filedesc *fdp,
	  int fd, struct vnode *vp, struct proc *pp, struct proc *p)
{
	struct vattr va;

	memset(kf, 0, sizeof(*kf));

	kf->fd_fd = fd;		/* might not really be an fd */

	if (fp != NULL) {
		kf->f_fileaddr = PTRTOINT64(fp);
		kf->f_flag = fp->f_flag;
		kf->f_iflags = fp->f_iflags;
		kf->f_type = fp->f_type;
		kf->f_count = fp->f_count;
		kf->f_msgcount = fp->f_msgcount;
		kf->f_ucred = PTRTOINT64(fp->f_cred);
		kf->f_uid = fp->f_cred->cr_uid;
		kf->f_gid = fp->f_cred->cr_gid;
		kf->f_ops = PTRTOINT64(fp->f_ops);
		kf->f_offset = fp->f_offset;
		kf->f_data = PTRTOINT64(fp->f_data);
		kf->f_usecount = fp->f_usecount;

		if (suser(p, 0) == 0 || p->p_ucred->cr_uid == fp->f_cred->cr_uid) {
			kf->f_rxfer = fp->f_rxfer;
			kf->f_rwfer = fp->f_wxfer;
			kf->f_seek = fp->f_seek;
			kf->f_rbytes = fp->f_rbytes;
			kf->f_wbytes = fp->f_rbytes;
		}
	} else if (vp != NULL) {
		/* fake it */
		kf->f_type = DTYPE_VNODE;
		kf->f_flag = FREAD;
		if (fd == KERN_FILE_TRACE)
			kf->f_flag |= FWRITE;
	}

	/* information about the object associated with this file */
	switch (kf->f_type) {
	case DTYPE_VNODE:
		if (fp != NULL)
			vp = (struct vnode *)fp->f_data;

		kf->v_un = PTRTOINT64(vp->v_un.vu_socket);
		kf->v_type = vp->v_type;
		kf->v_tag = vp->v_tag;
		kf->v_flag = vp->v_flag;
		kf->v_data = PTRTOINT64(vp->v_data);
		kf->v_mount = PTRTOINT64(vp->v_mount);
		if (vp->v_mount)
			strlcpy(kf->f_mntonname,
			    vp->v_mount->mnt_stat.f_mntonname,
			    sizeof(kf->f_mntonname));

		if (VOP_GETATTR(vp, &va, p->p_ucred, p) == 0) {
			kf->va_fileid = va.va_fileid;
			kf->va_mode = MAKEIMODE(va.va_type, va.va_mode);
			kf->va_size = va.va_size;
			kf->va_rdev = va.va_rdev;
			kf->va_fsid = va.va_fsid & 0xffffffff;
		}
		break;

	case DTYPE_SOCKET: {
		struct socket *so = (struct socket *)fp->f_data;

		kf->so_type = so->so_type;
		kf->so_state = so->so_state;
		kf->so_pcb = PTRTOINT64(so->so_pcb);
		kf->so_protocol = so->so_proto->pr_protocol;
		kf->so_family = so->so_proto->pr_domain->dom_family;
		if (!so->so_pcb)
			break;
		switch (kf->so_family) {
		case AF_INET: {
			struct inpcb *inpcb = so->so_pcb;

			kf->inp_ppcb = PTRTOINT64(inpcb->inp_ppcb);
			kf->inp_lport = inpcb->inp_lport;
			kf->inp_laddru[0] = inpcb->inp_laddr.s_addr;
			kf->inp_fport = inpcb->inp_fport;
			kf->inp_faddru[0] = inpcb->inp_faddr.s_addr;
			break;
		    }
		case AF_INET6: {
			struct inpcb *inpcb = so->so_pcb;

			kf->inp_ppcb = PTRTOINT64(inpcb->inp_ppcb);
			kf->inp_lport = inpcb->inp_lport;
			kf->inp_laddru[0] = inpcb->inp_laddr6.s6_addr32[0];
			kf->inp_laddru[1] = inpcb->inp_laddr6.s6_addr32[1];
			kf->inp_laddru[2] = inpcb->inp_laddr6.s6_addr32[2];
			kf->inp_laddru[3] = inpcb->inp_laddr6.s6_addr32[3];
			kf->inp_fport = inpcb->inp_fport;
			kf->inp_faddru[0] = inpcb->inp_laddr6.s6_addr32[0];
			kf->inp_faddru[1] = inpcb->inp_faddr6.s6_addr32[1];
			kf->inp_faddru[2] = inpcb->inp_faddr6.s6_addr32[2];
			kf->inp_faddru[3] = inpcb->inp_faddr6.s6_addr32[3];
			break;
		    }
		case AF_UNIX: {
			struct unpcb *unpcb = so->so_pcb;

			kf->unp_conn = PTRTOINT64(unpcb->unp_conn);
			break;
		    }
		}
		break;
	    }

	case DTYPE_PIPE: {
		struct pipe *pipe = (struct pipe *)fp->f_data;

		kf->pipe_peer = PTRTOINT64(pipe->pipe_peer);
		kf->pipe_state = pipe->pipe_state;
		break;
	    }

	case DTYPE_KQUEUE: {
		struct kqueue *kqi = (struct kqueue *)fp->f_data;

		kf->kq_count = kqi->kq_count;
		kf->kq_state = kqi->kq_state;
		break;
	    }
	case DTYPE_SYSTRACE: {
		struct fsystrace *f = (struct fsystrace *)fp->f_data;

		kf->str_npolicies = f->npolicies;
		break;
	    }
	}

	/* per-process information for KERN_FILE_BY[PU]ID */
	if (pp != NULL) {
		kf->p_pid = pp->p_pid;
		kf->p_uid = pp->p_ucred->cr_uid;
		kf->p_gid = pp->p_ucred->cr_gid;
		strlcpy(kf->p_comm, pp->p_comm, sizeof(kf->p_comm));
	}
	if (fdp != NULL)
		kf->fd_ofileflags = fdp->fd_ofileflags[fd];
}

/*
 * Get file structures.
 */
int
sysctl_file2(int *name, u_int namelen, char *where, size_t *sizep,
    struct proc *p)
{
	struct kinfo_file2 *kf;
	struct filedesc *fdp;
	struct file *fp;
	struct proc *pp;
	size_t buflen, elem_size, elem_count, outsize;
	char *dp = where;
	int arg, i, error = 0, needed = 0;
	u_int op;

	if (namelen > 4)
		return (ENOTDIR);
	if (namelen < 4 || name[2] > sizeof(*kf))
		return (EINVAL);

	buflen = where != NULL ? *sizep : 0;
	op = name[0];
	arg = name[1];
	elem_size = name[2];
	elem_count = name[3];
	outsize = MIN(sizeof(*kf), elem_size);

	if (elem_size < 1 || elem_count < 0)
		return (EINVAL);

	kf = malloc(sizeof(*kf), M_TEMP, M_WAITOK);

#define FILLIT(fp, fdp, i, vp, pp) do {				\
	if (buflen >= elem_size && elem_count > 0) {		\
		fill_file2(kf, fp, fdp, i, vp, pp, p);		\
		error = copyout(kf, dp, outsize);		\
		if (error)					\
			break;					\
		dp += elem_size;				\
		buflen -= elem_size;				\
		elem_count--;					\
	}							\
	needed += elem_size;					\
} while (0)

	switch (op) {
	case KERN_FILE_BYFILE:
		if (arg != 0) {
			/* no arg in file mode */
			error = EINVAL;
			break;
		}
		LIST_FOREACH(fp, &filehead, f_list) {
			if (fp->f_count == 0)
				continue;
			FILLIT(fp, NULL, 0, NULL, NULL);
		}
		break;
	case KERN_FILE_BYPID:
		/* A arg of -1 indicates all processes */
		if (arg < -1) {
			error = EINVAL;
			break;
		}
		LIST_FOREACH(pp, &allproc, p_list) {
			/* skip system, exiting, embryonic and undead processes */
			if ((pp->p_flag & P_SYSTEM) || (pp->p_flag & P_WEXIT)
			    || pp->p_stat == SIDL || pp->p_stat == SZOMB)
				continue;
			if (arg > 0 && pp->p_pid != (pid_t)arg) {
				/* not the pid we are looking for */
				continue;
			}
			fdp = pp->p_fd;
			if (pp->p_textvp)
				FILLIT(NULL, NULL, KERN_FILE_TEXT, pp->p_textvp, pp);
			if (fdp->fd_cdir)
				FILLIT(NULL, NULL, KERN_FILE_CDIR, fdp->fd_cdir, pp);
			if (fdp->fd_rdir)
				FILLIT(NULL, NULL, KERN_FILE_RDIR, fdp->fd_rdir, pp);
			if (pp->p_tracep)
				FILLIT(NULL, NULL, KERN_FILE_TRACE, pp->p_tracep, pp);
			for (i = 0; i < fdp->fd_nfiles; i++) {
				if ((fp = fdp->fd_ofiles[i]) == NULL)
					continue;
				if (!FILE_IS_USABLE(fp))
					continue;
				FILLIT(fp, fdp, i, NULL, pp);
			}
		}
		break;
	case KERN_FILE_BYUID:
		LIST_FOREACH(pp, &allproc, p_list) {
			/* skip system, exiting, embryonic and undead processes */
			if ((pp->p_flag & P_SYSTEM) || (pp->p_flag & P_WEXIT)
			    || pp->p_stat == SIDL || pp->p_stat == SZOMB)
				continue;
			if (arg > 0 && pp->p_ucred->cr_uid != (uid_t)arg) {
				/* not the uid we are looking for */
				continue;
			}
			fdp = pp->p_fd;
			if (fdp->fd_cdir)
				FILLIT(NULL, NULL, KERN_FILE_CDIR, fdp->fd_cdir, pp);
			if (fdp->fd_rdir)
				FILLIT(NULL, NULL, KERN_FILE_RDIR, fdp->fd_rdir, pp);
			if (pp->p_tracep)
				FILLIT(NULL, NULL, KERN_FILE_TRACE, pp->p_tracep, pp);
			for (i = 0; i < fdp->fd_nfiles; i++) {
				if ((fp = fdp->fd_ofiles[i]) == NULL)
					continue;
				if (!FILE_IS_USABLE(fp))
					continue;
				FILLIT(fp, fdp, i, NULL, pp);
			}
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	free(kf, M_TEMP);

	if (!error) {
		if (where == NULL)
			needed += KERN_FILESLOP * elem_size;
		*sizep = needed;
	}

	return (error);
}

/*
 * try over estimating by 5 procs
 */
#define KERN_PROCSLOP	(5 * sizeof (struct kinfo_proc))

int
sysctl_doproc(int *name, u_int namelen, char *where, size_t *sizep)
{
	struct kinfo_proc2 *kproc2 = NULL;
	struct eproc *eproc = NULL;
	struct proc *p;
	struct process *pr;
	char *dp;
	int arg, buflen, doingzomb, elem_size, elem_count;
	int error, needed, type, op;

	dp = where;
	buflen = where != NULL ? *sizep : 0;
	needed = error = 0;
	type = name[0];

	if (type == KERN_PROC) {
		if (namelen != 3 && !(namelen == 2 &&
		    (name[1] == KERN_PROC_ALL || name[1] == KERN_PROC_KTHREAD)))
			return (EINVAL);
		op = name[1];
		arg = op == KERN_PROC_ALL ? 0 : name[2];
		elem_size = elem_count = 0;
		eproc = malloc(sizeof(struct eproc), M_TEMP, M_WAITOK);
	} else /* if (type == KERN_PROC2) */ {
		if (namelen != 5 || name[3] < 0 || name[4] < 0 ||
		    name[3] > sizeof(*kproc2))
			return (EINVAL);
		op = name[1];
		arg = name[2];
		elem_size = name[3];
		elem_count = name[4];
		kproc2 = malloc(sizeof(struct kinfo_proc2), M_TEMP, M_WAITOK);
	}
	p = LIST_FIRST(&allproc);
	doingzomb = 0;
again:
	for (; p != 0; p = LIST_NEXT(p, p_list)) {
		/*
		 * Skip embryonic processes.
		 */
		if (p->p_stat == SIDL)
			continue;

		/* XXX skip processes in the middle of being zapped */
		pr = p->p_p;
		if (pr->ps_pgrp == NULL)
			continue;

		/*
		 * TODO - make more efficient (see notes below).
		 */
		switch (op) {

		case KERN_PROC_PID:
			/* could do this with just a lookup */
			if (p->p_pid != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_PGRP:
			/* could do this by traversing pgrp */
			if (pr->ps_pgrp->pg_id != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_SESSION:
			if (pr->ps_session->s_leader == NULL ||
			    pr->ps_session->s_leader->ps_pid != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_TTY:
			if ((pr->ps_flags & PS_CONTROLT) == 0 ||
			    pr->ps_session->s_ttyp == NULL ||
			    pr->ps_session->s_ttyp->t_dev != (dev_t)arg)
				continue;
			break;

		case KERN_PROC_UID:
			if (p->p_ucred->cr_uid != (uid_t)arg)
				continue;
			break;

		case KERN_PROC_RUID:
			if (p->p_cred->p_ruid != (uid_t)arg)
				continue;
			break;

		case KERN_PROC_ALL:
			if (p->p_flag & P_SYSTEM)
				continue;
			break;
		case KERN_PROC_KTHREAD:
			/* no filtering */
			break;
		default:
			error = EINVAL;
			goto err;
		}
		if (type == KERN_PROC) {
			if (buflen >= sizeof(struct kinfo_proc)) {
				fill_eproc(p, eproc);
				error = copyout((caddr_t)p,
				    &((struct kinfo_proc *)dp)->kp_proc,
				    sizeof(struct proc));
				if (error)
					goto err;
				error = copyout((caddr_t)eproc,
				    &((struct kinfo_proc *)dp)->kp_eproc,
				    sizeof(*eproc));
				if (error)
					goto err;
				dp += sizeof(struct kinfo_proc);
				buflen -= sizeof(struct kinfo_proc);
			}
			needed += sizeof(struct kinfo_proc);
		} else /* if (type == KERN_PROC2) */ {
			if (buflen >= elem_size && elem_count > 0) {
				fill_kproc2(p, kproc2);
				/*
				 * Copy out elem_size, but not larger than
				 * the size of a struct kinfo_proc2.
				 */
				error = copyout(kproc2, dp,
				    min(sizeof(*kproc2), elem_size));
				if (error)
					goto err;
				dp += elem_size;
				buflen -= elem_size;
				elem_count--;
			}
			needed += elem_size;
		}
	}
	if (doingzomb == 0) {
		p = LIST_FIRST(&zombproc);
		doingzomb++;
		goto again;
	}
	if (where != NULL) {
		*sizep = dp - where;
		if (needed > *sizep) {
			error = ENOMEM;
			goto err;
		}
	} else {
		needed += KERN_PROCSLOP;
		*sizep = needed;
	}
err:
	if (eproc)
		free(eproc, M_TEMP);
	if (kproc2)
		free(kproc2, M_TEMP);
	return (error);
}

/*
 * Fill in an eproc structure for the specified process.
 */
void
fill_eproc(struct proc *p, struct eproc *ep)
{
	struct tty *tp;

	ep->e_paddr = p;
	ep->e_sess = p->p_p->ps_pgrp->pg_session;
	ep->e_pcred = *p->p_cred;
	ep->e_ucred = *p->p_ucred;
	if (p->p_stat == SIDL || P_ZOMBIE(p)) {
		ep->e_vm.vm_rssize = 0;
		ep->e_vm.vm_tsize = 0;
		ep->e_vm.vm_dsize = 0;
		ep->e_vm.vm_ssize = 0;
		bzero(&ep->e_pstats, sizeof(ep->e_pstats));
		ep->e_pstats_valid = 0;
	} else {
		struct vmspace *vm = p->p_vmspace;

		ep->e_vm.vm_rssize = vm_resident_count(vm);
		ep->e_vm.vm_tsize = vm->vm_tsize;
		ep->e_vm.vm_dsize = vm->vm_dused;
		ep->e_vm.vm_ssize = vm->vm_ssize;
		ep->e_pstats = *p->p_stats;
		ep->e_pstats_valid = 1;
	}
	if (p->p_p->ps_pptr)
		ep->e_ppid = p->p_p->ps_pptr->ps_pid;
	else
		ep->e_ppid = 0;
	ep->e_pgid = p->p_p->ps_pgrp->pg_id;
	ep->e_jobc = p->p_p->ps_pgrp->pg_jobc;
	if ((p->p_p->ps_flags & PS_CONTROLT) &&
	     (tp = ep->e_sess->s_ttyp)) {
		ep->e_tdev = tp->t_dev;
		ep->e_tpgid = tp->t_pgrp ? tp->t_pgrp->pg_id : NO_PID;
		ep->e_tsess = tp->t_session;
	} else
		ep->e_tdev = NODEV;
	ep->e_flag = ep->e_sess->s_ttyvp ? EPROC_CTTY : 0;
	if (SESS_LEADER(p->p_p))
		ep->e_flag |= EPROC_SLEADER;
	strncpy(ep->e_wmesg, p->p_wmesg ? p->p_wmesg : "", WMESGLEN);
	ep->e_wmesg[WMESGLEN] = '\0';
	ep->e_xsize = ep->e_xrssize = 0;
	ep->e_xccount = ep->e_xswrss = 0;
	strncpy(ep->e_login, ep->e_sess->s_login, MAXLOGNAME-1);
	ep->e_login[MAXLOGNAME-1] = '\0';
	strncpy(ep->e_emul, p->p_emul->e_name, EMULNAMELEN);
	ep->e_emul[EMULNAMELEN] = '\0';
	ep->e_maxrss = p->p_rlimit ? p->p_rlimit[RLIMIT_RSS].rlim_cur : 0;
	ep->e_limit = p->p_p->ps_limit;
}

/*
 * Fill in a kproc2 structure for the specified process.
 */
void
fill_kproc2(struct proc *p, struct kinfo_proc2 *ki)
{
	struct process *pr = p->p_p;
	struct session *s = pr->ps_session;
	struct tty *tp;
	struct timeval ut, st;

	FILL_KPROC2(ki, strlcpy, p, pr, p->p_cred, p->p_ucred, pr->ps_pgrp,
	    p, pr, s, p->p_vmspace, pr->ps_limit, p->p_stats);

	/* stuff that's too painful to generalize into the macros */
	if (pr->ps_pptr)
		ki->p_ppid = pr->ps_pptr->ps_pid;
	if (s->s_leader)
		ki->p_sid = s->s_leader->ps_pid;

	if ((pr->ps_flags & PS_CONTROLT) && (tp = s->s_ttyp)) {
		ki->p_tdev = tp->t_dev;
		ki->p_tpgid = tp->t_pgrp ? tp->t_pgrp->pg_id : -1;
		ki->p_tsess = PTRTOINT64(tp->t_session);
	} else {
		ki->p_tdev = NODEV;
		ki->p_tpgid = -1;
	}

	/* fixups that can only be done in the kernel */
	if (!P_ZOMBIE(p)) {
		if (p->p_stat != SIDL)
			ki->p_vm_rssize = vm_resident_count(p->p_vmspace);

		calcru(p, &ut, &st, NULL);
		ki->p_uutime_sec = ut.tv_sec;
		ki->p_uutime_usec = ut.tv_usec;
		ki->p_ustime_sec = st.tv_sec;
		ki->p_ustime_usec = st.tv_usec;

#ifdef MULTIPROCESSOR
		if (p->p_cpu != NULL)
			ki->p_cpuid = CPU_INFO_UNIT(p->p_cpu);
#endif
	}
}

int
sysctl_proc_args(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    struct proc *cp)
{
	struct proc *vp;
	pid_t pid;
	struct ps_strings pss;
	struct iovec iov;
	struct uio uio;
	int error, cnt, op;
	size_t limit;
	char **rargv, **vargv;		/* reader vs. victim */
	char *rarg, *varg, *buf;
	struct vmspace *vm;

	if (namelen > 2)
		return (ENOTDIR);
	if (namelen < 2)
		return (EINVAL);

	pid = name[0];
	op = name[1];

	switch (op) {
	case KERN_PROC_ARGV:
	case KERN_PROC_NARGV:
	case KERN_PROC_ENV:
	case KERN_PROC_NENV:
		break;
	default:
		return (EOPNOTSUPP);
	}

	if ((vp = pfind(pid)) == NULL)
		return (ESRCH);

	if (oldp == NULL) {
		if (op == KERN_PROC_NARGV || op == KERN_PROC_NENV)
			*oldlenp = sizeof(int);
		else
			*oldlenp = ARG_MAX;	/* XXX XXX XXX */
		return (0);
	}

	if (P_ZOMBIE(vp) || (vp->p_flag & P_SYSTEM))
		return (EINVAL);

	/* Exiting - don't bother, it will be gone soon anyway */
	if ((vp->p_flag & P_WEXIT))
		return (ESRCH);

	/* Execing - danger. */
	if ((vp->p_flag & P_INEXEC))
		return (EBUSY);

	vm = vp->p_vmspace;
	vm->vm_refcnt++;
	vp = NULL;

	buf = malloc(PAGE_SIZE, M_TEMP, M_WAITOK);

	iov.iov_base = &pss;
	iov.iov_len = sizeof(pss);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;	
	uio.uio_offset = (off_t)(vaddr_t)PS_STRINGS;
	uio.uio_resid = sizeof(pss);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_procp = cp;

	if ((error = uvm_io(&vm->vm_map, &uio, 0)) != 0)
		goto out;

	if (op == KERN_PROC_NARGV) {
		error = sysctl_rdint(oldp, oldlenp, NULL, pss.ps_nargvstr);
		goto out;
	}
	if (op == KERN_PROC_NENV) {
		error = sysctl_rdint(oldp, oldlenp, NULL, pss.ps_nenvstr);
		goto out;
	}

	if (op == KERN_PROC_ARGV) {
		cnt = pss.ps_nargvstr;
		vargv = pss.ps_argvstr;
	} else {
		cnt = pss.ps_nenvstr;
		vargv = pss.ps_envstr;
	}

	/* -1 to have space for a terminating NUL */
	limit = *oldlenp - 1;
	*oldlenp = 0;

	rargv = oldp;

	/*
	 * *oldlenp - number of bytes copied out into readers buffer.
	 * limit - maximal number of bytes allowed into readers buffer.
	 * rarg - pointer into readers buffer where next arg will be stored.
	 * rargv - pointer into readers buffer where the next rarg pointer
	 *  will be stored.
	 * vargv - pointer into victim address space where the next argument
	 *  will be read.
	 */

	/* space for cnt pointers and a NULL */
	rarg = (char *)(rargv + cnt + 1);
	*oldlenp += (cnt + 1) * sizeof(char **);

	while (cnt > 0 && *oldlenp < limit) {
		size_t len, vstrlen;

		/* Write to readers argv */
		if ((error = copyout(&rarg, rargv, sizeof(rarg))) != 0)
			goto out;

		/* read the victim argv */
		iov.iov_base = &varg;
		iov.iov_len = sizeof(varg);
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_offset = (off_t)(vaddr_t)vargv;
		uio.uio_resid = sizeof(varg);
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_rw = UIO_READ;
		uio.uio_procp = cp;
		if ((error = uvm_io(&vm->vm_map, &uio, 0)) != 0)
			goto out;

		if (varg == NULL)
			break;

		/*
		 * read the victim arg. We must jump through hoops to avoid
		 * crossing a page boundary too much and returning an error.
		 */
more:
		len = PAGE_SIZE - (((vaddr_t)varg) & PAGE_MASK);
		/* leave space for the terminating NUL */
		iov.iov_base = buf;
		iov.iov_len = len;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_offset = (off_t)(vaddr_t)varg;
		uio.uio_resid = len;
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_rw = UIO_READ;
		uio.uio_procp = cp;
		if ((error = uvm_io(&vm->vm_map, &uio, 0)) != 0)
			goto out;

		for (vstrlen = 0; vstrlen < len; vstrlen++) {
			if (buf[vstrlen] == '\0')
				break;
		}

		/* Don't overflow readers buffer. */
		if (*oldlenp + vstrlen + 1 >= limit) {
			error = ENOMEM;
			goto out;
		}

		if ((error = copyout(buf, rarg, vstrlen)) != 0)
			goto out;

		*oldlenp += vstrlen;
		rarg += vstrlen;

		/* The string didn't end in this page? */
		if (vstrlen == len) {
			varg += vstrlen;
			goto more;
		}

		/* End of string. Terminate it with a NUL */
		buf[0] = '\0';
		if ((error = copyout(buf, rarg, 1)) != 0)
			goto out;
		*oldlenp += 1;
		rarg += 1;

		vargv++;
		rargv++;
		cnt--;
	}

	if (*oldlenp >= limit) {
		error = ENOMEM;
		goto out;
	}

	/* Write the terminating null */
	rarg = NULL;
	error = copyout(&rarg, rargv, sizeof(rarg));

out:
	uvmspace_free(vm);
	free(buf, M_TEMP);
	return (error);
}

#endif

/*
 * Initialize disknames/diskstats for export by sysctl. If update is set,
 * then we simply update the disk statistics information.
 */
int
sysctl_diskinit(int update, struct proc *p)
{
	struct diskstats *sdk;
	struct disk *dk;
	int i, tlen, l;

	if ((i = rw_enter(&sysctl_disklock, RW_WRITE|RW_INTR)) != 0)
		return i;

	if (disk_change) {
		for (dk = TAILQ_FIRST(&disklist), tlen = 0; dk;
		    dk = TAILQ_NEXT(dk, dk_link))
			tlen += strlen(dk->dk_name) + 1;
		tlen++;

		if (disknames)
			free(disknames, M_SYSCTL);
		if (diskstats)
			free(diskstats, M_SYSCTL);
		diskstats = NULL;
		disknames = NULL;
		diskstats = malloc(disk_count * sizeof(struct diskstats),
		    M_SYSCTL, M_WAITOK);
		disknames = malloc(tlen, M_SYSCTL, M_WAITOK);
		disknames[0] = '\0';

		for (dk = TAILQ_FIRST(&disklist), i = 0, l = 0; dk;
		    dk = TAILQ_NEXT(dk, dk_link), i++) {
			snprintf(disknames + l, tlen - l, "%s,",
			    dk->dk_name ? dk->dk_name : "");
			l += strlen(disknames + l);
			sdk = diskstats + i;
			strlcpy(sdk->ds_name, dk->dk_name,
			    sizeof(sdk->ds_name));
			mtx_enter(&dk->dk_mtx);
			sdk->ds_busy = dk->dk_busy;
			sdk->ds_rxfer = dk->dk_rxfer;
			sdk->ds_wxfer = dk->dk_wxfer;
			sdk->ds_seek = dk->dk_seek;
			sdk->ds_rbytes = dk->dk_rbytes;
			sdk->ds_wbytes = dk->dk_wbytes;
			sdk->ds_attachtime = dk->dk_attachtime;
			sdk->ds_timestamp = dk->dk_timestamp;
			sdk->ds_time = dk->dk_time;
			mtx_leave(&dk->dk_mtx);
		}

		/* Eliminate trailing comma */
		if (l != 0)
			disknames[l - 1] = '\0';
		disk_change = 0;
	} else if (update) {
		/* Just update, number of drives hasn't changed */
		for (dk = TAILQ_FIRST(&disklist), i = 0; dk;
		    dk = TAILQ_NEXT(dk, dk_link), i++) {
			sdk = diskstats + i;
			strlcpy(sdk->ds_name, dk->dk_name,
			    sizeof(sdk->ds_name));
			mtx_enter(&dk->dk_mtx);
			sdk->ds_busy = dk->dk_busy;
			sdk->ds_rxfer = dk->dk_rxfer;
			sdk->ds_wxfer = dk->dk_wxfer;
			sdk->ds_seek = dk->dk_seek;
			sdk->ds_rbytes = dk->dk_rbytes;
			sdk->ds_wbytes = dk->dk_wbytes;
			sdk->ds_attachtime = dk->dk_attachtime;
			sdk->ds_timestamp = dk->dk_timestamp;
			sdk->ds_time = dk->dk_time;
			mtx_leave(&dk->dk_mtx);
		}
	}
	rw_exit_write(&sysctl_disklock);
	return 0;
}

#if defined(SYSVMSG) || defined(SYSVSEM) || defined(SYSVSHM)
int
sysctl_sysvipc(int *name, u_int namelen, void *where, size_t *sizep)
{
#ifdef SYSVSEM
	struct sem_sysctl_info *semsi;
#endif
#ifdef SYSVSHM
	struct shm_sysctl_info *shmsi;
#endif
	size_t infosize, dssize, tsize, buflen;
	int i, nds, error, ret;
	void *buf;

	if (namelen != 1)
		return (EINVAL);

	buflen = *sizep;

	switch (*name) {
	case KERN_SYSVIPC_MSG_INFO:
#ifdef SYSVMSG
		return (sysctl_sysvmsg(name, namelen, where, sizep));
#else
		return (EOPNOTSUPP);
#endif
	case KERN_SYSVIPC_SEM_INFO:
#ifdef SYSVSEM
		infosize = sizeof(semsi->seminfo);
		nds = seminfo.semmni;
		dssize = sizeof(semsi->semids[0]);
		break;
#else
		return (EOPNOTSUPP);
#endif
	case KERN_SYSVIPC_SHM_INFO:
#ifdef SYSVSHM
		infosize = sizeof(shmsi->shminfo);
		nds = shminfo.shmmni;
		dssize = sizeof(shmsi->shmids[0]);
		break;
#else
		return (EOPNOTSUPP);
#endif
	default:
		return (EINVAL);
	}
	tsize = infosize + (nds * dssize);

	/* Return just the total size required. */
	if (where == NULL) {
		*sizep = tsize;
		return (0);
	}

	/* Not enough room for even the info struct. */
	if (buflen < infosize) {
		*sizep = 0;
		return (ENOMEM);
	}
	buf = malloc(min(tsize, buflen), M_TEMP, M_WAITOK|M_ZERO);

	switch (*name) {
#ifdef SYSVSEM
	case KERN_SYSVIPC_SEM_INFO:
		semsi = (struct sem_sysctl_info *)buf;
		semsi->seminfo = seminfo;
		break;
#endif
#ifdef SYSVSHM
	case KERN_SYSVIPC_SHM_INFO:
		shmsi = (struct shm_sysctl_info *)buf;
		shmsi->shminfo = shminfo;
		break;
#endif
	}
	buflen -= infosize;

	ret = 0;
	if (buflen > 0) {
		/* Fill in the IPC data structures.  */
		for (i = 0; i < nds; i++) {
			if (buflen < dssize) {
				ret = ENOMEM;
				break;
			}
			switch (*name) {
#ifdef SYSVSEM
			case KERN_SYSVIPC_SEM_INFO:
				if (sema[i] != NULL)
					bcopy(sema[i], &semsi->semids[i],
					    dssize);
				else
					bzero(&semsi->semids[i], dssize);
				break;
#endif
#ifdef SYSVSHM
			case KERN_SYSVIPC_SHM_INFO:
				if (shmsegs[i] != NULL)
					bcopy(shmsegs[i], &shmsi->shmids[i],
					    dssize);
				else
					bzero(&shmsi->shmids[i], dssize);
				break;
#endif
			}
			buflen -= dssize;
		}
	}
	*sizep -= buflen;
	error = copyout(buf, where, *sizep);
	free(buf, M_TEMP);
	/* If copyout succeeded, use return code set earlier. */
	return (error ? error : ret);
}
#endif /* SYSVMSG || SYSVSEM || SYSVSHM */

#ifndef	SMALL_KERNEL

int
sysctl_intrcnt(int *name, u_int namelen, void *oldp, size_t *oldlenp)
{
	return (evcount_sysctl(name, namelen, oldp, oldlenp, NULL, 0));
}


int
sysctl_sensors(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	struct ksensor *ks;
	struct sensor *us;
	struct ksensordev *ksd;
	struct sensordev *usd;
	int dev, numt, ret;
	enum sensor_type type;

	if (namelen != 1 && namelen != 3)
		return (ENOTDIR);

	dev = name[0];
	if (namelen == 1) {
		ret = sensordev_get(dev, &ksd);
		if (ret)
			return (ret);

		/* Grab a copy, to clear the kernel pointers */
		usd = malloc(sizeof(*usd), M_TEMP, M_WAITOK|M_ZERO);
		usd->num = ksd->num;
		strlcpy(usd->xname, ksd->xname, sizeof(usd->xname));
		memcpy(usd->maxnumt, ksd->maxnumt, sizeof(usd->maxnumt));
		usd->sensors_count = ksd->sensors_count;

		ret = sysctl_rdstruct(oldp, oldlenp, newp, usd,
		    sizeof(struct sensordev));

		free(usd, M_TEMP);
		return (ret);
	}

	type = name[1];
	numt = name[2];

	ret = sensor_find(dev, type, numt, &ks);
	if (ret)
		return (ret);

	/* Grab a copy, to clear the kernel pointers */
	us = malloc(sizeof(*us), M_TEMP, M_WAITOK|M_ZERO);
	memcpy(us->desc, ks->desc, sizeof(us->desc));
	us->tv = ks->tv;
	us->value = ks->value;
	us->type = ks->type;
	us->status = ks->status;
	us->numt = ks->numt;
	us->flags = ks->flags;

	ret = sysctl_rdstruct(oldp, oldlenp, newp, us,
	    sizeof(struct sensor));
	free(us, M_TEMP);
	return (ret);
}

int
sysctl_emul(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	int enabled, error;
	struct emul *e;

	if (name[0] == KERN_EMUL_NUM) {
		if (namelen != 1)
			return (ENOTDIR);
		return (sysctl_rdint(oldp, oldlenp, newp, nexecs));
	}

	if (namelen != 2)
		return (ENOTDIR);
	if (name[0] > nexecs || name[0] < 0)
		return (EINVAL);
	e = execsw[name[0] - 1].es_emul;
	if (e == NULL)
		return (EINVAL);

	switch (name[1]) {
	case KERN_EMUL_NAME:
		return (sysctl_rdstring(oldp, oldlenp, newp, e->e_name));
	case KERN_EMUL_ENABLED:
		enabled = (e->e_flags & EMUL_ENABLED);
		error = sysctl_int(oldp, oldlenp, newp, newlen,
		    &enabled);
		e->e_flags = (enabled & EMUL_ENABLED);
		return (error);
	default:
		return (EINVAL);
	}
}

#endif	/* SMALL_KERNEL */

int
sysctl_cptime2(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	int i;

	if (namelen != 1)
		return (ENOTDIR);

	i = name[0];

	CPU_INFO_FOREACH(cii, ci) {
		if (i-- == 0)
			break;
	}
	if (i > 0)
		return (ENOENT);

	return (sysctl_rdstruct(oldp, oldlenp, newp,
	    &ci->ci_schedstate.spc_cp_time,
	    sizeof(ci->ci_schedstate.spc_cp_time)));
}
