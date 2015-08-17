/*	$OpenBSD$	*/

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <dev/cons.h>
#include <libsa.h>
#include <cmd.h>
#include <stand/boot/bootarg.h>

#include <efi.h>
#include <efiprot.h>
#include <eficonsctl.h>
#include <efi.h>

#include "efiboot.h"
#include "run_i386.h"

EFI_SYSTEM_TABLE	*ST;
EFI_BOOT_SERVICES	*BS;
EFI_RUNTIME_SERVICES	*RS;
EFI_HANDLE		*IH;
EFI_PHYSICAL_ADDRESS	 heap;
UINTN			 heapsiz = 3 * 1024 * 1024;
UINTN			 mmap_key;

static void	 efi_heap_init(void);
static void	 eif_memprobe_internal(void);
static void	 efi_video_init(void);
static void	 efi_video_reset(void);
static void	 hexdump(u_char *, int) __unused;

void (*run_i386)(u_long, u_long, int, int, int, int, int, int, int, int)
    __attribute__ ((noreturn));


EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab)
{
	extern char	*progname;

	ST = systab;
	BS = ST->BootServices;
	RS = ST->RuntimeServices;
	IH = image;

	efi_video_init();
	efi_heap_init();

	/* allocate run_i386_start() on heap */
	if ((run_i386 = alloc(run_i386_size)) == NULL)
		panic("alloc() failed");
	memcpy(run_i386, run_i386_start, run_i386_size);

	/* can't use sa_cleanup since printf is used after sa_cleanup() */
	/* sa_cleanup = efi_cleanup; */

	progname = "EFIBOOT";
	boot(0);

	panic("XXX");
}

void
efi_cleanup(void)
{
	EFI_STATUS	 status;

	eif_memprobe_internal();	/* sync the current map */
	status = BS->ExitBootServices(IH, mmap_key);
	if (status != EFI_SUCCESS)
		panic("ExitBootServices");
}

/***********************************************************************
 * Memory
 ***********************************************************************/
bios_memmap_t		 bios_memmap[64];
u_int			 cnvmem, extmem;
volatile struct BIOS_regs	BIOS_regs;	/* used by memprobe.c */

static void
efi_heap_init(void)
{
	EFI_STATUS	 status;

	heap = 0x1000000;	/* Below kernel base address */
	status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(heapsiz), &heap);
	if (status != EFI_SUCCESS)
		panic("BS->AllocatePages()");
}

void
efi_memprobe(void)
{
	u_int		 n = 0;
	bios_memmap_t	*bm;

	printf(" mem[");
	eif_memprobe_internal();
	for (bm = bios_memmap; bm->type != BIOS_MAP_END; bm++) {
		if (bm->type == BIOS_MAP_FREE && bm->size > 12 * 1024) {
			if (n++ != 0)
				printf(" ");
			if (bm->size > 1024 * 1024)
				printf("%uM", bm->size / 1024 / 1024);
			else
				printf("%uK", bm->size / 1024);
		}
	}
	printf("]");
}

