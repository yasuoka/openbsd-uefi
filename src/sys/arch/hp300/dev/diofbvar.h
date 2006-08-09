/*	$OpenBSD: diofbvar.h,v 1.9 2006/08/09 21:23:51 miod Exp $	*/

/*
 * Copyright (c) 2005, Miodrag Vallat
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef	_KERNEL

struct diocmap {
	u_int8_t r[256], g[256], b[256];
};

/*
 * Minimal frame buffer state structure.
 */
struct diofb {
	caddr_t	regkva;			/* KVA of registers */
	caddr_t	fbkva;			/* KVA of framebuffer */

	caddr_t	regaddr;		/* control registers physaddr */
	caddr_t	fbaddr;			/* frame buffer physaddr */

	u_int	fbsize;			/* frame buffer size */
	u_int	fbwidth;		/* frame buffer width */
	u_int	fbheight;		/* frame buffer height */
	u_int	dwidth;			/* displayed part width */
	u_int	dheight;		/* displayed part height */

	u_int	planes;			/* number of planes */
	u_int	planemask;		/* and related mask */

	/* rasops information */
	struct rasops_info ri;

	/* color information */
	struct diocmap cmap;

	/* wsdisplay information */
	struct wsscreen_descr wsd;
	struct wsscreen_list wsl;
	struct wsscreen_descr *scrlist[1];
	int	nscreens;
	u_int	mapmode;

	/* blockmove routine */
	void	(*bmv)(struct diofb *, u_int16_t, u_int16_t,
		    u_int16_t, u_int16_t, u_int16_t, u_int16_t, int);
};

/* Replacement Rules (rops) */
#define	RR_CLEAR		0x0
#define	RR_COPY			0x3
#define	RR_XOR			0x6
#define	RR_INVERT		0xa
#define	RR_COPYINVERTED  	0xc

void	diofb_cnattach(struct diofb *);
void	diofb_end_attach(void *, struct wsdisplay_accessops *, struct diofb *,
	    int, const char *);
int	diofb_fbinquire(struct diofb *, int, struct diofbreg *);
void	diofb_fbsetup(struct diofb *);
int	diofb_getcmap(struct diofb *, struct wsdisplay_cmap *);
void	diofb_resetcmap(struct diofb *);

int	diofb_alloc_attr(void *, int, int, int, long *);
int	diofb_alloc_screen(void *, const struct wsscreen_descr *, void **,
	    int *, int *, long *);
void	diofb_free_screen(void *, void *);
paddr_t	diofb_mmap(void *, off_t, int);
int	diofb_show_screen(void *, void *, int, void (*)(void *, int, int),
	    void *);

void	diofb_mono_windowmove(struct diofb *, u_int16_t, u_int16_t,
	    u_int16_t, u_int16_t, u_int16_t, u_int16_t, int);

/* Console support */
void	dvboxcninit(void);
void	gboxcninit(void);
void	hypercninit(void);
void	rboxcninit(void);
void	topcatcninit(void);
void	tvrxcninit(void);
extern	struct diofb diofb_cn;		/* struct diofb for console device */

#endif

/*
 * In mapped mode, mmap() will provide the following layout:
 * 0 - (DIOFB_REGSPACE - 1)	frame buffer registers
 * DIOFB_REGSPACE onwards	frame buffer memory
 */
#define	DIOFB_REGSPACE	0x020000
