/* Kresten VM plugin: Intel VT-x (VMX) hardware virtualization.
 *
 * Runs a small 32-bit guest OS (vm_guest.h) in a real virtual machine:
 *   - EPT: guest RAM is private (4MB in vm_ram), the VGA text window
 *     0xB8000 is aliased to the host framebuffer so the guest draws
 *     directly on screen (VMware-style shared display).
 *   - Guest hardware: PS/2 keyboard (host scancode buffers), VGA cursor
 *     ports (passthrough), port 0xF4 write = "stop VM" request.
 *   - CPUID exits are reflected to the host CPU.
 *
 * Invoke with `vm` from the shell or the desktop "VM" app.
 */

#include <plugin.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <io.h>
#include <keyboard.h>
#include "vm_guest.h"
#include <vm_guestif.h>

/* ---------- MSRs ---------- */
#define MSR_VMX_BASIC           0x480
#define MSR_VMX_PINBASED        0x481
#define MSR_VMX_PROCBASED       0x482
#define MSR_VMX_EXIT_CTLS       0x483
#define MSR_VMX_ENTRY_CTLS      0x484
#define MSR_VMX_CR0_FIXED0      0x486
#define MSR_VMX_CR0_FIXED1      0x487
#define MSR_VMX_CR4_FIXED0      0x488
#define MSR_VMX_CR4_FIXED1      0x489
#define MSR_VMX_PROCBASED2      0x48B
#define MSR_VMX_EPT_VPID        0x48C
#define MSR_VMX_TRUE_PINBASED   0x48D
#define MSR_VMX_TRUE_PROCBASED  0x48E
#define MSR_VMX_TRUE_EXIT_CTLS  0x48F
#define MSR_VMX_TRUE_ENTRY_CTLS 0x490
#define MSR_EFER                0xC0000080
#define MSR_FS_BASE             0xC0000100
#define MSR_GS_BASE             0xC0000101

/* ---------- VMCS field encodings (SDM Appendix B) ---------- */
#define VMC_VMX_ERR             0x4400  /* VM-instruction error */
#define VMC_EXIT_REASON         0x4402
#define VMC_EXIT_QUAL           0x6400
#define VMC_GUEST_PHYS          0x2400
#define VMC_ENTRY_INTR          0x4016
#define VMC_ENTRY_ERRCODE       0x4018
#define VMC_ENTRY_INSTLEN       0x401A
#define VMC_LINK_PTR            0x2800
#define VMC_PIN                 0x4000
#define VMC_PRIMARY             0x4002
#define VMC_EXC_BITMAP          0x4004
#define VMC_EXIT_CTLS           0x400C
#define VMC_ENTRY_CTLS          0x4012
#define VMC_SECONDARY           0x401E
#define VMC_CR3_TARGET_COUNT    0x400A
#define VMC_EXIT_MSR_STORE      0x400E
#define VMC_EXIT_MSR_LOAD       0x4010
#define VMC_ENTRY_MSR_LOAD      0x4014
#define VMC_EPT_PTR             0x201A

/* guest-state */
#define VG_ES 0x0800
#define VG_CS 0x0802
#define VG_SS 0x0804
#define VG_DS 0x0806
#define VG_FS 0x0808
#define VG_GS 0x080A
#define VG_LDTR 0x080C
#define VG_TR 0x080E

#define VG_CR0 0x6800
#define VG_CR3 0x6802
#define VG_CR4 0x6804
#define VG_ES_B 0x6806
#define VG_CS_B 0x6808
#define VG_SS_B 0x680A
#define VG_DS_B 0x680C
#define VG_FS_B 0x680E
#define VG_GS_B 0x6810
#define VG_LDTR_B 0x6812
#define VG_TR_B 0x6814
#define VG_GDTR_B 0x6816
#define VG_IDTR_B 0x6818
#define VG_DR7 0x681A
#define VG_RSP 0x681C
#define VG_RIP 0x681E
#define VG_RFLAGS 0x6820
#define VG_SYSENTER_ESP 0x6826
#define VG_SYSENTER_EIP 0x6828

#define VG_GDTR_LIMIT 0x480C
#define VG_IDTR_LIMIT 0x480E
#define VG_ES_AR 0x4816
#define VG_CS_AR 0x4818
#define VG_SS_AR 0x481A
#define VG_DS_AR 0x481C
#define VG_FS_AR 0x481E
#define VG_GS_AR 0x4820
#define VG_LDTR_AR 0x4822
#define VG_TR_AR 0x4824
#define VG_SYSENTER_CS 0x482A
#define VG_EFER 0x2806
#define VG_DEBUGCTL 0x2802
#define VG_PAT 0x2804

