/*	$OpenBSD: malloc.h,v 1.43 2001/05/16 08:59:03 art Exp $	*/
/*	$NetBSD: malloc.h,v 1.39 1998/07/12 19:52:01 augustss Exp $	*/

/*
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)malloc.h	8.5 (Berkeley) 5/3/95
 */

#ifndef _SYS_MALLOC_H_
#define	_SYS_MALLOC_H_

#define KERN_MALLOC_BUCKETS	1
#define KERN_MALLOC_BUCKET	2
#define KERN_MALLOC_KMEMNAMES	3
#define KERN_MALLOC_KMEMSTATS	4
#define KERN_MALLOC_MAXID	5

#define CTL_KERN_MALLOC_NAMES { \
	{ 0, 0 }, \
	{ "buckets", CTLTYPE_STRING }, \
	{ "bucket", CTLTYPE_NODE }, \
	{ "kmemnames", CTLTYPE_STRING }, \
	{ "kmemstat", CTLTYPE_NODE }, \
}

/*
 * flags to malloc
 */
#define	M_WAITOK	0x0000
#define	M_NOWAIT	0x0001

/*
 * Types of memory to be allocated
 */
#define	M_FREE		0	/* should be on free list */
#define	M_MBUF		1	/* mbuf */
#define	M_DEVBUF	2	/* device driver memory */
#define	M_SOCKET	3	/* socket structure */
#define	M_PCB		4	/* protocol control block */
#define	M_RTABLE	5	/* routing tables */
#define	M_HTABLE	6	/* IMP host tables */
#define	M_FTABLE	7	/* fragment reassembly header */
#define	M_ZOMBIE	8	/* zombie proc status */
#define	M_IFADDR	9	/* interface address */
#define	M_SOOPTS	10	/* socket options */
#define	M_SYSCTL	11	/* sysctl buffers (persistent storage) */
#define	M_NAMEI		12	/* namei path name buffer */
#define	M_GPROF		13	/* kernel profiling buffer */
#define	M_IOCTLOPS	14	/* ioctl data buffer */

#define	M_CRED		16	/* credentials */
#define	M_PGRP		17	/* process group header */
#define	M_SESSION	18	/* session header */
#define	M_IOV		19	/* large iov's */
#define	M_MOUNT		20	/* vfs mount struct */

#define	M_NFSREQ	22	/* NFS request header */
#define	M_NFSMNT	23	/* NFS mount structure */
#define	M_NFSNODE	24	/* NFS vnode private part */
#define	M_VNODE		25	/* Dynamically allocated vnodes */
#define	M_CACHE		26	/* Dynamically allocated cache entries */
#define	M_DQUOT		27	/* UFS quota entries */
#define	M_UFSMNT	28	/* UFS mount structure */
#define	M_SHM		29	/* SVID compatible shared memory segments */
#define	M_VMMAP		30	/* VM map structures */
#define	M_VMMAPENT	31	/* VM map entry structures */
#define	M_VMOBJ		32	/* VM object structure */
#define	M_VMOBJHASH	33	/* VM object hash structure */
#define	M_VMPMAP	34	/* VM pmap */
#define	M_VMPVENT	35	/* VM phys-virt mapping entry */
#define	M_VMPAGER	36	/* XXX: VM pager struct */
#define	M_VMPGDATA	37	/* XXX: VM pager private data */
#define	M_FILE		38	/* Open file structure */
#define	M_FILEDESC	39	/* Open file descriptor table */
#define	M_LOCKF		40	/* Byte-range locking structures */
#define	M_PROC		41	/* Proc structures */
#define	M_SUBPROC	42	/* Proc sub-structures */
#define	M_SEGMENT	43	/* Segment for LFS */
#define	M_LFSNODE	44	/* LFS vnode private part */
#define	M_FFSNODE	45	/* FFS vnode private part */
#define	M_MFSNODE	46	/* MFS vnode private part */
#define	M_NQLEASE	47	/* Nqnfs lease */
#define	M_NQMHOST	48	/* Nqnfs host address table */
#define	M_NETADDR	49	/* Export host address structure */
#define	M_NFSSVC	50	/* Nfs server structure */
#define	M_NFSUID	51	/* Nfs uid mapping structure */
#define	M_NFSD		52	/* Nfs server daemon structure */
#define	M_IPMOPTS	53	/* internet multicast options */
#define	M_IPMADDR	54	/* internet multicast address */
#define	M_IFMADDR	55	/* link-level multicast address */
#define	M_MRTABLE	56	/* multicast routing tables */
#define	M_ISOFSMNT	57	/* ISOFS mount structure */
#define	M_ISOFSNODE	58	/* ISOFS vnode private part */
#define	M_MSDOSFSMNT	59	/* MSDOS FS mount structure */
#define	M_MSDOSFSFAT	60	/* MSDOS FS fat table */
#define	M_MSDOSFSNODE	61	/* MSDOS FS vnode private part */
#define	M_TTYS		62	/* allocated tty structures */
#define	M_EXEC		63	/* argument lists & other mem used by exec */
#define	M_MISCFSMNT	64	/* miscfs mount structures */

