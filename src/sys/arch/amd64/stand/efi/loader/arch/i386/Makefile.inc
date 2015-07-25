# $FreeBSD: head/sys/boot/efi/loader/arch/i386/Makefile.inc 281434 2015-04-11 10:36:48Z andrew $

SRCS+=	start.S \
	efimd.c \
	elf32_freebsd.c \
	exec.c

.PATH:	${.CURDIR}/../../i386/libi386
SRCS+=	nullconsole.c \
	comconsole.c

CFLAGS+=	-fPIC
LDFLAGS+=	-Wl,-znocombreloc
