/*	$NetBSD: memreg.c,v 1.7 1995/02/01 12:37:53 pk Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *
 *	@(#)memreg.c	8.1 (Berkeley) 6/11/93
 */

#include <sys/param.h>
#include <sys/device.h>

#include <machine/autoconf.h>
#include <machine/ctlreg.h>

#include <sparc/sparc/memreg.h>
#include <sparc/sparc/vaddrs.h>

static int memregmatch __P((struct device *, void *, void *));
static void memregattach __P((struct device *, struct device *, void *));
struct cfdriver memregcd =
    { 0, "memreg", memregmatch, memregattach, DV_DULL, sizeof(struct device) };

/*
 * The OPENPROM calls this "memory-error".
 */
static int
memregmatch(parent, vcf, aux)
	struct device *parent;
	void *aux, *vcf;
{
	struct cfdata *cf = vcf;
	register struct confargs *ca = aux;

	if (cputyp == CPU_SUN4) {
		if (ca->ca_bustype == BUS_OBIO)
			return (strcmp(cf->cf_driver->cd_name,
				       ca->ca_ra.ra_name) == 0);
		return (0);
	}
	return (strcmp("memory-error", ca->ca_ra.ra_name) == 0);
}

/* ARGSUSED */
static void
memregattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	register struct confargs *ca = aux;
	register struct romaux *ra = &ca->ca_ra;

	if (cputyp == CPU_SUN4) {
		if (par_err_reg == NULL)
			panic("memregattach");
		ra->ra_vaddr = (caddr_t)par_err_reg;
	} else {
		par_err_reg = ra->ra_vaddr ? (volatile int *)ra->ra_vaddr :
		    (volatile int *)mapiodev(ra->ra_reg, 0, sizeof(int),
		    ca->ca_bustype);
	}
	printf("\n");
}

/*
 * Synchronous and asynchronous memory error handler.
 * (This is the level 15 interrupt, which is not vectored.)
 * Should kill the process that got its bits clobbered,
 * and take the page out of the page pool, but for now...
 */
void
memerr(issync, ser, sva, aer, ava)
	int issync, ser, sva, aer, ava;
{

	if (cputyp == CPU_SUN4) {
		if (par_err_reg) {
			printf("mem err: ser=%b sva=%x\n", ser, SER_BITS, sva);
			printf("parity error register = %b\n",
				*par_err_reg, PER_BITS);
		} else {
			printf("mem err: ser=? sva=?\n");
			printf("parity error register not mapped yet!\n"); /* XXX */
		}
	} else {
		printf("%ssync mem err: ser=%b sva=%x aer=%b ava=%x\n",
		    issync ? "" : "a", ser, SER_BITS, sva, aer & 0xff,
		    AER_BITS, ava);
		if (par_err_reg)
			printf("parity error register = %b\n",
				*par_err_reg, PER_BITS);
	}
#ifdef DEBUG
	callrom();
#else
	panic("memory error");		/* XXX */
#endif
}
