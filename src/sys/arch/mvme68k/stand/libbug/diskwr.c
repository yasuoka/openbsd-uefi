/*	$OpenBSD: diskwr.c,v 1.1 1996/05/07 11:25:07 deraadt Exp $ */

/*
 * bug routines -- assumes that the necessary sections of memory
 * are preserved.
 */
#include <sys/types.h>
#include <machine/prom.h>

/* returns 0: success, nonzero: error */
int
mvmeprom_diskwr(arg)
	struct mvmeprom_dskio *arg;
{
	int ret;

	MVMEPROM_ARG1(arg);
	MVMEPROM_CALL(MVMEPROM_DSKWR);
	MVMEPROM_STATRET(ret);
}
