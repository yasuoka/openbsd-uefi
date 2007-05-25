/*	$OpenBSD: if_malovar.h,v 1.1 2007/05/25 05:33:51 mglocker Exp $ */

/*
 * Copyright (c) 2007 Marcus Glocker <mglocker@openbsd.org>
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

/* simplify bus space access */
#define MALO_READ_1(sc, reg) \
	bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define MALO_READ_2(sc, reg) \
	bus_space_read_2((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define	MALO_READ_MULTI_2(sc, reg, off, size) \
	bus_space_read_multi_2((sc)->sc_iot, (sc)->sc_ioh, (reg), (off), \
	(size))
#define MALO_WRITE_1(sc, reg, val) \
	bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define MALO_WRITE_2(sc, reg, val) \
	bus_space_write_2((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define MALO_WRITE_MULTI_2(sc, reg, off, size) \
	bus_space_write_multi_2((sc)->sc_iot, (sc)->sc_ioh, (reg), (off), \
	(size))

/* miscellaneous */
#define FW_HELPER_BSIZE		256	/* helper FW block size */
#define FW_HELPER_OK		0x10	/* helper FW loaded */
#define FW_MAIN_MAX_RETRY	20	/* main FW block resend max retry */
#define CMD_BUFFER_SIZE		256	/* cmd buffer */

/* FW command header */
struct malo_cmd_header {
	uint16_t	cmd;
	uint16_t	size;
	uint16_t	seqnum;
	uint16_t	result;
};

/* FW command bodies */
struct malo_cmd_body_spec {
	uint16_t	hw_if_version;
	uint16_t	hw_version;
	uint16_t	num_of_wcb;
	uint16_t	num_of_mcast;
	uint8_t		macaddr[ETHER_ADDR_LEN];
	uint16_t	regioncode;
	uint16_t	num_of_antenna;
	uint32_t	fw_version;
	uint32_t	wcbbase;
	uint32_t	rxpdrdptr;
	uint32_t	rxpdwrptr;
	uint32_t	fw_capinfo;
};

struct malo_softc {
	struct device		 sc_dev;

	struct ieee80211com	 sc_ic;

	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;

	void			*sc_cmd;
};
