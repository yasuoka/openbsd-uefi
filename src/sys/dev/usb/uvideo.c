/*	$OpenBSD: uvideo.c,v 1.67 2008/08/01 08:20:26 mglocker Exp $ */

/*
 * Copyright (c) 2008 Robert Nagy <robert@openbsd.org>
 * Copyright (c) 2008 Marcus Glocker <mglocker@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <sys/reboot.h>
#include <sys/selinfo.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/stat.h>
#include <sys/device.h>
#include <sys/poll.h>
#include <uvm/uvm.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/uvideo.h>

#include <dev/video_if.h>

#ifdef UVIDEO_DEBUG
int uvideo_debug = 1;
#define DPRINTF(l, x...) do { if ((l) <= uvideo_debug) printf(x); } while (0)
#else
#define DPRINTF(l, x...)
#endif

int		uvideo_enable(void *);
void		uvideo_disable(void *);
int		uvideo_open(void *, int, int *, uint8_t *, void (*)(void *),
		    void *arg);
int		uvideo_close(void *);

int             uvideo_match(struct device *, void *, void *);
void            uvideo_attach(struct device *, struct device *, void *);
int             uvideo_detach(struct device *, int);
int             uvideo_activate(struct device *, enum devact);

int		uvideo_vc_parse_desc(struct uvideo_softc *);
int		uvideo_vc_parse_desc_header(struct uvideo_softc *,
		    const usb_descriptor_t *);

int		uvideo_vs_parse_desc(struct uvideo_softc *,
		    struct usb_attach_arg *, usb_config_descriptor_t *);
int		uvideo_vs_parse_desc_input_header(struct uvideo_softc *,
		    const usb_descriptor_t *);
int		uvideo_vs_parse_desc_format(struct uvideo_softc *);
int		uvideo_vs_parse_desc_format_mjpeg(struct uvideo_softc *,
		    const usb_descriptor_t *);
int		uvideo_vs_parse_desc_format_uncompressed(struct uvideo_softc *,
		    const usb_descriptor_t *);
int		uvideo_vs_parse_desc_frame(struct uvideo_softc *);
int		uvideo_vs_parse_desc_frame_mjpeg(struct uvideo_softc *,
		    const usb_descriptor_t *);
int		uvideo_vs_parse_desc_frame_uncompressed(struct uvideo_softc *,
		    const usb_descriptor_t *);
int		uvideo_vs_parse_desc_alt(struct uvideo_softc *,
		    struct usb_attach_arg *uaa, int, int, int);
int		uvideo_vs_set_alt(struct uvideo_softc *, usbd_interface_handle,
		    int);
int		uvideo_desc_len(const usb_descriptor_t *, int, int, int, int);
int		uvideo_find_res(struct uvideo_softc *, int, int, int,
		    struct uvideo_res *);

usbd_status	uvideo_vs_negotiation(struct uvideo_softc *, int);
usbd_status	uvideo_vs_set_probe(struct uvideo_softc *, uint8_t *);
usbd_status	uvideo_vs_get_probe(struct uvideo_softc *, uint8_t *, uint8_t);
usbd_status	uvideo_vs_set_commit(struct uvideo_softc *, uint8_t *);
usbd_status	uvideo_vs_alloc_sample(struct uvideo_softc *);
void		uvideo_vs_free_sample(struct uvideo_softc *);
usbd_status	uvideo_vs_alloc(struct uvideo_softc *);
void		uvideo_vs_free(struct uvideo_softc *);
usbd_status	uvideo_vs_open(struct uvideo_softc *);
void		uvideo_vs_close(struct uvideo_softc *);
usbd_status	uvideo_vs_init(struct uvideo_softc *);
void		uvideo_vs_start(struct uvideo_softc *);
void		uvideo_vs_cb(usbd_xfer_handle, usbd_private_handle,
		    usbd_status);
int		uvideo_vs_decode_stream_header(struct uvideo_softc *,
		    uint8_t *, int); 
int		uvideo_mmap_queue(struct uvideo_softc *, uint8_t *, int);
int		uvideo_read(struct uvideo_softc *, uint8_t *, int);
#ifdef UVIDEO_DEBUG
void		uvideo_dump_desc_all(struct uvideo_softc *);
void		uvideo_dump_desc_vc_header(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_input_header(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_input(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_output(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_endpoint(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_interface(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_config(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_cs_endpoint(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_colorformat(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_frame_mjpeg(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_format_mjpeg(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_format_uncompressed(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_frame_uncompressed(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_extension(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_hexdump(void *, int, int);
int		uvideo_debug_file_open(struct uvideo_softc *);
void		uvideo_debug_file_write_sample(void *);
#endif

/*
 * IOCTL's
 */
int		uvideo_querycap(void *, struct v4l2_capability *);
int		uvideo_enum_fmt(void *, struct v4l2_fmtdesc *);
int		uvideo_s_fmt(void *, struct v4l2_format *);
int		uvideo_g_fmt(void *, struct v4l2_format *);
int		uvideo_enum_input(void *, struct v4l2_input *);
int		uvideo_s_input(void *, int);
int		uvideo_reqbufs(void *, struct v4l2_requestbuffers *);
int		uvideo_querybuf(void *, struct v4l2_buffer *);
int		uvideo_qbuf(void *, struct v4l2_buffer *);
int		uvideo_dqbuf(void *, struct v4l2_buffer *);
int		uvideo_streamon(void *, int);
int		uvideo_streamoff(void *, int);
int		uvideo_try_fmt(void *, struct v4l2_format *);

/*
 * Other hardware interface related functions
 */
caddr_t		uvideo_mappage(void *, off_t, int);
int		uvideo_get_bufsize(void *);
int		uvideo_start_read(void *);

#define DEVNAME(_s) ((_s)->sc_dev.dv_xname)

const struct cfattach uvideo_ca = {
	sizeof(struct uvideo_softc),
	uvideo_match,
	uvideo_attach,
	uvideo_detach,
	uvideo_activate,
};

struct cfdriver uvideo_cd = {
	NULL,
	"uvideo",
	DV_DULL
};

usbd_status 
uvideo_usb_request(struct uvideo_softc * sc, u_int8_t type, u_int8_t request,
    u_int16_t value, u_int16_t index, u_int16_t length, u_int8_t * data);

struct video_hw_if uvideo_hw_if = {
	uvideo_open,		/* open */
	uvideo_close,		/* close */
	uvideo_querycap,	/* VIDIOC_QUERYCAP */
	uvideo_enum_fmt,	/* VIDIOC_ENUM_FMT */
	uvideo_s_fmt,		/* VIDIOC_S_FMT */
	uvideo_g_fmt,		/* VIDIOC_G_FMT */
	uvideo_enum_input,	/* VIDIOC_ENUMINPUT */
	uvideo_s_input,		/* VIDIOC_S_INPUT */
	uvideo_reqbufs,		/* VIDIOC_REQBUFS */
	uvideo_querybuf,	/* VIDIOC_QUERYBUF */
	uvideo_qbuf,		/* VIDIOC_QBUF */
	uvideo_dqbuf,		/* VIDIOC_DQBUF */
	uvideo_streamon,	/* VIDIOC_STREAMON */
	uvideo_streamoff,	/* VIDIOC_STREAMOFF */
	uvideo_try_fmt,		/* VIDIOC_TRY_FMT */
	NULL,			/* VIDIOC_QUERYCTRL */
	uvideo_mappage,		/* mmap */
	uvideo_get_bufsize,	/* read */
	uvideo_start_read	/* start stream for read */
};

int
uvideo_enable(void *v)
{
	struct uvideo_softc *sc = v;

	DPRINTF(1, "%s: uvideo_enable sc=%p\n", DEVNAME(sc), sc);

	if (sc->sc_dying)
		return (EIO);

	if (sc->sc_enabled)
		return (EBUSY);

	sc->sc_enabled = 1;

	return (0);
}

void
uvideo_disable(void *v)
{
	struct uvideo_softc *sc = v;

	DPRINTF(1, "%s: uvideo_disable sc=%p\n", DEVNAME(sc), sc);

	if (!sc->sc_enabled) {
		printf("uvideo_disable: already disabled!\n");
		return;
	}

	sc->sc_enabled = 0;
}

int
uvideo_open(void *addr, int flags, int *size, uint8_t *buffer,
    void (*intr)(void *), void *arg)
{
	struct uvideo_softc *sc = addr;

	DPRINTF(1, "%s: uvideo_open: sc=%p\n", DEVNAME(sc), sc);

	if (sc->sc_dying)
		return (EIO);

	/* pointers to upper layer which we need */
	sc->sc_uplayer_arg = arg;
	sc->sc_uplayer_fsize = size;
	sc->sc_uplayer_fbuffer = buffer;
	sc->sc_uplayer_intr = intr;

	sc->sc_mmap_flag = 0;
	sc->sc_negotiated_flag = 0;

	return (0);
}

int
uvideo_close(void *addr)
{
	struct uvideo_softc *sc = addr;

	DPRINTF(1, "%s: uvideo_close: sc=%p\n", DEVNAME(sc), sc);

	/* close video stream pipe */
	uvideo_vs_close(sc);

	/* free video stream xfer buffer */
	uvideo_vs_free(sc);

	/* free video stream sample buffer */
	uvideo_vs_free_sample(sc);
#ifdef UVIDEO_DUMP
	usb_rem_task(sc->sc_udev, &sc->sc_task_write);
#endif
	return (0);
}

/*
 * Some devices do not report themselfs as UVC compatible although
 * they are.  They report UICLASS_VENDOR in the bInterfaceClass
 * instead of UICLASS_VIDEO.  Give those devices a chance to attach
 * by looking up their USB ID.
 *
 * If the device also doesn't set UDCLASS_VIDEO you need to add an
 * entry in usb_quirks.c, too, so the ehci disown works.
 */
static const struct usb_devno uvideo_quirk_devs [] = {
	{ USB_VENDOR_LOGITECH,	USB_PRODUCT_LOGITECH_QUICKCAMOEM_1 }
};

