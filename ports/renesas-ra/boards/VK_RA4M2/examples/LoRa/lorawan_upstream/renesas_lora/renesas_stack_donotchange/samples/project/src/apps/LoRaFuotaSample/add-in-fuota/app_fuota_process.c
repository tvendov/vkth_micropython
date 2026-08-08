/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "board.h"
#include "radio.h"

#include "LoRaMac.h"
#include "LoRaFuotaProcess.h"
#include "app_fuota_fwupdate.h"
#include "app_fuota_at_proc.h"

#include "lora_sample.h"
#ifdef APP_COMPLIANCE
#include "app_compliance.h"
#endif

#if ( (FUOTAUPDT_SWAPMODE != FUOTAUPDT_SWAPMODE_BOOTSWAP) && (FUOTAUPDT_SWAPMODE != FUOTAUPDT_SWAPMODE_BANKSWAP) )
    #error "Error - swap mode is not defined."
#endif

/*** define ***/
/*** Event/Callback functions from FUOTA ***/
static void AppFuotaEvent_RmtMcSessionSetupIndication( DeviceClass_t sessionClass, 
                                                       uint8_t       mcGroupId, 
                                                       uint32_t      timeToStartSec,
                                                       uint32_t      timeoutSec );
static void AppFuotaEvent_RmtMcSessionStartIndication( DeviceClass_t sessionClass, 
                                                       uint8_t       mcGroupId, 
                                                       uint32_t      timeoutSec );
static void AppFuotaEvent_RmtMcSessionEndIndication( DeviceClass_t sessionClass, uint8_t mcGroupId );
static FuotaStatus_t AppFuotaEvent_FrgmntSessionSetupIndication( uint8_t fragIndex, uint32_t descriptor );
static void AppFuotaEvent_FrgmntDataBlockIndication( uint8_t  fragIndex, 
                                                     uint8_t  *p_dataBlk, 
                                                     uint32_t dataBlkSize );
static void AppFuotaEvent_FrgmntSessionEndIndication( uint8_t fragIndex );
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
static void AppFuotaEvent_FwMngVersionInfoRequest( uint32_t *p_fwVersion, uint32_t *p_hwVersion );
static FuotaStatus_t AppFuotaEvent_FwMngRebootRequestIndication( uint32_t rebootSec );
static void AppFuotaEvent_FwMngRebootCanceledIndication( void );
static void AppFuotaEvent_FwMngRebootExecIndication( void );
static uint8_t AppFuotaEvent_FwMngUpImageStatusRequest( uint32_t *p_nextFirmwareVersion );
static uint8_t AppFuotaEvent_FwMngDeleteImageRequest( uint32_t fwToDelVersion );
#endif

/*** Event/Callback functions from FW update (app_fuota_fwupdate.c) ***/
static void AppFuotaEvent_AppFuotaUpdateReady( void );
static void AppFuotaEvent_AppFuotaUpdateError( uint8_t status );
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
static void AppFuotaEvent_AppFuotaUpdateFinished( bool bIsSuccess );
#endif

/***  ***/
static void AppFuotaStartFirmwareUpdatePre( void );

/*!
 * Init FUOTA
 */
