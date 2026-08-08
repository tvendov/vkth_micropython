// Data Flash access module for Renesas RA (FSP Flash API)
//
// Provides a small EEPROM-like byte storage API backed by MCU Data Flash.

#include "py/mpconfig.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"

#include <string.h>

#include "hal_data.h"   // pulls in bsp_api.h → R_BSP_FlashCache{Enable,Disable}, BSP_FEATURE_*

#include "ra_utils.h"

#ifndef MICROPY_HW_DATAFLASH_PARTITIONED
#define MICROPY_HW_DATAFLASH_PARTITIONED (0)
#endif

#if MICROPY_HW_DATAFLASH_PARTITIONED
#include "lorawan/system/flash/dataflash_partition.h"
#endif

// FCACHE invalidation after data-flash erase/write.
//
// R_FLASH_HP calls R_BSP_FlashCacheEnable() only when exiting Code Flash P/E mode.
// For Data Flash it only touches R_CACHE->CCAFCT (guarded by BSP_FEATURE_BSP_HAS_CODE_SYSTEM_CACHE)
// which is 0 on RA4M2.  BSP_FEATURE_BSP_FLASH_CACHE is 1 on RA4M2, so FCACHE is active but
// never re-validated after DF writes.  AHB reads of 0x08000000+ then return stale cache lines.
// Fix: disable → invalidate → re-enable around every operation that reads DF via AHB pointer.
static inline void dataflash_fcache_sync(void) {
#if BSP_FEATURE_BSP_FLASH_CACHE
    __DSB();
    R_BSP_FlashCacheDisable();   // FCACHEE = 0
    R_BSP_FlashCacheEnable();    // FCACHEIV = 1 → wait → FCACHEE = 1
    __DSB();
    __ISB();
#endif
}

// MICROPY_EVENT_POLL_HOOK is not guaranteed to be provided by all ports.
#ifndef MICROPY_EVENT_POLL_HOOK
#define MICROPY_EVENT_POLL_HOOK do { } while (0)
#endif

typedef struct {
    uint32_t start;
    uint32_t size;
    uint32_t erase_block_size;
    uint32_t write_size;
} dataflash_info_t;

#if MICROPY_HW_DATAFLASH_PARTITIONED
// Region-scoped view object returned by dataflash.region(name). Carries an
// absolute (start,size) window into data flash; all read/write/erase offsets
// are relative to `start` and bounds-checked against `size`. The default
// (un-scoped) module functions use the APP partition.
typedef struct _dataflash_view_obj_t {
    mp_obj_base_t base;
    uint32_t start;
    uint32_t size;
    const char *name;
} dataflash_view_obj_t;

static const mp_obj_type_t dataflash_view_type;
#endif

static void dataflash_wait_idle_raise(uint32_t timeout_ms) {
    uint32_t t0 = mp_hal_ticks_ms();
    for (;;) {
        flash_status_t status;
        fsp_err_t err = g_flash0.p_api->statusGet(g_flash0.p_ctrl, &status);
        if (err != FSP_SUCCESS) {
            mp_raise_OSError(MP_EIO);
        }
        if (status == FLASH_STATUS_IDLE) {
            return;
        }
        if ((uint32_t) (mp_hal_ticks_ms() - t0) > timeout_ms) {
            mp_raise_OSError(MP_ETIMEDOUT);
        }
        MICROPY_EVENT_POLL_HOOK;
    }
}

static void dataflash_blankcheck_raise(uint32_t addr, uint32_t len) {
    flash_result_t result = FLASH_RESULT_BLANK;
    fsp_err_t err = g_flash0.p_api->blankCheck(g_flash0.p_ctrl, addr, len, &result);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    if (result == FLASH_RESULT_BGO_ACTIVE) {
        // Should not happen with our current (non-BGO) flash config, but fail fast if it does.
        mp_raise_OSError(MP_EBUSY);
    }
    if (result != FLASH_RESULT_BLANK) {
        mp_raise_OSError(MP_EIO);
    }
}

