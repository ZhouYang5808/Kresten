TOP_DIR = $(shell pwd)
BUILD_DIR = $(TOP_DIR)/build
DIST_DIR = $(TOP_DIR)/dist

CC = gcc
AS = as
LD = ld
CFLAGS = -m32 -nostdlib -ffreestanding -Wall -Werror -DX86_ARCH \
         -I$(TOP_DIR)/include -I$(TOP_DIR)/shared -I$(TOP_DIR)/kernel/drivers
ASFLAGS = --32
LDFLAGS = -melf_i386 -T link.ld

ARCH_S_SRCS = $(TOP_DIR)/arch/boot.s $(TOP_DIR)/arch/interrupt.s $(TOP_DIR)/arch/switch.s $(TOP_DIR)/arch/mbr_data.s
ARCH_C_SRCS = $(wildcard $(TOP_DIR)/arch/*.c)
KERNEL_SRCS = $(wildcard $(TOP_DIR)/kernel/*.c)
LIBC_SRCS   = $(wildcard $(TOP_DIR)/libc/*.c)
DRIVER_SRCS = $(wildcard $(TOP_DIR)/drivers/*.c)
PLUGIN_SRCS = $(wildcard $(TOP_DIR)/plugins/*.c)
SHARED_SRCS = $(TOP_DIR)/shared/plugins/desktop.c $(TOP_DIR)/shared/drivers/ps2packet.c
CRT_TABLE_LD = $(TOP_DIR)/crt_compressed.ld

ARCH_S_OBJS = $(patsubst $(TOP_DIR)/%.s,$(BUILD_DIR)/%.o,$(ARCH_S_SRCS))
ARCH_C_OBJS = $(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/%.o,$(ARCH_C_SRCS))
KERNEL_OBJS = $(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_SRCS))
LIBC_OBJS   = $(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIBC_SRCS))
DRIVER_OBJS = $(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/%.o,$(DRIVER_SRCS))
PLUGIN_OBJS = $(patsubst $(TOP_DIR)/%.c,$(BUILD_DIR)/%.o,$(PLUGIN_SRCS))
SHARED_OBJS = $(patsubst $(TOP_DIR)/shared/%.c,$(BUILD_DIR)/shared/%.o,$(SHARED_SRCS))

OBJS = $(ARCH_S_OBJS) $(ARCH_C_OBJS) $(KERNEL_OBJS) $(LIBC_OBJS) \
       $(DRIVER_OBJS) $(PLUGIN_OBJS) $(SHARED_OBJS)
TARGET = $(DIST_DIR)/kernel.elf

all: $(TARGET)

kernel-install: $(TARGET) $(BUILD_DIR)/install_img_data.o
	$(LD) $(LDFLAGS) $(OBJS) $(BUILD_DIR)/install_img_data.o -o $(DIST_DIR)/kernel-install.elf

$(BUILD_DIR)/%.o: $(TOP_DIR)/%.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(TOP_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shared/%.o: $(TOP_DIR)/shared/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CRT_TABLE_LD): $(TOP_DIR)/scripts/gen_crt_table.py
	python3 $< x86 > $@

$(TARGET): $(OBJS) $(CRT_TABLE_LD)
	@mkdir -p $(DIST_DIR)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

run: $(TARGET)
	qemu-system-x86_64 -machine pc -bios /usr/share/seabios/bios.bin -kernel $(TARGET) -serial telnet::1234,server,nowait -display none -no-reboot

iso: $(TARGET) kernel-install
	bash $(TOP_DIR)/scripts/make_iso_x86.sh
	bash $(TOP_DIR)/scripts/make_install_iso_x86.sh

iso-x86: $(TARGET) kernel-install
	bash $(TOP_DIR)/scripts/make_iso_x86.sh
	bash $(TOP_DIR)/scripts/make_install_iso_x86.sh

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(CRT_TABLE_LD)

.PHONY: all run clean iso iso-x86

MBR_BIN = $(BUILD_DIR)/mbr.bin

$(MBR_BIN): $(TOP_DIR)/arch/mbr.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $(BUILD_DIR)/mbr.o $(TOP_DIR)/arch/mbr.s
	objcopy -O binary $(BUILD_DIR)/mbr.o $(MBR_BIN)

$(BUILD_DIR)/arch/mbr_data.o: $(MBR_BIN)

INSTALL_IMG_C = $(BUILD_DIR)/install_img_data.c

$(INSTALL_IMG_C): $(TARGET)
	python3 $(TOP_DIR)/scripts/pack_install_img.py $(TARGET) $@

$(BUILD_DIR)/install_img_data.o: $(INSTALL_IMG_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: kernel-install
