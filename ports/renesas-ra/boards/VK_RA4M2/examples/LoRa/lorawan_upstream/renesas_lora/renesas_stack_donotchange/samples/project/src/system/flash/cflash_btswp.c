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

/******************************************************************************
   Macro definitions
******************************************************************************/

/******************************************************************************
   Imported global variables and functions (from other files)
******************************************************************************/
extern void R_CFlash_Init( void );  // r_cflash_init.c

/******************************************************************************
   Function prototypes
******************************************************************************/
void R_CFlash_ResetFW( void ) __attribute__((noinline)) PLACE_IN_RAM_SECTION;
uint8_t R_CFlash_SwitchBootCluster( void ) __attribute__((noinline)) PLACE_IN_RAM_SECTION;

/******************************************************************************
   Public function bodies
******************************************************************************/
/***********************************************************************
 * function name  : R_CFlash_ResetFW
 * description    : reset firmware
 * parameters     : none
 * return value   : none
 **********************************************************************/
void R_CFlash_ResetFW( void )
{
    NVIC_SystemReset();
    while(1);  // Reset will be occurred before comming here
}

/***********************************************************************
 * function name  : R_CFlash_SwitchBootCluster
 * description    : boot swap and reset
 * parameters     : none
 * return value   : R_CFLASH_RESULT_FAILED (only if boot swap is failed)
 **********************************************************************/
uint8_t R_CFlash_SwitchBootCluster( void )
{
    fsp_err_t   err;

    /*--- init flash lib ---*/
    R_CFlash_Init();

    /*--- Get current boot cluster ---*/
    err = R_FLASH_LP_StartUpAreaSelect( R_CFLASH_P_FLASH_LP_CTRL, 
                                        R_CFLASH_STARTUPSEL_TO, 
                                        false );
    if( err == FSP_SUCCESS)
    {
        R_CFlash_ResetFW();
    }

    return R_CFLASH_RESULT_FAILED;
}

/*******************************************************************************
 * Copyright (C) 2020 Renesas Electronics Corporation.
 ******************************************************************************/
