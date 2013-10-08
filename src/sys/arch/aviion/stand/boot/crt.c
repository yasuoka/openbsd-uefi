/*	$OpenBSD: crt.c,v 1.1 2013/10/08 21:55:21 miod Exp $ */

#include <sys/types.h>
#include <machine/prom.h>

#include "stand.h"

extern void boot(const char *, int, int, int);

/*
 * This is the boot code entry point.
 * Note that we do not bother to set r31, and use the default value supplied
 * by the PROM, which is the top of memory, minus the PROM data area (usually
 * 128KB).
 */
void
start(const char *args, int dev, int unit, int part)
{
	extern int edata, end;

	/* 
	 * This code enables the SFU1 and is used for single stage 
	 * bootstraps or the first stage of a two stage bootstrap.
	 * Do not use any low register to enable the SFU1. This wipes out
	 * the args.  Not cool at all... r25 seems free. 
	 */
	asm("	ldcr	%%r25,%%cr1" ::: "r25");
	asm("	clr	%r25,%r25,1<3>"); /* bit 3 is SFU1D */
	asm("	stcr	%r25,%cr1");

	memset(&edata, 0, ((int)&end - (int)&edata));

	boot(args, dev, unit, part);
	_rtt();
	/* NOTREACHED */
}
