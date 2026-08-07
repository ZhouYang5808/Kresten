#!/bin/bash
# Build dist/kresten-x86.iso - GRUB-bootable ISO for VMware/QEMU.
set -e
cd "$(dirname "$0")/.."

VERSION="1.0"
STAGE="build/isox86"

rm -rf "$STAGE"
mkdir -p "$STAGE/boot/grub"
for cand in dist/kernel.elf x86/dist/kernel.elf; do
    if [ -f "$cand" ]; then cp "$cand" "$STAGE/boot/kernel.elf"; break; fi
done

cat > "$STAGE/boot/grub/grub.cfg" <<EOF
set timeout=0
set default=0

menuentry "Kresten v$VERSION (x86)" {
    multiboot /boot/kernel.elf
    boot
}
EOF

grub-mkrescue --quiet -o dist/kresten-x86.iso "$STAGE"

echo "=== ISO built: dist/kresten-x86.iso ==="
ls -l dist/kresten-x86.iso
