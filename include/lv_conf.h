// LVGL 8.3 configuration for ESP32-2432S028 (320x240, RGB565)
// Anything not defined here falls back to the defaults in lv_conf_internal.h.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0

#define LV_MEM_CUSTOM      1  // LVGL allocates from the ESP32 heap (shared with Wi-Fi/HTTPS), no fixed pool

#define LV_DISP_DEF_REFR_PERIOD  20
#define LV_INDEV_DEF_READ_PERIOD 20

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE      "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL   1
#define LV_USE_ASSERT_MALLOC 1

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1  // big numbers in apps (digits + symbols only, no Thai)
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_FONT_COMPRESSED 1  // the Thai fonts in src/fonts are RLE-compressed by lv_font_conv

#define LV_USE_DROPDOWN 1
#define LV_USE_LABEL    1
#define LV_USE_SPINNER  1
#define LV_USE_ARC      1

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#endif /*LV_CONF_H*/
#endif
