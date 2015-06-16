/*	$OpenBSD: bpf.h,v 1.49 2015/06/16 11:09:39 mpi Exp $	*/
/*	$NetBSD: bpf.h,v 1.15 1996/12/13 07:57:33 mikel Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
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
 *	@(#)bpf.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NET_BPF_H_
#define _NET_BPF_H_

/* BSD style release date */
#define BPF_RELEASE 199606

typedef	int32_t	bpf_int32;
typedef u_int32_t	bpf_u_int32;
/*
 * Alignment macros.  BPF_WORDALIGN rounds up to the next even multiple of
 * BPF_ALIGNMENT (which is at least as much as what a timeval needs).
 */
#define BPF_ALIGNMENT sizeof(u_int32_t)
#define BPF_WORDALIGN(x) (((x) + (BPF_ALIGNMENT - 1)) & ~(BPF_ALIGNMENT - 1))

#define BPF_MAXINSNS 512
#define BPF_MAXBUFSIZE (2 * 1024 * 1024)
#define BPF_MINBUFSIZE 32

/*
 *  Structure for BIOCSETF.
 */
struct bpf_program {
	u_int bf_len;
	struct bpf_insn *bf_insns;
};

/*
 * Struct returned by BIOCGSTATS.
 */
struct bpf_stat {
	u_int bs_recv;		/* number of packets received */
	u_int bs_drop;		/* number of packets dropped */
};

/*
 * Struct return by BIOCVERSION.  This represents the version number of
 * the filter language described by the instruction encodings below.
 * bpf understands a program iff kernel_major == filter_major &&
 * kernel_minor >= filter_minor, that is, if the value returned by the
 * running kernel has the same major number and a minor number equal
 * equal to or less than the filter being downloaded.  Otherwise, the
 * results are undefined, meaning an error may be returned or packets
 * may be accepted haphazardly.
 * It has nothing to do with the source code version.
 */
struct bpf_version {
	u_short bv_major;
	u_short bv_minor;
};
/* Current version number of filter architecture. */
#define BPF_MAJOR_VERSION 1
#define BPF_MINOR_VERSION 1

/*
 * BPF ioctls
 */
#define	BIOCGBLEN	_IOR('B',102, u_int)
#define	BIOCSBLEN	_IOWR('B',102, u_int)
#define	BIOCSETF	_IOW('B',103, struct bpf_program)
#define	BIOCFLUSH	_IO('B',104)
#define BIOCPROMISC	_IO('B',105)
#define	BIOCGDLT	_IOR('B',106, u_int)
#define BIOCGETIF	_IOR('B',107, struct ifreq)
#define BIOCSETIF	_IOW('B',108, struct ifreq)
#define BIOCSRTIMEOUT	_IOW('B',109, struct timeval)
#define BIOCGRTIMEOUT	_IOR('B',110, struct timeval)
#define BIOCGSTATS	_IOR('B',111, struct bpf_stat)
#define BIOCIMMEDIATE	_IOW('B',112, u_int)
#define BIOCVERSION	_IOR('B',113, struct bpf_version)
#define BIOCSRSIG	_IOW('B',114, u_int)
#define BIOCGRSIG	_IOR('B',115, u_int)
#define BIOCGHDRCMPLT	_IOR('B',116, u_int)
#define BIOCSHDRCMPLT	_IOW('B',117, u_int)
#define	BIOCLOCK	_IO('B',118)
#define	BIOCSETWF	_IOW('B',119, struct bpf_program)
#define BIOCGFILDROP	_IOR('B',120, u_int)
#define BIOCSFILDROP	_IOW('B',121, u_int)
#define BIOCSDLT	_IOW('B',122, u_int)
#define BIOCGDLTLIST	_IOWR('B',123, struct bpf_dltlist)
#define BIOCGDIRFILT	_IOR('B',124, u_int)
#define BIOCSDIRFILT	_IOW('B',125, u_int)
#define BIOCGQUEUE	_IOR('B',126, u_int)
#define BIOCSQUEUE	_IOW('B',127, u_int)

/*
 * Direction filters for BIOCSDIRFILT/BIOCGDIRFILT
 */
#define BPF_DIRECTION_IN	1
#define BPF_DIRECTION_OUT	(1<<1)

struct bpf_timeval {
	u_int32_t	tv_sec;
	u_int32_t	tv_usec;
};

/*
 * Structure prepended to each packet.
 */
struct bpf_hdr {
	struct bpf_timeval bh_tstamp;	/* time stamp */
	u_int32_t	bh_caplen;	/* length of captured portion */
	u_int32_t	bh_datalen;	/* original length of packet */
	u_int16_t	bh_hdrlen;	/* length of bpf header (this struct
					   plus alignment padding) */
};
/*
 * Because the structure above is not a multiple of 4 bytes, some compilers
 * will insist on inserting padding; hence, sizeof(struct bpf_hdr) won't work.
 * Only the kernel needs to know about it; applications use bh_hdrlen.
 * XXX To save a few bytes on 32-bit machines, we avoid end-of-struct
 * XXX padding by using the size of the header data elements.  This is
 * XXX fail-safe: on new machines, we just use the 'safe' sizeof.
 */
#ifdef _KERNEL
#if defined(__arm__) || defined(__i386__) || defined(__m68k__) || \
    defined(__mips__) || defined(__ns32k__) || defined(__sparc__) || \
    defined(__sparc64__) || defined(__vax__)
#define SIZEOF_BPF_HDR 18
#else
#define SIZEOF_BPF_HDR sizeof(struct bpf_hdr)
#endif
#endif

