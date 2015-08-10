UEFI boot for OpenBSD
=====================

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
- Select your OpenBSD partition in part1, part2, ... partN
- ls command can be used for find the kernel image

Example:

    boot> ls part1:/        (show the file list on parition #1)
    boot> boot part1:/bsd   (boot the /bsd)

Other commands added to boot(8):

    machine video ..... show available video mode
    machine video # ... change the current video mode
    machine exit ...... exit EFI BOOT, then next EFI boot will start

TODO
----

- Pass the boot parameters to the kernel completely
  - BOOTDUID
- Support boot from softraid
- Support serial console
- Use "drive" instead of partition
- Read boot.conf
- Read random.seed
- Layout of the BOOT codes
  - Should we put the boot code and boot.conf on the OpenBSD partion?
    Then put the 1st boot on \BOOT\EFI\BOOTX64.EFI and it starts /boot
    put on the OpenBSD partition?
  - how to work with installboot(8)?
  - how to work with fdisk(8)? GPT?
- Boot from CDROM
  - How to create a UEFI CDROM.  Can it work both on BIOS and UEFI?
- Support i386?
- Test other machines than VAIO
