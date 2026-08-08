/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "board.h"

#include "LoRaMac.h"
#include "LoRaFuotaProcess.h"

#include "cflash.h"

#include "app_fuota_fwupdate.h"
#include "app_fwupdate_area.h"

#if ( (FUOTAUPDT_SWAPMODE != FUOTAUPDT_SWAPMODE_BOOTSWAP) && (FUOTAUPDT_SWAPMODE != FUOTAUPDT_SWAPMODE_BANKSWAP) )
    #error "Error - swap mode is not defined."
#endif

/*--- F/W image ---*/
FuotaUpdtImageInfo_t    g_tmpFwImageInfo;
FuotaUpdtImageBlock_t   g_tmpImageBlock;

uint32_t g_addrInfo_storage_start;
uint32_t g_addrInfo_storage_end;

/*--- F/W update; statement ---*/
typedef struct {
    uint8_t                     state;
    /*--*/
    FuotaUpdtImageInfo_t *p_fwImgInfo;
    uint32_t                    fwImgStartAddr;
    /*--*/
    uint32_t                    writtenSize;
    uint32_t                    currentIndex;
    uint32_t                    writtenIndex;
} FuotaUpdtMng_t;

FuotaUpdtMng_t g_fuotaUpdtMng = { .state        = FUOTAUPDT_STATE_NONE, 
                                  .currentIndex = (uint32_t)(-1),
                                  .writtenIndex = (uint32_t)(-1) };

/*--- Event; notify to app_fuota_process.c ---*/
FuotaUpdateEventCb_t g_fuotaUpdtEventCbFuncs = {0};

/*--- static functions ---*/
static void AppFuotaUpdt_Init( void );
static FuotaUpdateStatus_t AppFuotaUpdate_CheckFwImgHeader( uint8_t *p_dataBlk );
static FuotaUpdateStatus_t AppFuotaUpdt_VerifyChecksum( void );
static FuotaUpdateStatus_t AppFuotaUpdate_CheckFwImage( void );

// Event/Callback functions from FW update (app_fuota_fwupdate_bank.c)
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
static void AppFuotaUpdate_EventBankUpdateStateChanged( AppFuotaBankUpdtProcState_t stateBankUpdt );
static void AppFuotaUpdate_EventUpdateFinished( bool bIsSuccess );
#endif

/*!
 * Initialization
 */
FuotaUpdateStatus_t AppFuotaUpdateInitialization( FuotaUpdateEventCb_t *p_appFuotaUpdtEventCb )
{
    FuotaUpdateStatus_t     res;
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
    AppFuotaBankEventCb_t   appFuotaBankUpdateCallbacks;
#endif

    // initial check
    if( p_appFuotaUpdtEventCb == NULL )
    {
        return FUOTAUPDT_STATUS_ERROR;
    }
    if( ( p_appFuotaUpdtEventCb->AppFuotaUpdateReadyIndication == NULL ) ||
        ( p_appFuotaUpdtEventCb->AppFuotaUpdateErrorIndication == NULL ) )
    {
        return FUOTAUPDT_STATUS_ERROR;
    }
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
    if( p_appFuotaUpdtEventCb->AppFuotaUpdateFinishedIndication == NULL )
    {
        return FUOTAUPDT_STATUS_ERROR;
    }
#endif

    // init
    res = FUOTAUPDT_STATUS_OK;

    // Get address information of storage area
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BOOTSWAP)
    g_addrInfo_storage_start = *( (uint32_t *)FWUPDATE_ADDRESSINFO_STORAGEAREA_START );
    g_addrInfo_storage_end   = *( (uint32_t *)FWUPDATE_ADDRESSINFO_STORAGEAREA_END );
#else  // #if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
    g_addrInfo_storage_start = FWUPDATE_STARTADDR_STORAGEAREA + FWUPDATE_BANKMODE_BANKSIZE;
    g_addrInfo_storage_end   = FWUPDATE_ENDADDR_STORAGEAREA   + FWUPDATE_BANKMODE_BANKSIZE;
#endif

#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BOOTSWAP)
    // address information of storage area must be same with definitions in F/W update program
    if( ( g_addrInfo_storage_start != FWUPDATE_STARTADDR_STORAGEAREA ) || 
        ( g_addrInfo_storage_end != FWUPDATE_ENDADDR_STORAGEAREA ) )
    {
        // address information of storage area is not same or there is not F/W update program
        g_fuotaUpdtMng.state = FUOTAUPDT_STATE_FATAL_ERR;
        res = FUOTAUPDT_STATUS_ERROR;
    }
