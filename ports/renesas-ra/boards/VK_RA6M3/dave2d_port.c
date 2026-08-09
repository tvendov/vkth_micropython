#include <stddef.h>
#include <stdint.h>

#include "py/misc.h"
#include "py/mpstate.h"

#include "dave2d_port.h"
#include "dave_driver.h"
#include "lvgl/src/core/lv_global.h"

typedef struct _vk_ra6m3_drw_allocation_t {
    struct _vk_ra6m3_drw_allocation_t *next;
    size_t size;
} vk_ra6m3_drw_allocation_t;

MP_REGISTER_ROOT_POINTER(void *vk_ra6m3_drw_allocations);

const uint8_t DRW_INT_IPL = 12;

static volatile uint32_t drw_irq_count;
static uint32_t drw_allocation_count;
static uint32_t drw_active_bytes;
static uint32_t drw_peak_bytes;

void *d1_malloc(size_t size) {
    if (size > SIZE_MAX - sizeof(vk_ra6m3_drw_allocation_t)) {
        return NULL;
    }

    vk_ra6m3_drw_allocation_t *allocation =
        m_malloc_maybe(sizeof(vk_ra6m3_drw_allocation_t) + size);
    if (allocation == NULL) {
        return NULL;
    }

    allocation->next = (vk_ra6m3_drw_allocation_t *)MP_STATE_PORT(vk_ra6m3_drw_allocations);
    allocation->size = size;
    MP_STATE_PORT(vk_ra6m3_drw_allocations) = allocation;
    drw_allocation_count++;
    drw_active_bytes += size;
    if (drw_active_bytes > drw_peak_bytes) {
        drw_peak_bytes = drw_active_bytes;
    }

    return allocation + 1;
}

void d1_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    vk_ra6m3_drw_allocation_t *allocation =
        ((vk_ra6m3_drw_allocation_t *)ptr) - 1;
    vk_ra6m3_drw_allocation_t **cursor =
        (vk_ra6m3_drw_allocation_t **)&MP_STATE_PORT(vk_ra6m3_drw_allocations);

    while (*cursor != NULL && *cursor != allocation) {
        cursor = &(*cursor)->next;
    }
    if (*cursor == NULL) {
        return;
    }

    *cursor = allocation->next;
    drw_allocation_count--;
    drw_active_bytes -= allocation->size;
    m_free(allocation);
}

void drw_int_isr(void);

void vk_ra6m3_drw_int_isr(void) {
    drw_irq_count++;
    drw_int_isr();
}

/* Keep the upstream renderer in the pinned LVGL submodule while adding the
 * lifecycle required by MicroPython soft resets in this board translation unit. */
#include "lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c"

void vk_ra6m3_lvgl_gc_init(void) {
    mp_lv_roots = MP_STATE_VM(mp_lv_roots) = m_new0(lv_global_t, 1);
}

void vk_ra6m3_lvgl_gc_deinit(void) {
    if (mp_lv_roots != NULL) {
        void *roots = mp_lv_roots;
        mp_lv_roots = NULL;
        MP_STATE_VM(mp_lv_roots) = NULL;
        m_del(lv_global_t, roots, 1);
    }
}

void lv_draw_dave2d_fill_single(lv_draw_task_t *task, const lv_draw_fill_dsc_t *draw_dsc,
    const lv_area_t *coords) {
    lv_draw_dave2d_fill(task, draw_dsc, coords);
}

void vk_ra6m3_dave2d_prepare_deinit(void) {
    if (_d2_handle != NULL) {
        if (!lv_ll_is_empty(&draw_tasks_on_dlist)) {
            dave2d_execute_dlist_and_flush();
        }
        d2_flushframe(_d2_handle);
    }
}

void vk_ra6m3_dave2d_finish_deinit(void) {
    if (_d2_handle != NULL) {
        if (_renderbuffer != NULL) {
            d2_freerenderbuffer(_d2_handle, _renderbuffer);
        }
        if (_label_renderbuffer != NULL) {
            d2_freerenderbuffer(_d2_handle, _label_renderbuffer);
        }
        d2_closedevice(_d2_handle);
        _d2_handle = NULL;
        _renderbuffer = NULL;
        _label_renderbuffer = NULL;
    }

    lv_ll_init(&draw_tasks_on_dlist, sizeof(uintptr_t));
    draw_pressure = 0;

    while (MP_STATE_PORT(vk_ra6m3_drw_allocations) != NULL) {
        vk_ra6m3_drw_allocation_t *allocation =
            (vk_ra6m3_drw_allocation_t *)MP_STATE_PORT(vk_ra6m3_drw_allocations);
        d1_free(allocation + 1);
    }
}

void vk_ra6m3_dave2d_get_stats(uint32_t *irq_count, uint32_t *allocation_count,
    uint32_t *active_bytes, uint32_t *peak_bytes) {
    *irq_count = drw_irq_count;
    *allocation_count = drw_allocation_count;
    *active_bytes = drw_active_bytes;
    *peak_bytes = drw_peak_bytes;
}
