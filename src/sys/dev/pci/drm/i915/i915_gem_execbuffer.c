/*	$OpenBSD: i915_gem_execbuffer.c,v 1.10 2013/08/08 21:35:56 kettenis Exp $	*/
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
 * Copyright © 2008,2010 Intel Corporation
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
 *    Chris Wilson <chris@chris-wilson.co.uk>
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

int	i915_reset_gen7_sol_offsets(struct drm_device *,
	    struct intel_ring_buffer *);
int	i915_gem_execbuffer_wait_for_flips(struct intel_ring_buffer *, u32);
int	i915_gem_execbuffer_move_to_gpu(struct intel_ring_buffer *,
	    struct drm_obj **, int);
void	i915_gem_execbuffer_move_to_active(struct drm_obj **, int,
	    struct intel_ring_buffer *);
void	i915_gem_execbuffer_retire_commands(struct drm_device *,
	    struct drm_file *, struct intel_ring_buffer *);
int	need_reloc_mappable(struct drm_i915_gem_object *);
int	i915_gem_execbuffer_reserve(struct intel_ring_buffer *,
	    struct drm_file *, struct list_head *);

// struct eb_objects {
// eb_create
// eb_reset
// eb_add_object
// eb_get_object
// eb_destroy

static inline int use_cpu_reloc(struct drm_i915_gem_object *obj)
{
	return (obj->base.write_domain == I915_GEM_DOMAIN_CPU ||
		!obj->map_and_fenceable ||
		obj->cache_level != I915_CACHE_NONE);
}

// i915_gem_execbuffer_relocate_entry
// i915_gem_execbuffer_relocate_object
// i915_gem_execbuffer_relocate_object_slow
// i915_gem_execbuffer_relocate

#define  __EXEC_OBJECT_HAS_PIN (1<<31)
#define  __EXEC_OBJECT_HAS_FENCE (1<<30)

int
need_reloc_mappable(struct drm_i915_gem_object *obj)
{
	struct drm_i915_gem_exec_object2 *entry = obj->exec_entry;
	return entry->relocation_count && !use_cpu_reloc(obj);
}

int
i915_gem_execbuffer_reserve_object(struct drm_i915_gem_object *obj,
				   struct intel_ring_buffer *ring)
{
#ifdef notyet
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
#endif
	struct drm_i915_gem_exec_object2 *entry = obj->exec_entry;
	bool has_fenced_gpu_access = INTEL_INFO(ring->dev)->gen < 4;
	bool need_fence, need_mappable;
	int ret;

	need_fence =
		has_fenced_gpu_access &&
		entry->flags & EXEC_OBJECT_NEEDS_FENCE &&
		obj->tiling_mode != I915_TILING_NONE;
	need_mappable = need_fence || need_reloc_mappable(obj);

	ret = i915_gem_object_pin(obj, entry->alignment, need_mappable, false);
	if (ret)
		return ret;

	entry->flags |= __EXEC_OBJECT_HAS_PIN;

	if (has_fenced_gpu_access) {
		if (entry->flags & EXEC_OBJECT_NEEDS_FENCE) {
			ret = i915_gem_object_get_fence(obj);
			if (ret)
				return ret;

			if (i915_gem_object_pin_fence(obj))
				entry->flags |= __EXEC_OBJECT_HAS_FENCE;

			obj->pending_fenced_gpu_access = true;
		}
	}

#ifdef notyet
	/* Ensure ppgtt mapping exists if needed */
	if (dev_priv->mm.aliasing_ppgtt && !obj->has_aliasing_ppgtt_mapping) {
		i915_ppgtt_bind_object(dev_priv->mm.aliasing_ppgtt,
				       obj, obj->cache_level);

		obj->has_aliasing_ppgtt_mapping = 1;
	}
#endif

	entry->offset = obj->gtt_offset;
	return 0;
}

void
i915_gem_execbuffer_unreserve_object(struct drm_i915_gem_object *obj)
{
	struct drm_i915_gem_exec_object2 *entry;

	if (obj->dmamap == NULL)
		return;

	entry = obj->exec_entry;

	if (entry->flags & __EXEC_OBJECT_HAS_FENCE)
		i915_gem_object_unpin_fence(obj);

	if (entry->flags & __EXEC_OBJECT_HAS_PIN)
		i915_gem_object_unpin(obj);

	entry->flags &= ~(__EXEC_OBJECT_HAS_FENCE | __EXEC_OBJECT_HAS_PIN);
}

