/*
 * Copyright (c) 1995 - 2000 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $Id: xfs_fs.h,v 1.7 2003/07/24 22:00:24 mickey Exp $ */

#ifndef _xfs_h
#define _xfs_h

#include <sys/types.h>

#include <xfs/xfs_common.h>
#include <xfs/xfs_node.h>
#include <xfs/xfs_attr.h>

#include <xfs/nxfs.h>

/*
 * Filesystem struct.
 */
struct xfs {
    u_int status;		       /* Inited, opened or mounted */
#define XFS_MOUNTED	0x1
    struct mount *mp;
    struct xfs_node *root;
    u_int nnodes;
    int fd;
};

#ifdef __osf__
#ifdef HAVE_STRUCT_MOUNT_M_INFO
#define VFS_TO_XFS(v)      ((struct xfs *) ((v)->m_info))
#else
#define VFS_TO_XFS(v)      ((struct xfs *) ((v)->m_data))
#endif
#else
#define VFS_TO_XFS(v)      ((struct xfs *) ((v)->mnt_data))
#endif
#define XFS_TO_VFS(x)      ((x)->mp)

#define XFS_FROM_VNODE(vp) VFS_TO_XFS((vp)->v_mount)
#define XFS_FROM_XNODE(xp) XFS_FROM_VNODE(XNODE_TO_VNODE(xp))

extern struct xfs xfs[];

struct xfs_node *xfs_node_find(struct xfs *, struct xfs_handle *);
int new_xfs_node(struct xfs *, struct xfs_msg_node *, struct xfs_node **,
		 struct proc *);
void free_xfs_node(struct xfs_node *);
int free_all_xfs_nodes(struct xfs *, int, int);

int xfs_dnlc_enter(struct vnode *, xfs_componentname *, struct vnode *);
int xfs_dnlc_enter_name(struct vnode *, const char *, struct vnode *);
void xfs_dnlc_purge_mp(struct mount *);
void xfs_dnlc_purge(struct vnode *);
int xfs_dnlc_lookup(struct vnode *, xfs_componentname *, struct vnode **);
int xfs_dnlc_lookup_name(struct vnode *, const char *, struct vnode **);

void vattr2xfs_attr(const struct vattr *, struct xfs_attr *);
void xfs_attr2vattr(const struct xfs_attr *, struct vattr *, int);

int xfs_has_pag(const struct xfs_node *, xfs_pag_t);

#endif				       /* _xfs_h */