void AppFuotaInit( void )
{
    FuotaEventCb_t  appFuotaCallbacks;
    FuotaUpdateEventCb_t    appFuotaUpdateCallbacks;

    appFuotaCallbacks.FuotaRmtMcSessionSetupIndication  = AppFuotaEvent_RmtMcSessionSetupIndication;
    appFuotaCallbacks.FuotaRmtMcSessionStartIndication  = AppFuotaEvent_RmtMcSessionStartIndication;
    appFuotaCallbacks.FuotaRmtMcSessionEndIndication    = AppFuotaEvent_RmtMcSessionEndIndication;
    appFuotaCallbacks.FuotaFrgmntSessionSetupIndicaiton = AppFuotaEvent_FrgmntSessionSetupIndication;
    appFuotaCallbacks.FuotaFrgmntDataBlockIndication    = AppFuotaEvent_FrgmntDataBlockIndication;
    appFuotaCallbacks.FuotaFrgmntSessionEndIndication   = AppFuotaEvent_FrgmntSessionEndIndication;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    appFuotaCallbacks.FuotaFwMngVersionInfoRequest       = AppFuotaEvent_FwMngVersionInfoRequest;
    appFuotaCallbacks.FuotaFwMngRebootRequestIndication  = AppFuotaEvent_FwMngRebootRequestIndication;
    appFuotaCallbacks.FuotaFwMngRebootCanceledIndication = AppFuotaEvent_FwMngRebootCanceledIndication;
    appFuotaCallbacks.FuotaFwMngRebootExecIndication     = AppFuotaEvent_FwMngRebootExecIndication;
    appFuotaCallbacks.FuotaFwMngUpImageStatusRequest     = AppFuotaEvent_FwMngUpImageStatusRequest;
    appFuotaCallbacks.FuotaFwMngDeleteImageRequest       = AppFuotaEvent_FwMngDeleteImageRequest;
#endif
    FuotaInit( &appFuotaCallbacks );

    // init FW update process
    appFuotaUpdateCallbacks.AppFuotaUpdateReadyIndication    = AppFuotaEvent_AppFuotaUpdateReady;
    appFuotaUpdateCallbacks.AppFuotaUpdateErrorIndication    = AppFuotaEvent_AppFuotaUpdateError;
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
    appFuotaUpdateCallbacks.AppFuotaUpdateFinishedIndication = AppFuotaEvent_AppFuotaUpdateFinished;
#else
    appFuotaUpdateCallbacks.AppFuotaUpdateFinishedIndication = NULL;
#endif
    AppFuotaUpdateInitialization( &appFuotaUpdateCallbacks );
}

/*!
 * Start FUOTA
 */
void AppFuotaStart( void )
{
    FuotaStart();
}

/*!
 * Stop FUOTA
 */
void AppFuotaStop( void )
{
    FuotaStop();
}

/*!
 * MCPS-Indication event function for FUOTA
 */
FuotaStatus_t AppFuotaMcpsIndication( McpsIndication_t *p_mcpsIndication )
{
    FuotaStatus_t fuotaProc;

    fuotaProc = FuotaMcpsIndication( p_mcpsIndication );

    return fuotaProc;
}

/*!
 * MLME-Confirm event function for FUOTA
 */
void AppFuotaMlmeConfirm( MlmeConfirm_t *p_mlmeConfirm )
{
    FuotaMlmeConfirm( p_mlmeConfirm );
}

/*!
 * FUOTA IB get request
 */
FuotaStatus_t AppFuotaIbGetRequest( uint8_t ib, void *vpVal )
{
    FuotaStatus_t   res;

    res = FuotaIbGetRequest( ib, vpVal );

    return res;
}

/*!
 * FUOTA IB set request
 */
FuotaStatus_t AppFuotaIbSetRequest( uint8_t ib, void *vpVal )
{
    FuotaStatus_t   res;

    res = FuotaIbSetRequest( ib, vpVal );

    return res;
}

/*!
 * FUOTA process
 */
void AppFuotaProcess( void )
{
    FuotaProcess();
    AppFuotaUpdateProcess();
}

/*!
 * Start firmware update (bootswap and reset)
 */
FuotaStatus_t AppFuotaStartFirmwareUpdate( void )
{
    FuotaStatus_t   res;

    res = AppFuotaUpdateStartFwUpdate( AppFuotaStartFirmwareUpdatePre );

    // only come here in case of error.
    return res;
}

static void AppFuotaStartFirmwareUpdatePre( void )
{
    // (AT command) command response
    AppAtFuotaUpdateActResult( FUOTA_STATUS_OK );
}

/*!
 * Low Power
 */
bool AppFuotaIsLowPowerAllowed( void )
{
    bool    ret;

    ret = FuotaIsLowPowerAllowed();

    if( ret == true )
    {
        ret = AppFuotaUpdateIsLowPowerAllowed();
    }

    return ret;
}


//--------------------------------------------------------------------------------------------------

/*!
 * Event/Callback from FUOTA: Remote multicast session will be started.
 */
