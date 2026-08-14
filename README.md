# Kresten v2.0

A small hobby operating system, written from scratch, for three architectures:
**x86** (x86-64 kernel with a tabbed browser-style GUI desktop, a real
installer that writes itself to a hard disk, and an Intel VT-x hypervisor),
**ARM** (Versatile PB, semihosted console build) and **RISC-V** (QEMU virt,
U-mode guest / M-mode kernel isolation).

Each build ships with the **Linux subsystem (LX)** — a WSL1-style binary
compatibility layer that translates Linux syscalls to Kresten kernel
services, running a statically linked busybox `ash` shell interactively.

## Features

| | x86 | ARM | RISC-V |
|---|---|---|---|
| Kernel | x86-64 long mode (`-m64`), PML4 paging | ARM32 (armv5te) | RV64 (rv64gc) |
| Guest isolation | ring0 flat (WSL1-style) | USR mode (svc traps) | **U-mode + M-mode traps** |
| Tabbed desktop GUI | yes | yes | yes |
| Preemptive scheduler (100 Hz) | PIT | CLINT | CLINT |
| ATA PIO disk / installer | yes | - | - |
| CD-ROM (iso9660) + `cdrom` cmd | yes | - | - |
| Registry | yes | - | - |
| NIC + TCP/IP stack | rtl8139 | SMC91C111 | stub (no NIC) |
| VT-x hypervisor | yes (VMX/EPT) | - | - |
| VM plugin (software mode) | fallback on TCG | yes | yes |
| Linux subsystem (LX) | x86-64 busybox | ARM32 busybox | RV64 busybox |
| LX: interactive ash | yes | yes | yes (termios2 + ppoll) |
| LX: `/mnt/cdrom` | yes | - | - |

## Build (Windows, one command per architecture)

| Script | Location | Produces |
|---|---|---|
| `build_iso.ps1` | repo root | `Desktop\kresten-x86.iso` (GRUB-bootable) + `Desktop\kresten-x86-install.iso` (installer) |
| `build_arm.ps1` | repo root | `Desktop\kresten-arm.iso` (distribution: kernel + README + RUN scripts) |
| `build_rv64.ps1` | repo root | `Desktop\kresten-riscv.iso` + `Kresten\riscv\dist\kernel.elf` |

All scripts stage the sources through
`C:\Users\Lenovo\AppData\Local\Temp\opencode\` (relay), build inside WSL,
and copy the results to the Desktop.

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Users\Lenovo\Documents\New OpenCode Project\build_iso.ps1"
powershell -ExecutionPolicy Bypass -File "C:\Users\Lenovo\Documents\New OpenCode Project\build_arm.ps1"
powershell -ExecutionPolicy Bypass -File "C:\Users\Lenovo\Documents\New OpenCode Project\build_rv64.ps1"
```

## Build (Linux / WSL)

```sh
cd x86 && make && make iso        # dist/kresten-x86.iso + dist/kresten-x86-install.iso
cd arm && make && make iso        # dist/kresten-arm.iso
cd riscv && make && make iso      # dist/kresten-riscv.iso
```

## Per-architecture scripts

| Architecture | Scripts |
|---|---|
| x86 | `x86/scripts/rebuild.sh` (WSL make + ISO), `make_iso_x86.sh` (GRUB ISO), `make_install_iso_x86.sh` (installer ISO), `pack_install_img.py` (kernel → install image, `.install_img` at 0xE04000), `gen_vm_guest.sh` + `vm_guest.asm` (VT-x guest) |
| ARM | `arm/scripts/make_iso.sh` (distribution ISO), `gen_crt_table.py` (crt_compressed.ld) |
| RISC-V | `riscv/scripts/make_iso.sh` (distribution ISO), `gen_crt_table.py` |

## Run (QEMU)

```sh
# x86: regular ISO
qemu-system-x86_64 -m 128M -cdrom dist/kresten-x86.iso -boot d -vga vmware
# x86: install to a virtual disk, then boot from it
qemu-system-x86_64 -m 128M -hda install.img -cdrom dist/kresten-x86-install.iso -boot once=d,order=c -vga vmware

# ARM (Versatile PB)
qemu-system-arm -machine versatilepb -kernel dist/kernel.elf -nographic \
  -serial mon:stdio -no-reboot -semihosting -net nic,model=smc91c111 -net user

# RISC-V (virt)
qemu-system-riscv64 -machine virt -cpu max -bios none -kernel dist/kernel.elf \
  -nographic -serial mon:stdio -no-reboot
```

In the shell: `help` for commands; `vm linux` (or `vm`) enters the Linux
subsystem — an interactive busybox `ash` (`echo hello`, `exit`).

## Requirements (build)

A Linux (WSL) environment with:

```sh
sudo apt install build-essential gcc-multilib binutils python3 grub-pc-bin \
                 xorriso mtools gcc-arm-none-eabi genisoimage \
                 gcc-riscv64-linux-gnu qemu-system-x86 qemu-system-arm \
                 qemu-system-riscv64 seabios
```

Tested with: GCC 15.2 (x86), GCC 15.2 riscv64-linux-gnu, arm-none-eabi,
GRUB 2.14, QEMU 10.2, genisoimage, Python 3.

## Repository layout

```
Kresten/
  README.md          this file
  CHANGELOG.md       release history (v2.0: RISC-V, LX subsystem, ...)
  x86/               x86-64 build (multiboot boot, GDT/IDT, MBR, installer,
                     drivers incl. ATA/iso9660/rtl8139, net stack, registry,
                     VT-x vm plugin, LX subsystem, tests/)
  arm/               ARM build (Versatile PB: lcd/timer/net + LX subsystem)
  riscv/             RISC-V build (virt: lcd/timer + U-mode guest, LX subsystem)
build_iso.ps1        x86 build script (kernel + both ISOs -> Desktop)
build_arm.ps1        ARM build script (kernel + ISO -> Desktop)
build_rv64.ps1       RISC-V build script (kernel + ISO -> Desktop)
```

The desktop GUI (`shared/plugins/desktop.c`), the PS/2 packet decoder and
the minimal libc are shared between the ARM and RISC-V builds; x86 keeps its
own richer driver set.

## License

GPL-3.0 — see [LICENSE](LICENSE).
