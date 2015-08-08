#	$OpenBSD$

S=	${.CURDIR}/../../../..
SADIR=	${.CURDIR}/..

.include <bsd.own.mk>

PROG=		efiboot.efi
SRCS=		start.S self_reloc.c
SRCS+=		efiboot.c
SRCS+=		conf.c run_i386.S
NOMAN=		#

LDSCRIPT=	ldscript.${MACHINE}
LDFLAGS+=	-nostdlib -T${.CURDIR}/${LDSCRIPT} -Bsymbolic -shared
COPTS+=		-ffreestanding -std=gnu99
COPTS+=		-fshort-wchar -fPIC
COPTS+=		-I${.CURDIR}/../efi/include
COPTS+=		-I${.CURDIR}/../efi/include/amd64
COPTS+=		-DNEEDS_HEAP_H -DLINKADDR=${LINKADDR}
COPTS+=		-DEFIBOOT
AFLAGS+=	-pipe -fPIC

OBJCOPY?=	objcopy
OBJDUMP?=	objdump

${PROG}: efiboot.so
	${OBJCOPY} -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	    -j .rel.* -j .rela -j .rela.* -j .reloc \
	    --target=efi-app-x86_64 efiboot.so ${.TARGET}


CLEANFILES+=	efiboot.so

# XXX
.PATH:	${S}/stand/boot
SRCS+=	boot.c bootarg.c cmd.c vars.c

.PATH:	${S}/lib/libsa
SRCS+=	alloc.c ctime.c exit.c getchar.c memcmp.c memcpy.c memset.c printf.c \
	putchar.c snprintf.c strcmp.c strerror.c strlen.c strncmp.c strncpy.c \
	strtol.c strtoll.c
SRCS+=	close.c closeall.c cons.c cread.c dev.c disklabel.c dkcksum.c fstat.c \
	lseek.c open.c read.c readdir.c stat.c
SRCS+=	ufs.c

.PATH:	${S}/lib/libz
SRCS+=	adler32.c crc32.c inflate.c inftrees.c

.PATH:	${S}/lib/libkern
SRCS+=	strlcpy.c fls.c

.PATH:	${SADIR}/libsa
SRCS+=	loadfile.c elf64.c elf32.c memprobe.c cmd_i386.c exec_i386.c

CPPFLAGS+=	-DSMALL -DSLOW -DNOBYFOUR -D__INTERNAL_LIBSA_CREAD
COPTS+=		-I${S}/stand/boot
COPTS+=		-D_STANDALONE -nostdinc -fno-builtin

.include <bsd.prog.mk>

CFLAGS+=	-Wno-pointer-sign

CLEANFILES= efiboot.so.tmp

efiboot.so: ${OBJS}
	${LD} ${LDFLAGS} -o ${.TARGET}.tmp ${OBJS} ${LDADD}
	@if ${OBJDUMP} -t ${.TARGET}.tmp | grep 'UND'; then	\
		(echo Undefined symbols; false);		\
	fi
	mv ${.TARGET}.tmp ${.TARGET}