/*
 * Data-link level type codes.
 */
#define DLT_NULL		0	/* no link-layer encapsulation */
#define DLT_EN10MB		1	/* Ethernet (10Mb) */
#define DLT_EN3MB		2	/* Experimental Ethernet (3Mb) */
#define DLT_AX25		3	/* Amateur Radio AX.25 */
#define DLT_PRONET		4	/* Proteon ProNET Token Ring */
#define DLT_CHAOS		5	/* Chaos */
#define DLT_IEEE802		6	/* IEEE 802 Networks */
#define DLT_ARCNET		7	/* ARCNET */
#define DLT_SLIP		8	/* Serial Line IP */
#define DLT_PPP			9	/* Point-to-point Protocol */
#define DLT_FDDI		10	/* FDDI */
#define DLT_ATM_RFC1483		11	/* LLC/SNAP encapsulated atm */
#define DLT_LOOP		12	/* loopback type (af header) */
#define DLT_ENC			13	/* IPSEC enc type (af header, spi, flags) */
#define DLT_RAW			14	/* raw IP */
#define DLT_SLIP_BSDOS		15	/* BSD/OS Serial Line IP */
#define DLT_PPP_BSDOS		16	/* BSD/OS Point-to-point Protocol */
#define DLT_PFSYNC		18	/* Packet filter state syncing */
#define DLT_PPP_SERIAL		50	/* PPP over Serial with HDLC */
#define DLT_PPP_ETHER		51	/* PPP over Ethernet; session only w/o ether header */
#define DLT_C_HDLC		104	/* Cisco HDLC */
#define DLT_IEEE802_11		105	/* IEEE 802.11 wireless */
#define DLT_PFLOG		117	/* Packet filter logging, by pcap people */
#define DLT_IEEE802_11_RADIO	127	/* IEEE 802.11 plus WLAN header */
#define DLT_MPLS		219	/* MPLS Provider Edge header */

/*
 * The instruction encodings.
 */
/* instruction classes */
#define BPF_CLASS(code) ((code) & 0x07)
#define		BPF_LD		0x00
#define		BPF_LDX		0x01
#define		BPF_ST		0x02
#define		BPF_STX		0x03
#define		BPF_ALU		0x04
#define		BPF_JMP		0x05
#define		BPF_RET		0x06
#define		BPF_MISC	0x07

/* ld/ldx fields */
#define BPF_SIZE(code)	((code) & 0x18)
#define		BPF_W		0x00
#define		BPF_H		0x08
#define		BPF_B		0x10
#define BPF_MODE(code)	((code) & 0xe0)
#define		BPF_IMM		0x00
#define		BPF_ABS		0x20
#define		BPF_IND		0x40
#define		BPF_MEM		0x60
#define		BPF_LEN		0x80
#define		BPF_MSH		0xa0

/* alu/jmp fields */
#define BPF_OP(code)	((code) & 0xf0)
#define		BPF_ADD		0x00
#define		BPF_SUB		0x10
#define		BPF_MUL		0x20
#define		BPF_DIV		0x30
#define		BPF_OR		0x40
#define		BPF_AND		0x50
#define		BPF_LSH		0x60
#define		BPF_RSH		0x70
#define		BPF_NEG		0x80
#define		BPF_JA		0x00
#define		BPF_JEQ		0x10
#define		BPF_JGT		0x20
#define		BPF_JGE		0x30
#define		BPF_JSET	0x40
#define BPF_SRC(code)	((code) & 0x08)
#define		BPF_K		0x00
#define		BPF_X		0x08

/* ret - BPF_K and BPF_X also apply */
#define BPF_RVAL(code)	((code) & 0x18)
#define		BPF_A		0x10

/* misc */
#define BPF_MISCOP(code) ((code) & 0xf8)
#define		BPF_TAX		0x00
#define		BPF_TXA		0x80

/*
 * The instruction data structure.
 */
struct bpf_insn {
	u_int16_t	code;
	u_char		jt;
	u_char		jf;
	u_int32_t	k;
};

/*
 * Structure to retrieve available DLTs for the interface.
 */
struct bpf_dltlist {
	u_int	bfl_len;	/* number of bfd_list array */
	u_int	*bfl_list;	/* array of DLTs */
};

/*
 * Macros for insn array initializers.
 */
#define BPF_STMT(code, k) { (u_int16_t)(code), 0, 0, k }
#define BPF_JUMP(code, k, jt, jf) { (u_int16_t)(code), jt, jf, k }

#ifdef _KERNEL
struct ifnet;

int	 bpf_validate(struct bpf_insn *, int);
int	 bpf_tap(caddr_t, u_char *, u_int, u_int);
void	 bpf_mtap(caddr_t, struct mbuf *, u_int);
void	 bpf_mtap_hdr(caddr_t, caddr_t, u_int, struct mbuf *, u_int,
	    void (*)(const void *, void *, size_t));
void	 bpf_mtap_af(caddr_t, u_int32_t, struct mbuf *, u_int);
void	 bpf_mtap_ether(caddr_t, struct mbuf *, u_int);
void	 bpfattach(caddr_t *, struct ifnet *, u_int, u_int);
void	 bpfdetach(struct ifnet *);
void	 bpfilterattach(int);
u_int	 bpf_filter(struct bpf_insn *, u_char *, u_int, u_int);
#endif /* _KERNEL */

/*
 * Number of scratch memory words (for BPF_LD|BPF_MEM and BPF_ST).
 */
#define BPF_MEMWORDS 16

#endif /* _NET_BPF_H_ */
