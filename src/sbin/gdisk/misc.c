/*
 * Copyright (c) 1997 Tobias Weingartner
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

#include <sys/types.h>
#include <sys/disklabel.h>
#include <err.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "misc.h"

struct unit_type unit_types[] = {
	{"b", 1			, "Bytes"},
	{" ", DEV_BSIZE		, "Sectors"},
	{"K", 1024		, "Kilobytes"},
	{"M", 1024 * 1024	, "Megabytes"},
	{"G", 1024 * 1024 *1024	, "Gigabytes"},
	{NULL, 0		, NULL },
};

int
unit_lookup(char *units)
{
	int i = 0;
	if (units == NULL)
		return (UNIT_TYPE_DEFAULT);

	while (unit_types[i].abbr != NULL) {
		if (strncasecmp(unit_types[i].abbr, units, 1) == 0)
			break;
		i++;
	}
	/* default */
	if (unit_types[i].abbr == NULL)
		return (UNIT_TYPE_DEFAULT);

	return (i);
}

int
ask_cmd(cmd_t *cmd)
{
	char lbuf[100], *cp, *buf;
	size_t lbuflen;

	/* Get input */
	if (fgets(lbuf, sizeof lbuf, stdin) == NULL)
		errx(1, "eof");
	lbuflen = strlen(lbuf);
	if (lbuflen > 0 && lbuf[lbuflen - 1] == '\n')
		lbuf[lbuflen - 1] = '\0';

	/* Parse input */
	buf = lbuf;
	buf = &buf[strspn(buf, " \t")];
	cp = &buf[strcspn(buf, " \t")];
	*cp++ = '\0';
	strncpy(cmd->cmd, buf, sizeof(cmd->cmd));
	buf = &cp[strspn(cp, " \t")];
	strncpy(cmd->args, buf, sizeof(cmd->args));

	return (0);
}

int
ask_num(const char *str, int dflt, int low, int high)
{
	char lbuf[100];
	const char *errstr;
	size_t lbuflen;
	int num;

	if (dflt < low)
		dflt = low;
	else if (dflt > high)
		dflt = high;

	do {
		printf("%s [%d - %d]: [%d] ", str, low, high, dflt);

		if (fgets(lbuf, sizeof lbuf, stdin) == NULL)
			errx(1, "eof");

		lbuflen = strlen(lbuf);
		if (lbuflen > 0 && lbuf[lbuflen - 1] == '\n')
			lbuf[lbuflen - 1] = '\0';

		if (lbuf[0] == '\0') {
			num = dflt;
			errstr = NULL;
		} else {
			num = (int)strtonum(lbuf, low, high, &errstr);
			if (errstr)
				printf("%s is %s: %s.\n", str, errstr, lbuf);
		}
	} while (errstr);

	return (num);
}

void
ask_string(const char *str, char *buf, int len)
{
	printf("%s: ", str);
	if (fgets(buf, len, stdin) == NULL)
		errx(1, "eof");

	len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = '\0';
}

int
ask_pid(int dflt)
{
	char lbuf[100], *cp;
	size_t lbuflen;
	int num;
	const int low = 0, high = 0xffff;

	if (dflt < low)
		dflt = low;
	else if (dflt > high)
		dflt = high;

	do {
again:
		printf("Partition id ('0' to disable) [%X - %X]: [%X] ", low,
		    high, dflt);
		printf("(? for help) ");

		if (fgets(lbuf, sizeof lbuf, stdin) == NULL)
			errx(1, "eof");
		lbuflen = strlen(lbuf);
		if (lbuflen > 0 && lbuf[lbuflen - 1] == '\n')
			lbuf[lbuflen - 1] = '\0';

		if (lbuf[0] == '?') {
			PRT_printall();
			goto again;
		}

		/* Convert */
		cp = lbuf;
		num = strtol(lbuf, &cp, 16);

		/* Make sure only number present */
		if (cp == lbuf)
			num = dflt;
		if (*cp != '\0') {
			printf("'%s' is not a valid number.\n", lbuf);
			num = low - 1;
		} else if (num < low || num > high) {
			printf("'%x' is out of range.\n", num);
		}
	} while (num < low || num > high);

	return (num);
}

