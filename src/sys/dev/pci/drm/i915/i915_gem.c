/*	$OpenBSD: i915_gem.c,v 1.28 2013/07/10 02:21:09 jsg Exp $	*/
/*
 * Copyright (c) 2008-2009 Owain G. Ainsworth <oga@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright © 2008 Intel Corporation
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <dev/pci/drm/drmP.h>
#include <dev/pci/drm/drm.h>
#include <dev/pci/drm/i915_drm.h>
#include "i915_drv.h"
#include "intel_drv.h"

#include <machine/pmap.h>

#include <sys/queue.h>
#include <sys/workq.h>

int i915_gem_object_needs_bit17_swizzle(struct drm_i915_gem_object *obj);
int i915_gem_object_get_pages_gtt(struct drm_i915_gem_object *obj);
void i915_gem_object_put_pages_gtt(struct drm_i915_gem_object *obj);
uint32_t i915_gem_get_gtt_size(struct drm_device *dev, uint32_t size,
			       int tiling_mode);
uint32_t i915_gem_get_gtt_alignment(struct drm_device *dev,
				    uint32_t size, int tiling_mode);
void i915_gem_object_finish_gtt(struct drm_i915_gem_object *);
void i915_gem_object_flush_cpu_write_domain(struct drm_i915_gem_object *);
int i915_gem_init_phys_object(struct drm_device *, int, int, int);
int i915_gem_phys_pwrite(struct drm_device *, struct drm_i915_gem_object *,
			 struct drm_i915_gem_pwrite *, struct drm_file *);
bool intel_enable_blt(struct drm_device *);
int i915_gem_handle_seqno_wrap(struct drm_device *);
void i915_gem_object_update_fence(struct drm_i915_gem_object *,
    struct drm_i915_fence_reg *, bool);
int i915_gem_object_flush_fence(struct drm_i915_gem_object *);
struct drm_i915_fence_reg *i915_find_fence_reg(struct drm_device *);
void i915_gem_reset_ring_lists(struct drm_i915_private *,
    struct intel_ring_buffer *);
void i915_gem_object_flush_gtt_write_domain(struct drm_i915_gem_object *);
void i915_gem_request_remove_from_client(struct drm_i915_gem_request *);
int i915_gem_object_flush_active(struct drm_i915_gem_object *);
int i915_gem_check_olr(struct intel_ring_buffer *, u32);
void i915_gem_object_truncate(struct drm_i915_gem_object *obj);
int i915_gem_object_bind_to_gtt(struct drm_i915_gem_object *obj,
				unsigned alignment, bool map_and_fenceable,
				bool nonblocking);
int i915_gem_wait_for_error(struct drm_device *);
int __wait_seqno(struct intel_ring_buffer *, uint32_t, bool, struct timespec *);
int i915_gem_object_create_mmap_offset(struct drm_i915_gem_object *);
void i915_gem_object_free_mmap_offset(struct drm_i915_gem_object *);
void i915_gem_object_init(struct drm_i915_gem_object *);

extern int ticks;

static inline void
i915_gem_object_fence_lost(struct drm_i915_gem_object *obj)
{
	if (obj->tiling_mode)
		i915_gem_release_mmap(obj);

	/* As we do not have an associated fence register, we will force
	 * a tiling change if we ever need to acquire one.
	 */
	obj->fence_dirty = false;
	obj->fence_reg = I915_FENCE_REG_NONE;
}

// i915_gem_info_add_obj
// i915_gem_info_remove_obj

int
i915_gem_wait_for_error(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	if (!atomic_read(&dev_priv->mm.wedged))
		return 0;

	/*
	 * Only wait 10 seconds for the gpu reset to complete to avoid hanging
	 * userspace. If it takes that long something really bad is going on and
	 * we should simply try to bail out and fail as gracefully as possible.
	 */
	mtx_enter(&dev_priv->error_completion_lock);
	while (dev_priv->error_completion == 0) {
		ret = -msleep(&dev_priv->error_completion,
		    &dev_priv->error_completion_lock, PCATCH, "915wco", 10*hz);
		if (ret != 0) {
			mtx_leave(&dev_priv->error_completion_lock);
			return (ret);
		}
	}
	mtx_leave(&dev_priv->error_completion_lock);

	if (atomic_read(&dev_priv->mm.wedged)) {
		mtx_enter(&dev_priv->error_completion_lock);
		dev_priv->error_completion++;
		mtx_leave(&dev_priv->error_completion_lock);
	}
	return 0;
}

int
i915_mutex_lock_interruptible(struct drm_device *dev)
{
	int ret;

	ret = i915_gem_wait_for_error(dev);
	if (ret)
		return ret;

	ret = rw_enter(&dev->dev_lock, RW_WRITE | RW_INTR);
	if (ret)
		return ret;

	WARN_ON(i915_verify_lists(dev));
	return 0;
}

static inline bool
i915_gem_object_is_inactive(struct drm_i915_gem_object *obj)
{
	return obj->dmamap && !obj->active && obj->pin_count == 0;
}

int
i915_gem_init_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct inteldrm_softc		*dev_priv = dev->dev_private;
	struct drm_i915_gem_init	*args = data;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return ENODEV;

	DRM_LOCK();

	if (args->gtt_start >= args->gtt_end ||
	    args->gtt_end > dev->agp->info.ai_aperture_size ||
	    (args->gtt_start & PAGE_MASK) != 0 ||
	    (args->gtt_end & PAGE_MASK) != 0) {
		DRM_UNLOCK();
		return (EINVAL);
	}
	/*
	 * putting stuff in the last page of the aperture can cause nasty
	 * problems with prefetch going into unassigned memory. Since we put
	 * a scratch page on all unused aperture pages, just leave the last
	 * page as a spill to prevent gpu hangs.
	 */
	if (args->gtt_end == dev->agp->info.ai_aperture_size)
		args->gtt_end -= 4096;

	if (agp_bus_dma_init((struct agp_softc *)dev->agp->agpdev,
	    dev->agp->base + args->gtt_start, dev->agp->base + args->gtt_end,
	    &dev_priv->agpdmat) != 0) {
		DRM_UNLOCK();
		return (ENOMEM);
	}

	dev->gtt_total = (uint32_t)(args->gtt_end - args->gtt_start);
	inteldrm_set_max_obj_size(dev_priv);

	DRM_UNLOCK();

	return 0;
}

int
i915_gem_get_aperture_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file)
{
	struct drm_i915_gem_get_aperture *args = data;

	/* we need a write lock here to make sure we get the right value */
	DRM_LOCK();
	args->aper_size = dev->gtt_total;
	args->aper_available_size = (args->aper_size -
	    atomic_read(&dev->pin_memory));
	DRM_UNLOCK();

	return 0;
}

int
i915_gem_create(struct drm_file *file,
		struct drm_device *dev,
		uint64_t size,
		uint32_t *handle_p)
{
	struct drm_i915_gem_object *obj;
	int ret;
	u32 handle;

	size = round_page(size);
	if (size == 0)
		return -EINVAL;

	/* Allocate the new object */
	obj = i915_gem_alloc_object(dev, size);
	if (obj == NULL)
		return -ENOMEM;

	handle = 0;
	ret = drm_handle_create(file, &obj->base, &handle);
	if (ret != 0) {
		drm_unref(&obj->base.uobj);
		return (-ret);
	}

	*handle_p = handle;
	return 0;
}

int
i915_gem_dumb_create(struct drm_file *file,
		     struct drm_device *dev,
		     struct drm_mode_create_dumb *args)
{
	/* have to work out size/pitch and return them */
	args->pitch = roundup2(args->width * ((args->bpp + 7) / 8), 64);
	args->size = args->pitch * args->height;
	return i915_gem_create(file, dev,
			       args->size, &args->handle);
}

int
i915_gem_dumb_destroy(struct drm_file *file, struct drm_device *dev,
    uint32_t handle)
{

	printf("%s stub\n", __func__);
	return ENOSYS;
//	return (drm_gem_handle_delete(file, handle));
}

/**
 * Creates a new mm object and returns a handle to it.
 */
int
i915_gem_create_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file)
{
	struct inteldrm_softc		*dev_priv = dev->dev_private;
	struct drm_i915_gem_create	*args = data;
	struct drm_i915_gem_object	*obj;
	int				 handle, ret;

	args->size = round_page(args->size);
	/*
	 * XXX to avoid copying between 2 objs more than half the aperture size
	 * we don't allow allocations that are that big. This will be fixed
	 * eventually by intelligently falling back to cpu reads/writes in
	 * such cases. (linux allows this but does cpu maps in the ddx instead).
	 */
	if (args->size > dev_priv->max_gem_obj_size)
		return (EFBIG);

	/* Allocate the new object */
	obj = i915_gem_alloc_object(dev, args->size);
	if (obj == NULL)
		return (ENOMEM);

	/* we give our reference to the handle */
	ret = drm_handle_create(file, &obj->base, &handle);

	if (ret == 0)
		args->handle = handle;
	else
		drm_unref(&obj->base.uobj);

	return (ret);
}

int
i915_gem_object_needs_bit17_swizzle(struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = obj->base.dev->dev_private;

	return dev_priv->mm.bit_6_swizzle_x == I915_BIT_6_SWIZZLE_9_10_17 &&
		obj->tiling_mode != I915_TILING_NONE;
}

/**
 * Reads data from the object referenced by handle.
 *
 * On error, the contents of *data are undefined.
 */
int
i915_gem_pread_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file)
{
	struct inteldrm_softc		*dev_priv = dev->dev_private;
	struct drm_i915_gem_pread	*args = data;
	struct drm_i915_gem_object	*obj;
	char				*vaddr;
	bus_space_handle_t		 bsh;
	bus_size_t			 bsize;
	voff_t				 offset;
	int				 ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (obj == NULL)
		return ENOENT;
	DRM_READLOCK();
	drm_hold_object(&obj->base);

	/*
	 * Bounds check source.
	 */
	if (args->offset > obj->base.size || args->size > obj->base.size ||
	    args->offset + args->size > obj->base.size) {
		ret = EINVAL;
		goto out;
	}

	ret = i915_gem_object_pin(obj, 0, true, true);
	if (ret)
		goto out;

	ret = i915_gem_object_set_to_gtt_domain(obj, false);
	if (ret)
		goto unpin;

	offset = obj->gtt_offset + args->offset;
	bsize = round_page(offset + args->size) - trunc_page(offset);

	if ((ret = agp_map_subregion(dev_priv->agph,
	    trunc_page(offset), bsize, &bsh)) != 0)
		goto unpin;
	vaddr = bus_space_vaddr(dev->bst, bsh);
	if (vaddr == NULL) {
		ret = EFAULT;
		goto unmap;
	}

	ret = copyout(vaddr + (offset & PAGE_MASK),
	    (char *)(uintptr_t)args->data_ptr, args->size);

unmap:
	agp_unmap_subregion(dev_priv->agph, bsh, bsize);
unpin:
	i915_gem_object_unpin(obj);
out:
	drm_unhold_and_unref(&obj->base);
	DRM_READUNLOCK();

	return (ret);
}

/**
 * Writes data to the object referenced by handle.
 *
 * On error, the contents of the buffer that were to be modified are undefined.
 */
