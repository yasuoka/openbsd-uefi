/*	$OpenBSD: boot.c,v 1.29 2003/08/11 06:23:07 deraadt Exp $	*/

/*
 * Copyright (c) 2003 Dale Rahn
 * Copyright (c) 1997,1998 Michael Shalayeff
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <libsa.h>
#include <lib/libsa/loadfile.h>
#include <lib/libkern/funcs.h>

#include "cmd.h"

static const char *const kernels[] = {
	"/bsd",
	"/obsd",
	"/bsd.old",
	NULL
};

extern	const char version[];
struct cmd_state cmd;
int bootprompt = 1;

void
boot(dev_t bootdev)
{
	const char *bootfile = kernels[0];
	int i = 0, try = 0, st;
	u_long marks[MARK_MAX];

	machdep();

	printf(">> OpenBSD/" MACHINE " BOOT %s\n", version);

	devboot(bootdev, cmd.bootdev);
	strlcpy(cmd.image, bootfile, sizeof(cmd.image));
	cmd.boothowto = 0;
	cmd.conf = "/etc/boot.conf";
	cmd.addr = (void *)DEFAULT_KERNEL_ADDRESS;
	cmd.timeout = 5;

	st = read_conf();
	if (!bootprompt)
		snprintf(cmd.path, sizeof cmd.path, "%s:%s",
		    cmd.bootdev, cmd.image);

	while (1) {
		/* no boot.conf, or no boot cmd in there */
		if (bootprompt && st <= 0)
			do {
				printf("boot> ");
			} while(!getcmd());
		st = 0;
		bootprompt = 1;	/* allow reselect should we fail */

		printf("booting %s: ", cmd.path);
		marks[MARK_START] = (u_long)cmd.addr;
		if (loadfile(cmd.path, marks, LOAD_ALL) >= 0)
			break;

		if (kernels[++i] == NULL) {
			try += 1;
			bootfile = kernels[i=0];
		} else
			bootfile = kernels[i];
		strlcpy(cmd.image, bootfile, sizeof(cmd.image));
		printf(" failed(%d). will try %s\n", errno, bootfile);

		if (try < 2)
			cmd.timeout++;
		else {
			if (cmd.timeout)
				printf("Turning timeout off.\n");
			cmd.timeout = 0;
		}
	}

	/* exec */
	run_loadfile(marks, cmd.boothowto);
}

#ifdef _TEST
int
main()
{
	boot(0);
	return 0;
}
#endif
