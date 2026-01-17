/**
 * LVGL v9.4.0 Configuration for ESP32-P4
 * 800x480 MIPI-DSI Display - Performance Optimized
 * Hardware: 360MHz RISC-V dual-core, 32MB PSRAM (OPI), 16MB Flash
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/**********************
 * COLOR SETTINGS
 *********************/
#define LV_COLOR_DEPTH 16                       // RGB565 (2 bytes/pixel)
#define LV_COLOR_16_SWAP 0                      // No swap for RGB parallel
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00FF00)

/**********************
 * MEMORY SETTINGS (ESP32-P4 with 32MB PSRAM)
 *********************/
#define LV_MEM_CUSTOM 1                         // Custom memory allocation
#define LV_MEM_SIZE (512U * 1024U)              // 512KB for LVGL heap (leverage 32MB PSRAM)
#define LV_MEM_ADR 0                            // Let LVGL allocate
#define LV_MEM_BUF_MAX_NUM 32                   // More buffers for complex UIs
#define LV_MEM_POOL_INCLUDE <stdlib.h>
#define LV_MEM_POOL_ALLOC malloc
#define LV_MEMCPY_MEMSET_STD 1

/**********************
 * DISPLAY SETTINGS
 *********************/
#define LV_DISPLAY_DEF_WIDTH  800              // Horizontal resolution
#define LV_DISPLAY_DEF_HEIGHT 480              // Vertical resolution
#define LV_DPI_DEF 130                         // DPI for 4.3" display

/**********************
 * RENDERING PERFORMANCE
 *********************/
#define LV_DEF_REFR_PERIOD 16                  // 60 FPS (16ms refresh)
#define LV_INDEV_DEF_READ_PERIOD 20            // 50Hz input polling (optimized for performance)
#define LV_USE_PERF_MONITOR 0                  // Disable for production

/* Rendering optimization (ESP32-P4: 32MB PSRAM, 360MHz) */
#define LV_LAYER_SIMPLE_BUF_SIZE (64U * 1024U) // 64KB layer buffer (more PSRAM available)
#define LV_IMG_CACHE_DEF_SIZE 20               // Cache 20 images (album art caching)
#define LV_GRADIENT_MAX_STOPS 8
#define LV_GRAD_CACHE_DEF_SIZE 1024            // Larger gradient cache

/**********************
 * GPU ACCELERATION
 *********************/
#define LV_USE_DRAW_SW 1
#define LV_USE_DRAW_MASKS 1
#define LV_DRAW_SW_SHADOW_CACHE_SIZE 0         // Disable shadow cache
#define LV_DRAW_SW_GRADIENT_CACHE_DEF_SIZE 512
#define LV_DRAW_SW_COMPLEX 1

/* Disable ARM-specific acceleration (ESP32-P4 is RISC-V, not ARM) */
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE
#define LV_DRAW_SW_SUPPORT_HELIUM 0
#define LV_DRAW_SW_SUPPORT_NEON 0
#define LV_USE_NATIVE_HELIUM_ASM 0

/**********************
 * LOGGING
 *********************/
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN     // Only warnings and errors
    #define LV_LOG_PRINTF 1
#endif

/**********************
 * FEATURE USAGE
 *********************/

/* Enable features we need */
#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

/**********************
 * WIDGETS
 *********************/
#define LV_USE_ANIMIMG 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_LIST 1
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 1
#define LV_USE_ROLLER 1
#define LV_USE_SLIDER 1
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 1
#define LV_USE_SWITCH 1
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 1
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_SCALE 0
#define LV_USE_CHART 0
#define LV_USE_CALENDAR 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 1
#define LV_USE_KEYBOARD 1
#define LV_USE_LED 0
#define LV_USE_METER 0

/**********************
 * THEMES
 *********************/
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_SIMPLE 1
#define LV_USE_THEME_MONO 0

/**********************
 * LAYOUTS
 *********************/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/**********************
 * FONT SETTINGS
 *********************/
/* Font support - optimized set */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* Enable symbol fonts */
#define LV_FONT_MONTSERRAT_12_SUBPX 0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK 0
#define LV_FONT_UNSCII_8 0
#define LV_FONT_UNSCII_16 0

/**********************
 * TEXT SETTINGS
 *********************/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_COLOR_CMD "#"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/**********************
 * TICK INTERFACE
 *********************/
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

/**********************
 * ASSERT
 *********************/
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

#define LV_ASSERT_HANDLER_INCLUDE <stdint.h>
#define LV_ASSERT_HANDLER while(1);

/**********************
 * OTHERS
 *********************/
#define LV_USE_USER_DATA 1

/* Memory attributes */
#define LV_ATTRIBUTE_FAST_MEM IRAM_ATTR
#define LV_ATTRIBUTE_FAST_MEM_INIT
#define LV_ATTRIBUTE_LARGE_CONST

/* Compiler prefix for large array declaration */
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY

/* Extend malloc/free functions */
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

#endif /*LV_CONF_H*/
