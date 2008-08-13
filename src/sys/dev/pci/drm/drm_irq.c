/*-
 * Copyright 2003 Eric Anholt
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
 * ERIC ANHOLT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <anholt@FreeBSD.org>
 *
 */

/** @file drm_irq.c
 * Support code for handling setup/teardown of interrupt handlers and
 * handing interrupt handlers off to the drivers.
 */

#include <sys/workq.h>

#include "drmP.h"
#include "drm.h"

irqreturn_t	drm_irq_handler_wrap(DRM_IRQ_ARGS);
void		vblank_disable(void *);
void		drm_update_vblank_count(struct drm_device *, int);
void		drm_locked_task(void *context, void *pending);

int
drm_irq_by_busid(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_irq_busid_t *irq = data;

	if ((irq->busnum >> 8) != dev->pci_domain ||
	    (irq->busnum & 0xff) != dev->pci_bus ||
	    irq->devnum != dev->pci_slot ||
	    irq->funcnum != dev->pci_func)
		return EINVAL;

	irq->irq = dev->irq;

	DRM_DEBUG("%d:%d:%d => IRQ %d\n",
		  irq->busnum, irq->devnum, irq->funcnum, irq->irq);

	return 0;
}

irqreturn_t
drm_irq_handler_wrap(DRM_IRQ_ARGS)
{
	irqreturn_t ret;
	struct drm_device *dev = (struct drm_device *)arg;

	DRM_SPINLOCK(&dev->irq_lock);
	ret = dev->driver.irq_handler(arg);
	DRM_SPINUNLOCK(&dev->irq_lock);

	return ret;
}

int
drm_irq_install(struct drm_device *dev)
{
	int retcode;
	pci_intr_handle_t ih;
	const char *istr;

	if (dev->irq == 0 || dev->dev_private == NULL)
		return EINVAL;

	DRM_DEBUG( "%s: irq=%d\n", __FUNCTION__, dev->irq );

	DRM_LOCK();
	if (dev->irq_enabled) {
		DRM_UNLOCK();
		return EBUSY;
	}
	dev->irq_enabled = 1;

	mtx_init(&dev->irq_lock, IPL_BIO);

				/* Before installing handler */
	dev->driver.irq_preinstall(dev);
	DRM_UNLOCK();

				/* Install handler */
	if (pci_intr_map(&dev->pa, &ih) != 0) {
		retcode = ENOENT;
		goto err;
	}
	istr = pci_intr_string(dev->pa.pa_pc, ih);
	dev->irqh = pci_intr_establish(dev->pa.pa_pc, ih, IPL_BIO,
	    drm_irq_handler_wrap, dev,
	    dev->device.dv_xname);
	if (!dev->irqh) {
		retcode = ENOENT;
		goto err;
	}
	DRM_DEBUG("%s: interrupting at %s\n", dev->device.dv_xname, istr);

				/* After installing handler */
	DRM_LOCK();
	dev->driver.irq_postinstall(dev);
	DRM_UNLOCK();

	return 0;
err:
	DRM_LOCK();
	dev->irq_enabled = 0;
	DRM_SPINUNINIT(&dev->irq_lock);
	DRM_UNLOCK();
	return retcode;
}

int
drm_irq_uninstall(struct drm_device *dev)
{

	if (!dev->irq_enabled)
		return EINVAL;

	dev->irq_enabled = 0;

	DRM_DEBUG( "%s: irq=%d\n", __FUNCTION__, dev->irq );

	dev->driver.irq_uninstall(dev);

	pci_intr_disestablish(dev->pa.pa_pc, dev->irqh);

	drm_vblank_cleanup(dev);
	DRM_SPINUNINIT(&dev->irq_lock);

	return 0;
}

int
drm_control(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_control_t *ctl = data;
	int err;

	switch ( ctl->func ) {
	case DRM_INST_HANDLER:
		/* Handle drivers whose DRM used to require IRQ setup but the
		 * no longer does.
		 */
		if (!dev->driver.use_irq)
			return 0;
		if (dev->if_version < DRM_IF_VERSION(1, 2) &&
		    ctl->irq != dev->irq)
			return EINVAL;
		return drm_irq_install(dev);
	case DRM_UNINST_HANDLER:
		if (!dev->driver.use_irq)
			return 0;
		DRM_LOCK();
		err = drm_irq_uninstall(dev);
		DRM_UNLOCK();
		return err;
	default:
		return EINVAL;
	}
}