static void dataflash_get_info_raise(dataflash_info_t * out) {
    flash_info_t info;
    fsp_err_t err = g_flash0.p_api->infoGet(g_flash0.p_ctrl, &info);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }

    if ((info.data_flash.num_regions < 1) || (info.data_flash.p_block_array == NULL)) {
        mp_raise_OSError(MP_ENODEV);
    }

    const flash_block_info_t * b = &info.data_flash.p_block_array[0];
    uint32_t start = b->block_section_st_addr;
    uint32_t end_inclusive = b->block_section_end_addr;
    uint32_t size = (end_inclusive >= start) ? (end_inclusive - start + 1U) : 0U;

    if ((size == 0U) || (b->block_size == 0U) || (b->block_size_write == 0U)) {
        mp_raise_OSError(MP_ENODEV);
    }

    out->start = start;
    out->size = size;
    out->erase_block_size = b->block_size;
    out->write_size = b->block_size_write;
}

#if MICROPY_HW_DATAFLASH_PARTITIONED
static void dataflash_validate_partition_raise(const dataflash_info_t *df) {
    if ((df->start != DF_BASE) || (df->size < DF_TOTAL_SIZE)
        || (df->erase_block_size != DF_ERASE_BLOCK)
        || (df->write_size != DF_WRITE_UNIT)) {
        mp_raise_OSError(MP_ENODEV);
    }
}
#endif

// The partitioned VK_RA4M2 build exposes APP as the default window. Other
// boards expose the complete data flash region reported by the FSP driver.
static void dataflash_default_window(uint32_t *start, uint32_t *size) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);
#if MICROPY_HW_DATAFLASH_PARTITIONED
    dataflash_validate_partition_raise(&df);
    *start = df.start + DF_APP_OFFSET;
    *size = DF_APP_SIZE;
#else
    *start = df.start;
    *size = df.size;
#endif
}

// ---- Window-aware workers ------------------------------------------------
//
// Each takes an absolute (win_start, win_size) data-flash window. Offsets are
// relative to win_start and bounds-checked against win_size.

static mp_obj_t dataflash_size_win(uint32_t win_start, uint32_t win_size) {
    (void) win_start;
    return mp_obj_new_int_from_uint(win_size);
}

static mp_obj_t dataflash_block_size_win(void) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);
    return mp_obj_new_int_from_uint(df.erase_block_size);
}

static mp_obj_t dataflash_write_size_win(void) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);
    return mp_obj_new_int_from_uint(df.write_size);
}

static mp_obj_t dataflash_read_win(uint32_t win_start, uint32_t win_size,
    mp_obj_t off_in, mp_obj_t len_in) {
    size_t off = (size_t) mp_obj_get_int(off_in);
    size_t len = (size_t) mp_obj_get_int(len_in);
    if (off > win_size || len > (win_size - off)) {
        mp_raise_ValueError(MP_ERROR_TEXT("out of range"));
    }

    // Flush any stale FCACHE lines before reading DF via AHB pointer.
    dataflash_fcache_sync();
    const uint8_t * src = (const uint8_t *) (win_start + (uint32_t) off);
    return mp_obj_new_bytes(src, len);
}

#if MICROPY_HW_DATAFLASH_PARTITIONED
// Read into a caller-provided writable buffer (zero alloc). Returns the byte
// count read (== min(len(buf), win_size - off)). Used by the demo NVM restore
// hot path to avoid allocating a fresh bytes for the ~1.4 KB blob.
static mp_obj_t dataflash_readinto_win(uint32_t win_start, uint32_t win_size,
    mp_obj_t off_in, mp_obj_t buf_in) {
    size_t off = (size_t) mp_obj_get_int(off_in);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    size_t len = bufinfo.len;

    if (off > win_size) {
        mp_raise_ValueError(MP_ERROR_TEXT("out of range"));
    }
    if (len > (win_size - off)) {
        len = win_size - off;
    }

    // Flush any stale FCACHE lines before reading DF via AHB pointer.
    dataflash_fcache_sync();
    const uint8_t * src = (const uint8_t *) (win_start + (uint32_t) off);
    memcpy(bufinfo.buf, src, len);
    return mp_obj_new_int_from_uint(len);
}
#endif

