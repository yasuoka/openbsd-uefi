/*	$OpenBSD: linux_misc.c,v 1.37 2001/07/03 21:56:26 jasoni Exp $	*/
/*	$NetBSD: linux_misc.c,v 1.27 1996/05/20 01:59:21 fvdl Exp $	*/

/*
 * Copyright (c) 1995 Frank van der Linden
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project
 *      by Frank van der Linden
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Linux compatibility module. Try to deal with various Linux system calls.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/filedesc.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/unistd.h>

#include <sys/syscallargs.h>

#include <vm/vm.h>
#include <vm/vm_param.h>

#include <compat/linux/linux_types.h>
#include <compat/linux/linux_fcntl.h>
#include <compat/linux/linux_misc.h>
#include <compat/linux/linux_mmap.h>
#include <compat/linux/linux_sched.h>
#include <compat/linux/linux_signal.h>
#include <compat/linux/linux_syscallargs.h>
#include <compat/linux/linux_util.h>
#include <compat/linux/linux_dirent.h>

#include <compat/common/compat_dir.h>

/* linux_misc.c */
static void bsd_to_linux_statfs __P((struct statfs *, struct linux_statfs *));
int	linux_select1 __P((struct proc *, register_t *, int, fd_set *,
     fd_set *, fd_set *, struct timeval *));

/*
 * The information on a terminated (or stopped) process needs
 * to be converted in order for Linux binaries to get a valid signal
 * number out of it.
 */
void
bsd_to_linux_wstat(status)
	int *status;
{

	if (WIFSIGNALED(*status))
		*status = (*status & ~0177) |
		    bsd_to_linux_sig[WTERMSIG(*status)];
	else if (WIFSTOPPED(*status))
		*status = (*status & ~0xff00) |
		    (bsd_to_linux_sig[WSTOPSIG(*status)] << 8);
}

/*
 * waitpid(2). Passed on to the OpenBSD call, surrounded by code to
 * reserve some space for a OpenBSD-style wait status, and converting
 * it to what Linux wants.
 */
int
linux_sys_waitpid(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_waitpid_args /* {
		syscallarg(int) pid;
		syscallarg(int *) status;
		syscallarg(int) options;
	} */ *uap = v;
	struct sys_wait4_args w4a;
	int error, *status, tstat;
	caddr_t sg;

	if (SCARG(uap, status) != NULL) {
		sg = stackgap_init(p->p_emul);
		status = (int *) stackgap_alloc(&sg, sizeof status);
	} else
		status = NULL;

	SCARG(&w4a, pid) = SCARG(uap, pid);
	SCARG(&w4a, status) = status;
	SCARG(&w4a, options) = SCARG(uap, options);
	SCARG(&w4a, rusage) = NULL;

	if ((error = sys_wait4(p, &w4a, retval)))
		return error;

	p->p_siglist &= ~sigmask(SIGCHLD);

	if (status != NULL) {
		if ((error = copyin(status, &tstat, sizeof tstat)))
			return error;

		bsd_to_linux_wstat(&tstat);
		return copyout(&tstat, SCARG(uap, status), sizeof tstat);
	}

	return 0;
}

/*
 * This is very much the same as waitpid()
 */
int
linux_sys_wait4(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_wait4_args /* {
		syscallarg(int) pid;
		syscallarg(int *) status;
		syscallarg(int) options;
		syscallarg(struct rusage *) rusage;
	} */ *uap = v;
	struct sys_wait4_args w4a;
	int error, *status, tstat, linux_options, options;
	caddr_t sg;

	if (SCARG(uap, status) != NULL) {
		sg = stackgap_init(p->p_emul);
		status = (int *) stackgap_alloc(&sg, sizeof status);
	} else
		status = NULL;

	linux_options = SCARG(uap, options);
	options = 0;
	if (linux_options &
	    ~(LINUX_WAIT4_WNOHANG|LINUX_WAIT4_WUNTRACED|LINUX_WAIT4_WCLONE))
		return (EINVAL);

	if (linux_options & LINUX_WAIT4_WNOHANG)
		options |= WNOHANG;
	if (linux_options & LINUX_WAIT4_WUNTRACED)
		options |= WUNTRACED;
	if (linux_options & LINUX_WAIT4_WCLONE)
		options |= WALTSIG;

	SCARG(&w4a, pid) = SCARG(uap, pid);
	SCARG(&w4a, status) = status;
	SCARG(&w4a, options) = options;
	SCARG(&w4a, rusage) = SCARG(uap, rusage);

	if ((error = sys_wait4(p, &w4a, retval)))
		return error;

	p->p_siglist &= ~sigmask(SIGCHLD);

	if (status != NULL) {
		if ((error = copyin(status, &tstat, sizeof tstat)))
			return error;

		bsd_to_linux_wstat(&tstat);
		return copyout(&tstat, SCARG(uap, status), sizeof tstat);
	}

	return 0;
}

