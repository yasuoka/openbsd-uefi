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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the Kungliga Tekniska
 *      H�gskolan and its contributors.
 *
 * 4. Neither the name of the Institute nor the names of its contributors
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

#include <xfs/xfs_locl.h>
#include <xfs/xfs_common.h>
#include <xfs/xfs_fs.h>
#include <xfs/xfs_deb.h>
#include <xfs/xfs_vnodeops.h>

RCSID("$Id: xfs_node-bsd.c,v 1.3 2000/09/11 14:26:53 art Exp $");

extern vop_t **xfs_vnodeop_p;

#ifndef LK_NOPAUSE
#define LK_NOPAUSE 0
#endif

/*
 * Allocate a new vnode with handle `handle' in `mp' and return it in
 * `vpp'.  Return 0 or error.
 */

int
xfs_getnewvnode(struct mount *mp, struct vnode **vpp, 
		struct xfs_handle *handle)
{
    struct xfs_node *result;
    int error;

    error = getnewvnode(VT_AFS, mp, xfs_vnodeop_p, vpp);
    if (error)
	return error;
    
    result = xfs_alloc(sizeof(*result));
    bzero(result, sizeof(*result));
    
    (*vpp)->v_data = result;
    result->vn = *vpp;
    
    result->handle = *handle;
    result->flags = 0;
    result->tokens = 0;
#if defined(HAVE_KERNEL_LOCKMGR) || defined(HAVE_KERNEL_DEBUGLOCKMGR)
    lockinit (&result->lock, PVFS, "xfs_lock", 0, LK_NOPAUSE);
#else
    result->vnlocks = 0;
#endif
    result->anonrights = 0;
    result->cred = NULL;

    return 0;
}

/*
 * Create a new xfs_node and make a vget
 *
 * Also prevents creation of duplicates. This happens
 * whenever there are more than one name to a file,
 * "." and ".." are common cases.  */

int
new_xfs_node(struct xfs *xfsp,
	     struct xfs_msg_node *node,
	     struct xfs_node **xpp,
	     struct proc *p)
{
    struct xfs_node *result;

    XFSDEB(XDEBNODE, ("new_xfs_node (%d,%d,%d,%d)\n",
		      node->handle.a,
		      node->handle.b,
		      node->handle.c,
		      node->handle.d));

retry:
    /* Does not allow duplicates */
    result = xfs_node_find(xfsp, &node->handle);
    if (result == 0) {
	int error;
	struct vnode *v;

	error = xfs_getnewvnode(XFS_TO_VFS(xfsp), &v, &node->handle);
	if (error)
	    return error;

	result = VNODE_TO_XNODE(v);
	result->anonrights = node->anonrights;

	xfsp->nnodes++;
    } else {
	/* Node is already cached */
	if(xfs_do_vget(XNODE_TO_VNODE(result), 0, p))
	    goto retry;
    }

    /* Init other fields */
    xfs_attr2vattr(&node->attr, &result->attr);
    result->vn->v_type = result->attr.va_type;
    XFS_TOKEN_SET(result, XFS_ATTR_R, XFS_ATTR_MASK);
    bcopy(node->id, result->id, sizeof(result->id));
    bcopy(node->rights, result->rights, sizeof(result->rights));

    *xpp = result;
    XFSDEB(XDEBNODE, ("return: new_xfs_node\n"));
    return 0;
}

void
free_xfs_node(struct xfs_node *node)
{
    struct xfs *xfsp = XFS_FROM_XNODE(node);

    XFSDEB(XDEBNODE, ("free_xfs_node(%lx) (%d,%d,%d,%d)\n",
		      (unsigned long)node,
		      node->handle.a,
		      node->handle.b,
		      node->handle.c,
		      node->handle.d));

    /* XXX Really need to put back dirty data first. */

    if (DATA_FROM_XNODE(node)) {
	vrele(DATA_FROM_XNODE(node));
	DATA_FROM_XNODE(node) = NULL;
    }
    xfsp->nnodes--;
    XNODE_TO_VNODE(node)->v_data = NULL;
    if (node->cred) {
	crfree (node->cred);
	node->cred = NULL;
    }

    xfs_free(node, sizeof(*node));

    XFSDEB(XDEBNODE, ("free_xfs_node done\n"));
}

