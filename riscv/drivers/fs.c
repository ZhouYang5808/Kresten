/* ===== kernel/drivers/fs_drv.c: filesystem driver ===== */
#include <driver.h>
#include <fs.h>

int fs_init(void);

static int fs_driver_init(void) {
    return fs_init();
}

static Driver fs_driver = {
    .name = "fs",
    .type = DRV_TYPE_FS,
    .init = fs_driver_init,
};

int fs_driver_register(void) {
    return driver_register(&fs_driver);
}