int
i915_gem_pwrite_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file)
{
	struct inteldrm_softc		*dev_priv = dev->dev_private;
	struct drm_i915_gem_pwrite	*args = data;
	struct drm_i915_gem_object	*obj;
	char				*vaddr;
	bus_space_handle_t		 bsh;
	bus_size_t			 bsize;
	off_t				 offset;
	int				 ret = 0;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (obj == NULL)
		return ENOENT;
	DRM_READLOCK();
	drm_hold_object(&obj->base);

	/* Bounds check destination. */
	if (args->offset > obj->base.size || args->size > obj->base.size ||
	    args->offset + args->size > obj->base.size) {
		ret = EINVAL;
		goto out;
	}

	if (obj->phys_obj) {
		ret = i915_gem_phys_pwrite(dev, obj, args, file);
		goto out;
	}

	ret = i915_gem_object_pin(obj, 0, true, true);
	if (ret)
		goto out;

	ret = i915_gem_object_set_to_gtt_domain(obj, true);
	if (ret)
		goto unpin;

	ret = i915_gem_object_put_fence(obj);
	if (ret)
		goto unpin;

	offset = obj->gtt_offset + args->offset;
	bsize = round_page(offset + args->size) - trunc_page(offset);

	if ((ret = agp_map_subregion(dev_priv->agph,
	    trunc_page(offset), bsize, &bsh)) != 0)
		goto unpin;
	vaddr = bus_space_vaddr(dev_priv->bst, bsh);
	if (vaddr == NULL) {
		ret = EFAULT;
		goto unmap;
	}

	ret = copyin((char *)(uintptr_t)args->data_ptr,
	    vaddr + (offset & PAGE_MASK), args->size);

unmap:
	agp_unmap_subregion(dev_priv->agph, bsh, bsize);
unpin:
	i915_gem_object_unpin(obj);
out:
	drm_unhold_and_unref(&obj->base);
	DRM_READUNLOCK();

	return (ret);
}

int
i915_gem_check_wedge(struct drm_i915_private *dev_priv,
		     bool interruptible)
{
	if (atomic_read(&dev_priv->mm.wedged)) {
		bool recovery_complete;

		/* Give the error handler a chance to run. */
		mtx_enter(&dev_priv->error_completion_lock);
		recovery_complete = (&dev_priv->error_completion) > 0;
		mtx_leave(&dev_priv->error_completion_lock);
		
		/* Non-interruptible callers can't handle -EAGAIN, hence return
		 * -EIO unconditionally for these. */
		if (!interruptible)
			return -EIO;

		/* Recovery complete, but still wedged means reset failure. */
		if (recovery_complete)
			return -EIO;

		return -EAGAIN;
	}

	return 0;
}

/*
 * Compare seqno against outstanding lazy request. Emit a request if they are
 * equal.
 */
int
i915_gem_check_olr(struct intel_ring_buffer *ring, u32 seqno)
{
	int ret;

//	BUG_ON(!mutex_is_locked(&ring->dev->struct_mutex));

	ret = 0;
	if (seqno == ring->outstanding_lazy_request)
		ret = i915_add_request(ring, NULL, NULL);

	return ret;
}

/**
 * __wait_seqno - wait until execution of seqno has finished
 * @ring: the ring expected to report seqno
 * @seqno: duh!
 * @interruptible: do an interruptible wait (normally yes)
 * @timeout: in - how long to wait (NULL forever); out - how much time remaining
 *
 * Returns 0 if the seqno was found within the alloted time. Else returns the
 * errno with remaining time filled in timeout argument.
 */
int
__wait_seqno(struct intel_ring_buffer *ring, uint32_t seqno,
		bool interruptible, struct timespec *timeout)
{
	struct drm_device *dev = ring->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret = 0;

	mtx_enter(&dev_priv->irq_lock);
	if (!i915_seqno_passed(ring->get_seqno(ring, true), seqno)) {
		ring->irq_get(ring);
		while (ret == 0) {
			if (i915_seqno_passed(ring->get_seqno(ring, false),
			    seqno) || dev_priv->mm.wedged)
				break;
			ret = msleep(ring, &dev_priv->irq_lock,
			    PZERO | (interruptible ? PCATCH : 0),
			    "gemwt", 0);
		}
		ring->irq_put(ring);
	}
	mtx_leave(&dev_priv->irq_lock);
	if (dev_priv->mm.wedged)
		ret = EIO;

	return (ret);
}

/**
 * Waits for a sequence number to be signaled, and cleans up the
 * request and object lists appropriately for that event.
 */
int
i915_wait_seqno(struct intel_ring_buffer *ring, uint32_t seqno)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible = dev_priv->mm.interruptible;
	int ret;

//	BUG_ON(!mutex_is_locked(&dev->struct_mutex));
	BUG_ON(seqno == 0);

	ret = i915_gem_check_wedge(dev_priv, interruptible);
	if (ret)
		return ret;

	ret = i915_gem_check_olr(ring, seqno);
	if (ret)
		return ret;

	return __wait_seqno(ring, seqno, interruptible, NULL);
}

int
i915_gem_object_wait_rendering(struct drm_i915_gem_object *obj,
			       bool readonly)
{
	struct intel_ring_buffer *ring = obj->ring;
	u32 seqno;
	int ret;

	seqno = readonly ? obj->last_write_seqno : obj->last_read_seqno;
	if (seqno == 0)
		return 0;

	ret = i915_wait_seqno(ring, seqno);
	if (ret)
		return ret;

	i915_gem_retire_requests_ring(ring);

	/* Manually manage the write flush as we may have not yet
	 * retired the buffer.
	 */
	if (obj->last_write_seqno &&
	    i915_seqno_passed(seqno, obj->last_write_seqno)) {
		obj->last_write_seqno = 0;
		obj->base.write_domain &= ~I915_GEM_GPU_DOMAINS;
	}

	return 0;
}

/* A nonblocking variant of the above wait. This is a highly dangerous routine
 * as the object state may change during this call.
 */
static int
i915_gem_object_wait_rendering__nonblocking(struct drm_i915_gem_object *obj,
					    bool readonly)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring = obj->ring;
	u32 seqno;
	int ret;

	rw_assert_wrlock(&dev->dev_lock);
	BUG_ON(!dev_priv->mm.interruptible);

	seqno = readonly ? obj->last_write_seqno : obj->last_read_seqno;
	if (seqno == 0)
		return 0;

	ret = i915_gem_check_wedge(dev_priv, true);
	if (ret)
		return ret;

	ret = i915_gem_check_olr(ring, seqno);
	if (ret)
		return ret;

	DRM_UNLOCK();
	ret = __wait_seqno(ring, seqno, true, NULL);
	DRM_LOCK();

	i915_gem_retire_requests_ring(ring);

	/* Manually manage the write flush as we may have not yet
	 * retired the buffer.
	 */
	if (obj->last_write_seqno &&
	    i915_seqno_passed(seqno, obj->last_write_seqno)) {
		obj->last_write_seqno = 0;
		obj->base.write_domain &= ~I915_GEM_GPU_DOMAINS;
	}

	return ret;
}

/**
 * Called when user space prepares to use an object with the CPU, either
 * through the mmap ioctl's mapping or a GTT mapping.
 */
int
i915_gem_set_domain_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct drm_i915_gem_set_domain *args = data;
	struct drm_i915_gem_object *obj;
	uint32_t read_domains = args->read_domains;
	uint32_t write_domain = args->write_domain;
	int ret;

	/* Only handle setting domains to types used by the CPU. */
	if (write_domain & I915_GEM_GPU_DOMAINS)
		return EINVAL;

	if (read_domains & I915_GEM_GPU_DOMAINS)
		return EINVAL;

	/* Having something in the write domain implies it's in the read
	 * domain, and only that read domain.  Enforce that in the request.
	 */
	if (write_domain != 0 && read_domains != write_domain)
		return EINVAL;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = ENOENT;
		goto unlock;
	}

	/* Try to flush the object off the GPU without holding the lock.
	 * We will repeat the flush holding the lock in the normal manner
	 * to catch cases where we are gazumped.
	 */
	ret = i915_gem_object_wait_rendering__nonblocking(obj, !write_domain);
	if (ret)
		goto unref;

	if (read_domains & I915_GEM_DOMAIN_GTT) {
		ret = i915_gem_object_set_to_gtt_domain(obj, write_domain != 0);

		/* Silently promote "you're not bound, there was nothing to do"
		 * to success, since the client was just asking us to
		 * make sure everything was done.
		 */
		if (ret == EINVAL)
			ret = 0;
	} else {
		ret = i915_gem_object_set_to_cpu_domain(obj, write_domain != 0);
	}

unref:
	drm_gem_object_unreference(&obj->base);
unlock:
	DRM_UNLOCK();
	return ret;
}

/**
 * Called when user space has done writes to this buffer
 */
int
i915_gem_sw_finish_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file)
{
	struct drm_i915_gem_sw_finish *args = data;
	struct drm_i915_gem_object *obj;
	int ret = 0;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = ENOENT;
		goto unlock;
	}

	/* Pinned buffers may be scanout, so flush the cache */
	if (obj->pin_count)
		i915_gem_object_flush_cpu_write_domain(obj);

	drm_gem_object_unreference(&obj->base);
unlock:
	DRM_UNLOCK();
	return ret;
}

/**
 * Maps the contents of an object, returning the address it is mapped
 * into.
 *
 * While the mapping holds a reference on the contents of the object, it doesn't
 * imply a ref on the object itself.
 */
int
i915_gem_mmap_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_mmap *args = data;
	struct drm_obj *obj;
	vaddr_t addr;
	voff_t offset;
	vsize_t end, nsize;
	int ret;

	obj = drm_gem_object_lookup(dev, file, args->handle);
	if (obj == NULL)
		return ENOENT;

	/* Since we are doing purely uvm-related operations here we do
	 * not need to hold the object, a reference alone is sufficient
	 */

	/* Check size. Also ensure that the object is not purgeable */
	if (args->size == 0 || args->offset > obj->size || args->size >
	    obj->size || (args->offset + args->size) > obj->size ||
	    i915_gem_object_is_purgeable(to_intel_bo(obj))) {
		ret = EINVAL;
		goto done;
	}

	end = round_page(args->offset + args->size);
	offset = trunc_page(args->offset);
	nsize = end - offset;

	/*
	 * We give our reference from object_lookup to the mmap, so only
	 * must free it in the case that the map fails.
	 */
	addr = 0;
	ret = uvm_map(&curproc->p_vmspace->vm_map, &addr, nsize, obj->uao,
	    offset, 0, UVM_MAPFLAG(UVM_PROT_RW, UVM_PROT_RW,
	    UVM_INH_SHARE, UVM_ADV_RANDOM, 0));
	if (ret == 0)
		uao_reference(obj->uao);

done:
	if (ret == 0)
		args->addr_ptr = (uint64_t) addr + (args->offset & PAGE_MASK);
	else
		drm_unref(&obj->uobj);

	return (ret);
}

int
i915_gem_fault(struct drm_obj *gem_obj, struct uvm_faultinfo *ufi,
    off_t offset, vaddr_t vaddr, vm_page_t *pps, int npages, int centeridx,
    vm_prot_t access_type, int flags)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem_obj);
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	paddr_t paddr;
	int lcv, ret;
	int write = !!(access_type & VM_PROT_WRITE);
	vm_prot_t mapprot;
	boolean_t locked = TRUE;

	dev_priv->entries++;

	KASSERT(obj->base.map);
	offset -= obj->base.map->ext;

	if (rw_enter(&dev->dev_lock, RW_NOSLEEP | RW_READ) != 0) {
		uvmfault_unlockall(ufi, NULL, &obj->base.uobj, NULL);
		DRM_READLOCK();
		locked = uvmfault_relock(ufi);
		if (locked)
			drm_lock_obj(&obj->base);
	}
	if (locked)
		drm_hold_object_locked(&obj->base);
	else { /* obj already unlocked */
		dev_priv->entries--;
		return (VM_PAGER_REFAULT);
	}

	/* we have a hold set on the object now, we can unlock so that we can
	 * sleep in binding and flushing.
	 */
	drm_unlock_obj(&obj->base);

	/* Now bind into the GTT if needed */
	if (!obj->map_and_fenceable) {
		ret = i915_gem_object_unbind(obj);
		if (ret)
			goto error;
	}

	if (obj->dmamap == NULL) {
		ret = i915_gem_object_bind_to_gtt(obj, 0, true, false);
		if (ret)
			goto error;

		ret = i915_gem_object_set_to_gtt_domain(obj, write);
		if (ret)
			goto error;
	}

	ret = i915_gem_object_get_fence(obj);
	if (ret)
		goto error;

	if (i915_gem_object_is_inactive(obj))
		list_move_tail(&obj->mm_list, &dev_priv->mm.inactive_list);

	obj->fault_mappable = true;

	mapprot = ufi->entry->protection;
	/*
	 * if it's only a read fault, we only put ourselves into the gtt
	 * read domain, so make sure we fault again and set ourselves to write.
	 * this prevents us needing userland to do domain management and get
	 * it wrong, and makes us fully coherent with the gpu re mmap.
	 */
	if (write == 0)
		mapprot &= ~VM_PROT_WRITE;
	/* XXX try and  be more efficient when we do this */
	for (lcv = 0 ; lcv < npages ; lcv++, offset += PAGE_SIZE,
	    vaddr += PAGE_SIZE) {
		if ((flags & PGO_ALLPAGES) == 0 && lcv != centeridx)
			continue;

		if (pps[lcv] == PGO_DONTCARE)
			continue;

		paddr = dev->agp->base + obj->gtt_offset + offset;

		if (pmap_enter(ufi->orig_map->pmap, vaddr, paddr,
		    mapprot, PMAP_CANFAIL | mapprot) != 0) {
			drm_unhold_object(&obj->base);
			uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap,
			    NULL, NULL);
			DRM_READUNLOCK();
			dev_priv->entries--;
			uvm_wait("intelflt");
			return (VM_PAGER_REFAULT);
		}
	}