void
vblank_disable(void *arg)
{
	struct drm_device *dev = (struct drm_device*)arg;
	int i;

	if (!dev->vblank_disable_allowed)
		return;

	for (i=0; i < dev->num_crtcs; i++){
		DRM_SPINLOCK(&dev->vbl_lock);
		if (atomic_read(&dev->vblank_refcount[i]) == 0 &&
		    dev->vblank_enabled[i]) {
			dev->last_vblank[i] =
			    dev->driver.get_vblank_counter(dev, i);
			dev->driver.disable_vblank(dev, i);
			dev->vblank_enabled[i] = 0;
		}
		DRM_SPINUNLOCK(&dev->vbl_lock);
	}
}

void
drm_vblank_cleanup(struct drm_device *dev)
{
	if (dev->num_crtcs == 0)
		return; /* not initialised */

	timeout_del(&dev->vblank_disable_timer);

	vblank_disable(dev);

	drm_free(dev->vbl_queue, sizeof(*dev->vbl_queue) *
	    dev->num_crtcs, M_DRM);
#if 0 /* disabled for now */
	drm_free(dev->vbl_sigs, sizeof(*dev->vbl_sigs) * dev->num_crtcs, M_DRM);
#endif
	drm_free(dev->_vblank_count, sizeof(*dev->_vblank_count) *
	    dev->num_crtcs, M_DRM);
	drm_free(dev->vblank_refcount, sizeof(*dev->vblank_refcount) *
	    dev->num_crtcs, M_DRM);
	drm_free(dev->vblank_enabled, sizeof(*dev->vblank_enabled) *
	    dev->num_crtcs, M_DRM);
	drm_free(dev->last_vblank, sizeof(*dev->last_vblank) *
	    dev->num_crtcs, M_DRM);
	drm_free(dev->vblank_inmodeset, sizeof(*dev->vblank_inmodeset) *
	    dev->num_crtcs, M_DRM);

	dev->num_crtcs = 0;
	DRM_SPINUNINIT(&dev->vbl_lock);
}

int
drm_vblank_init(struct drm_device *dev, int num_crtcs)
{
	int i;

	timeout_set(&dev->vblank_disable_timer, vblank_disable, dev);
	mtx_init(&dev->vbl_lock, IPL_BIO);
	atomic_set(&dev->vbl_signal_pending, 0);
	dev->num_crtcs = num_crtcs;

	if ((dev->vbl_queue = drm_calloc(num_crtcs, sizeof(*dev->vbl_queue),
	    M_DRM)) == NULL)
		goto err;

	if ((dev->_vblank_count = drm_calloc(num_crtcs,
	    sizeof(*dev->_vblank_count), M_DRM)) == NULL)
		goto err;

	if ((dev->vblank_refcount = drm_calloc(num_crtcs,
	    sizeof(*dev->vblank_refcount), M_DRM)) == NULL)
		goto err;
	if ((dev->vblank_enabled = drm_calloc(num_crtcs,
	    sizeof(*dev->vblank_enabled), M_DRM)) == NULL)
		goto err;
	if ((dev->last_vblank = drm_calloc(num_crtcs,
	    sizeof(*dev->last_vblank), M_DRM)) == NULL)
		goto err;
	if ((dev->vblank_inmodeset = drm_calloc(num_crtcs,
	    sizeof(*dev->vblank_inmodeset), M_DRM)) == NULL)
		goto err;

	/* Zero everything */
	for (i = 0; i < num_crtcs; i++) {
		atomic_set(&dev->_vblank_count[i], 0);
		atomic_set(&dev->vblank_refcount[i], 0);
	}

	dev->vblank_disable_allowed = 0;

	return (0);

err:
	drm_vblank_cleanup(dev);
	return ENOMEM;
}

u_int32_t
drm_vblank_count(struct drm_device *dev, int crtc)
{
	return atomic_read(&dev->_vblank_count[crtc]);
}