int
i915_gem_execbuffer_reserve(struct intel_ring_buffer *ring,
			    struct drm_file *file,
			    struct list_head *objects)
{
	struct drm_i915_gem_object *obj;
	struct list_head ordered_objects;
	bool has_fenced_gpu_access = INTEL_INFO(ring->dev)->gen < 4;
	int retry;

	INIT_LIST_HEAD(&ordered_objects);
	while (!list_empty(objects)) {
		struct drm_i915_gem_exec_object2 *entry;
		bool need_fence, need_mappable;

		obj = list_first_entry(objects,
				       struct drm_i915_gem_object,
				       exec_list);
		entry = obj->exec_entry;

		need_fence =
			has_fenced_gpu_access &&
			entry->flags & EXEC_OBJECT_NEEDS_FENCE &&
			obj->tiling_mode != I915_TILING_NONE;
		need_mappable = need_fence || need_reloc_mappable(obj);

		if (need_mappable)
			list_move(&obj->exec_list, &ordered_objects);
		else
			list_move_tail(&obj->exec_list, &ordered_objects);

		obj->base.pending_read_domains = 0;
		obj->base.pending_write_domain = 0;
		obj->pending_fenced_gpu_access = false;
	}
	list_splice(&ordered_objects, objects);

	/* Attempt to pin all of the buffers into the GTT.
	 * This is done in 3 phases:
	 *
	 * 1a. Unbind all objects that do not match the GTT constraints for
	 *     the execbuffer (fenceable, mappable, alignment etc).
	 * 1b. Increment pin count for already bound objects.
	 * 2.  Bind new objects.
	 * 3.  Decrement pin count.
	 *
	 * This avoid unnecessary unbinding of later objects in order to make
	 * room for the earlier objects *unless* we need to defragment.
	 */
	retry = 0;
	do {
		int ret = 0;

		/* Unbind any ill-fitting objects or pin. */
		list_for_each_entry(obj, objects, exec_list) {
			struct drm_i915_gem_exec_object2 *entry = obj->exec_entry;
			bool need_fence, need_mappable;

			if (obj->dmamap == NULL)
				continue;

			need_fence =
				has_fenced_gpu_access &&
				entry->flags & EXEC_OBJECT_NEEDS_FENCE &&
				obj->tiling_mode != I915_TILING_NONE;
			need_mappable = need_fence || need_reloc_mappable(obj);

			if ((entry->alignment && obj->gtt_offset & (entry->alignment - 1)) ||
			    (need_mappable && !obj->map_and_fenceable))
				ret = i915_gem_object_unbind(obj);
			else
				ret = i915_gem_execbuffer_reserve_object(obj, ring);
			if (ret)
				goto err;
		}

		/* Bind fresh objects */
		list_for_each_entry(obj, objects, exec_list) {
			if (obj->dmamap != NULL)
				continue;

			ret = i915_gem_execbuffer_reserve_object(obj, ring);
			if (ret)
				goto err;
		}

err:		/* Decrement pin count for bound objects */
		list_for_each_entry(obj, objects, exec_list)
			i915_gem_execbuffer_unreserve_object(obj);

		if (ret != -ENOSPC || retry++)
			return ret;

		ret = i915_gem_evict_everything(ring->dev);
		if (ret)
			return ret;
	} while (1);
}

int
i915_gem_execbuffer_wait_for_flips(struct intel_ring_buffer *ring, u32 flips)
{
	u32 plane, flip_mask;
	int ret;

	/* Check for any pending flips. As we only maintain a flip queue depth
	 * of 1, we can simply insert a WAIT for the next display flip prior
	 * to executing the batch and avoid stalling the CPU.
	 */

	for (plane = 0; flips >> plane; plane++) {
		if (((flips >> plane) & 1) == 0)
			continue;

		if (plane)
			flip_mask = MI_WAIT_FOR_PLANE_B_FLIP;
		else
			flip_mask = MI_WAIT_FOR_PLANE_A_FLIP;

		ret = intel_ring_begin(ring, 2);
		if (ret)
			return ret;

		intel_ring_emit(ring, MI_WAIT_FOR_EVENT | flip_mask);
		intel_ring_emit(ring, MI_NOOP);
		intel_ring_advance(ring);
	}

	return 0;
}

int
i915_gem_execbuffer_move_to_gpu(struct intel_ring_buffer *ring,
   struct drm_obj **object_list, int buffer_count)
{
	struct drm_i915_gem_object *obj;
	uint32_t flush_domains = 0;
	uint32_t flips = 0;
	int ret, i;

