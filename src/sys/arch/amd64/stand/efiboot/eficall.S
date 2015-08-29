/*	$OpenBSD$	*/

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
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

#include <machine/asm.h>

/*
 * Rearrange the arguments to call the given function by EFI ABI.
 *
 * efi_call(nargs, func, arg[0], arg[1], arg[2], arg[3], arg[4], ...)
 * ----------------------------------------------------------------------
 * BSD:     RDI    RSI   RDX     RCX     R8      R9      stack
 * EFI:     -      -     RCX     RDX     R8      R9      stack (w/shadow)
 */
ENTRY(efi_call)
	push	%rbp
	push	%rbx
	mov	%rsp, %rbp

	xchg	%rcx, %rdx
	mov	%rcx, %rax
	mov	%rdi, %rcx

	mov	%rdi, %rbx
	add	$2, %rbx	/* nargs - 2 + 4 */
	shl	$3, %rbx
	sub	%rbx, %rsp

	sub	$3, %rcx
	cmp	$1, %rcx
	jle	2f

1:	/* copy the arg[n-1] .. arg[4] */
	mov	0x10(%rbp, %rcx, 8), %rdi
	mov	%rdi, 0x18(%rsp, %rcx, 8)
	loopnz	1b
2:
	mov	%rax, %rcx

	call	*%rsi

	add	%rbx, %rsp
	pop	%rbx
	pop	%rbp
	retq
