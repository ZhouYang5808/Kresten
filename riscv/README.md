# Kresten RISC-V (v2.0)

RISC-V (QEMU virt) 移植版:U-mode 访客 + M-mode 内核的隔离模型,Linux 子系统实现最新 (完整 termios2 + 交互式 ash)。

## 功能

- **内核**:RV64 (`riscv64-linux-gnu-gcc`,rv64gc/lp64,medany),U-mode 访客 + M-mode 内核,ecall 自陷翻译,抢占式调度器 (CLINT 100 Hz)
- **特权模型**:访客运行于 U-mode (mstatus MPP=U),自带 8KB 陷阱栈,访客 glibc 栈 canary 检查完整通过
- **桌面**:与 x86 相同的标签式 GUI (App Manager / Shell / FMGR / My Computer / Recycle Bin)
- **驱动**:LCD 帧缓冲 (lcd)、键盘 (kbd)、PS/2 鼠标、RTC、CLINT timer;网络为硬件 stub (net 命令报 "no NIC")
- **VM 插件**:软件模拟模式 (`vm_soft.c`,RISC-V 无硬件虚拟化);`vm` / `vm linux` / `vm list`
- **Linux 子系统 (LX)**:RV64 ELF busybox (1,226,560 字节),ecall 翻译,交互式 busybox ash:
  - 行编辑器 (ppoll 阻塞等待输入)、termios2 ioctl (TCGETS2/TCSETS2 等)、`sh: can't access tty` 为预期行为
  - `vm linux` 进入:`echo hello` / `exit` 全流程可用 (smoke 测试通过)

## 构建 (Windows)

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Users\Lenovo\Documents\New OpenCode Project\build_rv64.ps1"
```

产出:桌面 `kresten-riscv.iso` (分发版)+ `Kresten\riscv\dist\kernel.elf`。virt 机器经 `-kernel` 启动,ISO 为发行介质。

## 运行 (QEMU)

```sh
qemu-system-riscv64 -machine virt -cpu max -bios none -kernel dist/kernel.elf \
  -nographic -serial mon:stdio -no-reboot
# 桌面: Enter Enter -> shell -> vm linux -> busybox sh
```

## 脚本位置

| 脚本 | 位置 | 作用 |
|---|---|---|
| `build_rv64.ps1` | 仓库根 | 同步 + make + ISO → 桌面 + dist/kernel.elf |
| `scripts/make_iso.sh` | 本目录 | 分发版 ISO (kernel + README + RUN.BAT/RUN.SH) |
| `scripts/gen_crt_table.py` | 本目录 | 生成 `crt_compressed.ld` (压缩 CRT 跳转表) |

## 构建 (Linux/WSL)

```sh
cd riscv && make && make iso   # dist/kernel.elf + dist/kresten-riscv.iso
```

## 冒烟测试

`rv64_smoke.sh` (在 `C:\Users\Lenovo\AppData\Local\Temp\opencode\`):启动 → 桌面 → shell → `vm linux` → busybox sh → `echo hello` → `exit` → 回桌面。日志 `/tmp/lx_rv64_smoke.log`。