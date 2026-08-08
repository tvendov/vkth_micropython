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
   definitions
******************************************************************************/
#define R_CFLASH_WRITE_TIMES            (3)

#define R_CFLASH_BlockSize              (2048)  //= 2KB
typedef struct {
    uint32_t    blockStartAddress;
    uint8_t     blockBuff[ R_CFLASH_BlockSize ];
} r_cflash_buffmng_t;

#define R_CFLASH_GET_BLOCK_STARTADDR( cfAddr )      ( (cfAddr) & (uint32_t)(~(R_CFLASH_BlockSize - 1)) )
#define R_CFLASH_GET_BLOCK_RELATIVEADDR( cfAddr )   ( (cfAddr) & (uint32_t)(R_CFLASH_BlockSize - 1) )
#define R_CFLASH_GET_BLOCK_NO( cfAddr )             ( (cfAddr) >> 11 )  // 2KB/block

#define R_CFLASH_WAIT_CFDFSEQ_END( statusFlag ) {\
    while( R_RFD_ENUM_RET_STS_BUSY == R_RFD_CheckCFDFSeqEndStep1() ){}; \
    while( R_RFD_ENUM_RET_STS_BUSY == R_RFD_CheckCFDFSeqEndStep2() ){}; \
    R_RFD_GetSeqErrorStatus( &(statusFlag) );                           \
    R_RFD_ClearSeqRegister();                                           \
}

/******************************************************************************
   Imported global variables and functions (from other files)
******************************************************************************/
extern void R_CFlash_Init( void );  // r_cflash_init.c

/******************************************************************************
   Private global variables
******************************************************************************/
#define R_CFLASH_NUM_WRITE_PROTECT      8

static uint32_t g_cFlashWriteProtect[R_CFLASH_NUM_WRITE_PROTECT][2];  //[0]=start address, [1]=end address
static uint8_t  g_numFrashWriteProtect;

/*--- firmware update ---*/
static r_cflash_buffmng_t g_cFlashBuff;

/******************************************************************************
   Private function prototypes
******************************************************************************/
static void R_CFlash_InitBuffer( uint32_t startAddress );
static fsp_err_t R_CFlash_OneBlockErase( uint32_t blockStartAddr );
static uint8_t R_CFlash_OneBlockWriteData( void );

/******************************************************************************
   Public function bodies [firmware update]
******************************************************************************/
/***********************************************************************
 * function name  : R_CFlash_Update_Init
 * description    : Initialization to write data to the flash ROM
 * parameters     : none
 * return value   : none
 **********************************************************************/
void R_CFlash_Update_Init( void )
{
    g_cFlashBuff.blockStartAddress = 0xFFFFFFFF;

    memset( (uint8_t *)( &g_cFlashWriteProtect[0][0] ), 
            0x00, R_CFLASH_NUM_WRITE_PROTECT * 2 * sizeof(uint32_t) );
    g_numFrashWriteProtect = 0;
}

/***********************************************************************
 * function name  : R_CFlash_AddWriteProtectArea
 * description    : Check flash rom are to write
 * parameters     : cfAddrStart ... start address to protect
 *                  cfAddrEnd   ... end address to protect
 * return value   : R_CFLASH_RESULT_SUCCESS / R_CFLASH_RESULT_FAILED
 **********************************************************************/
uint8_t R_CFlash_AddWriteProtectArea( uint32_t cfAddrStart, uint32_t cfAddrEnd )
{
    uint8_t     ret;

    // init
    ret = R_CFLASH_RESULT_FAILED;

    if( ( g_numFrashWriteProtect < R_CFLASH_NUM_WRITE_PROTECT ) &&
        ( cfAddrStart < cfAddrEnd ) )
    {
        g_cFlashWriteProtect[ g_numFrashWriteProtect ][ 0 ] = cfAddrStart;
        g_cFlashWriteProtect[ g_numFrashWriteProtect ][ 1 ] = cfAddrEnd;

        g_numFrashWriteProtect++;
        ret = R_CFLASH_RESULT_SUCCESS;
    }

    return ret;
}

/***********************************************************************
 * function name  : R_CFlash_CheckWriteArea
 * description    : Check flash rom are to write
 * parameters     : cfAddr         ... write address
 *                  cfSize         ... write size
 * return value   : R_CFLASH_RESULT_SUCCESS / R_CFLASH_RESULT_FAILED
 **********************************************************************/