#define	M_ADOSFSMNT	66	/* adosfs mount structures */

#define	M_ANODE		68	/* adosfs anode structures and tables. */
#define	M_IPQ		69	/* IP packet queue entry */
#define	M_AFS		70	/* Andrew File System */
#define	M_ADOSFSBITMAP	71	/* adosfs bitmap */
#define	M_EXT2FSNODE	72	/* EXT2FS vnode private part */
#define	M_PFIL		73	/* packer filter */
#define	M_PFKEY		74	/* pfkey data */
#define	M_TDB		75	/* Transforms database */
#define	M_XDATA		76	/* IPsec data */
#define M_VFS           77      /* VFS file systems */
#define	M_PAGEDEP	78	/* File page dependencies */
#define	M_INODEDEP	79	/* Inode dependencies */
#define	M_NEWBLK	80	/* New block allocation */
#define	M_BMSAFEMAP	81	/* Block or frag alloc'ed from cyl group map */
#define	M_ALLOCDIRECT	82	/* Block or frag dependency for an inode */
#define	M_INDIRDEP	83	/* Indirect block dependencies */
#define	M_ALLOCINDIR	84	/* Block dependency for an indirect block */
#define	M_FREEFRAG	85	/* Previously used frag for an inode */
#define	M_FREEBLKS	86	/* Blocks freed from an inode */
#define	M_FREEFILE	87	/* Inode deallocated */
#define	M_DIRADD	88	/* New directory entry */
#define	M_MKDIR		89	/* New directory */
#define	M_DIRREM	90	/* Directory entry deleted */
#define M_VMPBUCKET	91	/* VM page buckets */
#define M_VMSWAP	92	/* VM swap structures */

#define	M_RAIDFRAME	97	/* Raidframe data */
#define M_UVMAMAP	98	/* UVM amap and realted */
#define M_UVMAOBJ	99	/* UVM aobj and realted */
#define M_POOL		100	/* Pool memory */
#define	M_USB		101	/* USB general */
#define	M_USBDEV	102	/* USB device driver */
#define	M_USBHC		103	/* USB host controller */
#define M_PIPE		104	/* Pipe structures */
#define M_MEMDESC	105	/* Memory range */
#define M_DEBUG		106	/* MALLOC_DEBUG structures */
#define M_KNOTE		107	/* kernel event queue */  
#define M_CRYPTO_DATA   108	/* Crypto framework data buffers (keys etc.) */
#define M_IPSEC_POLICY  109	/* IPsec SPD structures */
#define M_CREDENTIALS   110	/* IPsec-related credentials and ID info */
#define M_PACKET_TAGS   111	/* Packet-attached information */
#define M_CRYPTO_OPS    112	/* Crypto framework operation structures */

/* KAME IPv6 */
#define	M_IP6OPT	123	/* IPv6 options */
#define	M_IP6NDP	124	/* IPv6 Neighbour Discovery */
#define	M_IP6RR		125	/* IPv6 Router Renumbering Prefix */
#define	M_RR_ADDR	126	/* IPv6 Router Renumbering Ifid */
#define	M_TEMP		127	/* misc temporary data buffers */
#define M_LAST          128     /* Must be last type + 1 */


