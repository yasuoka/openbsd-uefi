# $FreeBSD: head/sys/boot/efi/Makefile.inc 281027 2015-04-03 15:25:59Z andrew $

BINDIR?=	/boot

.if ${MACHINE_CPUARCH} == "i386"
CFLAGS+=        -march=i386
.endif

# Options used when building app-specific efi components
# See conf/kern.mk for the correct set of these
CFLAGS+=	-ffreestanding -Wformat -msoft-float
LDFLAGS+=	-nostdlib

.if ${MACHINE_CPUARCH} == "amd64"
CFLAGS+=	-fshort-wchar
CFLAGS+=	-mno-red-zone
CFLAGS+=	-mno-mmx -mno-sse -mno-aes -mno-avx
.endif


.include "../Makefile.inc"
