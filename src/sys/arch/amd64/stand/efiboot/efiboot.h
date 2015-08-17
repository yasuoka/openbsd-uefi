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

void	 efi_cleanup(void);
void	 efi_cons_probe (struct consdev *);
void	 efi_memprobe (void);
void	 efi_cons_init (struct consdev *);
int	 efi_cons_getc (dev_t);
void	 efi_cons_putc (dev_t, int);
int	 efi_cons_getshifts (dev_t);
int	 efip_strategy (void *, int, daddr32_t, size_t, void *, size_t *);
int	 efip_open (struct open_file *, ...);
int	 efip_close (struct open_file *);
int	 efip_ioctl (struct open_file *, u_long, void *);
void	 efip_probe (void);
int	 Xvideo_efi(void);
int	 Xexit_efi(void);
int	 Xdisk_efi(void);
void	 efi_makebootargs(void);

extern void (*run_i386)(u_long, u_long, int, int, int, int, int, int, int, int)
    __attribute__ ((noreturn));