/* host-state */
#define VH_ES 0x0C00
#define VH_CS 0x0C02
#define VH_SS 0x0C04
#define VH_DS 0x0C06
#define VH_FS 0x0C08
#define VH_GS 0x0C0A
#define VH_TR 0x0C0C
#define VH_CR0 0x6C00
#define VH_CR3 0x6C02
#define VH_CR4 0x6C04
#define VH_FS_B 0x6C06
#define VH_GS_B 0x6C08
#define VH_TR_B 0x6C0A
#define VH_GDTR_B 0x6C0C
#define VH_IDTR_B 0x6C0E
#define VH_RSP 0x6C14
#define VH_RIP 0x6C16
#define VH_GDTR_LIMIT 0x4C00
#define VH_IDTR_LIMIT 0x4C02
#define VH_TR_AR 0x4C04
#define VH_SYSENTER_CS 0x4C06
#define VH_SYSENTER_ESP 0x6C10
#define VH_SYSENTER_EIP 0x6C12
#define VH_EFER 0x2C02
#define VH_PAT 0x2C00

/* ---------- guest memory map ---------- */
#define GUEST_GDTR_PHYS  0x100000u   /* guest GDT + image start */
#define GUEST_PD_PHYS    0x1000u     /* guest page directory (4MB pages) */
#define GUEST_PD_PAE     0x2000u     /* PDEs for the PAE case */
#define GUEST_PDPTE      0x1000u
#define GUEST_ENTRY_RIP  0x100100u   /* image offset 0x100 */
#define GUEST_STACK_RSP  0x200000u
#define VM_EXIT_PORT     0xF4u
#define VGA_WIN          0xB8000u    /* host text framebuffer */

/* ---------- static state (identity mapped: &array == physical) ---------- */
static uint8_t __attribute__((aligned(4096))) vm_ram[0x400000];
static uint8_t __attribute__((aligned(4096))) vmcs_region[4096];
static uint8_t __attribute__((aligned(4096))) vmxon_region[4096];
static uint64_t __attribute__((aligned(4096))) ept_pml4[512];
static uint64_t __attribute__((aligned(4096))) ept_pdpt[512];
static uint64_t __attribute__((aligned(4096))) ept_pd[512];
static uint64_t __attribute__((aligned(4096))) ept_pt0[512];
static uint64_t __attribute__((aligned(4096))) ept_pt1[512];

/* guest register save area, touched by the asm VM loop */
uint64_t vm_s_rax, vm_s_rbx, vm_s_rcx, vm_s_rdx, vm_s_rsi, vm_s_rdi, vm_s_rbp;
uint64_t vm_s_r8, vm_s_r9, vm_s_r10, vm_s_r11, vm_s_r12, vm_s_r13, vm_s_r14, vm_s_r15;

static int vm_exit_go;
static uint64_t vm_exit_count;
static uint64_t vm_io_count;
static char vm_status[96] = "ready";

/* ---------- CPU / MSR helpers ---------- */
static inline uint64_t vm_rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void vm_cpuid(uint32_t leaf, uint32_t sub,
                            uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile ("cpuid"
                      : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                      : "a"(leaf), "c"(sub));
}

static inline void vm_get_cr(uint32_t which, uint64_t *v) {
    if (which == 0)
        __asm__ volatile ("mov %%cr0, %0" : "=r"(*v));
    else if (which == 3)
        __asm__ volatile ("mov %%cr3, %0" : "=r"(*v));
    else
        __asm__ volatile ("mov %%cr4, %0" : "=r"(*v));
}

static inline void vm_set_cr4(uint64_t v) {
    __asm__ volatile ("mov %0, %%cr4" :: "r"(v) : "memory");
}

/* ---------- status queries (used by the desktop VM app) ---------- */
int plugin_vm_available(void) {
    uint32_t a, b, c, d;
    vm_cpuid(1, 0, &a, &b, &c, &d);
    (void)a;
    (void)b;
    (void)d;
    return (c & (1u << 5)) != 0;
}

const char *plugin_vm_status(void) {
    return vm_status;
}

void vm_set_status(const char *s) {
    strncpy(vm_status, s, sizeof(vm_status) - 1);
    vm_status[sizeof(vm_status) - 1] = 0;
}

/* software-emulation guest (vm_soft.c): used when the CPU has no VT-x */
int vm_run_soft(void);