uint8_t R_CFlash_CheckWriteArea( uint32_t cfAddr, uint32_t cfSize )
{
    uint8_t     i;
    uint32_t    cfAddrEnd;

    // init
    cfAddrEnd = cfAddr + cfSize - 1;
    if( cfAddrEnd < cfAddr )
    {
        return R_CFLASH_RESULT_FAILED;
    }

    for( i = 0; i < g_numFrashWriteProtect; i++ )
    {
        if( (cfAddrEnd >= g_cFlashWriteProtect[i][0]) &&
            (cfAddr    <= g_cFlashWriteProtect[i][1]) )
        {
            // cannot write
            return R_CFLASH_RESULT_FAILED;
        }
    }

    return R_CFLASH_RESULT_SUCCESS;
}

/***********************************************************************
 * function name  : R_CFlash_Update_WriteData
 * description    : Write (or prepare to write) F/W update data to the flash ROM
 * parameters     : cfAddr         ... write address
 *                  cfSize         ... write size
 *                  p_cfData       ... F/W update data to write
 *                  isContinueData ... 0 = not continue data / non-zero = data will be continued
 * return value   : R_CFLASH_RESULT_SUCCESS / R_CFLASH_RESULT_FAILED
 **********************************************************************/
uint8_t R_CFlash_Update_WriteData( uint32_t         cfAddr, 
                                   uint32_t         cfSize, 
                                   uint8_t *p_cfData, 
                                   uint8_t          isContinueData )
{
    uint8_t     res;
    uint8_t     *p_buff;
    uint32_t    storeSize;
    uint32_t    blockStartAddr;
    uint32_t    posInBlock;

    // check param
    if( ( p_cfData == NULL ) || ( cfSize == 0 ) )
    {
        return R_CFLASH_RESULT_FAILED;
    }

    res = R_CFlash_CheckWriteArea( cfAddr, cfSize );
    if( res != R_CFLASH_RESULT_SUCCESS )
    {
        return R_CFLASH_RESULT_FAILED;
    }

    /*--- write previous data to flash before starting ---*/
    blockStartAddr = R_CFLASH_GET_BLOCK_STARTADDR( cfAddr );

    if( ( g_cFlashBuff.blockStartAddress != 0xFFFFFFFF ) &&
        ( g_cFlashBuff.blockStartAddress != blockStartAddr ) )
    {
        res = R_CFlash_OneBlockWriteData();
        g_cFlashBuff.blockStartAddress = 0xFFFFFFFF;
    }

    /*--- Init buffer ---*/
    if( ( res == R_CFLASH_RESULT_SUCCESS ) &&
        ( g_cFlashBuff.blockStartAddress == 0xFFFFFFFF ) )
    {
        R_CFlash_InitBuffer( blockStartAddr );
    }

    /*--- Write ---*/
    storeSize = 0;  // init (use it after while() loop)
    while ( ( res == R_CFLASH_RESULT_SUCCESS ) && ( cfSize > 0 ) )
    {
        posInBlock = R_CFLASH_GET_BLOCK_RELATIVEADDR( cfAddr );  // start position in a block
        p_buff = &(g_cFlashBuff.blockBuff[ posInBlock ]);

        if ((posInBlock + cfSize) <= R_CFLASH_BlockSize)
        {
            storeSize = cfSize;
        }
        else
        {
            storeSize = R_CFLASH_BlockSize - posInBlock;
        }

        /*--- write data to buffer(RAM) ---*/
        memcpy( p_buff, p_cfData, storeSize );

        // for next loop
        cfAddr   += storeSize;
        cfSize   -= storeSize;
        p_cfData += storeSize;

        /*--- write data to flash before crossing over blocks ---*/
        blockStartAddr = R_CFLASH_GET_BLOCK_STARTADDR( cfAddr );
        if (blockStartAddr != g_cFlashBuff.blockStartAddress)
        {
            res = R_CFlash_OneBlockWriteData();

            if (cfSize > 0)
            {
                // init buffer for next
                R_CFlash_InitBuffer( blockStartAddr );
            }
            else
            {
                g_cFlashBuff.blockStartAddress = 0xFFFFFFFF;
            }

            storeSize = 0;  // reset
        }
    }

    /*--- write remained data to flash ---*/
    if( res == R_CFLASH_RESULT_SUCCESS )
    {
        if( ( storeSize > 0 ) && ( isContinueData == 0 ) )
        {
            res = R_CFlash_OneBlockWriteData();

            g_cFlashBuff.blockStartAddress = 0xFFFFFFFF;
        }
    }

    return res;
}

