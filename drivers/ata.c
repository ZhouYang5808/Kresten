#include <ata.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

static int ata_present = 0;

static int ata_wait(uint16_t io, int timeout) {
    for (int i = 0; i < timeout; i++) {
        uint8_t st = inb(io + ATA_REG_STATUS);
        if (!(st & ATA_SR_BSY)) {
            if (st & ATA_SR_ERR) return -1;
            if (st & ATA_SR_DRQ) return 1;
            if (i > 100) return 1;
        }
    }
    return -1;
}

static int ata_poll(uint16_t io) {
    for (int i = 0; i < 100000; i++) {
        uint8_t st = inb(io + ATA_REG_STATUS);
        if (!(st & ATA_SR_BSY)) {
            return (st & ATA_SR_ERR) ? -1 : 0;
        }
    }
    return -1;
}

int ata_init(void) {
    outb(ATA_PRIMARY_CTRL + 1, 0x02);
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    inb(ATA_PRIMARY_IO + ATA_REG_COMMAND);
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    if (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) == 0) { ata_present = 0; return -1; }
    if (ata_wait(ATA_PRIMARY_IO, 10000) < 0) { ata_present = 0; return -1; }
    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    if ((id[0] & 0x8000) || id[0] == 0xFFFF || id[0] == 0x0000) { ata_present = 0; return -1; }
    ata_present = 1;
    return 0;
}

void ata_print_info(void) {
    if (!ata_present) { puts("ATA: No drive detected\n"); return; }
    ata_init();
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    if (ata_wait(ATA_PRIMARY_IO, 10000) < 0) return;
    uint16_t id[256];
    char model[41];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    for (int i = 0; i < 20; i++) {
        model[i*2]   = (id[27 + i] >> 8) & 0xFF;
        model[i*2+1] = id[27 + i] & 0xFF;
    }
    model[40] = '\0';
    for (int i = 39; i > 0; i--) {
        if (model[i] == ' ') model[i] = '\0';
        else if (model[i]) break;
    }
    uint32_t sectors = *(uint32_t *)&id[60];
    uint32_t size_mb = (sectors / 2048);
    printf("  Model: %s\n", model);
    printf("  Sectors: %u (%u MB)\n", sectors, size_mb);
}

int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buf) {
    if (!ata_present || count == 0) return -1;
    uint16_t io = (drive & 0x01) ? ATA_SECONDARY_IO : ATA_PRIMARY_IO;
    uint8_t drv = 0xE0 | ((drive & 0x01) << 4);
    outb(io + ATA_REG_DRIVE, drv | ((lba >> 24) & 0x0F));
    outb(io + ATA_REG_SECCOUNT, count);
    outb(io + ATA_REG_LBA_LOW, lba & 0xFF);
    outb(io + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(io + ATA_REG_COMMAND, ATA_CMD_READ);
    uint16_t *ptr = (uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait(io, 100000) < 0) return -1;
        for (int i = 0; i < 256; i++) ptr[s * 256 + i] = inw(io + ATA_REG_DATA);
    }
    ata_poll(io);
    return 0;
}

int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void *buf) {
    if (!ata_present || count == 0) return -1;
    uint16_t io = (drive & 0x01) ? ATA_SECONDARY_IO : ATA_PRIMARY_IO;
    uint8_t drv = 0xE0 | ((drive & 0x01) << 4);
    outb(io + ATA_REG_DRIVE, drv | ((lba >> 24) & 0x0F));
    outb(io + ATA_REG_SECCOUNT, count);
    outb(io + ATA_REG_LBA_LOW, lba & 0xFF);
    outb(io + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE);
    const uint16_t *ptr = (const uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait(io, 100000) < 0) return -1;
        for (int i = 0; i < 256; i++) outw(io + ATA_REG_DATA, ptr[s * 256 + i]);
        outb(io + ATA_REG_COMMAND, 0xE7);
        if (ata_poll(io) < 0) return -1;
    }
    return 0;
}
