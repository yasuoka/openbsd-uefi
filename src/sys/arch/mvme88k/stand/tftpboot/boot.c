/*	$OpenBSD: boot.c,v 1.3 2008/04/02 21:53:18 miod Exp $ */
/*	$NetBSD: boot.c,v 1.2 1995/09/23 03:42:52 gwr Exp $ */

/*-
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *
 * 	@(#)boot.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/reboot.h>

#include <machine/prom.h>

#include "stand.h"
#include "libsa.h"

extern char *version;
char line[80];

int
main()
{
	char *cp, *file;
	int flag, ret;
	int ask = 0;

	printf("\n>> OpenBSD/mvme88k tftpboot [%s]\n", version);

	ret = parse_args(&file, &flag);
	for (;;) {
		if (ask) {
			printf("boot: ");
			gets(line);
			if (line[0]) {
				bugargs.arg_start = line;
				cp = line;
				while (cp < (line + sizeof(line) -1) && *cp)
					cp++;
				bugargs.arg_end = cp;
				ret = parse_args(&file, &flag);
			}
		}
		if (ret) {
			printf("boot: -q returning to MVME-Bug\n");
			break;
		}
		exec_mvme(file, flag);
		printf("boot: %s: %s\n", file, strerror(errno));
		ask = 1;
	}
	return (0);
}
