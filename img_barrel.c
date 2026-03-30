#include "lvgl.h"

static const uint8_t img_barrel_map[] = {
    /* ... данные ... */
};

const lv_img_dsc_t img_barrel = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf    = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w     = 80,
    .header.h     = 80,
    .data_size    = sizeof(img_barrel_map),
    .data         = img_barrel_map,
};