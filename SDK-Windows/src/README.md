# Kresten x86 (v3.1)

x86-64 旗舰版:GRUB multiboot 内核,浏览器式标签桌面 (含图形任务管理器),硬盘安装器,真 VT-x 虚拟机,Windows 程序运行时 (Wine),Linux 子系统。

## 功能

- **内核**:x86-64 长模式 (`-m64`),PML4 四级分页 (2MiB 大页),GDT/IDT,抢占式调度器 (PIT 100 Hz)
- **桌面**:标签式 GUI (App Manager / Shell / FMGR / My Computer / Recycle Bin / Linux / 虚拟机 / 任务管理器),深色主题,平滑 AA 字体 (4-bit alpha,自 Terminus 8x16 生成)
- **显卡**:VMware SVGA (32bpp)、VESA VBE (8bpp)、mode 13h 回退;自适应分辨率 (640x480 ~ 1280x768)
- **驱动**:ATA PIO 磁盘、PS/2 键盘鼠标、RTC、rtl8139 网卡、iso9660 光驱 (`cdrom` 命令)、gzip (inflate)
- **网络**:完整 TCP/IP 协议栈 (ARP/DHCP/ICMP/DNS/UDP/TCP) + `ping/fetch/dhcp` 命令
- **安装器**:`kresten-x86-install.iso` 自动写 512B MBR + 内核镜像到第一块硬盘后重启
- **注册表**:key/value 注册表 (registry),持久化到内存映射区
- **VM 插件**:真 Intel VT-x hypervisor (VMXON/VMCS/EPT/VMLAUNCH),访客 OS 为 32 位保护模式 (`vm_guest.asm`);无 VT-x (QEMU TCG) 时优雅降级
- **Linux 子系统 (LX)**:x86-64 ELF64 busybox (WSL1 同构,LSTAR syscall 翻译),交互式 ash;`/mnt/cdrom` 经 iso9660 挂载
- **可加载插件 (.kplg)**:内核内置迷你动态链接器 (`kernel/kplg.c`),插件编译为 ELF64 共享对象 (`-fPIC -shared`),开机从 ISO `/plugins/` 自动加载 (runtime/linux/vm/shell/desktop/hello/taskmgr,按源码归属分存 `/plugins/{kernel,runtime,user}/`),符号解析:插件自身 → 内核导出表 (`.ksymtab`,318 个符号) → 已加载模块;无插件结构的最小模块 (如 `linux.kplg` 只导出 `lx_guest`) 也可作为依赖加载。加载区 0x04000000..0x05000000 (避开 0x01A00000..0x03200000 的 rootfs 解包缓冲)。shell 内 `plugins` 列出已加载插件,`plugins load <name>` 手动加载
- **可加载驱动 (.kdrv)**:外设驱动 (gfx/kbd/mouse/rtc/ps2packet/rootfs) 编译为 ELF64 共享对象,开机先于插件加载;字体独立成模块 (`fonts/`,含 1-bit 与 AA 字模)
- **任务管理器**:`taskmgr` (控制台进程表) / `taskmgr --graphical` (自绘 AA 图形进程查看器,ESC 返回桌面);开机自动加载,桌面「Task Manager」标签页 Enter/点击打开;内核导出 `proc_get_count()/proc_get_all()`
- **Wine 运行时**:`vm linux wine <prog>` 经 LX syscall 翻译直接运行真实 Wine (wine loader + ld.so 动态链接,libc/wine 的 .so 从介质读出后由 ld.so 装载);wine-tiny.iso 最小介质 (卷标 WINETINY,`/WINE` + `/LIB`),`winefs.c` 挂载 + `wine_iso_map.h` 路径映射;介质生成 `scripts/gen_tiny_iso.sh`
- **崩溃守护 (Crash Guardian)**:异常处理 blob + 看门狗,异常/超时输出寄存器现场报告后停机;启动完成自毁并归还内存

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
| `scripts/gen_kernel_symtab.py` + `kernel_symtab_stub.c` | 本目录 | 两遍链接:第一遍 nm 收集内核导出符号 → 生成 `.ksymtab` 表 (供 .kplg 动态解析) |

## 测试

`tests/`:`x86_svga_test.py` / `x86_vbe_test3.py` / `x86_install_test.py` / `tests/linux/` (LX guest ELF 生成)。

## 文档

- `docs/LINUX-SUBSYSTEM.md` — LX 子系统设计文档 (WSL1 对照)