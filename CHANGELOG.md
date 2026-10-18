# Kresten 更新日志

## v3.1 (2026-10-18) — 全动态插件架构与 Windows 程序运行时

### 新增功能

- **全动态插件架构 (.kplg)** (x86/ARM/RISC-V):插件不再静态链接,全部编译为 ELF64 共享对象,启动时由内核迷你动态链接器 (`kernel/kplg.c`) 从 ISO `/plugins/{kernel,runtime,user}` 或内嵌表加载
  - 双遍链接生成内核符号导出表 `.ksymtab` (~318 符号);解析顺序:自身 dynsym → 内核导出表 → 已加载模块
  - 内嵌插件表 (kplg_embed):安装器/硬盘启动无插件 ISO 时自动回退加载
  - Shell `plugins` / `plugins load <name>` 命令管理插件;ARM (SHT_REL/SHT_RELA) 与 RISC-V (RELA) 同步实现
- **动态外设驱动 (.kdrv)** (x86):外设驱动 (gfx/kbd/mouse/rtc/ps2packet/rootfs 等) 编译为可加载模块,启动时先于插件按依赖顺序加载;字体数据独立成模块 (`fonts/`,含 1-bit 点阵与 4-bit AA 两套字模)
- **Wine / Windows 程序运行时** (x86):`vm linux wine <prog>` 在 LX 子系统内运行 Windows 程序
  - 真实 Wine 直接跑在 LX 上:wine loader (ET_DYN) + ld.so 动态链接,libc/wine 的 .so 全部经 LX syscall (open/mmap/read) 从介质读取后由 ld.so 装载
  - wine-tiny.iso 最小运行介质 (卷标 WINETINY,`/WINE/*.SO` + `/LIB/*.SO` + PE dll 闭包),静态驱动 `winefs.c` 挂载,`wine_iso_map.h` 真实路径→ISO 8.3 映射
  - 介质生成脚本 `scripts/gen_tiny_iso.sh` + 测试 PE `scripts/gen_hello_pe.py`
- **任务管理器** (x86):图形化进程查看器 (自绘 AA 界面,实时进程表:PID/名称/状态)
  - 控制台 `taskmgr`、图形 `taskmgr --graphical`;桌面「Task Manager」标签页 (Enter/点击打开,ESC 返回)
  - 内核新增 `proc_get_count()`/`proc_get_all()` 进程表导出
- **平滑 (抗锯齿) 桌面字体** (三架构):4-bit alpha 字模取代 1-bit 点阵,边缘按 alpha 与背景混色 + 调色板最近邻查表 (5-5-5 LUT)
- **崩溃守护 Crash Guardian** (三架构):异常处理 blob 复制到保留内存 (64 KB),看门狗超时/异常时输出完整现场报告 (原因/寄存器/堆栈) 后停机;启动完成后自毁并把内存归还内核堆

### 改进

- **C++ 支持**:C++ 运行时 (`libc/cpp_runtime.cpp`) + 交叉编译器包装,内核与插件可用 C++ 构建
- 桌面 App Manager 应用一览扩展:Shell / 文件管理器 / 我的电脑 / 回收站 / Linux / 虚拟机 / 任务管理器
- **SDK 同步 v3.1**:`SDK/` 与 `SDK-Windows/` 的 sysroot 头文件/静态库、示例 (`hello.kplg`、`my_driver.kdrv`) 与源码快照 (`SDK-Windows/src/`) 全部刷新到 v3.1 (含任务管理器);修复被截断的插件模板 `templates/plugin/plugin.c`;重打包 `SDK-Windows.tar.gz`

### 修复

- **x86 安装器 ISO 缺内核**:动态化重构期间 Makefile 丢失 `kernel-install` 与安装镜像内嵌规则,安装器 ISO 未携带内核;已恢复 (KRSTENBR 镜像,`.install_img` 段 0xE04000),安装到硬盘与硬盘启动重新可用
- **任务管理器桌面入口**:插件未在自动加载列表、桌面键位分发表未接通 Enter;已修复 (自动加载 + Enter/点击启动)
- **build_iso.ps1 构建失败**:脚本尾部误调用仓库根目录遗留的旧版安装脚本 (MyOS 时代),且从错误目录执行;已改为 WSL 构建树直接复制双 ISO 到桌面

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