error:
	drm_unhold_object(&obj->base);
	uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, NULL, NULL);
	DRM_READUNLOCK();
	dev_priv->entries--;
	pmap_update(ufi->orig_map->pmap);
	if (ret == EIO) {
		/*
		 * EIO means we're wedged, so upon resetting the gpu we'll
		 * be alright and can refault. XXX only on resettable chips.
		 */
		ret = VM_PAGER_REFAULT;
	} else if (ret) {
		ret = VM_PAGER_ERROR;
	} else {
		ret = VM_PAGER_OK;
	}
	return (ret);
}

/**
 * i915_gem_release_mmap - remove physical page mappings
 * @obj: obj in question
 *
 * Preserve the reservation of the mmapping with the DRM core code, but
 * relinquish ownership of the pages back to the system.
 *
 * It is vital that we remove the page mapping if we have mapped a tiled
 * object through the GTT and then lose the fence register due to
 * resource pressure. Similarly if the object has been moved out of the
 * aperture, than pages mapped into userspace must be revoked. Removing the
 * mapping will then trigger a page fault on the next user access, allowing
 * fixup by i915_gem_fault().
 */
void
i915_gem_release_mmap(struct drm_i915_gem_object *obj)
{
	struct inteldrm_softc *dev_priv = obj->base.dev->dev_private;
	struct vm_page *pg;

	if (!obj->fault_mappable)
		return;

	for (pg = &dev_priv->pgs[atop(obj->gtt_offset)];
	     pg != &dev_priv->pgs[atop(obj->gtt_offset + obj->base.size)];
	     pg++)
		pmap_page_protect(pg, VM_PROT_NONE);

	obj->fault_mappable = false;
}

uint32_t
i915_gem_get_gtt_size(struct drm_device *dev, uint32_t size, int tiling_mode)
{
	uint32_t gtt_size;

	if (INTEL_INFO(dev)->gen >= 4 ||
	    tiling_mode == I915_TILING_NONE)
		return size;

	/* Previous chips need a power-of-two fence region when tiling */
	if (INTEL_INFO(dev)->gen == 3)
		gtt_size = 1024*1024;
	else
		gtt_size = 512*1024;

	while (gtt_size < size)
		gtt_size <<= 1;

	return gtt_size;
}

/**
 * i915_gem_get_gtt_alignment - return required GTT alignment for an object
 * @obj: object to check
 *
 * Return the required GTT alignment for an object, taking into account
 * potential fence register mapping.
 */
uint32_t
i915_gem_get_gtt_alignment(struct drm_device *dev,
			   uint32_t size,
			   int tiling_mode)
{
	/*
	 * Minimum alignment is 4k (GTT page size), but might be greater
	 * if a fence register is needed for the object.
	 */
	if (INTEL_INFO(dev)->gen >= 4 ||
	    tiling_mode == I915_TILING_NONE)
		return 4096;

	/*
	 * Previous chips need to be aligned to the size of the smallest
	 * fence register that can contain the object.
	 */
	return i915_gem_get_gtt_size(dev, size, tiling_mode);
}

/**
 * i915_gem_get_unfenced_gtt_alignment - return required GTT alignment for an
 *					 unfenced object
 * @dev: the device
 * @size: size of the object
 * @tiling_mode: tiling mode of the object
 *
 * Return the required GTT alignment for an object, only taking into account
 * unfenced tiled surface requirements.
 */
uint32_t
i915_gem_get_unfenced_gtt_alignment(struct drm_device *dev,
				    uint32_t size,
				    int tiling_mode)
{
	/*
	 * Minimum alignment is 4k (GTT page size) for sane hw.
	 */
	if (INTEL_INFO(dev)->gen >= 4 || IS_G33(dev) ||
	    tiling_mode == I915_TILING_NONE)
		return 4096;

	/* Previous hardware however needs to be aligned to a power-of-two
	 * tile height. The simplest method for determining this is to reuse
	 * the power-of-tile object size.
	 */
	return i915_gem_get_gtt_size(dev, size, tiling_mode);
}

int
i915_gem_object_create_mmap_offset(struct drm_i915_gem_object *obj)
{
#if 0
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
#endif
	int ret;

	if (obj->base.map)
		return 0;

#if 0
	dev_priv->mm.shrinker_no_lock_stealing = true;
#endif

	ret = drm_gem_create_mmap_offset(&obj->base);
#if 0
	if (ret != -ENOSPC)
		goto out;

	/* Badly fragmented mmap space? The only way we can recover
	 * space is by destroying unwanted objects. We can't randomly release
	 * mmap_offsets as userspace expects them to be persistent for the
	 * lifetime of the objects. The closest we can is to release the
	 * offsets on purgeable objects by truncating it and marking it purged,
	 * which prevents userspace from ever using that object again.
	 */
	i915_gem_purge(dev_priv, obj->base.size >> PAGE_SHIFT);
	ret = drm_gem_create_mmap_offset(&obj->base);
	if (ret != -ENOSPC)
		goto out;

	i915_gem_shrink_all(dev_priv);
	ret = drm_gem_create_mmap_offset(&obj->base);
out:
	dev_priv->mm.shrinker_no_lock_stealing = false;
#endif

	return ret;
}

void
i915_gem_object_free_mmap_offset(struct drm_i915_gem_object *obj)
{
	if (!obj->base.map)
		return;

	drm_gem_free_mmap_offset(&obj->base);
}

int
i915_gem_mmap_gtt(struct drm_file *file,
		  struct drm_device *dev,
		  uint32_t handle,
		  uint64_t *offset)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->base.size > dev_priv->mm.gtt_mappable_end) {
		ret = -E2BIG;
		goto out;
	}

	if (obj->madv != I915_MADV_WILLNEED) {
		DRM_ERROR("Attempting to mmap a purgeable buffer\n");
		ret = -EINVAL;
		goto out;
	}

	ret = i915_gem_object_create_mmap_offset(obj);
	if (ret)
		goto out;

	*offset = (u64)obj->base.map->ext;

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	DRM_UNLOCK();
	return ret;
}

/**
 * i915_gem_mmap_gtt_ioctl - prepare an object for GTT mmap'ing
 * @dev: DRM device
 * @data: GTT mapping ioctl data
 * @file: GEM object info
 *
 * Simply returns the fake offset to userspace so it can mmap it.
 * The mmap call will end up in drm_gem_mmap(), which will set things
 * up so we can get faults in the handler above.
 *
 * The fault handler will take care of binding the object into the GTT
 * (since it may have been evicted to make room for something), allocating
 * a fence register, and mapping the appropriate aperture address into
 * userspace.
 */
int
i915_gem_mmap_gtt_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file)
{
	struct drm_i915_gem_mmap_gtt *args = data;

	return i915_gem_mmap_gtt(file, dev, args->handle, &args->offset);
}

/* Immediately discard the backing storage */
void
i915_gem_object_truncate(struct drm_i915_gem_object *obj)
{
	DRM_ASSERT_HELD(&obj->base);

	i915_gem_object_free_mmap_offset(obj);

	simple_lock(&obj->base.uao->vmobjlock);
	obj->base.uao->pgops->pgo_flush(obj->base.uao, 0, obj->base.size,
	    PGO_ALLPAGES | PGO_FREE);
	simple_unlock(&obj->base.uao->vmobjlock);

	obj->madv = __I915_MADV_PURGED;
}

// i915_gem_object_is_purgeable
// i915_gem_object_put_pages
// __i915_gem_shrink
// i915_gem_purge
// i915_gem_shrink_all
// i915_gem_object_get_pages

int
i915_gem_object_get_pages_gtt(struct drm_i915_gem_object *obj)
{
#if 0
	int page_count, i;
	struct address_space *mapping;
	struct inode *inode;
	struct page *page;

	/* Get the list of pages out of our struct file.  They'll be pinned
	 * at this point until we release them.
	 */
	page_count = obj->base.size / PAGE_SIZE;
	BUG_ON(obj->pages != NULL);
	obj->pages = drm_malloc_ab(page_count, sizeof(struct page *));
	if (obj->pages == NULL)
		return -ENOMEM;

	inode = obj->base.filp->f_path.dentry->d_inode;
	mapping = inode->i_mapping;
	gfpmask |= mapping_gfp_mask(mapping);

	for (i = 0; i < page_count; i++) {
		page = shmem_read_mapping_page_gfp(mapping, i, gfpmask);
		if (IS_ERR(page))
			goto err_pages;

		obj->pages[i] = page;
	}
#endif

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_do_bit_17_swizzle(obj);

	return 0;

#if 0
err_pages:
	while (i--)
		page_cache_release(obj->pages[i]);

	drm_free_large(obj->pages);
	obj->pages = NULL;
	return PTR_ERR(page);
#endif
}

void
i915_gem_object_put_pages_gtt(struct drm_i915_gem_object *obj)
{
#if 0
	int page_count = obj->base.size / PAGE_SIZE;
	int i;
#endif

	BUG_ON(obj->madv == __I915_MADV_PURGED);

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_save_bit_17_swizzle(obj);

	if (obj->madv == I915_MADV_DONTNEED)
		obj->dirty = 0;

#if 0
	for (i = 0; i < page_count; i++) {
		if (obj->dirty)
			set_page_dirty(obj->pages[i]);

		if (obj->madv == I915_MADV_WILLNEED)
			mark_page_accessed(obj->pages[i]);

		page_cache_release(obj->pages[i]);
	}
#endif
	obj->dirty = 0;

#if 0
	drm_free_large(obj->pages);
	obj->pages = NULL;
#endif
}

void
i915_gem_object_move_to_active(struct drm_i915_gem_object *obj,
			       struct intel_ring_buffer *ring)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 seqno = intel_ring_get_seqno(ring);

	BUG_ON(ring == NULL);
	obj->ring = ring;

	/* Add a reference if we're newly entering the active list. */
	if (!obj->active) {
		drm_gem_object_reference(&obj->base);
		obj->active = 1;
	}

	/* Move from whatever list we were on to the tail of execution. */
	list_move_tail(&obj->mm_list, &dev_priv->mm.active_list);
	list_move_tail(&obj->ring_list, &ring->active_list);

	obj->last_read_seqno = seqno;

	if (obj->fenced_gpu_access) {
		obj->last_fenced_seqno = seqno;

		/* Bump MRU to take account of the delayed flush */
		if (obj->fence_reg != I915_FENCE_REG_NONE) {
			struct drm_i915_fence_reg *reg;

			reg = &dev_priv->fence_regs[obj->fence_reg];
			list_move_tail(&reg->lru_list,
				       &dev_priv->mm.fence_list);
		}
	}
}

void
i915_gem_object_move_to_inactive(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	BUG_ON(obj->base.write_domain & ~I915_GEM_GPU_DOMAINS);
	BUG_ON(!obj->active);

	if (obj->pin_count != 0)
		list_del_init(&obj->mm_list);
	else
		list_move_tail(&obj->mm_list, &dev_priv->mm.inactive_list);

	list_del_init(&obj->ring_list);
	obj->ring = NULL;

	obj->last_read_seqno = 0;
	obj->last_write_seqno = 0;
	obj->base.write_domain = 0;

	obj->last_fenced_seqno = 0;
	obj->fenced_gpu_access = false;

	obj->active = 0;
	drm_gem_object_unreference(&obj->base);

	WARN_ON(i915_verify_lists(dev));
}

