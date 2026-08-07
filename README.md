# Kresten

A small hobby operating system, written from scratch, for two architectures:
**x86** (multiboot kernel with a tabbed browser-style GUI desktop and a real
installer that writes itself to a hard disk) and **ARM** (Versatile PB,
semihosted console build).

The x86 build boots via GRUB (multiboot v1) and includes its own 512-byte MBR
boot sector that loads the kernel from the hard disk in protected mode via
raw ATA PIO — no BIOS disk services, no second-stage bootloader.

## Features

- **Kernel** — i386 multiboot kernel linked at 1 MB (`link.ld`), GDT/IDT,
  IRQ handling, preemptive round-robin scheduler (PIT 100 Hz)
- **Tabbed desktop GUI** — fullscreen browser-style tabs, dark theme,
  App Manager launcher (mouse + keyboard navigation), Shell terminal,
  FMGR file list, My Computer, Recycle Bin; Terminus 8x16 bitmap font
- **Graphics** — VMware SVGA (32 bpp), VESA VBE (8 bpp), mode 13h fallback;
  auto-fit resolutions (640x480, 800x600, up to 1280x768)
- **Drivers** — ATA PIO disk, PS/2 keyboard + mouse, RTC
- **Plugin system** — plugins are linked into the kernel image and
  discovered via the `.plugins` linker section
- **Registry** — simple key/value registry persisted to a RAM region chosen
  from the multiboot memory map
- **Installer** — `make iso` also builds an installer ISO that detects the
  first ATA drive, writes the 512-byte MBR boot sector + kernel image to it,
  then reboots automatically into the installed system
- **Minimal libc** — stdio / stdlib / string / ctype, kernel-friendly

## Requirements (build)

A Linux environment with the 32-bit GCC multilib toolchain (x86) and/or the
ARM cross toolchain (ARM):

```sh
sudo apt install gcc-multilib binutils python3 grub-pc-bin xorriso mtools
sudo apt install gcc-arm-none-eabi   # ARM build only
```

(QEMU is optional but recommended for testing: `qemu-system-x86`,
`qemu-system-arm`)

## Build

The repository is split into two independent builds, one per architecture:

```sh
cd x86 && make          # x86/dist/kernel.elf
cd x86 && make iso      # x86/dist/myos-x86.iso + myos-x86-install.iso

cd arm && make          # arm/dist/kernel.elf (Versatile PB)
```

### x86

- `dist/myos-x86.iso` — regular bootable ISO (GRUB)
- `dist/myos-x86-install.iso` — installer ISO: boots, installs to the
  first hard disk (MBR + image), reboots into the installed system

## Run

Quick test in QEMU:

```sh
cd x86 && make run
```

Or boot the ISO:

```sh
qemu-system-i386 -m 128M -cdrom dist/myos-x86.iso -boot d -vga vmware
```

Install to a virtual hard disk and boot from it (two phases, one command):

```sh
qemu-system-i386 -m 128M -hda install.img -cdrom dist/myos-x86-install.iso \
  -boot once=d,order=c -vga vmware
```

After the kernel prints `[INFO] INSTALL DONE` it reboots; the VM then
boots from the hard disk into the desktop. Tested on QEMU and VMware.

ARM (Versatile PB, semihosted console):

```sh
cd arm && make run
```

## Repository layout

```
x86/       x86 build: Makefile, arch/ (multiboot boot.s, GDT/IDT,
           interrupts, MBR boot sector), kernel/, drivers/ (SVGA/VBE),
           libc/, include/, plugins/, shared/ (desktop GUI), scripts/
           (ISO build + install packer), tests/ (QEMU regression scripts)
arm/       ARM build: Makefile (arm-none-eabi), boot/start.s, kernel/
           (Versatile PB: lcd/timer/net + installer), include/, shared/
           (desktop GUI), scripts/ (gen_crt_table.py, make_iso.sh)
```

The desktop GUI (`shared/plugins/desktop.c`), the PS/2 packet decoder and
the minimal libc are shared between both architectures.

## License

GPL-3.0 — see [LICENSE](LICENSE).