#endif

    if( res == FUOTAUPDT_STATUS_OK )
    {
        // Get callback functions
        memcpy( &g_fuotaUpdtEventCbFuncs, p_appFuotaUpdtEventCb, sizeof(g_fuotaUpdtEventCbFuncs) );

#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
        // init FW update process (for bank mode)
        appFuotaBankUpdateCallbacks.AppFuotaBankUpdateChangedStateIndication = AppFuotaUpdate_EventBankUpdateStateChanged;
        appFuotaBankUpdateCallbacks.AppFuotaBankUpdateFinishedIndication     = AppFuotaUpdate_EventUpdateFinished;
        res = AppFuotaBankUpdateInitialization( &appFuotaBankUpdateCallbacks );
#endif
    }

    return res;
}

/*!
 * Firmware update process
 */
void AppFuotaUpdateProcess( void )
{
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BOOTSWAP)
    // nothing to do
#else  // #if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
    AppFuotaBankUpdateProcess();
#endif
}


/*!
 * Get data block (= part of F/W image)
 */
FuotaUpdateStatus_t AppFuotaUpdateStoreFwImage( uint8_t *p_dataBlk, uint16_t dataSize )
{
    FuotaUpdateStatus_t         res;
    uint8_t                     resWrite;
    uint32_t                    cfAddr;
    FuotaUpdtImageInfo_t *p_fwImgInfo;
    FuotaUpdateStatus_t         resChkFwImg;
    uint8_t                     i, *p_tmp8;
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
    FuotaBankUpdateStatus_t     resBankUpdt;
#endif

    // initial chec
    if( (p_dataBlk == NULL) || (dataSize == 0) )
    {
        return FUOTAUPDT_STATUS_ERROR;
    }
    if( g_fuotaUpdtMng.state == FUOTAUPDT_STATE_FATAL_ERR )
    {
        return FUOTAUPDT_STATUS_ERROR;  // cannot process update
    }

    // Check F/W image header
    res = AppFuotaUpdate_CheckFwImgHeader( p_dataBlk );
    if( res == FUOTAUPDT_STATUS_OK )
    {
        p_dataBlk += FUOTAUPDT_SIZEOF_FWIMGHDR;
        dataSize  -= FUOTAUPDT_SIZEOF_FWIMGHDR;
    }
    else
    {
        // nothing to do (discard dataBlk)
        return res;
    }

    // Initialize flash update
    if( g_fuotaUpdtMng.state == FUOTAUPDT_STATE_INITIAL )
    {
        R_CFlash_Update_Init();

        // set write protect except storage area of F/W image
        R_CFlash_AddWriteProtectArea( 0, ( g_addrInfo_storage_start - 1 ) );
        R_CFlash_AddWriteProtectArea( ( g_addrInfo_storage_end + 1 ), 0xFFFFFFFF );

        g_fuotaUpdtMng.state = FUOTAUPDT_STATE_RUNNING;
    }

    // write to flash
    if( g_fuotaUpdtMng.state == FUOTAUPDT_STATE_RUNNING )
    {
        resWrite = R_CFLASH_RESULT_SUCCESS;  // init

        // check; index
        if( g_fuotaUpdtMng.currentIndex != (g_fuotaUpdtMng.writtenIndex + 1) )
        {
            // error; index is not continuous
            resWrite = R_CFLASH_RESULT_FAILED;
        }
        // check; write size
        if( ( resWrite == R_CFLASH_RESULT_SUCCESS ) &&
            ( g_fuotaUpdtMng.p_fwImgInfo != NULL ) )        // NULL = I don't know total size yet.
        {
            p_fwImgInfo = g_fuotaUpdtMng.p_fwImgInfo;
            if( ( g_fuotaUpdtMng.writtenSize + dataSize ) > p_fwImgInfo->imageSize )
            {
                // error; will be overwrite
                resWrite = R_CFLASH_RESULT_FAILED;
            }
        }

        // write
        if( resWrite == R_CFLASH_RESULT_SUCCESS )
        {
            cfAddr = g_fuotaUpdtMng.fwImgStartAddr + g_fuotaUpdtMng.writtenSize;
            resWrite = R_CFlash_Update_WriteData( cfAddr, dataSize, p_dataBlk, 0 );
        }

        if( resWrite == R_CFLASH_RESULT_SUCCESS )
        {
            // update mng first
            g_fuotaUpdtMng.writtenIndex = g_fuotaUpdtMng.currentIndex;
            g_fuotaUpdtMng.writtenSize += (uint32_t)dataSize;

            // get F/W image information (if not yet)
            if( ( g_fuotaUpdtMng.p_fwImgInfo == NULL ) &&
                ( g_fuotaUpdtMng.writtenSize >= FUOTAUPDT_SIZEOF_FWIMGINFO ) )
            {
                p_tmp8 = (uint8_t *)g_fuotaUpdtMng.fwImgStartAddr;

                memset( &g_tmpFwImageInfo, 0x00, sizeof(FuotaUpdtImageInfo_t) );
                g_tmpFwImageInfo.imageBlockNum   = (*p_tmp8++);
                g_tmpFwImageInfo.imageBlockIndex = (*p_tmp8++);
                // g_tmpFwImageInfo.imageVersion has been cleared
                for( i = 0; i < 4; i++ )
                {
                    g_tmpFwImageInfo.imageVersion |= (uint32_t)(*p_tmp8++) << (i*8);
                }
                // g_tmpFwImageInfo.imageSize has been cleared
                for( i = 0; i < 4; i++ )
                {
                    g_tmpFwImageInfo.imageSize |= (uint32_t)(*p_tmp8++) << (i*8);
                }
                g_tmpFwImageInfo.imagePriority = (*p_tmp8++);
                g_tmpFwImageInfo._reserved     = (*p_tmp8++);
                memcpy( g_tmpFwImageInfo.imageVerify, p_tmp8, sizeof(g_tmpFwImageInfo.imageVerify) );

                g_fuotaUpdtMng.p_fwImgInfo = &g_tmpFwImageInfo;
            }

            p_fwImgInfo = g_fuotaUpdtMng.p_fwImgInfo;
            if( p_fwImgInfo != NULL )
            {
                if( g_fuotaUpdtMng.writtenSize == p_fwImgInfo->imageSize )
                {
                    // checksum & format check
                    resChkFwImg = AppFuotaUpdate_CheckFwImage();
                    if( resChkFwImg == FUOTAUPDT_STATUS_OK )
                    {
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BOOTSWAP)
                        g_fuotaUpdtMng.state = FUOTAUPDT_STATE_SUCCESS;
                        // notify to upper
                        g_fuotaUpdtEventCbFuncs.AppFuotaUpdateReadyIndication();  // non-null

#else  // #if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
                        // Process in app_fuota_fwupdt.c is success. Waiting for the ready.
                        g_fuotaUpdtMng.state = FUOTAUPDT_STATE_SUCCESS_WAITING_READY;

                        resBankUpdt = AppFuotaBankUpdateStoreFwImageFinish();
                        if( resBankUpdt != FUOTABANKUPDT_STATUS_OK )
                        {
                            g_fuotaUpdtMng.state = FUOTAUPDT_STATE_FAILED;
                            // notify to upper
                            g_fuotaUpdtEventCbFuncs.AppFuotaUpdateErrorIndication( FUOTAUPDT_UPDATE_READY_ERR_FWIMG_STORED );  // non-null
                        }
#endif
                    }
                    else
                    {
                        g_fuotaUpdtMng.state = FUOTAUPDT_STATE_VERIFY_ERR;
                        // notify to upper
                        g_fuotaUpdtEventCbFuncs.AppFuotaUpdateErrorIndication( FUOTAUPDT_UPDATE_READY_ERR_INVALID_FWIMG );  // non-null
                    }
                }
            }

            res = FUOTAUPDT_STATUS_OK;
        }
        else
        {
            g_fuotaUpdtMng.state = FUOTAUPDT_STATE_FAILED;
            // notify to upper
            g_fuotaUpdtEventCbFuncs.AppFuotaUpdateErrorIndication( FUOTAUPDT_UPDATE_READY_ERR_FWIMG_STORED );  // non-null
        }
    }

    return res;
}

