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
 *    Gareth Hughes <gareth@valinux.com>
 *
 */

/** @file drm_ioctl.c
 * Varios minor DRM ioctls not applicable to other files, such as versioning
 * information and reporting DRM information to userland.
 */

#include "drmP.h"

int	drm_set_busid(struct drm_device *);

/*
 * Beginning in revision 1.1 of the DRM interface, getunique will return
 * a unique in the form pci:oooo:bb:dd.f (o=domain, b=bus, d=device, f=function)
 * before setunique has been called.  The format for the bus-specific part of
 * the unique is not defined for any other bus.
 */
int
drm_getunique(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_unique	 *u = data;

	if (u->unique_len >= dev->unique_len) {
		if (DRM_COPY_TO_USER(u->unique, dev->unique, dev->unique_len))
			return EFAULT;
	}
	u->unique_len = dev->unique_len;

	return 0;
}

/* Deprecated in DRM version 1.1, and will return EBUSY when setversion has
 * requested version 1.1 or greater.
 */
int
drm_setunique(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_unique	*u = data;
	char			*busid;
	int			 domain, bus, slot, func, ret;
#if defined (__NetBSD__) 
	return EOPNOTSUPP;
#endif

	/* Check and copy in the submitted Bus ID */
	if (!u->unique_len || u->unique_len > 1024)
		return EINVAL;

	busid = drm_alloc(u->unique_len + 1, DRM_MEM_DRIVER);
	if (busid == NULL)
		return ENOMEM;

	if (DRM_COPY_FROM_USER(busid, u->unique, u->unique_len)) {
		drm_free(busid, u->unique_len + 1, DRM_MEM_DRIVER);
		return EFAULT;
	}
	busid[u->unique_len] = '\0';

	/* Return error if the busid submitted doesn't match the device's actual
	 * busid.
	 */
#ifdef __FreeBSD__
	ret = sscanf(busid, "PCI:%d:%d:%d", &bus, &slot, &func);
#endif /* Net and Openbsd don't have sscanf in the kernel this is deprecated anyway. */

	if (ret != 3) {
		drm_free(busid, u->unique_len + 1, DRM_MEM_DRIVER);
		return EINVAL;
	}
	domain = bus >> 8;
	bus &= 0xff;
	
	if ((domain != dev->pci_domain) || (bus != dev->pci_bus) ||
	    (slot != dev->pci_slot) || (func != dev->pci_func)) {
		drm_free(busid, u->unique_len + 1, DRM_MEM_DRIVER);
		return EINVAL;
	}

	/* Actually set the device's busid now. */
	DRM_LOCK();
	if (dev->unique_len || dev->unique) {
		DRM_UNLOCK();
		return EBUSY;
	}

	dev->unique_len = u->unique_len;
	dev->unique = busid;
	DRM_UNLOCK();

	return 0;
}


int
drm_set_busid(struct drm_device *dev)
{

	DRM_LOCK();

	if (dev->unique != NULL) {
		DRM_UNLOCK();
		return EBUSY;
	}

	dev->unique_len = 20;
	dev->unique = drm_alloc(dev->unique_len + 1, DRM_MEM_DRIVER);
	if (dev->unique == NULL) {
		DRM_UNLOCK();
		return ENOMEM;
	}

	snprintf(dev->unique, dev->unique_len, "pci:%04x:%02x:%02x.%1x",
	    dev->pci_domain, dev->pci_bus, dev->pci_slot, dev->pci_func);

	DRM_UNLOCK();

	return 0;
}

int
drm_getmap(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_map	*map = data;
	drm_local_map_t	*mapinlist;
	int		 idx, i = 0;

	idx = map->offset;

	DRM_LOCK();
	if (idx < 0) {
		DRM_UNLOCK();
		return EINVAL;
	}

	TAILQ_FOREACH(mapinlist, &dev->maplist, link) {
		if (i == idx) {
			map->offset = mapinlist->offset;
			map->size = mapinlist->size;
			map->type = mapinlist->type;
			map->flags = mapinlist->flags;
			map->handle = mapinlist->handle;
			map->mtrr = mapinlist->mtrr;
			break;
		}
		i++;
	}

	DRM_UNLOCK();

 	if (mapinlist == NULL)
		return EINVAL;

	return 0;
}

int
drm_getclient(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return (EINVAL);
}

int
drm_getstats(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return (EINVAL);
}

#define DRM_IF_MAJOR	1
#define DRM_IF_MINOR	2

int
drm_setversion(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_set_version	ver, *sv = data;
	int			if_version;

	/* Save the incoming data, and set the response before continuing
	 * any further.
	 */
	ver = *sv;
	sv->drm_di_major = DRM_IF_MAJOR;
	sv->drm_di_minor = DRM_IF_MINOR;
	sv->drm_dd_major = dev->driver.major;
	sv->drm_dd_minor = dev->driver.minor;

	if (ver.drm_di_major != -1) {
		if (ver.drm_di_major != DRM_IF_MAJOR || ver.drm_di_minor < 0 ||
		    ver.drm_di_minor > DRM_IF_MINOR) {
			return EINVAL;
		}
		if_version = DRM_IF_VERSION(ver.drm_di_major, ver.drm_dd_minor);
		dev->if_version = DRM_MAX(if_version, dev->if_version);
		if (ver.drm_di_minor >= 1) {
			/*
			 * Version 1.1 includes tying of DRM to specific device
			 */
			drm_set_busid(dev);
		}
	}

	if (ver.drm_dd_major != -1) {
		if (ver.drm_dd_major != dev->driver.major ||
		    ver.drm_dd_minor < 0 ||
		    ver.drm_dd_minor > dev->driver.minor)
		{
			return EINVAL;
		}
	}

	return 0;
}


int
drm_noop(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	DRM_DEBUG("\n");
	return 0;
}
