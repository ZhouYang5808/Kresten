TOP_DIR = $(shell pwd)
BUILD_DIR = $(TOP_DIR)/build
DIST_DIR = $(TOP_DIR)/dist

CC = riscv64-linux-gnu-gcc
CFLAGS = -march=rv64gc -mabi=lp64 -mcmodel=medany -nostdlib -ffreestanding \
	-fno-omit-frame-pointer -Wall -Werror -Wno-unused-function -Wno-unused-variable \
	-I$(TOP_DIR)/include
LDFLAGS = -T link.ld -mcmodel=medany -no-pie

BOOT_SRC = $(TOP_DIR)/boot/start.S
GUARD_SRC = $(TOP_DIR)/kernel/guard_asm.s
KERNEL_SRC = $(wildcard $(TOP_DIR)/kernel/*.c)
LIBC_SRC = $(wildcard $(TOP_DIR)/kernel/libc/*.c)
CRT_TABLE_LD = $(TOP_DIR)/crt_compressed.ld

# ---- 驱动分层: 启动子系统必需(timer/keyboard/fs)静态编译进内核;
#      外围驱动(gfx/kbd/lcd/mouse/rtc)编译为 .kdrv,
#      字体资源 fonts/font.c 编译为 font.kdrv。
#      启动时由 kernel/plugin.c 的 drv_load_all() 加载并注册。
#      (RISC-V virt 无 PS/2: 无 ps2packet 模块) ----
KEEP_DRIVERS  = driver fs timer keyboard
DRIVER_MODS   = gfx kbd lcd mouse rtc
KEEP_DRIVER_SRCS = $(foreach d,$(KEEP_DRIVERS),$(TOP_DIR)/drivers/$(d).c)
DRIVER_SRCS   = $(KEEP_DRIVER_SRCS)

BOOT_OBJ = $(BUILD_DIR)/boot/start.o
GUARD_OBJ = $(BUILD_DIR)/kernel/guard_asm.o
KERNEL_OBJS = $(patsubst $(TOP_DIR)/kernel/%.c,$(BUILD_DIR)/kernel/%.o,$(KERNEL_SRC))
LIBC_OBJS = $(patsubst $(TOP_DIR)/kernel/libc/%.c,$(BUILD_DIR)/kernel/libc/%.o,$(LIBC_SRC))
DRIVER_OBJS = $(patsubst $(TOP_DIR)/drivers/%.c,$(BUILD_DIR)/drivers/%.o,$(DRIVER_SRCS))

OBJS = $(BOOT_OBJ) $(GUARD_OBJ) $(KERNEL_OBJS) $(DRIVER_OBJS) $(LIBC_OBJS)
TARGET = $(DIST_DIR)/kernel.elf

ISO_TARGET = $(DIST_DIR)/kresten-riscv.iso

# ---- 可加载插件 (.kplg = ELF64 ET_DYN, RISC-V) ----
PLUGIN_CFLAGS = -march=rv64gc -mabi=lp64 -mcmodel=medany -nostdlib -ffreestanding \
                -fPIC -fno-stack-protector -fno-omit-frame-pointer \
                -Wall -Werror -Wno-unused-function -Wno-unused-variable \
                -I$(TOP_DIR)/include -I$(TOP_DIR)/fonts

PLUGINS = crt_bind linux vm shell hello desktop

crt_bind_SRC = $(TOP_DIR)/plugins/kernel/crt_bind.c
linux_SRC   = $(TOP_DIR)/plugins/runtime/linux.c $(TOP_DIR)/plugins/runtime/lx_elf.c \
              $(TOP_DIR)/plugins/runtime/lx_proc.c $(TOP_DIR)/plugins/runtime/lx_syscall.c
vm_SRC      = $(TOP_DIR)/plugins/kernel/vm.c $(TOP_DIR)/plugins/kernel/vm_soft.c
shell_SRC   = $(TOP_DIR)/plugins/user/shell.c
hello_SRC   = $(TOP_DIR)/plugins/user/hello.c
desktop_SRC = $(TOP_DIR)/plugins/user/desktop.c

PLUGIN_OBJS = $(foreach p,$(PLUGINS),$(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/plg/%.o,$($(p)_SRC)))

# ---- 可加载驱动 (.kdrv = ELF64 ET_DYN, 同 .kplg 机制, 每个驱动模块导出 kdrv_driver) ----
DRV_TARGETS  = $(foreach d,$(DRIVER_MODS),$(DIST_DIR)/drivers/$(d).kdrv)
FONT_MODS    = font
FONT_TARGETS = $(foreach f,$(FONT_MODS),$(DIST_DIR)/fonts/$(f).kdrv)

# ---- 内核导出符号表 (两遍链接: kernel.elf.1st -> nm -> kernel_symtab.c) ----
SYMTAB_C = $(BUILD_DIR)/kernel_symtab.c
SYMTAB_O = $(BUILD_DIR)/kernel_symtab.o
FIRST_ELF = $(BUILD_DIR)/kernel.elf.1st
KSYMTAB_STUB_O = $(BUILD_DIR)/kernel_symtab_stub.o

# ---- 嵌入式 .kplg/.kdrv 表 (RISC-V 无光驱, 插件+驱动+字体从内存表加载) ----
KPLG_EMBED_C = $(BUILD_DIR)/kplg_embed.c
KPLG_EMBED_O = $(BUILD_DIR)/kplg_embed.o

PLUGIN_TARGETS = $(foreach p,$(PLUGINS),$(DIST_DIR)/plugins/$(p).kplg)

all: $(TARGET)

plugins: $(PLUGIN_TARGETS)

$(KPLG_EMBED_C): $(PLUGIN_TARGETS) $(DRV_TARGETS) $(FONT_TARGETS) $(TOP_DIR)/scripts/pack_kplg_embed.py
	python3 $(TOP_DIR)/scripts/pack_kplg_embed.py --section .lx_img $(DIST_DIR)/plugins $(DIST_DIR)/drivers $(DIST_DIR)/fonts $@

$(KPLG_EMBED_O): $(KPLG_EMBED_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(TOP_DIR)/kernel -c $< -o $@

$(BOOT_OBJ): $(BOOT_SRC)
	@mkdir -p $(BUILD_DIR)/boot
	$(CC) $(CFLAGS) -c $< -o $@

$(GUARD_OBJ): $(GUARD_SRC)
	@mkdir -p $(BUILD_DIR)/kernel
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: $(TOP_DIR)/kernel/%.c
	@mkdir -p $(BUILD_DIR)/kernel
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/libc/%.o: $(TOP_DIR)/kernel/libc/%.c
	@mkdir -p $(BUILD_DIR)/kernel/libc
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/drivers/%.o: $(TOP_DIR)/drivers/%.c
	@mkdir -p $(BUILD_DIR)/drivers
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/drv/%.o: $(TOP_DIR)/drivers/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUGIN_CFLAGS) -c $< -o $@

$(DIST_DIR)/drivers/%.kdrv: $(BUILD_DIR)/drv/%.o
	@mkdir -p $(DIST_DIR)/drivers
	$(CC) $(PLUGIN_CFLAGS) -shared -nostdlib -o $@ $<

# ---- 字体模块 ----
$(BUILD_DIR)/fnt/%.o: $(TOP_DIR)/fonts/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUGIN_CFLAGS) -c $< -o $@

$(DIST_DIR)/fonts/%.kdrv: $(BUILD_DIR)/fnt/%.o
	@mkdir -p $(DIST_DIR)/fonts
	$(CC) $(PLUGIN_CFLAGS) -shared -nostdlib -o $@ $<

$(BUILD_DIR)/plg/%.o: $(TOP_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUGIN_CFLAGS) -c $< -o $@

$(DIST_DIR)/plugins/%.kplg: $(PLUGIN_OBJS)
	@mkdir -p $(DIST_DIR)/plugins
	$(CC) $(PLUGIN_CFLAGS) -shared -nostdlib -o $@ \
		$(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/plg/%.o,$($(basename $(notdir $@))_SRC))

$(CRT_TABLE_LD): $(TOP_DIR)/scripts/gen_crt_table.py
	python3 $< riscv > $@

$(FIRST_ELF): $(OBJS) $(KSYMTAB_STUB_O) $(KPLG_EMBED_O) $(CRT_TABLE_LD)
	@mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(KSYMTAB_STUB_O) $(KPLG_EMBED_O) -lgcc -o $@

$(KSYMTAB_STUB_O): $(TOP_DIR)/scripts/kernel_symtab_stub.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(SYMTAB_C): $(FIRST_ELF) $(TOP_DIR)/scripts/gen_kernel_symtab.py
	python3 $(TOP_DIR)/scripts/gen_kernel_symtab.py $(FIRST_ELF) $@

$(SYMTAB_O): $(SYMTAB_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(SYMTAB_O) $(KPLG_EMBED_O) $(CRT_TABLE_LD)
	@mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(SYMTAB_O) $(KPLG_EMBED_O) -lgcc -o $@

run: $(TARGET)
	qemu-system-riscv64 -machine virt -bios none -kernel $(TARGET) -nographic -serial mon:stdio -no-reboot

iso: $(TARGET) $(PLUGIN_TARGETS) $(DRV_TARGETS) $(FONT_TARGETS)
	bash $(TOP_DIR)/scripts/make_iso.sh

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(CRT_TABLE_LD)

.PHONY: all run clean iso plugins