/***********************************************************************
 * function name  : R_CFlash_Update_WriteData_inBlock
 * description    : Write (or prepare to write) F/W update data to the flash ROM
 * parameters     : cfAddr             ... write address
 *                  cfSize             ... write size
 *                  p_cfData           ... F/W update data to write
 *                  isEraseBeforeWrite ... erase the block of flash ROM if 1
 * return value   : R_CFLASH_RESULT_SUCCESS / R_CFLASH_RESULT_FAILED
 **********************************************************************/
uint8_t R_CFlash_Update_WriteData_inBlock( uint32_t         cfAddr,
                                           uint32_t         cfSize,
                                           uint8_t *p_cfData,
                                           uint8_t          isEraseBeforeWrite )
{
    uint8_t     res;
    uint32_t    tmpBlockAddr;
    uint8_t     numTryWrite;
    int32_t     comp;
    fsp_err_t   flash_err;

    // check param
    if( ( p_cfData == NULL ) || ( cfSize == 0 ) )
    {
        return R_CFLASH_RESULT_FAILED;
    }

    res = R_CFlash_CheckWriteArea( cfAddr, cfSize );
    if( res != R_CFLASH_RESULT_SUCCESS )
    {
        return R_CFLASH_RESULT_FAILED;
    }

    g_cFlashBuff.blockStartAddress = R_CFLASH_GET_BLOCK_STARTADDR( cfAddr );
    tmpBlockAddr                   = R_CFLASH_GET_BLOCK_STARTADDR( cfAddr + cfSize - 1 );
    if( g_cFlashBuff.blockStartAddress != tmpBlockAddr )
    {
        return R_CFLASH_RESULT_FAILED;
    }

    // init
    res         = R_CFLASH_RESULT_FAILED;
    numTryWrite = 0;
    memcpy( &(g_cFlashBuff.blockBuff[0]), (uint8_t *)p_cfData, cfSize );

    do
    {
        /*--- init flash lib ---*/
        R_CFlash_Init();
        BoardDisableAllIrq();
        {
            flash_err = FSP_SUCCESS;  // init

            if( isEraseBeforeWrite == 1 )
            {
                /*--- Blank check and erase ---*/
                flash_err = R_CFlash_OneBlockErase( g_cFlashBuff.blockStartAddress );
            }

            /*--- Write ---*/
            if( flash_err == FSP_SUCCESS )
            {
                R_FLASH_LP_Write( R_CFLASH_P_FLASH_LP_CTRL,
                                  (uint32_t)&g_cFlashBuff.blockBuff[0],
                                  cfAddr,
                                  cfSize );  // must be a multuple of programming size (= 32bit)
            }
        }
        BoardEnableAllIrq();

        /*-- data compare --*/
        if( flash_err == FSP_SUCCESS )
        {
            comp = memcmp( p_cfData, (uint8_t *)cfAddr, cfSize );
            if( comp == 0 )
            {
                res = R_CFLASH_RESULT_SUCCESS;
            }
        }

        numTryWrite += 1;
    } while ( ( res != R_CFLASH_RESULT_SUCCESS ) && ( numTryWrite < R_CFLASH_WRITE_TIMES ) );

    g_cFlashBuff.blockStartAddress = 0xFFFFFFFF;
    return res;
}

/***********************************************************************
 * function name  : R_CFlash_EraseOneBlock
 * description    : Erase one flash block
 * parameters     : cfAddr ... flash address (erase the block to which cfAddr belongs)
 * return value   : R_CFLASH_RESULT_SUCCESS / R_CFLASH_RESULT_FAILED
 **********************************************************************/
uint8_t R_CFlash_EraseBlock( uint32_t cfAddr )
{
    uint8_t     res;
    fsp_err_t   flash_err;

    // init
    res    = R_CFLASH_RESULT_FAILED;
    cfAddr = R_CFLASH_GET_BLOCK_STARTADDR( cfAddr );

    /*--- init flash lib ---*/
    R_CFlash_Init();
    BoardDisableAllIrq();
    {
        /*--- Blank check and erase ---*/
        flash_err = R_CFlash_OneBlockErase( cfAddr );

        if( flash_err == FSP_SUCCESS )
        {
            res = R_CFLASH_RESULT_SUCCESS;
        }
    }
    BoardEnableAllIrq();

    return res;
}