int
i915_gem_handle_seqno_wrap(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	int ret, i, j;

	/* The hardware uses various monotonic 32-bit counters, if we
	 * detect that they will wraparound we need to idle the GPU
	 * and reset those counters.
	 */
	ret = 0;
	for_each_ring(ring, dev_priv, i) {
		for (j = 0; j < nitems(ring->sync_seqno); j++)
			ret |= ring->sync_seqno[j] != 0;
	}
	if (ret == 0)
		return ret;

	ret = i915_gpu_idle(dev);
	if (ret)
		return ret;

	i915_gem_retire_requests(dev);
	for_each_ring(ring, dev_priv, i) {
		for (j = 0; j < nitems(ring->sync_seqno); j++)
			ring->sync_seqno[j] = 0;
	}

	return 0;
}

int
i915_gem_get_seqno(struct drm_device *dev, u32 *seqno)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* reserve 0 for non-seqno */
	if (dev_priv->next_seqno == 0) {
		int ret = i915_gem_handle_seqno_wrap(dev);
		if (ret)
			return ret;

		dev_priv->next_seqno = 1;
	}

	*seqno = dev_priv->next_seqno++;
	return 0;
}

int
i915_add_request(struct intel_ring_buffer *ring,
		 struct drm_file *file,
		 u32 *out_seqno)
{
	drm_i915_private_t *dev_priv = ring->dev->dev_private;
	struct drm_i915_gem_request *request;
	u32 request_ring_position;
	int was_empty;
	int ret;

	/*
	 * Emit any outstanding flushes - execbuf can fail to emit the flush
	 * after having emitted the batchbuffer command. Hence we need to fix
	 * things up similar to emitting the lazy request. The difference here
	 * is that the flush _must_ happen before the next request, no matter
	 * what.
	 */
	ret = intel_ring_flush_all_caches(ring);
	if (ret)
		return ret;

	request = drm_alloc(sizeof(*request));
	if (request == NULL)
		return -ENOMEM;


	/* Record the position of the start of the request so that
	 * should we detect the updated seqno part-way through the
	 * GPU processing the request, we never over-estimate the
	 * position of the head.
	 */
	request_ring_position = intel_ring_get_tail(ring);

	ret = ring->add_request(ring);
	if (ret) {
		drm_free(request);
		return ret;
	}

	request->seqno = intel_ring_get_seqno(ring);
	request->ring = ring;
	request->tail = request_ring_position;
	request->emitted_ticks = ticks;
	was_empty = list_empty(&ring->request_list);
	list_add_tail(&request->list, &ring->request_list);
	request->file_priv = NULL;

	if (file) {
		struct drm_i915_file_private *file_priv = file->driver_priv;

		mtx_enter(&file_priv->mm.lock);
		request->file_priv = file_priv;
		list_add_tail(&request->client_list,
			      &file_priv->mm.request_list);
		mtx_leave(&file_priv->mm.lock);
	}

	ring->outstanding_lazy_request = 0;

	if (!dev_priv->mm.suspended) {
		if (i915_enable_hangcheck) {
			timeout_add_msec(&dev_priv->hangcheck_timer,
			    DRM_I915_HANGCHECK_PERIOD);
		}
		if (was_empty) {
			timeout_add_sec(&dev_priv->mm.retire_timer, 1);
			intel_mark_busy(ring->dev);
		}
	}

	if (out_seqno)
		*out_seqno = request->seqno;
	return 0;
}

void
i915_gem_request_remove_from_client(struct drm_i915_gem_request *request)
{
	struct drm_i915_file_private *file_priv = request->file_priv;

	if (!file_priv)
		return;

	mtx_enter(&file_priv->mm.lock);
	if (request->file_priv) {
		list_del(&request->client_list);
		request->file_priv = NULL;
	}
	mtx_leave(&file_priv->mm.lock);
}

void
i915_gem_reset_ring_lists(struct drm_i915_private *dev_priv,
				      struct intel_ring_buffer *ring)
{
	while (!list_empty(&ring->request_list)) {
		struct drm_i915_gem_request *request;

		request = list_first_entry(&ring->request_list,
					   struct drm_i915_gem_request,
					   list);

		list_del(&request->list);
		i915_gem_request_remove_from_client(request);
		free(request, M_DRM);
	}

	while (!list_empty(&ring->active_list)) {
		struct drm_i915_gem_object *obj;

		obj = list_first_entry(&ring->active_list,
				       struct drm_i915_gem_object,
				       ring_list);

		i915_gem_object_move_to_inactive(obj);
	}
}

void
i915_gem_reset_fences(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	for (i = 0; i < dev_priv->num_fence_regs; i++) {
		struct drm_i915_fence_reg *reg = &dev_priv->fence_regs[i];

		i915_gem_write_fence(dev, i, NULL);

		if (reg->obj)
			i915_gem_object_fence_lost(reg->obj);

		reg->pin_count = 0;
		reg->obj = NULL;
		INIT_LIST_HEAD(&reg->lru_list);
	}

	INIT_LIST_HEAD(&dev_priv->mm.fence_list);
}

void
i915_gem_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	struct intel_ring_buffer *ring;
	int i;

	for_each_ring(ring, dev_priv, i)
		i915_gem_reset_ring_lists(dev_priv, ring);

	/* Move everything out of the GPU domains to ensure we do any
	 * necessary invalidation upon reuse.
	 */
	list_for_each_entry(obj,
			    &dev_priv->mm.inactive_list,
			    mm_list)
	{
		obj->base.read_domains &= ~I915_GEM_GPU_DOMAINS;
	}

	/* The fence registers are invalidated so clear them out */
	i915_gem_reset_fences(dev);
}

/**
 * This function clears the request list as sequence numbers are passed.
 */
void
i915_gem_retire_requests_ring(struct intel_ring_buffer *ring)
{
	uint32_t seqno;

	if (list_empty(&ring->request_list))
		return;

	seqno = ring->get_seqno(ring, true);

	while (!list_empty(&ring->request_list)) {
		struct drm_i915_gem_request *request;

		request = list_first_entry(&ring->request_list,
					   struct drm_i915_gem_request,
					   list);

		if (!i915_seqno_passed(seqno, request->seqno))
			break;

//		trace_i915_gem_request_retire(ring, request->seqno);
		/* We know the GPU must have read the request to have
		 * sent us the seqno + interrupt, so use the position
		 * of tail of the request to update the last known position
		 * of the GPU head.
		 */
		ring->last_retired_head = request->tail;

		list_del(&request->list);
		i915_gem_request_remove_from_client(request);
		drm_free(request);
	}

	/* Move any buffers on the active list that are no longer referenced
	 * by the ringbuffer to the flushing/inactive lists as appropriate.
	 */
	while (!list_empty(&ring->active_list)) {
		struct drm_i915_gem_object *obj;

		obj = list_first_entry(&ring->active_list,
				      struct drm_i915_gem_object,
				      ring_list);

		if (!i915_seqno_passed(seqno, obj->last_read_seqno))
			break;

		i915_gem_object_move_to_inactive(obj);
	}
}

void
i915_gem_retire_requests(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	int i;

	for_each_ring(ring, dev_priv, i)
		i915_gem_retire_requests_ring(ring);
}

void
i915_gem_retire_work_handler(void *arg1, void *unused)
{
	drm_i915_private_t *dev_priv = arg1;
	struct drm_device *dev;
	struct intel_ring_buffer *ring;
	bool idle;
	int i;

	dev = (struct drm_device *)dev_priv->drmdev;

	/* Come back later if the device is busy... */
	if (rw_enter(&dev->dev_lock, RW_NOSLEEP | RW_WRITE)) {
		timeout_add_sec(&dev_priv->mm.retire_timer, 1);
		return;
	}

	i915_gem_retire_requests(dev);

	/* Send a periodic flush down the ring so we don't hold onto GEM
	 * objects indefinitely.
	 */
	idle = true;
	for_each_ring(ring, dev_priv, i) {
		if (ring->gpu_caches_dirty)
			i915_add_request(ring, NULL, NULL);

		idle &= list_empty(&ring->request_list);
	}

	if (!dev_priv->mm.suspended && !idle)
		timeout_add_sec(&dev_priv->mm.retire_timer, 1);
	if (idle)
		intel_mark_idle(dev);

	DRM_UNLOCK();
}

/**
 * Ensures that an object will eventually get non-busy by flushing any required
 * write domains, emitting any outstanding lazy request and retiring and
 * completed requests.
 */
int
i915_gem_object_flush_active(struct drm_i915_gem_object *obj)
{
	int ret;

	if (obj->active) {
		ret = i915_gem_check_olr(obj->ring, obj->last_read_seqno);
		if (ret)
			return ret;

		i915_gem_retire_requests_ring(obj->ring);
	}

	return 0;
}

// i915_gem_wait_ioctl

/**
 * i915_gem_object_sync - sync an object to a ring.
 *
 * @obj: object which may be in use on another ring.
 * @to: ring we wish to use the object on. May be NULL.
 *
 * This code is meant to abstract object synchronization with the GPU.
 * Calling with NULL implies synchronizing the object with the CPU
 * rather than a particular GPU ring.
 *
 * Returns 0 if successful, else propagates up the lower layer error.
 */
int
i915_gem_object_sync(struct drm_i915_gem_object *obj,
		     struct intel_ring_buffer *to)
{
	struct intel_ring_buffer *from = obj->ring;
	u32 seqno;
	int ret, idx;

	if (from == NULL || to == from)
		return 0;

	if (to == NULL || !i915_semaphore_is_enabled(obj->base.dev))
		return i915_gem_object_wait_rendering(obj, false);

	idx = intel_ring_sync_index(from, to);

	seqno = obj->last_read_seqno;
	if (seqno <= from->sync_seqno[idx])
		return 0;

	ret = i915_gem_check_olr(obj->ring, seqno);
	if (ret)
		return ret;

	ret = to->sync_to(to, from, seqno);
	if (!ret)
		/* We use last_read_seqno because sync_to()
		 * might have just caused seqno wrap under
		 * the radar.
		 */
		from->sync_seqno[idx] = obj->last_read_seqno;

	return ret;
}

void
i915_gem_object_finish_gtt(struct drm_i915_gem_object *obj)
{
	u32 old_write_domain, old_read_domains;

	/* Act a barrier for all accesses through the GTT */
	DRM_MEMORYBARRIER();

	/* Force a pagefault for domain tracking on next user access */
	i915_gem_release_mmap(obj);

	if ((obj->base.read_domains & I915_GEM_DOMAIN_GTT) == 0)
		return;

	old_read_domains = obj->base.read_domains;
	old_write_domain = obj->base.write_domain;

	obj->base.read_domains &= ~I915_GEM_DOMAIN_GTT;
	obj->base.write_domain &= ~I915_GEM_DOMAIN_GTT;

#if 0
	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    old_write_domain);
#endif
}

/**
 * Unbinds an object from the GTT aperture.
 *
 * XXX track dirty and pass down to uvm (note, DONTNEED buffers are clean).
 */
