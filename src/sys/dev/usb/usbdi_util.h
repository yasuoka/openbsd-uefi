/*	$OpenBSD: usbdi_util.h,v 1.23 2013/11/02 12:23:58 mpi Exp $ */
/*	$NetBSD: usbdi_util.h,v 1.28 2002/07/11 21:14:36 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usbdi_util.h,v 1.9 1999/11/17 22:33:50 n_hibma Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

usbd_status	usbd_get_desc(struct usbd_device *dev, int type,
		    int index, int len, void *desc);
usbd_status	usbd_set_address(struct usbd_device *dev, int addr);
usbd_status	usbd_get_port_status(struct usbd_device *,
		    int, usb_port_status_t *);
usbd_status	usbd_set_hub_feature(struct usbd_device *dev, int);
usbd_status	usbd_clear_hub_feature(struct usbd_device *, int);
usbd_status	usbd_set_port_feature(struct usbd_device *dev, int, int);
usbd_status	usbd_clear_port_feature(struct usbd_device *, int, int);
usbd_status	usbd_get_device_status(struct usbd_device *, usb_status_t *);
usbd_status	usbd_get_hub_status(struct usbd_device *, usb_hub_status_t *);
usbd_status	usbd_get_protocol(struct usbd_interface *dev, u_int8_t *report);
usbd_status	usbd_set_protocol(struct usbd_interface *dev, int report);
usbd_status	usbd_get_report_descriptor(struct usbd_device *dev, int ifcno,
		    int size, void *d);
struct usb_hid_descriptor *usbd_get_hid_descriptor(struct usbd_interface *ifc);
usbd_status	usbd_set_report(struct usbd_interface *iface, int type, int id,
		    void *data,int len);
usbd_status	usbd_set_report_async(struct usbd_interface *iface, int type,
		    int id, void *data, int len);
usbd_status	usbd_get_report(struct usbd_interface *iface, int type, int id,
		    void *data, int len);
usbd_status	usbd_set_idle(struct usbd_interface *iface, int duration,int id);
usbd_status	usbd_read_report_desc(struct usbd_interface *ifc, void **descp,
		    int *sizep, int mem);
usbd_status	usbd_get_config(struct usbd_device *dev, u_int8_t *conf);
usbd_status	usbd_get_string_desc(struct usbd_device *dev, int sindex,
		    int langid,usb_string_descriptor_t *sdesc, int *sizep);
void		usbd_delay_ms(struct usbd_device *, u_int);


usbd_status	usbd_set_config_no(struct usbd_device *dev, int no, int msg);
usbd_status	usbd_set_config_index(struct usbd_device *dev, int index,
		    int msg);

void usb_detach_wait(struct device *);
void usb_detach_wakeup(struct device *);