int
free_all_xfs_nodes(struct xfs *xfsp, int flags, int unmountp)
{
    int error = 0;
    struct mount *mp = XFS_TO_VFS(xfsp);

    if (mp == NULL) {
	XFSDEB(XDEBNODE, ("free_all_xfs_nodes already freed\n"));
	return 0;
    }

    XFSDEB(XDEBNODE, ("free_all_xfs_nodes starting\n"));

    xfs_dnlc_purge_mp(mp);

    if (xfsp->root) {
	XFSDEB(XDEBNODE, ("free_all_xfs_nodes now removing root\n"));

	vgone(XNODE_TO_VNODE(xfsp->root));
	xfsp->root = 0;
    }

    XFSDEB(XDEBNODE, ("free_all_xfs_nodes root removed\n"));
    XFSDEB(XDEBNODE, ("free_all_xfs_nodes now killing all remaining nodes\n"));

#ifdef HAVE_STRUCT_MOUNT_MNT_SYNCER
    if (!unmountp) {
	XFSDEB(XDEBNODE, ("free_all_xfs_nodes not flushing syncer vnode\n"));
	error = vflush(mp, mp->mnt_syncer, flags);
    } else
#endif
    {
	error = vflush(mp, NULL, flags);
    }

    if (error) {
	XFSDEB(XDEBNODE, ("xfree_all_xfs_nodes: vflush() error == %d\n",
			  error));
	return error;
    }

    XFSDEB(XDEBNODE, ("free_all_xfs_nodes done\n"));
    return error;
}

struct xfs_node *
xfs_node_find(struct xfs *xfsp, xfs_handle *handlep)
{
    struct vnode *t;
    struct xfs_node *xn = NULL;

    XFSDEB(XDEBNODE, ("xfs_node_find: xfsp = %lx "
		      " handlep = (%d,%d,%d,%d)\n", 
		      (unsigned long)xfsp,
		      handlep->a,
		      handlep->b,
		      handlep->c,
		      handlep->d));

    /*
     * XXXSMP - the vnodes on mnt_vnodelist are invalid unless we hold
     *          mntvnode_slock (same on Open,Free and Net - current).
     * XXX - Another problem here is that the data in the vnode doesn't
     *       have to be correct unless we do a vget first (if usecount on
     *       vnode == 0). This should only be a problem when someone uses
     *       revoke, when unmounting or when arlad dies or on systems
     *       with a shortage of vnodes.
     *       We might want to vget here, but that was a problem
     *       on FreeBSD once.
     */

    for(t = XFS_TO_VFS(xfsp)->mnt_vnodelist.lh_first;
	t != NULL; 
	t = t->v_mntvnodes.le_next) {
	xn = VNODE_TO_XNODE(t);
	if (xn && xfs_handle_eq(&xn->handle, handlep))
	    break;
    }

    if (t != NULL)
	return xn;
    else
	return NULL;
}

void
vattr2xfs_attr(const struct vattr *va, struct xfs_attr *xa)
{
    bzero(xa, sizeof(*xa));
    if (va->va_mode != (mode_t)VNOVAL)
	XA_SET_MODE(xa, va->va_mode);
    if (va->va_nlink != VNOVAL)
	XA_SET_NLINK(xa, va->va_nlink);
    if (va->va_size != VNOVAL)
	XA_SET_SIZE(xa, va->va_size);
    if (va->va_uid != VNOVAL)
	XA_SET_UID(xa, va->va_uid);
    if (va->va_gid != VNOVAL)
	XA_SET_GID(xa, va->va_gid);
    if (va->va_atime.tv_sec != VNOVAL)
	XA_SET_ATIME(xa, va->va_atime.tv_sec);
    if (va->va_mtime.tv_sec != VNOVAL)
	XA_SET_MTIME(xa, va->va_mtime.tv_sec);
    if (va->va_ctime.tv_sec != VNOVAL)
	XA_SET_CTIME(xa, va->va_ctime.tv_sec);
    if (va->va_fileid != VNOVAL)
	XA_SET_FILEID(xa, va->va_fileid);
    switch (va->va_type) {
    case VNON:
	xa->xa_type = XFS_FILE_NON;
	break;
    case VREG:
	xa->xa_type = XFS_FILE_REG;
	break;
    case VDIR:
	xa->xa_type = XFS_FILE_DIR;
	break;
    case VBLK:
	xa->xa_type = XFS_FILE_BLK;
	break;
    case VCHR:
	xa->xa_type = XFS_FILE_CHR;
	break;
    case VLNK:
	xa->xa_type = XFS_FILE_LNK;
	break;
    case VSOCK:
	xa->xa_type = XFS_FILE_SOCK;
	break;
    case VFIFO:
	xa->xa_type = XFS_FILE_FIFO;
	break;
    case VBAD:
	xa->xa_type = XFS_FILE_BAD;
	break;
    default:
	panic("xfs_attr2attr: bad value");
    }
}

