#!/bin/bash
# Build dist/kresten-arm.iso — Kresten release ISO (kernel + docs + run scripts).
# Note: versatilepb boots via -kernel; the ISO is the distribution medium.
set -e
cd "$(dirname "$0")/.."

VERSION="1.0"
STAGE="build/iso"

rm -rf "$STAGE"
mkdir -p "$STAGE/boot"
cp dist/kernel.elf "$STAGE/boot/kernel.elf"

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
