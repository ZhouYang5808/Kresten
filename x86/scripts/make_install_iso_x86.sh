#!/bin/bash
# Build dist/kresten-x86-install.iso - installer ISO.
# Uses kernel-install.elf (kernel with the install image embedded); the
# kernel's install mode writes MBR + image to the first ATA drive.
set -e
cd "$(dirname "$0")/.."

VERSION="1.0"
STAGE="build/isox86-install"

rm -rf "$STAGE"
mkdir -p "$STAGE/boot/grub"

for cand in dist/kernel-install.elf x86/dist/kernel-install.elf; do
    if [ -f "$cand" ]; then cp "$cand" "$STAGE/boot/kernel.elf"; break; fi
done

cat > "$STAGE/boot/grub/grub.cfg" <<EOF
set timeout=0
set default=0

menuentry "Kresten v$VERSION installer (x86)" {
    multiboot /boot/kernel.elf
    boot
}
EOF

grub-mkrescue --quiet -o dist/kresten-x86-install.iso "$STAGE"

echo "=== Installer ISO built: dist/kresten-x86-install.iso ==="
ls -l dist/kresten-x86-install.iso
