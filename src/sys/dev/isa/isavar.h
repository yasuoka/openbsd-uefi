/*	$OpenBSD: isavar.h,v 1.24 1997/12/25 09:22:44 downsj Exp $	*/
/*	$NetBSD: isavar.h,v 1.24 1996/10/21 22:41:11 thorpej Exp $	*/
/*	$NetBSD: isapnpvar.h,v 1.5.4.2 1997/10/29 00:40:49 thorpej Exp $	*/

/*
 * Copyright (c) 1996 Christos Zoulas.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1995 Chris G. Demetriou
 * Copyright (c) 1992 Berkeley Software Design, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Berkeley Software
 *	Design, Inc.
 * 4. The name of Berkeley Software Design must not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	BSDI Id: isavar.h,v 1.5 1992/12/01 18:06:00 karels Exp 
 */

#ifndef _DEV_ISA_ISAVAR_H_
#define	_DEV_ISA_ISAVAR_H_

/*
 * Definitions for ISA and ISA PnP autoconfiguration.
 */

#include <sys/queue.h>
#include <machine/bus.h>

/* 
 * Structures and definitions needed by the machine-dependent header.
 */
struct isabus_attach_args;

#if (alpha + amiga + i386 + arc + wgrisc + power4e != 1)
ERROR: COMPILING ISA FOR UNSUPPORTED MACHINE, OR MORE THAN ONE.
#endif
#if alpha
#include <alpha/isa/isa_machdep.h>
#endif
#if amiga
#include <amiga/isa/isa_machdep.h>
#endif
#if i386
#include <i386/isa/isa_machdep.h>
#endif
#if arc
#include <arc/isa/isa_machdep.h>
#endif
#if wgrisc
#include <wgrisc/isa/isa_machdep.h>
#endif
#if power4e
#include <powerpc/isa/isa_machdep.h>
#endif

#include "isapnp.h"

#if NISAPNP > 0
/*
 * Structures and definitions needed by the machine-dependent header.
 */
struct isapnp_softc;

#if (i386 != 1)
ERROR: COMPILING ISAPNP FOR UNSUPPORTED MACHINE, OR MORE THAN ONE.
#endif
#if i386
#include <i386/isa/isapnp_machdep.h>
#endif
#endif	/* NISAPNP */

# define ISAPNP_WRITE_ADDR(sc, v) \
    bus_space_write_1(sc->sc_iot, sc->sc_addr_ioh, 0, v)
# define ISAPNP_WRITE_DATA(sc, v) \
    bus_space_write_1(sc->sc_iot, sc->sc_wrdata_ioh, 0, v)
# define ISAPNP_READ_DATA(sc) \
    bus_space_read_1(sc->sc_iot, sc->sc_read_ioh, 0)

# define ISAPNP_MALLOC(a) malloc(a, M_DEVBUF, M_WAITOK)
# define ISAPNP_FREE(a) free(a, M_DEVBUF)

#ifndef _DEV_ISA_ISAPNPREG_H_
/*
 * `reg' defines needed only for these structures.
 */
#define ISAPNP_MAX_CARDS 	8
#define ISAPNP_MAX_IDENT	32
#define ISAPNP_MAX_DEVCLASS	16
#define ISAPNP_SERIAL_SIZE	9

#define ISAPNP_NUM_MEM		4
#define ISAPNP_NUM_IO		8
#define ISAPNP_NUM_IRQ		16
#define ISAPNP_NUM_DRQ		8
#define ISAPNP_NUM_MEM32	4
#endif	/* _DEV_ISA_ISAPNPREG_H_ */

/*
 * ISA PnP-specific structures.
 */
struct isapnp_softc {
	struct device		sc_dev;
	int			sc_read_port;
	bus_space_tag_t		sc_iot;
	bus_space_tag_t		sc_memt;
	bus_space_handle_t	sc_addr_ioh;
	bus_space_handle_t	sc_wrdata_ioh;
	bus_space_handle_t	sc_read_ioh;
	bus_space_handle_t	sc_memh;
	u_int8_t		sc_ncards;
    	u_int8_t		sc_id[ISAPNP_MAX_CARDS][ISAPNP_SERIAL_SIZE];
};

struct isapnp_region {
	bus_space_handle_t h;
	u_int32_t base;

	u_int32_t minbase;
	u_int32_t maxbase;
	u_int32_t length;
	u_int32_t align;
	u_int8_t  flags;
};

struct isapnp_pin {
	int16_t	  num;
	u_int8_t  flags:4;
	u_int8_t  type:4;
	u_int16_t bits;
};

struct isapnp_knowndev {
	const char *pnpid;
	const char *driver;
};

/*
 * ISA bus attach arguments
 */
struct isabus_attach_args {
	char	*iba_busname;		/* XXX should be common */
	bus_space_tag_t iba_iot;	/* isa i/o space tag */
	bus_space_tag_t iba_memt;	/* isa mem space tag */
	isa_chipset_tag_t iba_ic;
};

/*
 * ISA/ISA PnP shared driver attach arguments
 */
struct isa_attach_args {
	struct device  *ia_isa;		/* isa device */
	bus_space_tag_t ia_iot;		/* isa i/o space tag */
	bus_space_tag_t ia_memt;	/* isa mem space tag */

	bus_space_handle_t ia_delaybah;	/* i/o handle for `delay port' */

	isa_chipset_tag_t ia_ic;

	/*
	 * ISA PnP configuration support.  `ipa_' prefixes are used to denote
	 * PnP specific members of this structure.
	 */
	struct isa_attach_args	*ipa_sibling;
	struct isa_attach_args	*ipa_child;

