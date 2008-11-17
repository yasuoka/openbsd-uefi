/*-
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
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

/** @file drm_drv.c
 * The catch-all file for DRM device support, including module setup/teardown,
 * open/close, and ioctl dispatch.
 */

#include <sys/limits.h>
#include <sys/ttycom.h> /* for TIOCSGRP */

#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"

#ifdef DRM_DEBUG_DEFAULT_ON
int drm_debug_flag = 1;
#else
int drm_debug_flag = 0;
#endif

drm_pci_id_list_t *drm_find_description(int , int ,
	    drm_pci_id_list_t *);
int	 drm_firstopen(struct drm_device *);
int	 drm_lastclose(struct drm_device *);


struct drm_device *drm_units[DRM_MAXUNITS];

static int init_units = 1;

int
drm_probe(struct pci_attach_args *pa, drm_pci_id_list_t *idlist)
{
	int unit;
	drm_pci_id_list_t *id_entry;

	/* first make sure there is place for the device */
	for (unit=0; unit<DRM_MAXUNITS; unit++)
		if (drm_units[unit] == NULL)
			break;
	if (unit == DRM_MAXUNITS)
		return 0;

	id_entry = drm_find_description(PCI_VENDOR(pa->pa_id),
	    PCI_PRODUCT(pa->pa_id), idlist);
	if (id_entry != NULL)
		return 1;

	return 0;
}

void
drm_attach(struct device *parent, struct device *kdev,
    struct pci_attach_args *pa, drm_pci_id_list_t *idlist)
{
	int unit;
	struct drm_device *dev;
	drm_pci_id_list_t *id_entry;

	if (init_units) {
		for (unit=0; unit<DRM_MAXUNITS; unit++)
			drm_units[unit] = NULL;
		init_units = 0;
	}


        for (unit=0; unit<DRM_MAXUNITS; unit++)
		if (drm_units[unit] == NULL)
			break;
	if (unit == DRM_MAXUNITS)
		return;

	dev = drm_units[unit] = (struct drm_device*)kdev;
	dev->unit = unit;

	/* needed for pci_mapreg_* */
	memcpy(&dev->pa, pa, sizeof(dev->pa));
	dev->vga_softc = (struct vga_pci_softc *)parent;

	dev->irq = pa->pa_intrline;
	dev->pci_domain = 0;
	dev->pci_bus = pa->pa_bus;
	dev->pci_slot = pa->pa_device;
	dev->pci_func = pa->pa_function;
	dev->pci_vendor = PCI_VENDOR(dev->pa.pa_id);
	dev->pci_device = PCI_PRODUCT(dev->pa.pa_id);

	rw_init(&dev->dev_lock, "drmdevlk");
	mtx_init(&dev->drw_lock, IPL_NONE);
	mtx_init(&dev->lock.spinlock, IPL_NONE);

	id_entry = drm_find_description(PCI_VENDOR(pa->pa_id),
	    PCI_PRODUCT(pa->pa_id), idlist);
	dev->id_entry = id_entry;

	TAILQ_INIT(&dev->maplist);

	drm_mem_init();
	TAILQ_INIT(&dev->files);

	/*
	 * the dma buffers api is just weird. offset 1Gb to ensure we don't
	 * conflict with it.
	 */
	dev->handle_ext = extent_create("drmext", 1024*1024*1024, LONG_MAX,
	    M_DRM, NULL, NULL, EX_NOWAIT | EX_NOCOALESCE);
	if (dev->handle_ext == NULL) {
		DRM_ERROR("Failed to initialise handle extent\n");
		goto error;
	}

	if (dev->driver->load != NULL) {
		int retcode;

		retcode = dev->driver->load(dev, dev->id_entry->driver_private);
		if (retcode != 0)
			goto error;
	}

	if (dev->driver->flags & DRIVER_AGP) {
		if (drm_device_is_agp(dev))
			dev->agp = drm_agp_init();
		if (dev->driver->flags & DRIVER_AGP_REQUIRE &&
		    dev->agp == NULL) {
			printf(": couldn't find agp\n");
			goto error;
		}
		if (dev->agp != NULL) {
			if (drm_mtrr_add(dev->agp->info.ai_aperture_base,
			    dev->agp->info.ai_aperture_size, DRM_MTRR_WC) == 0)
				dev->agp->mtrr = 1;
		}
	}

