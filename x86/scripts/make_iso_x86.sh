#!/bin/bash
# Build dist/myos-x86.iso 鈥?GRUB-bootable ISO for VMware/QEMU.
set -e
cd "$(dirname "$0")/.."

VERSION="1.0"
STAGE="build/isox86"

rm -rf "$STAGE"
mkdir -p "$STAGE/boot/grub"
cp dist/kernel.elf "$STAGE/boot/kernel.elf"

cat > "$STAGE/boot/grub/grub.cfg" <<EOF
set timeout=0
set default=0

menuentry "MyOS v$VERSION (x86)" {
    multiboot /boot/kernel.elf
    boot
}
EOF

grub-mkrescue --quiet -o dist/myos-x86.iso "$STAGE"

echo "=== ISO built: dist/myos-x86.iso ==="
ls -l dist/myos-x86.iso