static mp_obj_t dataflash_erase_win(uint32_t win_start, uint32_t win_size) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);

    if ((win_start % df.erase_block_size) != 0U
        || (win_size % df.erase_block_size) != 0U) {
        mp_raise_OSError(MP_EIO);
    }
    uint32_t blocks = win_size / df.erase_block_size;
    if (blocks == 0U) {
        mp_raise_OSError(MP_ENODEV);
    }

    uint32_t state = ra_disable_irq();
    fsp_err_t err = g_flash0.p_api->erase(g_flash0.p_ctrl, win_start, blocks);
    ra_enable_irq(state);

    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }

    // Some configurations can return from erase() before the operation completes (BGO).
    // Ensure completion and verify blank state.
    // RA4M2 DF erase: ~4 ms per 64-byte block; 8 blocks max = ~32 ms total.
    dataflash_wait_idle_raise(500U + blocks * 20U);
    for (uint32_t i = 0; i < blocks; i++) {
        dataflash_blankcheck_raise(win_start + i * df.erase_block_size, df.erase_block_size);
    }
    dataflash_fcache_sync();
    return mp_const_none;
}

static mp_obj_t dataflash_erase_block_win(uint32_t win_start, uint32_t win_size,
    mp_obj_t index_in) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);

    if ((win_start % df.erase_block_size) != 0U
        || (win_size % df.erase_block_size) != 0U) {
        mp_raise_OSError(MP_EIO);
    }
    uint32_t blocks = win_size / df.erase_block_size;
    uint32_t index = (uint32_t) mp_obj_get_int(index_in);
    if (index >= blocks) {
        mp_raise_ValueError(MP_ERROR_TEXT("block out of range"));
    }

    uint32_t addr = win_start + index * df.erase_block_size;
    uint32_t state = ra_disable_irq();
    fsp_err_t err = g_flash0.p_api->erase(g_flash0.p_ctrl, addr, 1);
    ra_enable_irq(state);

    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }

    // RA4M2 DF single-block erase: ~4 ms.
    dataflash_wait_idle_raise(100U);
    dataflash_blankcheck_raise(addr, df.erase_block_size);
    dataflash_fcache_sync();
    return mp_const_none;
}

static mp_obj_t dataflash_is_blank_win(uint32_t win_start, uint32_t win_size,
    mp_obj_t off_in, mp_obj_t len_in) {
    uint32_t off = (uint32_t) mp_obj_get_int(off_in);
    uint32_t len = (uint32_t) mp_obj_get_int(len_in);

    if (len == 0U) {
        return mp_const_true;
    }

    if (off > win_size || len > (win_size - off)) {
        mp_raise_ValueError(MP_ERROR_TEXT("out of range"));
    }

    flash_result_t result = FLASH_RESULT_BLANK;
    fsp_err_t err = g_flash0.p_api->blankCheck(g_flash0.p_ctrl, win_start + off, len, &result);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    if (result == FLASH_RESULT_BGO_ACTIVE) {
        // Non-blocking (BGO) mode is not expected with our flash config.
        mp_raise_OSError(MP_EBUSY);
    }
    return mp_obj_new_bool(result == FLASH_RESULT_BLANK);
}