/*!
 * Get current status
 */
uint8_t AppFuotaUpdateGetStatus( void )
{
    return g_fuotaUpdtMng.state;
}

/*!
 * Start F/W update (start F/W update program by bootswap)
 */
FuotaUpdateStatus_t AppFuotaUpdateStartFwUpdate( AppFuotaUpdatePre_t p_preUpdateCbFunc )
{
    FuotaUpdateStatus_t res;

    // initial check
    if( g_fuotaUpdtMng.state != FUOTAUPDT_STATE_SUCCESS )
    {
        return FUOTAUPDT_STATUS_ERROR;
    }

    // init
    res = FUOTAUPDT_STATUS_ERROR;

    if( LoRaMacStop() == LORAMAC_STATUS_OK )
    {
        FuotaStop();

        if( p_preUpdateCbFunc != NULL)
        {
            (*p_preUpdateCbFunc)();
        }

#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BOOTSWAP)
        (void)R_CFlash_SwitchBootCluster();
#else  // #if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
        (void)AppFuotaBankUpdateBankSwap();
#endif

        // It comes here only if upper function (swap) is failed. 
        // restart stack
        LoRaMacStart();
        FuotaStart();
    }
    else
    {
        res = FUOTAUPDT_STATUS_BUSY;
    }

    return res;
}

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*!
 * Get F/W image version
 */
