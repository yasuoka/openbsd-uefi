UEFI boot for OpenBSD
=====================

Experimental.


Current Status
--------------

- Boot complete on VAIO Z VJZ13A1D


How to play
-----------

- Use OpenBSD/amd64
- Requires 'llvm' (clang) to compile the EFI boot loader.  Since EFI uses
  MS-ABI which is not supported by OpenBSD's gcc.


Compile "efiboot.efi":

    % cd sys/arch/amd64/stand/efiboot
    % env CC=clang make

Compile "kernel":

    % cd sys/arch/amd64/conf
    % config GENERIC.MP
    % cd ../compile/GENERIC.MP
    % make

Install:

- Copy efiboot.efi as /BOOT/EFI/BOOTX64.EFI on first active partition on
  first disk
- Copy the compiled kernel to somewhere on a OpenBSD partition

Boot:

- OpenBSD boot(8) will start

Other commands added to boot(8):

    machine video ..... show available video mode
    machine video # ... change the current video mode
    machine exit ...... exit EFI BOOT, then next EFI boot will start

X11:

- Currently only works with framebuffer if the machine don't have a VGA.
- Patch xenocara-wsfb.diff to xenocra
- Use xorg.conf to use wsfb.

GPT:

- Compile src/sbin/gdisk
- Try gdisk(8)

TODO
----

- Support serial console, CDROM and floppy disk.
- Support 32bit x86 UEFI
- Test other machines than VAIO