/* ---------- VMCS access ---------- */
static inline uint64_t vmcs_read(uint32_t field) {
    uint64_t v;
    __asm__ volatile ("vmread %1, %0" : "=r"(v) : "r"((uint64_t)field) : "cc");
    return v;
}

static inline void vmcs_write(uint32_t field, uint64_t val) {
    __asm__ volatile ("vmwrite %0, %1"
                      :: "r"(val), "r"((uint64_t)field) : "cc");
}

/* ---------- guest device emulation ---------- */
static uint32_t vm_io_in(uint32_t port, int size) {
    (void)size;
    switch (port) {
    case 0x60: {                     /* PS/2 data: host scancode buffer */
        int sc = keyboard_poll_scancode();
        return sc >= 0 ? (uint32_t)(uint8_t)sc : 0;
    }
    case 0x64:                       /* PS/2 status: output buffer full */
        return keyboard_scancode_pending() ? 1u : 0u;
    case 0x3D5:                      /* VGA cursor data (read: 0) */
        return 0;
    default:
        return 0xFFu;
    }
}

static void vm_io_out(uint32_t port, uint32_t val) {
    switch (port) {
    case 0x3D4:
        outb((unsigned short)0x3D4, (unsigned char)val);
        break;
    case 0x3D5:
        outb((unsigned short)0x3D5, (unsigned char)val);
        break;
    default:
        break;                       /* everything else is ignored */
    }
}

/* ---------- C half of the VM loop (called from asm) ---------- */
void vm_handle_exit(void) {
    uint32_t reason = (uint32_t)vmcs_read(VMC_EXIT_REASON) & 0xFFFFu;
    uint64_t qual = vmcs_read(VMC_EXIT_QUAL);
    vm_exit_count++;
    switch (reason) {
    case 1: {                        /* external interrupt: host ISR, keep guest */
        __asm__ volatile ("sti" ::: "memory");
        vm_exit_go = 1;
        break;
    }
    case 10: {                       /* CPUID: reflect to host */
        uint32_t a, b, c, d;
        vm_cpuid((uint32_t)vm_s_rax, (uint32_t)vm_s_rcx, &a, &b, &c, &d);
        vm_s_rax = a;
        vm_s_rbx = b;
        vm_s_rcx = c;
        vm_s_rdx = d;
        vm_exit_go = 1;
        break;
    }
    case 30: {                       /* IN/OUT */
        vm_io_count++;
        uint32_t port = (uint32_t)(qual >> 16);
        uint32_t size = ((uint32_t)qual & 7u) + 1u;
        uint32_t mask = size == 4 ? 0xFFFFFFFFu
                     : (size == 2 ? 0xFFFFu : 0xFFu);
        if (qual & 8u) {
            uint32_t val = vm_io_in(port, (int)size);
            vm_s_rax = (vm_s_rax & ~mask) | (val & mask);
        } else {
            vm_io_out(port, (uint32_t)(vm_s_rax & mask));
        }
        vm_exit_go = (port != VM_EXIT_PORT);
        break;
    }
    case 48: {                       /* EPT violation */
        uint64_t gpa = vmcs_read(VMC_GUEST_PHYS);
        printf("vm: EPT violation guest-phys=%08x%08x qual=%08x%08x\n",
               (unsigned)(gpa >> 32), (unsigned)gpa,
               (unsigned)(qual >> 32), (unsigned)qual);
        vm_set_status("EPT violation");
        vm_exit_go = 0;
        break;
    }
    case 2:                          /* triple fault */
        printf("vm: guest triple fault\n");
        vm_set_status("guest triple fault");
        vm_exit_go = 0;
        break;
    default:
        printf("vm: unhandled exit reason %u qual=%08x%08x\n", reason,
               (unsigned)(qual >> 32), (unsigned)qual);
        vm_set_status("unhandled VM exit");
        vm_exit_go = 0;
        break;
    }
}