uint32_t AppFuotaUpdateGetFirmwareImageVersion( void )
{
    uint32_t                    ret;
    FuotaUpdtImageInfo_t *p_fwImgInfo;

    // init
    ret = 0;

    if( g_fuotaUpdtMng.state == FUOTAUPDT_STATE_SUCCESS )
    {
        p_fwImgInfo  = g_fuotaUpdtMng.p_fwImgInfo;
        ret = p_fwImgInfo->imageVersion;
    }

    return ret;
}

/*!
 * Reset firmware update (disable current F/W image)
 */
void AppFuotaUpdateReset( void )
{
    AppFuotaUpdt_Init();
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
    if( g_fuotaUpdtMng.state == FUOTAUPDT_STATE_INITIAL )
    {
        AppFuotaBankUpdateReset();
    }
#endif
}
#endif

/*!
 * misc; low power mode
 */
bool AppFuotaUpdateIsLowPowerAllowed( void )
{
    bool    bRet;

#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BOOTSWAP)
    bRet = true;  // always allow low power
#else  // #if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
    bRet = AppFuotaBankUpdateIsLowPowerAllowed();
#endif

    return bRet;
}


//-----------------------------------------------------------------------------

static void AppFuotaUpdt_Init( void )
{
    if( g_fuotaUpdtMng.state != FUOTAUPDT_STATE_FATAL_ERR )
    {
        memset1( (uint8_t *)&g_fuotaUpdtMng, 0x00, sizeof(FuotaUpdtMng_t) );
        g_fuotaUpdtMng.fwImgStartAddr = g_addrInfo_storage_start;
        g_fuotaUpdtMng.writtenIndex   = (uint32_t)(-1);

        g_fuotaUpdtMng.state = FUOTAUPDT_STATE_INITIAL;
    }
}

static FuotaUpdateStatus_t AppFuotaUpdate_CheckFwImgHeader( uint8_t *p_dataBlk )
{
    FuotaStatus_t           res;
    FuotaUpdtImageHeader_t  fwImgHeader, *p_fwImgHeader;
    uint8_t                 i;

    // init
    res = FUOTAUPDT_STATUS_ERROR;

    fwImgHeader.index = 0;
    for( i = 0; i < 4; i++ )
    {
        fwImgHeader.index |= (uint32_t)(*p_dataBlk++) << (i*8);
    }
    p_fwImgHeader = &fwImgHeader;

    // check index
    if( p_fwImgHeader->index == 0 )
    {
        // init firmware update
        AppFuotaUpdt_Init();
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
        if( g_fuotaUpdtMng.state == FUOTAUPDT_STATE_INITIAL )
        {
            AppFuotaBankUpdateStoreFwImageStart();
        }
#endif
        res = FUOTAUPDT_STATUS_OK;
    }
    else
    {
        if( ( g_fuotaUpdtMng.state == FUOTAUPDT_STATE_RUNNING ) &&
            ( ( g_fuotaUpdtMng.writtenIndex + 1 ) == p_fwImgHeader->index ) )
        {
            // - write process is running and
            // - descriptor (index) is continuous
            g_fuotaUpdtMng.currentIndex = p_fwImgHeader->index;
            res = FUOTAUPDT_STATUS_OK;
        }
    }

    return res;
}

