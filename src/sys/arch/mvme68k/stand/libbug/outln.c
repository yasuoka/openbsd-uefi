/*	$OpenBSD: outln.c,v 1.2 1996/05/16 02:25:40 chuck Exp $ */

/*
 * bug routines -- assumes that the necessary sections of memory
 * are preserved.
 */
#include <sys/types.h>
#include <machine/prom.h>

#include "libbug.h"

void
mvmeprom_outln(start, end)
	char *start, *end;
{
	MVMEPROM_ARG1(end);
	MVMEPROM_ARG2(start);
	MVMEPROM_CALL(MVMEPROM_OUTSTRCRLF);
}