static void AppFuotaEvent_RmtMcSessionSetupIndication( DeviceClass_t sessionClass, 
                                                       uint8_t       mcGroupId, 
                                                       uint32_t      timeToStartSec,
                                                       uint32_t      timeoutSec )
{
    // NOTE: execute beacon acquisition in case of classB session

    // (AT command) notify to user.
    AppAtFuotaRmtMcSessionSetupIndication( sessionClass, 
                                           mcGroupId, 
                                           timeToStartSec,
                                           timeoutSec );
}

/*!
 * Event/Callback from FUOTA: Remote multicast session is started.
 */
static void AppFuotaEvent_RmtMcSessionStartIndication( DeviceClass_t sessionClass, 
                                                       uint8_t       mcGroupId, 
                                                       uint32_t      timeoutSec )
{
    // (AT command) notify to user.
    AppAtFuotaRmtMcSessionStartIndication( sessionClass, 
                                           mcGroupId, 
                                           timeoutSec );
}

/*!
 * Event/Callback from FUOTA: Remote multicast session has been deleted.
 */
static void AppFuotaEvent_RmtMcSessionEndIndication( DeviceClass_t sessionClass, uint8_t mcGroupId )
{
    // (AT command) notify to user.
    AppAtFuotaRmtMcSessionEndIndication( sessionClass, mcGroupId );
}

/*!
 * Event/Callback from FUOTA: Fragment session will be started. Check descriptor if necessary.
 */
static FuotaStatus_t AppFuotaEvent_FrgmntSessionSetupIndication( uint8_t fragIndex, uint32_t descriptor )
{
    return FUOTA_STATUS_OK;  // nothing to check
}

/*!
 * Event/Callback from FUOTA: Data block
 */
static void AppFuotaEvent_FrgmntDataBlockIndication( uint8_t  fragIndex, 
                                                     uint8_t  *p_dataBlk, 
                                                     uint32_t dataBlkSize )
{
#ifdef APP_COMPLIANCE
    // disable firmware update process if certification test program is active
    if( appLoraWanSettings.complianceTestMode != APP_COMPLIANCE_TESTMODE_NONE )
    {
        return;
    }
#endif

    // store data block to flash ROM.
    AppFuotaUpdateStoreFwImage( p_dataBlk, dataBlkSize );
}

/*!
 * Event/Callback from FUOTA: Fragment session has been deleted.
 */
static void AppFuotaEvent_FrgmntSessionEndIndication( uint8_t fragIndex )
{
}

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*!
 * Event/Callback from FUOTA: version information request from AS
 */
static void AppFuotaEvent_FwMngVersionInfoRequest( uint32_t *p_fwVersion, uint32_t *p_hwVersion )
{
    (*p_fwVersion) = AppFwVersion.Value;
    (*p_hwVersion) = AppHwVersion.Value;
}

/*!
 * Event/Callback from FUOTA: receive the reboot request from AS
 */
static FuotaStatus_t AppFuotaEvent_FwMngRebootRequestIndication( uint32_t rebootSec )
{
    FuotaStatus_t   ret;

    // (AT command) notify to user.
    AppAtFuotaUpdateTimeToRebootSecIndication( rebootSec );
    
    ret = FUOTA_STATUS_OK;

    return ret;
}

/*!
 * Event/Callback from FUOTA: cancel the reboot request
 */
static void AppFuotaEvent_FwMngRebootCanceledIndication( void )
{
    // (AT command) notify to user.
    AppAtFuotaUpdateTimeToRebootSecIndication( (uint32_t)0xFFFFFFFF );
}

/*!
 * Event/Callback from FUOTA: reboot time has come (or immediate)
 */
static void AppFuotaEvent_FwMngRebootExecIndication( void )
{
    // (AT command) notify to user.
    AppAtFuotaUpdateRebootTimingIndication();

#ifdef APP_COMPLIANCE
    // reboot program
    if( appLoraWanSettings.complianceTestMode == APP_COMPLIANCE_TESTMODE_FUOTA200 )
    {
        AppSaveParams();    // write complianceTestMode to join after reset.
        BoardResetMcu();    // reset (never returns)
    }
#endif
}

