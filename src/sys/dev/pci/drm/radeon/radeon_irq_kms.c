/*	$OpenBSD: radeon_irq_kms.c,v 1.5 2015/01/27 03:17:36 dlg Exp $	*/
/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <dev/pci/drm/drmP.h>
#include <dev/pci/drm/drm_crtc_helper.h>
#include <dev/pci/drm/radeon_drm.h>
#include "radeon_reg.h"
#include "radeon.h"
#include "atom.h"

#define RADEON_WAIT_IDLE_TIMEOUT 200

int	 radeon_driver_irq_handler_kms(void *);
void	 radeon_driver_irq_preinstall_kms(struct drm_device *);
int	 radeon_driver_irq_postinstall_kms(struct drm_device *);
void	 radeon_driver_irq_uninstall_kms(struct drm_device *);

/**
 * radeon_driver_irq_handler_kms - irq handler for KMS
 *
 * @DRM_IRQ_ARGS: args
 *
 * This is the irq handler for the radeon KMS driver (all asics).
 * radeon_irq_process is a macro that points to the per-asic
 * irq handler callback.
 */
int
radeon_driver_irq_handler_kms(void *arg)
{
	struct drm_device *dev = arg;
	struct radeon_device *rdev = dev->dev_private;

	if (!rdev->irq.installed)
		return (0);

	return radeon_irq_process(rdev);
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
/**
 * radeon_hotplug_work_func - display hotplug work handler
 *
 * @work: work struct
 *
 * This is the hot plug event work handler (all asics).
 * The work gets scheduled from the irq handler if there
 * was a hot plug interrupt.  It walks the connector table
 * and calls the hotplug handler for each one, then sends
 * a drm hotplug event to alert userspace.
 */
void
radeon_hotplug_work_func(void *arg1)
{
	struct radeon_device *rdev = arg1;
	struct drm_device *dev = rdev->ddev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;

	if (mode_config->num_connector) {
		list_for_each_entry(connector, &mode_config->connector_list, head)
			radeon_connector_hotplug(connector);
	}
	/* Just fire off a uevent and let userspace tell us what to do */
	drm_helper_hpd_irq_event(dev);
}

/**
 * radeon_driver_irq_preinstall_kms - drm irq preinstall callback
 *
 * @dev: drm dev pointer
 *
 * Gets the hw ready to enable irqs (all asics).
 * This function disables all interrupt sources on the GPU.
 */
void radeon_driver_irq_preinstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	unsigned i;

	mtx_enter(&rdev->irq.lock);
	/* Disable *all* interrupts */
	for (i = 0; i < RADEON_NUM_RINGS; i++)
		atomic_set(&rdev->irq.ring_int[i], 0);
	for (i = 0; i < RADEON_MAX_HPD_PINS; i++)
		rdev->irq.hpd[i] = false;
	for (i = 0; i < RADEON_MAX_CRTCS; i++) {
		rdev->irq.crtc_vblank_int[i] = false;
		atomic_set(&rdev->irq.pflip[i], 0);
		rdev->irq.afmt[i] = false;
	}
	radeon_irq_set(rdev);
	mtx_leave(&rdev->irq.lock);
	/* Clear bits */
	radeon_irq_process(rdev);
}

/**
 * radeon_driver_irq_postinstall_kms - drm irq preinstall callback
 *
 * @dev: drm dev pointer
 *
 * Handles stuff to be done after enabling irqs (all asics).
 * Returns 0 on success.
 */
int radeon_driver_irq_postinstall_kms(struct drm_device *dev)
{
	dev->max_vblank_count = 0x001fffff;
	return 0;
}

/**
 * radeon_driver_irq_uninstall_kms - drm irq uninstall callback
 *
 * @dev: drm dev pointer
 *
 * This function disables all interrupt sources on the GPU (all asics).
 */
void radeon_driver_irq_uninstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	unsigned i;

	if (rdev == NULL) {
		return;
	}
	mtx_enter(&rdev->irq.lock);
	/* Disable *all* interrupts */
	for (i = 0; i < RADEON_NUM_RINGS; i++)
		atomic_set(&rdev->irq.ring_int[i], 0);
	for (i = 0; i < RADEON_MAX_HPD_PINS; i++)
		rdev->irq.hpd[i] = false;
	for (i = 0; i < RADEON_MAX_CRTCS; i++) {
		rdev->irq.crtc_vblank_int[i] = false;
		atomic_set(&rdev->irq.pflip[i], 0);
		rdev->irq.afmt[i] = false;
	}
	radeon_irq_set(rdev);
	mtx_leave(&rdev->irq.lock);
}

/**
 * radeon_msi_ok - asic specific msi checks
 *
 * @rdev: radeon device pointer
 *
 * Handles asic specific MSI checks to determine if
 * MSIs should be enabled on a particular chip (all asics).
 * Returns true if MSIs should be enabled, false if MSIs
 * should not be enabled.
 */