	if (drm_ctxbitmap_init(dev) != 0) {
		printf(": couldn't allocate memory for context bitmap.\n");
		goto error;
	}
	printf("\n");
	return;

error:
	drm_lastclose(dev);
}

int
drm_detach(struct device *self, int flags)
{
	struct drm_device *dev = (struct drm_device *)self;

	drm_ctxbitmap_cleanup(dev);

	extent_destroy(dev->handle_ext);

	if (dev->agp && dev->agp->mtrr) {
		int retcode;

		retcode = drm_mtrr_del(0, dev->agp->info.ai_aperture_base,
		    dev->agp->info.ai_aperture_size, DRM_MTRR_WC);
		DRM_DEBUG("mtrr_del = %d", retcode);
	}

	drm_lastclose(dev);

	if (dev->agp != NULL) {
		drm_free(dev->agp, sizeof(*dev->agp), DRM_MEM_AGPLISTS);
		dev->agp = NULL;
	}

	if (dev->driver->unload != NULL)
		dev->driver->unload(dev);

	drm_mem_uninit();
	return 0;
}

int
drm_activate(struct device *self, enum devact act)
{
	switch (act) {
	case DVACT_ACTIVATE:
		return (EOPNOTSUPP);
		break;

	case DVACT_DEACTIVATE:
		/* FIXME */
		break;
	}
	return (0);
}

drm_pci_id_list_t *
drm_find_description(int vendor, int device, drm_pci_id_list_t *idlist)
{
	int i = 0;
	
	for (i = 0; idlist[i].vendor != 0; i++) {
		if ((idlist[i].vendor == vendor) &&
		    (idlist[i].device == device))
			return &idlist[i];
	}
	return NULL;
}

int
drm_firstopen(struct drm_device *dev)
{
	drm_local_map_t *map;
	int i;

	/* prebuild the SAREA */
	i = drm_addmap(dev, 0, SAREA_MAX, _DRM_SHM,
	    _DRM_CONTAINS_LOCK, &map);
	if (i != 0)
		return i;

	if (dev->driver->firstopen)
		dev->driver->firstopen(dev);

	if (dev->driver->flags & DRIVER_DMA) {
		i = drm_dma_setup(dev);
		if (i != 0)
			return i;
	}

	dev->magicid = 1;
	SPLAY_INIT(&dev->magiclist);

	dev->irq_enabled = 0;
	dev->if_version = 0;

	dev->buf_pgid = 0;

	DRM_DEBUG("\n");

	return 0;
}

int
drm_lastclose(struct drm_device *dev)
{
	struct drm_magic_entry *pt;
	drm_local_map_t *map, *mapsave;

	DRM_DEBUG("\n");

	if (dev->driver->lastclose != NULL)
		dev->driver->lastclose(dev);

	if (dev->irq_enabled)
		drm_irq_uninstall(dev);

	drm_agp_takedown(dev);
	drm_drawable_free_all(dev);
	drm_dma_takedown(dev);

	DRM_LOCK();
	if (dev->unique != NULL) {
		drm_free(dev->unique, dev->unique_len + 1,  DRM_MEM_DRIVER);
		dev->unique = NULL;
		dev->unique_len = 0;
	}

	/* Clear pid list */
	while ((pt = SPLAY_ROOT(&dev->magiclist)) != NULL) {
		SPLAY_REMOVE(drm_magic_tree, &dev->magiclist, pt);
		drm_free(pt, sizeof(*pt), DRM_MEM_MAGIC);
	}

	if (dev->sg != NULL) {
		drm_sg_mem_t *sg = dev->sg; 
		dev->sg = NULL;

		DRM_UNLOCK();
		drm_sg_cleanup(sg);
		DRM_LOCK();
	}

	for (map = TAILQ_FIRST(&dev->maplist); map != TAILQ_END(&dev->maplist);
	    map = mapsave) {
		mapsave = TAILQ_NEXT(map, link);
		if (!(map->flags & _DRM_DRIVER))
			drm_rmmap_locked(dev, map);
	}

	if (dev->lock.hw_lock != NULL) {
		dev->lock.hw_lock = NULL; /* SHM removed */
		dev->lock.file_priv = NULL;
		wakeup(&dev->lock); /* there should be nothing sleeping on it */
	}
	DRM_UNLOCK();

	return 0;
}

