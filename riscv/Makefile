TOP_DIR = $(shell pwd)
BUILD_DIR = $(TOP_DIR)/build
DIST_DIR = $(TOP_DIR)/dist

CC = riscv64-linux-gnu-gcc
CFLAGS = -march=rv64gc -mabi=lp64 -mcmodel=medany -nostdlib -ffreestanding \
	-fno-omit-frame-pointer -Wall -Werror -Wno-unused-function -Wno-unused-variable \
	-I$(TOP_DIR)/include -I$(TOP_DIR)/shared
LDFLAGS = -T link.ld -mcmodel=medany -no-pie

BOOT_SRC = $(TOP_DIR)/boot/start.S
KERNEL_SRC = $(wildcard $(TOP_DIR)/kernel/*.c)
DRIVER_SRCS = $(wildcard $(TOP_DIR)/kernel/drivers/*.c)
LIBC_SRC = $(wildcard $(TOP_DIR)/kernel/libc/*.c)
PLUGIN_SRCS = $(wildcard $(TOP_DIR)/kernel/plugins/*.c)
SHARED_SRCS = $(wildcard $(TOP_DIR)/shared/plugins/*.c)
CRT_TABLE_LD = $(TOP_DIR)/crt_compressed.ld

BOOT_OBJ = $(BUILD_DIR)/boot/start.o
KERNEL_OBJS = $(patsubst $(TOP_DIR)/kernel/%.c,$(BUILD_DIR)/kernel/%.o,$(KERNEL_SRC))
DRIVER_OBJS = $(patsubst $(TOP_DIR)/kernel/drivers/%.c,$(BUILD_DIR)/kernel/drivers/%.o,$(DRIVER_SRCS))
LIBC_OBJS = $(patsubst $(TOP_DIR)/kernel/libc/%.c,$(BUILD_DIR)/kernel/libc/%.o,$(LIBC_SRC))
PLUGIN_OBJS = $(patsubst $(TOP_DIR)/kernel/plugins/%.c,$(BUILD_DIR)/kernel/plugins/%.o,$(PLUGIN_SRCS))
SHARED_OBJS = $(patsubst $(TOP_DIR)/shared/%.c,$(BUILD_DIR)/shared/%.o,$(SHARED_SRCS))

OBJS = $(BOOT_OBJ) $(KERNEL_OBJS) $(DRIVER_OBJS) $(LIBC_OBJS) $(PLUGIN_OBJS) $(SHARED_OBJS)
TARGET = $(DIST_DIR)/kernel.elf

all: $(TARGET)

$(BOOT_OBJ): $(BOOT_SRC)
	@mkdir -p $(BUILD_DIR)/boot
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: $(TOP_DIR)/kernel/%.c
	@mkdir -p $(BUILD_DIR)/kernel
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/libc/%.o: $(TOP_DIR)/kernel/libc/%.c
	@mkdir -p $(BUILD_DIR)/kernel/libc
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/drivers/%.o: $(TOP_DIR)/kernel/drivers/%.c
	@mkdir -p $(BUILD_DIR)/kernel/drivers
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/plugins/%.o: $(TOP_DIR)/kernel/plugins/%.c
	@mkdir -p $(BUILD_DIR)/kernel/plugins
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shared/%.o: $(TOP_DIR)/shared/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CRT_TABLE_LD): $(TOP_DIR)/scripts/gen_crt_table.py
	python3 $< riscv > $@

$(TARGET): $(OBJS) $(CRT_TABLE_LD)
	@mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -lgcc -o $@

run: $(TARGET)
	qemu-system-riscv64 -machine virt -bios none -kernel $(TARGET) -nographic -serial mon:stdio -no-reboot

iso: $(TARGET)
	bash $(TOP_DIR)/scripts/make_iso.sh

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(CRT_TABLE_LD)

.PHONY: all run iso clean