int
i915_gem_object_unbind(struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = obj->base.dev->dev_private;
	struct drm_device *dev = obj->base.dev;
	int ret = 0;

	DRM_ASSERT_HELD(&obj->base);
	/*
	 * if it's already unbound, or we've already done lastclose, just
	 * let it happen. XXX does this fail to unwire?
	 */
	if (obj->dmamap == NULL || dev_priv->agpdmat == NULL)
		return 0;

	if (obj->pin_count)
		return EBUSY;

	ret = i915_gem_object_finish_gpu(obj);
	if (ret == ERESTART || ret == EINTR)
		return ret;
	/* Continue on if we fail due to EIO, the GPU is hung so we
	 * should be safe and we need to cleanup or else we might
	 * cause memory corruption through use-after-free.
	 */

	i915_gem_object_finish_gtt(obj);

	/* release the fence reg _after_ flushing */
	ret = i915_gem_object_put_fence(obj);
	if (ret == ERESTART || ret == EINTR)
		return ret;

	i915_gem_object_put_pages_gtt(obj);

	/*
	 * unload the map, then unwire the backing object.
	 */
	bus_dmamap_unload(dev_priv->agpdmat, obj->dmamap);
	uvm_objunwire(obj->base.uao, 0, obj->base.size);
	/* XXX persistent dmamap worth the memory? */
	bus_dmamap_destroy(dev_priv->agpdmat, obj->dmamap);
	obj->dmamap = NULL;
	free(obj->dma_segs, M_DRM);
	obj->dma_segs = NULL;

	list_del_init(&obj->gtt_list);
	list_del_init(&obj->mm_list);
	/* Avoid an unnecessary call to unbind on rebind. */
	obj->map_and_fenceable = true;

	obj->gtt_offset = 0;
	atomic_dec(&dev->gtt_count);
	atomic_sub(obj->base.size, &dev->gtt_memory);

	if (i915_gem_object_is_purgeable(obj))
		i915_gem_object_truncate(obj);

	return ret;
}

int
i915_gpu_idle(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	int ret, i;

	/* Flush everything onto the inactive list. */
	for_each_ring(ring, dev_priv, i) {
#ifdef notyet
		ret = i915_switch_context(ring, NULL, DEFAULT_CONTEXT_ID);
		if (ret)
			return ret;
#endif

		ret = intel_ring_idle(ring);
		if (ret)
			return ret;
	}

	return 0;
}

void
sandybridge_write_fence_reg(struct drm_device *dev, int reg,
					struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint64_t val;

	if (obj) {
		u32 size = obj->dmamap->dm_segs[0].ds_len;

		val = (uint64_t)((obj->gtt_offset + size - 4096) &
				 0xfffff000) << 32;
		val |= obj->gtt_offset & 0xfffff000;
		val |= (uint64_t)((obj->stride / 128) - 1) <<
			SANDYBRIDGE_FENCE_PITCH_SHIFT;

		if (obj->tiling_mode == I915_TILING_Y)
			val |= 1 << I965_FENCE_TILING_Y_SHIFT;
		val |= I965_FENCE_REG_VALID;
	} else
		val = 0;

	I915_WRITE64(FENCE_REG_SANDYBRIDGE_0 + reg * 8, val);
	POSTING_READ(FENCE_REG_SANDYBRIDGE_0 + reg * 8);
}

void
i965_write_fence_reg(struct drm_device *dev, int reg,
				 struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint64_t val;

	if (obj) {
		u32 size = obj->dmamap->dm_segs[0].ds_len;

		val = (uint64_t)((obj->gtt_offset + size - 4096) &
				 0xfffff000) << 32;
		val |= obj->gtt_offset & 0xfffff000;
		val |= ((obj->stride / 128) - 1) << I965_FENCE_PITCH_SHIFT;
		if (obj->tiling_mode == I915_TILING_Y)
			val |= 1 << I965_FENCE_TILING_Y_SHIFT;
		val |= I965_FENCE_REG_VALID;
	} else
		val = 0;

	I915_WRITE64(FENCE_REG_965_0 + reg * 8, val);
	POSTING_READ(FENCE_REG_965_0 + reg * 8);
}

void
i915_write_fence_reg(struct drm_device *dev, int reg,
				 struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 val;

	if (obj) {
		u32 size = obj->dmamap->dm_segs[0].ds_len;
		int pitch_val;
		int tile_width;

		WARN((obj->gtt_offset & ~I915_FENCE_START_MASK) ||
		     (size & -size) != size ||
		     (obj->gtt_offset & (size - 1)),
		     "object 0x%08x [fenceable? %d] not 1M or pot-size (0x%08x) aligned\n",
		     obj->gtt_offset, obj->map_and_fenceable, size);

		if (obj->tiling_mode == I915_TILING_Y && HAS_128_BYTE_Y_TILING(dev))
			tile_width = 128;
		else
			tile_width = 512;

		/* Note: pitch better be a power of two tile widths */
		pitch_val = obj->stride / tile_width;
		pitch_val = ffs(pitch_val) - 1;

		val = obj->gtt_offset;
		if (obj->tiling_mode == I915_TILING_Y)
			val |= 1 << I830_FENCE_TILING_Y_SHIFT;
		val |= I915_FENCE_SIZE_BITS(size);
		val |= pitch_val << I830_FENCE_PITCH_SHIFT;
		val |= I830_FENCE_REG_VALID;
	} else
		val = 0;

	if (reg < 8)
		reg = FENCE_REG_830_0 + reg * 4;
	else
		reg = FENCE_REG_945_8 + (reg - 8) * 4;

	I915_WRITE(reg, val);
	POSTING_READ(reg);
}

void
i830_write_fence_reg(struct drm_device *dev, int reg,
				struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t val;

	if (obj) {
		u32 size = obj->dmamap->dm_segs[0].ds_len;
		uint32_t pitch_val;

		WARN((obj->gtt_offset & ~I830_FENCE_START_MASK) ||
		     (size & -size) != size ||
		     (obj->gtt_offset & (size - 1)),
		     "object 0x%08x not 512K or pot-size 0x%08x aligned\n",
		     obj->gtt_offset, size);

		pitch_val = obj->stride / 128;
		pitch_val = ffs(pitch_val) - 1;

		val = obj->gtt_offset;
		if (obj->tiling_mode == I915_TILING_Y)
			val |= 1 << I830_FENCE_TILING_Y_SHIFT;
		val |= I830_FENCE_SIZE_BITS(size);
		val |= pitch_val << I830_FENCE_PITCH_SHIFT;
		val |= I830_FENCE_REG_VALID;
	} else
		val = 0;

	I915_WRITE(FENCE_REG_830_0 + reg * 4, val);
	POSTING_READ(FENCE_REG_830_0 + reg * 4);
}

void
i915_gem_write_fence(struct drm_device *dev, int reg,
				 struct drm_i915_gem_object *obj)
{
	switch (INTEL_INFO(dev)->gen) {
	case 7:
	case 6: sandybridge_write_fence_reg(dev, reg, obj); break;
	case 5:
	case 4: i965_write_fence_reg(dev, reg, obj); break;
	case 3: i915_write_fence_reg(dev, reg, obj); break;
	case 2: i830_write_fence_reg(dev, reg, obj); break;
	default: break;
	}
}

static inline int
fence_number(struct drm_i915_private *dev_priv,
			       struct drm_i915_fence_reg *fence)
{
	return fence - dev_priv->fence_regs;
}

#ifdef __linux__
void
i915_gem_write_fence__ipi(void *data)
{
	wbinvd();
}
#endif

void
i915_gem_object_update_fence(struct drm_i915_gem_object *obj,
					 struct drm_i915_fence_reg *fence,
					 bool enable)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int fence_reg = fence_number(dev_priv, fence);

	/* In order to fully serialize access to the fenced region and
	 * the update to the fence register we need to take extreme
	 * measures on SNB+. In theory, the write to the fence register
	 * flushes all memory transactions before, and coupled with the
	 * mb() placed around the register write we serialise all memory
	 * operations with respect to the changes in the tiler. Yet, on
	 * SNB+ we need to take a step further and emit an explicit wbinvd()
	 * on each processor in order to manually flush all memory
	 * transactions before updating the fence register.
	 */
	if (HAS_LLC(obj->base.dev))
#ifdef __linux__
		on_each_cpu(i915_gem_write_fence__ipi, NULL, 1);
#else
		wbinvd();
#endif
	i915_gem_write_fence(dev, fence_reg, enable ? obj : NULL);

	if (enable) {
		obj->fence_reg = fence_reg;
		fence->obj = obj;
		list_move_tail(&fence->lru_list, &dev_priv->mm.fence_list);
	} else {
		obj->fence_reg = I915_FENCE_REG_NONE;
		fence->obj = NULL;
		list_del_init(&fence->lru_list);
	}
}

int
i915_gem_object_flush_fence(struct drm_i915_gem_object *obj)
{
	if (obj->last_fenced_seqno) {
		int ret = i915_wait_seqno(obj->ring, obj->last_fenced_seqno);
		if (ret)
			return ret;

		obj->last_fenced_seqno = 0;
	}

	/* Ensure that all CPU reads are completed before installing a fence
	 * and all writes before removing the fence.
	 */
	if (obj->base.read_domains & I915_GEM_DOMAIN_GTT)
		DRM_WRITEMEMORYBARRIER();

	obj->fenced_gpu_access = false;
	return 0;
}

int
i915_gem_object_put_fence(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	int ret;

	ret = i915_gem_object_flush_fence(obj);
	if (ret)
		return ret;

	if (obj->fence_reg == I915_FENCE_REG_NONE)
		return 0;

	i915_gem_object_update_fence(obj,
				     &dev_priv->fence_regs[obj->fence_reg],
				     false);
	i915_gem_object_fence_lost(obj);

	return 0;
}

struct drm_i915_fence_reg *
i915_find_fence_reg(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_fence_reg *reg, *avail;
	int i;

	/* First try to find a free reg */
	avail = NULL;
	for (i = dev_priv->fence_reg_start; i < dev_priv->num_fence_regs; i++) {
		reg = &dev_priv->fence_regs[i];
		if (!reg->obj)
			return reg;

		if (!reg->pin_count)
			avail = reg;
	}

	if (avail == NULL)
		return NULL;

	/* None available, try to steal one or wait for a user to finish */
	list_for_each_entry(reg, &dev_priv->mm.fence_list, lru_list) {
		if (reg->pin_count)
			continue;

		return reg;
	}

	return NULL;
}

/**
 * i915_gem_object_get_fence - set up fencing for an object
 * @obj: object to map through a fence reg
 *
 * When mapping objects through the GTT, userspace wants to be able to write
 * to them without having to worry about swizzling if the object is tiled.
 * This function walks the fence regs looking for a free one for @obj,
 * stealing one if it can't find any.
 *
 * It then sets up the reg based on the object's properties: address, pitch
 * and tiling format.
 *
 * For an untiled surface, this removes any existing fence.
 */
int
i915_gem_object_get_fence(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool enable = obj->tiling_mode != I915_TILING_NONE;
	struct drm_i915_fence_reg *reg;
	int ret;

	/* Have we updated the tiling parameters upon the object and so
	 * will need to serialise the write to the associated fence register?
	 */
	if (obj->fence_dirty) {
		ret = i915_gem_object_flush_fence(obj);
		if (ret)
			return ret;
	}

	/* Just update our place in the LRU if our fence is getting reused. */
	if (obj->fence_reg != I915_FENCE_REG_NONE) {
		reg = &dev_priv->fence_regs[obj->fence_reg];
		if (!obj->fence_dirty) {
			list_move_tail(&reg->lru_list,
				       &dev_priv->mm.fence_list);
			return 0;
		}
	} else if (enable) {
		reg = i915_find_fence_reg(dev);
		if (reg == NULL)
			return -EDEADLK;

		if (reg->obj) {
			struct drm_i915_gem_object *old = reg->obj;

			ret = i915_gem_object_flush_fence(old);
			if (ret)
				return ret;

			i915_gem_object_fence_lost(old);
		}
	} else
		return 0;

	i915_gem_object_update_fence(obj, reg, enable);
	obj->fence_dirty = false;

	return 0;
}

// i915_gem_valid_gtt_space
// i915_gem_verify_gtt

/**
 * Finds free space in the GTT aperture and binds the object there.
 */