#define	INITKMEMNAMES { \
	"free",		/* 0 M_FREE */ \
	"mbuf",		/* 1 M_MBUF */ \
	"devbuf",	/* 2 M_DEVBUF */ \
	"socket",	/* 3 M_SOCKET */ \
	"pcb",		/* 4 M_PCB */ \
	"routetbl",	/* 5 M_RTABLE */ \
	"hosttbl",	/* 6 M_HTABLE */ \
	"fragtbl",	/* 7 M_FTABLE */ \
	"zombie",	/* 8 M_ZOMBIE */ \
	"ifaddr",	/* 9 M_IFADDR */ \
	"soopts",	/* 10 M_SOOPTS */ \
	"sysctl",	/* 11 M_SYSCTL */ \
	"namei",	/* 12 M_NAMEI */ \
	"gprof",	/* 13 M_GPROF */ \
	"ioctlops",	/* 14 M_IOCTLOPS */ \
	NULL, \
	"cred",		/* 16 M_CRED */ \
	"pgrp",		/* 17 M_PGRP */ \
	"session",	/* 18 M_SESSION */ \
	"iov",		/* 19 M_IOV */ \
	"mount",	/* 20 M_MOUNT */ \
	"fhandle",	/* 21 M_FHANDLE */ \
	"NFS req",	/* 22 M_NFSREQ */ \
	"NFS mount",	/* 23 M_NFSMNT */ \
	"NFS node",	/* 24 M_NFSNODE */ \
	"vnodes",	/* 25 M_VNODE */ \
	"namecache",	/* 26 M_CACHE */ \
	"UFS quota",	/* 27 M_DQUOT */ \
	"UFS mount",	/* 28 M_UFSMNT */ \
	"shm",		/* 29 M_SHM */ \
	"VM map",	/* 30 M_VMMAP */ \
	"VM mapent",	/* 31 M_VMMAPENT */ \
	"VM object",	/* 32 M_VMOBJ */ \
	"VM objhash",	/* 33 M_VMOBJHASH */ \
	"VM pmap",	/* 34 M_VMPMAP */ \
	"VM pvmap",	/* 35 M_VMPVENT */ \
	"VM pager",	/* 36 M_VMPAGER */ \
	"VM pgdata",	/* 37 M_VMPGDATA */ \
	"file",		/* 38 M_FILE */ \
	"file desc",	/* 39 M_FILEDESC */ \
	"lockf",	/* 40 M_LOCKF */ \
	"proc",		/* 41 M_PROC */ \
	"subproc",	/* 42 M_SUBPROC */ \
	"LFS segment",	/* 43 M_SEGMENT */ \
	"LFS node",	/* 44 M_LFSNODE */ \
	"FFS node",	/* 45 M_FFSNODE */ \
	"MFS node",	/* 46 M_MFSNODE */ \
	"NQNFS Lease",	/* 47 M_NQLEASE */ \
	"NQNFS Host",	/* 48 M_NQMHOST */ \
	"Export Host",	/* 49 M_NETADDR */ \
	"NFS srvsock",	/* 50 M_NFSSVC */ \
	"NFS uid",	/* 51 M_NFSUID */ \
	"NFS daemon",	/* 52 M_NFSD */ \
	"ip_moptions",	/* 53 M_IPMOPTS */ \
	"in_multi",	/* 54 M_IPMADDR */ \
	"ether_multi",	/* 55 M_IFMADDR */ \
	"mrt",		/* 56 M_MRTABLE */ \
	"ISOFS mount",	/* 57 M_ISOFSMNT */ \
	"ISOFS node",	/* 58 M_ISOFSNODE */ \
	"MSDOSFS mount", /* 59 M_MSDOSFSMNT */ \
	"MSDOSFS fat",	/* 60 M_MSDOSFSFAT */ \
	"MSDOSFS node",	/* 61 M_MSDOSFSNODE */ \
	"ttys",		/* 62 M_TTYS */ \
	"exec",		/* 63 M_EXEC */ \
	"miscfs mount",	/* 64 M_MISCFSMNT */ \
	NULL, \
	"adosfs mount",	/* 66 M_ADOSFSMNT */ \
	NULL, \
	"adosfs anode",	/* 68 M_ANODE */ \
	"IP queue ent", /* 69 M_IPQ */ \
	"afs",		/* 70 M_AFS */ \
	"adosfs bitmap", /* 71 M_ADOSFSBITMAP */ \
	"EXT2FS node",	/* 72 M_EXT2FSNODE */ \
	"pfil",		/* 73 M_PFIL */ \
	"pfkey data",   /* 74 M_PFKEY */ \
	"tdb",		/* 75 M_TDB */ \
	"xform_data",	/* 76 M_XDATA */ \
	"vfs",          /* 77 M_VFS */ \
 	"pagedep",	/* 78 M_PAGEDEP */ \
 	"inodedep",	/* 79 M_INODEDEP */ \
 	"newblk",	/* 80 M_NEWBLK */ \
 	"bmsafemap",	/* 81 M_BMSAFEMAP */ \
 	"allocdirect",	/* 82 M_ALLOCDIRECT */ \
 	"indirdep",	/* 83 M_INDIRDEP */ \
 	"allocindir",	/* 84 M_ALLOCINDIR */ \
 	"freefrag",	/* 85 M_FREEFRAG */ \
 	"freeblks",	/* 86 M_FREEBLKS */ \
 	"freefile",	/* 87 M_FREEFILE */ \
 	"diradd",	/* 88 M_DIRADD */ \
 	"mkdir",	/* 89 M_MKDIR */ \
 	"dirrem",	/* 90 M_DIRREM */ \
 	"VM page bucket", /* 91 M_VMPBUCKET */ \
	"VM swap",	/* 92 M_VMSWAP */ \
	NULL, NULL, NULL, NULL, \
	"RaidFrame data", /* 97 M_RAIDFRAME */ \
	"UVM amap",	/* 98 M_UVMAMAP */ \
	"UVM aobj",	/* 99 M_UVMAOBJ */ \
	"pool",		/* 100 M_POOL */ \
	"USB",		/* 101 M_USB */ \
	"USB device",	/* 102 M_USBDEV */ \
	"USB HC",	/* 103 M_USBHC */ \
	"pipe", 	/* 104 M_PIPE */ \
	"memdesc",	/* 105 M_MEMDESC */ \
	"malloc debug",	/* 106 M_DEBUG */ \
	"knote",	/* 107 M_KNOTE */ \
	"crypto data",	/* 108 M_CRYPTO_DATA */ \
	"SPD info",	/* 109 M_IPSEC_POLICY */ \
	"IPsec credentials", /* 110 M_CREDENTIALS */ \
	"packet tags",	/* 111 M_PACKET_TAGS */ \
	"crypto ops",	/* 112 M_CRYPTO_OPS */ \
	NULL, NULL, NULL, NULL, \
	NULL, NULL, NULL, NULL, NULL, \
	NULL, \
	"ip6_options",	/* 123 M_IP6OPT */ \
	"NDP",		/* 124 M_IP6NDP */ \
	"ip6rr",	/* 125 M_IP6RR */ \
	"rp_addr",	/* 126 M_RR_ADDR */ \
	"temp",		/* 127 M_TEMP */ \
}

