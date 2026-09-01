#ifndef MICROPY_INCLUDED_RENESAS_RA_BOARDS_VK_RA6M3_LV_CONF_BOARD_H
#define MICROPY_INCLUDED_RENESAS_RA_BOARDS_VK_RA6M3_LV_CONF_BOARD_H

#include "lv_conf.h"

#undef LV_FONT_MONTSERRAT_8
#define LV_FONT_MONTSERRAT_8 1
#undef LV_FONT_MONTSERRAT_10
#define LV_FONT_MONTSERRAT_10 1
#undef LV_FONT_MONTSERRAT_18
#define LV_FONT_MONTSERRAT_18 1
#undef LV_FONT_MONTSERRAT_20
#define LV_FONT_MONTSERRAT_20 1
#undef LV_FONT_MONTSERRAT_22
#define LV_FONT_MONTSERRAT_22 1
#undef LV_FONT_MONTSERRAT_24
#define LV_FONT_MONTSERRAT_24 1
#undef LV_FONT_MONTSERRAT_26
#define LV_FONT_MONTSERRAT_26 1
#undef LV_FONT_MONTSERRAT_28
#define LV_FONT_MONTSERRAT_28 1
#undef LV_FONT_MONTSERRAT_30
#define LV_FONT_MONTSERRAT_30 1
#undef LV_FONT_MONTSERRAT_32
#define LV_FONT_MONTSERRAT_32 1
#undef LV_FONT_MONTSERRAT_34
#define LV_FONT_MONTSERRAT_34 1
#undef LV_FONT_MONTSERRAT_36
#define LV_FONT_MONTSERRAT_36 1
#undef LV_FONT_MONTSERRAT_38
#define LV_FONT_MONTSERRAT_38 1

#undef LV_USE_DRAW_DAVE2D
#define LV_USE_DRAW_DAVE2D 1

/* Optional LVGL 9.4 features used by the complete MicroPython examples. */
#undef LV_USE_DRAW_SW_COMPLEX_GRADIENTS
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 1

#undef LV_USE_MATRIX
#define LV_USE_MATRIX 1
#undef LV_USE_FLOAT
#define LV_USE_FLOAT 1
#undef LV_DRAW_TRANSFORM_USE_MATRIX
#define LV_DRAW_TRANSFORM_USE_MATRIX 1

#undef LV_USE_VECTOR_GRAPHIC
#define LV_USE_VECTOR_GRAPHIC 1
#undef LV_USE_THORVG_INTERNAL
#define LV_USE_THORVG_INTERNAL 1
#undef LV_USE_LOTTIE
#define LV_USE_LOTTIE 1

#ifndef PYCPARSER
    void vk_ra6m3_lvgl_gc_init(void);
    void vk_ra6m3_lvgl_gc_deinit(void);
    #ifdef __cplusplus
    extern "C" void vk_ra6m3_thorvg_gc_root_set(unsigned int slot, void *ptr);
    #else
    void vk_ra6m3_thorvg_gc_root_set(unsigned int slot, void *ptr);
    #endif
    #undef LV_GC_INIT
    #define LV_GC_INIT() vk_ra6m3_lvgl_gc_init()
    #define LV_GC_DEINIT() vk_ra6m3_lvgl_gc_deinit()
    #define LV_THORVG_SW_MPOOL_ROOT(ptr) vk_ra6m3_thorvg_gc_root_set(0U, (ptr))
    #define LV_THORVG_TASK_SCHEDULER_ROOT(ptr) vk_ra6m3_thorvg_gc_root_set(1U, (ptr))
#endif

#endif