#define SET_TIMEVAL(X, S, N) do { (X)->tv_sec = (S); (X)->tv_nsec = (N); } while(0)

void
xfs_attr2vattr(const struct xfs_attr *xa, struct vattr *va)
{
    VATTR_NULL(va);
    if (XA_VALID_MODE(xa))
	va->va_mode = xa->xa_mode;
    if (XA_VALID_NLINK(xa))
	va->va_nlink = xa->xa_nlink;
    if (XA_VALID_SIZE(xa))
	va->va_size = xa->xa_size;
    if (XA_VALID_UID(xa))
	va->va_uid = xa->xa_uid;
    if (XA_VALID_GID(xa))
	va->va_gid = xa->xa_gid;
    if (XA_VALID_ATIME(xa)) {
	SET_TIMEVAL(&va->va_atime, xa->xa_atime, 0);
    }
    if (XA_VALID_MTIME(xa)) {
	SET_TIMEVAL(&va->va_mtime, xa->xa_mtime, 0);
    }
    if (XA_VALID_CTIME(xa)) {
	SET_TIMEVAL(&va->va_ctime, xa->xa_ctime, 0);
    }
    if (XA_VALID_FILEID(xa)) {
	va->va_fileid = xa->xa_fileid;
    }
    if (XA_VALID_TYPE(xa)) {
	switch (xa->xa_type) {
	case XFS_FILE_NON:
	    va->va_type = VNON;
	    break;
	case XFS_FILE_REG:
	    va->va_type = VREG;
	    break;
	case XFS_FILE_DIR:
	    va->va_type = VDIR;
	    break;
	case XFS_FILE_BLK:
	    va->va_type = VBLK;
	    break;
	case XFS_FILE_CHR:
	    va->va_type = VCHR;
	    break;
	case XFS_FILE_LNK:
	    va->va_type = VLNK;
	    break;
	case XFS_FILE_SOCK:
	    va->va_type = VSOCK;
	    break;
	case XFS_FILE_FIFO:
	    va->va_type = VFIFO;
	    break;
	case XFS_FILE_BAD:
	    va->va_type = VBAD;
	    break;
	default:
	    panic("xfs_attr2vattr: bad value");
	}
    }
    va->va_flags = 0;
    va->va_blocksize = 8192;
    va->va_bytes = va->va_size;
}

/*
 * A single entry DNLC for systems for handling long names that don't
 * get put into the system DNLC.
 */

struct long_entry {
    struct vnode *dvp, *vp;
    char name[MAXNAMLEN + 1];
    size_t len;
    u_long dvpid, vpid;
};

static struct long_entry tbl;

/*
 * Nuke the `tbl'
 */

static void
tbl_clear (void)
{
    tbl.dvp = tbl.vp = NULL;
    tbl.name[0] = '\0';
    tbl.len = 0;
    tbl.dvpid = tbl.vpid = 0;
}

/*
 * Set the entry in the `tbl'
 */

static void
tbl_enter (size_t len, const char *name, struct vnode *dvp, struct vnode *vp)
{
    tbl.len = len;
    bcopy(name, tbl.name, len);
    tbl.dvp = dvp;
    tbl.vp = vp;
    tbl.dvpid = dvp->v_id;
    tbl.vpid = vp->v_id;
}

/*
 * Lookup in tbl (`dvp', `name', `len') and return result in `res'.
 * Return -1 if succesful, otherwise 0.
 */

static int
tbl_lookup (struct componentname *cnp,
	    struct vnode *dvp,
	    struct vnode **res)
{
    if (tbl.dvp == dvp
	&& tbl.len == cnp->cn_namelen
	&& strncmp(tbl.name, cnp->cn_nameptr, tbl.len) == 0
	&& tbl.dvpid == tbl.dvp->v_id
	&& tbl.vpid == tbl.vp->v_id) {

	*res = tbl.vp;
	return -1;
    } else
	return 0;
}

/*
 * Store a componentname in the DNLC
 */

