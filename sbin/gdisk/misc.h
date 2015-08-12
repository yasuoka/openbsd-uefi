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

#ifndef _MISC_H
#define _MISC_H

#include "cmd.h"

/* typedefs */

struct unit_type {
	char	*abbr;
	int	conversion;
	char	*lname;
};
extern struct unit_type unit_types[];
#define SECTORS	1	/* units are bytes/sectors/kbytes/mbytes/gbytes */

/* Constants */
#define ASK_HEX 0x01
#define ASK_DEC 0x02
#define UNIT_TYPE_DEFAULT 1

/* Prototypes */
int unit_lookup(char *);
int ask_cmd(cmd_t *);
int ask_num(const char *, int, int, int);
void ask_string(const char *, char *, int);
int ask_pid(int);
int ask_yn(const char *);
uint16_t getshort(void *);
uint32_t getlong(void *);
void putshort(void *, uint16_t);
void putlong(void *, uint32_t);
uint64_t getuint(disk_t *, char *, uint64_t, uint64_t);

#endif /* _MISC_H */