int
uvideo_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	id = usbd_get_interface_descriptor(uaa->iface);
	if (id == NULL)
		return (UMATCH_NONE);

	if (id->bInterfaceClass == UICLASS_VIDEO &&
	    id->bInterfaceSubClass == UISUBCLASS_VIDEOCONTROL)
		return (UMATCH_VENDOR_PRODUCT_CONF_IFACE);

	if (usb_lookup(uvideo_quirk_devs, uaa->vendor, uaa->product) != NULL &&
	    id->bInterfaceClass == UICLASS_VENDOR &&
	    id->bInterfaceSubClass == UISUBCLASS_VIDEOCONTROL)
		return (UMATCH_VENDOR_PRODUCT_CONF_IFACE);

	return (UMATCH_NONE);
}

void
uvideo_attach(struct device *parent, struct device *self, void *aux)
{
	struct uvideo_softc *sc = (struct uvideo_softc *) self;
	struct usb_attach_arg *uaa = aux;
	usb_config_descriptor_t *cdesc;
	usbd_status error;

	sc->sc_udev = uaa->device;

	if (uaa->device->bus->usbrev == USBREV_2_0) {
		printf("%s: ehci(4) does not support isochronous transfers "
		    "yet, disable it.\n", DEVNAME(sc));
		return; 
	}

	/* get the config descriptor */
	cdesc = usbd_get_config_descriptor(sc->sc_udev);
	if (cdesc == NULL) {
		printf("%s: failed to get configuration descriptor\n",
		    DEVNAME(sc));
		return;
	}
#ifdef UVIDEO_DEBUG
	uvideo_dump_desc_all(sc);
#endif
	/* parse video control descriptors */
	error = uvideo_vc_parse_desc(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/* parse video stream descriptors */
	error = uvideo_vs_parse_desc(sc, uaa, cdesc);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/* set default video stream interface */
	error = usbd_set_interface(sc->sc_vs_curr->ifaceh, 0);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/* do device negotiation without commit */
	error = uvideo_vs_negotiation(sc, 0);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/* init mmap queue */
	SIMPLEQ_INIT(&sc->sc_mmap_q);
	sc->sc_mmap_cur = 0;
	sc->sc_mmap_count = 0;

	usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, sc->sc_udev, &sc->sc_dev);

	DPRINTF(1, "uvideo_attach: doing video_attach_mi\n");
	sc->sc_videodev = video_attach_mi(&uvideo_hw_if, sc, &sc->sc_dev);
}

int
uvideo_detach(struct device *self, int flags)
{
	struct uvideo_softc *sc = (struct uvideo_softc *)self;
	int rv = 0;

	/* Wait for outstanding requests to complete */
	usbd_delay_ms(sc->sc_udev, UVIDEO_NFRAMES_MAX);

	uvideo_vs_free_sample(sc);

	if (sc->sc_videodev != NULL)
		rv = config_detach(sc->sc_videodev, flags);

	usbd_add_drv_event(USB_EVENT_DRIVER_DETACH, sc->sc_udev, &sc->sc_dev);

	return (rv);
}

int
uvideo_activate(struct device *self, enum devact act)
{
	struct uvideo_softc *sc = (struct uvideo_softc *) self;
	int rv = 0;

	DPRINTF(1, "uvideo_activate: sc=%p\n", sc);

	switch (act) {
	case DVACT_ACTIVATE:
		break;
	case DVACT_DEACTIVATE:
		if (sc->sc_videodev != NULL)
			config_deactivate(sc->sc_videodev);
		sc->sc_dying = 1;
		break;
	}

	return (rv);
}

int
uvideo_vc_parse_desc(struct uvideo_softc *sc)
{
	usbd_desc_iter_t iter;
	const usb_descriptor_t *desc;
	int vc_header_found;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	vc_header_found = 0;

	usb_desc_iter_init(sc->sc_udev, &iter);
	desc = usb_desc_iter_next(&iter);
	while (desc) {
		if (desc->bDescriptorType != UDESC_CS_INTERFACE) {
			desc = usb_desc_iter_next(&iter);
			continue;
		}

		switch (desc->bDescriptorSubtype) {
		case UDESCSUB_VC_HEADER:
			if (!uvideo_desc_len(desc, 12, 11, 1, 0))
				break;
			if (vc_header_found) {
				printf("%s: too many VC_HEADERs!\n",
				    DEVNAME(sc));
				return (-1);
			}
			if (uvideo_vc_parse_desc_header(sc, desc) != 0)
				return (-1);
			vc_header_found = 1;
			break;

		/* TODO: which VC descriptors do we need else? */
		}

		desc = usb_desc_iter_next(&iter);
	}

	if (vc_header_found == 0) {
		printf("%s: no VC_HEADER found!\n", DEVNAME(sc));
		return (-1);
	}

	return (0);
}

int
uvideo_vc_parse_desc_header(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_header_desc *d;

	d = (struct usb_video_header_desc *)(uint8_t *)desc;

	if (d->bInCollection == 0) {
		printf("%s: no VS interface found!\n",
		    DEVNAME(sc));
		return (-1);
	}
	
	sc->sc_desc_vc_header.fix = d;
	sc->sc_desc_vc_header.baInterfaceNr = (uByte *)(d + 1);

	return (0);
}

