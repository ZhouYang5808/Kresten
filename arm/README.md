# Kresten ARM (v3.1)

ARM (Versatile PB) 原生版本:与 RISC-V 版共享桌面 GUI 与 Linux 子系统,带完整网络协议栈 (SMC91C111)。

## 功能

- **内核**:ARM32 (`arm-none-eabi`,armv5te),semihosted 控制台,抢占式调度器 (CLINT 100 Hz)
- **桌面**:标签式 GUI (App Manager / Shell / FMGR / My Computer / Recycle Bin)
- **驱动**:PL110 LCD 帧缓冲 (lcd)、PL050 键盘 (kbd)、PS/2 鼠标、RTC、timer
- **网络**:SMC91C111 + 完整 TCP/IP 协议栈 (DHCP/ARP/ICMP/PING/DNS/HTTP-FETCH)
- **VM 插件**:软件模拟模式 (`vm_soft.c`,ARM 无硬件虚拟化);`vm` / `vm linux` / `vm list`
- **Linux 子系统 (LX)**:ARM32 ELF busybox,svc 自陷翻译 + undef 模拟 CP15 c13 (TLS),交互式 ash
- **Shell**:DOS 风格命令 (help 查看),含 `netstat/ping/fetch/nslookup/dhcp` 网络命令

## 构建 (Windows)

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Users\Lenovo\Documents\New OpenCode Project\build_arm.ps1"
```

产出到桌面:分发版 `kresten-arm.iso` (boot/kernel.elf + README + RUN 脚本)。Versatile PB 经 `-kernel` 启动,ISO 为发行介质。

## 运行 (QEMU)

```sh
qemu-system-arm -machine versatilepb -kernel dist/kernel.elf \
  -nographic -serial mon:stdio -no-reboot -semihosting \
  -net nic,model=smc91c111 -net user
```

## 脚本位置

| 脚本 | 位置 | 作用 |
|---|---|---|
| `build_arm.ps1` | 仓库根 | 同步 + make + ISO → 桌面 |
| `scripts/make_iso.sh` | 本目录 | 分发版 ISO (kernel + README + RUN.BAT/RUN.SH) |
| `scripts/gen_crt_table.py` | 本目录 | 生成 `crt_compressed.ld` (压缩 CRT 跳转表) |

## 构建 (Linux/WSL)

```sh
cd arm && make && make iso   # dist/kernel.elf + dist/kresten-arm.iso
```