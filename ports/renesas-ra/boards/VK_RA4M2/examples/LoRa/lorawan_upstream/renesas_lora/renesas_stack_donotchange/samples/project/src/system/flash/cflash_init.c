/*
    (C) 2020-2022 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

/******************************************************************************
   Pragma
******************************************************************************/

/******************************************************************************
   Include
******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "hal_data.h"
#include "cflash.h"

#include "board.h"

static uint8_t  g_isInit = 0;

/******************************************************************************
   Exported function and global variables (to be accessed by other files)
******************************************************************************/
void R_CFlash_Init( void );

/******************************************************************************
   Public function bodies
******************************************************************************/
/***********************************************************************
 * function name  : R_CFlash_Init
 * description    : Initialization to use flash self programing library
 * parameters     : none
 * return value   : none
 **********************************************************************/
void R_CFlash_Init( void )
{
    if( g_isInit == 0 )
    {
        R_FLASH_LP_Open( R_CFLASH_P_FLASH_LP_CTRL, R_CFLASH_P_FLASH_LP_CFG );

        g_isInit = 1;
    }
}

/*******************************************************************************
 * Copyright (C) 2020-2022 Renesas Electronics Corporation.
 ******************************************************************************/
