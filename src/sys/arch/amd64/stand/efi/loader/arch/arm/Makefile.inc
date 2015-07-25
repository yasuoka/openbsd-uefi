# $FreeBSD: head/sys/boot/efi/loader/arch/arm/Makefile.inc 281435 2015-04-11 11:00:53Z andrew $

SRCS+=	exec.c \
	start.S

LOADER_FDT_SUPPORT=yes
