# Kresten Linux 子系统（LX）— 设计文档

> 参考 WSL1（Windows NT 内核 + LXSS/LXCORE 子系统）的架构，为 Kresten
> 设计一套**不修改内核**的 Linux 二进制兼容子系统。全部代码为新增插件，
> 经 `Makefile` 的 `plugins/*.c` 通配自动编译进内核镜像（零构建脚本改动）。

## 1. 架构映射（WSL1 → LX）

| WSL1 (Windows)              | Kresten LX                                 |
|-----------------------------|--------------------------------------------|
| NT 内核（API/驱动层）        | Kresten 内核（进程/调度/fs/net/gfx）        |
| LXSS 管理服务（lxss.sys）    | `plugins/linux.c`（子系统生命周期）          |
| LXCORE 内核服务（lxcore.sys）| `plugins/lx_syscall.c`（syscall 翻译层）    |
| ELF 加载（LxcFullImage）     | `plugins/lx_elf.c`（ELF64 加载器）          |
| Linux 进程（PicoProcess）    | `plugins/lx_proc.c`（LX 进程运行环境）       |
| syscall 指令入口（LSTAR MSR）| 同：插件安装 IA32_LSTAR，翻译入口在代码区    |
| DrvFs 卷映射                 | （里程碑 2+）fs 层路径映射                   |

关键点：**宿主内核零改动**。LX 通过 CPU 自身的 MSR 挂载点（IA32_LSTAR +
EFER.SCE）把 Linux ABI 的 `syscall` 指令翻译为 Kresten 内核服务，与 WSL1
把 Linux syscall 翻译为 NT 服务同构。

## 2. 为什么是可行的（与 Chromium 移植的对比）

Chromium 全量移植需要 200MB+ 内存、完整 POSIX、C++17 stdlib、Skia/ANGLE、
TLS 1.3 —— 在 64MB 裸机内核上不可行。LX 只做 **ABI 翻译层**：

- 静态链接的 Linux x86-64 ELF（无 libc / musl 静态）可直接运行
- 所需 syscall 面（~25 个）映射到既有内核服务（putchar/fs/proc）
- 程序运行在 ring0 平铺内存（与内核同特权级，WSL1 早期同构），
  分页为全 4GB identity 2MiB 大页（boot.s 已建），无虚拟内存隔离

## 3. 代码区性能路径（插件热路径直挂）

- 插件为**静态链接**，代码、数据、翻译栈都在内核镜像（代码区）内，
  LSTAR 直接指向 `linux_syscall_entry`（汇编裸函数），零间接分发
- syscall 分发用 `switch`（编译器生成跳转表），最热路径（write/exit）
  单独内联分支
- 翻译临界区 `cli` 保护防嵌套；翻译完成 `iretq` 恢复调用者 IF 位
- 内存池为高内存物理线性分配（页粒度），不经过内核 64KB malloc 堆

## 4. 内存布局（identity 映射，物理=虚拟）

```
0x0040_0000   Linux 程序镜像区（ELF PT_LOAD 段按 vaddr 直载）
0x0100_0000   brk 池（16MB 连续，brk 语义）
0x0140_0000   mmap 池（页分配器，MAP_ANONYMOUS）
0x0180_0000   LX 进程主栈（256KB）
```

均落在内核镜像（_end ≈ 0x15_xxxx）之上、VBE LFB（0xE000_0000）之下，
与内核 64KB 静态堆无交集。

## 5. syscall 翻译表（v0，x86-64 调用号）

| 号 | 名字            | 实现                                          |
|----|-----------------|-----------------------------------------------|
| 0  | read            | 串口/键盘 → 简单读取（v0 返回 EOF）            |
| 1  | write           | putchar（VGA+串口），fd 1/2                    |
| 9  | mmap            | 页池分配（匿名）                              |
| 11 | munmap          | 释放页池                                      |
| 12 | brk             | brk 池                                         |
| 25 | mprotect        | no-op                                         |
| 39 | getpid          | LX 进程 PID                                   |
| 60 | exit            | proc_exit                                     |
| 63 | uname           | "Kresten"                                     |
| 96 | gettimeofday    | RTC → epoch                                   |
| 158| arch_prctl      | ARCH_SET_FS → WRMSR IA32_FS_BASE（TLS）       |
| 186| gettid          | = PID                                         |
| 201| time            | RTC → epoch                                   |
| 202| futex           | no-op（v0 无并发）                            |
| 218| set_tid_address | 返回 PID                                      |
| 228| clock_gettime   | RTC → timespec                                |
| 231| exit_group      | proc_exit                                     |

其余 → -ENOSYS（可观测扩展）。

## 6. 程序加载与启动

1. `lx <name>` → 插件命令（shell 未知命令经 plugin_dispatch 自动路由）
2. 程序源：v0 为嵌入内核镜像的字节数组（`lx_tests.h`，避开 fs 文本
   API 的 `strlen` 截断限制；里程碑 2+ 改为 fs 二进制读取）
3. 校验 ELF64：magic、ET_EXEC、e_machine=62（x86-64）
4. PT_LOAD 段 → `memcpy((void*)vaddr, ...)` 直载 + bss 清零
5. 栈帧：`[argc][argv0][0][envp0][0][auxv AT_NULL]`（Linux ABI 标准）
6. 蹦床：切 LX 大栈 → 清寄存器 → `jmp entry`
7. 进程经 `proc_create` 注册（ps 可见、yield 调度），exit_group 时
   `proc_exit` 返回桌面/其他进程

## 7. 里程碑

- **M1（本次交付）**：翻译层 + ELF64 加载 + 无 libc 测试程序
  （write/exit_group）跑通
- **M2**：musl 静态 hello world（补齐 sigaction/fs 相关 ~10 syscall）、
  fs 二进制加载（ELF 走 ATA/RAM 盘）
- **M3**：busybox 子集 / HTTP 客户端类程序（复用 net_tcp）
- **M4（远期）**：每进程 syscall 状态隔离、页保护位（利用现有 2MiB
  页表改属性）、信号/多线程