void
drm_update_vblank_count(struct drm_device *dev, int crtc)
{
	u_int32_t cur_vblank, diff;

	/*
	 * Interrupt was disabled prior to this call, so deal with counter wrap
	 * note that we may have lost a full dev->max_vblank_count events if
	 * the register is small or the interrupts were off for a long time.
	 */
	cur_vblank = dev->driver.get_vblank_counter(dev, crtc);
	diff = cur_vblank - dev->last_vblank[crtc];
	if (cur_vblank < dev->last_vblank[crtc])
		diff += dev->max_vblank_count;

	atomic_add(diff, &dev->_vblank_count[crtc]);
}

int
drm_vblank_get(struct drm_device *dev, int crtc)
{
	int ret = 0;

	DRM_SPINLOCK(&dev->vbl_lock);

	atomic_add(1, &dev->vblank_refcount[crtc]);
	if (dev->vblank_refcount[crtc] == 1 &&
	    dev->vblank_enabled[crtc] == 0) {
		ret = dev->driver.enable_vblank(dev, crtc);
		if (ret) {
			atomic_dec(&dev->vblank_refcount[crtc]);
		} else {
			dev->vblank_enabled[crtc] = 1;
			drm_update_vblank_count(dev, crtc);
		}
	}
	DRM_SPINUNLOCK(&dev->vbl_lock);

	return (ret);
}

void
drm_vblank_put(struct drm_device *dev, int crtc)
{
	DRM_SPINLOCK(&dev->vbl_lock);
	/* Last user schedules interrupt disable */
	atomic_dec(&dev->vblank_refcount[crtc]);
	if (dev->vblank_refcount[crtc] == 0) 
		timeout_add_sec(&dev->vblank_disable_timer, 5);
	DRM_SPINUNLOCK(&dev->vbl_lock);
}

int
drm_modeset_ctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_modeset_ctl *modeset = data;
	int crtc, ret = 0;

	/* not initialised yet, just noop */
	if (dev->num_crtcs == 0)
		goto out;

	crtc = modeset->crtc;
	if (crtc >= dev->num_crtcs) {
		ret = EINVAL;
		goto out;
	}

	/* If interrupts are enabled/disabled between calls to this ioctl then
	 * it can get nasty. So just grab a reference so that the interrupts
	 * keep going through the modeset
	 */
	switch (modeset->cmd) {
	case _DRM_PRE_MODESET:
		if (dev->vblank_inmodeset[crtc] == 0) {
			dev->vblank_inmodeset[crtc] = 1;
			drm_vblank_get(dev, crtc);
		}
		break;
	case _DRM_POST_MODESET:
		if (dev->vblank_inmodeset[crtc]) {
			DRM_SPINLOCK(&dev->vbl_lock);
			dev->vblank_disable_allowed = 1;
			dev->vblank_inmodeset[crtc] = 0;
			DRM_SPINUNLOCK(&dev->vbl_lock);
			drm_vblank_put(dev, crtc);
		}
		break;
	default:
		ret = EINVAL;
		break;
	}

out:
	return (ret);
}

