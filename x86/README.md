# Kresten x86 (v2.0)

x86-64 旗舰版:GRUB multiboot 内核,浏览器式标签桌面,硬盘安装器,真 VT-x 虚拟机,Linux 子系统。

## 功能

- **内核**:x86-64 长模式 (`-m64`),PML4 四级分页 (2MiB 大页),GDT/IDT,抢占式调度器 (PIT 100 Hz)
- **桌面**:标签式 GUI (App Manager / Shell / FMGR / My Computer / Recycle Bin),深色主题,Terminus 8x16 字体
- **显卡**:VMware SVGA (32bpp)、VESA VBE (8bpp)、mode 13h 回退;自适应分辨率 (640x480 ~ 1280x768)
- **驱动**:ATA PIO 磁盘、PS/2 键盘鼠标、RTC、rtl8139 网卡、iso9660 光驱 (`cdrom` 命令)、gzip (inflate)
- **网络**:完整 TCP/IP 协议栈 (ARP/DHCP/ICMP/DNS/UDP/TCP) + `ping/fetch/dhcp` 命令
- **安装器**:`kresten-x86-install.iso` 自动写 512B MBR + 内核镜像到第一块硬盘后重启
- **注册表**:key/value 注册表 (registry),持久化到内存映射区
- **VM 插件**:真 Intel VT-x hypervisor (VMXON/VMCS/EPT/VMLAUNCH),访客 OS 为 32 位保护模式 (`vm_guest.asm`);无 VT-x (QEMU TCG) 时优雅降级
- **Linux 子系统 (LX)**:x86-64 ELF64 busybox (WSL1 同构,LSTAR syscall 翻译),交互式 ash;`/mnt/cdrom` 经 iso9660 挂载

## 构建 (Windows)

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Users\Lenovo\Documents\New OpenCode Project\build_iso.ps1"
```

产出到桌面:普通版 `kresten-x86.iso` + 安装版 `kresten-x86-install.iso`。

## 运行 (QEMU)

```sh
qemu-system-x86_64 -machine pc -bios /usr/share/seabios/bios.bin -m 128M \
  -cdrom dist/kresten-x86.iso -boot d -vga vmware
# 安装版(两阶段,一条命令):
qemu-system-x86_64 -m 128M -hda install.img -cdrom dist/kresten-x86-install.iso \
  -boot once=d,order=c -vga vmware
```

## 脚本位置

| 脚本 | 位置 | 作用 |
|---|---|---|
| `build_iso.ps1` | 仓库根 | 内核 + 普通 ISO + 安装版 ISO → 桌面 |
| `scripts/rebuild.sh` | 本目录 | WSL 内 make + 普通 ISO |
| `scripts/make_iso_x86.sh` | 本目录 | GRUB 可引导 ISO |
| `scripts/make_install_iso_x86.sh` | 本目录 | 安装版 ISO (kernel-install) |
| `scripts/pack_install_img.py` | 本目录 | kernel.elf → 安装镜像 C 数组 (`.install_img` 段, 0xE04000) |
| `scripts/gen_vm_guest.sh` + `vm_guest.asm` | 本目录 | VMX 访客 OS 汇编 → `plugins/vm_guest.h` |

## 测试

`tests/`:`x86_svga_test.py` / `x86_vbe_test3.py` / `x86_install_test.py` / `tests/linux/` (LX guest ELF 生成)。

## 文档

- `docs/LINUX-SUBSYSTEM.md` — LX 子系统设计文档 (WSL1 对照)