	for (i = 0; i < buffer_count; i++) {
		obj = to_intel_bo(object_list[i]);
		ret = i915_gem_object_sync(obj, ring);
		if (ret)
			return ret;

		if (obj->base.write_domain & I915_GEM_DOMAIN_CPU)
			i915_gem_clflush_object(obj);

		if (obj->base.pending_write_domain)
			flips |= atomic_read(&obj->pending_flip);

		flush_domains |= obj->base.write_domain;
	}

	if (flips) {
		ret = i915_gem_execbuffer_wait_for_flips(ring, flips);
		if (ret)
			return ret;
	}

	if (flush_domains & I915_GEM_DOMAIN_CPU)
		i915_gem_chipset_flush(ring->dev);

	if (flush_domains & I915_GEM_DOMAIN_GTT)
		DRM_WRITEMEMORYBARRIER();

	/* Unconditionally invalidate gpu caches and ensure that we do flush
	 * any residual writes from the previous batch.
	 */
	return intel_ring_invalidate_all_caches(ring);
}

// i915_gem_check_execbuffer
// validate_exec_list

void
i915_gem_execbuffer_move_to_active(struct drm_obj **object_list,
    int buffer_count, struct intel_ring_buffer *ring)
{
	struct drm_i915_gem_object *obj;
	int i;

	for (i = 0; i < buffer_count; i++) {
		obj = to_intel_bo(object_list[i]);
#if 0
		u32 old_read = obj->base.read_domains;
		u32 old_write = obj->base.write_domain;
#endif

		obj->base.read_domains = obj->base.pending_read_domains;
		obj->base.write_domain = obj->base.pending_write_domain;
		obj->fenced_gpu_access = obj->pending_fenced_gpu_access;

		i915_gem_object_move_to_active(obj, ring);
		if (obj->base.write_domain) {
			obj->dirty = 1;
			obj->last_write_seqno = intel_ring_get_seqno(ring);
			if (obj->pin_count) /* check for potential scanout */
				intel_mark_fb_busy(obj);
		}

//		trace_i915_gem_object_change_domain(obj, old_read, old_write);
	}
}

void
i915_gem_execbuffer_retire_commands(struct drm_device *dev,
				    struct drm_file *file,
				    struct intel_ring_buffer *ring)
{
	/* Unconditionally force add_request to emit a full flush. */
	ring->gpu_caches_dirty = true;

	/* Add a breadcrumb for the completion of the batch buffer */
	i915_add_request(ring, file, NULL);
}

// i915_gem_fix_mi_batchbuffer_end

int
i915_reset_gen7_sol_offsets(struct drm_device *dev,
			    struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret, i;

	if (!IS_GEN7(dev) || ring != &dev_priv->ring[RCS])
		return 0;

	ret = intel_ring_begin(ring, 4 * 3);
	if (ret)
		return ret;

	for (i = 0; i < 4; i++) {
		intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
		intel_ring_emit(ring, GEN7_SO_WRITE_OFFSET(i));
		intel_ring_emit(ring, 0);
	}

	intel_ring_advance(ring);

	return 0;
}

// i915_gem_do_execbuffer
// i915_gem_execbuffer

