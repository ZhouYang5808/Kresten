#!/bin/bash
# Build dist/kresten-arm.iso 鈥?Kresten release ISO (kernel + docs + run scripts).
# Note: versatilepb boots via -kernel; the ISO is the distribution medium
# (kernel.elf embeds plugins/drivers/fonts, the ISO copies are for reference).
set -e
cd "$(dirname "$0")/.."

VERSION="3.1"
STAGE="build/iso"

rm -rf "$STAGE"
mkdir -p "$STAGE/boot"
cp dist/kernel.elf "$STAGE/boot/kernel.elf"

if [ -d dist/plugins ]; then
    mkdir -p "$STAGE/plugins/kernel" "$STAGE/plugins/runtime" "$STAGE/plugins/user"
    for p in crt_bind vm; do
        if [ -f "dist/plugins/$p.kplg" ]; then cp "dist/plugins/$p.kplg" "$STAGE/plugins/kernel/"; fi
    done
    for p in linux; do
        if [ -f "dist/plugins/$p.kplg" ]; then cp "dist/plugins/$p.kplg" "$STAGE/plugins/runtime/"; fi
    done
    for p in shell desktop hello; do
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

cat > "$STAGE/README.TXT" <<EOF
Kresten v$VERSION - ARM Versatile/PB (QEMU)

A small ARM operating system with:
  - DOS-like shell (help for commands)
  - FAT-style virtual filesystem (driver: fs)
  - SMC91C111 network stack: DHCP, ARP, ICMP/PING, DNS, HTTP/FETCH
  - PL110 LCD framebuffer driver (lcd) and PL050 keyboard driver (kbd)
  - Driver framework: drivers loaded at boot (DRIVERS command)

RUN:
  Windows : double-click RUN.BAT
  Linux   : ./RUN.SH
  Manual  : qemu-system-arm -machine versatilepb -kernel boot/kernel.elf
            -nographic -serial mon:stdio -no-reboot -semihosting
            -net nic,model=smc91c111 -net user

Build: make && make iso
EOF

cat > "$STAGE/RUN.BAT" <<EOF
@echo off
qemu-system-arm -machine versatilepb -kernel "%~dp0boot\kernel.elf" -nographic -serial mon:stdio -no-reboot -semihosting -net nic,model=smc91c111 -net user
EOF

cat > "$STAGE/RUN.SH" <<'EOF'
#!/bin/sh
DIR=$(dirname "$0")
exec qemu-system-arm -machine versatilepb -kernel "$DIR/boot/kernel.elf" -nographic -serial mon:stdio -no-reboot -semihosting -net nic,model=smc91c111 -net user
EOF
chmod +x "$STAGE/RUN.SH"

genisoimage -quiet -o dist/kresten-arm.iso \
    -V "KRESTEN_$VERSION" -A "Kresten v$VERSION" -J -R "$STAGE"

echo "=== ISO built: dist/kresten-arm.iso ==="
ls -l dist/kresten-arm.iso
