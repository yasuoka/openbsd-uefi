UEFI boot for OpenBSD
=====================

Current Status
--------------

- Start booting on VAIO Z VJZ13A1D
- Frame buffer console is available
- ACPI doesn't work yet.  But it will available soon.


How to play
-----------

- Use OpenBSD/amd64
- Requires 'llvm' (clang) to compile the EFI boot loader.  Since EFI uses
  MS-ABI which is not supported by OpenBSD's gcc.


Compile efiboot.efi:

    % cd sys/arch/amd64/stand/efiboot
    % env CC=clang make

Compile kernel:

    % cd sys/arch/amd64/conf
    % config GENERIC.MP
    % cd ../compile/GENERIC.MP
    % make

Install:

- Place efiboot.efi to /BOOT/EFI/BOOTX86.EFI in the boot disk
- Place the compiled kernel to somewhere on a OpenBSD partition

Specify the kernel image:

    boot> ls part1:/        (show the file list on parition #1)
    boot> boot part1:/bsd   (boot the /bsd)

Other commands:

    machine video ..... show available video mode
    machine video # ... change the current video mode
    machine exit ...... exit EFI BOOT, then next EFI boot will start

