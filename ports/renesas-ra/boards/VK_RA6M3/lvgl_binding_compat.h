#ifndef MICROPY_INCLUDED_VK_RA6M3_LVGL_BINDING_COMPAT_H
#define MICROPY_INCLUDED_VK_RA6M3_LVGL_BINDING_COMPAT_H

#include "lvgl/lvgl.h"

#if LV_USE_LOTTIE
/* LVGL 9.4 defines this widget class but omits its public declaration. */
LV_ATTRIBUTE_EXTERN_DATA extern const lv_obj_class_t lv_lottie_class;
#endif

#endif
