#ifndef MICROPY_INCLUDED_RENESAS_RA_BOARDS_VK_RA6M3_DAVE2D_COMPAT_H
#define MICROPY_INCLUDED_RENESAS_RA_BOARDS_VK_RA6M3_DAVE2D_COMPAT_H

#include "lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.h"

#if LV_USE_DRAW_DAVE2D
void lv_draw_dave2d_fill_single(lv_draw_task_t *task, const lv_draw_fill_dsc_t *draw_dsc,
    const lv_area_t *coords);
#endif

#endif