int
linux_sys_setresgid16(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_setresgid16_args /* {
		syscallarg(gid_t) rgid;
		syscallarg(gid_t) egid;
		syscallarg(gid_t) sgid;
	} */ *uap = v;
	struct pcred *pc = p->p_cred;
	gid_t rgid, egid, sgid;
	int error;

	rgid = SCARG(uap, rgid);
	egid = SCARG(uap, egid);
	sgid = SCARG(uap, sgid);

	/*
	 * Note: These checks are a little different than the OpenBSD
	 * setregid(2) call performs.  This precisely follows the
	 * behavior of the Linux kernel.
	 */
	if (rgid != (gid_t)-1 &&
	    rgid != pc->p_rgid &&
	    rgid != pc->pc_ucred->cr_gid &&
	    rgid != pc->p_svgid &&
	    (error = suser(pc->pc_ucred, &p->p_acflag)))
		return (error);

	if (egid != (gid_t)-1 &&
	    egid != pc->p_rgid &&
	    egid != pc->pc_ucred->cr_gid &&
	    egid != pc->p_svgid &&
	    (error = suser(pc->pc_ucred, &p->p_acflag)))
		return (error);

	if (sgid != (gid_t)-1 &&
	    sgid != pc->p_rgid &&
	    sgid != pc->pc_ucred->cr_gid &&
	    sgid != pc->p_svgid &&
	    (error = suser(pc->pc_ucred, &p->p_acflag)))
		return (error);

	/*
	 * Now assign the real, effective, and saved GIDs.
	 * Note that Linux, unlike OpenBSD in setregid(2), does not
	 * set the saved UID in this call unless the user specifies
	 * it.
	 */
	if (rgid != (gid_t)-1)
		pc->p_rgid = rgid;

	if (egid != (gid_t)-1) {
		pc->pc_ucred = crcopy(pc->pc_ucred);
		pc->pc_ucred->cr_gid = egid;
	}

	if (sgid != (gid_t)-1)
		pc->p_svgid = sgid;

	if (rgid != (gid_t)-1 && egid != (gid_t)-1 && sgid != (gid_t)-1)
		p->p_flag |= P_SUGID;
	return (0);
}

int
linux_sys_getresgid16(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_getresgid16_args /* {
		syscallarg(gid_t *) rgid;
		syscallarg(gid_t *) egid;
		syscallarg(gid_t *) sgid;
	} */ *uap = v;
	struct pcred *pc = p->p_cred;
	int error;

	/*
	 * Linux copies these values out to userspace like so:
	 *
	 *	1. Copy out rgid.
	 *	2. If that succeeds, copy out egid.
	 *	3. If both of those succeed, copy out sgid.
	 */
	if ((error = copyout(&pc->p_rgid, SCARG(uap, rgid),
			     sizeof(gid_t))) != 0)
		return (error);

	if ((error = copyout(&pc->pc_ucred->cr_uid, SCARG(uap, egid),
			     sizeof(gid_t))) != 0)
		return (error);

	return (copyout(&pc->p_svgid, SCARG(uap, sgid), sizeof(gid_t)));
}

/*
 * This is the old brk(2) call. I don't think anything in the Linux
 * world uses this anymore
 */
int
linux_sys_break(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
#if 0
	struct linux_sys_brk_args /* {
		syscallarg(char *) nsize;
	} */ *uap = v;
#endif

	return ENOSYS;
}

/*
 * Linux brk(2). The check if the new address is >= the old one is
 * done in the kernel in Linux. OpenBSD does it in the library.
 */
int
linux_sys_brk(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_brk_args /* {
		syscallarg(char *) nsize;
	} */ *uap = v;
	char *nbrk = SCARG(uap, nsize);
	struct sys_obreak_args oba;
	struct vmspace *vm = p->p_vmspace;
	caddr_t oldbrk;

	oldbrk = vm->vm_daddr + ctob(vm->vm_dsize);
	/*
	 * XXX inconsistent.. Linux always returns at least the old
	 * brk value, but it will be page-aligned if this fails,
	 * and possibly not page aligned if it succeeds (the user
	 * supplied pointer is returned).
	 */
	SCARG(&oba, nsize) = nbrk;

	if ((caddr_t) nbrk > vm->vm_daddr && sys_obreak(p, &oba, retval) == 0)
		retval[0] = (register_t)nbrk;
	else
		retval[0] = (register_t)oldbrk;

	return 0;
}

/*
 * I wonder why Linux has gettimeofday() _and_ time().. Still, we
 * need to deal with it.
 */
int
linux_sys_time(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_time_args /* {
		linux_time_t *t;
	} */ *uap = v;
	struct timeval atv;
	linux_time_t tt;
	int error;

	microtime(&atv);

	tt = atv.tv_sec;
	if (SCARG(uap, t) && (error = copyout(&tt, SCARG(uap, t), sizeof tt)))
		return error;

	retval[0] = tt;
	return 0;
}

/*
 * Convert BSD statfs structure to Linux statfs structure.
 * The Linux structure has less fields, and it also wants
 * the length of a name in a dir entry in a field, which
 * we fake (probably the wrong way).
 */
static void
bsd_to_linux_statfs(bsp, lsp)
	struct statfs *bsp;
	struct linux_statfs *lsp;
{