struct kmemstats {
	long	ks_inuse;	/* # of packets of this type currently in use */
	long	ks_calls;	/* total packets of this type ever allocated */
	long 	ks_memuse;	/* total memory held in bytes */
	u_short	ks_limblocks;	/* number of times blocked for hitting limit */
	u_short	ks_mapblocks;	/* number of times blocked for kernel map */
	long	ks_maxused;	/* maximum number ever used */
	long	ks_limit;	/* most that are allowed to exist */
	long	ks_size;	/* sizes of this thing that are allocated */
	long	ks_spare;
};

/*
 * Array of descriptors that describe the contents of each page
 */
struct kmemusage {
	short ku_indx;		/* bucket index */
	union {
		u_short freecnt;/* for small allocations, free pieces in page */
		u_short pagecnt;/* for large allocations, pages alloced */
	} ku_un;
};
#define	ku_freecnt ku_un.freecnt
#define	ku_pagecnt ku_un.pagecnt

/*
 * Set of buckets for each size of memory block that is retained
 */
struct kmembuckets {
	caddr_t   kb_next;	/* list of free blocks */
	caddr_t   kb_last;	/* last free block */
	u_int64_t kb_calls;	/* total calls to allocate this size */
	u_int64_t kb_total;	/* total number of blocks allocated */
	u_int64_t kb_totalfree;	/* # of free elements in this bucket */
	u_int64_t kb_elmpercl;	/* # of elements in this sized allocation */
	u_int64_t kb_highwat;	/* high water mark */
	u_int64_t kb_couldfree;	/* over high water mark and could free */
};