static void
eif_memprobe_internal(void)
{
	EFI_STATUS		 status;
	UINTN			 mapkey, mmsiz, siz;
	UINT32			 mmver;
	EFI_MEMORY_DESCRIPTOR	*mm0, *mm;
	int			 i, n;
	bios_memmap_t		 *bm, bm0;

	cnvmem = extmem = 0;
	bios_memmap[0].type = BIOS_MAP_END;

	siz = 0;
	status = BS->GetMemoryMap(&siz, NULL, &mapkey, &mmsiz, &mmver);
	if (status != EFI_BUFFER_TOO_SMALL)
		panic("cannot get the size of memory map");
	mm0 = alloc(siz);
	status = BS->GetMemoryMap(&siz, mm0, &mapkey, &mmsiz, &mmver);
	if (status != EFI_SUCCESS)
		panic("cannot get the memory map");
	n = siz / mmsiz;
	mmap_key = mapkey;

	for (i = 0, mm = mm0; i < n; i++, mm = NextMemoryDescriptor(mm, mmsiz)){
		bm0.type = BIOS_MAP_END;
		bm0.addr = mm->PhysicalStart;
		bm0.size = mm->NumberOfPages * EFI_PAGE_SIZE;
		if (mm->Type == EfiReservedMemoryType ||
		    mm->Type == EfiUnusableMemory)
			bm0.type = BIOS_MAP_RES;
		else if (mm->Type == EfiUnusableMemory ||
		    mm->Type == EfiLoaderCode || mm->Type == EfiLoaderData ||
		    mm->Type == EfiBootServicesCode ||
		    mm->Type == EfiBootServicesData ||
		    mm->Type == EfiRuntimeServicesCode ||
		    mm->Type == EfiRuntimeServicesData ||
		    mm->Type == EfiConventionalMemory)
			bm0.type = BIOS_MAP_FREE;
		else if (mm->Type == EfiACPIReclaimMemory)
			bm0.type = BIOS_MAP_ACPI;
		else if (mm->Type == EfiACPIMemoryNVS)
			bm0.type = BIOS_MAP_NVS;
		else
			/*
			 * XXX Is there anything to do for EfiMemoryMappedIO
			 * XXX EfiMemoryMappedIOPortSpace EfiPalCode?
			 */
			bm0.type = BIOS_MAP_RES;

		for (bm = bios_memmap; bm->type != BIOS_MAP_END; bm++) {
			if (bm->type != bm0.type)
				continue;
			if (bm->addr <= bm0.addr &&
			    bm0.addr <= bm->addr + bm->size) {
				bm->size = bm0.addr + bm0.size - bm->addr;
				break;
			} else if (bm0.addr <= bm->addr &&
			    bm->addr <= bm0.addr + bm0.size) {
				bm->size = bm->addr + bm->size - bm0.addr;
				bm->addr = bm0.addr;
				break;
			}
		}
		if (bm->type == BIOS_MAP_END)
			*bm = bm0;
		(++bm)->type = BIOS_MAP_END;
	}
	for (bm = bios_memmap; bm->type != BIOS_MAP_END; bm++) {
		if (bm->addr < 0x0a0000)	/* Below memory hole */
			cnvmem =
			    max(cnvmem, (bm->addr + bm->size) / 1024);
		if (bm->addr >= 0x10000 /* Above the memory hole */ &&
		    bm->addr / 1024 == extmem + 1024)
			extmem += bm->size / 1024;
	}
	free(mm, siz);
}

/***********************************************************************
 *
 ***********************************************************************/
struct diskinfo {
	EFI_HANDLE		*handle;
	int			 unit;
	TAILQ_ENTRY(diskinfo)	 next;
};
static TAILQ_HEAD(,diskinfo)	 dskinfoh = TAILQ_HEAD_INITIALIZER(dskinfoh);

/***********************************************************************
 * Console
 ***********************************************************************/
static SIMPLE_TEXT_OUTPUT_INTERFACE     *conout = NULL;
static SIMPLE_INPUT_INTERFACE           *conin;
static EFI_GUID				 ConsoleControlGUID
					    = EFI_CONSOLE_CONTROL_PROTOCOL_GUID;
static EFI_GUID				 GraphicsOutputGUID
					    = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

struct efi_video {
	int	cols;
	int	rows;
} efi_video[32];

static void
efi_video_init(void)
{
	EFI_CONSOLE_CONTROL_PROTOCOL	*ConsoleControl = NULL;
	int				 i, mode80x25, mode100x31;
	UINTN				 cols, rows;
	EFI_STATUS			 status;

	conout = ST->ConOut;
	status = BS->LocateProtocol(&ConsoleControlGUID, NULL,
	    (void **)&ConsoleControl);
        if (status == EFI_SUCCESS)
		(void)ConsoleControl->SetMode(ConsoleControl,
		    EfiConsoleControlScreenText);
        mode80x25 = -1;
        mode100x31 = -1;
        for (i = 0; ; i++) {
                status = conout->QueryMode(conout, i, &cols, &rows);
                if (EFI_ERROR(status))
                        break;
		if (mode80x25 < 0 && cols == 80 && rows == 25)
                        mode80x25 = i;
		if (mode100x31 < 0 && cols == 100 && rows == 31)
                        mode100x31 = i;
		if (i < nitems(efi_video)) {
			efi_video[i].cols = cols;
			efi_video[i].rows = rows;
		}
        }
	if (mode100x31 >= 0)
                conout->SetMode(conout, mode100x31);
        else if (mode80x25 >= 0)
                conout->SetMode(conout, mode80x25);
	conin = ST->ConIn;
	efi_video_reset();
}

static void
efi_video_reset(void)
{
	conout->EnableCursor(conout, TRUE);
	conout->SetAttribute(conout, EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));
	conout->ClearScreen(conout);
}

