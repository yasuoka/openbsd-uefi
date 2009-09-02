/*	$OpenBSD: workq.h,v 1.6 2009/09/02 14:05:05 dlg Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2007 Ted Unangst <tedu@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SYS_WORKQ_H_
#define _SYS_WORKQ_H_

#include <sys/queue.h>

typedef void (*workq_fn)(void *, void *);

struct workq_task {
	int		wqt_flags;
	workq_fn	wqt_func;
	void		*wqt_arg1;
	void		*wqt_arg2;

	SIMPLEQ_ENTRY(workq_task) wqt_entry;
};

#define WQ_WAITOK	(1<<0)
#define WQ_MPSAFE	(1<<1)

struct workq;

struct workq	*workq_create(const char *, int, int);
int		workq_add_task(struct workq *, int /* flags */, workq_fn,
		    void *, void *);
void		workq_queue_task(struct workq *, struct workq_task *,
		    int /* flags */, workq_fn, void *, void *);
void		workq_destroy(struct workq *);

#endif /* _SYS_WORKQ_H_ */