	/*
	 * Convert BSD filesystem names to Linux filesystem type numbers
	 * where possible.  Linux statfs uses a value of -1 to indicate
	 * an unsupported field.
	 */
	if (!strcmp(bsp->f_fstypename, MOUNT_FFS) ||
	    !strcmp(bsp->f_fstypename, MOUNT_MFS))
		lsp->l_ftype = 0x11954;
	else if (!strcmp(bsp->f_fstypename, MOUNT_NFS))
		lsp->l_ftype = 0x6969;
	else if (!strcmp(bsp->f_fstypename, MOUNT_MSDOS))
		lsp->l_ftype = 0x4d44;
	else if (!strcmp(bsp->f_fstypename, MOUNT_PROCFS))
		lsp->l_ftype = 0x9fa0;
	else if (!strcmp(bsp->f_fstypename, MOUNT_EXT2FS))
		lsp->l_ftype = 0xef53;
	else if (!strcmp(bsp->f_fstypename, MOUNT_CD9660))
		lsp->l_ftype = 0x9660;
	else if (!strcmp(bsp->f_fstypename, MOUNT_NCPFS))
		lsp->l_ftype = 0x6969;
	else
		lsp->l_ftype = -1;

	lsp->l_fbsize = bsp->f_bsize;
	lsp->l_fblocks = bsp->f_blocks;
	lsp->l_fbfree = bsp->f_bfree;
	lsp->l_fbavail = bsp->f_bavail;
	lsp->l_ffiles = bsp->f_files;
	lsp->l_fffree = bsp->f_ffree;
	lsp->l_ffsid.val[0] = bsp->f_fsid.val[0];
	lsp->l_ffsid.val[1] = bsp->f_fsid.val[1];
	lsp->l_fnamelen = MAXNAMLEN;	/* XXX */
}

/*
 * Implement the fs stat functions. Straightforward.
 */
int
linux_sys_statfs(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_statfs_args /* {
		syscallarg(char *) path;
		syscallarg(struct linux_statfs *) sp;
	} */ *uap = v;
	struct statfs btmp, *bsp;
	struct linux_statfs ltmp;
	struct sys_statfs_args bsa;
	caddr_t sg;
	int error;

	sg = stackgap_init(p->p_emul);
	bsp = (struct statfs *) stackgap_alloc(&sg, sizeof (struct statfs));

	LINUX_CHECK_ALT_EXIST(p, &sg, SCARG(uap, path));

	SCARG(&bsa, path) = SCARG(uap, path);
	SCARG(&bsa, buf) = bsp;

	if ((error = sys_statfs(p, &bsa, retval)))
		return error;

	if ((error = copyin((caddr_t) bsp, (caddr_t) &btmp, sizeof btmp)))
		return error;

	bsd_to_linux_statfs(&btmp, &ltmp);

	return copyout((caddr_t) &ltmp, (caddr_t) SCARG(uap, sp), sizeof ltmp);
}

int
linux_sys_fstatfs(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_fstatfs_args /* {
		syscallarg(int) fd;
		syscallarg(struct linux_statfs *) sp;
	} */ *uap = v;
	struct statfs btmp, *bsp;
	struct linux_statfs ltmp;
	struct sys_fstatfs_args bsa;
	caddr_t sg;
	int error;

	sg = stackgap_init(p->p_emul);
	bsp = (struct statfs *) stackgap_alloc(&sg, sizeof (struct statfs));

	SCARG(&bsa, fd) = SCARG(uap, fd);
	SCARG(&bsa, buf) = bsp;

	if ((error = sys_fstatfs(p, &bsa, retval)))
		return error;

	if ((error = copyin((caddr_t) bsp, (caddr_t) &btmp, sizeof btmp)))
		return error;

	bsd_to_linux_statfs(&btmp, &ltmp);

	return copyout((caddr_t) &ltmp, (caddr_t) SCARG(uap, sp), sizeof ltmp);
}

/*
 * uname(). Just copy the info from the various strings stored in the
 * kernel, and put it in the Linux utsname structure. That structure
 * is almost the same as the OpenBSD one, only it has fields 65 characters
 * long, and an extra domainname field.
 */
int
linux_sys_uname(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_uname_args /* {
		syscallarg(struct linux_utsname *) up;
	} */ *uap = v;
	extern char ostype[], hostname[], osrelease[], version[], machine[],
	    domainname[];
	struct linux_utsname luts;
	int len;
	char *cp;

	strncpy(luts.l_sysname, ostype, sizeof(luts.l_sysname) - 1);
	luts.l_sysname[sizeof(luts.l_sysname) - 1] = '\0';
	strncpy(luts.l_nodename, hostname, sizeof(luts.l_nodename) - 1);
	luts.l_nodename[sizeof(luts.l_nodename) - 1] = '\0';
	strncpy(luts.l_release, osrelease, sizeof(luts.l_release) - 1);
	luts.l_release[sizeof(luts.l_release) - 1] = '\0';
	strncpy(luts.l_version, version, sizeof(luts.l_version) - 1);
	luts.l_version[sizeof(luts.l_version) - 1] = '\0';
	strncpy(luts.l_machine, machine, sizeof(luts.l_machine) - 1);
	luts.l_machine[sizeof(luts.l_machine) - 1] = '\0';
	strncpy(luts.l_domainname, domainname, sizeof(luts.l_domainname) - 1);
	luts.l_domainname[sizeof(luts.l_domainname) - 1] = '\0';

	/* This part taken from the the uname() in libc */
	len = sizeof(luts.l_version);
	for (cp = luts.l_version; len--; ++cp)
		if (*cp == '\n' || *cp == '\t')
			*cp = (len > 1) ? ' ' : '\0';

	return copyout(&luts, SCARG(uap, up), sizeof(luts));
}