int
drm_version(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_version	*version = data;
	int			 len;

#define DRM_COPY(name, value)						\
	len = strlen( value );						\
	if ( len > name##_len ) len = name##_len;			\
	name##_len = strlen( value );					\
	if ( len && name ) {						\
		if ( DRM_COPY_TO_USER( name, value, len ) )		\
			return EFAULT;				\
	}

	version->version_major = dev->driver->major;
	version->version_minor = dev->driver->minor;
	version->version_patchlevel = dev->driver->patchlevel;

	DRM_COPY(version->name, dev->driver->name);
	DRM_COPY(version->date, dev->driver->date);
	DRM_COPY(version->desc, dev->driver->desc);

	return 0;
}

int
drmopen(dev_t kdev, int flags, int fmt, struct proc *p)
{
	struct drm_device	*dev = NULL;
	struct drm_file		*priv;
	int			 ret = 0;

	dev = drm_get_device_from_kdev(kdev);
	if (dev == NULL)
		return (ENXIO);

	DRM_DEBUG("open_count = %d\n", dev->open_count);

	if (flags & O_EXCL)
		return (EBUSY); /* No exclusive opens */

	DRM_LOCK();
	if (dev->open_count++ == 0) {
		DRM_UNLOCK();
		if ((ret = drm_firstopen(dev)) != 0)
			goto err;
	} else {
		DRM_UNLOCK();
	}


	priv = drm_calloc(1, sizeof(*priv), DRM_MEM_FILES);
	if (priv == NULL) {
		ret = ENOMEM;
		goto err;
	}

	priv->kdev = kdev;
	priv->flags = flags;
	priv->minor = minor(kdev);
	DRM_DEBUG("minor = %d\n", priv->minor);

	/* for compatibility root is always authenticated */
	priv->authenticated = DRM_SUSER(p);

	if (dev->driver->open) {
		ret = dev->driver->open(dev, priv);
		if (ret != 0) {
			goto free_priv;
		}
	}

	DRM_LOCK();
	/* first opener automatically becomes master if root */
	if (TAILQ_EMPTY(&dev->files) && !DRM_SUSER(p)) {
		DRM_UNLOCK();
		ret = EPERM;
		goto free_priv;
	}

	priv->master = TAILQ_EMPTY(&dev->files);

	TAILQ_INSERT_TAIL(&dev->files, priv, link);
	DRM_UNLOCK();

	return (0);

free_priv:
	drm_free(priv, sizeof(*priv), DRM_MEM_FILES);
err:
	DRM_LOCK();
	--dev->open_count;
	DRM_UNLOCK();
	return (ret);
}

int
drmclose(dev_t kdev, int flags, int fmt, struct proc *p)
{
	struct drm_device *dev = drm_get_device_from_kdev(kdev);
	struct drm_file *file_priv;
	int retcode = 0;

	DRM_DEBUG("open_count = %d\n", dev->open_count);

	DRM_LOCK();
	file_priv = drm_find_file_by_minor(dev, minor(kdev));
	if (!file_priv) {
		DRM_UNLOCK();
		DRM_ERROR("can't find authenticator\n");
		retcode = EINVAL;
		goto done;
	}
	DRM_UNLOCK();

	if (dev->driver->preclose != NULL)
		dev->driver->preclose(dev, file_priv);

	DRM_DEBUG("pid = %d, device = 0x%lx, open_count = %d\n",
	    DRM_CURRENTPID, (long)&dev->device, dev->open_count);

	if (dev->lock.hw_lock && _DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)
	    && dev->lock.file_priv == file_priv) {
		DRM_DEBUG("Process %d dead, freeing lock for context %d\n",
		    DRM_CURRENTPID,
		    _DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock));
		if (dev->driver->reclaim_buffers_locked != NULL)
			dev->driver->reclaim_buffers_locked(dev, file_priv);

		drm_lock_free(&dev->lock,
		    _DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock));
	} else if (dev->driver->reclaim_buffers_locked != NULL &&
	    dev->lock.hw_lock != NULL) {
		mtx_enter(&dev->lock.spinlock);
		/* The lock is required to reclaim buffers */
		for (;;) {
			if (dev->lock.hw_lock == NULL) {
				/* Device has been unregistered */
				retcode = EINTR;
				break;
			}
			if (drm_lock_take(&dev->lock, DRM_KERNEL_CONTEXT)) {
				dev->lock.file_priv = file_priv;
				dev->lock.lock_time = jiffies;
				break;	/* Got lock */
			}
				/* Contention */
			retcode = msleep(&dev->lock,
			    &dev->lock.spinlock, PZERO | PCATCH, "drmlk2", 0);
			if (retcode)
				break;
		}
		mtx_leave(&dev->lock.spinlock);
		if (retcode == 0) {
			dev->driver->reclaim_buffers_locked(dev, file_priv);
			drm_lock_free(&dev->lock, DRM_KERNEL_CONTEXT);
		}
	}

	if (dev->driver->flags & DRIVER_DMA &&
	    !dev->driver->reclaim_buffers_locked)
		drm_reclaim_buffers(dev, file_priv);

	dev->buf_pgid = 0;

	if (dev->driver->postclose != NULL)
		dev->driver->postclose(dev, file_priv);

	DRM_LOCK();
	TAILQ_REMOVE(&dev->files, file_priv, link);
	drm_free(file_priv, sizeof(*file_priv), DRM_MEM_FILES);

