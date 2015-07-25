UEFI boot for OpenBSD
=====================

- requires 'llvm' from package.  since OpenBSD's gcc doesn't support MS
  ABI which is required to call EFI firmware functions.
- Also do setenv CC=clang before build for same reason
