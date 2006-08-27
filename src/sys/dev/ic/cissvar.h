/*	$OpenBSD: cissvar.h,v 1.4 2006/08/27 22:12:10 mickey Exp $	*/

/*
 * Copyright (c) 2005,2006 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

struct sensor;

struct ciss_ld {
	struct ciss_blink bling;	/* a copy of blink state */
	char	xname[16];		/* copy of the sdN name */
	int	ndrives;
	u_int8_t tgts[1];
};

struct ciss_softc {
	struct device	sc_dev;
	struct scsi_link sc_link;
	struct scsi_link *sc_link_raw;
	struct timeout	sc_hb;
	void		*sc_ih;
	void		*sc_sh;
	struct proc	*sc_thread;
	int		sc_flush;
	struct sensor	*sensors;

	u_int	sc_flags;
	int ccblen, maxcmd, maxsg, nbus, ndrives, maxunits;
	ciss_queue_head	sc_free_ccb, sc_ccbq, sc_ccbdone;

	bus_space_tag_t	iot;
	bus_space_handle_t ioh, cfg_ioh;
	bus_dma_tag_t	dmat;
	bus_dmamap_t	cmdmap;
	bus_dma_segment_t cmdseg[1];
	void		*ccbs;
	void		*scratch;

	struct ciss_config cfg;
	int cfgoff;
	u_int32_t iem;
	u_int32_t heartbeat;
	struct ciss_ld **sc_lds;
};

struct ciss_rawsoftc {
	struct scsi_link sc_link;
	struct ciss_softc *sc_softc;
	u_int8_t	sc_channel;
};

/* XXX These have to become spinlocks in case of fine SMP */
#define	CISS_LOCK(sc) splbio()
#define	CISS_UNLOCK(sc, lock) splx(lock)
#define	CISS_LOCK_SCRATCH(sc) splbio()
#define	CISS_UNLOCK_SCRATCH(sc, lock) splx(lock)
typedef	int ciss_lock_t;

int	ciss_attach(struct ciss_softc *sc);
int	ciss_intr(void *v);
