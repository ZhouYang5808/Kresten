#!/bin/bash
# Build dist/kresten-x86.iso - GRUB-bootable ISO for VMware/QEMU.
set -e
cd "$(dirname "$0")/.."

VERSION="3.1"
STAGE="build/isox86"

rm -rf "$STAGE"
mkdir -p "$STAGE/boot/grub"
for cand in dist/kernel.elf x86/dist/kernel.elf; do
    if [ -f "$cand" ]; then cp "$cand" "$STAGE/boot/kernel.elf"; break; fi
done

if [ -d dist/plugins ]; then
    mkdir -p "$STAGE/plugins/kernel" "$STAGE/plugins/runtime" "$STAGE/plugins/user"
    for p in runtime vm; do
        if [ -f "dist/plugins/$p.kplg" ]; then cp "dist/plugins/$p.kplg" "$STAGE/plugins/kernel/"; fi
    done
    for p in linux; do
        if [ -f "dist/plugins/$p.kplg" ]; then cp "dist/plugins/$p.kplg" "$STAGE/plugins/runtime/"; fi
    done
    for p in shell desktop hello taskmgr; do
        if [ -f "dist/plugins/$p.kplg" ]; then cp "dist/plugins/$p.kplg" "$STAGE/plugins/user/"; fi
    done
fi

if [ -d dist/drivers ]; then
    mkdir -p "$STAGE/drivers"
    cp dist/drivers/*.kdrv "$STAGE/drivers/"
fi

if [ -d dist/fonts ]; then
    mkdir -p "$STAGE/fonts"
    cp dist/fonts/*.kdrv "$STAGE/fonts/"
fi

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