/***********************************************************************
 * static functions [firmware update]
 **********************************************************************/
/***********************************************************************
 * function name  : R_CFlash_InitBuffer
 * description    : Initialize buffer which is used to write to the flash ROM
 * parameters     : startAddress ... address to wrtie
 * return value   : (none)
 **********************************************************************/
static void R_CFlash_InitBuffer( uint32_t startAddress )
{
    uint32_t    blockStartAddress;

    blockStartAddress = R_CFLASH_GET_BLOCK_STARTADDR( startAddress );

    memcpy( &(g_cFlashBuff.blockBuff[0]), 
            (uint8_t *)blockStartAddress,
            R_CFLASH_BlockSize );

    g_cFlashBuff.blockStartAddress = blockStartAddress;
}

/***********************************************************************
 * function name  : R_CFlash_OneBlockErase
 * description    : Erase one block
 *                  ** Function caller needs to call R_FLASH_LP_Open().
 * parameters     : blockStartAddr ... start address of the block to erase
 * return value   : FSP status code (FSP_SUCCESS/ERR_xxx)
 **********************************************************************/
static fsp_err_t R_CFlash_OneBlockErase( uint32_t blockStartAddr )
{
    fsp_err_t       flash_err;
    flash_result_t  blank_check_result;

    // init (fail-safe)
    blockStartAddr = R_CFLASH_GET_BLOCK_STARTADDR( blockStartAddr );

    /*--- Blank check ---*/
    flash_err = R_FLASH_LP_BlankCheck( R_CFLASH_P_FLASH_LP_CTRL, 
                                       blockStartAddr, 
                                       R_CFLASH_BlockSize, 
                                       &blank_check_result );
    if( ( flash_err == FSP_SUCCESS ) && 
        ( blank_check_result == FLASH_RESULT_NOT_BLANK ) )
    {
        /*--- Erase ---*/
        flash_err = R_FLASH_LP_Erase( R_CFLASH_P_FLASH_LP_CTRL, 
                                      blockStartAddr, 
                                      1 );
    }

    return flash_err;
}

/***********************************************************************
 * function name  : R_CFlash_OneBlockWriteData
 * description    : Write data to the flash ROM with using library
 * parameters     : (none)
 * return value   : R_CFLASH_RESULT_SUCCESS / R_CFLASH_RESULT_FAILED
 **********************************************************************/
static uint8_t R_CFlash_OneBlockWriteData( void )
{
    uint8_t         res;
    uint8_t         numTryWrite;
    int32_t         comp;
    fsp_err_t       flash_err;

    // init
    res         = R_CFLASH_RESULT_FAILED;
    numTryWrite = 0;

    do
    {
        /*--- init flash lib ---*/
        R_CFlash_Init();
        BoardDisableAllIrq();
        {
            /*--- Blank check and erase ---*/
            flash_err = R_CFlash_OneBlockErase( g_cFlashBuff.blockStartAddress );

            /*--- Write ---*/
            if( flash_err == FSP_SUCCESS )
            {
                R_FLASH_LP_Write( R_CFLASH_P_FLASH_LP_CTRL, 
                                  (uint32_t)&g_cFlashBuff.blockBuff[0],
                                  g_cFlashBuff.blockStartAddress,
                                  R_CFLASH_BlockSize );  // must be a multuple of programming size (= 32bit)
            }
        }
        BoardEnableAllIrq();

        /*-- data compare --*/
        if( flash_err == FSP_SUCCESS )
        {
            comp = memcmp( &(g_cFlashBuff.blockBuff[0]),
                           (uint8_t *)g_cFlashBuff.blockStartAddress,
                           R_CFLASH_BlockSize );
            if( comp == 0 )
            {
                res = R_CFLASH_RESULT_SUCCESS;
            }
        }

        numTryWrite += 1;
    } while ( ( res != R_CFLASH_RESULT_SUCCESS ) && ( numTryWrite < R_CFLASH_WRITE_TIMES ) );

    return res;
}

/*******************************************************************************
 * Copyright (C) 2014-2018 Renesas Electronics Corporation.
 ******************************************************************************/