/*!
 * Event/Callback from FUOTA: request the version of F/W image
 */
static uint8_t AppFuotaEvent_FwMngUpImageStatusRequest( uint32_t *p_nextFirmwareVersion )
{
    uint8_t retStatus;  // FUOTA_FWIMG_STATUS_NONE          ... no F/W image
                        // FUOTA_FWIMG_STATUS_INVALID       ... wrong F/W image
                        // FUOTA_FWIMG_STATUS_HW_NONSUPPORT ... F/W image is not for own H/W platform
                        // FUOTA_FWIMG_STATUS_AVAILABLE     ... F/W image is valid
    uint8_t funcRet;

    // init
    retStatus = FUOTA_FWIMG_STATUS_NONE;
    (*p_nextFirmwareVersion) = 0;

    // Check firmware update state
    funcRet = AppFuotaUpdateGetStatus();
    switch( funcRet )
    {
        case FUOTAUPDT_STATE_SUCCESS:
            retStatus = FUOTA_FWIMG_STATUS_AVAILABLE;
            (*p_nextFirmwareVersion) = AppFuotaUpdateGetFirmwareImageVersion();
            break;

        case FUOTAUPDT_STATE_VERIFY_ERR:
            retStatus = FUOTA_FWIMG_STATUS_INVALID;
            break;

        default:
            break;
    }

    return retStatus;
}

/*!
 * Event/Callback from FUOTA: request to delete the F/W image
 */
static uint8_t AppFuotaEvent_FwMngDeleteImageRequest( uint32_t fwToDelVersion )
{
    uint8_t     retStatus;  // FUOTA_FWIMG_DELETEIMG_STATUS_OK               ... F/W image has been deleted
                            // FUOTA_FWIMG_DELETEIMG_STATUS_INVALID_VERSION ... version mismatch
                            // FUOTA_FWIMG_DELETEIMG_STATUS_NO_VALID_IMAGE  ... no valid F/W image
    uint8_t     fwImgStatus;
    uint32_t    fwImgVersion;

    // init
    retStatus = FUOTA_FWIMG_DELETEIMG_STATUS_NO_VALID_IMAGE;

    // use upper function
    fwImgStatus = AppFuotaEvent_FwMngUpImageStatusRequest( &fwImgVersion );
    if( fwImgStatus == FUOTA_FWIMG_STATUS_AVAILABLE )
    {
        if( fwImgVersion == fwToDelVersion )
        {
            AppFuotaUpdateReset();
            retStatus = FUOTA_FWIMG_DELETEIMG_STATUS_OK;

            // (AT command) notify to user.
            AppAtFuotaUpdateDeleteFwImageIndication( fwImgVersion );
        }
        else
        {
            retStatus = FUOTA_FWIMG_DELETEIMG_STATUS_INVALID_VERSION;
        }
    }

    return retStatus;
}
#endif

/*!
 * Event/Callback from app_fuota_fwupdate.c: ready to update
 */
static void AppFuotaEvent_AppFuotaUpdateReady( void )
{
    // (AT command) notify to user.
    AppAtFuotaUpdateReadyIndication();
}

/*!
 * Event/Callback from app_fuota_fwupdate.c: error is occurred during update preparation
 */
static void AppFuotaEvent_AppFuotaUpdateError( uint8_t status )
{
    // (AT command) notify to user.
    AppAtFuotaUpdateErrorIndication( status );
}


/*!
 * Event/Callback from app_fuota_fwupdate.c: finished FW update
 */
#if (FUOTAUPDT_SWAPMODE == FUOTAUPDT_SWAPMODE_BANKSWAP)
static void AppFuotaEvent_AppFuotaUpdateFinished( bool bIsSuccess )
{
    FuotaStatus_t   result;

    // (AT command) notify to user.
    if( bIsSuccess == true )
    {
        result = FUOTA_STATUS_OK;
    }
    else
    {
        result = FUOTA_STATUS_ERROR;
    }

    AppAtFuotaUpdateActConfirm( result );
}
#endif
