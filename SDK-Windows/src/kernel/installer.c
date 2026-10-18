#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ata.h>

extern const char mbr_bin_start[], mbr_bin_end[];
extern const unsigned char __install_img[] __attribute__((weak));
extern const uint32_t __install_img_size __attribute__((weak));

/* Install mode: only the installer kernel carries the embedded install image
 * (weak symbols are NULL/0 in the normal desktop kernel). Writes the MBR boot
 * sector to LBA 0 and the image to LBA 1+, returns 0 on success. */
int install_to_hdd(void) {
    if (__install_img == NULL || __install_img_size == 0) {
        puts("[INSTALL] not an installer build - normal boot\n");
        return -1;
    }
    uint32_t size = __install_img_size;
    printf("[INSTALL] embedded image %u bytes\n", size);
    if (size < 16 || strncmp((const char *)__install_img, "KRSTENBR", 8) != 0) {
        puts("[INSTALL] bad image magic\n");
        return -1;
    }

    if (ata_init() < 0) {
        puts("[INSTALL] no ATA drive found\n");
        return -1;
    }

    static uint8_t mbr[512];
    memset(mbr, 0, sizeof(mbr));
    uint32_t mbr_size = (uint32_t)(mbr_bin_end - mbr_bin_start);
    if (mbr_size > 512) {
        puts("[INSTALL] MBR too large\n");
        return -1;
    }
    memcpy(mbr, mbr_bin_start, mbr_size);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    if (ata_write_sectors(0, 0, 1, mbr) < 0) {
        puts("[INSTALL] MBR write failed\n");
        return -1;
    }
    puts("[INSTALL] MBR written\n");

    uint32_t sectors = (size + 511) / 512;
    printf("[INSTALL] writing %u sectors\n", sectors);
    static uint8_t buf[512 * 255];
    uint32_t lba = 1;
    uint32_t left = sectors;
    while (left > 0) {
        uint32_t n = left > 255 ? 255 : left;
        uint32_t off = (lba - 1) * 512;
        uint32_t avail = size > off ? size - off : 0;
        uint32_t copy = avail > n * 512 ? n * 512 : avail;
        memset(buf, 0, sizeof(buf));
        memcpy(buf, __install_img + off, copy);
        if (ata_write_sectors(0, lba, (uint8_t)n, buf) < 0) {
            printf("[INSTALL] write failed at LBA %u\n", lba);
            return -1;
        }
        lba += n;
        left -= n;
    }
    puts("[INSTALL] install complete, rebooting...\n");
    return 0;
}
