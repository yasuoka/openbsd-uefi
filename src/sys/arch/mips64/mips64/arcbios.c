/*	$OpenBSD: arcbios.c,v 1.20 2009/05/15 22:56:08 miod Exp $	*/
/*-
 * Copyright (c) 1996 M. Warner Losh.  All rights reserved.
 * Copyright (c) 1996-2004 Opsycon AB.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/memconf.h>
#include <machine/vmparam.h>

#include <mips64/arcbios.h>
#include <mips64/archtype.h>

#if defined(TGT_ORIGIN200) || defined(TGT_ORIGIN2000)
#include <machine/mnode.h>
#endif

int bios_is_32bit;
/*
 * If we cannot get the onboard Ethernet address to override this bogus
 * value, ether_ifattach() will pick a valid address.
 */
char bios_enaddr[20] = "ff:ff:ff:ff:ff:ff";

char bios_console[10];			/* Primary console. */

extern int	physmem;		/* Total physical memory size */
extern int	rsvdmem;		/* Total reserved memory size */

void bios_configure_memory(void);
int bios_get_system_type(void);

arc_dsp_stat_t	displayinfo;		/* Save area for display status info. */

static struct systypes {
	char *sys_vend;		/* Vendor ID if name is ambiguous */
	char *sys_name;		/* May be left NULL if name is sufficient */
	int  sys_type;
} sys_types[] = {
    { NULL,		"PICA-61",			ACER_PICA_61 },
    { NULL,		"NEC-R94",			ACER_PICA_61 },
    { NULL,		"DESKTECH-TYNE",		DESKSTATION_TYNE },
    { NULL,		"DESKTECH-ARCStation I",	DESKSTATION_RPC44 },
    { NULL,		"Microsoft-Jazz",		MAGNUM },
    { NULL,		"RM200PCI",			SNI_RM200 },
    { NULL,		"SGI-IP17",			SGI_CRIMSON },
    { NULL,		"SGI-IP19",			SGI_ONYX },
    { NULL,		"SGI-IP20",			SGI_INDIGO },
    { NULL,		"SGI-IP21",			SGI_POWER },
    { NULL,		"SGI-IP22",			SGI_INDY },
    { NULL,		"SGI-IP25",			SGI_POWER10 },
    { NULL,		"SGI-IP26",			SGI_POWERI },
    { NULL,		"SGI-IP30",			SGI_OCTANE },
    { NULL,		"SGI-IP32",			SGI_O2 }
};

#define KNOWNSYSTEMS (sizeof(sys_types) / sizeof(struct systypes))

/*
 *	ARC Bios trampoline code.
 */
