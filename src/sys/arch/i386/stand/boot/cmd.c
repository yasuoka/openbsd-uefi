/*	$OpenBSD: cmd.c,v 1.12 1997/04/26 17:50:07 mickey Exp $	*/

/*
 * Copyright (c) 1997 Michael Shalayeff
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
#include <string.h>
#include <libsa.h>
#include <debug.h>
#include <sys/reboot.h>
#include "cmd.h"
#ifndef _TEST
#include <biosdev.h>
#endif

extern int debug;

#define CTRL(c)	((c)&0x1f)

static int Xaddr __P((register struct cmd_state *));
static int Xboot __P((register struct cmd_state *));
static int Xcd __P((register struct cmd_state *));
static int Xcp __P((register struct cmd_state *));
static int Xdevice __P((register struct cmd_state *));
#ifdef DEBUG
static int Xdebug __P((register struct cmd_state *));
#endif
static int Xhelp __P((register struct cmd_state *));
static int Ximage __P((register struct cmd_state *));
static int Xls __P((register struct cmd_state *));
static int Xnope __P((register struct cmd_state *));
static int Xreboot __P((register struct cmd_state *));
static int Xregs __P((register struct cmd_state *));
static int Xset __P((register struct cmd_state *));
static int Xhowto __P((register struct cmd_state *));
static int Xtty __P((register struct cmd_state *));

struct cmd_table {
	char *cmd_name;
	int (*cmd_exec) __P((register struct cmd_state *));
	const struct cmd_table *cmd_table;
};

static const struct cmd_table cmd_set[] = {
	{"addr",      Xaddr},
	{"boothowto", Xhowto},
#ifdef DEBUG	
	{"debug",     Xdebug},
#endif
	{"device",    Xdevice},
	{"tty",       Xtty},
	{"image",     Ximage},
	{NULL,0}
};

static const struct cmd_table cmd_table[] = {
	{"boot",   Xboot}, /* XXX must be first */
	{"cd",     Xcd},
	{"cp",     Xcp},
	{"help",   Xhelp},
	{"ls",     Xls},
	{"nope",   Xnope},
	{"reboot", Xreboot},
	{"regs",   Xregs},
	{"set",    Xset, cmd_set},
	{NULL, 0},
};

extern const char version[];
static void ls __P((char *, register struct stat *));
static int readline __P((register char *, int));
char *nextword __P((register char *));
static int bootparse __P((register struct cmd_state *, int));
static char *whatcmd
	__P((register const struct cmd_table **, register char *));
static int docmd __P((register struct cmd_state *));
static char *qualify __P((register struct cmd_state *, char *));

static char cmd_buf[133];

int
getcmd(cmd)
	register struct cmd_state *cmd;
{
	cmd->cmd = NULL;

	if (!readline(cmd_buf, cmd->timeout))
		cmd->cmd = cmd_table;

	return docmd(cmd);
}

int
read_conf(cmd)
	register struct cmd_state *cmd;
{
#ifndef INSECURE
	struct stat sb;
#endif
	int fd, eof = 0;

	if ((fd = open(qualify(cmd, cmd->conf), 0)) < 0) {
		if (errno != ENOENT) {
			printf("open(%s): %s\n", cmd->path, strerror(errno));
			return 0;
		}
		return -1;
	}

#ifndef INSECURE
	(void) fstat(fd, &sb);
	if (sb.st_uid || (sb.st_mode & 2)) {
		printf("non-secure %s, will not proceed\n", cmd->path);
		close(fd);
		return -1;
	}
#endif

	do {
		register char *p = cmd_buf;

		cmd->cmd = NULL;

		do
			eof = read(fd, p, 1);
		while (eof > 0 && *p++ != '\n');

		if (eof < 0)
			printf("%s: %s\n", cmd->path, strerror(errno));
		else
			*--p = '\0';

	} while (eof > 0 && !(eof = docmd(cmd)));

	close(fd);
	return eof;
}