static mp_obj_t dataflash_write_win(uint32_t win_start, uint32_t win_size,
    mp_obj_t off_in, mp_obj_t buf_in) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);

    size_t off = (size_t) mp_obj_get_int(off_in);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    size_t len = bufinfo.len;

    if (off > win_size || len > (win_size - off)) {
        mp_raise_ValueError(MP_ERROR_TEXT("out of range"));
    }

    const uint8_t * src_user = (const uint8_t *) bufinfo.buf;
    uint32_t dst_start = win_start + (uint32_t) off;
    uint32_t dst_end = dst_start + (uint32_t) len;

    uint32_t w = df.write_size;
    if ((w & (w - 1U)) != 0U) {
        // Assume power-of-two write sizes.
        mp_raise_OSError(MP_EIO);
    }

    uint32_t aligned_start = dst_start & ~(w - 1U);
    uint32_t aligned_end = (dst_end + (w - 1U)) & ~(w - 1U);

    // Chunked program buffer to keep stack usage bounded.
    enum { DATAFLASH_MAX_CHUNK = 256 };
    uint8_t tmp[DATAFLASH_MAX_CHUNK] __attribute__((aligned(4)));

    uint32_t addr = aligned_start;
    while (addr < aligned_end) {
        uint32_t remaining = aligned_end - addr;
        uint32_t seg_len = remaining > DATAFLASH_MAX_CHUNK ? DATAFLASH_MAX_CHUNK : remaining;
        seg_len -= (seg_len % w);
        if (seg_len == 0U) {
            mp_raise_OSError(MP_EIO);
        }

        // Per RA4M2 §44.16.2: AHB reads from erased-but-not-yet-programmed data flash
        // return undefined values, so we cannot use the AHB pointer to determine the
        // old cell contents.  FACI blankCheck is authoritative and must be consulted
        // first to decide whether RMW is needed at all.
        flash_result_t blank_result = FLASH_RESULT_NOT_BLANK;
        fsp_err_t bc_err = g_flash0.p_api->blankCheck(g_flash0.p_ctrl, addr, seg_len, &blank_result);
        // On error, be conservative: treat the region as non-blank so the RMW guard
        // still runs and rejects any 0->1 transition attempt.
        bool region_is_blank = (bc_err == FSP_SUCCESS) && (blank_result == FLASH_RESULT_BLANK);

        // Overlap of this segment with the user write range.
        uint32_t seg_end = addr + seg_len;
        uint32_t ov_start = (addr > dst_start) ? addr : dst_start;
        uint32_t ov_end = (seg_end < dst_end) ? seg_end : dst_end;

        if (region_is_blank) {
            // Region is erased: skip the AHB read entirely (undefined per §44.16.2).
            // Initialize tmp to 0xFF (erased state) then overlay the user data.
            memset(tmp, 0xFF, seg_len);
            if (ov_start < ov_end) {
                size_t tmp_off = (size_t) (ov_start - addr);
                size_t src_off = (size_t) (ov_start - dst_start);
                size_t ov_len = (size_t) (ov_end - ov_start);
                memcpy(tmp + tmp_off, src_user + src_off, ov_len);
            }
        } else {
            // Region has programmed data: RMW with guard to prevent illegal 0->1 transitions.
            // FCACHE must be coherent before reading DF contents via AHB pointer.
            dataflash_fcache_sync();
            const uint8_t * flash_ptr = (const uint8_t *) addr;
            memcpy(tmp, flash_ptr, seg_len);

            if (ov_start < ov_end) {
                size_t tmp_off = (size_t) (ov_start - addr);
                size_t src_off = (size_t) (ov_start - dst_start);
                size_t ov_len = (size_t) (ov_end - ov_start);

                // Check that we never try to set bits from 0 -> 1 without an erase.
                for (size_t i = 0; i < ov_len; i++) {
                    uint8_t oldb = tmp[tmp_off + i];
                    uint8_t newb = src_user[src_off + i];
                    if ((oldb & newb) != newb) {
                        mp_raise_OSError(MP_EIO);
                    }
                    tmp[tmp_off + i] = newb;
                }
            }
        }

        uint32_t state = ra_disable_irq();
        fsp_err_t err = g_flash0.p_api->write(g_flash0.p_ctrl, (uint32_t) tmp, addr, seg_len);
        ra_enable_irq(state);

        if (err != FSP_SUCCESS) {
            mp_raise_OSError(MP_EIO);
        }

        // Ensure completion for possible BGO configurations.
        // RA4M2 DF write: ~1 ms per 4-byte unit; 256-byte chunk = ~64 ms worst-case → 50 ms conservative.
        dataflash_wait_idle_raise(50U);
        // Invalidate FCACHE so next iteration's RMW read sees the just-written data.
        dataflash_fcache_sync();

        addr += seg_len;
    }

    return mp_obj_new_int_from_uint(len);
}

// ---- Default module functions --------------------------------------------