int
linux_sys_olduname(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_uname_args /* {
		syscallarg(struct linux_oldutsname *) up;
	} */ *uap = v;
	extern char ostype[], hostname[], osrelease[], version[], machine[];
	struct linux_oldutsname luts;
	int len;
	char *cp;

	strncpy(luts.l_sysname, ostype, sizeof(luts.l_sysname));
	strncpy(luts.l_nodename, hostname, sizeof(luts.l_nodename));
	strncpy(luts.l_release, osrelease, sizeof(luts.l_release));
	strncpy(luts.l_version, version, sizeof(luts.l_version));
	strncpy(luts.l_machine, machine, sizeof(luts.l_machine));

	/* This part taken from the the uname() in libc */
	len = sizeof(luts.l_version);
	for (cp = luts.l_version; len--; ++cp)
		if (*cp == '\n' || *cp == '\t')
			*cp = (len > 1) ? ' ' : '\0';

	return copyout(&luts, SCARG(uap, up), sizeof(luts));
}

int
linux_sys_oldolduname(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_uname_args /* {
		syscallarg(struct linux_oldoldutsname *) up;
	} */ *uap = v;
	extern char ostype[], hostname[], osrelease[], version[], machine[];
	struct linux_oldoldutsname luts;
	int len;
	char *cp;

	strncpy(luts.l_sysname, ostype, sizeof(luts.l_sysname));
	strncpy(luts.l_nodename, hostname, sizeof(luts.l_nodename));
	strncpy(luts.l_release, osrelease, sizeof(luts.l_release));
	strncpy(luts.l_version, version, sizeof(luts.l_version));
	strncpy(luts.l_machine, machine, sizeof(luts.l_machine));

	/* This part taken from the the uname() in libc */
	len = sizeof(luts.l_version);
	for (cp = luts.l_version; len--; ++cp)
		if (*cp == '\n' || *cp == '\t')
			*cp = (len > 1) ? ' ' : '\0';

	return copyout(&luts, SCARG(uap, up), sizeof(luts));
}

/*
 * Linux wants to pass everything to a syscall in registers. However,
 * mmap() has 6 of them. Oops: out of register error. They just pass
 * everything in a structure.
 */
int
linux_sys_mmap(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_mmap_args /* {
		syscallarg(struct linux_mmap *) lmp;
	} */ *uap = v;
	struct linux_mmap lmap;
	struct sys_mmap_args cma;
	int error, flags;

	if ((error = copyin(SCARG(uap, lmp), &lmap, sizeof lmap)))
		return error;

	flags = 0;
	flags |= cvtto_bsd_mask(lmap.lm_flags, LINUX_MAP_SHARED, MAP_SHARED);
	flags |= cvtto_bsd_mask(lmap.lm_flags, LINUX_MAP_PRIVATE, MAP_PRIVATE);
	flags |= cvtto_bsd_mask(lmap.lm_flags, LINUX_MAP_FIXED, MAP_FIXED);
	flags |= cvtto_bsd_mask(lmap.lm_flags, LINUX_MAP_ANON, MAP_ANON);

	SCARG(&cma,addr) = lmap.lm_addr;
	SCARG(&cma,len) = lmap.lm_len;
	if (lmap.lm_prot & VM_PROT_WRITE)	/* XXX */
		lmap.lm_prot |= VM_PROT_READ;
 	SCARG(&cma,prot) = lmap.lm_prot;
	SCARG(&cma,flags) = flags;
	SCARG(&cma,fd) = flags & MAP_ANON ? -1 : lmap.lm_fd;
	SCARG(&cma,pad) = 0;
	SCARG(&cma,pos) = lmap.lm_pos;

	return sys_mmap(p, &cma, retval);
}