int
drm_wait_vblank(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_wait_vblank_t *vblwait = data;
	int ret, flags, crtc, seq;

	if (!dev->irq_enabled)
		return EINVAL;

	flags = vblwait->request.type & _DRM_VBLANK_FLAGS_MASK;
	crtc = flags & _DRM_VBLANK_SECONDARY ? 1 : 0;

	if (crtc >= dev->num_crtcs)
		return EINVAL;

	ret = drm_vblank_get(dev, crtc);
	if (ret)
		return (ret);
	seq = drm_vblank_count(dev,crtc);

	if (vblwait->request.type & _DRM_VBLANK_RELATIVE) {
		vblwait->request.sequence += seq;
		vblwait->request.type &= ~_DRM_VBLANK_RELATIVE;
	}

	flags = vblwait->request.type & _DRM_VBLANK_FLAGS_MASK;
	if ((flags & _DRM_VBLANK_NEXTONMISS) &&
	    (seq - vblwait->request.sequence) <= (1<<23)) {
		vblwait->request.sequence = seq + 1;
	}

	if (flags & _DRM_VBLANK_SIGNAL) {
#if 0 /* disabled */
		drm_vbl_sig_t *vbl_sig = drm_calloc(1, sizeof(drm_vbl_sig_t),
		    DRM_MEM_DRIVER);
		if (vbl_sig == NULL)
			return ENOMEM;

		vbl_sig->sequence = vblwait->request.sequence;
		vbl_sig->signo = vblwait->request.signal;
		vbl_sig->pid = DRM_CURRENTPID;

		vblwait->reply.sequence = atomic_read(&dev->vbl_received);

		
		DRM_SPINLOCK(&dev->vbl_lock);
		TAILQ_INSERT_HEAD(&dev->vbl_sig_list, vbl_sig, link);
		DRM_SPINUNLOCK(&dev->vbl_lock);
		ret = 0;
#endif
		ret = EINVAL;
	} else {
		while (ret == 0) {
			DRM_SPINLOCK(&dev->vbl_lock);
			if ((drm_vblank_count(dev, crtc)
			    - vblwait->request.sequence) <= (1 << 23)) {
				DRM_SPINUNLOCK(&dev->vbl_lock);
				break;
			}
			ret = msleep(&dev->vbl_queue[crtc],
			    &dev->vbl_lock, PZERO | PCATCH,
			    "drmvblq", 3 * DRM_HZ);
			DRM_SPINUNLOCK(&dev->vbl_lock);
		}

		if (ret != EINTR) {
			struct timeval now;

			microtime(&now);
			vblwait->reply.tval_sec = now.tv_sec;
			vblwait->reply.tval_usec = now.tv_usec;
			vblwait->reply.sequence = drm_vblank_count(dev, crtc);
		}
	}

	drm_vblank_put(dev, crtc);
	return (ret);
}

void
drm_vbl_send_signals(struct drm_device *dev, int crtc)
{
}

#if 0 /* disabled */
void
drm_vbl_send_signals(struct drm_device *dev, int crtc)
{
	drm_vbl_sig_t *vbl_sig;
	unsigned int vbl_seq = atomic_read( &dev->vbl_received );
	struct proc *p;

	vbl_sig = TAILQ_FIRST(&dev->vbl_sig_list);
	while (vbl_sig != NULL) {
		drm_vbl_sig_t *next = TAILQ_NEXT(vbl_sig, link);

		if ( ( vbl_seq - vbl_sig->sequence ) <= (1<<23) ) {
			p = pfind(vbl_sig->pid);
			if (p != NULL)
				psignal(p, vbl_sig->signo);

			TAILQ_REMOVE(&dev->vbl_sig_list, vbl_sig, link);
			drm_free(vbl_sig, sizeof(*vbl_sig), DRM_MEM_DRIVER);
		}
		vbl_sig = next;
	}
}
#endif

void
drm_handle_vblank(struct drm_device *dev, int crtc)
{
	atomic_inc(&dev->_vblank_count[crtc]);
	DRM_WAKEUP(&dev->vbl_queue[crtc]);
	drm_vbl_send_signals(dev, crtc);
}

void
drm_locked_task(void *context, void *pending)
{
	struct drm_device *dev = context;

	DRM_SPINLOCK(&dev->tsk_lock);

	DRM_LOCK(); /* XXX drm_lock_take() should do its own locking */
	if (dev->locked_task_call == NULL ||
	    drm_lock_take(&dev->lock, DRM_KERNEL_CONTEXT) == 0) {
		DRM_UNLOCK();
		DRM_SPINUNLOCK(&dev->tsk_lock);
		return;
	}

	dev->lock.file_priv = NULL; /* kernel owned */
	dev->lock.lock_time = jiffies;
	atomic_inc(&dev->counts[_DRM_STAT_LOCKS]);

	DRM_UNLOCK();

	dev->locked_task_call(dev);

	drm_lock_free(&dev->lock, DRM_KERNEL_CONTEXT);

	dev->locked_task_call = NULL;

	DRM_SPINUNLOCK(&dev->tsk_lock);
}

void
drm_locked_tasklet(struct drm_device *dev, void (*tasklet)(struct drm_device *))
{
	DRM_SPINLOCK(&dev->tsk_lock);
	if (dev->locked_task_call != NULL) {
		DRM_SPINUNLOCK(&dev->tsk_lock);
		return;
	}

	dev->locked_task_call = tasklet;
	DRM_SPINUNLOCK(&dev->tsk_lock);

	if (workq_add_task(NULL, 0, drm_locked_task, dev, NULL) == ENOMEM)
		DRM_ERROR("error adding task to workq\n");
}