static mp_obj_t dataflash_size(void) {
    uint32_t s, n;
    dataflash_default_window(&s, &n);
    return dataflash_size_win(s, n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(dataflash_size_obj, dataflash_size);

static mp_obj_t dataflash_block_size(void) {
    return dataflash_block_size_win();
}
static MP_DEFINE_CONST_FUN_OBJ_0(dataflash_block_size_obj, dataflash_block_size);

static mp_obj_t dataflash_write_size(void) {
    return dataflash_write_size_win();
}
static MP_DEFINE_CONST_FUN_OBJ_0(dataflash_write_size_obj, dataflash_write_size);

static mp_obj_t dataflash_read(mp_obj_t off_in, mp_obj_t len_in) {
    uint32_t s, n;
    dataflash_default_window(&s, &n);
    return dataflash_read_win(s, n, off_in, len_in);
}
static MP_DEFINE_CONST_FUN_OBJ_2(dataflash_read_obj, dataflash_read);

static mp_obj_t dataflash_write(mp_obj_t off_in, mp_obj_t buf_in) {
    uint32_t s, n;
    dataflash_default_window(&s, &n);
    return dataflash_write_win(s, n, off_in, buf_in);
}
static MP_DEFINE_CONST_FUN_OBJ_2(dataflash_write_obj, dataflash_write);

static mp_obj_t dataflash_erase(void) {
    uint32_t s, n;
    dataflash_default_window(&s, &n);
    return dataflash_erase_win(s, n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(dataflash_erase_obj, dataflash_erase);

static mp_obj_t dataflash_erase_block(mp_obj_t index_in) {
    uint32_t s, n;
    dataflash_default_window(&s, &n);
    return dataflash_erase_block_win(s, n, index_in);
}
static MP_DEFINE_CONST_FUN_OBJ_1(dataflash_erase_block_obj, dataflash_erase_block);

static mp_obj_t dataflash_is_blank(mp_obj_t off_in, mp_obj_t len_in) {
    uint32_t s, n;
    dataflash_default_window(&s, &n);
    return dataflash_is_blank_win(s, n, off_in, len_in);
}
static MP_DEFINE_CONST_FUN_OBJ_2(dataflash_is_blank_obj, dataflash_is_blank);

#if MICROPY_HW_DATAFLASH_PARTITIONED
// ---- Region view object --------------------------------------------------

static mp_obj_t dataflash_view_size(mp_obj_t self_in) {
    dataflash_view_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return dataflash_size_win(self->start, self->size);
}
static MP_DEFINE_CONST_FUN_OBJ_1(dataflash_view_size_obj, dataflash_view_size);

static mp_obj_t dataflash_view_block_size(mp_obj_t self_in) {
    (void) self_in;
    return dataflash_block_size_win();
}
static MP_DEFINE_CONST_FUN_OBJ_1(dataflash_view_block_size_obj, dataflash_view_block_size);

static mp_obj_t dataflash_view_write_size(mp_obj_t self_in) {
    (void) self_in;
    return dataflash_write_size_win();
}
static MP_DEFINE_CONST_FUN_OBJ_1(dataflash_view_write_size_obj, dataflash_view_write_size);

static mp_obj_t dataflash_view_read(mp_obj_t self_in, mp_obj_t off_in, mp_obj_t len_in) {
    dataflash_view_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return dataflash_read_win(self->start, self->size, off_in, len_in);
}
static MP_DEFINE_CONST_FUN_OBJ_3(dataflash_view_read_obj, dataflash_view_read);

static mp_obj_t dataflash_view_readinto(mp_obj_t self_in, mp_obj_t off_in, mp_obj_t buf_in) {
    dataflash_view_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return dataflash_readinto_win(self->start, self->size, off_in, buf_in);
}
static MP_DEFINE_CONST_FUN_OBJ_3(dataflash_view_readinto_obj, dataflash_view_readinto);

static mp_obj_t dataflash_view_write(mp_obj_t self_in, mp_obj_t off_in, mp_obj_t buf_in) {
    dataflash_view_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return dataflash_write_win(self->start, self->size, off_in, buf_in);
}
static MP_DEFINE_CONST_FUN_OBJ_3(dataflash_view_write_obj, dataflash_view_write);

static mp_obj_t dataflash_view_erase(mp_obj_t self_in) {
    dataflash_view_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return dataflash_erase_win(self->start, self->size);
}
static MP_DEFINE_CONST_FUN_OBJ_1(dataflash_view_erase_obj, dataflash_view_erase);

static mp_obj_t dataflash_view_erase_block(mp_obj_t self_in, mp_obj_t index_in) {
    dataflash_view_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return dataflash_erase_block_win(self->start, self->size, index_in);
}
static MP_DEFINE_CONST_FUN_OBJ_2(dataflash_view_erase_block_obj, dataflash_view_erase_block);

static mp_obj_t dataflash_view_is_blank(mp_obj_t self_in, mp_obj_t off_in, mp_obj_t len_in) {
    dataflash_view_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return dataflash_is_blank_win(self->start, self->size, off_in, len_in);
}
static MP_DEFINE_CONST_FUN_OBJ_3(dataflash_view_is_blank_obj, dataflash_view_is_blank);

static void dataflash_view_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void) kind;
    dataflash_view_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "dataflash.region('%s', start=0x%08x, size=%u)",
        self->name, (unsigned int) self->start, (unsigned int) self->size);
}

static const mp_rom_map_elem_t dataflash_view_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_size),        MP_ROM_PTR(&dataflash_view_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_block_size),  MP_ROM_PTR(&dataflash_view_block_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_size),  MP_ROM_PTR(&dataflash_view_write_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_read),        MP_ROM_PTR(&dataflash_view_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto),    MP_ROM_PTR(&dataflash_view_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),       MP_ROM_PTR(&dataflash_view_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_erase),       MP_ROM_PTR(&dataflash_view_erase_obj) },
    { MP_ROM_QSTR(MP_QSTR_erase_block), MP_ROM_PTR(&dataflash_view_erase_block_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_blank),    MP_ROM_PTR(&dataflash_view_is_blank_obj) },
};
static MP_DEFINE_CONST_DICT(dataflash_view_locals_dict, dataflash_view_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    dataflash_view_type,
    MP_QSTR_DataFlashRegion,
    MP_TYPE_FLAG_NONE,
    print, dataflash_view_print,
    locals_dict, &dataflash_view_locals_dict
);