void vm_launch_failed(void) {
    uint32_t err = (uint32_t)vmcs_read(VMC_VMX_ERR);
    printf("vm: VM entry failed (VM-instruction error %u)\n", err);
    char buf[64];
    static const char pre[] = "VM entry failed (error ";
    int i = 0;
    while (pre[i] && i < (int)sizeof(buf) - 8) {
        buf[i] = pre[i];
        i++;
    }
    char digits[12];
    int d = 0;
    unsigned v = err;
    do {
        digits[d++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v && d < 12);
    while (d > 0 && i < (int)sizeof(buf) - 2)
        buf[i++] = digits[--d];
    buf[i++] = ')';
    buf[i] = 0;
    vm_set_status(buf);
    vm_exit_go = 0;
}

/* ---------- the VM entry/exit loop (naked asm) ----------
 * Guest GP registers live in our GP registers between transitions, so we
 * save/restore them to the vm_s_* globals around each handler call. */
__attribute__((naked)) static void vm_run(void) {
    __asm__ volatile (
        "push %rbp\n\t"
        "push %rbx\n\t"
        "push %r12\n\t"
        "push %r13\n\t"
        "push %r14\n\t"
        "push %r15\n\t"
        "sub $8, %rsp\n\t"                 /* 16-byte stack alignment */
        /* host RSP/RIP for the VMCS, taken at launch time */
        "mov %rsp, %rax\n\t"
        "mov $0x6c14, %rcx\n\t"
        "vmwrite %rax, %rcx\n\t"
        "leaq 1f(%rip), %rax\n\t"
        "mov $0x6c16, %rcx\n\t"
        "vmwrite %rax, %rcx\n\t"
        "vmlaunch\n\t"
        "jc 2f\n\t"
        "1:\n\t"                             /* we resume here on VM exit */
        "mov %rax, vm_s_rax(%rip)\n\t"
        "mov %rbx, vm_s_rbx(%rip)\n\t"
        "mov %rcx, vm_s_rcx(%rip)\n\t"
        "mov %rdx, vm_s_rdx(%rip)\n\t"
        "mov %rsi, vm_s_rsi(%rip)\n\t"
        "mov %rdi, vm_s_rdi(%rip)\n\t"
        "mov %rbp, vm_s_rbp(%rip)\n\t"
        "mov %r8, vm_s_r8(%rip)\n\t"
        "mov %r9, vm_s_r9(%rip)\n\t"
        "mov %r10, vm_s_r10(%rip)\n\t"
        "mov %r11, vm_s_r11(%rip)\n\t"
        "mov %r12, vm_s_r12(%rip)\n\t"
        "mov %r13, vm_s_r13(%rip)\n\t"
        "mov %r14, vm_s_r14(%rip)\n\t"
        "mov %r15, vm_s_r15(%rip)\n\t"
        "call vm_handle_exit\n\t"
        "mov vm_exit_go(%rip), %eax\n\t"
        "test %eax, %eax\n\t"
        "jz 3f\n\t"
        "mov vm_s_r15(%rip), %r15\n\t"
        "mov vm_s_r14(%rip), %r14\n\t"
        "mov vm_s_r13(%rip), %r13\n\t"
        "mov vm_s_r12(%rip), %r12\n\t"
        "mov vm_s_r11(%rip), %r11\n\t"
        "mov vm_s_r10(%rip), %r10\n\t"
        "mov vm_s_r9(%rip), %r9\n\t"
        "mov vm_s_r8(%rip), %r8\n\t"
        "mov vm_s_rbp(%rip), %rbp\n\t"
        "mov vm_s_rdi(%rip), %rdi\n\t"
        "mov vm_s_rsi(%rip), %rsi\n\t"
        "mov vm_s_rdx(%rip), %rdx\n\t"
        "mov vm_s_rcx(%rip), %rcx\n\t"
        "mov vm_s_rbx(%rip), %rbx\n\t"
        "mov vm_s_rax(%rip), %rax\n\t"
        "vmresume\n\t"
        "jc 2f\n\t"
        "jmp 1b\n\t"
        "2:\n\t"                             /* launch/resume failed */
        "call vm_launch_failed\n\t"
        "3:\n\t"
        "add $8, %rsp\n\t"
        "pop %r15\n\t"
        "pop %r14\n\t"
        "pop %r13\n\t"
        "pop %r12\n\t"
        "pop %rbx\n\t"
        "pop %rbp\n\t"
        "ret\n\t");
}

/* ---------- guest memory ---------- */
static void vm_setup_guest_mem(int pae) {
    /* guest page tables: identity map 0..4GB with 4MB pages */
    uint64_t *pd;
    if (pae) {
        vm_ram[0x1000] = 0x01;               /* PDPTE[0] -> PD at 0x2000 */
        vm_ram[0x1001] = 0x00;
        vm_ram[0x1002] = 0x02;
        vm_ram[0x1003] = 0x00;
        for (int i = 4; i < 4096; i++) vm_ram[0x1000 + i] = 0;
        pd = (uint64_t *)(vm_ram + 0x2000);
    } else {
        pd = (uint64_t *)(vm_ram + 0x1000);
    }
    for (int i = 0; i < 1024; i++)
        pd[i] = ((uint64_t)i << 22) | 0x83ULL;   /* present|rw|ps */
    for (int i = 1024; i < 4096; i++) pd[i] = 0;
    /* guest image: GDT at 0x100000, code at 0x100100 */
    memcpy(vm_ram + GUEST_GDTR_PHYS, vm_guest_bin, vm_guest_bin_len);
}

/* ---------- EPT (guest phys 0..4MB -> vm_ram, VGA window -> host) ---------- */
static void vm_setup_ept(void) {
    uintptr_t pt0 = (uintptr_t)ept_pt0;
    uintptr_t pt1 = (uintptr_t)ept_pt1;
    ept_pml4[0] = (uintptr_t)ept_pdpt | 0x7ULL;
    ept_pdpt[0] = (uintptr_t)ept_pd | 0x7ULL;
    ept_pd[0] = pt0 | 0x7ULL;
    ept_pd[1] = pt1 | 0x7ULL;
    for (int i = 0; i < 512; i++) {
        ept_pt0[i] = ((uintptr_t)vm_ram + (uintptr_t)i * 4096) | 0x7ULL;
        ept_pt1[i] = ((uintptr_t)vm_ram + 0x200000u + (uintptr_t)i * 4096) | 0x7ULL;
    }
    /* alias guest 0xB8000-0xBFFFF to the host text framebuffer */
    for (int i = 0xB8; i < 0xC0; i++)
        ept_pt0[i] = (VGA_WIN + (uintptr_t)(i - 0xB8) * 4096) | 0x7ULL;
}

/* ---------- control MSR -> VMCS value ---------- */
static uint32_t vm_ctl_value(uint32_t msr_no, uint32_t desired, int *ok) {
    uint64_t m = vm_rdmsr(msr_no);
    uint32_t lo = (uint32_t)m;
    uint32_t hi = (uint32_t)(m >> 32);
    if (desired & ~hi) {
        *ok = 0;
        return 0;
    }
    *ok = 1;
    return (~lo) | desired;
}

/* ---------- host TR state (selector/base/limit/access-rights) ---------- */
static void vm_host_tr(uint16_t *sel, uint64_t *base, uint64_t *limit, uint32_t *ar) {
    *sel = 0;
    *base = 0;
    *limit = 0;
    *ar = 0;
    uint16_t s;
    __asm__ volatile ("str %0" : "=r"(s));
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr;
    __asm__ volatile ("sgdt %0" : "=m"(gdtr));
    if (s == 0 || (s & 4))
        return;
    uint64_t *desc = (uint64_t *)(uintptr_t)(gdtr.base + (s & ~7u));
    uint64_t d1 = desc[0];
    uint64_t d2 = desc[1];
    *sel = s;
    *base = ((d1 >> 16) & 0xFFFFFF) | ((d1 << 32) & 0xFF00000000ULL)
          | (d2 << 32);
    *limit = (d1 & 0xFFFF) | ((d1 >> 32) & 0xF0000ULL);
    *ar = (uint32_t)((d1 >> 40) & 0xFF);
}

/* ---------- full VMCS setup ---------- */
static int vm_setup_vmcs(uint64_t basic) {
    uint64_t cr0, cr3, cr4;
    vm_get_cr(0, &cr0);
    vm_get_cr(3, &cr3);
    vm_get_cr(4, &cr4);
    uint16_t cs_sel, ss_sel, ds_sel;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs_sel));
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss_sel));
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds_sel));
    uint16_t tr_sel;
    uint64_t tr_base, tr_limit;
    uint32_t tr_ar;
    vm_host_tr(&tr_sel, &tr_base, &tr_limit, &tr_ar);

    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr, idtr;
    __asm__ volatile ("sgdt %0" : "=m"(gdtr));
    __asm__ volatile ("sidt %0" : "=m"(idtr));

    uint64_t efer = vm_rdmsr(MSR_EFER);
    uint64_t fs_base = vm_rdmsr(MSR_FS_BASE);
    uint64_t gs_base = vm_rdmsr(MSR_GS_BASE);

    /* fixed bits */
    uint64_t cr0_fixed0 = vm_rdmsr(MSR_VMX_CR0_FIXED0);
    uint64_t cr0_fixed1 = vm_rdmsr(MSR_VMX_CR0_FIXED1);
    uint64_t cr4_fixed0 = vm_rdmsr(MSR_VMX_CR4_FIXED0);
    uint64_t cr4_fixed1 = vm_rdmsr(MSR_VMX_CR4_FIXED1);

    int use_true = (basic >> 55) & 1;
    int ok;
    uint32_t pin = vm_ctl_value(use_true ? MSR_VMX_TRUE_PINBASED : MSR_VMX_PINBASED,
                                1u << 2, &ok);       /* external interrupt exiting */
    if (!ok) return -1;
    uint32_t primary = vm_ctl_value(use_true ? MSR_VMX_TRUE_PROCBASED : MSR_VMX_PROCBASED,
                                    1u << 31, &ok);   /* activate secondary */
    if (!ok) return -1;
    uint32_t secondary = vm_ctl_value(MSR_VMX_PROCBASED2, 1u << 0, &ok);  /* EPT */
    if (!ok) return -1;
    uint32_t exit_ctl = vm_ctl_value(use_true ? MSR_VMX_TRUE_EXIT_CTLS : MSR_VMX_EXIT_CTLS,
                                     1u << 9, &ok);   /* 64-bit host */
    if (!ok) return -1;
    uint32_t entry_ctl = vm_ctl_value(use_true ? MSR_VMX_TRUE_ENTRY_CTLS : MSR_VMX_ENTRY_CTLS,
                                      0, &ok);
    if (!ok) return -1;

    /* guest CR0/CR4: fixed0 bits + paging + PE/ET; sanity vs fixed1 */
    uint64_t guest_cr0 = cr0_fixed0 | 0x11ULL | (1ULL << 31);
    uint64_t guest_cr4 = cr4_fixed0 | 0x10ULL;   /* PSE for 4MB pages */
    int pae = (int)((guest_cr4 >> 5) & 1);
    if ((guest_cr0 & ~cr0_fixed1) || (guest_cr4 & ~cr4_fixed1)) {
        printf("vm: CR0/CR4 fixed-bit conflict (0=%08x%08x f0=%08x%08x)\n",
               (unsigned)(guest_cr0 >> 32), (unsigned)guest_cr0,
               (unsigned)(cr4_fixed0 >> 32), (unsigned)cr4_fixed0);
        return -1;
    }

    /* controls */
    vmcs_write(VMC_PIN, pin);
    vmcs_write(VMC_PRIMARY, primary);
    vmcs_write(VMC_SECONDARY, secondary);
    vmcs_write(VMC_EXIT_CTLS, exit_ctl);
    vmcs_write(VMC_ENTRY_CTLS, entry_ctl);
    vmcs_write(VMC_EXC_BITMAP, 0);
    vmcs_write(VMC_CR3_TARGET_COUNT, 0);
    vmcs_write(VMC_EXIT_MSR_STORE, 0);
    vmcs_write(VMC_EXIT_MSR_LOAD, 0);
    vmcs_write(VMC_ENTRY_MSR_LOAD, 0);
    vmcs_write(VMC_ENTRY_INTR, 0);
    vmcs_write(VMC_ENTRY_ERRCODE, 0);
    vmcs_write(VMC_ENTRY_INSTLEN, 0);
    vmcs_write(VMC_LINK_PTR, ~0ULL);
    vmcs_write(VMC_EPT_PTR, ((uintptr_t)ept_pml4) | 0x1EULL);  /* 4-level walk, WB */

    /* host state */
    vmcs_write(VH_CR0, cr0);
    vmcs_write(VH_CR3, cr3);
    vmcs_write(VH_CR4, cr4);
    vmcs_write(VH_ES, ds_sel);
    vmcs_write(VH_CS, cs_sel);
    vmcs_write(VH_SS, ss_sel);
    vmcs_write(VH_DS, ds_sel);
    vmcs_write(VH_FS, ds_sel);
    vmcs_write(VH_GS, ds_sel);
    vmcs_write(VH_TR, tr_sel);
    vmcs_write(VH_FS_B, fs_base);
    vmcs_write(VH_GS_B, gs_base);
    vmcs_write(VH_TR_B, tr_base);
    vmcs_write(VH_TR_AR, tr_ar);
    vmcs_write(VH_GDTR_B, gdtr.base);
    vmcs_write(VH_IDTR_B, idtr.base);
    vmcs_write(VH_GDTR_LIMIT, gdtr.limit);
    vmcs_write(VH_IDTR_LIMIT, idtr.limit);
    vmcs_write(VH_SYSENTER_CS, 0);
    vmcs_write(VH_SYSENTER_ESP, 0);
    vmcs_write(VH_SYSENTER_EIP, 0);
    vmcs_write(VH_EFER, efer);
    vmcs_write(VH_PAT, 0);

    /* guest state */
    vmcs_write(VG_CR0, guest_cr0);
    vmcs_write(VG_CR3, GUEST_PD_PHYS);
    vmcs_write(VG_CR4, guest_cr4);
    vmcs_write(VG_ES, 0x10);
    vmcs_write(VG_CS, 0x08);
    vmcs_write(VG_SS, 0x10);
    vmcs_write(VG_DS, 0x10);
    vmcs_write(VG_FS, 0x10);
    vmcs_write(VG_GS, 0x10);
    vmcs_write(VG_LDTR, 0);
    vmcs_write(VG_TR, 0);
    vmcs_write(VG_ES_B, 0);
    vmcs_write(VG_CS_B, 0);
    vmcs_write(VG_SS_B, 0);
    vmcs_write(VG_DS_B, 0);
    vmcs_write(VG_FS_B, 0);
    vmcs_write(VG_GS_B, 0);
    vmcs_write(VG_LDTR_B, 0);
    vmcs_write(VG_TR_B, 0);
    vmcs_write(VG_GDTR_B, GUEST_GDTR_PHYS);
    vmcs_write(VG_IDTR_B, 0);
    vmcs_write(VG_DR7, 0x400);
    vmcs_write(VG_RSP, GUEST_STACK_RSP);
    vmcs_write(VG_RIP, GUEST_ENTRY_RIP);
    vmcs_write(VG_RFLAGS, 0x202);    /* IF=1 so external-interrupt exits fire */
    vmcs_write(VG_SYSENTER_ESP, 0);
    vmcs_write(VG_SYSENTER_EIP, 0);
    vmcs_write(VG_GDTR_LIMIT, 0x2F);
    vmcs_write(VG_IDTR_LIMIT, 0);
    vmcs_write(VG_ES_AR, 0x3093);
    vmcs_write(VG_CS_AR, 0x309B);
    vmcs_write(VG_SS_AR, 0x3093);
    vmcs_write(VG_DS_AR, 0x3093);
    vmcs_write(VG_FS_AR, 0x3093);
    vmcs_write(VG_GS_AR, 0x3093);
    vmcs_write(VG_LDTR_AR, 0x82);    /* unusable */
    vmcs_write(VG_TR_AR, 0x83);      /* unusable */
    vmcs_write(VG_SYSENTER_CS, 0);
    vmcs_write(VG_EFER, 0);
    vmcs_write(VG_DEBUGCTL, 0);
    vmcs_write(VG_PAT, 0);

    vm_setup_guest_mem(pae);
    vm_setup_ept();
    return 0;
}