static FuotaUpdateStatus_t AppFuotaUpdt_VerifyChecksum( void )
{
    FuotaUpdateStatus_t         res;
    uint32_t                    i;
    uint8_t *p_fwImg;
    FuotaUpdtImageInfo_t *p_fwImgInfo;
    uint32_t                    calcChksum, verifyChksum;

    // init
    res          = FUOTAUPDT_STATUS_ERROR;
    p_fwImgInfo  = g_fuotaUpdtMng.p_fwImgInfo;
    p_fwImg      = (uint8_t *)( g_fuotaUpdtMng.fwImgStartAddr );
    calcChksum   = 0;
    verifyChksum = 0;

    // calculate check sum
    for( i = 0; i < p_fwImgInfo->imageSize; i++ )
    {
        if( ( i < FUOTAUPDT_FWIMGINFO_IMGVERIFY_AREA) || 
            ( i >= (FUOTAUPDT_FWIMGINFO_IMGVERIFY_AREA + FUOTAUPDT_FWIMGINFO_IMGVERIFY_SIZE) ) )
        {
            calcChksum += (*p_fwImg);
        }

        p_fwImg++;
    }

    // get check sum
    verifyChksum = (uint32_t)( p_fwImgInfo->imageVerify[0] ) +
                   ( (uint32_t)( p_fwImgInfo->imageVerify[1] ) <<  8 ) + 
                   ( (uint32_t)( p_fwImgInfo->imageVerify[2] ) << 16 ) + 
                   ( (uint32_t)( p_fwImgInfo->imageVerify[3] ) << 24 );

    // next state
    if( calcChksum == verifyChksum )
    {
        res = FUOTAUPDT_STATUS_OK;
    }

    return res;
}

