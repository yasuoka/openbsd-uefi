/*-
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Daryll Strauss <daryll@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 */

/** @file drm_fops.c
 * Support code for dealing with the file privates associated with each
 * open of the DRM device.
 */

#include "drmP.h"

struct drm_file *
drm_find_file_by_minor(struct drm_device *dev, int minor)
{
	struct drm_file *priv;

	DRM_SPINLOCK_ASSERT(&dev->dev_lock);

	TAILQ_FOREACH(priv, &dev->files, link)
		if (priv->minor == minor)
			return (priv);
	return (NULL);
}

/* drm_open_helper is called whenever a process opens /dev/drm. */
int
drm_open_helper(DRM_CDEV kdev, int flags, int fmt, DRM_STRUCTPROC *p,
    struct drm_device *dev)
{
	struct drm_file   *priv;
	int m, retcode;

	m = minor(kdev);
	if (flags & O_EXCL)
		return (EBUSY); /* No exclusive opens */

	DRM_DEBUG("pid = %d, minor = %d\n", DRM_CURRENTPID, m);

	priv = drm_calloc(1, sizeof(*priv), DRM_MEM_FILES);
	if (priv == NULL) {
		return (ENOMEM);
	}
	priv->uid = DRM_UID(p);
	priv->pid = DRM_PID(p);
	priv->kdev = kdev;
	priv->flags = flags;
	priv->minor = m;

	/* for compatibility root is always authenticated */
	priv->authenticated = DRM_SUSER(p);

	DRM_LOCK();
	if (dev->driver.open) {
		/* shared code returns -errno */
		retcode = -dev->driver.open(dev, priv);
		if (retcode != 0) {
			DRM_UNLOCK();
			drm_free(priv, sizeof(*priv), DRM_MEM_FILES);
			return (retcode);
		}
	}

	/* first opener automatically becomes master if root */
	if (TAILQ_EMPTY(&dev->files) && !DRM_SUSER(p)) {
		DRM_UNLOCK();
		drm_free(priv, sizeof(*priv), DRM_MEM_FILES);
		return (EPERM);
	}

	priv->master = TAILQ_EMPTY(&dev->files);

	TAILQ_INSERT_TAIL(&dev->files, priv, link);
	DRM_UNLOCK();
	return (0);
}


/* The drm_read and drm_poll are stubs to prevent spurious errors
 * on older X Servers (4.3.0 and earlier) */

int
drmread(DRM_CDEV kdev, struct uio *uio, int ioflag)
{
	return 0;
}

int
drmpoll(DRM_CDEV kdev, int events, DRM_STRUCTPROC *p)
{
	return 0;
}
