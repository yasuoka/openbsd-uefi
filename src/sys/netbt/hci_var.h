/*	$OpenBSD: hci_var.h,v 1.1 2005/01/14 12:04:02 grange Exp $	*/

/*
 * ng_btsocket_hci_raw.h
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: hci_var.h,v 1.1 2005/01/14 12:04:02 grange Exp $
 * $FreeBSD: src/sys/netgraph/bluetooth/include/ng_btsocket_hci_raw.h,v 1.3 2003/11/14 03:45:29 emax Exp $
 */

#ifndef _NETGRAPH_BTSOCKET_HCI_RAW_H_
#define _NETGRAPH_BTSOCKET_HCI_RAW_H_

#define NG_BTSOCKET_HCI_RAW_SENDSPACE	(4 * 1024)
#define NG_BTSOCKET_HCI_RAW_RECVSPACE	(4 * 1024)

/*
 * Bluetooth raw HCI socket PCB
 */

struct ng_btsocket_hci_raw_pcb {
	struct socket				*so;     /* socket */
	u_int32_t				 flags;  /* flags */
#define NG_BTSOCKET_HCI_RAW_DIRECTION	(1 << 0)
#define NG_BTSOCKET_HCI_RAW_PRIVILEGED	(1 << 1)
	struct sockaddr_hci			 addr;   /* local address */
	struct ng_btsocket_hci_raw_filter	 filter; /* filter */
	u_int32_t				 token;  /* message token */
	struct ng_mesg				*msg;    /* message */
	LIST_ENTRY(ng_btsocket_hci_raw_pcb)	 next;   /* link to next */
#if 0
	struct mtx				 pcb_mtx; /* pcb mutex */
#endif
};
typedef struct ng_btsocket_hci_raw_pcb		ng_btsocket_hci_raw_pcb_t;
typedef struct ng_btsocket_hci_raw_pcb *	ng_btsocket_hci_raw_pcb_p;

#define	so2hci_raw_pcb(so) \
	((struct ng_btsocket_hci_raw_pcb *)((so)->so_pcb))

/*
 * Bluetooth raw HCI socket methods
 */

#ifdef _KERNEL

void hci_raw_init(void);

int  ng_btsocket_hci_raw_abort      (struct socket *);
int  ng_btsocket_hci_raw_attach     (struct socket *, int, struct proc *);
int  ng_btsocket_hci_raw_bind       (struct socket *, struct sockaddr *, 
                                     struct proc *);
int  ng_btsocket_hci_raw_connect    (struct socket *, struct sockaddr *, 
                                     struct proc *);
int  ng_btsocket_hci_raw_control    (struct socket *, u_long, caddr_t,
                                     struct ifnet *, struct proc *);
int  hci_raw_ctloutput(int, struct socket *, int, int, struct mbuf **);
int  ng_btsocket_hci_raw_detach     (struct socket *);
int  ng_btsocket_hci_raw_disconnect (struct socket *);
int  ng_btsocket_hci_raw_peeraddr   (struct socket *, struct sockaddr **);
int  ng_btsocket_hci_raw_send       (struct socket *, int, struct mbuf *,
                                     struct sockaddr *, struct mbuf *,
                                     struct proc *);
int  ng_btsocket_hci_raw_sockaddr   (struct socket *, struct sockaddr **);

int  hci_raw_usrreq(struct socket *, int, struct mbuf *, struct mbuf *,
	struct mbuf *);

void ng_btsocket_hci_raw_data_input(struct mbuf *);
int ng_btsocket_hci_raw_node_rcvdata(struct ifnet *, struct mbuf *);

#endif /* _KERNEL */
 
#endif /* ndef _NETGRAPH_BTSOCKET_HCI_RAW_H_ */
