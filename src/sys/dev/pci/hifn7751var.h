/*	$OpenBSD: hifn7751var.h,v 1.8 2000/03/29 21:03:11 jason Exp $	*/

/*
 *  Invertex AEON / Hi/fn 7751 driver
 *  Copyright (c) 1999 Invertex Inc. All rights reserved.
 *
 *  Please send any comments, feedback, bug-fixes, or feature requests to
 *  software@invertex.com.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
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
 *
 */

#ifndef __HIFN_EXPORT_H__
#define __HIFN_EXPORT_H__

/*
 *  Length values for cryptography
 */
#define HIFN_DES_KEY_LENGTH		8
#define HIFN_3DES_KEY_LENGTH		24
#define HIFN_MAX_CRYPT_KEY_LENGTH	HIFN_3DES_KEY_LENGTH
#define HIFN_IV_LENGTH			8

/*
 *  Length values for authentication
 */
#define HIFN_MAC_KEY_LENGTH		64
#define HIFN_MD5_LENGTH			16
#define HIFN_SHA1_LENGTH		20
#define HIFN_MAC_TRUNC_LENGTH		12

#define MAX_SCATTER 10

/*
 *  hifn_command_t
 *
 *  This is the control structure used to pass commands to hifn_encrypt().
 *
 *  flags
 *  -----
 *  Flags is the bitwise "or" values for command configuration.  A single
 *  encrypt direction needs to be set:
 *
 *	HIFN_ENCODE or HIFN_DECODE
 *
 *  To use cryptography, a single crypto algorithm must be included:
 *
 *	HIFN_CRYPT_3DES or HIFN_CRYPT_DES
 *
 *  To use authentication is used, a single MAC algorithm must be included:
 *
 *	HIFN_MAC_MD5 or HIFN_MAC_SHA1
 *
 *  By default MD5 uses a 16 byte hash and SHA-1 uses a 20 byte hash.
 *  If the value below is set, hash values are truncated or assumed
 *  truncated to 12 bytes:
 *
 *	HIFN_MAC_TRUNC
 *
 *  Keys for encryption and authentication can be sent as part of a command,
 *  or the last key value used with a particular session can be retrieved
 *  and used again if either of these flags are not specified.
 *
 *	HIFN_CRYPT_NEW_KEY, HIFN_MAC_NEW_KEY
 *
 *  Whether we block or not waiting for the dest data to be ready is
 *  determined by whether a callback function is given.  The other
 *  place we could block is when all the DMA rings are full.  If 
 *  it is not okay to block while waiting for an open slot in the
 *  rings, include in the following value:
 *
 *	HIFN_DMA_FULL_NOBLOCK
 *
 *  result_flags
 *  ------------
 *  result_flags is a bitwise "or" of result values.  The result_flags
 *  values should not be considered valid until:
 *
 *	callback routine NULL:  hifn_crypto() returns
 *	callback routine set:   callback routine called
 *
 *  Right now there is only one result flag:  HIFN_MAC_BAD
 *  It's bit is set on decode operations using authentication when a
 *  hash result does not match the input hash value.
 *  The HIFN_MAC_OK(r) macro can be used to help inspect this flag.
 *
 *  session_num
 *  -----------
 *  A number between 0 and 2048 (for DRAM models) or a number between 
 *  0 and 768 (for SRAM models).  Those who don't want to use session
 *  numbers should leave value at zero and send a new crypt key and/or
 *  new MAC key on every command.  If you use session numbers and
 *  don't send a key with a command, the last key sent for that same
 *  session number will be used.
 *
 *  Warning:  Using session numbers and multiboard at the same time
 *            is currently broken.
 *
 *  mbuf
 *  ----
 *  Either fill in the mbuf pointer and npa=0 or
 *	 fill packp[] and packl[] and set npa to > 0
 * 
 *  mac_header_skip
 *  ---------------
 *  The number of bytes of the source_buf that are skipped over before
 *  authentication begins.  This must be a number between 0 and 2^16-1
 *  and can be used by IPSec implementers to skip over IP headers.
 *  *** Value ignored if authentication not used ***
 *
 *  crypt_header_skip
 *  -----------------
 *  The number of bytes of the source_buf that are skipped over before
 *  the cryptographic operation begins.  This must be a number between 0
 *  and 2^16-1.  For IPSec, this number will always be 8 bytes larger
 *  than the auth_header_skip (to skip over the ESP header).
 *  *** Value ignored if cryptography not used ***
 *
 *  source_length
 *  -------------
 *  Length of input data including all skipped headers.  On decode
 *  operations using authentication, the length must also include the
 *  the appended MAC hash (12, 16, or 20 bytes depending on algorithm
 *  and truncation settings).
 *
 *  If encryption is used, the encryption payload must be a non-zero
 *  multiple of 8.  On encode operations, the encryption payload size
 *  is (source_length - crypt_header_skip - (MAC hash size)).  On
 *  decode operations, the encryption payload is
 *  (source_length - crypt_header_skip).
 *
 *  dest_length
 *  -----------
 *  Length of the dest buffer.  It must be at least as large as the
 *  source buffer when authentication is not used.  When authentication
 *  is used on an encode operation, it must be at least as long as the
 *  source length plus an extra 12, 16, or 20 bytes to hold the MAC
 *  value (length of mac value varies with algorithm used).  When
 *  authentication is used on decode operations, it must be at least
 *  as long as the source buffer minus 12, 16, or 20 bytes for the MAC
 *  value which is not included in the dest data.  Unlike source_length,
 *  the dest_length does not have to be exact, values larger than required
 *  are fine.
 *
 *  dest_ready_callback
 *  -------------------
 *  Callback routine called from HIFN's interrupt handler.  The routine
 *  must be quick and non-blocking.  The callback routine is passed a
 *  pointer to the same hifn_command_t structure used to initiate the
 *  command.
 *
 *  If this value is null, the hifn_crypto() routine will block until the
 *  dest data is ready.
 *
 *  private_data
 *  ------------
 *  An unsigned long quantity (i.e. large enough to hold a pointer), that
 *  can be used by the callback routine if desired.
 */