int
ask_yn(const char *str)
{
	int ch, first;
	extern int y_flag;

	if (y_flag)
		return (1);

	printf("%s [n] ", str);
	fflush(stdout);

	first = ch = getchar();
	while (ch != '\n' && ch != EOF)
		ch = getchar();

	if (ch == EOF || first == EOF)
		errx(1, "eof");

	return (first == 'y' || first == 'Y');
}

uint16_t
getshort(void *p)
{
	unsigned char *cp = p;

	return (cp[0] | (cp[1] << 8));
}

void
putshort(void *p, uint16_t l)
{
	unsigned char *cp = p;

	*cp++ = l;
	*cp++ = l >> 8;
}

uint32_t
getlong(void *p)
{
	unsigned char *cp = p;

	return (cp[0] | (cp[1] << 8) | (cp[2] << 16) | (cp[3] << 24));
}

void
putlong(void *p, uint32_t l)
{
	unsigned char *cp = p;

	*cp++ = l;
	*cp++ = l >> 8;
	*cp++ = l >> 16;
	*cp++ = l >> 24;
}

/*
 * adapted from sbin/disklabel/editor.c
 */
uint64_t
getuint(disk_t *disk, char *prompt, uint64_t oval, uint64_t maxval)
{
	char buf[BUFSIZ], *endptr, *p, operator = '\0';
	size_t n;
	int mult = 1, secsize = unit_types[SECTORS].conversion;
	double d, d2;
	int saveerr;
	char unit;

	if (oval > maxval)
		oval = maxval;

	do {
		printf("%s: [%llu] ", prompt, oval);

		if (fgets(buf, sizeof(buf), stdin) == NULL)
			errx(1, "eof");

		n = strlen(buf);
		if (n > 0 && buf[n-1] == '\n')
			buf[--n] = '\0';

		if (buf[0] == '\0') {
			return (oval);
		} else if (buf[0] == '*' && buf[1] == '\0') {
			return (maxval);
		}

		/* deal with units */
		switch (tolower(buf[n-1])) {
		case 'b':
			unit = 'b';
			mult = -secsize;
			buf[--n] = '\0';
			break;
		case 's':
			unit = 's';
			mult = 1;
			buf[--n] = '\0';
			break;
		case 'k':
			unit = 'k';
			if (secsize > 1024)
				mult = -secsize / 1024;
			else
				mult = 1024 / secsize;
			buf[--n] = '\0';
			break;
		case 'm':
			unit = 'm';
			mult = 1048576 / secsize;
			buf[--n] = '\0';
			break;
		case 'g':
			unit = 'g';
			mult = 1073741824 / secsize;
			buf[--n] = '\0';
			break;
		default:
			unit = ' ';
			mult = 1;
			break;
		}

		/* deal with the operator */
		p = &buf[0];
		if (*p == '+' || *p == '-')
			operator = *p++;
		else
			operator = ' ';

		endptr = p;
		errno = 0;
		d = strtod(p, &endptr);
		saveerr = errno;
		d2 = d;
		if (mult > 0)
			d *= mult;
		else {
			d /= (-mult);
			d2 = d;
		}

		/* Apply the operator */
		if (operator == '+')
			d = oval + d;
		else if (operator == '-') {
			d = oval - d;
			d2 = d;
		}

		if (saveerr == ERANGE || d > maxval || d < 0 || d < d2) {
			printf("%s is out of range: %c%s%c\n", prompt, operator,
			    p, unit); 
		} else if (*endptr != '\0') {
			printf("%s is invalid: %c%s%c\n", prompt, operator,
			    p, unit); 
		} else {
			break;
		}
	} while (1);

	return ((uint64_t)d);
}