bool
radeon_msi_ok(struct radeon_device *rdev)
{
	/* RV370/RV380 was first asic with MSI support */
	if (rdev->family < CHIP_RV380)
		return false;

	/* MSIs don't work on AGP */
	if (rdev->flags & RADEON_IS_AGP)
		return false;

	/* force MSI on */
	if (radeon_msi == 1)
		return true;
	else if (radeon_msi == 0)
		return false;

	/* Quirks */
	/* HP RS690 only seems to work with MSIs. */
	if ((rdev->pdev->device == 0x791f) &&
	    (rdev->pdev->subsystem_vendor == 0x103c) &&
	    (rdev->pdev->subsystem_device == 0x30c2))
		return true;

	/* Dell RS690 only seems to work with MSIs. */
	if ((rdev->pdev->device == 0x791f) &&
	    (rdev->pdev->subsystem_vendor == 0x1028) &&
	    (rdev->pdev->subsystem_device == 0x01fc))
		return true;

	/* Dell RS690 only seems to work with MSIs. */
	if ((rdev->pdev->device == 0x791f) &&
	    (rdev->pdev->subsystem_vendor == 0x1028) &&
	    (rdev->pdev->subsystem_device == 0x01fd))
		return true;

	/* Gateway RS690 only seems to work with MSIs. */
	if ((rdev->pdev->device == 0x791f) &&
	    (rdev->pdev->subsystem_vendor == 0x107b) &&
	    (rdev->pdev->subsystem_device == 0x0185))
		return true;

	/* try and enable MSIs by default on all RS690s */
	if (rdev->family == CHIP_RS690)
		return true;

	/* RV515 seems to have MSI issues where it loses
	 * MSI rearms occasionally. This leads to lockups and freezes.
	 * disable it by default.
	 */
	if (rdev->family == CHIP_RV515)
		return false;
	if (rdev->flags & RADEON_IS_IGP) {
		/* APUs work fine with MSIs */
		if (rdev->family >= CHIP_PALM)
			return true;
		/* lots of IGPs have problems with MSIs */
		return false;
	}

	return true;
}

/**
 * radeon_irq_kms_init - init driver interrupt info
 *
 * @rdev: radeon device pointer
 *
 * Sets up the work irq handlers, vblank init, MSIs, etc. (all asics).
 * Returns 0 for success, error for failure.
 */
int radeon_irq_kms_init(struct radeon_device *rdev)
{
	int r = 0;

	task_set(&rdev->hotplug_task, radeon_hotplug_work_func, rdev);
	task_set(&rdev->audio_task, r600_audio_update_hdmi, rdev);

	mtx_init(&rdev->irq.lock, IPL_TTY);
	r = drm_vblank_init(rdev->ddev, rdev->num_crtc);
	if (r) {
		return r;
	}
#ifdef notyet
	/* enable msi */
	rdev->msi_enabled = 0;

	if (radeon_msi_ok(rdev)) {
		int ret = pci_enable_msi(rdev->pdev);
		if (!ret) {
			rdev->msi_enabled = 1;
			dev_info(rdev->ddev, "radeon: using MSI.\n");
		}
	}
#endif
	rdev->irq.installed = true;
	r = drm_irq_install(rdev->ddev);
	if (r) {
		rdev->irq.installed = false;
		return r;
	}
	DRM_DEBUG("radeon: irq initialized.\n");
	return 0;
}

/**
 * radeon_irq_kms_fini - tear down driver interrrupt info
 *
 * @rdev: radeon device pointer
 *
 * Tears down the work irq handlers, vblank handlers, MSIs, etc. (all asics).
 */
void radeon_irq_kms_fini(struct radeon_device *rdev)
{
	drm_vblank_cleanup(rdev->ddev);
	if (rdev->irq.installed) {
		drm_irq_uninstall(rdev->ddev);
		rdev->irq.installed = false;
#ifdef notyet
		if (rdev->msi_enabled)
			pci_disable_msi(rdev->pdev);
#endif
	}
#ifdef notyet
	flush_work(&rdev->hotplug_work);
#endif
}

/**
 * radeon_irq_kms_sw_irq_get - enable software interrupt
 *
 * @rdev: radeon device pointer
 * @ring: ring whose interrupt you want to enable
 *
 * Enables the software interrupt for a specific ring (all asics).
 * The software interrupt is generally used to signal a fence on
 * a particular ring.
 */
void radeon_irq_kms_sw_irq_get(struct radeon_device *rdev, int ring)
{
	if (!rdev->ddev->irq_enabled)
		return;

	if (atomic_inc_return(&rdev->irq.ring_int[ring]) == 1) {
		mtx_enter(&rdev->irq.lock);
		radeon_irq_set(rdev);
		mtx_leave(&rdev->irq.lock);
	}
}

