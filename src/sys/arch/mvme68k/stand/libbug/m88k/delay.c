/*	$OpenBSD: delay.c,v 1.2 1996/04/28 10:48:49 deraadt Exp $ */

/*
 * bug routines -- assumes that the necessary sections of memory
 * are preserved.
 */
#include <sys/types.h>
#include <machine/prom.h>

/* BUG - timing routine */
void
mvmeprom_delay(msec)
	int msec;
{
	asm volatile ("or r2,r0,%0": : "r" (msec));
	MVMEPROM_CALL(MVMEPROM_DELAY);
}