int
i915_gem_object_bind_to_gtt(struct drm_i915_gem_object *obj,
			    unsigned alignment,
			    bool map_and_fenceable,
			    bool nonblocking)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 size, fence_size, fence_alignment, unfenced_alignment;
	bool mappable, fenceable;
	int ret;
	int flags;

	DRM_ASSERT_HELD(&obj->base);

	if (obj->madv != I915_MADV_WILLNEED) {
		DRM_ERROR("Attempting to bind a purgeable object\n");
		return EINVAL;
	}

	fence_size = i915_gem_get_gtt_size(dev,
					   obj->base.size,
					   obj->tiling_mode);
	fence_alignment = i915_gem_get_gtt_alignment(dev,
						     obj->base.size,
						     obj->tiling_mode);
	unfenced_alignment =
		i915_gem_get_unfenced_gtt_alignment(dev,
						    obj->base.size,
						    obj->tiling_mode);

	if (alignment == 0)
		alignment = map_and_fenceable ? fence_alignment :
						unfenced_alignment;
	if (map_and_fenceable && alignment & (fence_alignment - 1)) {
		DRM_ERROR("Invalid object alignment requested %u\n", alignment);
		return EINVAL;
	}

	size = map_and_fenceable ? fence_size : obj->base.size;

	/* If the object is bigger than the entire aperture, reject it early
	 * before evicting everything in a vain attempt to find space.
	 */
	if (obj->base.size >
	    (map_and_fenceable ? dev_priv->mm.gtt_mappable_end : dev_priv->mm.gtt_total)) {
		DRM_ERROR("Attempting to bind an object larger than the aperture\n");
		return -E2BIG;
	}

	if ((ret = bus_dmamap_create(dev_priv->agpdmat, size, 1,
	    size, 0, BUS_DMA_WAITOK, &obj->dmamap)) != 0) {
		DRM_ERROR("Failed to create dmamap\n");
		return (ret);
	}
	agp_bus_dma_set_alignment(dev_priv->agpdmat, obj->dmamap,
	    alignment);

 search_free:
	switch (obj->cache_level) {
	case I915_CACHE_NONE:
		flags = BUS_DMA_GTT_NOCACHE;
		break;
	case I915_CACHE_LLC:
		flags = BUS_DMA_GTT_CACHE_LLC;
		break;
	case I915_CACHE_LLC_MLC:
		flags = BUS_DMA_GTT_CACHE_LLC_MLC;
		break;
	default:
		BUG();
	}
	/*
	 * the helper function wires the uao then binds it to the aperture for
	 * us, so all we have to do is set up the dmamap then load it.
	 */
	ret = drm_gem_load_uao(dev_priv->agpdmat, obj->dmamap, obj->base.uao,
	    obj->base.size, BUS_DMA_WAITOK | obj->dma_flags | flags,
	    &obj->dma_segs);
	/* XXX NOWAIT? */
	if (ret != 0) {
		/* If the gtt is empty and we're still having trouble
		 * fitting our object in, we're out of memory.
		 */
		if (list_empty(&dev_priv->mm.inactive_list) &&
		    list_empty(&dev_priv->mm.active_list)) {
			DRM_ERROR("GTT full, but LRU list empty\n");
			goto error;
		}

		ret = i915_gem_evict_something(dev_priv, obj->base.size);
		if (ret != 0)
			goto error;
		goto search_free;
	}

	i915_gem_object_get_pages_gtt(obj);

	list_move_tail(&obj->gtt_list, &dev_priv->mm.bound_list);

	/* Assert that the object is not currently in any GPU domain. As it
	 * wasn't in the GTT, there shouldn't be any way it could have been in
	 * a GPU cache
	 */
	BUG_ON(obj->base.read_domains & I915_GEM_GPU_DOMAINS);
	BUG_ON(obj->base.write_domain & I915_GEM_GPU_DOMAINS);

	obj->gtt_offset = obj->dmamap->dm_segs[0].ds_addr - dev->agp->base;

	fenceable =
		obj->dmamap->dm_segs[0].ds_len == fence_size &&
		(obj->dmamap->dm_segs[0].ds_addr & (fence_alignment - 1)) == 0;

	mappable =
		obj->gtt_offset + obj->base.size <= dev_priv->mm.gtt_mappable_end;

	obj->map_and_fenceable = mappable && fenceable;

	atomic_inc(&dev->gtt_count);
	atomic_add(obj->base.size, &dev->gtt_memory);

	return (0);

error:
	bus_dmamap_destroy(dev_priv->agpdmat, obj->dmamap);
	obj->dmamap = NULL;
	obj->gtt_offset = 0;
	return (ret);
}

void
i915_gem_clflush_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;

	/* If we don't have a page list set up, then we're not pinned
	 * to GPU, and we can ignore the cache flush because it'll happen
	 * again at bind time.
	 */
	if (obj->dmamap == NULL)
		return;

	/* If the GPU is snooping the contents of the CPU cache,
	 * we do not need to manually clear the CPU cache lines.  However,
	 * the caches are only snooped when the render cache is
	 * flushed/invalidated.  As we always have to emit invalidations
	 * and flushes when moving into and out of the RENDER domain, correct
	 * snooping behaviour occurs naturally as the result of our domain
	 * tracking.
	 */
	if (obj->cache_level != I915_CACHE_NONE)
		return;

	bus_dmamap_sync(dev_priv->agpdmat, obj->dmamap, 0,
	    obj->base.size, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
}

/** Flushes the GTT write domain for the object if it's dirty. */
void
i915_gem_object_flush_gtt_write_domain(struct drm_i915_gem_object *obj)
{
	uint32_t old_write_domain;

	if (obj->base.write_domain != I915_GEM_DOMAIN_GTT)
		return;

	/* No actual flushing is required for the GTT write domain.  Writes
	 * to it immediately go to main memory as far as we know, so there's
	 * no chipset flush.  It also doesn't land in render cache.
	 *
	 * However, we do have to enforce the order so that all writes through
	 * the GTT land before any writes to the device, such as updates to
	 * the GATT itself.
	 */
	DRM_WRITEMEMORYBARRIER();

	old_write_domain = obj->base.write_domain;
	obj->base.write_domain = 0;

#if 0
	trace_i915_gem_object_change_domain(obj,
					    obj->base.read_domains,
					    old_write_domain);
#endif
}

/** Flushes the CPU write domain for the object if it's dirty. */
void
i915_gem_object_flush_cpu_write_domain(struct drm_i915_gem_object *obj)
{
	uint32_t old_write_domain;

	if (obj->base.write_domain != I915_GEM_DOMAIN_CPU)
		return;

	i915_gem_clflush_object(obj);
	i915_gem_chipset_flush(obj->base.dev);
	old_write_domain = obj->base.write_domain;
	obj->base.write_domain = 0;

#if 0
	trace_i915_gem_object_change_domain(obj,
					    obj->base.read_domains,
					    old_write_domain);
#endif
}

/**
 * Moves a single object to the GTT read, and possibly write domain.
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
int
i915_gem_object_set_to_gtt_domain(struct drm_i915_gem_object *obj, bool write)
{
	drm_i915_private_t *dev_priv = obj->base.dev->dev_private;
//	uint32_t old_write_domain, old_read_domains;
	int ret;

	DRM_ASSERT_HELD(&obj->base);

	/* Not valid to be called on unbound objects. */
	if (obj->dmamap == NULL)
		return (EINVAL);

	if (obj->base.write_domain == I915_GEM_DOMAIN_GTT)
		return 0;

	ret = i915_gem_object_wait_rendering(obj, !write);
	if (ret)
		return ret;

	i915_gem_object_flush_cpu_write_domain(obj);

//	old_write_domain = obj->base.write_domain;
//	old_read_domains = obj->base.read_domains;

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	WARN_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_GTT) != 0);
	obj->base.read_domains |= I915_GEM_DOMAIN_GTT;
	if (write) {
		obj->base.read_domains = I915_GEM_DOMAIN_GTT;
		obj->base.write_domain = I915_GEM_DOMAIN_GTT;
		obj->dirty = 1;
	}

//	trace_i915_gem_object_change_domain(obj,
//					    old_read_domains,
//					    old_write_domain);

	/* And bump the LRU for this access */
	if (i915_gem_object_is_inactive(obj))
		list_move_tail(&obj->mm_list, &dev_priv->mm.inactive_list);

	return 0;
}

int
i915_gem_object_set_cache_level(struct drm_i915_gem_object *obj,
				    enum i915_cache_level cache_level)
{
	struct drm_device *dev = obj->base.dev;
//	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	if (obj->cache_level == cache_level)
		return 0;

	if (obj->pin_count) {
		DRM_DEBUG("can not change the cache level of pinned objects\n");
		return -EBUSY;
	}

	if (obj->dmamap != NULL) {
		ret = i915_gem_object_finish_gpu(obj);
		if (ret)
			return ret;

		i915_gem_object_finish_gtt(obj);

		/* Before SandyBridge, you could not use tiling or fence
		 * registers with snooped memory, so relinquish any fences
		 * currently pointing to our region in the aperture.
		 */
		if (INTEL_INFO(dev)->gen < 6) {
			ret = i915_gem_object_put_fence(obj);
			if (ret)
				return ret;
		}

		i915_gem_gtt_rebind_object(obj, cache_level);
		
#ifdef notyet
		if (obj->has_aliasing_ppgtt_mapping)
			i915_ppgtt_bind_object(dev_priv->mm.aliasing_ppgtt,
					       obj, cache_level);
#endif
	}

	if (cache_level == I915_CACHE_NONE) {
		u32 old_read_domains, old_write_domain;

		/* If we're coming from LLC cached, then we haven't
		 * actually been tracking whether the data is in the
		 * CPU cache or not, since we only allow one bit set
		 * in obj->write_domain and have been skipping the clflushes.
		 * Just set it to the CPU cache for now.
		 */
		WARN_ON(obj->base.write_domain & ~I915_GEM_DOMAIN_CPU);
		WARN_ON(obj->base.read_domains & ~I915_GEM_DOMAIN_CPU);

		old_read_domains = obj->base.read_domains;
		old_write_domain = obj->base.write_domain;

		obj->base.read_domains = I915_GEM_DOMAIN_CPU;
		obj->base.write_domain = I915_GEM_DOMAIN_CPU;

#if 0
		trace_i915_gem_object_change_domain(obj,
						    old_read_domains,
						    old_write_domain);
#endif
	}

	obj->cache_level = cache_level;
	return 0;
}

int
i915_gem_get_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct drm_i915_gem_caching *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = ENOENT;
		goto unlock;
	}

	args->caching = obj->cache_level != I915_CACHE_NONE;

	drm_gem_object_unreference(&obj->base);
unlock:
	DRM_UNLOCK();
	return ret;
}

int
i915_gem_set_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct drm_i915_gem_caching *args = data;
	struct drm_i915_gem_object *obj;
	enum i915_cache_level level;
	int ret;

	switch (args->caching) {
	case I915_CACHING_NONE:
		level = I915_CACHE_NONE;
		break;
	case I915_CACHING_CACHED:
		level = I915_CACHE_LLC;
		break;
	default:
		return EINVAL;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = ENOENT;
		goto unlock;
	}

	ret = i915_gem_object_set_cache_level(obj, level);

	drm_gem_object_unreference(&obj->base);
unlock:
	DRM_UNLOCK();
	return ret;
}

/*
 * Prepare buffer for display plane (scanout, cursors, etc).
 * Can be called from an uninterruptible phase (modesetting) and allows
 * any flushes to be pipelined (for pageflips).
 */
int
i915_gem_object_pin_to_display_plane(struct drm_i915_gem_object *obj,
				     u32 alignment,
				     struct intel_ring_buffer *pipelined)
{
//	u32 old_read_domains, old_write_domain;
	int ret;

	if (pipelined != obj->ring) {
		ret = i915_gem_object_sync(obj, pipelined);
		if (ret)
			return ret;
	}

	/* The display engine is not coherent with the LLC cache on gen6.  As
	 * a result, we make sure that the pinning that is about to occur is
	 * done with uncached PTEs. This is lowest common denominator for all
	 * chipsets.
	 *
	 * However for gen6+, we could do better by using the GFDT bit instead
	 * of uncaching, which would allow us to flush all the LLC-cached data
	 * with that bit in the PTE to main memory with just one PIPE_CONTROL.
	 */
	ret = i915_gem_object_set_cache_level(obj, I915_CACHE_NONE);
	if (ret)
		return ret;

	/* As the user may map the buffer once pinned in the display plane
	 * (e.g. libkms for the bootup splash), we have to ensure that we
	 * always use map_and_fenceable for all scanout buffers.
	 */
	ret = i915_gem_object_pin(obj, alignment, true, false);
	if (ret)
		return ret;