static int
docmd(cmd)
	register struct cmd_state *cmd;
{
	const struct cmd_table *ct;
	register char *p = NULL;

	cmd->argc = 1;
	if (cmd->cmd == NULL) {

		/* command */
		for (p = cmd_buf; *p && (*p == ' ' || *p == '\t'); p++)
			;
		if (*p == '#' || *p == '\0') { /* comment or empty string */
#ifdef DEBUG
			printf("rem\n");
#endif
			return 0;
		}
		ct = cmd_table;
		cmd->argv[cmd->argc] = p; /* in case it's shortcut boot */
		p = whatcmd(&ct, p);
		if (ct == NULL) {
			cmd->argc++;
			ct = cmd_table;
		} else if (ct->cmd_table != NULL && p != NULL) {
			const struct cmd_table *cs = ct->cmd_table;
			p = whatcmd(&cs, p);
			if (cs == NULL) {
				printf("%s: syntax error\n", ct->cmd_name);
				return 0;
			}
			ct = cs;
		}
		cmd->cmd = ct;
	}

	cmd->argv[0] = ct->cmd_name;
	while (p && cmd->argc+1 < sizeof(cmd->argv) / sizeof(cmd->argv[0])) {
		cmd->argv[cmd->argc++] = p;
		p = nextword(p);
	}
	cmd->argv[cmd->argc] = NULL;

	return (*cmd->cmd->cmd_exec)(cmd);
}

static char *
whatcmd(ct, p)
	register const struct cmd_table **ct;
	register char *p;
{
	register char *q;
	register int l;

	q = nextword(p);

	for (l = 0; p[l]; l++)
		;

	while ((*ct)->cmd_name != NULL && strncmp(p, (*ct)->cmd_name, l))
		(*ct)++;

	if ((*ct)->cmd_name == NULL)
		*ct = NULL;

	return q;
}

static int
readline(buf, to)
	register char *buf;
	int	to;
{
	char *p = buf, *pe = buf, ch;
	int i;

	for (i = to; i-- && !ischar(); )
#ifndef _TEST
		if ((to = usleep(100000))) {
			printf ("usleep failed (%d)\n", to);
			i = 1;
			break;
		}
#else
		;
#endif
	if (i < 0)
		return 0;

	while (1) {
		switch ((ch = getchar())) {
		case CTRL('r'):
			while (pe > buf)
				putchar('\b');
			printf(buf);
		case CTRL('u'):
			while (pe >= buf) {
				pe--;
				putchar('\b');
			}
			p = pe = buf;
			continue;
		case '\n':
			pe[1] = *pe = '\0';
			break;
		case '\b':
			if (p > buf) {
				putchar('\b');
				p--;
				pe--;
			}
			continue;
		default:
			pe++;
			*p++ = ch;
			continue;
		}
		break;
	}

	return pe - buf;
}

/*
 * Search for spaces/tabs after the current word. If found, \0 the
 * first one.  Then pass a pointer to the first character of the
 * next word, or NULL if there is no next word. 
 */
char *
nextword(p)
	register char *p;
{
	/* skip blanks */
	while (*p && *p != '\t' && *p != ' ')
		p++;
	if (*p) {
		*p++ = '\0';
		while (*p == '\t' || *p == ' ')
			p++;
	}
	if (*p == '\0')
		p = NULL;
	return p;
}

#ifdef DEBUG
static int
Xdebug(cmd)
	struct cmd_state *cmd;
{
	if (cmd->argc !=2)
		printf("debug=%s\n", (debug? "on": "off"));
	else
		debug = (cmd->argv[1][0] == '0' ||
			 (cmd->argv[1][0] == 'o' && cmd->argv[1][1] == 'f'))?
			 0: 1;
	return 0;
}
#endif

static int
Xhelp(cmd)
	register struct cmd_state *cmd;
{
	register const struct cmd_table *ct;

	printf("commands: ");
	for (ct = cmd_table; ct->cmd_name != NULL; ct++)
		printf(" %s", ct->cmd_name);
	putchar('\n');

	return 0;
}

/* called only w/ no arguments */
static int
Xset(cmd)
	register struct cmd_state *cmd;
{
	register const struct cmd_table *ct;

	printf("OpenBSD boot[%s]\n", version);
	printf("cwd=%s\n", cmd->cwd);
	for (ct = cmd_set; ct->cmd_name != NULL; ct++)
		(*ct->cmd_exec)(cmd);
	return 0;
}

