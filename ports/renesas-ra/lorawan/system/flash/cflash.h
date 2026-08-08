/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __R_CFLASH_H__
#define __R_CFLASH_H__

/***********************************************************
 * include
 **********************************************************/

/***********************************************************
 * define
 **********************************************************/
/*----- return code -----*/
#define R_CFLASH_RESULT_SUCCESS     (0x00)
#define R_CFLASH_RESULT_FAILED      (0xFF)

/*----- (for internal use only) -----*/
#define R_CFLASH_FSQSTATUS_OK               (0x00u)
#define R_CFLASH_FSQSTATUS_ERR_ERASE        (0x01u)  // bit0
#define R_CFLASH_FSQSTATUS_ERR_WRITE        (0x02u)  // bit1
#define R_CFLASH_FSQSTATUS_ERR_BLANKCHECK   (0x08u)  // bit3
#define R_CFLASH_FSQSTATUS_ERR_CDDTSEQ      (0x10u)  // bit4
#define R_CFLASH_FSQSTATUS_ERR_EXSEQ        (0x20u)  // bit5

/*----- (FSP) -----*/
#define R_CFLASH_P_FLASH_LP_CTRL    (&g_flash2_ctrl)
#define R_CFLASH_P_FLASH_LP_CFG     (&g_flash2_cfg)

/*----- (Bootswap (startup area select)) -----*/
#if defined(FUOTA_ENABLED)
    #define R_CFLASH_STARTUPSEL_TO  FLASH_STARTUP_AREA_BLOCK1
#else
    #define R_CFLASH_STARTUPSEL_TO  FLASH_STARTUP_AREA_BLOCK0
#endif

/***********************************************************
 * typedef
 **********************************************************/

/***********************************************************
 * prototype
 **********************************************************/
// r_cflash_write.c
extern void R_CFlash_InitSct( void );
extern void R_CFlash_Update_Init( void );
extern uint8_t R_CFlash_AddWriteProtectArea( uint32_t cfAddrStart, uint32_t cfAddrEnd );
extern uint8_t R_CFlash_CheckWriteArea( uint32_t cfAddr, uint32_t cfSize );
extern uint8_t R_CFlash_Update_WriteData( uint32_t         cfAddr, 
                                          uint32_t         cfSize, 
                                          uint8_t *p_cfData, 
                                          uint8_t          isContinueData );
extern uint8_t R_CFlash_Update_WriteData_inBlock( uint32_t         cfAddr,
                                                  uint32_t         cfSize,
                                                  uint8_t *p_cfData,
                                                  uint8_t          isEraseBeforeWrite );
extern uint8_t R_CFlash_EraseBlock( uint32_t cfAddr );

// r_cflash_btswp.c
extern void R_CFlash_ResetFW( void );
extern uint8_t R_CFlash_SwitchBootCluster( void );

#endif /* __R_CFLASH_H__ */
/*******************************************************************************
 * Copyright (C) 2014-2018 Renesas Electronics Corporation.
 ******************************************************************************/

