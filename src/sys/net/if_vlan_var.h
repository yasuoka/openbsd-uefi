/*	$OpenBSD: if_vlan_var.h,v 1.11 2004/02/12 18:07:29 henning Exp $	*/

/*
 * Copyright 1998 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/net/if_vlan_var.h,v 1.3 1999/08/28 00:48:24 peter Exp $
 */

#ifndef _NET_IF_VLAN_VAR_H_
#define _NET_IF_VLAN_VAR_H_

#ifdef _KERNEL
#define mc_enm	mc_u.mcu_enm

struct vlan_mc_entry {
	LIST_ENTRY(vlan_mc_entry)	mc_entries;
	union {
		struct ether_multi	*mcu_enm;
	} mc_u;
	struct sockaddr_storage		mc_addr;
};

struct	ifvlan {
	struct	arpcom ifv_ac;	/* make this an interface */
	struct	ifnet *ifv_p;	/* parent interface of this vlan */
	struct	ifv_linkmib {
		int	ifvm_parent;
		u_int16_t ifvm_proto; /* encapsulation ethertype */
		u_int16_t ifvm_tag; /* tag to apply on packets leaving if */
	}	ifv_mib;
	LIST_HEAD(__vlan_mchead, vlan_mc_entry)	vlan_mc_listhead;
	LIST_ENTRY(ifvlan) ifv_list;
	int ifv_flags;
};

#define	ifv_if		ifv_ac.ac_if
#define	ifv_tag		ifv_mib.ifvm_tag
#define	IFVF_PROMISC	0x01
#endif /* _KERNEL */

struct	ether_vlan_header {
	u_char	evl_dhost[ETHER_ADDR_LEN];
	u_char	evl_shost[ETHER_ADDR_LEN];
	u_int16_t evl_encap_proto;
	u_int16_t evl_tag;
	u_int16_t evl_proto;
};

#define	EVL_VLANOFTAG(tag) ((tag) & 4095)
#define	EVL_PRIOFTAG(tag) (((tag) >> 13) & 7)
#define	EVL_ENCAPLEN	4	/* length in octets of encapsulation */

/* When these sorts of interfaces get their own identifier... */
#define	IFT_8021_VLAN	IFT_PROPVIRTUAL

/* sysctl(3) tags, for compatibility purposes */
#define	VLANCTL_PROTO	1
#define	VLANCTL_MAX	2

/*
 * Configuration structure for SIOCSETVLAN and SIOCGETVLAN ioctls.
 */
struct	vlanreq {
	char	vlr_parent[IFNAMSIZ];
	u_short	vlr_tag;
};
#define	SIOCSETVLAN	SIOCSIFGENERIC
#define	SIOCGETVLAN	SIOCGIFGENERIC

#ifdef _KERNEL
extern	int vlan_input(register struct ether_header *eh, struct mbuf *m);
extern	int vlan_input_tag(struct mbuf *m, u_int16_t t);
#endif /* _KERNEL */
#endif /* _NET_IF_VLAN_VAR_H_ */