static int
Xdevice(cmd)
	register struct cmd_state *cmd;
{
	if (cmd->argc != 2)
		printf("device=%s\n", cmd->bootdev);
	else
		strncpy(cmd->bootdev, cmd->argv[1], sizeof(cmd->bootdev));
	return 0;
}

static int
Ximage(cmd)
	register struct cmd_state *cmd;
{
	if (cmd->argc != 2)
		printf("image=%s\n", cmd->image);
	else
		strncpy(cmd->image, cmd->argv[1], sizeof(cmd->image));
	return 0;
}

static int
Xaddr(cmd)
	register struct cmd_state *cmd;
{
	register char *p;

	if (cmd->argc != 2)
		printf("addr=%p\n", cmd->addr);
	else {
		register u_long a;

		p = cmd->argv[1];
		if (p[0] == '0' && p[1] == 'x')
			p += 2;
		for (a = 0; *p != '\0'; p++) {
			a <<= 4;
			a |= (isdigit(*p)? *p - '0':
			      10 + tolower(*p) - 'a') & 0xf;
		}
		cmd->addr = (void *)a;
	}
	return 0;
}

static int
Xtty(cmd)
	register struct cmd_state *cmd;
{
	if (cmd->argc == 1)
		printf("tty=%s\n", ttyname(0));
	else {
	}

	return 0;
}

static int
Xls(cmd)
	register struct cmd_state *cmd;
{
	struct stat sb;
	register char *p;
	int fd;

	if (stat(qualify(cmd, (cmd->argv[1]? cmd->argv[1]: "/.")), &sb) < 0) {
		printf("stat(%s): %s\n", cmd->path, strerror(errno));
		return 0;
	}

	if ((sb.st_mode & S_IFMT) != S_IFDIR)
		ls(cmd->path, &sb);
	else {
		if ((fd = opendir(cmd->path)) < 0) {
			printf ("opendir(%s): %s\n", cmd->path,
				strerror(errno));
			return 0;
		}

		/* no strlen in lib !!! */
		for (p = cmd->path; *p; p++);
		*p++ = '/';
		*p = '\0';

		while(readdir(fd, p) >= 0) {
			if (stat(cmd->path, &sb) < 0)
				printf("stat(%s): %s\n", cmd->path,
					strerror(errno));
			else
				ls(p, &sb);
		}
		closedir (fd);
	}
	return 0;
}

#define lsrwx(mode,s) \
	putchar ((mode) & S_IROTH? 'r' : '-'); \
	putchar ((mode) & S_IWOTH? 'w' : '-'); \
	putchar ((mode) & S_IXOTH? *(s): (s)[1]);

static void
ls(name, sb)
	register char *name;
	register struct stat *sb;
{
	putchar("-fc-d-b---l-s-w-"[(sb->st_mode & S_IFMT) >> 12]);
	lsrwx(sb->st_mode >> 6, (sb->st_mode & S_ISUID? "sS" : "x-"));
	lsrwx(sb->st_mode >> 3, (sb->st_mode & S_ISUID? "sS" : "x-"));
	lsrwx(sb->st_mode     , (sb->st_mode & S_ISTXT? "tT" : "x-"));

	printf (" %u,%u\t%lu\t%s\n", sb->st_uid, sb->st_gid,
		(u_long)sb->st_size, name);
}
#undef lsrwx

static int
Xhowto(cmd)
	register struct cmd_state *cmd;
{
	if (cmd->argc < 2) {
		printf("boothowto=");
		if (boothowto) {
			putchar('-');
			if (boothowto & RB_ASKNAME)
				putchar('a');
			if (boothowto & RB_HALT)
				putchar('b');
			if (boothowto & RB_CONFIG)
				putchar('c');
			if (boothowto & RB_SINGLE)
				putchar('s');
			if (boothowto & RB_KDB)
				putchar('d');
		}
		putchar('\n');
	} else
		bootparse(cmd, 1);
	return 0;
}

