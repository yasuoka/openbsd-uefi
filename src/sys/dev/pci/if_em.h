/**************************************************************************

Copyright (c) 2001-2002 Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms of the Software, with or
without modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code of the Software may retain the above
    copyright notice, this list of conditions and the following disclaimer.

 2. Redistributions in binary form of the Software may reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.

 3. Neither the name of the Intel Corporation nor the names of its
    contributors shall be used to endorse or promote products derived from
    this Software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR ITS CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.

***************************************************************************/

/*$FreeBSD$*/

#ifndef _EM_H_DEFINED_
#define _EM_H_DEFINED_

#include <dev/pci/if_em_hw.h>

/* Tunables */
#define MAX_TXD                         256
#define MAX_RXD                         256
#define TX_CLEANUP_THRESHOLD            MAX_TXD / 8
#define TIDV                            128     
#define RIDV                            28      
#define DO_AUTO_NEG                     1       
#define WAIT_FOR_AUTO_NEG_DEFAULT       1       
#define AUTONEG_ADV_DEFAULT             (ADVERTISE_10_HALF |	\
					 ADVERTISE_10_FULL |	\
                                         ADVERTISE_100_HALF |	\
					 ADVERTISE_100_FULL |	\
                                         ADVERTISE_1000_FULL)
#define EM_REPORT_TX_EARLY              2
#define EM_CHECKSUM_FEATURES            (CSUM_TCP | CSUM_UDP)
#define EM_MAX_INTR                     3
#define EM_TX_TIMEOUT                   5    /* set to 5 seconds */


#define EM_VENDOR_ID                    0x8086
#define EM_MMBA                         0x0010 /* Mem base address */
#define EM_ROUNDUP(size, unit) (((size) + (unit) - 1) & ~((unit) - 1))
#define EM_JUMBO_PBA                    0x00000028
#define EM_DEFAULT_PBA                  0x00000030

#define IOCTL_CMD_TYPE                  u_long
#define MAX_NUM_MULTICAST_ADDRESSES     128
#define PCI_ANY_ID                      (~0U)
#define ETHER_ALIGN                     2
#define QTAG_TYPE                       0x8100

/* Defines for printing debug information */
#define DEBUG_INIT  0
#define DEBUG_IOCTL 0
#define DEBUG_HW    0

#define INIT_DEBUGOUT(S)            if (DEBUG_INIT)  printf(S "\n")
#define INIT_DEBUGOUT1(S, A)        if (DEBUG_INIT)  printf(S "\n", A)
#define INIT_DEBUGOUT2(S, A, B)     if (DEBUG_INIT)  printf(S "\n", A, B)
#define IOCTL_DEBUGOUT(S)           if (DEBUG_IOCTL) printf(S "\n")
#define IOCTL_DEBUGOUT1(S, A)       if (DEBUG_IOCTL) printf(S "\n", A)
#define IOCTL_DEBUGOUT2(S, A, B)    if (DEBUG_IOCTL) printf(S "\n", A, B)
#define HW_DEBUGOUT(S)              if (DEBUG_HW) printf(S "\n")
#define HW_DEBUGOUT1(S, A)          if (DEBUG_HW) printf(S "\n", A)
#define HW_DEBUGOUT2(S, A, B)       if (DEBUG_HW) printf(S "\n", A, B)


/* Supported RX Buffer Sizes */
#define EM_RXBUFFER_2048        2048
#define EM_RXBUFFER_4096        4096
#define EM_RXBUFFER_8192        8192
#define EM_RXBUFFER_16384      16384

struct em_tx_buffer {
	SIMPLEQ_ENTRY(em_tx_buffer) em_tx_entry;
	struct mbuf    *m_head;
	bus_dmamap_t	dmamap;
	u_int32_t       num_tx_desc_used;
};


/* ******************************************************************************
 * This structure stores information about the 2k aligned receive buffer
 * into which the E1000 DMA's frames. 
 * ******************************************************************************/
struct em_rx_buffer {
	SIMPLEQ_ENTRY(em_rx_buffer) em_rx_entry;
	struct mbuf    *m_head;
	bus_dmamap_t	dmamap;
	u_int64_t	buffer_addr;
};

typedef enum _XSUM_CONTEXT_T {
	OFFLOAD_NONE,
	OFFLOAD_TCP_IP,
	OFFLOAD_UDP_IP
} XSUM_CONTEXT_T;

/* Our adapter structure */
struct em_softc {
	struct device	sc_dv;
	struct arpcom	arpcom;         /* interface info */
	struct em_hw    hw;

	/* FreeBSD operating-system-specific structures */
	struct em_osdep osdep;

	int             io_rid;
	void                    *sc_intrhand;
	struct ifmedia  media;
	struct timeout	em_timeout;

	/* Info about the board itself */
	u_int32_t       part_num;
	u_int8_t        link_active;
	u_int16_t       link_speed;
	u_int16_t       link_duplex;
	u_int32_t       tx_int_delay;
	u_int32_t       rx_int_delay;

	XSUM_CONTEXT_T  active_checksum_context;

	/* Transmit definitions */
	struct em_tx_desc *first_tx_desc;
	struct em_tx_desc *last_tx_desc;
	struct em_tx_desc *next_avail_tx_desc;
	struct em_tx_desc *oldest_used_tx_desc;
	struct em_tx_desc *tx_desc_base;
	volatile u_int16_t num_tx_desc_avail;
	u_int16_t       num_tx_desc;
	u_int32_t       txd_cmd;
	struct em_tx_buffer   *tx_buffer_area;
	SIMPLEQ_HEAD(__em_tx_buffer_free, em_tx_buffer)  free_tx_buffer_list;
	SIMPLEQ_HEAD(__em_tx_buffer_used, em_tx_buffer)  used_tx_buffer_list;

	/* Receive definitions */
	struct em_rx_desc *first_rx_desc;
	struct em_rx_desc *last_rx_desc;
	struct em_rx_desc *next_rx_desc_to_check;
	struct em_rx_desc *rx_desc_base;
	u_int16_t       num_rx_desc;
	u_int32_t       rx_buffer_len;
	struct em_rx_buffer   *rx_buffer_area;
	SIMPLEQ_HEAD(__em_rx_buffer, em_rx_buffer)  rx_buffer_list;

	/* Jumbo frame */
	struct mbuf     *fmp;
	struct mbuf     *lmp;

	/* Misc stats maintained by the driver */
	unsigned long   dropped_pkts;
	unsigned long   mbuf_alloc_failed;
	unsigned long   mbuf_cluster_failed;
	unsigned long   xmit_pullup;
	unsigned long   no_tx_desc_avail;
	unsigned long   no_tx_buffer_avail1;
	unsigned long   no_tx_buffer_avail2;
#ifdef DBG_STATS
	unsigned long   no_pkts_avail;
	unsigned long   clean_tx_interrupts;

#endif

	struct em_hw_stats stats;
};

#endif                                                  /* _EM_H_DEFINED_ */