int
linux_sys_mremap(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{

	struct linux_sys_mremap_args /* {
		syscallarg(void *) old_address;
		syscallarg(size_t) old_size;
		syscallarg(size_t) new_size;
		syscallarg(u_long) flags;
	} */ *uap = v;
	struct sys_munmap_args mua;
	size_t old_size, new_size;
	int error;
 
	old_size = round_page(SCARG(uap, old_size));
	new_size = round_page(SCARG(uap, new_size));
 
	/*
	 * Growing mapped region.
	 */
	if (new_size > old_size) {
		/*
		 * XXX Implement me.  What we probably want to do is
		 * XXX dig out the guts of the old mapping, mmap that
		 * XXX object again with the new size, then munmap
		 * XXX the old mapping.
		 */
		*retval = 0;
		return (ENOMEM);
	}
	/*
	 * Shrinking mapped region.
	 */
	if (new_size < old_size) {
		SCARG(&mua, addr) = (caddr_t)SCARG(uap, old_address) + new_size;
		SCARG(&mua, len) = old_size - new_size;
		error = sys_munmap(p, &mua, retval);
		*retval = error ? 0 : (register_t)SCARG(uap, old_address);
		return (error);
	}
 
	/*
	 * No change.
	 */
	*retval = (register_t)SCARG(uap, old_address);
	return (0);

}

/*
 * This code is partly stolen from src/lib/libc/gen/times.c
 * XXX - CLK_TCK isn't declared in /sys, just in <time.h>, done here
 */

#define CLK_TCK 100
#define	CONVTCK(r)	(r.tv_sec * CLK_TCK + r.tv_usec / (1000000 / CLK_TCK))

int
linux_sys_times(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_times_args /* {
		syscallarg(struct times *) tms;
	} */ *uap = v;
	struct timeval t;
	struct linux_tms ltms;
	struct rusage ru;
	int error, s;

	calcru(p, &ru.ru_utime, &ru.ru_stime, NULL);
	ltms.ltms_utime = CONVTCK(ru.ru_utime);
	ltms.ltms_stime = CONVTCK(ru.ru_stime);

	ltms.ltms_cutime = CONVTCK(p->p_stats->p_cru.ru_utime);
	ltms.ltms_cstime = CONVTCK(p->p_stats->p_cru.ru_stime);

	if ((error = copyout(&ltms, SCARG(uap, tms), sizeof ltms)))
		return error;

	s = splclock();
	timersub(&time, &boottime, &t);
	splx(s);

	retval[0] = ((linux_clock_t)(CONVTCK(t)));
	return 0;
}

/*
 * OpenBSD passes fd[0] in retval[0], and fd[1] in retval[1].
 * Linux directly passes the pointer.
 */
int
linux_sys_pipe(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_pipe_args /* {
		syscallarg(int *) pfds;
	} */ *uap = v;
	int error;
	int pfds[2];
#ifdef __i386__
	int reg_edx = retval[1];
#endif /* __i386__ */

	if ((error = sys_opipe(p, 0, retval))) {
#ifdef __i386__
		retval[1] = reg_edx;
#endif /* __i386__ */
		return error;
	}

	/* Assumes register_t is an int */

	pfds[0] = retval[0];
	pfds[1] = retval[1];
	if ((error = copyout(pfds, SCARG(uap, pfds), 2 * sizeof (int)))) {
#ifdef __i386__
		retval[1] = reg_edx;
#endif /* __i386__ */
		fdrelease(p, retval[0]);
		fdrelease(p, retval[1]);
		return error;
	}

	retval[0] = 0;
#ifdef __i386__
	retval[1] = reg_edx;
#endif /* __i386__ */
	return 0;
}

/*
 * Alarm. This is a libc call which uses setitimer(2) in OpenBSD.
 * Fiddle with the timers to make it work.
 */
int
linux_sys_alarm(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_alarm_args /* {
		syscallarg(unsigned int) secs;
	} */ *uap = v;
	int s;
	struct itimerval *itp, it;
	int timo;

	itp = &p->p_realtimer;
	s = splclock();
	/*
	 * Clear any pending timer alarms.
	 */

	timeout_del(&p->p_realit_to);
	timerclear(&itp->it_interval);
	if (timerisset(&itp->it_value) &&
	    timercmp(&itp->it_value, &time, >))
		timersub(&itp->it_value, &time, &itp->it_value);
	/*
	 * Return how many seconds were left (rounded up)
	 */
	retval[0] = itp->it_value.tv_sec;
	if (itp->it_value.tv_usec)
		retval[0]++;

	/*
	 * alarm(0) just resets the timer.
	 */
	if (SCARG(uap, secs) == 0) {
		timerclear(&itp->it_value);
		splx(s);
		return 0;
	}

	/*
	 * Check the new alarm time for sanity, and set it.
	 */
	timerclear(&it.it_interval);
	it.it_value.tv_sec = SCARG(uap, secs);
	it.it_value.tv_usec = 0;
	if (itimerfix(&it.it_value) || itimerfix(&it.it_interval)) {
		splx(s);
		return (EINVAL);
	}

	if (timerisset(&it.it_value)) {
		timo = hzto(&it.it_value);
		if (timo <= 0)
			timo = 1;
		timeradd(&it.it_value, &time, &it.it_value);
		timeout_add(&p->p_realit_to, timo);
	}
	p->p_realtimer = it;
	splx(s);

	return 0;
}

/*
 * utime(). Do conversion to things that utimes() understands, 
 * and pass it on.
 */
int
linux_sys_utime(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_utime_args /* {
		syscallarg(char *) path;
		syscallarg(struct linux_utimbuf *)times;
	} */ *uap = v;
	caddr_t sg;
	int error;
	struct sys_utimes_args ua;
	struct timeval tv[2], *tvp;
	struct linux_utimbuf lut;

	sg = stackgap_init(p->p_emul);
	tvp = (struct timeval *) stackgap_alloc(&sg, sizeof(tv));
	LINUX_CHECK_ALT_EXIST(p, &sg, SCARG(uap, path));

	SCARG(&ua, path) = SCARG(uap, path);

	if (SCARG(uap, times) != NULL) {
		if ((error = copyin(SCARG(uap, times), &lut, sizeof lut)))
			return error;
		tv[0].tv_usec = tv[1].tv_usec = 0;
		tv[0].tv_sec = lut.l_actime;
		tv[1].tv_sec = lut.l_modtime;
		if ((error = copyout(tv, tvp, sizeof tv)))
			return error;
		SCARG(&ua, tptr) = tvp;
	}
	else
		SCARG(&ua, tptr) = NULL;

	return sys_utimes(p, &ua, retval);
}

/*
 * The old Linux readdir was only able to read one entry at a time,
 * even though it had a 'count' argument. In fact, the emulation
 * of the old call was better than the original, because it did handle
 * the count arg properly. Don't bother with it anymore now, and use
 * it to distinguish between old and new. The difference is that the
 * newer one actually does multiple entries, and the reclen field
 * really is the reclen, not the namelength.
 */
int
linux_sys_readdir(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_readdir_args /* {
		syscallarg(int) fd;
		syscallarg(struct linux_dirent *) dent;
		syscallarg(unsigned int) count;
	} */ *uap = v;

	SCARG(uap, count) = 1;

	return linux_sys_getdents(p, uap, retval);
}

/*
 * Linux 'readdir' call. This code is mostly taken from the
 * SunOS getdents call (see compat/sunos/sunos_misc.c), though
 * an attempt has been made to keep it a little cleaner (failing
 * miserably, because of the cruft needed if count 1 is passed).
 *
 * The d_off field should contain the offset of the next valid entry,
 * but in Linux it has the offset of the entry itself. We emulate
 * that bug here.
 *
 * Read in BSD-style entries, convert them, and copy them out.
 *
 * Note that this doesn't handle union-mounted filesystems.
 */
int linux_readdir_callback __P((void *, struct dirent *, off_t));

struct linux_readdir_callback_args {
	caddr_t outp;
	int     resid;
	int     oldcall;
};

int
linux_readdir_callback(arg, bdp, cookie)
	void *arg;
	struct dirent *bdp;
	off_t cookie;
{
	struct linux_dirent idb;
	struct linux_readdir_callback_args *cb = arg;
	int linux_reclen;
	int error;

	if (cb->oldcall == 2) 
		return (ENOMEM);

	linux_reclen = LINUX_RECLEN(&idb, bdp->d_namlen);
	if (cb->resid < linux_reclen)
		return (ENOMEM);

	/*
	 * Massage in place to make a Linux-shaped dirent (otherwise
	 * we have to worry about touching user memory outside of
	 * the copyout() call).
	 */
	idb.d_ino = (linux_ino_t)bdp->d_fileno;
	
	/*
	 * The old readdir() call misuses the offset and reclen fields.
	 */
	if (cb->oldcall) {
		idb.d_off = (linux_off_t)linux_reclen;
		idb.d_reclen = (u_short)bdp->d_namlen;
	} else {
		idb.d_off = (linux_off_t)cookie;
		idb.d_reclen = (u_short)linux_reclen;
	}
	
	strlcpy(idb.d_name, bdp->d_name, sizeof(idb.d_name));
	if ((error = copyout((caddr_t)&idb, cb->outp, linux_reclen)))
		return (error);

	/* advance output past Linux-shaped entry */
	cb->outp += linux_reclen;
	cb->resid -= linux_reclen;

	if (cb->oldcall == 1)
		++cb->oldcall;
	
	return (0);
}

int
linux_sys_getdents(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_getdents_args /* {
		syscallarg(int) fd;
		syscallarg(void *) dirent;
		syscallarg(unsigned) count;
	} */ *uap = v;
	struct linux_readdir_callback_args args;
	struct file *fp;
	int error;
	int nbytes = SCARG(uap, count);

	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);

	if (nbytes == 1) {	/* emulating old, broken behaviour */
		nbytes = sizeof(struct linux_dirent);
		args.oldcall = 1;
	} else {
		args.oldcall = 0;
	}

	args.resid = nbytes;
	args.outp = (caddr_t)SCARG(uap, dirent);

	if ((error = readdir_with_callback(fp, &fp->f_offset, nbytes,
	    linux_readdir_callback, &args)) != 0)
		goto exit;

	*retval = nbytes - args.resid;

 exit:
	return (error);
}