static int
Xboot(cmd)
	register struct cmd_state *cmd;
{
	if (cmd->argc > 1 && cmd->argv[1][0] != '-') {
		qualify(cmd, (cmd->argv[1]? cmd->argv[1]: cmd->image));
		if (bootparse(cmd, 2))
			return 0;
	} else {
		if (bootparse(cmd, 1))
			return 0;
		sprintf(cmd->path, "%s:%s%s", cmd->bootdev,
			cmd->cwd, cmd->image);
	}

	return 1;
}

/*
 * Qualifies the path adding neccessary dev&|cwd
 */

static char *
qualify(cmd, name)
	register struct cmd_state *cmd;
	char *name;
{
	register char *p;

	for (p = name; *p; p++)
		if (*p == ':')
			break;
	if (*p == ':')
		if (p[1] == '/')
			sprintf(cmd->path, "%s", name);
		else
			sprintf(cmd->path, "%s%s%s", name, cmd->cwd, name);
	else if (name[0] == '/')
		sprintf(cmd->path, "%s:%s", cmd->bootdev, name);
	else
		sprintf(cmd->path, "%s:%s%s", cmd->bootdev, cmd->cwd, name);
	return cmd->path;
}

static int
bootparse(cmd, i)
	register struct cmd_state *cmd;
	int i;
{
	register char *cp;
	int howto = boothowto;

	for (; i < cmd->argc; i++) {
		cp = cmd->argv[i];
		if (*cp == '-') {
			while (*++cp) {
				switch (*cp) {
				case 'a':
					howto |= RB_ASKNAME;
					break;
				case 'b':
					howto |= RB_HALT;
					break;
				case 'c':
					howto |= RB_CONFIG;
					break;
				case 's':
					howto |= RB_SINGLE;
					break;
				case 'd':
					howto |= RB_KDB;
					break;
				default:
					printf("howto: bad option: %c\n", *cp);
					return 1;
				}
			}
		} else {
			printf("boot: illegal argument %s\n", cmd->argv[i]);
			return 1;
		}
	}
	boothowto = howto;
	return 0;
}

static int
Xcd(cmd)
	register struct cmd_state *cmd;
{
	register char *p, *q;
	struct stat sb;

	/* cd home */
	if (cmd->argc == 1) {
		cmd->cwd[0] = '/';
		cmd->cwd[1] = '\0';
		return 0;
	}

	/* cd '.' */
	if (cmd->argv[1][0] == '.' && cmd->argv[1][1] == '\0')
		return 0;

	/* cd '..' */
	if (cmd->argv[1][0] == '.' && cmd->argv[1][1] == '.'
	    && cmd->argv[1][2] == '\0') {
		/* strrchr(cmd->cwd, '/'); */
		for (p = cmd->cwd; *++p;);
		for (p--; *--p != '/';);
		p[1] = '\0';
		return 0;
	}

	/* cd dir */
	sprintf(cmd->path, "%s:%s%s",
		cmd->bootdev, cmd->cwd, cmd->argv[1]);

	if (stat(cmd->path, &sb) < 0) {
		printf("stat(%s): %s\n", cmd->argv[1], strerror(errno));
		return 0;
	}

	if (!S_ISDIR(sb.st_mode)) {
		printf("boot: %s: not a dir\n", cmd->argv[1]);
		return 0;
	}

	/* change dir */
	for (p = cmd->cwd; *p; p++);
	for (q = cmd->argv[1]; (*p++ = *q++) != '\0';);
	if (p[-2] != '/') {
		p[-1] = '/';
		p[0] = '\0';
	}

	return 0;
}

static int
Xreboot(cmd)
	register struct cmd_state *cmd;
{
	printf("Rebooting...\n");
	exit();
	return 0; /* just in case */
}

static int
Xregs(cmd)
	register struct cmd_state *cmd;
{
	DUMP_REGS;
	return 0;
}

static int
Xnope(cmd)
	register struct cmd_state *cmd;
{
	return 0;
}

static int
Xcp(cmd)
	register struct cmd_state *cmd;
{
	printf("cp: no writable filesystems\n");
	return 0;
}