int
xfs_dnlc_enter(struct vnode *dvp,
	       xfs_componentname *cnp,
	       struct vnode *vp)
{
    XFSDEB(XDEBDNLC, ("xfs_dnlc_enter_cnp(%lx, %lx, %lx)\n",
		      (unsigned long)dvp,
		      (unsigned long)cnp,
		      (unsigned long)vp));
    XFSDEB(XDEBDNLC, ("xfs_dnlc_enter: v_id = %ld\n", dvp->v_id));

    XFSDEB(XDEBDNLC, ("xfs_dnlc_enter: calling cache_enter:"
		      "dvp = %lx, vp = %lx, cnp = (%s, %ld), "
		      "nameiop = %lu, flags = %lx\n",
		      (unsigned long)dvp,
		      (unsigned long)vp,
		      cnp->cn_nameptr, cnp->cn_namelen,
		      cnp->cn_nameiop, cnp->cn_flags));

#ifdef NCHNAMLEN
    if (cnp->cn_namelen <= NCHNAMLEN)
#endif
    {
	/*
	 * This is to make sure there's no negative entry already in the dnlc
	 */
	u_long save_nameiop;
	u_long save_flags;
	struct vnode *dummy;

	save_nameiop    = cnp->cn_nameiop;
	save_flags      = cnp->cn_flags;
	cnp->cn_nameiop = CREATE;
	cnp->cn_flags  &= ~MAKEENTRY;

/*
 * The version number here is not entirely correct, but it's conservative.
 * The real change is sys/kern/vfs_cache:1.20
 */

#if __NetBSD_Version__ >= 104120000
	if (cache_lookup(dvp, &dummy, cnp) != -1) {
	    VOP_UNLOCK(dummy, 0);
	    printf ("XFS PANIC WARNING! xfs_dnlc_enter: %s already in cache\n",
		    cnp->cn_nameptr);
	}
#else
	if (cache_lookup(dvp, &dummy, cnp) != 0) {
	    printf ("XFS PANIC WARNING! xfs_dnlc_enter: %s already in cache\n",
		    cnp->cn_nameptr);
	}
#endif


	cnp->cn_nameiop = save_nameiop;
	cnp->cn_flags   = save_flags;
	cache_enter(dvp, vp, cnp);
    }

    if (vp != NULL)
	tbl_enter (cnp->cn_namelen, cnp->cn_nameptr, dvp, vp);

    return 0;
}
		   

static void
xfs_cnp_init (struct componentname *cn,
	      const char *name,
	      struct proc *proc, struct ucred *cred,
	      int nameiop)
{
    const unsigned char *p;

    bzero(cn, sizeof(*cn));
    cn->cn_nameptr = (char *)name;
    cn->cn_namelen = strlen(name);
    cn->cn_flags   = 0;
#if __APPLE__
    {
	int i;

	cn->cn_hash = 0;
	for (p = cn->cn_nameptr, i = 1; *p; ++p, ++i)
	    cn->cn_hash += *p * i;
    }
#elif defined(HAVE_STRUCT_COMPONENTNAME_CN_HASH)
    cn->cn_hash = 0;
    for (p = cn->cn_nameptr; *p; ++p)
	cn->cn_hash += *p;
#endif
    cn->cn_nameiop = nameiop;
    cn->cn_proc = proc;
    cn->cn_cred = cred;
}


/*
 * Store (dvp, name, vp) in the DNLC
 */

int
xfs_dnlc_enter_name(struct vnode *dvp,
		    const char *name,
		    struct vnode *vp)
{
    struct componentname cn;

    XFSDEB(XDEBDNLC, ("xfs_dnlc_enter_name(%lx, \"%s\", %lx)\n",
		      (unsigned long)dvp,
		      name,
		      (unsigned long)vp));

    xfs_cnp_init (&cn, name, NULL, NULL, LOOKUP);
    return xfs_dnlc_enter (dvp, &cn, vp);
}

/*
 * Lookup (dvp, cnp) in the DNLC and return the result in `res'.
 * Return the result from cache_lookup.
 */

static int
xfs_dnlc_lookup_int(struct vnode *dvp,
		    xfs_componentname *cnp,
		    struct vnode **res)
{
    int error;
    u_long saved_flags;

    XFSDEB(XDEBDNLC, ("xfs_dnlc_lookup(%lx, \"%s\")\n",
		      (unsigned long)dvp, cnp->cn_nameptr));
    
