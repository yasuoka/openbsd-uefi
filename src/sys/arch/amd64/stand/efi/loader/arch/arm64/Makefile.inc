# $FreeBSD: head/sys/boot/efi/loader/arch/arm64/Makefile.inc 282474 2015-05-05 11:00:50Z andrew $

LOADER_FDT_SUPPORT=yes
SRCS+=	exec.c \
	start.S

.PATH:	${.CURDIR}/../../arm64/libarm64
CFLAGS+=-I${.CURDIR}/../../arm64/libarm64
SRCS+=	cache.c

CFLAGS+=	-msoft-float -mgeneral-regs-only
