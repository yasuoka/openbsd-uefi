/*	$OpenBSD: syscon.c,v 1.3 2006/11/16 23:21:56 miod Exp $ */
/*
 * Copyright (c) 1999 Steve Murphree, Jr.
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

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>

#include <machine/avcommon.h>
#include <aviion/dev/sysconreg.h>

struct sysconsoftc {
	struct device	sc_dev;

	struct intrhand sc_abih;	/* `abort' switch */
	struct intrhand sc_acih;	/* `ac fail' */
	struct intrhand sc_sfih;	/* `sys fail' */
};

void	sysconattach(struct device *, struct device *, void *);
int	sysconmatch(struct device *, void *, void *);

int	syscon_print(void *, const char *);
int	syscon_scan(struct device *, void *, void *);
int	sysconabort(void *);
int	sysconacfail(void *);
int	sysconsysfail(void *);
int	sysconav400(void *);

struct cfattach syscon_ca = {
	sizeof(struct sysconsoftc), sysconmatch, sysconattach
};

struct cfdriver syscon_cd = {
	NULL, "syscon", DV_DULL
};

int
sysconmatch(struct device *parent, void *cf, void *args)
{
	return (syscon_cd.cd_ndevs == 0);
}

void
sysconattach(struct device *parent, struct device *self, void *args)
{
	struct sysconsoftc *sc = (struct sysconsoftc *)self;

	printf("\n");

	/*
	 * Clear SYSFAIL if lit.
	 */
	*(volatile u_int32_t *)AV_UCSR |= UCSR_DRVSFBIT;

	/*
	 * pseudo driver, abort interrupt handler
	 */
	sc->sc_abih.ih_fn = sysconabort;
	sc->sc_abih.ih_arg = 0;
	sc->sc_abih.ih_wantframe = 1;
	sc->sc_abih.ih_ipl = IPL_ABORT;

	sc->sc_acih.ih_fn = sysconacfail;
	sc->sc_acih.ih_arg = 0;
	sc->sc_acih.ih_wantframe = 1;
	sc->sc_acih.ih_ipl = IPL_ABORT;

	sc->sc_sfih.ih_fn = sysconsysfail;
	sc->sc_sfih.ih_arg = 0;
	sc->sc_sfih.ih_wantframe = 1;
	sc->sc_sfih.ih_ipl = IPL_ABORT;

	sysconintr_establish(SYSCV_ABRT, &sc->sc_abih, "abort");
	sysconintr_establish(SYSCV_ACF, &sc->sc_acih, "acfail");
	sysconintr_establish(SYSCV_SYSF, &sc->sc_sfih, "sysfail");

	config_search(syscon_scan, self, args);
}

int
syscon_scan(struct device *parent, void *child, void *args)
{
	struct cfdata *cf = child;
	struct confargs oca, *ca = args;

	bzero(&oca, sizeof oca);
	oca.ca_iot = ca->ca_iot;
	oca.ca_offset = (paddr_t)cf->cf_loc[0];
	if (oca.ca_offset != (paddr_t)-1)
		oca.ca_paddr = ca->ca_paddr + oca.ca_offset;
	else
		oca.ca_paddr = (paddr_t)-1;
	oca.ca_ipl = (u_int)cf->cf_loc[1];

	if ((*cf->cf_attach->ca_match)(parent, cf, &oca) == 0)
		return (0);

	config_attach(parent, cf, &oca, syscon_print);
	return (1);
}

int
syscon_print(void *args, const char *pnp)
{
	struct confargs *ca = args;

	if (ca->ca_offset != (paddr_t)-1)
		printf(" offset 0x%x", ca->ca_offset);
	if (ca->ca_ipl != (u_int)-1)
		printf(" ipl %u", ca->ca_ipl);
	return (UNCONF);
}

int
sysconintr_establish(u_int vec, struct intrhand *ih, const char *name)
{
#ifdef DIAGNOSTIC
	if (vec < 0 || vec >= SYSCON_NVEC) {
		printf("sysconintr_establish: illegal vector 0x%x\n", vec);
		return (EINVAL);
	}
#endif

	return (intr_establish(SYSCON_VECT + vec, ih, name));
}

int
sysconabort(void *eframe)
{
	*(volatile u_int32_t *)AV_CLRINT = ISTATE_ABORT;
	nmihand(eframe);
	return (1);
}

int
sysconsysfail(void *eframe)
{
	*(volatile u_int32_t *)AV_CLRINT = ISTATE_SYSFAIL;
	printf("WARNING: SYSFAIL* ASSERTED\n");
	return (1);
}

int
sysconacfail(void *eframe)
{
	*(volatile u_int32_t *)AV_CLRINT = ISTATE_ACFAIL;
	printf("WARNING: ACFAIL* ASSERTED\n");
	return (1);
}
