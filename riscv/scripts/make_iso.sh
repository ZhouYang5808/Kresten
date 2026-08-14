#!/bin/bash
# Build dist/kresten-riscv.iso - Kresten release ISO (kernel + docs + run scripts).
# Note: the riscv virt machine boots via -kernel; the ISO is the distribution
# medium (same convention as the ARM build).
set -e
cd "$(dirname "$0")/.."

VERSION="2.0"
STAGE="build/iso"

rm -rf "$STAGE"
mkdir -p "$STAGE/boot"
cp dist/kernel.elf "$STAGE/boot/kernel.elf"

cat > "$STAGE/README.TXT" <<EOF
Kresten v$VERSION - RISC-V (QEMU virt)

A small RISC-V operating system with:
  - DOS-like shell (help for commands)
  - FAT-style virtual filesystem (driver: fs)
  - PL011 UART console, PL050 keyboard, PL110-style LCD framebuffer (lcd)
  - Driver framework: drivers loaded at boot (DRIVERS command)
  - VM plugin (software emulation): vm / vm list / vm linux
  - Linux subsystem (LX): interactive busybox ash (echo hello / exit)
  - Preemptive round-robin scheduler (CLINT, 100 Hz)

RUN:
  Windows : double-click RUN.BAT
  Linux   : ./RUN.SH
  Manual  : qemu-system-riscv64 -machine virt -cpu max -bios none
            -kernel boot/kernel.elf -nographic -serial mon:stdio -no-reboot

Build: make && make iso
EOF

cat > "$STAGE/RUN.BAT" <<EOF
@echo off
qemu-system-riscv64 -machine virt -cpu max -bios none -kernel "%~dp0boot\kernel.elf" -nographic -serial mon:stdio -no-reboot
EOF

cat > "$STAGE/RUN.SH" <<'EOF'
#!/bin/sh
DIR=$(dirname "$0")
exec qemu-system-riscv64 -machine virt -cpu max -bios none -kernel "$DIR/boot/kernel.elf" -nographic -serial mon:stdio -no-reboot
EOF
chmod +x "$STAGE/RUN.SH"

genisoimage -quiet -o dist/kresten-riscv.iso \
    -V "KRESTEN_$VERSION" -A "Kresten v$VERSION" -J -R "$STAGE"

echo "=== ISO built: dist/kresten-riscv.iso ==="
ls -l dist/kresten-riscv.iso