/* ---------- enter / leave VMX ---------- */
static int vm_vmx_active;

static int vm_prepare(void) {
    uint32_t a, b, c, d;
    vm_cpuid(1, 0, &a, &b, &c, &d);
    (void)a;
    (void)b;
    (void)d;
    if (!(c & (1u << 5))) {
        puts("vm: CPU lacks VT-x (CPUID.1:ECX.VMX=0); nothing to do");
        vm_set_status("VT-x not available");
        return -1;
    }
    uint64_t basic = vm_rdmsr(MSR_VMX_BASIC);
    if (!((basic >> 55) & 1)) {
        puts("vm: CPU lacks true VMCS control MSRs; cannot virtualize");
        vm_set_status("no true VMCS control MSRs");
        return -1;
    }
    uint32_t rev = (uint32_t)basic;

    uint64_t cr4;
    vm_get_cr(4, &cr4);
    vm_set_cr4(cr4 | (1ULL << 13));              /* CR4.VMXE */

    memset(vmxon_region, 0, sizeof(vmxon_region));
    memset(vmcs_region, 0, sizeof(vmcs_region));
    *(uint32_t *)vmxon_region = rev;
    *(uint32_t *)vmcs_region = rev;

    __asm__ volatile ("vmxon %0" :: "m"(vmxon_region[0]) : "memory", "cc");
    vm_vmx_active = 1;
    __asm__ volatile ("vmclear %0" :: "m"(vmcs_region[0]) : "memory", "cc");
    __asm__ volatile ("vmptrld %0" :: "m"(vmcs_region[0]) : "memory", "cc");

    if (vm_setup_vmcs(basic) != 0) {
        printf("vm: VMCS setup failed\n");
        vm_set_status("VMCS setup failed");
        return -1;
    }
    printf("vm: VMX active, launching guest...\n");
    return 0;
}