done:
	if (--dev->open_count == 0) {
		DRM_UNLOCK();
		retcode = drm_lastclose(dev);
	}

	DRM_UNLOCK();
	
	return retcode;
}

/* drmioctl is called whenever a process performs an ioctl on /dev/drm.
 */
int
drmioctl(dev_t kdev, u_long cmd, caddr_t data, int flags, 
    struct proc *p)
{
	struct drm_device *dev = drm_get_device_from_kdev(kdev);
	struct drm_file *file_priv;

	if (dev == NULL)
		return ENODEV;

	DRM_LOCK();
	file_priv = drm_find_file_by_minor(dev, minor(kdev));
	DRM_UNLOCK();
	if (file_priv == NULL) {
		DRM_ERROR("can't find authenticator\n");
		return EINVAL;
	}

	++file_priv->ioctl_count;

	DRM_DEBUG("pid=%d, cmd=0x%02lx, nr=0x%02x, dev 0x%lx, auth=%d\n",
	    DRM_CURRENTPID, cmd, DRM_IOCTL_NR(cmd), (long)&dev->device,
	    file_priv->authenticated);

	switch (cmd) {
	case FIONBIO:
	case FIOASYNC:
		return 0;

	case TIOCSPGRP:
		dev->buf_pgid = *(int *)data;
		return 0;

	case TIOCGPGRP:
		*(int *)data = dev->buf_pgid;
		return 0;
	case DRM_IOCTL_VERSION:
		return (drm_version(dev, data, file_priv));
	case DRM_IOCTL_GET_UNIQUE:
		return (drm_getunique(dev, data, file_priv));
	case DRM_IOCTL_GET_MAGIC:
		return (drm_getmagic(dev, data, file_priv));
	case DRM_IOCTL_GET_MAP:
		return (drm_getmap(dev, data, file_priv));
	case DRM_IOCTL_GET_CLIENT:
		return (drm_getclient(dev, data, file_priv));
	case DRM_IOCTL_GET_STATS:
		return (drm_getstats(dev, data, file_priv));
	case DRM_IOCTL_WAIT_VBLANK:
		return (drm_wait_vblank(dev, data, file_priv));
	case DRM_IOCTL_MODESET_CTL:
		return (drm_modeset_ctl(dev, data, file_priv));

	/*
	 * no-oped ioctls, we don't check permissions on them because
	 * they do nothing. they'll be removed as soon as userland is
	 * definitely purged
	 */
	case DRM_IOCTL_SET_SAREA_CTX:
	case DRM_IOCTL_BLOCK:
	case DRM_IOCTL_UNBLOCK:
	case DRM_IOCTL_MOD_CTX:
	case DRM_IOCTL_MARK_BUFS:
	case DRM_IOCTL_FINISH:
	case DRM_IOCTL_INFO_BUFS:
	case DRM_IOCTL_SWITCH_CTX:
	case DRM_IOCTL_NEW_CTX:
	case DRM_IOCTL_GET_SAREA_CTX:
		return (0);
	}

