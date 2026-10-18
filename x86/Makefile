TOP_DIR = $(shell pwd)
BUILD_DIR = $(TOP_DIR)/build
DIST_DIR = $(TOP_DIR)/dist

CC = gcc
CXX = g++
CXXFLAGS = -std=c++23 -fno-exceptions -fno-rtti -m64 -nostdlib -nostdinc++ -ffreestanding -fno-pie -mno-red-zone -Wall -Werror -DX86_ARCH \
           -I$(TOP_DIR)/include -I$(TOP_DIR)/kernel/drivers
AS = as
LD = ld
CFLAGS = -m64 -nostdlib -ffreestanding -fno-pie -mno-red-zone -Wall -Werror -DX86_ARCH \
         -I$(TOP_DIR)/include -I$(TOP_DIR)/kernel/drivers
ASFLAGS = --64
LDFLAGS = -melf_x86_64 -T link.ld

ARCH_S_SRCS = $(TOP_DIR)/kernel/arch/boot.s $(TOP_DIR)/kernel/arch/interrupt.s $(TOP_DIR)/kernel/arch/switch.s $(TOP_DIR)/kernel/arch/mbr_data.s $(TOP_DIR)/kernel/arch/guard_asm.s
ARCH_C_SRCS = $(wildcard $(TOP_DIR)/kernel/arch/*.c)
KERNEL_SRCS = $(wildcard $(TOP_DIR)/kernel/*.c)
LIBC_C_SRCS = $(wildcard $(TOP_DIR)/libc/*.c)
LIBC_CPP_SRCS = $(TOP_DIR)/libc/cpp_runtime.cpp
CRT_TABLE_LD = $(TOP_DIR)/crt_compressed.ld

KEEP_DRIVERS  = driver fs vga sched keyboard ata iso9660 rtl8139 inflate
DRIVER_MODS   = gfx kbd mouse rtc ps2packet rootfs
KEEP_DRIVER_SRCS = $(foreach d,$(KEEP_DRIVERS),$(TOP_DIR)/drivers/$(d).c)
DRIVER_SRCS   = $(KEEP_DRIVER_SRCS)

ARCH_S_OBJS = $(patsubst $(TOP_DIR)/%.s,$(BUILD_DIR)/%.o,$(ARCH_S_SRCS))
ARCH_C_OBJS = $(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/%.o,$(ARCH_C_SRCS))
KERNEL_OBJS = $(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_SRCS))
LIBC_C_OBJS = $(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIBC_C_SRCS))
LIBC_CPP_OBJS = $(patsubst $(TOP_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(LIBC_CPP_SRCS))
DRIVER_OBJS = $(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/%.o,$(DRIVER_SRCS))
DRV_TARGETS  = $(foreach d,$(DRIVER_MODS),$(DIST_DIR)/drivers/$(d).kdrv)
FONT_MODS    = font
FONT_TARGETS = $(foreach f,$(FONT_MODS),$(DIST_DIR)/fonts/$(f).kdrv)

OBJS = $(ARCH_S_OBJS) $(ARCH_C_OBJS) $(KERNEL_OBJS) $(LIBC_C_OBJS) $(LIBC_CPP_OBJS) $(DRIVER_OBJS)
TARGET = $(DIST_DIR)/kernel.elf

PLUGIN_CFLAGS = -m64 -nostdlib -ffreestanding -fPIC -fno-stack-protector -mno-red-zone \
                -Wall -Werror -DX86_ARCH \
                -I$(TOP_DIR)/include -I$(TOP_DIR)/kernel/drivers -I$(TOP_DIR)/fonts

PLUGINS = runtime linux vm shell desktop hello taskmgr

runtime_SRC = $(TOP_DIR)/plugins/kernel/runtime.c
linux_SRC   = $(TOP_DIR)/plugins/runtime/linux.c $(TOP_DIR)/plugins/runtime/lx_elf.c \
              $(TOP_DIR)/plugins/runtime/lx_proc.c $(TOP_DIR)/plugins/runtime/lx_syscall.c
vm_SRC      = $(TOP_DIR)/plugins/kernel/vm.c $(TOP_DIR)/plugins/kernel/vm_soft.c
shell_SRC   = $(TOP_DIR)/plugins/user/shell.c
desktop_SRC = $(TOP_DIR)/plugins/user/desktop.c
hello_SRC   = $(TOP_DIR)/plugins/user/hello.c

PLUGIN_OBJS = $(foreach p,$(PLUGINS),$(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/plg/%.o,$($(p)_SRC)))

SYMTAB_C = $(BUILD_DIR)/kernel_symtab.c
SYMTAB_O = $(BUILD_DIR)/kernel_symtab.o
FIRST_ELF = $(BUILD_DIR)/kernel.elf.1st
KSYMTAB_STUB_O = $(BUILD_DIR)/kernel_symtab_stub.o
KPLG_EMBED_O = $(BUILD_DIR)/kplg_embed.o

PLUGIN_TARGETS = $(foreach p,$(PLUGINS),$(DIST_DIR)/plugins/$(p).kplg)

$(BUILD_DIR)/drv/%.o: $(TOP_DIR)/drivers/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUGIN_CFLAGS) -c $< -o $@

$(DIST_DIR)/drivers/%.kdrv: $(BUILD_DIR)/drv/%.o
	@mkdir -p $(DIST_DIR)/drivers
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $<

$(BUILD_DIR)/fnt/%.o: $(TOP_DIR)/fonts/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUGIN_CFLAGS) -c $< -o $@

$(DIST_DIR)/fonts/%.kdrv: $(BUILD_DIR)/fnt/%.o
	@mkdir -p $(DIST_DIR)/fonts
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $<

$(BUILD_DIR)/%.o: $(TOP_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(TOP_DIR)/%.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(TOP_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

all: $(TARGET) $(PLUGIN_TARGETS) $(DRV_TARGETS) $(FONT_TARGETS)

$(TARGET): $(OBJS) $(SYMTAB_O) $(KPLG_EMBED_O)
	$(LD) $(LDFLAGS) -o $@ $^

$(SYMTAB_C): $(FIRST_ELF)
	@mkdir -p $(dir $@)
	scripts/gen_kernel_symtab.py $(FIRST_ELF) $@

$(CRT_TABLE_LD): $(TOP_DIR)/scripts/gen_crt_table.py
	python3 $< x86 > $@

$(FIRST_ELF): $(OBJS) $(KSYMTAB_STUB_O) $(KPLG_EMBED_O) $(CRT_TABLE_LD)
	@mkdir -p $(DIST_DIR)
	$(LD) $(LDFLAGS) $(OBJS) $(KSYMTAB_STUB_O) $(KPLG_EMBED_O) -o $@

$(KSYMTAB_STUB_O): scripts/kernel_symtab_stub.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DIST_DIR)/plugins/runtime.kplg: $(BUILD_DIR)/plg/plugins/kernel/runtime.o
	@mkdir -p $(dir $@)
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $^

$(DIST_DIR)/plugins/linux.kplg: $(BUILD_DIR)/plg/plugins/runtime/linux.o $(BUILD_DIR)/plg/plugins/runtime/lx_elf.o $(BUILD_DIR)/plg/plugins/runtime/lx_proc.o $(BUILD_DIR)/plg/plugins/runtime/lx_syscall.o
	@mkdir -p $(dir $@)
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $^

$(DIST_DIR)/plugins/vm.kplg: $(BUILD_DIR)/plg/plugins/kernel/vm.o $(BUILD_DIR)/plg/plugins/kernel/vm_soft.o
	@mkdir -p $(dir $@)
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $^

$(DIST_DIR)/plugins/shell.kplg: $(BUILD_DIR)/plg/plugins/user/shell.o
	@mkdir -p $(dir $@)
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $^

$(DIST_DIR)/plugins/desktop.kplg: $(BUILD_DIR)/plg/plugins/user/desktop.o
	@mkdir -p $(dir $@)
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $^

$(DIST_DIR)/plugins/hello.kplg: $(BUILD_DIR)/plg/plugins/user/hello.o
	@mkdir -p $(dir $@)
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $^

$(DIST_DIR)/plugins/taskmgr.kplg: $(BUILD_DIR)/plg/plugins/user/taskmgr.o
	@mkdir -p $(dir $@)
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $^

$(BUILD_DIR)/plg/plugins/kernel/%.o: $(TOP_DIR)/plugins/kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUGIN_CFLAGS) -c $< -o $@

$(BUILD_DIR)/plg/plugins/runtime/%.o: $(TOP_DIR)/plugins/runtime/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUGIN_CFLAGS) -c $< -o $@

$(BUILD_DIR)/plg/plugins/user/%.o: $(TOP_DIR)/plugins/user/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUGIN_CFLAGS) -c $< -o $@

$(FONT_TARGETS): $(DIST_DIR)/fonts/%.kdrv: $(BUILD_DIR)/fnt/%.o
	@mkdir -p $(dir $@)
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $<

$(DRV_TARGETS): $(DIST_DIR)/drivers/%.kdrv: $(BUILD_DIR)/drv/%.o
	@mkdir -p $(dir $@)
	$(LD) -melf_x86_64 -shared -z max-page-size=0x1000 -o $@ $<

INSTALL_IMG_C = $(BUILD_DIR)/install_img_data.c
INSTALL_IMG_O = $(BUILD_DIR)/install_img_data.o

$(INSTALL_IMG_C): scripts/pack_install_img.py $(TARGET)
	@mkdir -p $(dir $@)
	python3 $< $(TARGET) $@

$(INSTALL_IMG_O): $(INSTALL_IMG_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

kernel-install: $(TARGET) $(INSTALL_IMG_O)
	@mkdir -p $(DIST_DIR)
	$(LD) $(LDFLAGS) $(OBJS) $(SYMTAB_O) $(KPLG_EMBED_O) $(INSTALL_IMG_O) -o $(DIST_DIR)/kernel-install.elf

iso: all
	scripts/make_iso_x86.sh

install-iso: kernel-install
	scripts/make_install_iso_x86.sh

$(KPLG_EMBED_O): $(BUILD_DIR)/kplg_embed.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(TOP_DIR)/kernel -c $< -o $@

$(BUILD_DIR)/kplg_embed.c: scripts/pack_kplg_embed.py $(PLUGIN_TARGETS) $(DRV_TARGETS) $(FONT_TARGETS)
	@mkdir -p $(dir $@)
	python3 $< $(DIST_DIR)/plugins $(DIST_DIR)/drivers $(DIST_DIR)/fonts $@

$(KPLG_EMBED_O): $(BUILD_DIR)/kplg_embed.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(TOP_DIR)/kernel -c $< -o $@

$(BUILD_DIR)/kplg_embed.c: scripts/pack_kplg_embed.py $(PLUGIN_TARGETS) $(DRV_TARGETS) $(FONT_TARGETS)
	@mkdir -p $(dir $@)
	python3 $< $(DIST_DIR)/plugins $(DIST_DIR)/drivers $(DIST_DIR)/fonts $@

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(CRT_TABLE_LD)

MBR_OBJ = $(BUILD_DIR)/kernel/arch/mbr.o
MBR_ELF = $(BUILD_DIR)/mbr.elf
MBR_BIN = $(BUILD_DIR)/mbr.bin

$(MBR_OBJ): $(TOP_DIR)/kernel/arch/mbr.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(MBR_ELF): $(MBR_OBJ)
	$(LD) -melf_x86_64 -Ttext=0x7C00 -o $@ $<

$(MBR_BIN): $(MBR_ELF)
	objcopy -O binary $< $@

$(BUILD_DIR)/kernel/arch/mbr_data.o: $(MBR_BIN) $(TOP_DIR)/kernel/arch/mbr_data.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $(TOP_DIR)/kernel/arch/mbr_data.s -o $@

MBR_OBJ = $(BUILD_DIR)/kernel/arch/mbr.o
MBR_ELF = $(BUILD_DIR)/mbr.elf
MBR_BIN = $(BUILD_DIR)/mbr.bin

$(MBR_OBJ): $(TOP_DIR)/kernel/arch/mbr.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(MBR_ELF): $(MBR_OBJ)
	$(LD) -melf_x86_64 -Ttext=0x7C00 -o $@ $<

$(MBR_BIN): $(MBR_ELF)
	objcopy -O binary $< $@

$(BUILD_DIR)/kernel/arch/mbr_data.o: $(MBR_BIN) $(TOP_DIR)/kernel/arch/mbr_data.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $(TOP_DIR)/kernel/arch/mbr_data.s -o $@

.PHONY: all iso install-iso kernel-install clean