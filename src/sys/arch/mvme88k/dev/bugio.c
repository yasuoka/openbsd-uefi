/*	$OpenBSD: bugio.c,v 1.13 2004/04/12 13:14:54 miod Exp $ */
/*  Copyright (c) 1998 Steve Murphree, Jr. */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/asm_macro.h>
#include <machine/bugio.h>
#include <machine/prom.h>

register_t ossr0, ossr1, ossr2, ossr3;
register_t bugsr3;

unsigned long bugvec[2], sysbugvec[2];

void bug_vector(void);
void sysbug_vector(void);

#define MVMEPROM_CALL(x)						\
	__asm__ __volatile__ ("or r9,r0," __STRING(x));			\
	__asm__ __volatile__ ("tb0 0,r0,496")

void
bug_vector()
{
	unsigned long *vbr, psr;

	psr = disable_interrupts_return_psr();	/* paranoia */

	__asm__ __volatile__ ("ldcr %0, cr7" : "=r" (vbr));
	vbr[2 * MVMEPROM_VECTOR + 0] = bugvec[0];
	vbr[2 * MVMEPROM_VECTOR + 1] = bugvec[1];

	set_psr(psr);
}

void
sysbug_vector()
{
	unsigned long *vbr, psr;

	psr = disable_interrupts_return_psr();	/* paranoia */

	__asm__ __volatile__ ("ldcr %0, cr7" : "=r" (vbr));
	vbr[2 * MVMEPROM_VECTOR + 0] = sysbugvec[0];
	vbr[2 * MVMEPROM_VECTOR + 1] = sysbugvec[1];

	set_psr(psr);
}

#define	BUGCTXT()							\
{									\
	bug_vector();							\
	__asm__ __volatile__ ("ldcr %0, cr17" : "=r" (ossr0));		\
	__asm__ __volatile__ ("ldcr %0, cr18" : "=r" (ossr1));		\
	__asm__ __volatile__ ("ldcr %0, cr19" : "=r" (ossr2));		\
	__asm__ __volatile__ ("ldcr %0, cr20" : "=r" (ossr3));		\
									\
	__asm__ __volatile__ ("stcr %0, cr20" :: "r"(bugsr3));		\
}

#define	OSCTXT()							\
{									\
	__asm__ __volatile__ ("ldcr %0, cr20" : "=r" (bugsr3)::		\
	    "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8",		\
	    "r9", "r10", "r11", "r12", "r13");				\
									\
	__asm__ __volatile__ ("stcr %0, cr17" :: "r"(ossr0));		\
	__asm__ __volatile__ ("stcr %0, cr18" :: "r"(ossr1));		\
	__asm__ __volatile__ ("stcr %0, cr19" :: "r"(ossr2));		\
	__asm__ __volatile__ ("stcr %0, cr20" :: "r"(ossr3));		\
	sysbug_vector();						\
}

static void
bugpcrlf(void)
{
	BUGCTXT();
	MVMEPROM_CALL(MVMEPROM_OUTCRLF);
	OSCTXT();
}

void
buginit()
{
	__asm__ __volatile__ ("ldcr %0, cr20" : "=r" (bugsr3));
}

char
buginchr(void)
{
	int ret;
	BUGCTXT();
	MVMEPROM_CALL(MVMEPROM_INCHR);
	__asm__ __volatile__ ("or %0,r0,r2" : "=r" (ret) : );
	OSCTXT();
	return ((char)ret & 0xFF);
}

void
bugoutchr(unsigned char c)
{
	unsigned char cc;

	if ((cc = c) == '\n') {
		bugpcrlf();
		return;
	}

	BUGCTXT();
	__asm__ __volatile__ ("or r2,r0,%0" : : "r" (cc));
	MVMEPROM_CALL(MVMEPROM_OUTCHR);
	OSCTXT();
}

/* return 1 if not empty else 0 */
int
buginstat(void)
{
	register int ret;

	BUGCTXT();
	MVMEPROM_CALL(MVMEPROM_INSTAT);
	__asm__ __volatile__  ("or %0,r0,r2" : "=r" (ret) : );
	OSCTXT();
	return (ret & 0x4 ? 0 : 1);
}

void
bugoutstr(char *s, char *se)
{
	BUGCTXT();
	MVMEPROM_CALL(MVMEPROM_OUTSTR);
	OSCTXT();
}

void
bugrtcrd(struct mvmeprom_time *rtc)
{
	BUGCTXT();
	MVMEPROM_CALL(MVMEPROM_RTC_RD);
	OSCTXT();
}

void
bugreturn(void)
{
	BUGCTXT();
	MVMEPROM_CALL(MVMEPROM_EXIT);
	OSCTXT();
}

void
bugbrdid(struct mvmeprom_brdid *id)
{
	struct mvmeprom_brdid *ptr;

	BUGCTXT();
	MVMEPROM_CALL(MVMEPROM_GETBRDID);
	__asm__ __volatile__ ("or %0,r0,r2" : "=r" (ptr) : );
	OSCTXT();

	bcopy(ptr, id, sizeof(struct mvmeprom_brdid));
}

void
bugdiskrd(struct mvmeprom_dskio *dio)
{
	BUGCTXT();
	__asm__ __volatile__ ("or r2, r0, %0" : : "r" (dio));
	MVMEPROM_CALL(MVMEPROM_DSKRD);
	OSCTXT();
}