	i915_gem_object_flush_cpu_write_domain(obj);

//	old_write_domain = obj->write_domain;
//	old_read_domains = obj->read_domains;

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	obj->base.write_domain = 0;
	obj->base.read_domains |= I915_GEM_DOMAIN_GTT;

//	trace_i915_gem_object_change_domain(obj,
//					    old_read_domains,
//					    old_write_domain);

	return 0;
}

int
i915_gem_object_finish_gpu(struct drm_i915_gem_object *obj)
{
	int ret;

	if ((obj->base.read_domains & I915_GEM_GPU_DOMAINS) == 0)
		return 0;

	ret = i915_gem_object_wait_rendering(obj, false);
	if (ret)
		return ret;

	/* Ensure that we invalidate the GPU's caches and TLBs. */
	obj->base.read_domains &= ~I915_GEM_GPU_DOMAINS;
	return 0;
}

/**
 * Moves a single object to the CPU read, and possibly write domain.
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
int
i915_gem_object_set_to_cpu_domain(struct drm_i915_gem_object *obj, bool write)
{
//	uint32_t old_write_domain, old_read_domains;
	int ret;

	DRM_ASSERT_HELD(obj);

	if (obj->base.write_domain == I915_GEM_DOMAIN_CPU)
		return 0;

	ret = i915_gem_object_wait_rendering(obj, !write);
	if (ret)
		return ret;

	i915_gem_object_flush_gtt_write_domain(obj);

//	old_write_domain = obj->base.write_domain;
//	old_read_domains = obj->base.read_domains;

	/* Flush the CPU cache if it's still invalid. */
	if ((obj->base.read_domains & I915_GEM_DOMAIN_CPU) == 0) {
		i915_gem_clflush_object(obj);

		obj->base.read_domains |= I915_GEM_DOMAIN_CPU;
	}

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_CPU) != 0);

	/* If we're writing through the CPU, then the GPU read domains will
	 * need to be invalidated at next use.
	 */
	if (write) {
		obj->base.read_domains = I915_GEM_DOMAIN_CPU;
		obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	}

//	trace_i915_gem_object_change_domain(obj,
//					    old_read_domains,
//					    old_write_domain);

	return 0;
}

/* Throttle our rendering by waiting until the ring has completed our requests
 * emitted over 20 msec ago.
 *
 * Note that if we were to use the current jiffies each time around the loop,
 * we wouldn't escape the function with any frames outstanding if the time to
 * render a frame was over 20ms.
 *
 * This should get us reasonable parallelism between CPU and GPU but also
 * relatively low latency when blocking on a particular request to finish.
 */
int
i915_gem_ring_throttle(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	unsigned long recent_enough = ticks - msecs_to_jiffies(20);
	struct drm_i915_gem_request *request;
	struct intel_ring_buffer *ring = NULL;
	u32 seqno = 0;
	int ret;

	if (atomic_read(&dev_priv->mm.wedged))
		return EIO;

	mtx_enter(&file_priv->mm.lock);
	list_for_each_entry(request, &file_priv->mm.request_list, client_list) {
		if (time_after_eq(request->emitted_ticks, recent_enough))
			break;

		ring = request->ring;
		seqno = request->seqno;
	}
	mtx_leave(&file_priv->mm.lock);

	if (seqno == 0)
		return 0;

	ret = __wait_seqno(ring, seqno, true, NULL);
	if (ret == 0)
		timeout_add_sec(&dev_priv->mm.retire_timer, 0);

	return ret;
}

int
i915_gem_object_pin(struct drm_i915_gem_object *obj,
		    uint32_t alignment,
		    bool map_and_fenceable,
		    bool nonblocking)
{
	struct drm_device	*dev = obj->base.dev;
	int ret;

	DRM_ASSERT_HELD(&obj->base);
	inteldrm_verify_inactive(dev_priv, __FILE__, __LINE__);

	if (obj->dmamap != NULL) {
		if ((alignment && obj->gtt_offset & (alignment - 1)) ||
		    (map_and_fenceable && !obj->map_and_fenceable)) {
			WARN(obj->pin_count,
			     "bo is already pinned with incorrect alignment:"
			     " offset=%x, req.alignment=%x, req.map_and_fenceable=%d,"
			     " obj->map_and_fenceable=%d\n",
			     obj->gtt_offset, alignment,
			     map_and_fenceable,
			     obj->map_and_fenceable);
			ret = i915_gem_object_unbind(obj);
			if (ret)
				return ret;
		}
	}

	if (obj->dmamap == NULL) {
		ret = i915_gem_object_bind_to_gtt(obj, alignment,
						  map_and_fenceable,
						  nonblocking);
		if (ret)
			return ret;
	}

	if (obj->pin_count++ == 0) {
		atomic_inc(&dev->pin_count);
		atomic_add(obj->base.size, &dev->pin_memory);
		if (!obj->active)
			list_del_init(&obj->mm_list);
	}
	inteldrm_verify_inactive(dev_priv, __FILE__, __LINE__);

	return 0;
}

void
i915_gem_object_unpin(struct drm_i915_gem_object *obj)
{
	struct drm_device	*dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;

	DRM_ASSERT_HELD(&obj->base);
	inteldrm_verify_inactive(dev_priv, __FILE__, __LINE__);

	BUG_ON(obj->pin_count == 0);
	BUG_ON(obj->dmamap == NULL);

	if (--obj->pin_count == 0) {
		if (!obj->active)
			list_move_tail(&obj->mm_list,
				       &dev_priv->mm.inactive_list);
		atomic_dec(&dev->pin_count);
		atomic_sub(obj->base.size, &dev->pin_memory);
	}
	inteldrm_verify_inactive(dev_priv, __FILE__, __LINE__);
}

int
i915_gem_pin_ioctl(struct drm_device *dev, void *data,
		   struct drm_file *file)
{
	struct inteldrm_softc	*dev_priv = dev->dev_private;
	struct drm_i915_gem_pin *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = ENOENT;
		goto unlock;
	}

	drm_hold_object(&obj->base);

	if (obj->madv != I915_MADV_WILLNEED) {
		DRM_ERROR("Attempting to pin a purgeable buffer\n");
		ret = EINVAL;
		goto out;
	}

	if (obj->user_pin_count == 0) {
		ret = i915_gem_object_pin(obj, args->alignment, true, false);
		if (ret)
			goto out;
		inteldrm_set_max_obj_size(dev_priv);
	}

	obj->user_pin_count++;

	/* XXX - flush the CPU caches for pinned objects
	 * as the X server doesn't manage domains yet
	 */
	i915_gem_object_set_to_gtt_domain(obj, true);
	args->offset = obj->gtt_offset;
out:
	drm_unhold_and_unref(&obj->base);
unlock:
	DRM_UNLOCK();
	return ret;
}

int
i915_gem_unpin_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file)
{
	struct inteldrm_softc	*dev_priv = dev->dev_private;
	struct drm_i915_gem_pin *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = ENOENT;
		goto unlock;
	}

	drm_hold_object(&obj->base);

	if (obj->user_pin_count == 0) {
		ret = EINVAL;
		goto out;
	}

	obj->user_pin_count--;
	if (obj->user_pin_count == 0) {
		i915_gem_object_unpin(obj);
		inteldrm_set_max_obj_size(dev_priv);
	}

out:
	drm_unhold_and_unref(&obj->base);
unlock:
	DRM_UNLOCK();
	return ret;
}

int
i915_gem_busy_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_busy *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = ENOENT;
		goto unlock;
	}

	ret = i915_gem_object_flush_active(obj);
	args->busy = obj->active;

	drm_gem_object_unreference(&obj->base);
unlock:
	DRM_UNLOCK();
	return ret;
}

// i915_gem_throttle_ioctl

int
i915_gem_madvise_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_i915_gem_madvise *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	switch (args->madv) {
	case I915_MADV_DONTNEED:
	case I915_MADV_WILLNEED:
	    break;
	default:
	    return EINVAL;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file_priv, args->handle));
	if (&obj->base == NULL) {
		ret = ENOENT;
		goto unlock;
	}

	drm_hold_object(&obj->base);

	/* invalid to madvise on a pinned BO */
	if (obj->pin_count) {
		ret = EINVAL;
		goto out;
	}

	if (obj->madv != __I915_MADV_PURGED)
		obj->madv = args->madv;

	/* if the object is no longer attached, discard its backing storage */
	if (i915_gem_object_is_purgeable(obj) && obj->dmamap == NULL)
		i915_gem_object_truncate(obj);

	args->retained = obj->madv != __I915_MADV_PURGED;

out:
	drm_unhold_and_unref(&obj->base);
unlock:
	DRM_UNLOCK();
	return ret;
}

void
i915_gem_object_init(struct drm_i915_gem_object *obj)
{
	INIT_LIST_HEAD(&obj->mm_list);
	INIT_LIST_HEAD(&obj->gtt_list);
	INIT_LIST_HEAD(&obj->ring_list);
	INIT_LIST_HEAD(&obj->exec_list);

	obj->fence_reg = I915_FENCE_REG_NONE;
	obj->madv = I915_MADV_WILLNEED;
	/* Avoid an unnecessary call to unbind on the first bind. */
	obj->map_and_fenceable = true;

#ifdef notyet
	i915_gem_info_add_obj(obj->base.dev->dev_private, obj->base.size);
#endif
}

struct drm_i915_gem_object *
i915_gem_alloc_object(struct drm_device *dev, size_t size)
{
	struct drm_i915_gem_object *obj;

	obj = pool_get(&dev->objpl, PR_WAITOK | PR_ZERO);
	if (obj == NULL)
		return NULL;

	if (drm_gem_object_init(dev, &obj->base, size) != 0) {
		pool_put(&dev->objpl, obj);
		return NULL;
	}

	i915_gem_object_init(obj);

	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	obj->base.read_domains = I915_GEM_DOMAIN_CPU;

	if (HAS_LLC(dev)) {
		/* On some devices, we can have the GPU use the LLC (the CPU
		 * cache) for about a 10% performance improvement
		 * compared to uncached.  Graphics requests other than
		 * display scanout are coherent with the CPU in
		 * accessing this cache.  This means in this mode we
		 * don't need to clflush on the CPU side, and on the
		 * GPU side we only need to flush internal caches to
		 * get data visible to the CPU.
		 *
		 * However, we maintain the display planes as UC, and so
		 * need to rebind when first used as such.
		 */
		obj->cache_level = I915_CACHE_LLC;
	} else
		obj->cache_level = I915_CACHE_NONE;

	return obj;
}

int
i915_gem_init_object(struct drm_obj *obj)
{
	BUG();

	return 0;
}

void
i915_gem_free_object(struct drm_obj *gem_obj)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem_obj);
	struct drm_device *dev = gem_obj->dev;

	DRM_ASSERT_HELD(&obj->base);

	if (obj->phys_obj)
		i915_gem_detach_phys_object(dev, obj);
	
	while (obj->pin_count > 0)
		i915_gem_object_unpin(obj);

	i915_gem_object_unbind(obj);

	drm_gem_object_release(&obj->base);
#ifdef notyet
	i915_gem_info_remove_obj(dev_priv, obj->base.size);
#endif

	drm_free(obj->bit_17);
	pool_put(&dev->objpl, obj);
}

int
i915_gem_idle(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	DRM_LOCK();

	if (dev_priv->mm.suspended) {
		DRM_UNLOCK();
		return 0;
	}

	ret = i915_gpu_idle(dev);
	if (ret) {
		DRM_UNLOCK();
		return ret;
	}
	i915_gem_retire_requests(dev);

	/* Under UMS, be paranoid and evict. */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		i915_gem_evict_everything(dev);

	i915_gem_reset_fences(dev);

	/* Hack!  Don't let anybody do execbuf while we don't control the chip.
	 * We need to replace this with a semaphore, or something.
	 * And not confound mm.suspended!
	 */
	dev_priv->mm.suspended = 1;
	timeout_del(&dev_priv->hangcheck_timer);

	i915_kernel_lost_context(dev);
	i915_gem_cleanup_ringbuffer(dev);

	DRM_UNLOCK();

	/* Cancel the retire work handler, which should be idle now. */
	timeout_del(&dev_priv->mm.retire_timer);

	return 0;
}

// i915_gem_l3_remap