static mp_obj_t dataflash_region(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    dataflash_info_t df;
    dataflash_get_info_raise(&df);
    dataflash_validate_partition_raise(&df);
    for (size_t i = 0; i < DF_REGION_COUNT; i++) {
        if (strcmp(name, df_partition_map[i].name) == 0) {
            dataflash_view_obj_t *view = mp_obj_malloc(dataflash_view_obj_t, &dataflash_view_type);
            view->start = df.start + df_partition_map[i].offset;
            view->size = df_partition_map[i].size;
            view->name = df_partition_map[i].name;
            return MP_OBJ_FROM_PTR(view);
        }
    }
    mp_raise_ValueError(MP_ERROR_TEXT("unknown region"));
}
static MP_DEFINE_CONST_FUN_OBJ_1(dataflash_region_obj, dataflash_region);
#endif

static const mp_rom_map_elem_t dataflash_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),      MP_ROM_QSTR(MP_QSTR_dataflash) },
    { MP_ROM_QSTR(MP_QSTR_size),          MP_ROM_PTR(&dataflash_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_block_size),    MP_ROM_PTR(&dataflash_block_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_size),    MP_ROM_PTR(&dataflash_write_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_read),          MP_ROM_PTR(&dataflash_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),         MP_ROM_PTR(&dataflash_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_erase),         MP_ROM_PTR(&dataflash_erase_obj) },
    { MP_ROM_QSTR(MP_QSTR_erase_block),   MP_ROM_PTR(&dataflash_erase_block_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_blank),      MP_ROM_PTR(&dataflash_is_blank_obj) },
#if MICROPY_HW_DATAFLASH_PARTITIONED
    { MP_ROM_QSTR(MP_QSTR_region),        MP_ROM_PTR(&dataflash_region_obj) },
#endif
};
static MP_DEFINE_CONST_DICT(dataflash_module_globals, dataflash_module_globals_table);

const mp_obj_module_t mp_module_dataflash = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *) &dataflash_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_dataflash, mp_module_dataflash);
