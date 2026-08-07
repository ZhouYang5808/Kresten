# Kresten

A small hobby operating system for x86, written from scratch: a multiboot
kernel with preemptive multitasking, a tabbed browser-style GUI desktop, and
a real installer that writes itself to a hard disk and boots from it.

Kresten boots via GRUB (multiboot v1) and includes its own 512-byte MBR
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

A Linux environment with 32-bit GCC multilib toolchain:

```sh
sudo apt install gcc-multilib binutils python3 grub-pc-bin xorriso mtools
```

(QEMU is optional but recommended for testing: `qemu-system-x86`)

## Build

```sh
make          # dist/kernel.elf
make iso      # dist/myos-x86.iso + dist/myos-x86-install.iso
```

- `dist/myos-x86.iso` — regular bootable ISO (GRUB)
- `dist/myos-x86-install.iso` — installer ISO: boots, installs to the
  first hard disk (MBR + image), reboots into the installed system

## Run

Quick test in QEMU:

```sh
make run
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

## Repository layout

```
arch/      boot (multiboot header), GDT/IDT, interrupts, switch,
           MBR boot sector (arch/mbr.s)
kernel/    kernel_main, installer, scheduler support, registry, malloc,
           process, ELF, plugins, editor, fs, env
drivers/   ata, gfx (SVGA/VBE/mode13h), keyboard, mouse, rtc, vga
libc/      stdio, stdlib, string, ctype
include/   kernel headers
plugins/   hello, runtime, shell (linked-in plugins)
shared/    desktop GUI plugin, PS/2 packet decoder
scripts/   ISO build scripts, install image packer
tests/     QEMU regression scripts (install flow, SVGA, VBE)
Makefile   build (kernel / installer / iso targets)
link.ld    linker script (kernel at 1 MB, .plugins section)
```

## Screenshots

Boot the ISO in QEMU or VMware; the desktop opens to the App Manager.

## License

GPL-3.0 — see [LICENSE](LICENSE).
