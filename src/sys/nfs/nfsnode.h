/*	$OpenBSD: nfsnode.h,v 1.22 2004/08/03 17:11:48 marius Exp $	*/
/*	$NetBSD: nfsnode.h,v 1.16 1996/02/18 11:54:04 fvdl Exp $	*/

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
 *
 *	@(#)nfsnode.h	8.9 (Berkeley) 5/14/95
 */


#ifndef _NFS_NFSNODE_H_
#define _NFS_NFSNODE_H_

#ifndef _NFS_NFS_H_
#include <nfs/nfs.h>
#endif

#include <sys/rwlock.h>

/*
 * Silly rename structure that hangs off the nfsnode until the name
 * can be removed by nfs_inactive()
 */
struct sillyrename {
	struct	ucred *s_cred;
	struct	vnode *s_dvp;
	long	s_namlen;
	char	s_name[20];
};

/*
 * This structure is used to save the logical directory offset to
 * NFS cookie mappings.
 * The mappings are stored in a list headed
 * by n_cookies, as required.
 * There is one mapping for each NFS_DIRBLKSIZ bytes of directory information
 * stored in increasing logical offset byte order.
 */
#define NFSNUMCOOKIES		31

struct nfsdmap {
	LIST_ENTRY(nfsdmap)	ndm_list;
	int			ndm_eocookie;
	nfsuint64		ndm_cookies[NFSNUMCOOKIES];
};

/*
 * The nfsnode is the nfs equivalent to ufs's inode. Any similarity
 * is purely coincidental.
 * There is a unique nfsnode allocated for each active file,
 * each current directory, each mounted-on file, text file, and the root.
 * An nfsnode is 'named' by its file handle. (nget/nfs_node.c)
 * If this structure exceeds 256 bytes (it is currently 256 using 4.4BSD-Lite
 * type definitions), file handles of > 32 bytes should probably be split out
 * into a separate MALLOC()'d data structure. (Reduce the size of nfsfh_t by
 * changing the definition in sys/mount.h of NFS_SMALLFH.)
 * NB: Hopefully the current order of the fields is such that everything will
 *     be well aligned and, therefore, tightly packed.
 */
struct nfsnode {
	LIST_ENTRY(nfsnode)	n_hash;		/* Hash chain */
	CIRCLEQ_ENTRY(nfsnode)	n_timer;	/* Nqnfs timer chain */
	u_quad_t		n_size;		/* Current size of file */
	u_quad_t		n_brev;		/* Modify rev when cached */
	u_quad_t		n_lrev;		/* Modify rev for lease */
	struct vattr		n_vattr;	/* Vnode attribute cache */
	time_t			n_attrstamp;	/* Attr. cache timestamp */
	time_t			n_mtime;	/* Prev modify time. */
	time_t			n_ctime;	/* Prev create time. */
	time_t			n_expiry;	/* Lease expiry time */
	nfsfh_t			*n_fhp;		/* NFS File Handle */
	struct vnode		*n_vnode;	/* associated vnode */
	struct lockf		*n_lockf;	/* Locking record of file */
	int			n_error;	/* Save write error value */
	union {
		struct timespec	nf_atim;	/* Special file times */
		nfsuint64	nd_cookieverf;	/* Cookie verifier (dir only) */
	} n_un1;
	union {
		struct timespec	nf_mtim;
		off_t		nd_direof;	/* Dir. EOF offset cache */
	} n_un2;
	union {
		struct sillyrename *nf_silly;	/* Ptr to silly rename struct */
		LIST_HEAD(, nfsdmap) nd_cook;	/* cookies */
	} n_un3;
	short			n_fhsize;	/* size in bytes, of fh */
	short			n_flag;		/* Flag for locking.. */
	nfsfh_t			n_fh;		/* Small File Handle */
	struct ucred		*n_rcred;
	struct ucred		*n_wcred;

	off_t                    n_pushedlo;    /* 1st blk in commited range */
	off_t                    n_pushedhi;    /* Last block in range */
	off_t                    n_pushlo;      /* 1st block in commit range */
	off_t                    n_pushhi;      /* Last block in range */
	struct rwlock            n_commitlock;  /* Serialize commits */
	int                      n_commitflags;
};

/*
 * Values for n_commitflags
 */
#define NFS_COMMIT_PUSH_VALID   0x0001          /* push range valid */
#define NFS_COMMIT_PUSHED_VALID 0x0002          /* pushed range valid */

#define n_atim		n_un1.nf_atim
#define n_mtim		n_un2.nf_mtim
#define n_sillyrename	n_un3.nf_silly
#define n_cookieverf	n_un1.nd_cookieverf
#define n_direofoffset	n_un2.nd_direof
#define n_cookies	n_un3.nd_cook

/*
 * Flags for n_flag
 */
#define	NFLUSHWANT	0x0001	/* Want wakeup from a flush in prog. */
#define	NFLUSHINPROG	0x0002	/* Avoid multiple calls to vinvalbuf() */
#define	NMODIFIED	0x0004	/* Might have a modified buffer in bio */
#define	NWRITEERR	0x0008	/* Flag write errors so close will know */
#define	NACC		0x0100	/* Special file accessed */
#define	NUPD		0x0200	/* Special file updated */
#define	NCHG		0x0400	/* Special file times changed */

/*
 * Convert between nfsnode pointers and vnode pointers
 */
#define VTONFS(vp)	((struct nfsnode *)(vp)->v_data)
#define NFSTOV(np)	((np)->n_vnode)

/*
 * Queue head for nfsiod's
 */
extern TAILQ_HEAD(nfs_bufqhead, buf) nfs_bufq;

#ifdef _KERNEL
/*
 * Prototypes for NFS vnode operations
 */
int	nfs_lookup(void *);
int	nfs_create(void *);
int	nfs_mknod(void *);
int	nfs_open(void *);
int	nfs_close(void *);
int	nfsspec_close(void *);
int	nfsfifo_close(void *);
int	nfs_access(void *);
int	nfsspec_access(void *);
int	nfs_getattr(void *);
int	nfs_setattr(void *);
int	nfs_read(void *);
int	nfs_write(void *);
#define	nfs_lease_check ((int (*)(void *))nullop)
#define nqnfs_vop_lease_check	lease_check
int	nfsspec_read(void *);
int	nfsspec_write(void *);
int	nfsfifo_read(void *);
int	nfsfifo_write(void *);
#define nfs_ioctl ((int (*)(void *))enoioctl)
int	nfs_poll(void *);
#define nfs_revoke vop_generic_revoke
int	nfs_fsync(void *);
int	nfs_remove(void *);
int	nfs_link(void *);
int	nfs_rename(void *);
int	nfs_mkdir(void *);
int	nfs_rmdir(void *);
int	nfs_symlink(void *);
int	nfs_readdir(void *);
int	nfs_readlink(void *);
int	nfs_inactive(void *);
int	nfs_reclaim(void *);
int	nfsfifo_reclaim(void *);
#define nfs_lock ((int (*)(void *))vop_generic_lock)
#define nfs_unlock ((int (*)(void *))vop_generic_unlock)
#define nfs_islocked ((int (*)(void *))vop_generic_islocked)
int	nfs_bmap(void *);
int	nfs_strategy(void *);
int	nfs_print(void *);
int	nfs_pathconf(void *);
int	nfs_advlock(void *);
int	nfs_bwrite(void *);
int	nfs_vget(struct mount *, ino_t, struct vnode **);
#define nfs_reallocblks \
	((int (*)(void *))eopnotsupp)

/* other stuff */
int	nfs_removeit(struct sillyrename *);
int	nfs_nget(struct mount *,nfsfh_t *,int,struct nfsnode **);
int	nfs_lookitup(struct vnode *,char *,int,struct ucred *,struct proc *,struct nfsnode **);
int	nfs_sillyrename(struct vnode *,struct vnode *,struct componentname *);

extern int (**nfsv2_vnodeop_p)(void *);

#endif /* _KERNEL */

#endif