static FuotaUpdateStatus_t AppFuotaUpdate_CheckFwImage( void )
{
    FuotaUpdateStatus_t         res;
    FuotaUpdtImageInfo_t *p_fwImgInfo;
    FuotaUpdtImageBlock_t *p_imgBlock;
    uint32_t                    addressFwImgEnd;
    uint32_t                    cfAddr;
    uint32_t                    tmpAddrEnd;
    uint32_t                    storedImageSize;
    uint8_t                     i;

    // check checksum first
    res = AppFuotaUpdt_VerifyChecksum();
    if( res != FUOTAUPDT_STATUS_OK )
    {
        return res;
    }

    // get F/W image
    p_fwImgInfo = g_fuotaUpdtMng.p_fwImgInfo;

    addressFwImgEnd = g_addrInfo_storage_start + p_fwImgInfo->imageSize - 1;

#ifdef DEBUG_FWUPDT
    print( "GetInfo: F/W image" );
    print_newline();

    print( "GetInfo:   - Stored area   : 0x" );
    print_hex( g_addrInfo_storage_start, 8 );
    print( " - 0x" );
    print_hex( addressFwImgEnd, 8 );
    print_newline();

    print( "GetInfo:   - imageBlockNum : " );
    print_dec( p_fwImgInfo->imageBlockNum, 3, '\0' );
    print_newline();

    print( "GetInfo:   - imageVersion  : 0x" );
    print_hex( p_fwImgInfo->imageVersion, 8 );
    print_newline();

    print( "GetInfo:   - imageSize     : 0x" );
    print_hex( p_fwImgInfo->imageSize, 8 );
    print_newline();

    print( "GetInfo:   - Priority      : " );
    print_dec( p_fwImgInfo->imagePriority, 3, '\0' );
    print_newline();

#endif

    // check; stored area of F/W image
    if( ( addressFwImgEnd < g_addrInfo_storage_start ) || ( addressFwImgEnd >= g_addrInfo_storage_end ) )
    {
        // stored area of F/W image is invalid.
        res = FUOTAUPDT_STATUS_ERROR;

#ifdef DEBUG_FWUPDT
        print( "GetInfo: *** error; stored area" );
        print_newline();
#endif
    }

    // check; ImageBlockNum must be greater than 0
    if( ( res == FUOTAUPDT_STATUS_OK ) &&
        ( p_fwImgInfo->imageBlockNum == 0 ) )
    {
        res = FUOTAUPDT_STATUS_ERROR;

#ifdef DEBUG_FWUPDT
        print( "GetInfo: *** error; imageBlockNum" );
        print_newline();
#endif
    }

    // check; CodeAddress and CodeSize
    if( res == FUOTAUPDT_STATUS_OK )
    {
        p_imgBlock = NULL;  // init to get 1st entry
        for( i = 0; i < p_fwImgInfo->imageBlockNum; i++ )
        {
            AppFuotaUpdate_GetNextImageBlock( &p_imgBlock );
            cfAddr     = p_imgBlock->codeAddress;
            tmpAddrEnd = cfAddr + p_imgBlock->codeSize - 1;

#ifdef DEBUG_FWUPDT
            print( "GetInfo: ImageBlock#" );
            print_dec( p_imgBlock->imageBlockIndex, 3, '\0' );
            print( " of " );
            print_dec( p_imgBlock->imageBlockNum, 3, '\0' );
            print_newline();

            print( "GetInfo:   - CodeAddress : 0x" );
            print_hex( cfAddr, 8 );
            print( " - 0x" );
            print_hex( tmpAddrEnd, 8 );
            print_newline();

            print( "GetInfo:   - CodeSize    : 0x" );
            print_hex( p_imgBlock->codeSize, 8 );
            print_newline();
#endif

            // check image block num/index
            if( ( p_imgBlock->imageBlockNum != p_fwImgInfo->imageBlockNum ) ||
                ( p_imgBlock->imageBlockIndex != ( i + 1 ) ) )
            {
                res = FUOTAUPDT_STATUS_ERROR;
#ifdef DEBUG_FWUPDT
                print( "GetInfo: *** error; imageBlockNum/Index" );
                print_newline();
#endif
                break;  // exit for(i) loop
            }

#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BOOTSWAP)
            // check destination address; NOT boot claster 1
            if( ( tmpAddrEnd >= FWUPDATE_STARTADDR_BCL1 ) &&
                ( cfAddr <= FWUPDATE_ENDADDR_BCL1 ) )
            {
                res = FUOTAUPDT_STATUS_ERROR;
  #ifdef DEBUG_FUOTAUPDT_DBGPRINT
                print( "GetInfo: *** error; CodeAddress (in BCL1)" );
                print_newline();
  #endif
                break;  // exit for(i) loop
            }

            // check destination address; not guard/storage area
            if( tmpAddrEnd >= FWUPDATE_STARTADDR_AREA_FOR_FWUPDT )
            {
                res = FUOTAUPDT_STATUS_ERROR;
  #ifdef DEBUG_FUOTAUPDT_DBGPRINT
                print( "GetInfo: *** error; CodeAddress (kn guard area)" );
                print_newline();
  #endif
            }

#else  // #if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
            // check range of CodeAddress
            if( tmpAddrEnd >= FWUPDATE_STARTADDR_STORAGEAREA )
            {
                res = FUOTAUPDT_STATUS_ERROR;
  #ifdef DEBUG_FUOTAUPDT_DBGPRINT
                print( "GetInfo: *** error; CodeAddress (in storage)" );
                print_newline();
  #endif
                break;  // exit for(i) loop
            }
#endif
        }
    }

    // check; total size of F/W image
    if( res == FUOTAUPDT_STATUS_OK )
    {
        AppFuotaUpdate_GetNextImageBlock( &p_imgBlock );
        storedImageSize = p_imgBlock->_imageBlockStartAddr - g_addrInfo_storage_start;

        if( storedImageSize != p_fwImgInfo->imageSize )
        {
#ifdef DEBUG_FWUPDT
            print( "GetInfo: *** error; wrong ImageSize" );
            print_newline();
#endif
            res = FUOTAUPDT_STATUS_ERROR;
        }
    }

    return res;
}

/*!
 * Get next image block in FWImage
 */
