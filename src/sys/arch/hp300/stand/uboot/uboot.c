/*	$OpenBSD: uboot.c,v 1.7 2014/02/23 19:22:40 miod Exp $	*/
/*	$NetBSD: uboot.c,v 1.3 1997/04/27 21:17:13 thorpej Exp $	*/

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
 *	@(#)boot.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#define _KERNEL
#include <sys/fcntl.h>
#undef _KERNERL

#include <machine/exec.h>
#include <a.out.h>

#include <lib/libsa/stand.h>

#include "samachdep.h"

/*
 * Boot program... bits in `howto' determine whether boot stops to
 * ask for system name.	 Boot device is derived from ROM provided
 * information.
 */

char line[100];

extern	u_int opendev;
extern	char *lowram;
extern	int noconsole;

extern	const char version[];

extern int devparse(const char *, int *, int *, int *, int *, int *, char **);

/*
 * XXX UFS accepts a /, NFS doesn't.
 */
char *name;
char *names[] = {
	"bsd"
};
#define NUMNAMES	(sizeof(names) / sizeof(char *))

static int bdev, badapt, bctlr, bunit, bpart;

void	boot(char *, void *, int);
void	getbootdev(int *);
int	loadrandom(const char *, void *, size_t);

char   rnddata[BOOTRANDOM_MAX];

int
main(void)
{
	int currname = 0;

	printf("\n>> OpenBSD [%dKB] UNIFIED BOOT %s HP 9000/%s CPU\n",
	       (__LDPGSZ / 1024), version, getmachineid());
	printf(">> Enter \"reset\" to reset system.\n");

	bdev   = B_TYPE(bootdev);
	badapt = B_ADAPTOR(bootdev);
	bctlr  = B_CONTROLLER(bootdev);
	bunit  = B_UNIT(bootdev);
	bpart  = B_PARTITION(bootdev);

	for (;;) {
		name = names[currname++];
		if (currname == NUMNAMES)
			currname = 0;

		if (!noconsole) {
			howto = 0;
			getbootdev(&howto);
		} else
			printf(": %s\n", name);

		boot(name, lowram, howto);
		printf("boot: %s\n", strerror(errno));
	}
	return (0);
}

void
getbootdev(int *howto)
{
	char c, *ptr = line;

	printf("Boot: [[[%s%d%c:]%s][-acds]] :- ",
	    devsw[bdev].dv_name, bctlr + (8 * badapt), 'a' + bpart, name);

	if (tgets(line)) {
		if (strcmp(line, "reset") == 0) {
			call_req_reboot();      /* reset machine */
			printf("panic: can't reboot, halting\n");
			asm("stop #0x2700");
		}
		while ((c = *ptr) != '\0') {
			while (c == ' ')
				c = *++ptr;
			if (!c)
				return;
			if (c == '-')
				while ((c = *++ptr) && c != ' ')
					switch (c) {
					case 'a':
						*howto |= RB_ASKNAME;
						continue;
					case 'b':
						*howto |= RB_HALT;
						continue;
					case 'c':
						*howto |= RB_CONFIG;
						continue;
					case 'd':
						*howto |= RB_KDB;
						continue;
					case 's':
						*howto |= RB_SINGLE;
						continue;
					}
			else {
				name = ptr;
				while ((c = *++ptr) && c != ' ');
				if (c)
					*ptr++ = 0;
			}
		}
	} else
		printf("\n");
}

void
boot(char *path, void *loadaddr, int howto)
{
	static int rnd_loaded = 0;
	char rndpath[MAXPATHLEN];
	int dev, adapt, ctlr, unit, part;
	char *fname;

	/*
	 * Try and load randomness from the boot device if this is a disk.
	 */
	if (rnd_loaded == 0) {
		if (devparse(path, &dev, &adapt, &ctlr, &unit, &part,
		    &fname) != 0) {
			/*
			 * If the device name is bogus, devlookup() has
			 * complained loudly. No need to attempt to load a
			 * kernel from the same string.
			 */
			return;
		}
		if (fname - path < sizeof(rndpath)) {
			memcpy(rndpath, path, fname - path);
			rndpath[fname - path] = '\0';
			strlcat(rndpath, BOOTRANDOM, sizeof rndpath);
			rnd_loaded = loadrandom(rndpath, rnddata,
			    sizeof(rnddata));
		}
	}

	exec(path, loadaddr, howto);
}

int
loadrandom(const char *path, void *buf, size_t buflen)
{
	struct stat sb;
	int fd;
	int rc = 0;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		if (errno != EPERM)
			printf("cannot open %s: %s\n", path, strerror(errno));
		return 0;
	}
	if (fstat(fd, &sb) == -1 || sb.st_uid != 0 || !S_ISREG(sb.st_mode) ||
	    (sb.st_mode & (S_IWOTH|S_IROTH)))
		goto fail;
	(void) read(fd, buf, buflen);
	rc = 1;
fail:
	close(fd);
	return rc;
}