/*
 * Not sure why the arguments to this older version of select() were put
 * into a structure, because there are 5, and that can all be handled
 * in registers on the i386 like Linux wants to.
 */
int
linux_sys_oldselect(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_oldselect_args /* {
		syscallarg(struct linux_select *) lsp;
	} */ *uap = v;
	struct linux_select ls;
	int error;

	if ((error = copyin(SCARG(uap, lsp), &ls, sizeof(ls))))
		return error;

	return linux_select1(p, retval, ls.nfds, ls.readfds, ls.writefds,
	    ls.exceptfds, ls.timeout);
}

/*
 * Even when just using registers to pass arguments to syscalls you can
 * have 5 of them on the i386. So this newer version of select() does
 * this.
 */
int
linux_sys_select(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_select_args /* {
		syscallarg(int) nfds;
		syscallarg(fd_set *) readfds;
		syscallarg(fd_set *) writefds;
		syscallarg(fd_set *) exceptfds;
		syscallarg(struct timeval *) timeout;
	} */ *uap = v;

	return linux_select1(p, retval, SCARG(uap, nfds), SCARG(uap, readfds),
	    SCARG(uap, writefds), SCARG(uap, exceptfds), SCARG(uap, timeout));
}

/*
 * Common code for the old and new versions of select(). A couple of
 * things are important:
 * 1) return the amount of time left in the 'timeout' parameter
 * 2) select never returns ERESTART on Linux, always return EINTR
 */