void AppFuotaUpdate_GetNextImageBlock( FuotaUpdtImageBlock_t **pp_nextBlock )
{
    uint32_t    nextImgBlockAddr;
    uint8_t     isEndedImgBlk;
    uint8_t     i, *p_tmp8;

    // init
    isEndedImgBlk = 0;

    if( (*pp_nextBlock) == NULL )
    {
        // 1st image block
        nextImgBlockAddr  = FWUPDATE_STARTADDR_STORAGEAREA;
        nextImgBlockAddr += FUOTAUPDT_SIZEOF_FWIMGINFO;
    }
    else
    {
        // next image block   // +10 = sizeof(imageBlockNum + imageBlockIndex + codeAddress + codeSize)
        nextImgBlockAddr = (*pp_nextBlock)->_imageBlockStartAddr + (*pp_nextBlock)->codeSize + 10;
        nextImgBlockAddr = (nextImgBlockAddr + 1) & 0xFFFFFFFE;  // even address

        if( (*pp_nextBlock)->imageBlockIndex == (*pp_nextBlock)->imageBlockNum )
        {
            isEndedImgBlk = 1;  // non-zero
        }
    }

    memset( &g_tmpImageBlock, 0x00, sizeof(FuotaUpdtImageBlock_t) );
    g_tmpImageBlock._imageBlockStartAddr = nextImgBlockAddr;

    if( isEndedImgBlk == 0 )
    {
        p_tmp8 = (uint8_t *)nextImgBlockAddr;

        g_tmpImageBlock.imageBlockNum   = (*p_tmp8++);
        g_tmpImageBlock.imageBlockIndex = (*p_tmp8++);
        // g_tmpImageBlock.codeAddress has been cleared
        for( i = 0; i < 4; i++ )
        {
            g_tmpImageBlock.codeAddress |= (uint32_t)(*p_tmp8++) << (i*8);
        }
        // g_tmpImageBlock.codeSize has been cleared
        for( i = 0; i < 4; i++ )
        {
            g_tmpImageBlock.codeSize |= (uint32_t)(*p_tmp8++) << (i*8);
        }
        g_tmpImageBlock.p_code = p_tmp8;
    }

    (*pp_nextBlock) = &g_tmpImageBlock;
}


//--------------------------------------------------------------------------------------------------

#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
/*!
 * Event/Callback functions from FW update (app_fuota_fwupdate_bank.c): Get changed bank-update state
 * (it is called when bank-update state has been changed and initialization.)
 */
static void AppFuotaUpdate_EventBankUpdateStateChanged( AppFuotaBankUpdtProcState_t stateBankUpdt )
{
    // initial check
    if( g_fuotaUpdtMng.state == FUOTAUPDT_STATE_FATAL_ERR )
    {
        return;  // nothing to do
    }

    switch( stateBankUpdt )
    {
        case FUOTABANKUPDT_STATE_WRITE_FW_START:
        case FUOTABANKUPDT_STATE_WRITE_FW_RUN:
        case FUOTABANKUPDT_STATE_WRITE_FW_DONE:
            if( g_fuotaUpdtMng.state == FUOTAUPDT_STATE_NONE )
            {
                // Minimum initiallization
            }

            if( stateBankUpdt == FUOTABANKUPDT_STATE_WRITE_FW_DONE )
            {
                g_fuotaUpdtMng.state = FUOTAUPDT_STATE_SUCCESS;
                // notify to upper
                g_fuotaUpdtEventCbFuncs.AppFuotaUpdateReadyIndication();  // non-null
            }
            else
            {
                // Process in app_fuota_fwupdt.c is success. Waiting for the ready.
                g_fuotaUpdtMng.state = FUOTAUPDT_STATE_SUCCESS_WAITING_READY;
            }

            break;

        // Set error state
        case FUOTABANKUPDT_STATE_ERR_WRITE_FW:
            g_fuotaUpdtMng.state = FUOTAUPDT_STATE_FAILED;
            // notify to upper
            g_fuotaUpdtEventCbFuncs.AppFuotaUpdateErrorIndication( FUOTAUPDT_UPDATE_READY_ERR_UPDATE_FAILED );  // non-null
            break;

        // nothing to do
        // case FUOTABANKUPDT_STATE_IDLE:
        // case FUOTABANKUPDT_STATE_SWAP_0_TO_1:
        // case FUOTABANKUPDT_STATE_SWAP_1_TO_0:
        default:
            break;
    }
}

/*!
 * Event/Callback functions from FW update (app_fuota_fwupdate_bank.c): Update has been finished.
 */
static void AppFuotaUpdate_EventUpdateFinished( bool bIsSuccess )
{
    // notify to upper
    g_fuotaUpdtEventCbFuncs.AppFuotaUpdateFinishedIndication( bIsSuccess );  // non-null
}
#endif
