#	$OpenBSD$

.include <bsd.own.mk>

PROG=		BOOTX86.EFI
NOMAN=		#
OBJFMT=		efi-app-ia32
SOFTRAID=	yes
#SRCS+=		run_i386.S
SRCS+=		random_i386.S
LDSCRIPT=	ldscript.i386

COPTS=		-I${EFIDIR}/include/i386

.include "${.CURDIR}/../Makefile.common"

CFLAGS+=	-m32
AFLAGS+=	-m32
