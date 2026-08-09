#ifndef MICROPY_INCLUDED_RENESAS_RA_BOARDS_VK_RA6M3_DAVE2D_PORT_H
#define MICROPY_INCLUDED_RENESAS_RA_BOARDS_VK_RA6M3_DAVE2D_PORT_H

#include <stdint.h>

void vk_ra6m3_dave2d_prepare_deinit(void);
void vk_ra6m3_dave2d_finish_deinit(void);
void vk_ra6m3_lvgl_gc_init(void);
void vk_ra6m3_lvgl_gc_deinit(void);
void vk_ra6m3_drw_int_isr(void);
void vk_ra6m3_dave2d_get_stats(uint32_t *irq_count, uint32_t *allocation_count,
    uint32_t *active_bytes, uint32_t *peak_bytes);

#endif
