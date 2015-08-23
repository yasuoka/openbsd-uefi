#	$OpenBSD$

.include <bsd.own.mk>

PROG=		BOOTX64.EFI
NOMAN=		#
OBJFMT=		efi-app-x86_64
SOFTRAID=	yes
COPTS+=		-I${EFIDIR}/include/amd64
SRCS+=		run_i386.S

.include "${.CURDIR}/../Makefile.common"