int
i915_gem_execbuffer2(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	struct inteldrm_softc			*dev_priv = dev->dev_private;
	struct drm_i915_gem_execbuffer2		*args = data;
	struct drm_i915_gem_exec_object2	*exec_list = NULL;
	struct drm_i915_gem_relocation_entry	*relocs = NULL;
	struct drm_i915_gem_object		*batch_obj_priv;
	struct drm_obj				**object_list = NULL;
	struct drm_obj				*batch_obj, *obj;
	struct intel_ring_buffer		*ring;
	uint32_t				 ctx_id = i915_execbuffer2_get_context_id(*args);
	size_t					 oflow;
	int					 ret, ret2, i;
	int					 pinned = 0, pin_tries;
	uint32_t				 reloc_index;
	uint32_t				 flags;
	uint32_t				 exec_start, exec_len;
	uint32_t				 mask;
	int					 mode;

	/*
	 * Check for valid execbuffer offset. We can do this early because
	 * bound object are always page aligned, so only the start offset
	 * matters. Also check for integer overflow in the batch offset and size
	 */
	 if ((args->batch_start_offset | args->batch_len) & 0x7 ||
	    args->batch_start_offset + args->batch_len < args->batch_len ||
	    args->batch_start_offset + args->batch_len <
	    args->batch_start_offset)
		return -EINVAL;

	if (args->buffer_count < 1) {
		DRM_ERROR("execbuf with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	flags = 0;
	if (args->flags & I915_EXEC_SECURE) {
		if (!DRM_SUSER(curproc))
			return -EPERM;

		flags |= I915_DISPATCH_SECURE;
	}
	if (args->flags & I915_EXEC_IS_PINNED)
		flags |= I915_DISPATCH_PINNED;

	switch (args->flags & I915_EXEC_RING_MASK) {
	case I915_EXEC_DEFAULT:
	case I915_EXEC_RENDER:
		ring = &dev_priv->ring[RCS];
		break;
	case I915_EXEC_BSD:
		ring = &dev_priv->ring[VCS];
		break;
	case I915_EXEC_BLT:
		ring = &dev_priv->ring[BCS];
		break;
	default:
		printf("unknown ring %d\n",
		    (int)(args->flags & I915_EXEC_RING_MASK));
		return -EINVAL;
	}
	if (!intel_ring_initialized(ring)) {
		DRM_DEBUG("execbuf with invalid ring: %d\n",
			  (int)(args->flags & I915_EXEC_RING_MASK));
		return -EINVAL;
	}

	mode = args->flags & I915_EXEC_CONSTANTS_MASK;
	mask = I915_EXEC_CONSTANTS_MASK;
	switch (mode) {
	case I915_EXEC_CONSTANTS_REL_GENERAL:
	case I915_EXEC_CONSTANTS_ABSOLUTE:
	case I915_EXEC_CONSTANTS_REL_SURFACE:
		if (ring == &dev_priv->ring[RCS] &&
		    mode != dev_priv->relative_constants_mode) {
			if (INTEL_INFO(dev)->gen < 4)
				return -EINVAL;

			if (INTEL_INFO(dev)->gen > 5 &&
			    mode == I915_EXEC_CONSTANTS_REL_SURFACE)
				return -EINVAL;

			/* The HW changed the meaning on this bit on gen6 */
			if (INTEL_INFO(dev)->gen >= 6)
				mask &= ~I915_EXEC_CONSTANTS_REL_SURFACE;
		}
		break;
	default:
		DRM_DEBUG("execbuf with unknown constants: %d\n", mode);
		return -EINVAL;
	}

	/* Copy in the exec list from userland, check for overflow */
	oflow = SIZE_MAX / args->buffer_count;
	if (oflow < sizeof(*exec_list) || oflow < sizeof(*object_list))
		return -EINVAL;
	exec_list = drm_alloc(sizeof(*exec_list) * args->buffer_count);
	object_list = drm_alloc(sizeof(*object_list) * args->buffer_count);
	if (exec_list == NULL || object_list == NULL) {
		ret = -ENOMEM;
		goto pre_mutex_err;
	}
	ret = -copyin((void *)(uintptr_t)args->buffers_ptr, exec_list,
	    sizeof(*exec_list) * args->buffer_count);
	if (ret != 0)
		goto pre_mutex_err;

	ret = -i915_gem_get_relocs_from_user(exec_list, args->buffer_count,
	    &relocs);
	if (ret != 0)
		goto pre_mutex_err;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto pre_mutex_err;
	inteldrm_verify_inactive(dev_priv, __FILE__, __LINE__);

	/* XXX check these before we copyin... but we do need the lock */
	if (dev_priv->mm.wedged) {
		ret = -EIO;
		goto unlock;
	}

	if (dev_priv->mm.suspended) {
		ret = -EBUSY;
		goto unlock;
	}

	/* Look up object handles */
	for (i = 0; i < args->buffer_count; i++) {
		object_list[i] = drm_gem_object_lookup(dev, file_priv,
		    exec_list[i].handle);
		obj = object_list[i];
		if (obj == NULL) {
			DRM_ERROR("Invalid object handle %d at index %d\n",
				   exec_list[i].handle, i);
			ret = -ENOENT;
			goto err;
		}
		if (obj->do_flags & I915_IN_EXEC) {
			DRM_ERROR("Object %p appears more than once in object_list\n",
			    object_list[i]);
			ret = -EINVAL;
			goto err;
		}
		atomic_setbits_int(&obj->do_flags, I915_IN_EXEC);
	}

	/* Pin and relocate */
	for (pin_tries = 0; ; pin_tries++) {
		ret = pinned = 0;
		reloc_index = 0;

		for (i = 0; i < args->buffer_count; i++) {
			object_list[i]->pending_read_domains = 0;
			object_list[i]->pending_write_domain = 0;
			to_intel_bo(object_list[i])->pending_fenced_gpu_access = false;
			drm_hold_object(object_list[i]);
			to_intel_bo(object_list[i])->exec_entry = &exec_list[i];
			ret = i915_gem_object_pin_and_relocate(object_list[i],
			    file_priv, &exec_list[i], &relocs[reloc_index]);
			if (ret) {
				drm_unhold_object(object_list[i]);
				break;
			}
			pinned++;
			reloc_index += exec_list[i].relocation_count;
		}
		/* success */
		if (ret == 0)
			break;

		/* error other than GTT full, or we've already tried again */
		if (ret != -ENOSPC || pin_tries >= 1)
			goto err;

		/*
		 * unpin all of our buffers and unhold them so they can be
		 * unbound so we can try and refit everything in the aperture.
		 */
		for (i = 0; i < pinned; i++) {
			if (object_list[i]->do_flags & __EXEC_OBJECT_HAS_FENCE) {
				i915_gem_object_unpin_fence(to_intel_bo(object_list[i]));
				object_list[i]->do_flags &= ~__EXEC_OBJECT_HAS_FENCE;
			}
			i915_gem_object_unpin(to_intel_bo(object_list[i]));
			drm_unhold_object(object_list[i]);
		}
		pinned = 0;
		/* evict everyone we can from the aperture */
		ret = i915_gem_evict_everything(dev);
		if (ret)
			goto err;
	}

	/* If we get here all involved objects are referenced, pinned, relocated
	 * and held. Now we can finish off the exec processing.
	 *
	 * First, set the pending read domains for the batch buffer to
	 * command.
	 */
	batch_obj = object_list[args->buffer_count - 1];
	batch_obj_priv = to_intel_bo(batch_obj);
	if (args->batch_start_offset + args->batch_len > batch_obj->size ||
	    batch_obj->pending_write_domain) {
		ret = -EINVAL;
		goto err;
	}
	batch_obj->pending_read_domains |= I915_GEM_DOMAIN_COMMAND;

	ret = i915_gem_execbuffer_move_to_gpu(ring, object_list,
	    args->buffer_count);
	if (ret)
		goto err;

	ret = i915_switch_context(ring, file_priv, ctx_id);
	if (ret)
		goto err;

	if (ring == &dev_priv->ring[RCS] &&
	    mode != dev_priv->relative_constants_mode) {
		ret = intel_ring_begin(ring, 4);
		if (ret)
				goto err;

		intel_ring_emit(ring, MI_NOOP);
		intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
		intel_ring_emit(ring, INSTPM);
		intel_ring_emit(ring, mask << 16 | mode);
		intel_ring_advance(ring);

		dev_priv->relative_constants_mode = mode;
	}

	if (args->flags & I915_EXEC_GEN7_SOL_RESET) {
		ret = i915_reset_gen7_sol_offsets(dev, ring);
		if (ret)
			goto err;
	}

	/* Exec the batchbuffer */
	/*
	 * XXX make sure that this may never fail by preallocating the request.
	 */

	exec_start = batch_obj_priv->gtt_offset + args->batch_start_offset;
	exec_len = args->batch_len;

	ret = ring->dispatch_execbuffer(ring, exec_start, exec_len, flags);
	if (ret)
		goto err;

	i915_gem_execbuffer_move_to_active(object_list, args->buffer_count, ring);
	i915_gem_execbuffer_retire_commands(dev, file_priv, ring);

	ret = -copyout(exec_list, (void *)(uintptr_t)args->buffers_ptr,
	    sizeof(*exec_list) * args->buffer_count);

err:
	for (i = 0; i < args->buffer_count; i++) {
		if (object_list[i] == NULL)
			break;

		if (object_list[i]->do_flags & __EXEC_OBJECT_HAS_FENCE) {
			i915_gem_object_unpin_fence(to_intel_bo(object_list[i]));
			object_list[i]->do_flags &= ~__EXEC_OBJECT_HAS_FENCE;
		}

		atomic_clearbits_int(&object_list[i]->do_flags, I915_IN_EXEC |
		    I915_EXEC_NEEDS_FENCE);
		if (i < pinned) {
			i915_gem_object_unpin(to_intel_bo(object_list[i]));
			drm_unhold_and_unref(object_list[i]);
		} else {
			drm_unref(&object_list[i]->uobj);
		}
	}

unlock:
	DRM_UNLOCK();

pre_mutex_err:
	/* update userlands reloc state. */
	ret2 = -i915_gem_put_relocs_to_user(exec_list,
	    args->buffer_count, relocs);
	if (ret2 != 0 && ret == 0)
		ret = ret2;

	drm_free(object_list);
	drm_free(exec_list);

	return ret;
}
