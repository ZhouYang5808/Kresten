#ifndef _ATA_H
#define _ATA_H

#include <stdint.h>

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

#define ATA_REG_DATA        0
#define ATA_REG_ERROR       1
#define ATA_REG_SECCOUNT    2
#define ATA_REG_LBA_LOW     3
#define ATA_REG_LBA_MID     4
#define ATA_REG_LBA_HIGH    5
#define ATA_REG_DRIVE       6
#define ATA_REG_COMMAND     7
#define ATA_REG_STATUS      7

#define ATA_CMD_READ        0x20
#define ATA_CMD_WRITE       0x30
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_PACKET      0xA0

#define ATA_SR_BSY          0x80
#define ATA_SR_DRDY         0x40
#define ATA_SR_DRQ          0x08
#define ATA_SR_ERR          0x01

/* drive selector: bit0 = master/slave, bit1 = channel (0=primary,1=secondary)
 *   0 = primary master, 1 = primary slave, 2 = secondary master, 3 = secondary slave */
#define ATA_DEV_PM  0
#define ATA_DEV_PS  1
#define ATA_DEV_SM  2
#define ATA_DEV_SS  3

#define ATA_TYPE_NONE   0
#define ATA_TYPE_ATA    1
#define ATA_TYPE_ATAPI  2

int ata_init(void);
int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buf);
int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void *buf);
void ata_print_info(void);
int ata_dev_type(uint8_t drive);
int atapi_read(uint8_t drive, uint32_t lba, uint32_t sectors, void *buf);

#endif