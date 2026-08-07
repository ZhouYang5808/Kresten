#!/bin/bash
# Build dist/myos-x86-install.iso - installer ISO.
# Uses kernel-install.elf (kernel with the install image embedded); the
# kernel's install mode writes MBR + image to the first ATA drive.
set -e
cd "$(dirname "$0")/.."

VERSION="1.0"
STAGE="build/isox86-install"

rm -rf "$STAGE"
mkdir -p "$STAGE/boot/grub"

cp dist/kernel-install.elf "$STAGE/boot/kernel.elf"

cat > "$STAGE/boot/grub/grub.cfg" <<EOF
set timeout=0
set default=0

menuentry "MyOS v$VERSION installer (x86)" {
    multiboot /boot/kernel.elf
    boot
}
EOF

grub-mkrescue --quiet -o dist/myos-x86-install.iso "$STAGE"

echo "=== Installer ISO built: dist/myos-x86-install.iso ==="
ls -l dist/myos-x86-install.iso
