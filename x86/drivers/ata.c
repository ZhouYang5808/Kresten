#include <ata.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

static int ata_type[4];
static uint16_t ata_io[4];
static char ata_model[4][41];

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

static uint16_t ata_sel_reg(uint8_t drive) {
    return (drive & 2) ? ATA_SECONDARY_IO : ATA_PRIMARY_IO;
}

static uint8_t ata_sel_drv(uint8_t drive) {
    return (drive & 1) ? 0xF0 : 0xE0;
}

/* 探测单个槽位: 标准 ATAPI 探测 = IDENTIFY PACKET (0xA1) 优先,
 * 失败(ATA 盘或无设备)再回退 ATA IDENTIFY (0xEC). 成功则填 type/model. */
static void ata_probe_slot(uint8_t drive) {
    uint16_t io = ata_sel_reg(drive);
    uint8_t sel = ata_sel_drv(drive);
    ata_type[drive] = ATA_TYPE_NONE;

    outb(io + ATA_REG_DRIVE, sel);
    ata_poll(io);
    outb(io + ATA_REG_ERROR, 0);
    outb(io + ATA_REG_SECCOUNT, 1);   /* 某些控制器要求 lba1=1 用于 packet */
    outb(io + ATA_REG_LBA_LOW, 0);
    outb(io + ATA_REG_LBA_MID, 0);
    outb(io + ATA_REG_LBA_HIGH, 0);
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
    if (ata_wait(io, 10000) >= 0) {
        uint16_t id[256];
        for (int i = 0; i < 256; i++) id[i] = inw(io + ATA_REG_DATA);
        if ((id[0] & 0x8000) && id[0] != 0xFFFF && id[0] != 0x0000) {
            ata_type[drive] = ATA_TYPE_ATAPI;
            for (int i = 0; i < 20; i++) {
                ata_model[drive][i*2]   = (id[27 + i] >> 8) & 0xFF;
                ata_model[drive][i*2+1] = id[27 + i] & 0xFF;
            }
            ata_model[drive][40] = '\0';
            goto trim;
        }
    }
    /* 回退: ATA IDENTIFY */
    outb(io + ATA_REG_DRIVE, sel);
    ata_poll(io);
    outb(io + ATA_REG_ERROR, 0);
    outb(io + ATA_REG_SECCOUNT, 0);
    outb(io + ATA_REG_LBA_LOW, 0);
    outb(io + ATA_REG_LBA_MID, 0);
    outb(io + ATA_REG_LBA_HIGH, 0);
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    if (inb(io + ATA_REG_STATUS) != 0 && ata_wait(io, 10000) >= 0) {
        uint16_t id[256];
        for (int i = 0; i < 256; i++) id[i] = inw(io + ATA_REG_DATA);
        if (id[0] != 0xFFFF && id[0] != 0x0000 && !(id[0] & 0x8000)) {
            ata_type[drive] = ATA_TYPE_ATA;
            for (int i = 0; i < 20; i++) {
                ata_model[drive][i*2]   = (id[27 + i] >> 8) & 0xFF;
                ata_model[drive][i*2+1] = id[27 + i] & 0xFF;
            }
            ata_model[drive][40] = '\0';
        }
    }
trim:
    if (ata_type[drive] == ATA_TYPE_NONE) return;
    for (int i = 39; i > 0; i--) {
        if (ata_model[drive][i] == ' ') ata_model[drive][i] = '\0';
        else if (ata_model[drive][i]) break;
    }
    ata_io[drive] = io;
}

int ata_init(void) {
    outb(ATA_PRIMARY_CTRL + 1, 0x02);
    outb(ATA_SECONDARY_CTRL + 1, 0x02);
    for (int d = 0; d < 4; d++) ata_probe_slot(d);
    return 0;
}

int ata_dev_type(uint8_t drive) {
    if (drive > 3) return ATA_TYPE_NONE;
    return ata_type[drive];
}