typedef struct hifn_command {
	u_int	flags;
	volatile u_int result_flags;

	u_short	session_num;

	u_char	iv[HIFN_IV_LENGTH], *ck, *mac;
	int	ck_len, mac_len;

	struct mbuf *src_m;
	long	src_packp[MAX_SCATTER];
	int	src_packl[MAX_SCATTER];
	int	src_npa;
	int	src_l;

	struct mbuf *dst_m;
	long	dst_packp[MAX_SCATTER];
	int	dst_packl[MAX_SCATTER];
	int	dst_npa;
	int	dst_l;

	u_short mac_header_skip, mac_process_len;
	u_short crypt_header_skip, crypt_process_len;

	void (*dest_ready_callback)(struct hifn_command *);
	u_long private_data;
} hifn_command_t;

/*
 *  Return values for hifn_crypto()
 */
#define HIFN_CRYPTO_SUCCESS	0
#define HIFN_CRYPTO_BAD_INPUT	(-1)
#define HIFN_CRYPTO_RINGS_FULL	(-2)


/*
 *  Defines for the "config" parameter of hifn_command_t
 */
#define HIFN_ENCODE		0x0001
#define HIFN_DECODE		0x0002
#define HIFN_CRYPT_3DES		0x0004
#define HIFN_CRYPT_DES		0x0008
#define HIFN_MAC_MD5		0x0010
#define HIFN_MAC_SHA1		0x0020
#define HIFN_MAC_TRUNC		0x0040
#define HIFN_CRYPT_NEW_KEY	0x0080
#define HIFN_MAC_NEW_KEY	0x0100
#define HIFN_DMA_FULL_NOBLOCK	0x0200

#define HIFN_USING_CRYPT(f)	((f) & (HIFN_CRYPT_3DES|HIFN_CRYPT_DES))
#define HIFN_USING_MAC(f)	((f) & (HIFN_MAC_MD5|HIFN_MAC_SHA1))

/*
 *  Defines for the "result_flags" parameter of hifn_command_t.
 */
#define HIFN_MAC_BAD		1
#define HIFN_MAC_OK(r)		(!((r) & HIFN_MAC_BAD))

#ifdef _KERNEL

/**************************************************************************
 *
 *  Function:  hifn_crypto
 *
 *  Purpose:   Called by external drivers to begin an encryption on the
 *             HIFN board.
 *
 *  Blocking/Non-blocking Issues
 *  ============================
 *  If the dest_ready_callback field of the hifn_command structure
 *  is NULL, hifn_encrypt will block until the dest_data is ready --
 *  otherwise hifn_encrypt() will return immediately and the 
 *  dest_ready_callback routine will be called when the dest data is
 *  ready.
 *
 *  The routine can also block when waiting for an open slot when all
 *  DMA rings are full.  You can avoid this behaviour by sending the
 *  HIFN_DMA_FULL_NOBLOCK as part of the command flags.  This will
 *  make hifn_crypt() return immediately when the rings are full.
 *
 *  Return Values
 *  =============
 *  0 for success, negative values on error
 *
 *  Defines for negative error codes are:
 *  
 *    HIFN_CRYPTO_BAD_INPUT  :  The passed in command had invalid settings.
 *    HIFN_CRYPTO_RINGS_FULL :  All DMA rings were full and non-blocking
 *                              behaviour was requested.
 *
 *************************************************************************/

/*
 * Convert back and forth from 'sid' to 'card' and 'session'
 */
#define HIFN_CARD(sid)		(((sid) & 0xf0000000) >> 28)
#define HIFN_SESSION(sid)	((sid) & 0x000007ff)
#define HIFN_SID(crd,ses)	(((crd) << 28) | ((ses) & 0x7ff))

#endif /* _KERNEL */

#endif /* __HIFN_EXPORT_H__ */