int
linux_select1(p, retval, nfds, readfds, writefds, exceptfds, timeout)
	struct proc *p;
	register_t *retval;
	int nfds;
	fd_set *readfds, *writefds, *exceptfds;
	struct timeval *timeout;
{
	struct sys_select_args bsa;
	struct timeval tv0, tv1, utv, *tvp;
	caddr_t sg;
	int error;

	SCARG(&bsa, nd) = nfds;
	SCARG(&bsa, in) = readfds;
	SCARG(&bsa, ou) = writefds;
	SCARG(&bsa, ex) = exceptfds;
	SCARG(&bsa, tv) = timeout;

	/*
	 * Store current time for computation of the amount of
	 * time left.
	 */
	if (timeout) {
		if ((error = copyin(timeout, &utv, sizeof(utv))))
			return error;
		if (itimerfix(&utv)) {
			/*
			 * The timeval was invalid.  Convert it to something
			 * valid that will act as it does under Linux.
			 */
			sg = stackgap_init(p->p_emul);
			tvp = stackgap_alloc(&sg, sizeof(utv));
			utv.tv_sec += utv.tv_usec / 1000000;
			utv.tv_usec %= 1000000;
			if (utv.tv_usec < 0) {
				utv.tv_sec -= 1;
				utv.tv_usec += 1000000;
			}
			if (utv.tv_sec < 0)
				timerclear(&utv);
			if ((error = copyout(&utv, tvp, sizeof(utv))))
				return error;
			SCARG(&bsa, tv) = tvp;
		}
		microtime(&tv0);
	}

	error = sys_select(p, &bsa, retval);
	if (error) {
		/*
		 * See fs/select.c in the Linux kernel.  Without this,
		 * Maelstrom doesn't work.
		 */
		if (error == ERESTART)
			error = EINTR;
		return error;
	}

	if (timeout) {
		if (*retval) {
			/*
			 * Compute how much time was left of the timeout,
			 * by subtracting the current time and the time
			 * before we started the call, and subtracting
			 * that result from the user-supplied value.
			 */
			microtime(&tv1);
			timersub(&tv1, &tv0, &tv1);
			timersub(&utv, &tv1, &utv);
			if (utv.tv_sec < 0)
				timerclear(&utv);
		} else
			timerclear(&utv);
		if ((error = copyout(&utv, timeout, sizeof(utv))))
			return error;
	}

	return 0;
}

/*
 * Get the process group of a certain process. Look it up
 * and return the value.
 */
int
linux_sys_getpgid(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_getpgid_args /* {
		syscallarg(int) pid;
	} */ *uap = v;
	struct proc *targp;

	if (SCARG(uap, pid) != 0 && SCARG(uap, pid) != p->p_pid) {
		if ((targp = pfind(SCARG(uap, pid))) == 0)
			return ESRCH;
	}
	else
		targp = p;

	retval[0] = targp->p_pgid;
	return 0;
}

/*
 * Set the 'personality' (emulation mode) for the current process. Only
 * accept the Linux personality here (0). This call is needed because
 * the Linux ELF crt0 issues it in an ugly kludge to make sure that
 * ELF binaries run in Linux mode, not SVR4 mode.
 */
int
linux_sys_personality(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_personality_args /* {
		syscallarg(int) per;
	} */ *uap = v;

	if (SCARG(uap, per) != 0)
		return EINVAL;
	retval[0] = 0;
	return 0;
}

/*
 * The calls are here because of type conversions.
 */
int
linux_sys_setreuid16(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_setreuid16_args /* {
		syscallarg(int) ruid;
		syscallarg(int) euid;
	} */ *uap = v;
	struct compat_43_sys_setreuid_args bsa;
	
	SCARG(&bsa, ruid) = ((linux_uid_t)SCARG(uap, ruid) == (linux_uid_t)-1) ?
		(uid_t)-1 : SCARG(uap, ruid);
	SCARG(&bsa, euid) = ((linux_uid_t)SCARG(uap, euid) == (linux_uid_t)-1) ?
		(uid_t)-1 : SCARG(uap, euid);

	return compat_43_sys_setreuid(p, &bsa, retval);
}

int
linux_sys_setregid16(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_setregid16_args /* {
		syscallarg(int) rgid;
		syscallarg(int) egid;
	} */ *uap = v;
	struct compat_43_sys_setregid_args bsa;
	
	SCARG(&bsa, rgid) = ((linux_gid_t)SCARG(uap, rgid) == (linux_gid_t)-1) ?
		(uid_t)-1 : SCARG(uap, rgid);
	SCARG(&bsa, egid) = ((linux_gid_t)SCARG(uap, egid) == (linux_gid_t)-1) ?
		(uid_t)-1 : SCARG(uap, egid);

	return compat_43_sys_setregid(p, &bsa, retval);
}

int
linux_sys_getsid(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_getsid_args /* {
		syscallarg(int) pid;
	} */ *uap = v;
	struct proc *p1;
	pid_t pid;

	pid = (pid_t)SCARG(uap, pid);

	if (pid == 0) {
		retval[0] = (int)p->p_session;	/* XXX Oh well */
		return 0;
	}

	p1 = pfind((int)pid);
	if (p1 == NULL)
		return ESRCH;

	retval[0] = (int)p1->p_session;
	return 0;
}