void ata_print_info(void) {
    int have = 0;
    for (int d = 0; d < 4; d++) {
        if (ata_type[d] == ATA_TYPE_NONE) continue;
        have = 1;
        printf("  Drive %d (%s %s, %s): %s\n", d,
               (d & 2) ? "secondary" : "primary",
               (d & 1) ? "slave" : "master",
               ata_type[d] == ATA_TYPE_ATAPI ? "ATAPI" : "ATA",
               ata_model[d]);
    }
    if (!have) puts("ATA: No drive detected\n");
}

int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buf) {
    if (drive > 3 || ata_type[drive] != ATA_TYPE_ATA || count == 0) return -1;
    uint16_t io = ata_sel_reg(drive);
    uint8_t drv = ata_sel_drv(drive);
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
    if (drive > 3 || ata_type[drive] != ATA_TYPE_ATA || count == 0) return -1;
    uint16_t io = ata_sel_reg(drive);
    uint8_t drv = ata_sel_drv(drive);
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

/* ATAPI 12-byte CDB read (CMD PACKET, 2048-byte sectors). */
static int atapi_packet(uint8_t drive, uint8_t cdb[12], uint16_t max_len, void *buf) {
    uint16_t io = ata_sel_reg(drive);
    uint8_t drv = ata_sel_drv(drive);
    outb(io + ATA_REG_DRIVE, drv);
    ata_poll(io);
    outb(io + ATA_REG_ERROR, 0);
    outb(io + ATA_REG_SECCOUNT, 0);
    outb(io + ATA_REG_LBA_LOW, 0);
    outb(io + ATA_REG_LBA_MID, max_len & 0xFF);       /* BCL low */
    outb(io + ATA_REG_LBA_HIGH, (max_len >> 8) & 0xFF); /* BCL high */
    outb(io + ATA_REG_COMMAND, ATA_CMD_PACKET);
    {
        int got_drq = 0;
        for (int i = 0; i < 100000; i++) {
            uint8_t st = inb(io + ATA_REG_STATUS);
            if (st & ATA_SR_BSY) continue;
            if (st & ATA_SR_DRQ) { got_drq = 1; break; }
            if (st & ATA_SR_ERR) break;
        }
        if (!got_drq) {
            printf("[ATA] packet cdb handshake fail st=%02x err=%02x\n",
                   inb(io + ATA_REG_STATUS), inb(io + ATA_REG_ERROR));
            return -1;
        }
    }
    uint8_t *p = (uint8_t *)cdb;
    while (p < cdb + 12) {
        uint16_t w = p[0] | (p[1] << 8);
        outw(io + ATA_REG_DATA, w);
        p += 2;
    }
    uint8_t *dst = (uint8_t *)buf;
    while (max_len > 0) {
        int r = ata_wait(io, 100000);
        if (r < 0) {
            uint32_t lba = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            printf("[ATA] packet data fail st=%02x err=%02x cdb0=%02x lba=%u rem=%u\n",
                   inb(io + ATA_REG_STATUS), inb(io + ATA_REG_ERROR), cdb[0], lba, max_len);
            return -1;
        }
        uint32_t n = max_len > 2048 ? 2048 : max_len;
        for (uint32_t i = 0; i < n; i += 2)
            *(uint16_t *)(dst + i) = inw(io + ATA_REG_DATA);
        dst += n;
        max_len -= n;
    }
    ata_poll(io);
    return 0;
}

int atapi_read(uint8_t drive, uint32_t lba, uint32_t sectors, void *buf) {
    if (drive > 3 || ata_type[drive] != ATA_TYPE_ATAPI || sectors == 0) return -1;
    uint8_t cdb[12] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >> 8) & 0xFF;
    cdb[5] = lba & 0xFF;
    cdb[7] = (sectors >> 8) & 0xFF;
    cdb[8] = sectors & 0xFF;
    return atapi_packet(drive, cdb, sectors * 2048, buf);
}

/* 诊断: 发送任意 12 字节 CDB 并打印结果(仅调试) */
void atapi_debug_cdb(uint8_t drive, const uint8_t *cdb, uint16_t max_len) {
    static uint8_t tmp[2048];
    int r = atapi_packet(drive, (uint8_t *)cdb, max_len, tmp);
    printf("[ATA] dbg drive=%d r=%d cdb0=%02x\n", drive, r, cdb[0]);
}