    XFSDEB(XDEBDNLC, ("xfs_dnlc_lookup: v_id = %ld\n", dvp->v_id));
    
    XFSDEB(XDEBDNLC, ("xfs_dnlc_lookup: calling cache_lookup:"
		      "dvp = %lx, cnp = (%s, %ld), flags = %lx\n",
		      (unsigned long)dvp,
		      cnp->cn_nameptr, cnp->cn_namelen,
		      cnp->cn_flags));

    saved_flags = cnp->cn_flags;
    cnp->cn_flags |= MAKEENTRY | LOCKPARENT | ISLASTCN;

    error = cache_lookup(dvp, res, cnp);

    cnp->cn_flags = saved_flags;

    XFSDEB(XDEBDNLC, ("xfs_dnlc_lookup: cache_lookup returned. "
		      "error = %d, *res = %lx\n", error,
		      (unsigned long)*res));
    return error;
}

/*
 * do the last (and locking protocol) portion of xnlc_lookup
 *
 * return:
 * -1 for succesful
 * 0  for failed
 */

static int
xfs_dnlc_lock(struct vnode *dvp,
	      xfs_componentname *cnp,
	      struct vnode **res)
{
    int error = 0;

    /*
     * Try to handle the (complex) BSD locking protocol.
     */

    if (*res == dvp) {		/* "." */
	VREF(dvp);
    } else if (cnp->cn_flags & ISDOTDOT) { /* ".." */
	u_long vpid = dvp->v_id;

	xfs_vfs_unlock(dvp, xfs_cnp_to_proc(cnp));
	error = xfs_do_vget(*res, LK_EXCLUSIVE, xfs_cnp_to_proc(cnp));
	xfs_vfs_writelock(dvp, xfs_cnp_to_proc(cnp));

	if (error == 0 && dvp->v_id != vpid) {
	    vput(*res);
	    return 0;
	}
    } else {
	error = xfs_do_vget(*res, LK_EXCLUSIVE, xfs_cnp_to_proc(cnp));
    }

    if (error == 0)
	return -1;
    else
	return 0;
}

/*
 * Lookup (`dvp', `cnp') in the DNLC (and the local cache).
 *
 * Return -1 if succesful, 0 if not and ENOENT if the entry is known
 * not to exist.
 *
 * On modern NetBSD, cache_lookup has been changed to return 0 for
 * succesful and -1 for not.
 * (see the comment above for version information).
 */

#if __NetBSD_Version__ >= 104120000

int
xfs_dnlc_lookup(struct vnode *dvp,
		xfs_componentname *cnp,
		struct vnode **res)
{
    int error = xfs_dnlc_lookup_int (dvp, cnp, res);

    if (error == 0)
	return -1;
    else if (error == ENOENT)
	return error;

    error = tbl_lookup (cnp, dvp, res);

    if (error != -1)
	return error;

    return xfs_dnlc_lock (dvp, cnp, res);
}

#else /* !  __NetBSD_Version__ >= 104120000 */

int
xfs_dnlc_lookup(struct vnode *dvp,
		xfs_componentname *cnp,
		struct vnode **res)
{
    int error = xfs_dnlc_lookup_int (dvp, cnp, res);

    if (error == 0)
	error = tbl_lookup (cnp, dvp, res);

    if (error != -1)
	return error;

    return xfs_dnlc_lock (dvp, cnp, res);
}

#endif /*  __NetBSD_Version__ >= 104120000 */

/*
 * Remove one entry from the DNLC
 */

void
xfs_dnlc_purge (struct vnode *vp)
{
    XFSDEB(XDEBDNLC, ("xfs_dnlc_purge\n"));

    if (tbl.dvp == vp || tbl.vp == vp)
	tbl_clear ();

    cache_purge(vp);
}

/*
 * Remove all entries belong to `mp' from the DNLC
 */

void
xfs_dnlc_purge_mp(struct mount *mp)
{
    XFSDEB(XDEBDNLC, ("xfs_dnlc_purge_mp()\n"));

    tbl_clear ();
    cache_purgevfs(mp);
}

/*
 * Returns 1 if pag has any rights set in the node
 */

int
xfs_has_pag(const struct xfs_node *xn, xfs_pag_t pag)
{
    int i;

    for (i = 0; i < MAXRIGHTS; i++)
	if (xn->id[i] == pag)
	    return 1;

    return 0;
}
