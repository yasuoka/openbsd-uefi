/*	$OpenBSD: isapnpreg.h,v 1.2 1997/07/12 23:22:01 weingart Exp $	*/

/*
 * Copyright (c) 1996, Sujal M. Patel
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
 *      This product includes software developed by Sujal M. Patel
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _DEV_ISA_ISAPNPREG_H_
#define _DEV_ISA_ISAPNPREG_H_

/* Maximum Number of PnP Devices.  8 should be plenty */
#define MAX_CARDS 8


/* Static ports */
#define ADDRESS			0x279
#if 0
#define WRITE_DATA		0xa79
#else
#define WRITE_DATA		(ADDRESS+0x0800)
#endif


/* PnP Registers.  Write to ADDRESS and then use WRITE/READ_DATA */
#define SET_RD_DATA			0x00
#define SERIAL_ISOLATION	0x01
#define CONFIG_CONTROL		0x02
#define WAKE				0x03
#define	RESOURCE_DATA		0x04
#define STATUS				0x05
#define SET_CSN				0x06
#define SET_LDN				0x07
#define ACTIVATE			0x30
#define IO_RANGE_CHECK		0x31
#define MEM_CONFIG			0x40
#define IO_CONFIG_BASE		0x60
#define IRQ_CONFIG			0x70
#define DRQ_CONFIG			0x74

/* Small Resource Item names */
#define PNP_VERSION			0x1
#define LOG_DEVICE_ID		0x2
#define COMP_DEVICE_ID		0x3
#define IRQ_FORMAT			0x4
#define DMA_FORMAT			0x5
#define START_DEPEND_FUNC	0x6
#define END_DEPEND_FUNC		0x7
#define IO_PORT_DESC		0x8
#define FIXED_IO_PORT_DESC	0x9
#define SM_RES_RESERVED1	0xa
#define SM_RES_RESERVED2	0xb
#define SM_RES_RESERVED3	0xc
#define SM_RES_RESERVED4	0xd
#define SM_VENDOR_DEFINED	0xe
#define END_TAG				0xf

/* Large Resource Item names */
#define MEMORY_RANGE_DESC		0x1
#define ID_STRING_ANSI			0x2
#define ID_STRING_UNICODE		0x3
#define LG_VENDOR_DEFINED		0x4
#define _32BIT_MEM_RANGE_DESC	0x5
#define _32BIT_FIXED_LOC_DESC	0x6
#define LG_RES_RESERVED			0x7-0x7f

/* Priority for Resource Group */
#define BASIC_CONFIGURATION			-1
#define GOOD_CONFIGURATION			0x0
#define ACCEPTABLE_CONFIGURATION	0x1
#define SUBOPTIMAL_CONFIGURATION	0x2
#define RESERVED_CONFIGURATION		0x3

#endif /* !_DEV_ISA_ISAPNPREG_H_ */