void
efi_cons_probe(struct consdev *cp)
{
}

void
efi_cons_init(struct consdev *cp)
{
}

int
efi_cons_getc(dev_t dev)
{
	EFI_INPUT_KEY	 key;
	EFI_STATUS	 status;
	UINTN		 dummy;
	static int	 lastchar = 0;

	if (lastchar) {
		int r = lastchar;
		if ((dev & 0x80) == 0)
			lastchar = 0;
		return (r);
	}

	status = conin->ReadKeyStroke(conin, &key);
	while (status == EFI_NOT_READY) {
		if (dev & 0x80)
			return (0);
		BS->WaitForEvent(1, &conin->WaitForKey, &dummy);
		status = conin->ReadKeyStroke(conin, &key);
	}

	if (dev & 0x80)
		lastchar = key.UnicodeChar;

	return (key.UnicodeChar);
}

void
efi_cons_putc(dev_t dev, int c)
{
	CHAR16	buf[2];

	if (c == '\n')
		efi_cons_putc(dev, '\r');

	buf[0] = c;
	buf[1] = 0;

	conout->OutputString(conout, buf);
}

int
efi_cons_getshifts(dev_t dev)
{
	return (0);
}

/***********************************************************************
 *
 ***********************************************************************/
int
devopen(struct open_file *f, const char *fname, char **file)
{
	struct devsw	*dp = devsw;
	int		 i, rc = 1;

	*file = (char *)fname;
	for (i = 0; i < ndevs && rc != 0; dp++, i++) {
		if ((rc = (*dp->dv_open)(f, file)) == 0) {
			f->f_dev = dp;
			return 0;
		}
	}
	if ((f->f_flags & F_NODEV) == 0)
		f->f_dev = dp;

	return rc;
}

void
_rtt(void)
{
	printf("Hit any key to reboot\n");
	efi_cons_getc(0);
	RS->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
	while (1) { }
}

void
devboot(dev_t bootdev, char *p)
{
	printf("XXX %s %p\n", __func__, p);
}

