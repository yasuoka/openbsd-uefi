/*	$OpenBSD: svr4_fuser.h,v 1.2 1996/08/02 20:35:38 niklas Exp $	 */
/*	$NetBSD: svr4_fuser.h,v 1.3 1994/10/29 00:43:20 christos Exp $	 */

/*
 * Copyright (c) 1994 Christos Zoulas
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#ifndef	_SVR4_FUSER_H_
#define	_SVR4_FUSER_H_

#include <compat/svr4/svr4_types.h>

struct svr4_f_user {
	svr4_pid_t	fu_pid;
	int		fu_flags;
	uid_t		fu_uid;
};


#define SVR4_F_FILE_ONLY	1
#define SVR4_F_CONTAINED	2

#define SVR4_F_CDIR	0x01
#define SVR4_F_RDIR	0x02
#define SVR4_F_TEXT	0x04
#define SVR4_F_MAP	0x08
#define SVR4_F_OPEN	0x10
#define SVR4_F_TRACE	0x20
#define SVR4_F_TTY	0x40

#endif /* !_SVR4_FUSER_H_ */
