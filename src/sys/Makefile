#	$OpenBSD: Makefile,v 1.39 2013/03/26 19:20:57 jasper Exp $
#	$NetBSD: Makefile,v 1.5 1995/09/15 21:05:21 pk Exp $

SUBDIR=	dev/microcode \
	arch/alpha arch/amd64 arch/armish arch/aviion arch/beagle \
	arch/hp300 arch/hppa arch/hppa64 arch/i386 arch/landisk \
	arch/loongson arch/luna88k arch/m68k arch/macppc \
	arch/mvme68k arch/mvme88k arch/octeon arch/sgi \
	arch/socppc arch/solbourne arch/sparc arch/sparc64 arch/vax \
	arch/zaurus

tags:
	cd ${.CURDIR}/kern; make tags

.include <bsd.subdir.mk>