time_t
getsecs(void)
{
	EFI_TIME		t;
	time_t			r = 0;
	int			y = 0;
	int			daytab[][14] = {
	    { 0, -1, 30, 58, 89, 119, 150, 180, 211, 242, 272, 303, 333, 364 },
	    { 0, -1, 30, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	};
#define isleap(_y) (((_y) % 4) == 0 && (((_y) % 100) != 0 || ((_y) % 400) == 0))

	ST->RuntimeServices->GetTime(&t, NULL);

	/* Calc days from UNIX epoch */
	r = (t.Year - 1970) * 365;
	for (y = 1970; y < t.Year; y++) {
		if (isleap(y))
			r++;
	}
	r += daytab[isleap(t.Year)? 1 : 0][t.Month] + t.Day;

	/* Calc secs */
	r *= 60 * 60 * 24;
	r += ((t.Hour * 60) + t.Minute) * 60 + t.Second;
	if (-24 * 60 < t.TimeZone && t.TimeZone < 24 * 60)
		r += t.TimeZone * 60;

	return (r);
}

int
cnspeed(dev_t dev, int sp)
{
	printf("XXX %s\n", __func__);
	return (9600);
}

int
check_skip_conf(void)
{
	/* XXX */
	return (efi_cons_getshifts(0) & 0x04);
}

dev_t
ttydev(char *name)
{
	printf("XXX %s\n", __func__);
	return (0);
}

char *
ttyname(int fd)
{
	printf("XXX %s\n", __func__);
	return "pc0";
}

void
machdep(void)
{
	int			 i, j;
	struct i386_boot_probes *pr;

	/*
	 * The list of probe routines is now in conf.c.
	 */
	for (i = 0; i < nibprobes; i++) {
		pr = &probe_list[i];
		if (pr != NULL) {
			if (conout != NULL)
				printf("%s:", pr->name);

			for (j = 0; j < pr->count; j++) {
				(*(pr->probes)[j])();
			}

			printf("\n");
		}
	}
}

int
mdrandom(char *buf, size_t buflen)
{
	return (0);
}

/***********************************************************************
 * Paritition
 ***********************************************************************/
static EFI_GUID blkio_guid = BLOCK_IO_PROTOCOL;
static EFI_GUID devp_guid = DEVICE_PATH_PROTOCOL;

int
efip_strategy(void *devdata, int rw, daddr32_t blk, size_t size, void *buf,
    size_t *rsize)
{
	EFI_STATUS	 status = -1;
	EFI_BLOCK_IO	*blkio = devdata;
	size_t		 nsect;

	nsect = (size + blkio->Media->BlockSize - 1) / blkio->Media->BlockSize;
	switch (rw) {
	case F_READ:
		status = blkio->ReadBlocks(blkio,
		    blkio->Media->MediaId, blk,
		    nsect * blkio->Media->BlockSize, buf);
		if (status != EFI_SUCCESS)
			goto on_error;
		break;
	case F_WRITE:
		if (blkio->Media->ReadOnly)
			goto on_error;
		status = blkio->WriteBlocks(blkio,
		    blkio->Media->MediaId, blk,
		    nsect * blkio->Media->BlockSize, buf);
		if (status != EFI_SUCCESS)
			goto on_error;
	}
	if (rsize != NULL)
		*rsize = nsect * blkio->Media->BlockSize;

	return (0);

on_error:
	return (EIO);
}

int
efip_open(struct open_file *f, ...)
{
	va_list		 args;
	char		*cp, **file;
	int		 unit = 0;
	EFI_BLOCK_IO	*blkio;
	EFI_STATUS	 status;
	struct diskinfo *dsk;
	u_char		 buf[4092];
	ssize_t		 sz;

	va_start(args, f);
	cp = *(file = va_arg(args, char **));
	va_end(args);

	f->f_devdata = NULL;
	if (strncmp(cp, "part", 4) != 0)
		return (ENOENT);
	cp += 4;
	while ('0' <= *cp && *cp < '9') {
		unit *= 10;
		unit += (*cp++) - '0';
	}
	if (*cp != ':') {
		printf("Bad unit number\n");
		return EUNIT;
	}
	cp++;
	if (*cp != '\0')
		*file = cp;
	else
		f->f_flags |= F_RAW;

	TAILQ_FOREACH(dsk, &dskinfoh, next) {
		if (dsk->unit == unit)
			break;
	}
	if (dsk == NULL)
		return (ENOENT);

	status = BS->HandleProtocol(dsk->handle, &blkio_guid, (void **)&blkio);
	if (status != EFI_SUCCESS)
		return (EIO);

	efip_strategy(blkio, F_READ, 1, 1, buf, &sz);
	f->f_devdata = blkio;

	return (0);
}

int
efip_close(struct open_file *f)
{
	f->f_devdata = NULL;
	return (0);
}

int
efip_ioctl(struct open_file *f, u_long cmd, void *data)
{
	return (0);
}

void
efip_probe(void)
{
	EFI_STATUS	 status;
	EFI_HANDLE	*h;
	EFI_DEVICE_PATH	*devp0, *devp;
	UINTN		 sz;
	int		 i;
	struct diskinfo *dsk;

	sz = 0;
	status = BS->LocateHandle(ByProtocol, &blkio_guid, 0, &sz, 0);
	if (status == EFI_BUFFER_TOO_SMALL) {
		h = (EFI_HANDLE)alloc(sz * 3);
		BS->LocateHandle(ByProtocol, &blkio_guid, 0, &sz, h);
	}

	for (i = 0; i < sz / sizeof(EFI_HANDLE); i++) {
		status = BS->HandleProtocol(h[i], &devp_guid, (void **)&devp0);
		if (EFI_ERROR(status))
			continue;
		for (devp = devp0; !IsDevicePathEnd(devp);
		    devp = NextDevicePathNode(devp)) {
			if (DevicePathType(devp) != MEDIA_DEVICE_PATH ||
			    DevicePathSubType(devp) != MEDIA_HARDDRIVE_DP)
				continue;
			dsk = alloc(sizeof(struct diskinfo));
			if (dsk == NULL)
				panic("diskprobe(): alloc() failed");
			dsk->unit = i;
			dsk->handle = h[i];
			TAILQ_INSERT_TAIL(&dskinfoh, dsk, next);
			printf(" part%d", i);
		}
	}
	free(h, sz * 3);
}

/***********************************************************************
 * Commands
 ***********************************************************************/
int
Xexit_efi(void)
{
	BS->Exit(IH, 0, 0, NULL);
	while (1) { }
}

int
Xvideo_efi(void)
{
	int	 i, mode = -1;
	char	*p;

	for (i = 0; i < nitems(efi_video) && efi_video[i].cols > 0; i++) {
		printf("Mode %d: %d x %d\n", i,
		    efi_video[i].cols, efi_video[i].rows);
	}
	if (cmd.argc == 2) {
		p = cmd.argv[1];
		mode = strtol(p, &p, 10);
	}
	printf("\nCurrent Mode = %d\n", conout->Mode->Mode);
	if (0 <= mode && mode < i) {
		conout->SetMode(conout, mode);
		efi_video_reset();
	}

	return (0);
}

/*
 * ACPI GUID is confusing in UEFI spec.
 * {EFI_,}_ACPI_20_TABLE_GUID or EFI_ACPI_TABLE_GUID means
 * ACPI 2.0 or abobe.
 */
static EFI_GUID acpi_guid = ACPI_20_TABLE_GUID;
static EFI_GUID smbios_guid = SMBIOS_TABLE_GUID;

#define	efi_guidcmp(_a, _b)	memcmp((_a), (_b), sizeof(EFI_GUID))

void dump_diskinfo(void) { }
int com_addr;
int com_speed = -1;
struct diskinfo *bootdev_dip;

int
biosd_io(int a, bios_diskinfo_t *b, u_int c, int d, void *e)
{
	return -1;
}

int
bootbuf(void *a, int b)
{
	return -1;
}


bios_diskinfo_t *
bios_dklookup(int x)
{
	return NULL;
}

static void
hexdump(u_char *p, int len)
{
	int		 i;
	const char	 hexstr[] = "0123456789abcdef";
	char		 hs[3];

	for (i = 0; i < len; i++) {
		hs[0] = hexstr[p[i] >> 4];
		hs[1] = hexstr[p[i] & 0xf];
		hs[2] = '\0';
		if (i % 16 == 8)
			printf(" - ");
		else if (i % 16 != 0)
			printf(" ");
		printf("%s", hs);
		if (i % 16 == 15)
			printf("\n");
	}
	if (i % 16 != 0)
		printf("\n");
}

void
efi_makebootargs(void)
{
	int		 i;
	EFI_STATUS	 status;
	EFI_GRAPHICS_OUTPUT
			*gop;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION
			*gopi;
	bios_efiinfo_t	 ei;

	/*
	 * ACPI, BIOS configuration table
	 */
	for (i = 0; i < ST->NumberOfTableEntries; i++) {
		if (efi_guidcmp(&acpi_guid,
		    &ST->ConfigurationTable[i].VendorGuid) == 0)
			ei.config_acpi = (intptr_t)
			    ST->ConfigurationTable[i].VendorTable;
		else if (efi_guidcmp(&smbios_guid,
		    &ST->ConfigurationTable[i].VendorGuid) == 0)
			ei.config_smbios = (intptr_t)
			    ST->ConfigurationTable[i].VendorTable;
	}

	/*
	 * Frame buffer
	 */
	status = BS->LocateProtocol(&GraphicsOutputGUID, NULL, (void **)&gop);
	if (EFI_ERROR(status))
		panic("could not find EFI_GRAPHICS_OUTPUT_PROTOCOL");
	gopi = gop->Mode->Info;
	switch (gopi->PixelFormat) {
	case PixelBlueGreenRedReserved8BitPerColor:
		ei.fb_red_mask      = 0x00ff0000;
		ei.fb_green_mask    = 0x0000ff00;
		ei.fb_blue_mask     = 0x000000ff;
		ei.fb_reserved_mask = 0xff000000;
		break;
	case PixelRedGreenBlueReserved8BitPerColor:
		ei.fb_red_mask      = 0x000000ff;
		ei.fb_green_mask    = 0x0000ff00;
		ei.fb_blue_mask     = 0x00ff0000;
		ei.fb_reserved_mask = 0xff000000;
		break;
	case PixelBitMask:
		ei.fb_red_mask = gopi->PixelInformation.RedMask;
		ei.fb_green_mask = gopi->PixelInformation.GreenMask;
		ei.fb_blue_mask = gopi->PixelInformation.BlueMask;
		ei.fb_reserved_mask = gopi->PixelInformation.ReservedMask;
		break;
	default:
		break;
	}
	ei.fb_addr = gop->Mode->FrameBufferBase;
	ei.fb_size = gop->Mode->FrameBufferSize;
	ei.fb_height = gopi->VerticalResolution;
	ei.fb_width = gopi->HorizontalResolution;
	ei.fb_pixpsl = gopi->PixelsPerScanLine;

	addbootarg(BOOTARG_EFIINFO, sizeof(ei), &ei);
}