static void vm_teardown(void) {
    if (!vm_vmx_active)
        return;
    vm_vmx_active = 0;
    __asm__ volatile ("vmclear %0" :: "m"(vmcs_region[0]) : "memory", "cc");
    __asm__ volatile ("vmxoff" ::: "cc");
    uint64_t cr4;
    vm_get_cr(4, &cr4);
    vm_set_cr4(cr4 & ~(1ULL << 13));
}

/* ---------- plugin entry points ---------- */
static int vm_run_vm(void) {
    keyboard_flush();
    vm_exit_count = 0;
    vm_io_count = 0;
    vm_vmx_active = 0;
    if (vm_prepare() != 0) {
        vm_teardown();
        return -1;
    }
    vm_exit_go = 1;
    vm_run();
    printf("vm: guest stopped (exits %u, io %u)\n",
           (unsigned)(vm_exit_count & 0xFFFFFFFFu),
           (unsigned)(vm_io_count & 0xFFFFFFFFu));
    vm_set_status("guest stopped");
    vm_teardown();
    printf("vm: VMX off, back to Kresten\n");
    keyboard_flush();
    return 0;
}

/* ---------- vm guest interface (Hyper-V style platform) ---------- */

static const vm_guest_t *vm_guests[VM_GUEST_MAX];
static int vm_guest_count;

