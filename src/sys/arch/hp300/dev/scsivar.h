/*	$OpenBSD: scsivar.h,v 1.7 2003/06/02 23:27:45 millert Exp $	*/
/*	$NetBSD: scsivar.h,v 1.7 1997/03/31 07:40:05 scottr Exp $	*/

/*
 * Copyright (c) 1997 Jason R. Thorpe.  All rights reserved.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Van Jacobson of Lawrence Berkeley Laboratory.
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
 *	@(#)scsivar.h	8.1 (Berkeley) 6/10/93
 */

#include <sys/queue.h>

/*
 * A SCSI job queue entry.  Target drivers each have of of these,
 * used to queue requests with the initiator.
 */
struct scsiqueue {
	TAILQ_ENTRY(scsiqueue) sq_list;	/* entry on queue */
	void	*sq_softc;		/* target's softc */
	int	sq_target;		/* target on bus */
	int	sq_lun;			/* lun on target */

	/*
	 * Callbacks used to start and stop the target driver.
	 */
	void	(*sq_start)(void *);
	void	(*sq_go)(void *);
	void	(*sq_intr)(void *, int);
};

struct scsi_inquiry;
struct scsi_fmt_cdb;

struct oscsi_attach_args {
	int	osa_target;	/* target */
	int	osa_lun;	/* logical unit */
				/* inquiry data */
	struct	scsi_inquiry *osa_inqbuf;
};

#ifdef _KERNEL
int	scsi_print(void *, const char *);

void	scsi_delay(int);
void	scsistart(void *);
void	scsireset(int);
int	scsi_test_unit_rdy(int, int, int);
int	scsi_request_sense(int, int, int, u_char *, u_int);
int	scsi_immed_command(int, int, int, struct scsi_fmt_cdb *,
				u_char *, u_int, int);
int	scsi_tt_read(int, int, int, u_char *, u_int, daddr_t, int);
int	scsi_tt_write(int, int, int, u_char *, u_int, daddr_t, int);
int	scsireq(struct device *, struct scsiqueue *);
int	scsiustart(int);
void	scsistart(void *);
int	scsigo(int, int, int, struct buf *, struct scsi_fmt_cdb *, int);
void	scsidone(void *);
int	scsiintr(void *);
void	scsifree(struct device *, struct scsiqueue *);
int	scsi_tt_oddio(int, int, int, u_char *, u_int, int, int);
void	scsi_str(char *, char *, size_t);
int	scsi_probe_device(int, int, int, struct scsi_inquiry *, int);
#endif