/**
 * radeon_irq_kms_sw_irq_put - disable software interrupt
 *
 * @rdev: radeon device pointer
 * @ring: ring whose interrupt you want to disable
 *
 * Disables the software interrupt for a specific ring (all asics).
 * The software interrupt is generally used to signal a fence on
 * a particular ring.
 */
void radeon_irq_kms_sw_irq_put(struct radeon_device *rdev, int ring)
{
	if (!rdev->ddev->irq_enabled)
		return;

	if (atomic_dec_and_test(&rdev->irq.ring_int[ring])) {
		mtx_enter(&rdev->irq.lock);
		radeon_irq_set(rdev);
		mtx_leave(&rdev->irq.lock);
	}
}

/**
 * radeon_irq_kms_pflip_irq_get - enable pageflip interrupt
 *
 * @rdev: radeon device pointer
 * @crtc: crtc whose interrupt you want to enable
 *
 * Enables the pageflip interrupt for a specific crtc (all asics).
 * For pageflips we use the vblank interrupt source.
 */
void radeon_irq_kms_pflip_irq_get(struct radeon_device *rdev, int crtc)
{
	if (crtc < 0 || crtc >= rdev->num_crtc)
		return;

	if (!rdev->ddev->irq_enabled)
		return;

	if (atomic_inc_return(&rdev->irq.pflip[crtc]) == 1) {
		mtx_enter(&rdev->irq.lock);
		radeon_irq_set(rdev);
		mtx_leave(&rdev->irq.lock);
	}
}

/**
 * radeon_irq_kms_pflip_irq_put - disable pageflip interrupt
 *
 * @rdev: radeon device pointer
 * @crtc: crtc whose interrupt you want to disable
 *
 * Disables the pageflip interrupt for a specific crtc (all asics).
 * For pageflips we use the vblank interrupt source.
 */
void radeon_irq_kms_pflip_irq_put(struct radeon_device *rdev, int crtc)
{
	if (crtc < 0 || crtc >= rdev->num_crtc)
		return;

	if (!rdev->ddev->irq_enabled)
		return;

	if (atomic_dec_and_test(&rdev->irq.pflip[crtc])) {
		mtx_enter(&rdev->irq.lock);
		radeon_irq_set(rdev);
		mtx_leave(&rdev->irq.lock);
	}
}

/**
 * radeon_irq_kms_enable_afmt - enable audio format change interrupt
 *
 * @rdev: radeon device pointer
 * @block: afmt block whose interrupt you want to enable
 *
 * Enables the afmt change interrupt for a specific afmt block (all asics).
 */
void radeon_irq_kms_enable_afmt(struct radeon_device *rdev, int block)
{
	if (!rdev->ddev->irq_enabled)
		return;

	mtx_enter(&rdev->irq.lock);
	rdev->irq.afmt[block] = true;
	radeon_irq_set(rdev);
	mtx_leave(&rdev->irq.lock);

}

/**
 * radeon_irq_kms_disable_afmt - disable audio format change interrupt
 *
 * @rdev: radeon device pointer
 * @block: afmt block whose interrupt you want to disable
 *
 * Disables the afmt change interrupt for a specific afmt block (all asics).
 */
void radeon_irq_kms_disable_afmt(struct radeon_device *rdev, int block)
{
	if (!rdev->ddev->irq_enabled)
		return;

	mtx_enter(&rdev->irq.lock);
	rdev->irq.afmt[block] = false;
	radeon_irq_set(rdev);
	mtx_leave(&rdev->irq.lock);
}

/**
 * radeon_irq_kms_enable_hpd - enable hotplug detect interrupt
 *
 * @rdev: radeon device pointer
 * @hpd_mask: mask of hpd pins you want to enable.
 *
 * Enables the hotplug detect interrupt for a specific hpd pin (all asics).
 */
void radeon_irq_kms_enable_hpd(struct radeon_device *rdev, unsigned hpd_mask)
{
	int i;

	if (!rdev->ddev->irq_enabled)
		return;

	mtx_enter(&rdev->irq.lock);
	for (i = 0; i < RADEON_MAX_HPD_PINS; ++i)
		rdev->irq.hpd[i] |= !!(hpd_mask & (1 << i));
	radeon_irq_set(rdev);
	mtx_leave(&rdev->irq.lock);
}

/**
 * radeon_irq_kms_disable_hpd - disable hotplug detect interrupt
 *
 * @rdev: radeon device pointer
 * @hpd_mask: mask of hpd pins you want to disable.
 *
 * Disables the hotplug detect interrupt for a specific hpd pin (all asics).
 */
void radeon_irq_kms_disable_hpd(struct radeon_device *rdev, unsigned hpd_mask)
{
	int i;

	if (!rdev->ddev->irq_enabled)
		return;

	mtx_enter(&rdev->irq.lock);
	for (i = 0; i < RADEON_MAX_HPD_PINS; ++i)
		rdev->irq.hpd[i] &= !(hpd_mask & (1 << i));
	radeon_irq_set(rdev);
	mtx_leave(&rdev->irq.lock);
}

