#!/bin/bash
# Regenerate plugins/vm_guest.h from the compiled guest binary.
set -e
cd /tmp/kresten
sed -i 's/_tmp_kresten_vm_guest_bin/vm_guest_bin/' vm_guest_hex.c
{
  echo "// Generated from scripts/vm_guest.asm - Kresten VM guest OS binary."
  echo "// Regenerate: scripts/gen_vm_guest.sh (needs nasm; do not edit by hand)."
  echo "#ifndef _VM_GUEST_H"
  echo "#define _VM_GUEST_H"
  echo
  echo "extern unsigned char vm_guest_bin[];"
  echo "extern unsigned int vm_guest_bin_len;"
  echo
  echo "unsigned char vm_guest_bin[] = {"
  grep -v '^unsigned' vm_guest_hex.c
  echo "unsigned int vm_guest_bin_len = sizeof(vm_guest_bin);"
  echo "#endif"
} > vm_guest.h
cp vm_guest.h /mnt/c/Users/Lenovo/AppData/Local/Temp/opencode/vm_guest.h
echo "GENERATED_OK bytes=$(stat -c %s vm_guest.bin)"