int
uvideo_vs_parse_desc(struct uvideo_softc *sc, struct usb_attach_arg *uaa,
    usb_config_descriptor_t *cdesc)
{
	usbd_desc_iter_t iter;
	const usb_descriptor_t *desc;
	usb_interface_descriptor_t *id;
	int i, iface, numalts;
	usbd_status error;

	DPRINTF(1, "%s: number of total interfaces=%d\n",
	    DEVNAME(sc), uaa->nifaces);
	DPRINTF(1, "%s: number of VS interfaces=%d\n",
	    DEVNAME(sc), sc->sc_desc_vc_header.fix->bInCollection);

	usb_desc_iter_init(sc->sc_udev, &iter);
	desc = usb_desc_iter_next(&iter);
	while (desc) {
		if (desc->bDescriptorType != UDESC_CS_INTERFACE) {
			desc = usb_desc_iter_next(&iter);
			continue;
		}

		switch (desc->bDescriptorSubtype) {
		case UDESCSUB_VS_INPUT_HEADER:
			if (!uvideo_desc_len(desc, 13, 3, 0, 12))
				break;
			if (uvideo_vs_parse_desc_input_header(sc, desc) != 0)
				return (-1);
			break;

		/* TODO: which VS descriptors do we need else? */
		}

		desc = usb_desc_iter_next(&iter);
	}

	/* parse video stream format descriptors */
	error = uvideo_vs_parse_desc_format(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	/* parse video stream frame descriptors */
	error = uvideo_vs_parse_desc_frame(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	/* parse interface collection */
	for (i = 0; i < sc->sc_desc_vc_header.fix->bInCollection; i++) {
		iface = sc->sc_desc_vc_header.baInterfaceNr[i];

		id = usbd_get_interface_descriptor(uaa->ifaces[iface]);
		if (id == NULL) {
			printf("%s: can't get VS interface %d!\n",
			    DEVNAME(sc), iface);
			return (USBD_INVAL);
		}

		numalts = usbd_get_no_alts(cdesc, id->bInterfaceNumber);

		DPRINTF(1, "%s: VS interface %d, ", DEVNAME(sc), i);
		DPRINTF(1, "bInterfaceNumber=0x%02x, numalts=%d\n",
		    id->bInterfaceNumber, numalts);

		error = uvideo_vs_parse_desc_alt(sc, uaa, i, iface, numalts);
		if (error != USBD_NORMAL_COMPLETION)
			return (error);
	}

	/* XXX for now always use the first video stream */
	sc->sc_vs_curr = &sc->sc_vs_coll[0];

	return (USBD_NORMAL_COMPLETION);
}

int
uvideo_vs_parse_desc_input_header(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_input_header_desc *d;

	d = (struct usb_video_input_header_desc *)(uint8_t *)desc;

	/* on some devices bNumFormats is larger than the truth */
	if (d->bNumFormats == 0) {
		printf("%s: no INPUT FORMAT descriptors found!\n", DEVNAME(sc));
		return (-1);
	}

	sc->sc_desc_vs_input_header.fix = d;
	sc->sc_desc_vs_input_header.bmaControls = (uByte *)(d + 1);

	return (0);
}

int
uvideo_vs_parse_desc_format(struct uvideo_softc *sc)
{
	usbd_desc_iter_t iter;
	const usb_descriptor_t *desc;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	usb_desc_iter_init(sc->sc_udev, &iter);
	desc = usb_desc_iter_next(&iter);
	while (desc) {
		if (desc->bDescriptorType != UDESC_CS_INTERFACE) {
			desc = usb_desc_iter_next(&iter);
			continue;
		}

		switch (desc->bDescriptorSubtype) {
		case UDESCSUB_VS_FORMAT_MJPEG:
			if (desc->bLength == 11) {
				uvideo_vs_parse_desc_format_mjpeg(sc, desc);
			}
			break;
		case UDESCSUB_VS_FORMAT_UNCOMPRESSED:
			if (desc->bLength == 27) {
				uvideo_vs_parse_desc_format_uncompressed(sc,
				    desc);
			}
			break;
		}

		desc = usb_desc_iter_next(&iter);
	}

	sc->sc_fmtgrp_idx = 0;

	if (sc->sc_fmtgrp_num == 0) {
		printf("%s: no format descriptors found!\n", DEVNAME(sc));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: number of total format descriptors=%d\n",
	    DEVNAME(sc), sc->sc_fmtgrp_num);

	return (USBD_NORMAL_COMPLETION);
}

int
uvideo_vs_parse_desc_format_mjpeg(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_mjpeg_desc *d;

	d = (struct usb_video_format_mjpeg_desc *)(uint8_t *)desc;

	if (d->bNumFrameDescriptors == 0) {
		printf("%s: no MJPEG frame descriptors available!\n",
		    DEVNAME(sc));
		return (-1);
	}

	if (sc->sc_fmtgrp_idx > UVIDEO_MAX_FORMAT) {
		printf("%s: too many format descriptors found!\n", DEVNAME(sc));
		return (-1);
	}

	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format =
	    (struct uvideo_format_desc *)d;
	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format_dfidx =
	    sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format->u.mjpeg.bDefaultFrameIndex;
	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].pixelformat = V4L2_PIX_FMT_MJPEG;

	if (sc->sc_fmtgrp_cur == NULL)
		/* set MJPEG format */
		sc->sc_fmtgrp_cur = &sc->sc_fmtgrp[sc->sc_fmtgrp_idx];

	sc->sc_fmtgrp_idx++;
	sc->sc_fmtgrp_num++;

	return (0);
}

int
uvideo_vs_parse_desc_format_uncompressed(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_uncompressed_desc *d;
	int i;

	d = (struct usb_video_format_uncompressed_desc *)(uint8_t *)desc;

	if (d->bNumFrameDescriptors == 0) {
		printf("%s: no UNCOMPRESSED frame descriptors available!\n",
		    DEVNAME(sc));
		return (-1);
	}

	if (sc->sc_fmtgrp_idx > UVIDEO_MAX_FORMAT) {
		printf("%s: too many format descriptors found!\n", DEVNAME(sc));
		return (-1);
	}

	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format =
	    (struct uvideo_format_desc *)d;
	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format_dfidx =
	    sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format->u.uc.bDefaultFrameIndex;
	i = sc->sc_fmtgrp_idx;
	if (!strcmp(sc->sc_fmtgrp[i].format->u.uc.guidFormat, "YUY2")) {
		sc->sc_fmtgrp[i].pixelformat = V4L2_PIX_FMT_YUYV;
	} else if (!strcmp(sc->sc_fmtgrp[i].format->u.uc.guidFormat, "NV12")) {
		sc->sc_fmtgrp[i].pixelformat = V4L2_PIX_FMT_NV12;
	} else {
		sc->sc_fmtgrp[i].pixelformat = 0;
	}

	if (sc->sc_fmtgrp_cur == NULL)
		/* set UNCOMPRESSED format */
		sc->sc_fmtgrp_cur = &sc->sc_fmtgrp[sc->sc_fmtgrp_idx];

	sc->sc_fmtgrp_idx++;
	sc->sc_fmtgrp_num++;

	return (0);
}

int
uvideo_vs_parse_desc_frame(struct uvideo_softc *sc)
{
	usbd_desc_iter_t iter;
	const usb_descriptor_t *desc;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	usb_desc_iter_init(sc->sc_udev, &iter);
	desc = usb_desc_iter_next(&iter);
	while (desc) {
		if (desc->bDescriptorType != UDESC_CS_INTERFACE) {
			desc = usb_desc_iter_next(&iter);
			continue;
		}

		switch (desc->bDescriptorSubtype) {
		case UDESCSUB_VS_FRAME_MJPEG:
			if (uvideo_vs_parse_desc_frame_mjpeg(sc, desc))
				return (1);
			break;
		case UDESCSUB_VS_FRAME_UNCOMPRESSED:
			/* XXX do correct length calculation */
			if (desc->bLength > 25) {
				if (uvideo_vs_parse_desc_frame_uncompressed(sc,
				    desc))
					return (1);
			}
			break;
		}

		desc = usb_desc_iter_next(&iter);
	}

	return (0);
}

int
uvideo_vs_parse_desc_frame_mjpeg(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_frame_mjpeg_desc *d;
	int fmtidx;

	d = (struct usb_video_frame_mjpeg_desc *)(uint8_t *)desc;

	if (d->bFrameIndex == UVIDEO_MAX_FRAME) {
		printf("%s: too many MJPEG frame descriptors found!\n",
		    DEVNAME(sc));
		return (1);
	}

	fmtidx = sc->sc_fmtgrp_idx;
	sc->sc_fmtgrp[fmtidx].frame[d->bFrameIndex] = d;

	/*
	 * If bDefaultFrameIndex is not set by the device
	 * use the first bFrameIndex available, otherwise
	 * set it to the default one.
	 */
	if (sc->sc_fmtgrp[fmtidx].format->u.mjpeg.bDefaultFrameIndex == 0) {
		sc->sc_fmtgrp[fmtidx].frame_cur =
		    sc->sc_fmtgrp[fmtidx].frame[1];
	} else if (sc->sc_fmtgrp[fmtidx].format->u.mjpeg.bDefaultFrameIndex ==
	    d->bFrameIndex) {
		sc->sc_fmtgrp[fmtidx].frame_cur =
		    sc->sc_fmtgrp[fmtidx].frame[d->bFrameIndex];
	}

	sc->sc_fmtgrp[fmtidx].frame_num++;

	if (d->bFrameIndex ==
	    sc->sc_fmtgrp[fmtidx].format->bNumFrameDescriptors)
		sc->sc_fmtgrp_idx++;

	return (0);
}

int
uvideo_vs_parse_desc_frame_uncompressed(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_frame_uncompressed_desc *d;
	int fmtidx;

	d = (struct usb_video_frame_uncompressed_desc *)(uint8_t *)desc;

	if (d->bFrameIndex == UVIDEO_MAX_FRAME) {
		printf("%s: too many UNCOMPRESSED frame descriptors found!\n",
		    DEVNAME(sc));
		return (1);
	}

	fmtidx = sc->sc_fmtgrp_idx;
	sc->sc_fmtgrp[fmtidx].frame[d->bFrameIndex] =
	    (struct usb_video_frame_mjpeg_desc *)d;

	/*
	 * If bDefaultFrameIndex is not set by the device
	 * use the first bFrameIndex available, otherwise
	 * set it to the default one.
	 */
	if (sc->sc_fmtgrp[fmtidx].format->u.uc.bDefaultFrameIndex == 0) {
		sc->sc_fmtgrp[fmtidx].frame_cur =
		    sc->sc_fmtgrp[fmtidx].frame[1];
	} else if (sc->sc_fmtgrp[fmtidx].format->u.uc.bDefaultFrameIndex ==
	    d->bFrameIndex) {
		sc->sc_fmtgrp[fmtidx].frame_cur =
		    sc->sc_fmtgrp[fmtidx].frame[d->bFrameIndex];
	}

	sc->sc_fmtgrp[fmtidx].frame_num++;

	if (d->bFrameIndex ==
	    sc->sc_fmtgrp[fmtidx].format->bNumFrameDescriptors)
		sc->sc_fmtgrp_idx++;

	return (0);
}

int
uvideo_vs_parse_desc_alt(struct uvideo_softc *sc, struct usb_attach_arg *uaa,
    int vs_nr, int iface, int numalts)
{
	struct uvideo_vs_iface *vs;
	usbd_desc_iter_t iter;
	const usb_descriptor_t *desc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;

	vs = &sc->sc_vs_coll[vs_nr];

	usb_desc_iter_init(sc->sc_udev, &iter);
	desc = usb_desc_iter_next(&iter);
	while (desc) {
		/* find video stream interface */
		if (desc->bDescriptorType != UDESC_INTERFACE)
			goto next;
		id = (usb_interface_descriptor_t *)(uint8_t *)desc;
		if (id->bInterfaceNumber != iface)
			goto next;
		DPRINTF(1, "%s: bAlternateSetting=0x%02x, ",
		    DEVNAME(sc), id->bAlternateSetting);
		if (id->bNumEndpoints == 0) {
			DPRINTF(1, "no endpoint descriptor\n");
			goto next;
		}

		/* jump to corresponding endpoint descriptor */
		while ((desc = usb_desc_iter_next(&iter))) {
			if (desc->bDescriptorType == UDESC_ENDPOINT)
				break;
		}
		ed = (usb_endpoint_descriptor_t *)(uint8_t *)desc;
		DPRINTF(1, "bEndpointAddress=0x%02x, ", ed->bEndpointAddress);
		DPRINTF(1, "wMaxPacketSize=%d\n", UGETW(ed->wMaxPacketSize));

		/* we just support isoc endpoints yet */
		if (UE_GET_XFERTYPE(ed->bmAttributes) != UE_ISOCHRONOUS)
			goto next;

		/* save endpoint with largest bandwidth */
		if (UGETW(ed->wMaxPacketSize) > vs->max_packet_size) {
			vs->ifaceh = uaa->ifaces[iface];
			vs->endpoint = ed->bEndpointAddress;
			vs->numalts = numalts;
			vs->curalt = id->bAlternateSetting;
			vs->max_packet_size = UGETW(ed->wMaxPacketSize);
			vs->iface = iface;
		}
next:
		desc = usb_desc_iter_next(&iter);
	}

	/* check if we have found a valid alternate interface */
	if (vs->ifaceh == NULL) {
		printf("%s: no valid alternate interface found!\n",
		    DEVNAME(sc));
		return (USBD_INVAL);
	}

	return (USBD_NORMAL_COMPLETION);
}

int
uvideo_vs_set_alt(struct uvideo_softc *sc, usbd_interface_handle ifaceh,
    int max_packet_size)
{
	usbd_desc_iter_t iter;
	const usb_descriptor_t *desc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i;
	usbd_status error;

	i = 0;
	usb_desc_iter_init(sc->sc_udev, &iter);
	desc = usb_desc_iter_next(&iter);
	while (desc) {
		/* find video stream interface */
		if (desc->bDescriptorType != UDESC_INTERFACE)
			goto next;
		id = (usb_interface_descriptor_t *)(uint8_t *)desc;
		if (id->bInterfaceNumber != sc->sc_vs_curr->iface)
			goto next;
		if (id->bNumEndpoints == 0)
			goto next;

		/* jump to corresponding endpoint descriptor */
		desc = usb_desc_iter_next(&iter);
		if (desc->bDescriptorType != UDESC_ENDPOINT)
			goto next;
		ed = (usb_endpoint_descriptor_t *)(uint8_t *)desc;
		i++;

		/* save endpoint with requested bandwidth */
		if (UGETW(ed->wMaxPacketSize) == max_packet_size) {
			sc->sc_vs_curr->endpoint = ed->bEndpointAddress;
			sc->sc_vs_curr->curalt = id->bAlternateSetting;
			sc->sc_vs_curr->max_packet_size =
			    UGETW(ed->wMaxPacketSize);
			DPRINTF(1, "%s: set alternate iface to ", DEVNAME(sc));
			DPRINTF(1, "bAlternateSetting=0x%02x\n",
			    id->bAlternateSetting);
			break;
		}
next:
		desc = usb_desc_iter_next(&iter);
	}

	/* set alternate video stream interface */
	error = usbd_set_interface(ifaceh, i);
	if (error) {
		printf("%s: could not set alternate interface %d!\n",
		    DEVNAME(sc), i);
		return (USBD_INVAL);
	}

	return (USBD_NORMAL_COMPLETION);
}

/*
 * Thanks to the retarded USB Video Class specs there are different
 * descriptors types with the same bDescriptorSubtype which makes
 * it necessary to differ between those types by doing descriptor
 * size dances :-(
 *
 * size_fix:		total size of the fixed structure part
 * off_num_elements:	offset which tells the number of following elements
 * size_element:	size of a single element
 * off_size_element:	if size_element is 0 the element size is taken from
 *			this offset in the descriptor 
 */
int
uvideo_desc_len(const usb_descriptor_t *desc,
    int size_fix, int off_num_elements, int size_element, int off_size_element)
{
	uint8_t *buf;
	int size_elements, size_total;

	if (desc->bLength < size_fix)
		return (0);

	buf = (uint8_t *)desc;

	if (size_element == 0)
		size_element = buf[off_size_element];

	size_elements = buf[off_num_elements] * size_element;
	size_total = size_fix + size_elements;

	if (desc->bLength == size_total && size_elements != 0)
		return (1);

	return (0);
}

/*
 * Find the next best matching resolution which we can offer and
 * return it.
 */
int
uvideo_find_res(struct uvideo_softc *sc, int idx, int width, int height,
    struct uvideo_res *r)
{
	int i, w, h, diff, diff_best, size_want, size_is;

	size_want = width * height;

	for (i = 1; i <= sc->sc_fmtgrp[idx].frame_num; i++) {
		w = UGETW(sc->sc_fmtgrp[idx].frame[i]->wWidth);
		h = UGETW(sc->sc_fmtgrp[idx].frame[i]->wHeight);
		size_is = w * h;
		if (size_is > size_want)
			diff = size_is - size_want;
		else
			diff = size_want - size_is;
		if (i == 1)
			diff_best = diff;
		if (diff <= diff_best) {
			diff_best = diff;
			r->width = w;
			r->height = h;
			r->fidx = i;
		}
		DPRINTF(1, "%s: %s: frame index %d: width=%d, height=%d\n",
		    DEVNAME(sc), __func__, i, w, h);
	}

	return (0);
}

usbd_status
uvideo_vs_negotiation(struct uvideo_softc *sc, int commit)
{
	struct usb_video_probe_commit *pc;
	uint8_t probe_data[34];
	usbd_status error;

	pc = (struct usb_video_probe_commit *)probe_data;

	/* get probe */
	bzero(probe_data, sizeof(probe_data));
	error = uvideo_vs_get_probe(sc, probe_data, GET_DEF);
	if (error != USBD_NORMAL_COMPLETION) {
		error = uvideo_vs_get_probe(sc, probe_data, GET_CUR);
		if (error != USBD_NORMAL_COMPLETION)
			return (error);
	}

	/* set probe */
	pc->bFormatIndex = sc->sc_fmtgrp_cur->format->bFormatIndex;
	pc->bFrameIndex = sc->sc_fmtgrp_cur->format_dfidx;
	USETDW(pc->dwFrameInterval,
	    UGETDW(sc->sc_fmtgrp_cur->frame_cur->dwDefaultFrameInterval));
	USETDW(pc->dwMaxVideoFrameSize, 0);
	USETDW(pc->dwMaxPayloadTransferSize, 0);
	error = uvideo_vs_set_probe(sc, probe_data);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	/* get probe */
	bzero(probe_data, sizeof(probe_data));
	error = uvideo_vs_get_probe(sc, probe_data, GET_CUR);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	/* commit */
	if (commit) {
		error = uvideo_vs_set_commit(sc, probe_data);
		if (error != USBD_NORMAL_COMPLETION)
			return (error);
	}

	/* save a copy of probe commit */
	bcopy(pc, &sc->sc_desc_probe, sizeof(sc->sc_desc_probe));

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_set_probe(struct uvideo_softc *sc, uint8_t *probe_data)
{
	usb_device_request_t req;
	usbd_status err;
	uint16_t tmp;
	struct usb_video_probe_commit *pc;

	req.bmRequestType = UVIDEO_SET_IF;
	req.bRequest = SET_CUR;
	tmp = VS_PROBE_CONTROL;
	tmp = tmp << 8;
	USETW(req.wValue, tmp);
	USETW(req.wIndex, sc->sc_vs_curr->iface);
	USETW(req.wLength, 26);

	pc = (struct usb_video_probe_commit *)probe_data;

	err = usbd_do_request(sc->sc_udev, &req, probe_data);
	if (err) {
		printf("%s: could not SET probe request: %s\n",
		    DEVNAME(sc), usbd_errstr(err));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: SET probe request successfully\n", DEVNAME(sc));

	DPRINTF(1, "bmHint=0x%02x\n", UGETW(pc->bmHint));
	DPRINTF(1, "bFormatIndex=0x%02x\n", pc->bFormatIndex);
	DPRINTF(1, "bFrameIndex=0x%02x\n", pc->bFrameIndex);
	DPRINTF(1, "dwFrameInterval=%d (ns)\n", UGETDW(pc->dwFrameInterval));
	DPRINTF(1, "wKeyFrameRate=%d\n", UGETW(pc->wKeyFrameRate));
	DPRINTF(1, "wPFrameRate=%d\n", UGETW(pc->wPFrameRate));
	DPRINTF(1, "wCompQuality=%d\n", UGETW(pc->wCompQuality));
	DPRINTF(1, "wCompWindowSize=%d\n", UGETW(pc->wCompWindowSize));
	DPRINTF(1, "wDelay=%d (ms)\n", UGETW(pc->wDelay));
	DPRINTF(1, "dwMaxVideoFrameSize=%d (bytes)\n",
	    UGETDW(pc->dwMaxVideoFrameSize));
	DPRINTF(1, "dwMaxPayloadTransferSize=%d (bytes)\n",
	    UGETDW(pc->dwMaxPayloadTransferSize));

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_get_probe(struct uvideo_softc *sc, uint8_t *probe_data,
    uint8_t request)
{
	usb_device_request_t req;
	usbd_status err;
	uint16_t tmp;
	struct usb_video_probe_commit *pc;

	req.bmRequestType = UVIDEO_GET_IF;
	req.bRequest = request;
	tmp = VS_PROBE_CONTROL;
	tmp = tmp << 8;
	USETW(req.wValue, tmp);
	USETW(req.wIndex, sc->sc_vs_curr->iface);
	USETW(req.wLength, 26);

	pc = (struct usb_video_probe_commit *)probe_data;

	err = usbd_do_request(sc->sc_udev, &req, probe_data);
	if (err) {
		printf("%s: could not GET probe request: %s\n",
		    DEVNAME(sc), usbd_errstr(err));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: GET probe request successfully\n", DEVNAME(sc));

	DPRINTF(1, "bmHint=0x%02x\n", UGETW(pc->bmHint));
	DPRINTF(1, "bFormatIndex=0x%02x\n", pc->bFormatIndex);
	DPRINTF(1, "bFrameIndex=0x%02x\n", pc->bFrameIndex);
	DPRINTF(1, "dwFrameInterval=%d (ns)\n", UGETDW(pc->dwFrameInterval));
	DPRINTF(1, "wKeyFrameRate=%d\n", UGETW(pc->wKeyFrameRate));
	DPRINTF(1, "wPFrameRate=%d\n", UGETW(pc->wPFrameRate));
	DPRINTF(1, "wCompQuality=%d\n", UGETW(pc->wCompQuality));
	DPRINTF(1, "wCompWindowSize=%d\n", UGETW(pc->wCompWindowSize));
	DPRINTF(1, "wDelay=%d (ms)\n", UGETW(pc->wDelay));
	DPRINTF(1, "dwMaxVideoFrameSize=%d (bytes)\n",
	    UGETDW(pc->dwMaxVideoFrameSize));
	DPRINTF(1, "dwMaxPayloadTransferSize=%d (bytes)\n",
	    UGETDW(pc->dwMaxPayloadTransferSize));

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_set_commit(struct uvideo_softc *sc, uint8_t *probe_data)
{
	usb_device_request_t req;
	usbd_status err;
	uint16_t tmp;

	req.bmRequestType = UVIDEO_SET_IF;
	req.bRequest = SET_CUR;
	tmp = VS_COMMIT_CONTROL;
	tmp = tmp << 8;
	USETW(req.wValue, tmp);
	USETW(req.wIndex, sc->sc_vs_curr->iface);
	USETW(req.wLength, 26);

	err = usbd_do_request(sc->sc_udev, &req, probe_data);
	if (err) {
		printf("%s: could not SET commit request: %s\n",
		    DEVNAME(sc), usbd_errstr(err));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: SET commit request successfully\n", DEVNAME(sc));

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_alloc_sample(struct uvideo_softc *sc)
{
	struct uvideo_sample_buffer *fb = &sc->sc_sample_buffer;

	fb->buf_size = UGETDW(sc->sc_desc_probe.dwMaxVideoFrameSize);

	/* don't overflow the upper layer sample buffer */
	if (sc->sc_max_fbuf_size < fb->buf_size &&
	    sc->sc_mmap_flag == 0) {
		printf("%s: sofware video buffer is too small!\n", DEVNAME(sc));
		return (USBD_NOMEM);
	}

	fb->buf = malloc(fb->buf_size, M_DEVBUF, M_NOWAIT);
	if (fb->buf == NULL) {
		printf("%s: can't allocate sample buffer!\n", DEVNAME(sc));
		return (USBD_NOMEM);
	}

	DPRINTF(1, "%s: %s: allocated %d bytes sample buffer\n",
	    DEVNAME(sc), __func__, fb->buf_size);

	fb->fragment = 0;
	fb->fid = 0;
	fb->offset = 0;

	return (USBD_NORMAL_COMPLETION);
}

void
uvideo_vs_free_sample(struct uvideo_softc *sc)
{
	struct uvideo_sample_buffer *fb = &sc->sc_sample_buffer;

	if (fb->buf != NULL) {
		free(fb->buf, M_DEVBUF);
		fb->buf = NULL;
	}

	if (sc->sc_mmap_buffer != NULL) {
		free(sc->sc_mmap_buffer, M_DEVBUF);
		sc->sc_mmap_buffer = NULL;
	}
}

usbd_status
uvideo_vs_alloc(struct uvideo_softc *sc)
{
	int size;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	sc->sc_vs_curr->sc = sc;

	sc->sc_vs_curr->xfer = usbd_alloc_xfer(sc->sc_udev);	
	if (sc->sc_vs_curr->xfer == NULL) {
		printf("%s: could not allocate VS xfer!\n", DEVNAME(sc));
		return (USBD_NOMEM);	
	}

	size = sc->sc_vs_curr->max_packet_size * sc->sc_nframes;

	sc->sc_vs_curr->buf = usbd_alloc_buffer(sc->sc_vs_curr->xfer, size);
	if (sc->sc_vs_curr->buf == NULL) {
		printf("%s: could not allocate VS buffer!\n", DEVNAME(sc));
		return (USBD_NOMEM);
	}
	DPRINTF(1, "%s: allocated %d bytes VS xfer buffer\n",
	    DEVNAME(sc), size);

	return (USBD_NORMAL_COMPLETION);
}

void
uvideo_vs_free(struct uvideo_softc *sc)
{
	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	if (sc->sc_vs_curr->buf != NULL) {
		usbd_free_buffer(sc->sc_vs_curr->xfer);
		sc->sc_vs_curr->buf = NULL;
	}

	if (sc->sc_vs_curr->xfer != NULL) {
		usbd_free_xfer(sc->sc_vs_curr->xfer);
		sc->sc_vs_curr->xfer = NULL;
	}
}

usbd_status
uvideo_vs_open(struct uvideo_softc *sc)
{
	usb_endpoint_descriptor_t *ed;
	usbd_status error;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	if (sc->sc_negotiated_flag == 0) {
		/* do device negotiation with commit */
		error = uvideo_vs_negotiation(sc, 1);
		if (error != USBD_NORMAL_COMPLETION)
			return (error);
	}

	error = uvideo_vs_set_alt(sc, sc->sc_vs_curr->ifaceh,
	    UGETDW(sc->sc_desc_probe.dwMaxPayloadTransferSize));
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: could not set alternate interface!\n",
		    DEVNAME(sc));
		return (error);
	}

	ed = usbd_interface2endpoint_descriptor(sc->sc_vs_curr->ifaceh, 0);
	if (ed == NULL) {
		printf("%s: no endpoint descriptor for VS iface\n",
		    DEVNAME(sc));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: open pipe for ", DEVNAME(sc));
	DPRINTF(1, "bEndpointAddress=0x%02x (0x%02x), wMaxPacketSize=%d (%d)\n",
	    ed->bEndpointAddress,
	    sc->sc_vs_curr->endpoint,
	    UGETW(ed->wMaxPacketSize),
	    sc->sc_vs_curr->max_packet_size);

	error = usbd_open_pipe(
	    sc->sc_vs_curr->ifaceh,
	    sc->sc_vs_curr->endpoint,
	    USBD_EXCLUSIVE_USE,
	    &sc->sc_vs_curr->pipeh);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: could not open VS pipe: %s\n",
		    DEVNAME(sc), usbd_errstr(error));
		return (error);
	}

	/* calculate optimal isoc xfer size */
	if (strncmp(sc->sc_udev->bus->bdev.dv_xname, "ohci", 4) == 0) {
		/* ohci workaround */
		sc->sc_nframes = 6400 /
		    sc->sc_vs_curr->max_packet_size;
	} else {
		sc->sc_nframes = UGETDW(sc->sc_desc_probe.dwMaxVideoFrameSize) /
		    sc->sc_vs_curr->max_packet_size;
	}
	if (sc->sc_nframes > UVIDEO_NFRAMES_MAX)
		sc->sc_nframes = UVIDEO_NFRAMES_MAX;
	DPRINTF(1, "%s: nframes=%d\n", DEVNAME(sc), sc->sc_nframes);

	return (USBD_NORMAL_COMPLETION);
}

void
uvideo_vs_close(struct uvideo_softc *sc)
{
	if (sc->sc_vs_curr->pipeh) {
		usbd_abort_pipe(sc->sc_vs_curr->pipeh);
		usbd_close_pipe(sc->sc_vs_curr->pipeh);
		sc->sc_vs_curr->pipeh = NULL;
	}

	/* switch back to default interface (turns off cam LED) */
	(void)usbd_set_interface(sc->sc_vs_curr->ifaceh, 0);
}

usbd_status
uvideo_vs_init(struct uvideo_softc *sc)
{
	usbd_status error;

	/* open video stream pipe */
	error = uvideo_vs_open(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (EIO);

	/* allocate video stream xfer buffer */
	error = uvideo_vs_alloc(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (EIO);

	/* allocate video stream sample buffer */
	error = uvideo_vs_alloc_sample(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (EIO);
#ifdef UVIDEO_DUMP
	if (uvideo_debug_file_open(sc) != 0)
		return(EIO);
	usb_init_task(&sc->sc_task_write, uvideo_debug_file_write_sample, sc);
#endif
	return (USBD_NORMAL_COMPLETION);
}

void
uvideo_vs_start(struct uvideo_softc *sc)
{
	int i;

	DPRINTF(2, "%s: %s\n", DEVNAME(sc), __func__);

	if (sc->sc_dying)
		return;

	for (i = 0; i < sc->sc_nframes; i++)
		sc->sc_vs_curr->size[i] = sc->sc_vs_curr->max_packet_size;

	usbd_setup_isoc_xfer(
	    sc->sc_vs_curr->xfer,
	    sc->sc_vs_curr->pipeh,
	    sc->sc_vs_curr,
	    sc->sc_vs_curr->size,
	    sc->sc_nframes,
	    USBD_NO_COPY | USBD_SHORT_XFER_OK,
	    uvideo_vs_cb);

	(void)usbd_transfer(sc->sc_vs_curr->xfer);
}

void
uvideo_vs_cb(usbd_xfer_handle xfer, usbd_private_handle priv,
    usbd_status status)
{
	struct uvideo_vs_iface *vs = priv;
	struct uvideo_softc *sc = vs->sc;
	int len, i, frame_size;
	uint8_t *frame;

	DPRINTF(2, "%s: %s\n", DEVNAME(sc), __func__);

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF(1, "%s: %s: %s\n", DEVNAME(sc), __func__,
		    usbd_errstr(status));
		return;
        }
	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	DPRINTF(2, "%s: *** buffer len = %d\n", DEVNAME(sc), len);
	if (len == 0)
		goto skip;

	for (i = 0; i < sc->sc_nframes; i++) {
		frame = vs->buf + (i * vs->max_packet_size);
		frame_size = vs->size[i];

		if (frame_size == 0)
			/* frame is empty */
			continue;

		uvideo_vs_decode_stream_header(sc, frame, frame_size);
	}

skip:	/* setup new transfer */
	uvideo_vs_start(sc);
}

int
uvideo_vs_decode_stream_header(struct uvideo_softc *sc, uint8_t *frame,
    int frame_size)
{
	struct uvideo_sample_buffer *fb = &sc->sc_sample_buffer;
	uint8_t header_len, header_flags;
	int fragment_len;

	if (frame_size < 2)
		/* frame too small to contain a valid stream header */
		return (-1);

	header_len = frame[0];
	header_flags = frame[1];

	DPRINTF(2, "%s: header_len = %d\n", DEVNAME(sc), header_len);

	if (header_len != 12)
		/* frame header is 12 bytes long */
		return (-1);
	if (header_len == frame_size && !(header_flags & UVIDEO_STREAM_EOF)) {
		/* stream header without payload and no EOF */
		return (-1);
	}

	DPRINTF(2, "%s: frame_size = %d\n", DEVNAME(sc), frame_size);

	if (header_flags & UVIDEO_STREAM_FID) {
		DPRINTF(2, "%s: %s: FID ON (0x%02x)\n",
		    DEVNAME(sc), __func__,
		    header_flags & UVIDEO_STREAM_FID);
	} else {
		DPRINTF(2, "%s: %s: FID OFF (0x%02x)\n",
		    DEVNAME(sc), __func__,
		    header_flags & UVIDEO_STREAM_FID);
	}

	if (fb->fragment == 0) {
		/* first fragment for a sample */
		fb->fragment = 1;
		fb->fid = header_flags & UVIDEO_STREAM_FID;
		fb->offset = 0;
	} else {
		/* continues fragment for a sample, check consistency */
		if (fb->fid != (header_flags & UVIDEO_STREAM_FID)) {
			DPRINTF(1, "%s: %s: wrong FID, ignore last sample!\n",
			    DEVNAME(sc), __func__);
			fb->fragment = 1;
			fb->fid = header_flags & UVIDEO_STREAM_FID;
			fb->offset = 0;
		}
	}

	/* save sample fragment */
	fragment_len = frame_size - header_len;
	if ((fb->offset + fragment_len) <= fb->buf_size) {
		bcopy(frame + header_len, fb->buf + fb->offset, fragment_len);
		fb->offset += fragment_len;
	}

	if (header_flags & UVIDEO_STREAM_EOF) {
		/* got a full sample */
		DPRINTF(2, "%s: %s: EOF (sample size = %d bytes)\n",
		    DEVNAME(sc), __func__, fb->offset);

		if (fb->offset <= fb->buf_size) {
#ifdef UVIDEO_DUMP
			/* do the file write in process context */
			usb_rem_task(sc->sc_udev, &sc->sc_task_write);
			usb_add_task(sc->sc_udev, &sc->sc_task_write);
#endif
			if (sc->sc_mmap_flag) {
				/* mmap */
				uvideo_mmap_queue(sc, fb->buf, fb->offset);
			} else {
				/* read */
				uvideo_read(sc, fb->buf, fb->offset);
			}
		} else {
			DPRINTF(1, "%s: %s: sample too large, skipped!\n",
			    DEVNAME(sc), __func__);
		}

		fb->fragment = 0;
		fb->fid = 0;
	}

	return (0);
}

int
uvideo_mmap_queue(struct uvideo_softc *sc, uint8_t *buf, int len)
{
	/* find a buffer which is ready for queueing */
	while (sc->sc_mmap_cur < sc->sc_mmap_count) {
		if (sc->sc_mmap[sc->sc_mmap_cur].v4l2_buf.flags &
		    V4L2_BUF_FLAG_QUEUED)
			break;
		/* not ready for queueing, try next */
		sc->sc_mmap_cur++;
	}
	if (sc->sc_mmap_cur == sc->sc_mmap_count)
		panic("uvideo_mmap_queue: mmap queue is full!");

	/* copy frame to mmap buffer and report length */
	bcopy(buf, sc->sc_mmap[sc->sc_mmap_cur].buf, len);
	sc->sc_mmap[sc->sc_mmap_cur].v4l2_buf.bytesused = len;

	/* queue it */
	SIMPLEQ_INSERT_TAIL(&sc->sc_mmap_q, &sc->sc_mmap[sc->sc_mmap_cur],
	    q_frames);
	DPRINTF(2, "%s: %s: frame queued on index %d\n",
	    DEVNAME(sc), __func__, sc->sc_mmap_cur);

	/* point to next mmap buffer */
	sc->sc_mmap_cur++;
	if (sc->sc_mmap_cur == sc->sc_mmap_count)
		/* we reached the end of the mmap buffer, start over */
		sc->sc_mmap_cur = 0;

	wakeup(sc);

	return (0);
}

int
uvideo_read(struct uvideo_softc *sc, uint8_t *buf, int len)
{
	/*
	 * Copy video frame to upper layer buffer and call
	 * upper layer interrupt.
	 */
	*sc->sc_uplayer_fsize = len;
	bcopy(buf, sc->sc_uplayer_fbuffer, len);
	sc->sc_uplayer_intr(sc->sc_uplayer_arg);

	return (0);
}

#ifdef UVIDEO_DEBUG
void
uvideo_dump_desc_all(struct uvideo_softc *sc)
{
	usbd_desc_iter_t iter;
	const usb_descriptor_t *desc;

	usb_desc_iter_init(sc->sc_udev, &iter);
	desc = usb_desc_iter_next(&iter);
	while (desc) {
		printf("bLength=%d\n", desc->bLength);
		printf("bDescriptorType=0x%02x", desc->bDescriptorType);

		switch (desc->bDescriptorType) {
		case UDESC_CS_INTERFACE:
			printf(" (CS_INTERFACE)\n");

			switch (desc->bDescriptorSubtype) {
			case UDESCSUB_VC_HEADER:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				if (uvideo_desc_len(desc, 12, 11, 1, 0)) {
					printf(" (UDESCSUB_VC_HEADER)\n");
					printf("|\n");
					uvideo_dump_desc_vc_header(sc, desc);
					break;
				}
				if (uvideo_desc_len(desc, 13, 3, 0, 12)) {
					printf(" (UDESCSUB_VS_INPUT_HEADER)\n");
					printf("|\n");
					uvideo_dump_desc_input_header(sc, desc);
					break;
				}
				printf(" (unknown)\n");
				break;
			case UDESCSUB_VC_INPUT_TERMINAL:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VC_INPUT_TERMINAL)\n");
				printf("|\n");
				uvideo_dump_desc_input(sc, desc);
				break;
			case UDESCSUB_VC_OUTPUT_TERMINAL:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VC_OUTPUT)\n");
				printf("|\n");
				uvideo_dump_desc_output(sc, desc);
				break;
			case UDESCSUB_VC_SELECTOR_UNIT:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				if (desc->bLength == 27) {
					printf(" (UDESCSUB_VS_FORMAT_"
					    "UNCOMPRESSED)\n");
					uvideo_dump_desc_format_uncompressed(
					    sc, desc);
				} else {
					printf(" (UDESCSUB_VC_SELECTOR_"
					    "UNIT)\n");
					/* TODO */
				}
				break;
			case UDESCSUB_VC_PROCESSING_UNIT:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				/* XXX do correct length calculation */
				if (desc->bLength > 25) {
					printf(" (UDESCSUB_VS_FRAME_"
					    "UNCOMPRESSED)\n");
					uvideo_dump_desc_frame_uncompressed(
					    sc, desc);
				} else {
					printf(" (UDESCSUB_VC_PROCESSING_"
					    "UNIT)\n");
					/* TODO */
				}
				break;
			case UDESCSUB_VC_EXTENSION_UNIT:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				if (desc->bLength == 11) {
					printf(" (UDESCSUB_VS_FORMAT_MJPEG)\n");
					printf("|\n");
					uvideo_dump_desc_format_mjpeg(sc, desc);
				} else {
					printf(" (UDESCSUB_VC_EXTENSION_"
					    "UNIT)\n");
					printf("|\n");
					uvideo_dump_desc_extension(sc, desc);
				}
				break;
			case UDESCSUB_VS_FRAME_MJPEG:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VS_FRAME_MJPEG)\n");
				if (desc->bLength > 26) {
					printf("|\n");
					uvideo_dump_desc_frame_mjpeg(sc, desc);
				}
				break;
			case UDESCSUB_VS_COLORFORMAT:
				printf("bDescriptorSubtype=0x%02x",
				   desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VS_COLORFORMAT)\n");
				printf("|\n");
				uvideo_dump_desc_colorformat(sc, desc);
				break;
			}

			break;
		case UDESC_CS_ENDPOINT:
			printf(" (UDESC_CS_ENDPOINT)\n");

			switch (desc->bDescriptorSubtype) {
			case EP_INTERRUPT:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (EP_INTERRUPT)\n");
				printf("|\n");
				uvideo_dump_desc_cs_endpoint(sc, desc);
				break;
			case EP_GENERAL:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (EP_GENERAL)\n");
				printf("|\n");
				uvideo_dump_desc_cs_endpoint(sc, desc);
				break;
			}

			break;
		case UDESC_CONFIG:
			printf(" (UDESC_CONFIG)\n");
			printf("|\n");
			uvideo_dump_desc_config(sc, desc);
			break;
		case UDESC_ENDPOINT:
			printf(" (UDESC_ENDPOINT)\n");
			printf("|\n");
			uvideo_dump_desc_endpoint(sc, desc);
			break;
		case UDESC_INTERFACE:
			printf(" (UDESC_INTERFACE)\n");
			printf("|\n");
			uvideo_dump_desc_interface(sc, desc);
			break;
		default:
			printf(" (unknown)\n");
			break;
		}

		printf("\n");

		desc = usb_desc_iter_next(&iter);
	}	

}

void
uvideo_dump_desc_vc_header(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_header_desc *d;

	d = (struct usb_video_header_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bcdUVC=0x%04x\n", UGETW(d->bcdUVC));
	printf("wTotalLength=%d\n", UGETW(d->wTotalLength));
	printf("dwClockFrequency=%d\n", UGETDW(d->dwClockFrequency));
	printf("bInCollection=0x%02x\n", d->bInCollection);
}

void
uvideo_dump_desc_input_header(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_input_header_desc *d;

	d = (struct usb_video_input_header_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bNumFormats=%d\n", d->bNumFormats);
	printf("wTotalLength=%d\n", UGETW(d->wTotalLength));
	printf("bEndpointAddress=0x%02x\n", d->bEndpointAddress);
	printf("bmInfo=0x%02x\n", d->bmInfo);
	printf("bTerminalLink=0x%02x\n", d->bTerminalLink);
	printf("bStillCaptureMethod=0x%02x\n", d->bStillCaptureMethod);
	printf("bTriggerSupport=0x%02x\n", d->bTriggerSupport);
	printf("bTriggerUsage=0x%02x\n", d->bTriggerUsage);
	printf("bControlSize=%d\n", d->bControlSize);
}

void
uvideo_dump_desc_input(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_input_terminal_desc *d;

	d = (struct usb_video_input_terminal_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bTerminalID=0x%02x\n", d->bTerminalID);
	printf("wTerminalType=0x%04x\n", UGETW(d->wTerminalType));
	printf("bAssocTerminal=0x%02x\n", d->bAssocTerminal);
	printf("iTerminal=0x%02x\n", d->iTerminal);
}

void
uvideo_dump_desc_output(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_output_terminal_desc *d;

	d = (struct usb_video_output_terminal_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bTerminalID=0x%02x\n", d->bTerminalID);
	printf("bAssocTerminal=0x%02x\n", d->bAssocTerminal);
	printf("bSourceID=0x%02x\n", d->bSourceID);
	printf("iTerminal=0x%02x\n", d->iTerminal);

}

void
uvideo_dump_desc_endpoint(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	usb_endpoint_descriptor_t *d;

	d = (usb_endpoint_descriptor_t *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bEndpointAddress=0x%02x", d->bEndpointAddress);
	if (UE_GET_DIR(d->bEndpointAddress) == UE_DIR_IN)
		printf(" (IN)\n");
	if (UE_GET_DIR(d->bEndpointAddress) == UE_DIR_OUT)
		printf(" (OUT)\n");
	printf("bmAttributes=0x%02x", d->bmAttributes);
	if (UE_GET_XFERTYPE(d->bmAttributes) == UE_ISOCHRONOUS) {
		printf(" (UE_ISOCHRONOUS,");
		if (UE_GET_ISO_TYPE(d->bmAttributes) == UE_ISO_ASYNC)
			printf(" UE_ISO_ASYNC)\n");
		if (UE_GET_ISO_TYPE(d->bmAttributes) == UE_ISO_ADAPT)
			printf(" UE_ISO_ADAPT)\n");
		if (UE_GET_ISO_TYPE(d->bmAttributes) == UE_ISO_SYNC)
			printf(" UE_ISO_SYNC)\n");
	}
	if (UE_GET_XFERTYPE(d->bmAttributes) == UE_CONTROL)
		printf(" (UE_CONTROL)\n");
	if (UE_GET_XFERTYPE(d->bmAttributes) == UE_BULK)
		printf(" (UE_BULK)\n");
	if (UE_GET_XFERTYPE(d->bmAttributes) == UE_INTERRUPT)
		printf(" (UE_INTERRUPT)\n");
	printf("wMaxPacketSize=%d\n", UGETW(d->wMaxPacketSize));
	printf("bInterval=0x%02x\n", d->bInterval);
}

void
uvideo_dump_desc_interface(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	usb_interface_descriptor_t *d;

	d = (usb_interface_descriptor_t *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bInterfaceNumber=0x%02x\n", d->bInterfaceNumber);
	printf("bAlternateSetting=0x%02x\n", d->bAlternateSetting);
	printf("bNumEndpoints=%d\n", d->bNumEndpoints);
	printf("bInterfaceClass=0x%02x\n", d->bInterfaceClass);
	printf("bInterfaceSubClass=0x%02x\n", d->bInterfaceSubClass);
	printf("bInterfaceProtocol=0x%02x\n", d->bInterfaceProtocol);
	printf("iInterface=0x%02x\n", d->iInterface);
}

void
uvideo_dump_desc_config(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	usb_config_descriptor_t *d;

	d = (usb_config_descriptor_t *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("wTotalLength=%d\n", UGETW(d->wTotalLength));
	printf("bNumInterface=0x%02x\n", d->bNumInterface);
	printf("bConfigurationValue=0x%02x\n", d->bConfigurationValue);
	printf("iConfiguration=0x%02x\n", d->iConfiguration);
	printf("bmAttributes=0x%02x\n", d->bmAttributes);
	printf("bMaxPower=0x%02x\n", d->bMaxPower);
}

void
uvideo_dump_desc_cs_endpoint(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_vc_endpoint_desc *d;

	d = (struct usb_video_vc_endpoint_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("wMaxTransferSize=%d\n", UGETW(d->wMaxTransferSize));
}

void
uvideo_dump_desc_colorformat(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_color_matching_descr *d;

	d = (struct usb_video_color_matching_descr *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bColorPrimaries=0x%02x\n", d->bColorPrimaries);
	printf("bTransferCharacteristics=0x%02x\n",
	    d->bTransferCharacteristics);
	printf("bMatrixCoefficients=0x%02x\n", d->bMatrixCoefficients);
}

void
uvideo_dump_desc_frame_mjpeg(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_frame_mjpeg_desc *d;

	d = (struct usb_video_frame_mjpeg_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFrameIndex=0x%02x\n", d->bFrameIndex);
	printf("bmCapabilities=0x%02x\n", d->bmCapabilities);
	printf("wWidth=%d\n", UGETW(d->wWidth));
	printf("wHeight=%d\n", UGETW(d->wHeight));
	printf("dwMinBitRate=%d\n", UGETDW(d->dwMinBitRate));
	printf("dwMaxBitRate=%d\n", UGETDW(d->dwMaxBitRate));
	printf("dwMaxVideoFrameBufferSize=%d\n",
	    UGETDW(d->dwMaxVideoFrameBufferSize));
	printf("dwDefaultFrameInterval=%d\n",
	    UGETDW(d->dwDefaultFrameInterval));
	printf("bFrameIntervalType=0x%02x\n", d->bFrameIntervalType);
}

void
uvideo_dump_desc_format_mjpeg(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_mjpeg_desc *d;

	d = (struct usb_video_format_mjpeg_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFormatIndex=0x%02x\n", d->bFormatIndex);
	printf("bNumFrameDescriptors=0x%02x\n", d->bNumFrameDescriptors);
	printf("bmFlags=0x%02x\n", d->bmFlags);
	printf("bDefaultFrameIndex=0x%02x\n", d->bDefaultFrameIndex);
	printf("bAspectRatioX=0x%02x\n", d->bAspectRatioX);
	printf("bAspectRatioY=0x%02x\n", d->bAspectRatioY);
	printf("bmInterlaceFlags=0x%02x\n", d->bmInterlaceFlags);
	printf("bCopyProtect=0x%02x\n", d->bCopyProtect);
}

void
uvideo_dump_desc_frame_uncompressed(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_frame_uncompressed_desc *d;

	d = (struct usb_video_frame_uncompressed_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFrameIndex=0x%02x\n", d->bFrameIndex);
	printf("bmCapabilities=0x%02x\n", d->bmCapabilities);
	printf("wWidth=%d\n", UGETW(d->wWidth));
	printf("wHeight=%d\n", UGETW(d->wHeight));
	printf("dwMinBitRate=%d\n", UGETDW(d->dwMinBitRate));
	printf("dwMaxBitRate=%d\n", UGETDW(d->dwMaxBitRate));
	printf("dwMaxVideoFrameBufferSize=%d\n",
	    UGETDW(d->dwMaxVideoFrameBufferSize));
	printf("dwDefaultFrameInterval=%d\n",
	    UGETDW(d->dwDefaultFrameInterval));
	printf("bFrameIntervalType=0x%02x\n", d->bFrameIntervalType);
}

void
uvideo_dump_desc_format_uncompressed(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_uncompressed_desc *d;

	d = (struct usb_video_format_uncompressed_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFormatIndex=0x%02x\n", d->bFormatIndex);
	printf("bNumFrameDescriptors=0x%02x\n", d->bNumFrameDescriptors);
	printf("guidFormat=%s\n", d->guidFormat);
	printf("bBitsPerPixel=0x%02x\n", d->bBitsPerPixel);
	printf("bDefaultFrameIndex=0x%02x\n", d->bDefaultFrameIndex);
	printf("bAspectRatioX=0x%02x\n", d->bAspectRatioX);
	printf("bAspectRatioY=0x%02x\n", d->bAspectRatioY);
	printf("bmInterlaceFlags=0x%02x\n", d->bmInterlaceFlags);
	printf("bCopyProtect=0x%02x\n", d->bCopyProtect);
}

void
uvideo_dump_desc_extension(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_vc_extension_desc *d;

	d = (struct usb_video_vc_extension_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bUnitID=0x%02x\n", d->bUnitID);
	printf("guidExtensionCode=0x");
	uvideo_hexdump(d->guidExtensionCode, sizeof(d->guidExtensionCode), 1);
	printf("bNumControls=0x%02x\n", d->bNumControls);
	printf("bNrInPins=0x%02x\n", d->bNrInPins);
}

void
uvideo_hexdump(void *buf, int len, int quiet)
{
	int i;

	for (i = 0; i < len; i++) {
		if (quiet == 0) {
			if (i % 16 == 0)
				printf("%s%5i:", i ? "\n" : "", i);
			if (i % 4 == 0)
				printf(" ");
		}
		printf("%02x", (int)*((u_char *)buf + i));
	}
	printf("\n");
}

int
uvideo_debug_file_open(struct uvideo_softc *sc)
{
	struct proc *p = curproc;
	struct nameidata nd;
	char name[] = "/tmp/uvideo.mjpeg";
	int error;

	NDINIT(&nd, LOOKUP, NOFOLLOW, UIO_SYSSPACE, name, p);
	error = vn_open(&nd, O_CREAT | FWRITE | O_NOFOLLOW, S_IRUSR | S_IWUSR);
	if (error) {
		DPRINTF(1, "%s: %s: can't creat debug file %s!\n",
		    DEVNAME(sc), __func__, name);
		return (-1);
	}

	sc->sc_vp = nd.ni_vp;
	VOP_UNLOCK(sc->sc_vp, 0, p);
	if (nd.ni_vp->v_type != VREG) {
		vn_close(nd.ni_vp, FWRITE, p->p_ucred, p);
		return (-1);
	}

	DPRINTF(1, "%s: %s: created debug file %s\n",
	    DEVNAME(sc), __func__, name);

	return (0);
}

void
uvideo_debug_file_write_sample(void *arg)
{
	struct uvideo_softc *sc = arg;
	struct uvideo_sample_buffer *sb = &sc->sc_sample_buffer;
	struct proc *p = curproc;
	int error;

	if (sc->sc_vp == NULL) {
		printf("%s: %s: no file open!\n", DEVNAME(sc));
		return;
	}

	error = vn_rdwr(UIO_WRITE, sc->sc_vp, sb->buf, sb->offset, (off_t)0,
	    UIO_SYSSPACE, IO_APPEND|IO_UNIT, p->p_ucred, NULL, p);

	if (error)
		DPRINTF(1, "vn_rdwr error!\n");
}
#endif

/*
 * IOCTL's
 */
int
uvideo_querycap(void *v, struct v4l2_capability *caps)
{
	struct uvideo_softc *sc = v;

	bzero(caps, sizeof(caps));
	strlcpy(caps->driver, DEVNAME(sc), sizeof(caps->driver));
	strlcpy(caps->card, "Generic USB video class device",
	    sizeof(caps->card));
	strlcpy(caps->bus_info, "usb", sizeof(caps->bus_info));

	caps->version = 1;
	caps->capabilities = V4L2_CAP_VIDEO_CAPTURE
	    | V4L2_CAP_STREAMING
	    | V4L2_CAP_READWRITE;

	return (0);
}

int
uvideo_enum_fmt(void *v, struct v4l2_fmtdesc *fmtdesc)
{
	struct uvideo_softc *sc = v;
	int idx;

	if (fmtdesc->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		/* type not supported */
		return (EINVAL);

	if (fmtdesc->index == sc->sc_fmtgrp_num) 
		/* no more formats left */
		return (EINVAL);
	idx = fmtdesc->index;

	switch (sc->sc_fmtgrp[idx].format->bDescriptorSubtype) {
	case UDESCSUB_VS_FORMAT_MJPEG:
		fmtdesc->flags = V4L2_FMT_FLAG_COMPRESSED;
		(void)strlcpy(fmtdesc->description, "MJPEG",
		    sizeof(fmtdesc->description));
		fmtdesc->pixelformat = V4L2_PIX_FMT_MJPEG;
		bzero(fmtdesc->reserved, sizeof(fmtdesc->reserved));
		break;
	case UDESCSUB_VS_FORMAT_UNCOMPRESSED:
		fmtdesc->flags = 0;
		if (sc->sc_fmtgrp[idx].pixelformat ==
		    V4L2_PIX_FMT_YUYV) {
			(void)strlcpy(fmtdesc->description, "YUYV",
			    sizeof(fmtdesc->description));
			fmtdesc->pixelformat = V4L2_PIX_FMT_YUYV;
		} else if (sc->sc_fmtgrp[idx].pixelformat ==
		    V4L2_PIX_FMT_NV12) {
			(void)strlcpy(fmtdesc->description, "NV12",
			    sizeof(fmtdesc->description));
			fmtdesc->pixelformat = V4L2_PIX_FMT_NV12;
		} else {
			(void)strlcpy(fmtdesc->description, "Unknown UC Format",
			    sizeof(fmtdesc->description));
			fmtdesc->pixelformat = 0;
		}
		bzero(fmtdesc->reserved, sizeof(fmtdesc->reserved));
		break;
	default:
		fmtdesc->flags = 0;
		(void)strlcpy(fmtdesc->description, "Unknown Format",
		    sizeof(fmtdesc->description));
		fmtdesc->pixelformat = 0;
		bzero(fmtdesc->reserved, sizeof(fmtdesc->reserved));
		break;
	}

	return (0);
}

int
uvideo_s_fmt(void *v, struct v4l2_format *fmt)
{
	struct uvideo_softc *sc = v;
	struct uvideo_format_group *fmtgrp_save;
	struct usb_video_frame_mjpeg_desc *frame_save;
	struct uvideo_res r;
	int found, i;
	usbd_status error;

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);

	DPRINTF(1, "%s: %s: requested width=%d, height=%d\n",
	    DEVNAME(sc), __func__, fmt->fmt.pix.width, fmt->fmt.pix.height);

	/* search requested pixel format */
	for (found = 0, i = 0; i < sc->sc_fmtgrp_num; i++) {
		if (fmt->fmt.pix.pixelformat == sc->sc_fmtgrp[i].pixelformat) {
			found = 1;
			break;
		}
	}
	if (found == 0)
		return (EINVAL);

	/* search requested frame resolution */
	uvideo_find_res(sc, i, fmt->fmt.pix.width, fmt->fmt.pix.height, &r);

	/*
	 * Do negotiation.
	 */
	/* save a copy of current fromat group in case of negotiation fails */
	fmtgrp_save = sc->sc_fmtgrp_cur;
	frame_save = sc->sc_fmtgrp_cur->frame_cur;
	/* set new format group */
	sc->sc_fmtgrp_cur = &sc->sc_fmtgrp[i];
	sc->sc_fmtgrp[i].frame_cur = sc->sc_fmtgrp[i].frame[r.fidx];
	sc->sc_fmtgrp[i].format_dfidx = r.fidx;
	/* do device negotiation with commit */
	error = uvideo_vs_negotiation(sc, 1);
	if (error != USBD_NORMAL_COMPLETION) {
		sc->sc_fmtgrp_cur = fmtgrp_save;
		sc->sc_fmtgrp_cur->frame_cur = frame_save;
		return (EINVAL);
	}
	sc->sc_negotiated_flag = 1;

	/* offer closest resolution which we have found */
	fmt->fmt.pix.width = r.width;
	fmt->fmt.pix.height = r.height;

	DPRINTF(1, "%s: %s: offered width=%d, height=%d\n",
	    DEVNAME(sc), __func__, r.width, r.height);

	/* tell our sample buffer size */
	fmt->fmt.pix.sizeimage = UGETDW(sc->sc_desc_probe.dwMaxVideoFrameSize);

	return (0);
}

int
uvideo_g_fmt(void *v, struct v4l2_format *fmt)
{
	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);

	return (0);
}

int
uvideo_enum_input(void *v, struct v4l2_input *input)
{
	if (input->index != 0)
		/* XXX we just support one input for now */
		return (EINVAL);

	strlcpy(input->name, "Camera Terminal", sizeof(input->name));
	input->type = V4L2_INPUT_TYPE_CAMERA;

	return (0);
}

int
uvideo_s_input(void *v, int input)
{
	if (input != 0)
		/* XXX we just support one input for now */
		return (EINVAL);

	return (0);
}

int
uvideo_reqbufs(void *v, struct v4l2_requestbuffers *rb)
{
	struct uvideo_softc *sc = v;
	int i, buf_size, buf_size_total;

	DPRINTF(1, "%s: %s: count=%d\n", DEVNAME(sc), __func__, rb->count);

	/* limit the buffers */
	if (rb->count > UVIDEO_MAX_BUFFERS)
		sc->sc_mmap_count = UVIDEO_MAX_BUFFERS;
	else
		sc->sc_mmap_count = rb->count;

	/* allocate the total mmap buffer */	
	buf_size = UGETDW(sc->sc_desc_probe.dwMaxVideoFrameSize);
	buf_size_total = sc->sc_mmap_count * buf_size;
	buf_size_total = round_page(buf_size_total); /* page align buffer */
	sc->sc_mmap_buffer = malloc(buf_size_total, M_DEVBUF, M_NOWAIT);
	if (sc->sc_mmap_buffer == NULL) {
		printf("%s: can't allocate mmap buffer!\n", DEVNAME(sc));
		return (EINVAL);
	}
	DPRINTF(1, "%s: allocated %d bytes mmap buffer\n",
	    DEVNAME(sc), buf_size_total);

	/* fill the v4l2_buffer structure */
	for (i = 0; i < sc->sc_mmap_count; i++) {
		sc->sc_mmap[i].buf = sc->sc_mmap_buffer + (i * buf_size);

		sc->sc_mmap[i].v4l2_buf.index = i;
		sc->sc_mmap[i].v4l2_buf.m.offset = i * buf_size;
		sc->sc_mmap[i].v4l2_buf.length = buf_size;
		sc->sc_mmap[i].v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		sc->sc_mmap[i].v4l2_buf.sequence = 0;
		sc->sc_mmap[i].v4l2_buf.field = V4L2_FIELD_NONE;
		sc->sc_mmap[i].v4l2_buf.memory = V4L2_MEMORY_MMAP;
		sc->sc_mmap[i].v4l2_buf.flags = V4L2_MEMORY_MMAP;

		DPRINTF(1, "%s: %s: index=%d, offset=%d, length=%d\n",
		    DEVNAME(sc), __func__,
		    sc->sc_mmap[i].v4l2_buf.index,
		    sc->sc_mmap[i].v4l2_buf.m.offset,
		    sc->sc_mmap[i].v4l2_buf.length);
	}

	/* tell how many buffers we have really allocated */
	rb->count = sc->sc_mmap_count;

	return (0);
}

int
uvideo_querybuf(void *v, struct v4l2_buffer *qb)
{
	struct uvideo_softc *sc = v;

	if (qb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    qb->memory != V4L2_MEMORY_MMAP)
		return (EINVAL);

	bcopy(&sc->sc_mmap[qb->index].v4l2_buf, qb,
	    sizeof(struct v4l2_buffer));

	DPRINTF(1, "%s: %s: index=%d, offset=%d, length=%d\n",
	    DEVNAME(sc), __func__,
	    qb->index,
	    qb->m.offset,
	    qb->length);

	return (0);
}

int
uvideo_qbuf(void *v, struct v4l2_buffer *qb)
{
	struct uvideo_softc *sc = v;

	sc->sc_mmap[qb->index].v4l2_buf.flags &= ~V4L2_BUF_FLAG_DONE;
	sc->sc_mmap[qb->index].v4l2_buf.flags |= V4L2_BUF_FLAG_MAPPED;
	sc->sc_mmap[qb->index].v4l2_buf.flags |= V4L2_BUF_FLAG_QUEUED;

	DPRINTF(2, "%s: %s: buffer on index %d ready for queueing\n",
	    DEVNAME(sc), __func__, qb->index);

	return (0);
}

int
uvideo_dqbuf(void *v, struct v4l2_buffer *dqb)
{
	struct uvideo_softc *sc = v;
	struct uvideo_mmap *mmap;
	int error;

	if (SIMPLEQ_EMPTY(&sc->sc_mmap_q)) {
		/* mmap queue is empty, block until first frame is queued */
		error = tsleep(sc, 0, "vid_mmap", 10 * hz);
		if (error)
			return (EINVAL);
	}

	mmap = SIMPLEQ_FIRST(&sc->sc_mmap_q);
	if (mmap == NULL)
		panic("uvideo_dqbuf: NULL pointer!");

	bcopy(&mmap->v4l2_buf, dqb, sizeof(struct v4l2_buffer));

	mmap->v4l2_buf.flags |= V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_DONE;

	DPRINTF(2, "%s: %s: frame dequeued from index %d\n",
	    DEVNAME(sc), __func__, mmap->v4l2_buf.index);
	SIMPLEQ_REMOVE_HEAD(&sc->sc_mmap_q, q_frames);

	return (0);
}

int
uvideo_streamon(void *v, int type)
{
	struct uvideo_softc *sc = v;
	usbd_status error;

	error = uvideo_vs_init(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (EINVAL);

	uvideo_vs_start(sc);

	return (0);
}

int
uvideo_streamoff(void *v, int type)
{
	struct uvideo_softc *sc = v;

	uvideo_vs_close(sc);

	return (0);
}

int
uvideo_try_fmt(void *v, struct v4l2_format *fmt)
{
	struct uvideo_softc *sc = v;
	struct uvideo_res r;
	int found, i;

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);

	DPRINTF(1, "%s: %s: requested width=%d, height=%d\n",
	    DEVNAME(sc), __func__, fmt->fmt.pix.width, fmt->fmt.pix.height);

	/* search requested pixel format */
	for (found = 0, i = 0; i < sc->sc_fmtgrp_num; i++) {
		if (fmt->fmt.pix.pixelformat == sc->sc_fmtgrp[i].pixelformat) {
			found = 1;
			break;
		}
	}
	if (found == 0)
		return (EINVAL);

	/* search requested frame resolution */
	uvideo_find_res(sc, i, fmt->fmt.pix.width, fmt->fmt.pix.height, &r);

	/* offer closest resolution which we have found */
	fmt->fmt.pix.width = r.width;
	fmt->fmt.pix.height = r.height;

	DPRINTF(1, "%s: %s: offered width=%d, height=%d\n",
	    DEVNAME(sc), __func__, r.width, r.height);

	/* tell our sample buffer size */
	fmt->fmt.pix.sizeimage = sc->sc_sample_buffer.buf_size;

	return (0);
}

caddr_t
uvideo_mappage(void *v, off_t off, int prot)
{
	struct uvideo_softc *sc = v;
	caddr_t p;

	if (!sc->sc_mmap_flag)
		sc->sc_mmap_flag = 1;

	p = sc->sc_mmap_buffer + off;

	return (p);
}

int
uvideo_get_bufsize(void *v)
{
	struct uvideo_softc *sc = v;
	struct usb_video_probe_commit *pc;
	uint8_t probe_data[34];
	usbd_status error;

	pc = (struct usb_video_probe_commit *)probe_data;

	/* find the maximum frame size */
	bzero(probe_data, sizeof(probe_data));
	error = uvideo_vs_get_probe(sc, probe_data, GET_MAX);
	if (error != USBD_NORMAL_COMPLETION) {
		return (EINVAL);
	}

	sc->sc_max_fbuf_size = UGETDW(pc->dwMaxVideoFrameSize);

	return (sc->sc_max_fbuf_size);
}

int
uvideo_start_read(void *v)
{
	struct uvideo_softc *sc = v;
	usbd_status error;

	if (sc->sc_mmap_flag)
		sc->sc_mmap_flag = 0;

	error = uvideo_vs_init(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (EINVAL);

	uvideo_vs_start(sc);

	return (0);
}
