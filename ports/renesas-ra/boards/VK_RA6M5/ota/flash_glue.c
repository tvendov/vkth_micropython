/*
 * VK_RA6M5 OTA flash helper — exposed to MicroPython as `_ota`.
 *
 * Provides three primitives:
 *   _ota.erase_staging()              -> bool  (erases all 16 sectors)
 *   _ota.write(offset, bytes)         -> bool  (writes at 0x110000 + offset)
 *   _ota.commit()                     -> NoReturn  (flag swap + reset)
 *
 * These run from the active firmware (linked at 0x00010000) and program
 * the staging region at 0x00110000.  R_FLASH_HP_Write is placed by FSP
 * in a section that must reside in SRAM, so all flash operations work
 * fine even though the caller is itself executing from internal flash —
 * just keep IRQs disabled for the duration so an ISR doesn't dive back
 * into the code-flash region while it's being programmed in another bank.
 *
 * To enable: add this file and mod_ota.c to the renesas-ra Makefile
 * under USE_OTA=1, plus #define MICROPY_PY_OTA (1).
 */

#include <stdint.h>
#include <string.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/mphal.h"
#include "hal_data.h"
#include "r_flash_hp.h"

#define APP_BASE              (0x00010000UL)
#define APP_SIZE              (0x00100000UL)
#define STAGING_BASE          (0x00110000UL)
#define STAGING_SIZE          (0x00080000UL)
#define STAGING_FLAG_ADDR     (STAGING_BASE)
#define OTA_MAGIC_REQUEST_SWAP  (0x5741504DU)
#define FLASH_HP_CF_WRITE_SIZE  (128U)        /* RA6M5 code flash unit */

/* Provided by the FSP-generated hal_data.c — instantiate the flash_hp
 * module via the e2 studio Smart Configurator.  If you have not added
 * it yet, the link step will surface the missing symbols. */
extern flash_hp_instance_ctrl_t g_flash_hp_ctrl;
extern const flash_cfg_t        g_flash_hp_cfg;

static bool flash_open_done = false;

static fsp_err_t flash_open_once(void) {
    if (!flash_open_done) {
        fsp_err_t err = R_FLASH_HP_Open(&g_flash_hp_ctrl, &g_flash_hp_cfg);
        if (err != FSP_SUCCESS) return err;
        flash_open_done = true;
    }
    return FSP_SUCCESS;
}

static mp_obj_t ota_erase_staging(void) {
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    /* RA6M5 code-flash sector = 32 KB → STAGING_SIZE / 0x8000 sectors. */
    uint32_t state = mp_hal_quiet_timing_enter();
    fsp_err_t err = R_FLASH_HP_Erase(&g_flash_hp_ctrl, STAGING_BASE,
                                     STAGING_SIZE / 0x8000U);
    mp_hal_quiet_timing_exit(state);
    return mp_obj_new_bool(err == FSP_SUCCESS);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ota_erase_staging_obj, ota_erase_staging);

static mp_obj_t ota_write(mp_obj_t offset_in, mp_obj_t data_in) {
    uint32_t offset = mp_obj_get_int(offset_in);
    mp_buffer_info_t bi;
    mp_get_buffer_raise(data_in, &bi, MP_BUFFER_READ);

    if (offset + bi.len > STAGING_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("write out of staging area"));
    }
    if ((bi.len % FLASH_HP_CF_WRITE_SIZE) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("len must be multiple of 128"));
    }
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    uint32_t state = mp_hal_quiet_timing_enter();
    fsp_err_t err = R_FLASH_HP_Write(&g_flash_hp_ctrl,
                                     (uint32_t)bi.buf,
                                     STAGING_BASE + offset, bi.len);
    mp_hal_quiet_timing_exit(state);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(ota_write_obj, ota_write);

static mp_obj_t ota_commit(void) {
    /* Set the swap flag and reset.  The flag occupies the first 128 bytes
     * of the staging area; the rest of the image is shifted up by 128 bytes,
     * so write a flag block followed by NOPs first, then the firmware.
     *
     * In this simple scheme the user has to leave the first 128 bytes of
     * the staging area free — the helper writes the flag word there. */
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    uint32_t buf[FLASH_HP_CF_WRITE_SIZE / 4] __attribute__((aligned(4))) = {
        OTA_MAGIC_REQUEST_SWAP, 0, 0, 0,
    };
    for (size_t i = 4; i < sizeof(buf) / sizeof(buf[0]); ++i) {
        buf[i] = 0xFFFFFFFFu;
    }
    /* Flag is at the *very end* of the first staging sector, so the actual
     * image at STAGING_BASE+128 onward is not overlapped.  TODO: re-think
     * if firmware images are bigger than (STAGING_SIZE - 128). */
    uint32_t state = mp_hal_quiet_timing_enter();
    fsp_err_t err = R_FLASH_HP_Write(&g_flash_hp_ctrl,
                                     (uint32_t)buf,
                                     STAGING_FLAG_ADDR,
                                     FLASH_HP_CF_WRITE_SIZE);
    mp_hal_quiet_timing_exit(state);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    /* Reset.  Bootloader will see the flag and swap. */
    NVIC_SystemReset();
    return mp_const_none;                    /* unreachable */
}
static MP_DEFINE_CONST_FUN_OBJ_0(ota_commit_obj, ota_commit);

static const mp_rom_map_elem_t ota_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),       MP_ROM_QSTR(MP_QSTR__ota) },
    { MP_ROM_QSTR(MP_QSTR_erase_staging),  MP_ROM_PTR(&ota_erase_staging_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),          MP_ROM_PTR(&ota_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_commit),         MP_ROM_PTR(&ota_commit_obj) },
    { MP_ROM_QSTR(MP_QSTR_STAGING_BASE),   MP_ROM_INT(STAGING_BASE) },
    { MP_ROM_QSTR(MP_QSTR_STAGING_SIZE),   MP_ROM_INT(STAGING_SIZE) },
};
static MP_DEFINE_CONST_DICT(ota_module_globals, ota_module_globals_table);

const mp_obj_module_t mp_module_ota = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ota_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR__ota, mp_module_ota);
