/*
 * Copyright (c) 1998, 1999 Kungliga Tekniska H�gskolan
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

/* $Id: xfs_attr.h,v 1.4 2000/09/11 14:26:51 art Exp $ */

#ifndef _XFS_ATTR_H
#define _XFS_ATTR_H

#define XA_V_NONE       0
#define XA_V_MODE	(1 <<  0)
#define XA_V_NLINK	(1 <<  1)
#define XA_V_SIZE	(1 <<  2)
#define XA_V_UID	(1 <<  3)
#define XA_V_GID	(1 <<  4)
#define XA_V_ATIME	(1 <<  5)
#define XA_V_MTIME	(1 <<  6)
#define XA_V_CTIME	(1 <<  7)
#define XA_V_FILEID	(1 <<  8)
#define XA_V_TYPE       (1 <<  9)

#define XFS_FILE_NON 1
#define XFS_FILE_REG 2
#define XFS_FILE_DIR 3
#define XFS_FILE_BLK 4
#define XFS_FILE_CHR 5
#define XFS_FILE_LNK 6
#define XFS_FILE_SOCK 7
#define XFS_FILE_FIFO 8
#define XFS_FILE_BAD 9

#define XA_CLEAR(xa_p) \
        ((xa_p)->valid = XA_V_NONE)
#define XA_SET_MODE(xa_p, value) \
	(((xa_p)->valid) |= XA_V_MODE, ((xa_p)->xa_mode) = value)
#define XA_SET_NLINK(xa_p, value) \
	(((xa_p)->valid) |= XA_V_NLINK, ((xa_p)->xa_nlink) = value)
#define XA_SET_SIZE(xa_p, value) \
	(((xa_p)->valid) |= XA_V_SIZE, ((xa_p)->xa_size) = value)
#define XA_SET_UID(xa_p, value) \
	(((xa_p)->valid) |= XA_V_UID, ((xa_p)->xa_uid) = value)
#define XA_SET_GID(xa_p, value) \
	(((xa_p)->valid) |= XA_V_GID, ((xa_p)->xa_gid) = value)
#define XA_SET_ATIME(xa_p, value) \
	(((xa_p)->valid) |= XA_V_ATIME, ((xa_p)->xa_atime) = value)
#define XA_SET_MTIME(xa_p, value) \
	(((xa_p)->valid) |= XA_V_MTIME, ((xa_p)->xa_mtime) = value)
#define XA_SET_CTIME(xa_p, value) \
	(((xa_p)->valid) |= XA_V_CTIME, ((xa_p)->xa_ctime) = value)
#define XA_SET_FILEID(xa_p, value) \
	(((xa_p)->valid) |= XA_V_FILEID, ((xa_p)->xa_fileid) = value)
#define XA_SET_TYPE(xa_p, value) \
	(((xa_p)->valid) |= XA_V_TYPE, ((xa_p)->xa_type) = value)


#define XA_VALID_MODE(xa_p) \
	(((xa_p)->valid) & XA_V_MODE)
#define XA_VALID_NLINK(xa_p) \
	(((xa_p)->valid) & XA_V_NLINK)
#define XA_VALID_SIZE(xa_p) \
	(((xa_p)->valid) & XA_V_SIZE)
#define XA_VALID_UID(xa_p) \
	(((xa_p)->valid) & XA_V_UID)
#define XA_VALID_GID(xa_p) \
	(((xa_p)->valid) & XA_V_GID)
#define XA_VALID_ATIME(xa_p) \
	(((xa_p)->valid) & XA_V_ATIME)
#define XA_VALID_MTIME(xa_p) \
	(((xa_p)->valid) & XA_V_MTIME)
#define XA_VALID_CTIME(xa_p) \
	(((xa_p)->valid) & XA_V_CTIME)
#define XA_VALID_FILEID(xa_p) \
	(((xa_p)->valid) & XA_V_FILEID)
#define XA_VALID_TYPE(xa_p) \
	(((xa_p)->valid) & XA_V_TYPE)

struct xfs_attr {
    u_int32_t		valid;
    u_int32_t		xa_mode;

    u_int32_t		xa_nlink;
    u_int32_t		xa_size;

    u_int32_t		xa_uid;
    u_int32_t		xa_gid;

    u_int32_t		xa_atime;
    u_int32_t		xa_mtime;

    u_int32_t		xa_ctime;
    u_int32_t		xa_fileid;

    u_int32_t           xa_type;
    u_int32_t           pad1;
};

#endif /* _XFS_ATTR_H */