	char	ipa_devident[ISAPNP_MAX_IDENT];
	char	ipa_devlogic[ISAPNP_MAX_DEVCLASS];
	char	ipa_devcompat[ISAPNP_MAX_DEVCLASS];
	char	ipa_devclass[ISAPNP_MAX_DEVCLASS];

	u_char	ipa_pref;
	u_char	ipa_devnum;

	u_char	ipa_nio;
	u_char	ipa_nirq;
	u_char	ipa_ndrq;
	u_char	ipa_nmem;
	u_char	ipa_nmem32;

	struct isapnp_region	ipa_io[ISAPNP_NUM_IO];
	struct isapnp_region	ipa_mem[ISAPNP_NUM_MEM];
	struct isapnp_region	ipa_mem32[ISAPNP_NUM_MEM32];
	struct isapnp_pin	ipa_irq[ISAPNP_NUM_IRQ];
	struct isapnp_pin	ipa_drq[ISAPNP_NUM_DRQ];

	/*
	 * Compatibility defines for ISA drivers.
	 */
#define ia_iobase	ipa_io[0].base
#define ia_iosize	ipa_io[0].length
#define ia_irq		ipa_irq[0].num
#define	ia_drq		ipa_drq[0].num
#define ia_maddr	ipa_mem[0].base
#define ia_msize	ipa_mem[0].length
#define ia_ioh		ipa_io[0].h

	void	*ia_aux;		/* driver specific */
};

#define	IOBASEUNK	-1		/* i/o address is unknown */
#define	IRQUNK		-1		/* interrupt request line is unknown */
#define	DRQUNK		-1		/* DMA request line is unknown */
#define	MADDRUNK	-1		/* shared memory address is unknown */

/*
 * Per-device ISA variables
 */
struct isadev {
	struct  device *id_dev;		/* back pointer to generic */
	TAILQ_ENTRY(isadev)
		id_bchain;		/* bus chain */
};

/*
 * ISA master bus
 */
struct isa_softc {
	struct	device sc_dev;		/* base device */
	TAILQ_HEAD(, isadev)
		sc_subdevs;		/* list of all children */

	bus_space_tag_t sc_iot;		/* isa io space tag */
	bus_space_tag_t sc_memt;	/* isa mem space tag */

	isa_chipset_tag_t sc_ic;

	/*
	 * This i/o handle is used to map port 0x84, which is
	 * read to provide a 1.25us delay.  This access handle
	 * is mapped in isaattach(), and exported to drivers
	 * via isa_attach_args.
	 */
	bus_space_handle_t   sc_delaybah;
};

#define		cf_iobase		cf_loc[0]
#define		cf_iosize		cf_loc[1]
#define		cf_maddr		cf_loc[2]
#define		cf_msize		cf_loc[3]
#define		cf_irq			cf_loc[4]
#define		cf_drq			cf_loc[5]

/*
 * ISA interrupt handler manipulation.
 * 
 * To establish an ISA interrupt handler, a driver calls isa_intr_establish()
 * with the interrupt number, type, level, function, and function argument of
 * the interrupt it wants to handle.  Isa_intr_establish() returns an opaque
 * handle to an event descriptor if it succeeds, and invokes panic() if it
 * fails.  (XXX It should return NULL, then drivers should handle that, but
 * what should they do?)  Interrupt handlers should return 0 for "interrupt
 * not for me", 1  for "I took care of it", or -1 for "I guess it was mine,
 * but I wasn't expecting it."
 *
 * To remove an interrupt handler, the driver calls isa_intr_disestablish() 
 * with the handle returned by isa_intr_establish() for that handler.
 */

/* ISA interrupt sharing types */
char	*isa_intr_typename __P((int type));

void	isascan __P((struct device *parent, void *match));
int	isaprint __P((void *, const char *));

#ifdef NEWCONFIG
/*
 * Establish a device as being on the ISA bus (XXX NOT IMPLEMENTED).
 */
void isa_establish __P((struct isadev *, struct device *));
#endif

/*
 * ISA PnP prototypes and support macros.
 */
static __inline void isapnp_write_reg __P((struct isapnp_softc *, int, u_char));
static __inline u_char isapnp_read_reg __P((struct isapnp_softc *, int));

static __inline void
isapnp_write_reg(sc, r, v)
	struct isapnp_softc *sc;
	int r;
	u_char v;
{
	ISAPNP_WRITE_ADDR(sc, r);
	ISAPNP_WRITE_DATA(sc, v);
}

static __inline u_char
isapnp_read_reg(sc, r)
	struct isapnp_softc *sc;
{
	ISAPNP_WRITE_ADDR(sc, r);
	return ISAPNP_READ_DATA(sc);
}

struct isa_attach_args *
    isapnp_get_resource __P((struct isapnp_softc *, int));
char *isapnp_id_to_vendor __P((char *, const u_char *));

int isapnp_config __P((bus_space_tag_t, bus_space_tag_t,
    struct isa_attach_args *));
void isapnp_unconfig __P((bus_space_tag_t, bus_space_tag_t,
    struct isa_attach_args *));

#ifdef DEBUG_ISAPNP
void isapnp_print_mem __P((const char *, const struct isapnp_region *));
void isapnp_print_io __P((const char *, const struct isapnp_region *));
void isapnp_print_irq __P((const char *, const struct isapnp_pin *));
void isapnp_print_drq __P((const char *, const struct isapnp_pin *));
void isapnp_print_dep_start __P((const char *, const u_char));
void isapnp_print_attach __P((const struct isa_attach_args *));
void isapnp_get_config __P((struct isapnp_softc *,
	struct isa_attach_args *));
void isapnp_print_config __P((const struct isa_attach_args *));
#endif	/* DEBUG_ISAPNP */
#endif /* _DEV_ISA_ISAVAR_H_ */