#define ARC_Call(Name,Offset)	\
__asm__("\n"			\
"	.text\n"		\
"	.ent	" #Name "\n"	\
"	.align	3\n"		\
"	.set	noreorder\n"	\
"	.globl	" #Name "\n"	\
#Name":\n"			\
"	lw	$2, bios_is_32bit\n"\
"	beqz	$2, 1f\n"	\
"	nop\n"			\
"       lw      $2, 0xffffffff80001020\n"\
"       lw      $2," #Offset "($2)\n"\
"	jr	$2\n"		\
"	nop\n"			\
"1:\n"				\
"       ld      $2, 0xffffffff80001040\n"\
"	ld	$2, 2*" #Offset "($2)\n"\
"	jr	$2\n"		\
"	nop\n"			\
"	.end	" #Name "\n"	);

ARC_Call(Bios_Load,			0x00);
ARC_Call(Bios_Invoke,			0x04);
ARC_Call(Bios_Execute,			0x08);
ARC_Call(Bios_Halt,			0x0c);
ARC_Call(Bios_PowerDown,		0x10);
ARC_Call(Bios_Restart,			0x14);
ARC_Call(Bios_Reboot,			0x18);
ARC_Call(Bios_EnterInteractiveMode,	0x1c);
ARC_Call(Bios_Unused1,			0x20);
ARC_Call(Bios_GetPeer,			0x24);
ARC_Call(Bios_GetChild,			0x28);
ARC_Call(Bios_GetParent,		0x2c);
ARC_Call(Bios_GetConfigurationData,	0x30);
ARC_Call(Bios_AddChild,			0x34);
ARC_Call(Bios_DeleteComponent,		0x38);
ARC_Call(Bios_GetComponent,		0x3c);
ARC_Call(Bios_SaveConfiguration,	0x40);
ARC_Call(Bios_GetSystemId,		0x44);
ARC_Call(Bios_GetMemoryDescriptor,	0x48);
ARC_Call(Bios_Unused2,			0x4c);
ARC_Call(Bios_GetTime,			0x50);
ARC_Call(Bios_GetRelativeTime,		0x54);
ARC_Call(Bios_GetDirectoryEntry,	0x58);
ARC_Call(Bios_Open,			0x5c);
ARC_Call(Bios_Close,			0x60);
ARC_Call(Bios_Read,			0x64);
ARC_Call(Bios_GetReadStatus,		0x68);
ARC_Call(Bios_Write,			0x6c);
ARC_Call(Bios_Seek,			0x70);
ARC_Call(Bios_Mount,			0x74);
ARC_Call(Bios_GetEnvironmentVariable,	0x78);
ARC_Call(Bios_SetEnvironmentVariable,	0x7c);
ARC_Call(Bios_GetFileInformation,	0x80);
ARC_Call(Bios_SetFileInformation,	0x84);
ARC_Call(Bios_FlushAllCaches,		0x88);
ARC_Call(Bios_TestUnicodeCharacter,	0x8c);
ARC_Call(Bios_GetDisplayStatus,		0x90);

/*
 *	Simple getchar/putchar interface.
 */

int
bios_getchar()
{
	char buf[4];
	long  cnt;

	if (Bios_Read(0, &buf[0], 1, &cnt) != 0)
		return(-1);
	return(buf[0] & 255);
}

void
bios_putchar(c)
char c;
{
	char buf[4];
	long  cnt;

	if (c == '\n') {
		buf[0] = '\r';
		buf[1] = c;
		cnt = 2;
#ifdef __arc__
		if (displayinfo.CursorYPosition < displayinfo.CursorMaxYPosition)
			displayinfo.CursorYPosition++;
#endif
	}
	else {
		buf[0] = c;
		cnt = 1;
	}
	Bios_Write(1, &buf[0], cnt, &cnt);
}

void
bios_putstring(s)
char *s;
{
	while (*s) {
		bios_putchar(*s++);
	}
}

void
bios_printf(const char *fmt, ...)
{
	va_list ap;
	char buf[1024];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	bios_putstring(buf);
}

/*
 * Get memory descriptor for the memory configuration and
 * create a layout database used by pmap init to set up
 * the memory system.
 *
 * Concatenate obvious adjacent segments.
 */
void
bios_configure_memory()
{
	arc_mem_t *descr = NULL;
	struct phys_mem_desc *m;
	uint64_t start, count;
	MEMORYTYPE type;
	vaddr_t seg_start, seg_end;
#if defined(TGT_ORIGIN200) || defined(TGT_ORIGIN2000)
	int seen_free = 0;
#endif
	int i;

	descr = (arc_mem_t *)Bios_GetMemoryDescriptor(descr);
	while (descr != NULL) {
		if (bios_is_32bit) {
			start = descr->BasePage;
			count = descr->PageCount;
			type = descr->Type;
		} else {
			start = ((arc_mem64_t *)descr)->BasePage;
			count = ((arc_mem64_t *)descr)->PageCount;
			type = descr->Type;

#ifdef TGT_OCTANE
			/*
			 * Memory above 1GB physical (address 1.5GB)
			 * gets reported as reserved on Octane, while
			 * it isn't.
			 */
			if (sys_config.system_type == SGI_OCTANE &&
			    type == FirmwarePermanent &&
			    start >= 0x60000)
				type = FreeMemory;
#endif

#if defined(TGT_ORIGIN200) || defined(TGT_ORIGIN2000)
			if ((sys_config.system_type == SGI_O200 ||
			     sys_config.system_type == SGI_O300)) {

				/*
				 * For the lack of a better way to tell
				 * IP27 apart from IP35, look at the
				 * start of the first chunk of free
				 * memory. On IP27, it starts under
				 * 0x20000 (which allows us to link
				 * kernels at 0xa800000000020000).
				 * On IP35, it starts at 0x40000.
				 */
				if (type == FreeMemory && seen_free == 0) {
					seen_free = 1;
					if (start >= 0x20)	/* IP35 */
						sys_config.system_type =
						    SGI_O300;
				}

				/*
				 * On IP27 and IP35 systems, data after the
				 * first FirmwarePermanent entry is not
				 * reliable (entries conflict with each other),
				 * and memory after 32MB (or 64MB on IP35) is
				 * not listed anyway.
				 * So, break from the loop as soon as a
				 * FirmwarePermanent entry is found, after
				 * making it span the end of the first 32MB
				 * (64MB on IP35).
				 *
				 * The rest of the memory will be gathered
				 * from the node structures.  This loses some
				 * of the first few MB (well... all of them
				 * but the kernel image), but at least we're
				 * safe to use ARCBios after going virtual.
				 */
				if (type == FirmwarePermanent) {
					descr = NULL; /* abort loop */
					count = ((sys_config.system_type ==
					    SGI_O200 ?  32 : 64) << (20 - 12)) -
					    start;
				}
			}
#endif	/* O200 || O300 */
		}

		seg_start = start;
		seg_end = seg_start + count;

		switch (type) {
		case BadMemory:		/* Have no use for these */
			break;

		case FreeMemory:
		case FreeContigous:
			physmem += count;
			m = NULL;
			for (i = 0; i < MAXMEMSEGS; i++) {
				if (mem_layout[i].mem_last_page == 0) {
					if (m == NULL)
						m = &mem_layout[i]; /* free */
				}
				else if (seg_end == mem_layout[i].mem_first_page) {
					m = &mem_layout[i];
					m->mem_first_page = seg_start;
				}
				else if (mem_layout[i].mem_last_page == seg_start) {
					m = &mem_layout[i];
					m->mem_last_page = seg_end;
				}
			}
			if (m && m->mem_first_page == 0) {
				m->mem_first_page = seg_start;
				m->mem_last_page = seg_end;
				m->mem_freelist = VM_FREELIST_DEFAULT;
			}
			break;

		case ExceptionBlock:
		case SystemParameterBlock:
		case FirmwareTemporary:
		case FirmwarePermanent:
			rsvdmem += count;
			physmem += count;
			break;

		case LoadedProgram:	/* Count this into total memory */
			physmem += count;
			break;

		default:		/* Unknown type, leave it alone... */
			break;
		}
#if defined(TGT_ORIGIN200) || defined(TGT_ORIGIN2000)
		if (descr == NULL)
			break;
#endif
		descr = (arc_mem_t *)Bios_GetMemoryDescriptor(descr);
	}

#ifdef DEBUG_MEM_LAYOUT
	for (i = 0; i < MAXMEMSEGS; i++) {
		if (mem_layout[i].mem_first_page) {
			bios_printf("MEM %d, 0x%x to  0x%x\n",i,
				mem_layout[i].mem_first_page * 4096,
				mem_layout[i].mem_last_page * 4096);
	    }
	}
#endif
}

/*
 * Find out system type.
 */
int
bios_get_system_type()
{
	arc_config_t	*cf;
	arc_sid_t	*sid;
	char		*sysid;
	int		sysid_len;
	int		i;

	/*
	 * Figure out if this is an ARC Bios machine and if it is, see if we're
	 * dealing with a 32 or 64 bit version.
	 */
	if ((ArcBiosBase32->magic == ARC_PARAM_BLK_MAGIC) ||
	    (ArcBiosBase32->magic == ARC_PARAM_BLK_MAGIC_BUG)) {
		bios_is_32bit = 1;
		bios_printf("ARCS32 Firmware Version %d.%d\n",
		    ArcBiosBase32->version, ArcBiosBase32->revision);
	} else if ((ArcBiosBase64->magic == ARC_PARAM_BLK_MAGIC) ||
	    (ArcBiosBase64->magic == ARC_PARAM_BLK_MAGIC_BUG)) {
		bios_is_32bit = 0;
		bios_printf("ARCS64 Firmware Version %d.%d\n",
		    ArcBiosBase64->version, ArcBiosBase64->revision);
	} else {
		return -1;	/* XXX BAD BAD BAD!!! */
	}

	sid = (arc_sid_t *)Bios_GetSystemId();

	cf = (arc_config_t *)Bios_GetChild(NULL);
	if (cf != NULL) {
		if (bios_is_32bit) {
			sysid = (char *)(long)cf->id;
			sysid_len = cf->id_len;
		} else {
			sysid = (char *)((arc_config64_t *)cf)->id;
			sysid_len = ((arc_config64_t *)cf)->id_len;
		}

		if (sysid_len > 0 && sysid != NULL) {
			sysid_len--;
			for (i = 0; i < KNOWNSYSTEMS; i++) {
				if (strlen(sys_types[i].sys_name) !=sysid_len)
					continue;
				if (strncmp(sys_types[i].sys_name, sysid,
				    sysid_len) != 0)
					continue;
				if (sys_types[i].sys_vend &&
				    strncmp(sys_types[i].sys_vend, sid->vendor,
				      8) != 0)
					continue;
				return (sys_types[i].sys_type);	/* Found it. */
			}
		}
	} else {
#if defined(TGT_ORIGIN200) || defined(TGT_ORIGIN2000)
		if (IP27_KLD_KLCONFIG(0)->magic == IP27_KLDIR_MAGIC) {
			/*
			 * If we find a kldir assume IP27 for now.
			 * We'll decide whether this is IP27 or IP35 later.
			 */
			return SGI_O200;
		}
#endif
	}

	bios_printf("UNRECOGNIZED SYSTEM '%s' VENDOR '%8.8s' PRODUCT '%8.8s'\n",
	    cf == NULL ? "??" : sysid, sid->vendor, sid->prodid);
	bios_printf("Halting system!\n");
	Bios_Halt();
	bios_printf("Halting failed, use manual reset!\n");
	while(1);
}

/*
 * Incomplete version of bios_ident
 */
void
bios_ident()
{
	sys_config.system_type = bios_get_system_type();
	bios_configure_memory();
#ifdef __arc__
	displayinfo = *(arc_dsp_stat_t *)Bios_GetDisplayStatus(1);
#endif
}

/*
 * Return geometry of the display. Used by pccons.c to set up the
 * display configuration.
 */
void
bios_display_info(xpos, ypos, xsize, ysize)
    int	*xpos;
    int	*ypos;
    int *xsize;
    int *ysize;
{
#ifdef __arc__
	*xpos = displayinfo.CursorXPosition;
	*ypos = displayinfo.CursorYPosition;
	*xsize = displayinfo.CursorMaxXPosition;
	*ysize = displayinfo.CursorMaxYPosition;
#endif
}
