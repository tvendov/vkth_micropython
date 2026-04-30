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

static mp_obj_t dataflash_size(void) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);
    return mp_obj_new_int_from_uint(df.size);
}
static MP_DEFINE_CONST_FUN_OBJ_0(dataflash_size_obj, dataflash_size);

static mp_obj_t dataflash_block_size(void) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);
    return mp_obj_new_int_from_uint(df.erase_block_size);
}
static MP_DEFINE_CONST_FUN_OBJ_0(dataflash_block_size_obj, dataflash_block_size);

static mp_obj_t dataflash_write_size(void) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);
    return mp_obj_new_int_from_uint(df.write_size);
}
static MP_DEFINE_CONST_FUN_OBJ_0(dataflash_write_size_obj, dataflash_write_size);

static mp_obj_t dataflash_read(mp_obj_t off_in, mp_obj_t len_in) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);

    size_t off = (size_t) mp_obj_get_int(off_in);
    size_t len = (size_t) mp_obj_get_int(len_in);
    if (off > df.size || len > (df.size - off)) {
        mp_raise_ValueError(MP_ERROR_TEXT("out of range"));
    }

    // Flush any stale FCACHE lines before reading DF via AHB pointer.
    dataflash_fcache_sync();
    const uint8_t * src = (const uint8_t *) (df.start + (uint32_t) off);
    return mp_obj_new_bytes(src, len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(dataflash_read_obj, dataflash_read);

static mp_obj_t dataflash_erase(void) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);

    if ((df.size % df.erase_block_size) != 0U) {
        mp_raise_OSError(MP_EIO);
    }
    uint32_t blocks = df.size / df.erase_block_size;
    if (blocks == 0U) {
        mp_raise_OSError(MP_ENODEV);
    }

    uint32_t state = ra_disable_irq();
    fsp_err_t err = g_flash0.p_api->erase(g_flash0.p_ctrl, df.start, blocks);
    ra_enable_irq(state);

    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }

    // Some configurations can return from erase() before the operation completes (BGO).
    // Ensure completion and verify blank state.
    // RA4M2 DF erase: ~4 ms per 64-byte block; 8 blocks max = ~32 ms total.
    dataflash_wait_idle_raise(500U + blocks * 20U);
    for (uint32_t i = 0; i < blocks; i++) {
        dataflash_blankcheck_raise(df.start + i * df.erase_block_size, df.erase_block_size);
    }
    dataflash_fcache_sync();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(dataflash_erase_obj, dataflash_erase);

static mp_obj_t dataflash_erase_block(mp_obj_t index_in) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);

    if ((df.size % df.erase_block_size) != 0U) {
        mp_raise_OSError(MP_EIO);
    }
    uint32_t blocks = df.size / df.erase_block_size;
    uint32_t index = (uint32_t) mp_obj_get_int(index_in);
    if (index >= blocks) {
        mp_raise_ValueError(MP_ERROR_TEXT("block out of range"));
    }

    uint32_t addr = df.start + index * df.erase_block_size;
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
static MP_DEFINE_CONST_FUN_OBJ_1(dataflash_erase_block_obj, dataflash_erase_block);

static mp_obj_t dataflash_is_blank(mp_obj_t off_in, mp_obj_t len_in) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);

    uint32_t off = (uint32_t) mp_obj_get_int(off_in);
    uint32_t len = (uint32_t) mp_obj_get_int(len_in);

    if (len == 0U) {
        return mp_const_true;
    }

    if (off > df.size || len > (df.size - off)) {
        mp_raise_ValueError(MP_ERROR_TEXT("out of range"));
    }

    // Per r_flash_hp.c r_flash_hp_write_bc_parameter_checking(): data flash blankCheck
    // requires 4-byte aligned address and length that is a multiple of 4.
    if (((off & 3U) != 0U) || ((len & 3U) != 0U)) {
        mp_raise_ValueError(MP_ERROR_TEXT("alignment: offset and length must be 4-byte aligned"));
    }

    flash_result_t result = FLASH_RESULT_BLANK;
    fsp_err_t err = g_flash0.p_api->blankCheck(g_flash0.p_ctrl, df.start + off, len, &result);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    if (result == FLASH_RESULT_BGO_ACTIVE) {
        // Non-blocking (BGO) mode is not expected with our flash config.
        mp_raise_OSError(MP_EBUSY);
    }
    return mp_obj_new_bool(result == FLASH_RESULT_BLANK);
}
static MP_DEFINE_CONST_FUN_OBJ_2(dataflash_is_blank_obj, dataflash_is_blank);

static mp_obj_t dataflash_write(mp_obj_t off_in, mp_obj_t buf_in) {
    dataflash_info_t df;
    dataflash_get_info_raise(&df);

    size_t off = (size_t) mp_obj_get_int(off_in);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    size_t len = bufinfo.len;

    if (off > df.size || len > (df.size - off)) {
        mp_raise_ValueError(MP_ERROR_TEXT("out of range"));
    }

    const uint8_t * src_user = (const uint8_t *) bufinfo.buf;
    uint32_t dst_start = df.start + (uint32_t) off;
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
static MP_DEFINE_CONST_FUN_OBJ_2(dataflash_write_obj, dataflash_write);

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
};
static MP_DEFINE_CONST_DICT(dataflash_module_globals, dataflash_module_globals_table);

const mp_obj_module_t mp_module_dataflash = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *) &dataflash_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_dataflash, mp_module_dataflash);
