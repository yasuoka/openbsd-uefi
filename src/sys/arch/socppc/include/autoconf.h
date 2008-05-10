/*	$OpenBSD: autoconf.h,v 1.1 2008/05/10 12:02:21 kettenis Exp $	*/

/*
 * Copyright (c) 2008 Mark Kettenis
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

#ifndef _MACHINE_AUTOCONF_H_
#define _MACHINE_AUTOCONF_H_

#include <machine/bus.h>

struct mainbus_attach_args {
	bus_space_tag_t	ma_iot;
	char		*ma_name;
};

struct obio_attach_args {
	bus_space_tag_t	oa_iot;
	bus_addr_t	oa_offset;
	int		oa_ivec;
	char		*oa_name;
};

#define cf_offset	cf_loc[0]
#define cf_ivec		cf_loc[1]

typedef int (time_read_t)(time_t *sec);
typedef int (time_write_t)(time_t sec);

extern time_read_t *time_read;
extern time_write_t *time_write;

#endif /* _MACHINE_AUTOCONF_H_ */