int vm_guest_register(const vm_guest_t *g) {
    if (!g || !g->name || vm_guest_count >= VM_GUEST_MAX)
        return -1;
    for (int i = 0; i < vm_guest_count; i++)
        if (strcmp(vm_guests[i]->name, g->name) == 0)
            return 0;               /* already registered */
    vm_guests[vm_guest_count++] = g;
    printf("[VM] guest registered: %s - %s\n", g->name, g->desc);
    if (g->init)
        g->init();
    return 0;
}

const vm_guest_t *vm_guest_find(const char *name) {
    for (int i = 0; i < vm_guest_count; i++)
        if (strcasecmp(vm_guests[i]->name, name) == 0)
            return vm_guests[i];
    return 0;
}

void vm_guest_list(void) {
    puts("available guests:\n");
    for (int i = 0; i < vm_guest_count; i++)
        printf("  %-12s %s\n", vm_guests[i]->name, vm_guests[i]->desc);
    putchar('\n');
}

/* ---- default guest "kresten": hardware VMX when available, otherwise the
 *      software-emulation guest (vm_soft.c) - the session always works. */
static int vm_guest_kresten_run(const char *args) {
    (void)args;
    if (!plugin_vm_available()) {
        vm_run_soft();
        return 0;
    }
    puts("vm: starting virtual machine (guest OS: Kresten VM guest v1.0)");
    vm_run_vm();
    return 0;
}