#ifdef _KERNEL
#define	MINALLOCSIZE	(1 << MINBUCKET)
#define	BUCKETINDX(size) \
	((size) <= (MINALLOCSIZE * 128) \
		? (size) <= (MINALLOCSIZE * 8) \
			? (size) <= (MINALLOCSIZE * 2) \
				? (size) <= (MINALLOCSIZE * 1) \
					? (MINBUCKET + 0) \
					: (MINBUCKET + 1) \
				: (size) <= (MINALLOCSIZE * 4) \
					? (MINBUCKET + 2) \
					: (MINBUCKET + 3) \
			: (size) <= (MINALLOCSIZE* 32) \
				? (size) <= (MINALLOCSIZE * 16) \
					? (MINBUCKET + 4) \
					: (MINBUCKET + 5) \
				: (size) <= (MINALLOCSIZE * 64) \
					? (MINBUCKET + 6) \
					: (MINBUCKET + 7) \
		: (size) <= (MINALLOCSIZE * 2048) \
			? (size) <= (MINALLOCSIZE * 512) \
				? (size) <= (MINALLOCSIZE * 256) \
					? (MINBUCKET + 8) \
					: (MINBUCKET + 9) \
				: (size) <= (MINALLOCSIZE * 1024) \
					? (MINBUCKET + 10) \
					: (MINBUCKET + 11) \
			: (size) <= (MINALLOCSIZE * 8192) \
				? (size) <= (MINALLOCSIZE * 4096) \
					? (MINBUCKET + 12) \
					: (MINBUCKET + 13) \
				: (size) <= (MINALLOCSIZE * 16384) \
					? (MINBUCKET + 14) \
					: (MINBUCKET + 15))

/*
 * Turn virtual addresses into kmem map indicies
 */
#define	kmemxtob(alloc)	(kmembase + (alloc) * NBPG)
#define	btokmemx(addr)	(((caddr_t)(addr) - kmembase) / NBPG)
#define	btokup(addr)	(&kmemusage[((caddr_t)(addr) - kmembase) >> PAGE_SHIFT])

/*
 * Macro versions for the usual cases of malloc/free
 */
#if defined(KMEMSTATS) || defined(DIAGNOSTIC) || defined(_LKM) || defined(SMALL_KERNEL)
#define	MALLOC(space, cast, size, type, flags) \
	(space) = (cast)malloc((u_long)(size), type, flags)
#define	FREE(addr, type) free((caddr_t)(addr), type)

#else /* do not collect statistics */
#define	MALLOC(space, cast, size, type, flags) do { \
	register struct kmembuckets *kbp = &bucket[BUCKETINDX(size)]; \
	long s = splimp(); \
	if (kbp->kb_next == NULL) { \
		(space) = (cast)malloc((u_long)(size), type, flags); \
	} else { \
		(space) = (cast)kbp->kb_next; \
		kbp->kb_next = *(caddr_t *)(space); \
	} \
	splx(s); \
} while (0)

#define	FREE(addr, type) do { \
	register struct kmembuckets *kbp; \
	register struct kmemusage *kup = btokup(addr); \
	long s = splimp(); \
	if (1 << kup->ku_indx > MAXALLOCSAVE) { \
		free((caddr_t)(addr), type); \
	} else { \
		kbp = &bucket[kup->ku_indx]; \
		if (kbp->kb_next == NULL) \
			kbp->kb_next = (caddr_t)(addr); \
		else \
			*(caddr_t *)(kbp->kb_last) = (caddr_t)(addr); \
		*(caddr_t *)(addr) = NULL; \
		kbp->kb_last = (caddr_t)(addr); \
	} \
	splx(s); \
} while(0)
#endif /* do not collect statistics */

extern struct kmemstats kmemstats[];
extern struct kmemusage *kmemusage;
extern char *kmembase;
extern struct kmembuckets bucket[];

extern void *malloc __P((unsigned long size, int type, int flags));
extern void free __P((void *addr, int type));
extern int sysctl_malloc __P((int *, u_int, void *, size_t *, void *, size_t,
			      struct proc *));
#endif /* _KERNEL */
#endif /* !_SYS_MALLOC_H_ */