void
i915_gem_init_swizzling(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (INTEL_INFO(dev)->gen < 5 ||
	    dev_priv->mm.bit_6_swizzle_x == I915_BIT_6_SWIZZLE_NONE)
		return;

	I915_WRITE(DISP_ARB_CTL, I915_READ(DISP_ARB_CTL) |
				 DISP_TILE_SURFACE_SWIZZLING);

	if (IS_GEN5(dev))
		return;

	I915_WRITE(TILECTL, I915_READ(TILECTL) | TILECTL_SWZCTL);
	if (IS_GEN6(dev))
		I915_WRITE(ARB_MODE, _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_SNB));
	else
		I915_WRITE(ARB_MODE, _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_IVB));
}

bool
intel_enable_blt(struct drm_device *dev)
{
	if (!HAS_BLT(dev))
		return false;

#ifdef notyet
	/* The blitter was dysfunctional on early prototypes */
	if (IS_GEN6(dev) && dev->pdev->revision < 8) {
		DRM_INFO("BLT not supported on this pre-production hardware;"
			 " graphics performance will be degraded.\n");
		return false;
	}
#endif

	return true;
}

int
i915_gem_init_hw(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

#ifdef notyet
	if (INTEL_INFO(dev)->gen < 6 && !intel_enable_gtt())
		return -EIO;

	if (IS_HASWELL(dev) && (I915_READ(0x120010) == 1))
		I915_WRITE(0x9008, I915_READ(0x9008) | 0xf0000);

	i915_gem_l3_remap(dev);

#endif
	i915_gem_init_swizzling(dev);

	ret = intel_init_render_ring_buffer(dev);
	if (ret)
		return ret;

	if (HAS_BSD(dev)) {
		ret = intel_init_bsd_ring_buffer(dev);
		if (ret)
			goto cleanup_render_ring;
	}

	if (intel_enable_blt(dev)) {
		ret = intel_init_blt_ring_buffer(dev);
		if (ret)
			goto cleanup_bsd_ring;
	}

	dev_priv->next_seqno = 1;

	/*
	 * XXX: There was some w/a described somewhere suggesting loading
	 * contexts before PPGTT.
	 */
#ifdef notyet
	i915_gem_context_init(dev);
	i915_gem_init_ppgtt(dev);
#endif

	return 0;

cleanup_bsd_ring:
	intel_cleanup_ring_buffer(&dev_priv->ring[VCS]);
cleanup_render_ring:
	intel_cleanup_ring_buffer(&dev_priv->ring[RCS]);
	return ret;
}

// intel_enable_ppgtt

int
i915_gem_init(struct drm_device *dev)
{
	struct drm_i915_private		*dev_priv = dev->dev_private;
	uint64_t			 gtt_start, gtt_end;
	struct agp_softc		*asc;
	int				 ret;

	DRM_LOCK();

	asc = (struct agp_softc *)dev->agp->agpdev;
	gtt_start = asc->sc_stolen_entries * 4096;

	/*
	 * putting stuff in the last page of the aperture can cause nasty
	 * problems with prefetch going into unassigned memory. Since we put
	 * a scratch page on all unused aperture pages, just leave the last
	 * page as a spill to prevent gpu hangs.
	 */
	gtt_end = dev->agp->info.ai_aperture_size - 4096;

	if (agp_bus_dma_init(asc,
	    dev->agp->base + gtt_start, dev->agp->base + gtt_end,
	    &dev_priv->agpdmat) != 0) {
		DRM_UNLOCK();
		return (ENOMEM);
	}

	dev->gtt_total = (uint32_t)(gtt_end - gtt_start);
	inteldrm_set_max_obj_size(dev_priv);

	dev_priv->mm.gtt_start = gtt_start;
	dev_priv->mm.gtt_mappable_end = gtt_end;
	dev_priv->mm.gtt_end = gtt_end;
	dev_priv->mm.gtt_total = gtt_end - gtt_start;

	ret = i915_gem_init_hw(dev);
	if (ret != 0) {
		DRM_UNLOCK();
		return (ret);
	}

	DRM_UNLOCK();

	return 0;
}

void
i915_gem_cleanup_ringbuffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	int i;

	for_each_ring(ring, dev_priv, i)
		intel_cleanup_ring_buffer(ring);
}

int
i915_gem_entervt_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	if (atomic_read(&dev_priv->mm.wedged)) {
		DRM_ERROR("Reenabling wedged hardware, good luck\n");
		atomic_set(&dev_priv->mm.wedged, 0);
	}

	DRM_LOCK();
	dev_priv->mm.suspended = 0;

	ret = i915_gem_init_hw(dev);
	if (ret != 0) {
		DRM_UNLOCK();
		return ret;
	}

	BUG_ON(!list_empty(&dev_priv->mm.active_list));
	DRM_UNLOCK();

	ret = drm_irq_install(dev);
	if (ret)
		goto cleanup_ringbuffer;

	return 0;

cleanup_ringbuffer:
	DRM_LOCK();
	i915_gem_cleanup_ringbuffer(dev);
	dev_priv->mm.suspended = 1;
	DRM_UNLOCK();

	return ret;
}

int
i915_gem_leavevt_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	drm_irq_uninstall(dev);
	return i915_gem_idle(dev);
}

// i915_gem_lastclose

void
init_ring_lists(struct intel_ring_buffer *ring)
{
	INIT_LIST_HEAD(&ring->active_list);
	INIT_LIST_HEAD(&ring->request_list);
}

void
i915_gem_load(struct drm_device *dev)
{
	int i;
	drm_i915_private_t *dev_priv = dev->dev_private;

	INIT_LIST_HEAD(&dev_priv->mm.active_list);
	INIT_LIST_HEAD(&dev_priv->mm.inactive_list);
	INIT_LIST_HEAD(&dev_priv->mm.unbound_list);
	INIT_LIST_HEAD(&dev_priv->mm.bound_list);
	INIT_LIST_HEAD(&dev_priv->mm.fence_list);
	for (i = 0; i < I915_NUM_RINGS; i++)
		init_ring_lists(&dev_priv->ring[i]);
	for (i = 0; i < I915_MAX_NUM_FENCES; i++)
		INIT_LIST_HEAD(&dev_priv->fence_regs[i].lru_list);
	timeout_set(&dev_priv->mm.retire_timer, inteldrm_timeout, dev_priv);
#if 0
	init_completion(&dev_priv->error_completion);
#else
	dev_priv->error_completion = 0;
#endif

	/* On GEN3 we really need to make sure the ARB C3 LP bit is set */
	if (IS_GEN3(dev)) {
		I915_WRITE(MI_ARB_STATE,
			   _MASKED_BIT_ENABLE(MI_ARB_C3_LP_WRITE_ENABLE));
	}

	dev_priv->relative_constants_mode = I915_EXEC_CONSTANTS_REL_GENERAL;

	/* Old X drivers will take 0-2 for front, back, depth buffers */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		dev_priv->fence_reg_start = 3;

	if (INTEL_INFO(dev)->gen >= 4 || IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
		dev_priv->num_fence_regs = 16;
	else
		dev_priv->num_fence_regs = 8;

	/* Initialize fence registers to zero */
	i915_gem_reset_fences(dev);

	i915_gem_detect_bit_6_swizzle(dev);
#if 0
	init_waitqueue_head(&dev_priv->pending_flip_queue);
#endif

	dev_priv->mm.interruptible = true;

#if 0
	dev_priv->mm.inactive_shrinker.shrink = i915_gem_inactive_shrink;
	dev_priv->mm.inactive_shrinker.seeks = DEFAULT_SEEKS;
	register_shrinker(&dev_priv->mm.inactive_shrinker);
#endif
}

/*
 * Create a physically contiguous memory object for this object
 * e.g. for cursor + overlay regs
 */
int
i915_gem_init_phys_object(struct drm_device *dev,
			  int id, int size, int align)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_phys_object *phys_obj;
	int ret;

	if (dev_priv->mm.phys_objs[id - 1] || !size)
		return 0;

	phys_obj = drm_alloc(sizeof(struct drm_i915_gem_phys_object));
	if (!phys_obj)
		return -ENOMEM;

	phys_obj->id = id;

	phys_obj->handle = drm_dmamem_alloc(dev->dmat, size, align, 1, size, BUS_DMA_NOCACHE, 0);
	if (!phys_obj->handle) {
		ret = -ENOMEM;
		goto kfree_obj;
	}

	dev_priv->mm.phys_objs[id - 1] = phys_obj;

	return 0;
kfree_obj:
	drm_free(phys_obj);
	return ret;
}

// i915_gem_free_phys_object
// i915_gem_free_all_phys_object

void i915_gem_detach_phys_object(struct drm_device *dev,
				 struct drm_i915_gem_object *obj)
{
	char *vaddr;
	int i;
	int page_count;

	if (!obj->phys_obj)
		return;
	vaddr = obj->phys_obj->handle->kva;

	page_count = obj->base.size / PAGE_SIZE;
	for (i = 0; i < page_count; i++) {
#ifdef notyet
		struct page *page = shmem_read_mapping_page(mapping, i);
		if (!IS_ERR(page)) {
			char *dst = kmap_atomic(page);
			memcpy(dst, vaddr + i*PAGE_SIZE, PAGE_SIZE);
			kunmap_atomic(dst);

			drm_clflush_pages(&page, 1);

			set_page_dirty(page);
			mark_page_accessed(page);
			page_cache_release(page);
		}
#endif
	}
	i915_gem_chipset_flush(dev);

	obj->phys_obj->cur_obj = NULL;
	obj->phys_obj = NULL;
}

int
i915_gem_attach_phys_object(struct drm_device *dev,
			    struct drm_i915_gem_object *obj,
			    int id,
			    int align)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret = 0;
	int page_count;
	int i;

	if (id > I915_MAX_PHYS_OBJECT)
		return -EINVAL;

	if (obj->phys_obj) {
		if (obj->phys_obj->id == id)
			return 0;
		i915_gem_detach_phys_object(dev, obj);
	}

	/* create a new object */
	if (!dev_priv->mm.phys_objs[id - 1]) {
		ret = i915_gem_init_phys_object(dev, id,
						obj->base.size, align);
		if (ret) {
			DRM_ERROR("failed to init phys object %d size: %zu\n",
				  id, obj->base.size);
			return ret;
		}
	}

	/* bind to the object */
	obj->phys_obj = dev_priv->mm.phys_objs[id - 1];
	obj->phys_obj->cur_obj = obj;

	page_count = obj->base.size / PAGE_SIZE;

	for (i = 0; i < page_count; i++) {
#ifdef notyet
		struct page *page;
		char *dst, *src;

		page = shmem_read_mapping_page(mapping, i);
		if (IS_ERR(page))
			return PTR_ERR(page);

		src = kmap_atomic(page);
		dst = obj->phys_obj->handle->kva + (i * PAGE_SIZE);
		memcpy(dst, src, PAGE_SIZE);
		kunmap_atomic(src);

		mark_page_accessed(page);
		page_cache_release(page);
#endif
	}

	return 0;
}

int
i915_gem_phys_pwrite(struct drm_device *dev,
		     struct drm_i915_gem_object *obj,
		     struct drm_i915_gem_pwrite *args,
		     struct drm_file *file_priv)
{
	void *vaddr = obj->phys_obj->handle->kva + args->offset;
	int ret;

	ret = copyin((char *)(uintptr_t)args->data_ptr,
	    vaddr, args->size);

	i915_gem_chipset_flush(dev);

	return ret;
}

void
i915_gem_release(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	/* Clean up our request list when the client is going away, so that
	 * later retire_requests won't dereference our soon-to-be-gone
	 * file_priv.
	 */
	mtx_enter(&file_priv->mm.lock);
	while (!list_empty(&file_priv->mm.request_list)) {
		struct drm_i915_gem_request *request;

		request = list_first_entry(&file_priv->mm.request_list,
					   struct drm_i915_gem_request,
					   client_list);
		list_del(&request->client_list);
		request->file_priv = NULL;
	}
	mtx_leave(&file_priv->mm.lock);
}

// i915_gem_release
// mutex_is_locked_by
// i915_gem_inactive_shrink