static const vm_guest_t vm_guest_kresten = {
    "kresten",
    "Kresten VM guest OS (VT-x, fallback: software emulation)",
    0,
    vm_guest_kresten_run
};

/* the Linux subsystem guest is defined in linux.c */
extern const vm_guest_t lx_guest;

int plugin_vm_init(void) {
    uint32_t a, b, c, d;
    vm_cpuid(1, 0, &a, &b, &c, &d);
    (void)a;
    (void)b;
    (void)d;
    printf("[VM] plugin loaded: VT-x %s, guest image %u bytes\n",
           (c & (1u << 5)) ? "available" : "NOT available on this CPU",
           (unsigned)vm_guest_bin_len);
    vm_set_status((c & (1u << 5)) ? "ready" : "VT-x not available");
    vm_guest_register(&vm_guest_kresten);
    vm_guest_register(&lx_guest);
    return 0;
}

int plugin_vm_cmd(char *args) {
    if (!args || !*args) {              /* `vm` = default (kresten) guest */
        vm_guest_kresten_run(NULL);
        return 0;
    }
    char gname[32];
    int n = 0;
    while (args[n] && args[n] != ' ' && n < 31) {
        gname[n] = args[n];
        n++;
    }
    gname[n] = '\0';
    const char *rest = args + n;
    while (*rest == ' ')
        rest++;
    if (strcasecmp(gname, "list") == 0 || strcasecmp(gname, "help") == 0 ||
        strcasecmp(gname, "?") == 0) {
        vm_guest_list();
        return 0;
    }
    const vm_guest_t *g = vm_guest_find(gname);
    if (!g) {
        printf("vm: unknown guest '%s'\n", gname);
        vm_guest_list();
        return 0;
    }
    printf("vm: starting guest '%s'\n", g->name);
    g->run(*rest ? rest : NULL);
    keyboard_flush();               /* keep guest leftovers out of the shell */
    return 0;
}

REGISTER_PLUGIN(vm);
