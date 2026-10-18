/* ===== arm/fonts/font.c: 字体模块 (.kdrv) =====
 * 独立的可加载字体资源: 1-bit Terminus(vga_font8x16) + 4-bit AA
 * (vga_font8x16_aa)。数据以 static 数组内嵌于本模块(include 头文件
 * 得到本 TU 副本); gfx.kdrv(gfx_font_bmp)与 desktop.kplg(gfx_get_font_aa)
 * 在加载时经 gfx_font_bmp()/gfx_get_font_aa() 从本模块解析。
 * 加载顺序(kdrv_auto): font 在 gfx 之前。 */

#include <gfx.h>
#include <driver.h>
#include <font8x16.h>
#include <font_aa8x16.h>

const uint8_t *gfx_get_font_aa(void) {
    return vga_font8x16_aa;
}

const uint8_t *gfx_font_bmp(void) {
    return vga_font8x16;
}

/* 注册为驱动(在 shell `drivers` 列出); 无 init(纯数据模块) */
REGISTER_DRIVER("font", DRV_TYPE_VGA, 0);
