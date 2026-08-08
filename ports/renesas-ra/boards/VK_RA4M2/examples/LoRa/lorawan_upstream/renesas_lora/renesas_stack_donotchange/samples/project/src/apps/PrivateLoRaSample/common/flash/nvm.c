/*
    (C) 2021 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/     

/******************************************************************************
   Include
******************************************************************************/
#include "board.h"

#include "dflash.h"
#include "nvm.h"


/******************************************************************************
   Private function prototypes
******************************************************************************/


/******************************************************************************
   Public function bodies
******************************************************************************/
/***********************************************************************
 * function name  : NmRead
 * description    : Read data from data flash
 * parameters     : 
 * return value   : 
 **********************************************************************/
uint8_t NvmRead( uint8_t dataId, uint8_t *p_dataDst, uint8_t dataLen )
{
    uint8_t status;

    if( RpMcuEepromRead( p_dataDst, dataLen, dataId ) )
    {
        status = NVM_RESULT_SUCCESS;
    }
    else
    {
        status  = NVM_RESULT_FAILED;    
    }
    
    return status;
}

/***********************************************************************
 * function name  : NvmWrite
 * description    : Write data to data flash
 * parameters     : 
 * return value   : 
 **********************************************************************/
uint8_t NvmWrite( uint8_t dataId, uint8_t *p_dataSrc, uint8_t dataLen )
{
    uint8_t status;

    if( RpMcuEepromWrite( p_dataSrc, dataLen, dataId ) )
    {
        status = NVM_RESULT_SUCCESS;
    }
    else
    {
        status = NVM_RESULT_FAILED;
    }

    return status;
}

