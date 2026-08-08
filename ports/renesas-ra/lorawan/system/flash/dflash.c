/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/

#include "board.h"
#include "dflash.h"

static volatile bool g_vee_callback_called = false;

bool rp_veeprom_write(uint8_t id, uint8_t * p_src, size_t length) {

    fsp_err_t err, err2;

    err = RM_VEE_FLASH_Open(&g_vee0_ctrl, &g_vee0_cfg);
    if (FSP_SUCCESS == err) {

        g_vee_callback_called = false;
        err = RM_VEE_FLASH_RecordWrite(&g_vee0_ctrl, id, p_src, length);
        if (FSP_SUCCESS == err) {
            while (false == g_vee_callback_called) {
                ;
            }
        }
    }

    err2 = RM_VEE_FLASH_Close(&g_vee0_ctrl);
    if (FSP_SUCCESS == err)
    {
        err = err2;
    }

    if (FSP_SUCCESS == err) {
        return (true);
    } else {
        return (false);
    }
}

bool rp_veeprom_read(uint8_t id, uint8_t * p_dst, size_t length) {

    fsp_err_t err, err2;
    uint32_t size;
    uint8_t * p_src;

    err = RM_VEE_FLASH_Open(&g_vee0_ctrl, &g_vee0_cfg);
    if (FSP_SUCCESS == err) {

        err = RM_VEE_FLASH_RecordPtrGet(&g_vee0_ctrl, id, (uint8_t **)&p_src, &size);
        if (FSP_SUCCESS == err) {
            
            if (length <= size) {
                memcpy(p_dst, p_src, length);
            } else {
                err = FSP_ERR_INVALID_ARGUMENT;
            }
        }
    }

    err2 = RM_VEE_FLASH_Close(&g_vee0_ctrl);
    if (FSP_SUCCESS == err)
    {
        err = err2;
    }

    if (FSP_SUCCESS == err) {
        return (true);
    } else {
        return (false);
    }
}

bool rp_veeprom_format( void ) {

    fsp_err_t err, err2;

    err = RM_VEE_FLASH_Open(&g_vee0_ctrl, &g_vee0_cfg);
    if (FSP_SUCCESS == err) {

        err = RM_VEE_FLASH_Format (&g_vee0_ctrl, NULL);
    }

    err2 = RM_VEE_FLASH_Close(&g_vee0_ctrl);
    if (FSP_SUCCESS == err)
    {
        err = err2;
    }

    if (FSP_SUCCESS == err) {
        return (true);
    } else {
        return (false);
    }
}

void vee_callback (rm_vee_callback_args_t * p_args)
{
    g_vee_callback_called = true;
    FSP_PARAMETER_NOT_USED(p_args);
}


#if defined(LORAWAN_VERSION_1_0_4) || defined(LORAWAN_VERSION_1_0_3) || defined(PRVLORA_ENABLED)

static volatile bool g_vee1_callback_called = false;

bool rp_veeprom1_write(uint8_t id, uint8_t * p_src, size_t length) {

    fsp_err_t err, err2;

    err = RM_VEE_FLASH_Open(&g_vee1_ctrl, &g_vee1_cfg);
    if (FSP_SUCCESS == err) {
        
        g_vee1_callback_called = false;
        err = RM_VEE_FLASH_RecordWrite(&g_vee1_ctrl, id, p_src, length);
        if (FSP_SUCCESS == err) {
            while (false == g_vee1_callback_called) {
                ;
            }
        }
    }

    err2 = RM_VEE_FLASH_Close(&g_vee1_ctrl);
    if (FSP_SUCCESS == err)
    {
        err = err2;
    }

    if (FSP_SUCCESS == err) {
        return (true);
    } else {
        return (false);
    }
}

bool rp_veeprom1_read(uint8_t id, uint8_t * p_dst, size_t length) {

    fsp_err_t err, err2;
    uint32_t size;
    uint8_t * p_src;

    err = RM_VEE_FLASH_Open(&g_vee1_ctrl, &g_vee1_cfg);
    if (FSP_SUCCESS == err) {

        err = RM_VEE_FLASH_RecordPtrGet(&g_vee1_ctrl, id, (uint8_t **)&p_src, &size);
        if (FSP_SUCCESS == err) {
            if (length <= size) {
                memcpy(p_dst, p_src, length);
            } else {
                err = FSP_ERR_INVALID_ARGUMENT;
            }
        }
    }

    err2 = RM_VEE_FLASH_Close(&g_vee1_ctrl);
    if (FSP_SUCCESS == err)
    {
        err = err2;
    }

    if (FSP_SUCCESS == err) {
        return (true);
    } else {
        return (false);
    }
}

bool rp_veeprom1_format( void ) {

    fsp_err_t err, err2;

    err = RM_VEE_FLASH_Open(&g_vee1_ctrl, &g_vee1_cfg);
    if ((FSP_SUCCESS == err) || (FSP_ERR_ALREADY_OPEN == err)) {

        err = RM_VEE_FLASH_Format (&g_vee1_ctrl, NULL);
    }

    err2 = RM_VEE_FLASH_Close(&g_vee1_ctrl);
    if (FSP_SUCCESS == err)
    {
        err = err2;
    }

    if (FSP_SUCCESS == err) {
        return (true);
    } else {
        return (false);
    }
}
void vee1_callback (rm_vee_callback_args_t * p_args)
{
    g_vee1_callback_called = true;
    FSP_PARAMETER_NOT_USED(p_args);
}

#endif