int
linux_sys___sysctl(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys___sysctl_args /* {
		syscallarg(struct linux___sysctl *) lsp;
	} */ *uap = v;
	struct linux___sysctl ls;
	struct sys___sysctl_args bsa;
	int error;

	if ((error = copyin(SCARG(uap, lsp), &ls, sizeof ls)))
		return error;
	SCARG(&bsa, name) = ls.name;
	SCARG(&bsa, namelen) = ls.namelen;
	SCARG(&bsa, old) = ls.old;
	SCARG(&bsa, oldlenp) = ls.oldlenp;
	SCARG(&bsa, new) = ls.new;
	SCARG(&bsa, newlen) = ls.newlen;

	return sys___sysctl(p, &bsa, retval);
}

int
linux_sys_setresuid16(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_setresuid16_args /* {
		syscallarg(uid_t) ruid;
		syscallarg(uid_t) euid;
		syscallarg(uid_t) suid;
	} */ *uap = v;
	struct pcred *pc = p->p_cred;
	uid_t ruid, euid, suid;
	int error;

	ruid = SCARG(uap, ruid);
	euid = SCARG(uap, euid);
	suid = SCARG(uap, suid);

	/*
	 * Note: These checks are a little different than the OpenBSD
	 * setreuid(2) call performs.  This precisely follows the
	 * behavior of the Linux kernel.
	 */
	if (ruid != (uid_t)-1 &&
	    ruid != pc->p_ruid &&
	    ruid != pc->pc_ucred->cr_uid &&
	    ruid != pc->p_svuid &&
	    (error = suser(pc->pc_ucred, &p->p_acflag)))
		return (error);

	if (euid != (uid_t)-1 &&
	    euid != pc->p_ruid &&
	    euid != pc->pc_ucred->cr_uid &&
	    euid != pc->p_svuid &&
	    (error = suser(pc->pc_ucred, &p->p_acflag)))
		return (error);

	if (suid != (uid_t)-1 &&
	    suid != pc->p_ruid &&
	    suid != pc->pc_ucred->cr_uid &&
	    suid != pc->p_svuid &&
	    (error = suser(pc->pc_ucred, &p->p_acflag)))
		return (error);

	/*
	 * Now assign the new real, effective, and saved UIDs.
	 * Note that Linux, unlike OpenBSD in setreuid(2), does not
	 * set the saved UID in this call unless the user specifies
	 * it.
	 */
	if (ruid != (uid_t)-1) {
		(void)chgproccnt(pc->p_ruid, -1);
		(void)chgproccnt(ruid, 1);
		pc->p_ruid = ruid;
	}

	if (euid != (uid_t)-1) {
		pc->pc_ucred = crcopy(pc->pc_ucred);
		pc->pc_ucred->cr_uid = euid;
	}

	if (suid != (uid_t)-1)
		pc->p_svuid = suid;

	if (ruid != (uid_t)-1 && euid != (uid_t)-1 && suid != (uid_t)-1)
		p->p_flag |= P_SUGID;
	return (0);
}

int
linux_sys_getresuid(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_getresuid_args /* {
		syscallarg(uid_t *) ruid;
		syscallarg(uid_t *) euid;
		syscallarg(uid_t *) suid;
	} */ *uap = v;
	struct pcred *pc = p->p_cred;
	int error;

	/*
	 * Linux copies these values out to userspace like so:
	 *
	 *	1. Copy out ruid.
	 *	2. If that succeeds, copy out euid.
	 *	3. If both of those succeed, copy out suid.
	 */
	if ((error = copyout(&pc->p_ruid, SCARG(uap, ruid),
			     sizeof(uid_t))) != 0)
		return (error);

	if ((error = copyout(&pc->pc_ucred->cr_uid, SCARG(uap, euid),
			     sizeof(uid_t))) != 0)
		return (error);

	return (copyout(&pc->p_svuid, SCARG(uap, suid), sizeof(uid_t)));
}

/*
 * We have nonexistent fsuid equal to uid.
 * If modification is requested, refuse.
 */
int
linux_sys_setfsuid(p, v, retval)
	 struct proc *p;
	 void *v;
	 register_t *retval;
{
	 struct linux_sys_setfsuid_args /* {
		 syscallarg(uid_t) uid;
	 } */ *uap = v;
	 uid_t uid;

	 uid = SCARG(uap, uid);
	 if (p->p_cred->p_ruid != uid)
		 return sys_nosys(p, v, retval);
	 else
		 return (0);
}

int
linux_sys_getfsuid(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	return sys_getuid(p, v, retval);
}


int
linux_sys_nice(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_nice_args /* {
		syscallarg(int) incr;
	} */ *uap = v;
	struct sys_setpriority_args bsa;

	SCARG(&bsa, which) = PRIO_PROCESS;
	SCARG(&bsa, who) = 0;
	SCARG(&bsa, prio) = SCARG(uap, incr);
	return sys_setpriority(p, &bsa, retval);
}

int
linux_sys_stime(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct linux_sys_time_args /* {
		linux_time_t *t;
	} */ *uap = v;
	struct timeval atv;
	linux_time_t tt;
	int error;

	if ((error = suser(p->p_ucred, &p->p_acflag)) != 0)
		return (error);

	if ((error = copyin(SCARG(uap, t), &tt, sizeof(tt))) != 0)
		return (error);

	atv.tv_sec = tt;
	atv.tv_usec = 0;

	settime(&atv);

	return 0;
}