	if (file_priv->authenticated == 1) {
		switch (cmd) {
		case DRM_IOCTL_RM_MAP:
			return (drm_rmmap_ioctl(dev, data, file_priv));
		case DRM_IOCTL_GET_CTX:
			return (drm_getctx(dev, data, file_priv));
		case DRM_IOCTL_RES_CTX:
			return (drm_resctx(dev, data, file_priv));
		case DRM_IOCTL_LOCK:
			return (drm_lock(dev, data, file_priv));
		case DRM_IOCTL_UNLOCK:
			return (drm_unlock(dev, data, file_priv));
		case DRM_IOCTL_MAP_BUFS:
			return (drm_mapbufs(dev, data, file_priv));
		case DRM_IOCTL_FREE_BUFS:
			return (drm_freebufs(dev, data, file_priv));
		case DRM_IOCTL_DMA:
			return (drm_dma(dev, data, file_priv));
		case DRM_IOCTL_AGP_INFO:
			return (drm_agp_info_ioctl(dev, data, file_priv));
		}
	}

	/* master is always root */
	if (file_priv->master == 1) {
		switch(cmd) {
		case DRM_IOCTL_SET_VERSION:
			return (drm_setversion(dev, data, file_priv));
		case DRM_IOCTL_IRQ_BUSID:
			return (drm_irq_by_busid(dev, data, file_priv));
		case DRM_IOCTL_SET_UNIQUE:
			return (drm_setunique(dev, data, file_priv));
		case DRM_IOCTL_AUTH_MAGIC:
			return (drm_authmagic(dev, data, file_priv));
		case DRM_IOCTL_ADD_MAP:
			return (drm_addmap_ioctl(dev, data, file_priv));
		case DRM_IOCTL_ADD_CTX:
			return (drm_addctx(dev, data, file_priv));
		case DRM_IOCTL_RM_CTX:
			return (drm_rmctx(dev, data, file_priv));
		case DRM_IOCTL_ADD_DRAW:
			return (drm_adddraw(dev, data, file_priv));
		case DRM_IOCTL_RM_DRAW:
			return (drm_rmdraw(dev, data, file_priv));
		case DRM_IOCTL_ADD_BUFS:
			return (drm_addbufs_ioctl(dev, data, file_priv));
		case DRM_IOCTL_CONTROL:
			return (drm_control(dev, data, file_priv));
		case DRM_IOCTL_AGP_ACQUIRE:
			return (drm_agp_acquire_ioctl(dev, data, file_priv));
		case DRM_IOCTL_AGP_RELEASE:
			return (drm_agp_release_ioctl(dev, data, file_priv));
		case DRM_IOCTL_AGP_ENABLE:
			return (drm_agp_enable_ioctl(dev, data, file_priv));
		case DRM_IOCTL_AGP_ALLOC:
			return (drm_agp_alloc_ioctl(dev, data, file_priv));
		case DRM_IOCTL_AGP_FREE:
			return (drm_agp_free_ioctl(dev, data, file_priv));
		case DRM_IOCTL_AGP_BIND:
			return (drm_agp_bind_ioctl(dev, data, file_priv));
		case DRM_IOCTL_AGP_UNBIND:
			return (drm_agp_unbind_ioctl(dev, data, file_priv));
		case DRM_IOCTL_SG_ALLOC:
			return (drm_sg_alloc_ioctl(dev, data, file_priv));
		case DRM_IOCTL_SG_FREE:
			return (drm_sg_free(dev, data, file_priv));
		case DRM_IOCTL_UPDATE_DRAW:
			return (drm_update_draw(dev, data, file_priv));
		}
	}
	if (dev->driver->ioctl != NULL)
		return (dev->driver->ioctl(dev, cmd, data, file_priv));
	else
		return (EINVAL);
}

drm_local_map_t *
drm_getsarea(struct drm_device *dev)
{
	drm_local_map_t *map;

	DRM_LOCK();
	TAILQ_FOREACH(map, &dev->maplist, link) {
		if (map->type == _DRM_SHM && (map->flags & _DRM_CONTAINS_LOCK))
			break;
	}
	DRM_UNLOCK();
	return (map);
}
