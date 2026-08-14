# Kresten 更新日志

## v2.0 (2026-08-14) — 三架构里程碑

### 新增架构支持

- **RISC-V (QEMU virt)** 全新架构:RV64 (`rv64gc`/`lp64`/medany),`riscv64-linux-gnu-gcc` 工具链
  - **隔离特权模型**:访客运行于 U-mode (mstatus MPP=U),M-mode 内核 + ecall 自陷翻译,独立 8KB 陷阱栈;访客 glibc 栈 canary 检查完整通过
  - 完整桌面 GUI、Shell、VM 插件 (软件模拟)、Linux 子系统
  - Windows 构建入口 `build_rv64.ps1`

### 新增功能

- **Linux 子系统 (LX) 三架构全覆盖**:WSL1 同构的 syscall 翻译层 (ELF64 加载器 `lx_elf.c`、进程环境 `lx_proc.c`、翻译层 `lx_syscall.c`),内嵌 busybox 静态 ELF
  - x86:IA32_LSTAR 入口,x86-64 busybox
  - ARM:svc 自陷 + undef 模拟 CP15 c13 (TLS),ARM32 busybox
  - RISC-V:ecall 入口,RV64 busybox (1,226,560 字节)
- **交互式 busybox ash**(三架构):行编辑器输入 (ppoll 阻塞等待 + 逐字符回显),`echo hello` / `exit` 全流程
- **VM 插件统一接口** (`vm_guestif.h`):`vm_guest_register` / `vm_guest_find` / `vm_guest_list`,注册访客 `kresten` + `linux`;`vm` / `vm linux` / `vm list` 命令
- **x86 VT-x 硬件虚拟化**:真 VMX hypervisor (VMXON/VMCS/EPT/VMLAUNCH),32 位访客 OS (`vm_guest.asm`);QEMU TCG 无 VT-x 时优雅降级 (软件模拟)
- **iso9660 光驱** (x86):`cdrom` 命令 (info/ls/cat/copy),LX 内 `/mnt/cdrom` 挂载
- **注册表** (x86):key/value 持久化
- **Windows 一键构建脚本**:`build_iso.ps1` (x86 双 ISO)、`build_arm.ps1`、`build_rv64.ps1`(workspace → relay → WSL 构建 → 桌面输出)

### 改进

- **x86 内核升级 64 位**:i386 → x86-64 长模式 (`-m64`,PML4 四级分页,2MiB 大页)
- LX termios2 ioctl 完整实现 (TCGETS2/TCSETS2/TCSETSW2/TCSETSF2,44 字节 termios2 布局)
- 分发包 ISO (ARM/RISC-V):kernel + README + RUN.BAT/RUN.SH 发行介质

### 修复

- **访客 `t1` 被 ecall 返回路径破坏** (RISC-V `boot/start.S`):t1 复用加载 mepc 后未还原访客 t1,glibc ioctl 包装器的 canary 检查读到代码字节 → `__stack_chk_fail` 中止;改为 `csrw mepc` 之后才还原 t1
- **`lx_sc_ppoll` 不阻塞**:tsp==NULL (无限超时) 时立即返回 0,busybox `read_key` 误判超时 → ash 启动即退出;改为无限阻塞等待键盘输入 (有界超时 + POLLNVAL 支持)
- **x86 安装版链接失败**:安装镜像 (5.25MB) 置于 `.rodata` 把 `.text` 撑至 0x757f00 与 `.bss` (0x602000) 重叠;镜像移入独立 `.install_img` 段 (0xE04000)
- LX 调试追踪读 `*(s0-40)` 未加保护,访客 s0 为非指针值时内核触发 load fault;增加地址范围守卫

## v1.0

- **x86**:i386 multiboot 内核 (1MB 链接),GDT/IDT,抢占调度器 (PIT 100 Hz);标签式桌面 GUI (App Manager/Shell/FMGR/My Computer/Recycle Bin);SVGA/VBE/13h 显卡;ATA PIO、PS/2、RTC 驱动;安装器 ISO (MBR 512B + 镜像写硬盘);`make iso` / `make install-iso`
- **ARM**:Versatile PB (armv5te),semihosted 控制台;LCD/键盘/鼠标/RTC/timer 驱动;SMC91C111 + TCP/IP 协议栈 (DHCP/ARP/ICMP/PING/DNS/FETCH);`make iso` 分发介质
- 压缩 CRT 跳转表 (`crt_compressed.ld`)、插件框架 (`.plugins` 段发现)、DOS 风格 Shell
