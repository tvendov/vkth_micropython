/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    privatelora_proc.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/


#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "board.h"

#include "privatelora_sample.h"
#include "nvm.h"

/*------------------------*/
/* global variable (const) */
// NVM - format version
const uint8_t appPrvLoRaNvmFormatVer[ APP_PRVLORA_DATA_FORMAT_VERSION_LEN ] = { APP_PRVLORA_DATA_FORMAT_VERSION };

/*-----------------*/
/* global variable */
// NVM parameters
AppPrvLoRaNvmParameters_t appPrvLoRaNvmParameters = { .txCycleMng.isReady = false };

// parameters for application
static AppPrvLoRaSettings_t         *gp_appPrvLoraSettings      = &( appPrvLoRaNvmParameters.prvLoraSettings );
static AppPrvLoRaTxCycleMng_t       *gp_appPrvLoraTxCycleMng    = &( appPrvLoRaNvmParameters.txCycleMng );
static AppPrvLoRaRemoteDevInfo_t    *gp_appPrvLoRaRemoteDevInfo = &( appPrvLoRaNvmParameters.remoteDevInfo[ 0 ] );
// NVM - format version
static uint8_t *gp_appPrvLoRaNvmReadFormatVer = &( appPrvLoRaNvmParameters.nvmFormatVer[ 0 ] );

/*--------------------*/
/* function prototype */
static AppPrvLoRaRemoteDevInfo_t *AppPrvLoRaRmtDevInfoSearchEntry( uint8_t *p_devMacAddr, uint8_t *p_index );
static AppPrvLoRaRemoteDevInfo_t *AppPrvLoRaRmtDevInfoGetFreeEntry( uint8_t *p_devMacAddr, uint8_t *p_index );

static void AppPrvLoRaNvmDataRead( uint32_t readValFlags, uint8_t varIndex, uint32_t *p_readResultFlags );
static void AppPrvLoRaNvmDataWrite( uint32_t writeValFlags, uint8_t varIndex, uint32_t *p_writeResultFlags );

static bool AppPrvLoRaSubCheckMacAddr( uint8_t *p_macAddr );

//--------------------------------------------------------------------------------------------------
// PrivateLoRaInitialization

/*!
 * initialize PrivateLoRa stack and set parameters
 */
PrvLoRaStatus_t AppPrvLoRaInit( void )
{
    PrvLoRaStatus_t             status;
    PrvLoRaStatus_t             funcRet;
    static PrvLoRaPrimitives_t  appPrvLoRaPrimitives;   // static: necessary to keep pointers to primitives

    // initialize PrivateLoRa
    appPrvLoRaPrimitives.PrvLoRaMacMcpsConfirm    = AppPrvLoRaCallbackMcpsConfirm;
    appPrvLoRaPrimitives.PrvLoRaMacMcpsIndication = AppPrvLoRaCallbackMcpsIndication;
    appPrvLoRaPrimitives.PrvLoRaMacMlmeConfirm    = AppPrvLoRaCallbackMlmeConfirm;
    appPrvLoRaPrimitives.PrvLoRaMacMlmeIndication = AppPrvLoRaCallbackMlmeIndication;
    appPrvLoRaPrimitives.PrvLoRaMacNotification   = AppPrvLoRaCallbackMacNotification;

    status = PrivateLoRaInitialization( &appPrvLoRaPrimitives, gp_appPrvLoraSettings->region );
    if( status == PRVLORA_STATUS_OK )
    {
        // Set parametes
        funcRet = AppPrvLoRaSetMacAddr( gp_appPrvLoraSettings->macAddr );
        if( funcRet != PRVLORA_STATUS_OK )
        {
            AppPrvLoRaGetMacAddr( gp_appPrvLoraSettings->macAddr );
        }
        
        funcRet = AppPrvLoRaSetChannelId( gp_appPrvLoraSettings->channelId );
        if( funcRet != PRVLORA_STATUS_OK )
        {
            AppPrvLoRaGetChannelId( &( gp_appPrvLoraSettings->channelId ) );
        }
        
        funcRet = AppPrvLoRaSetDR( gp_appPrvLoraSettings->drIndex );
        if( funcRet != PRVLORA_STATUS_OK )
        {
            AppPrvLoRaGetDR( &( gp_appPrvLoraSettings->drIndex ) );
        }
        
        funcRet = AppPrvLoRaSetTxPower( gp_appPrvLoraSettings->txPower );
        if( funcRet != PRVLORA_STATUS_OK )
        {
            AppPrvLoRaGetTxPower( &( gp_appPrvLoraSettings->txPower ) );
        }

        funcRet = AppPrvLoRaSetRxOnWhenIdle( gp_appPrvLoraSettings->rxOnWhenIdle );
        if( funcRet != PRVLORA_STATUS_OK )
        {
            AppPrvLoRaGetRxOnWhenIdle( &( gp_appPrvLoraSettings->rxOnWhenIdle ) );
        }
        
        funcRet = AppPrvLoRaSetKeyReqPermit( gp_appPrvLoraSettings->permitKeyReq );
        if( funcRet != PRVLORA_STATUS_OK )
        {
            AppPrvLoRaGetKeyReqPermit( &( gp_appPrvLoraSettings->permitKeyReq ) );
        }

        // init remote device info
        AppPrvLoRaRmtDevInfoInit();

        // init tx cycle
        AppPrvLoRaTxCycleInit();
    }

    return status;
}

/*!
 * Set Region
 */
PrvLoRaStatus_t AppPrvLoRaSetRegion( PrvLoRaRegion_t region )
{
    PrvLoRaStatus_t     status;

    // init
    status = PRVLORA_STATUS_NOT_SUPPORTED;

    switch( region )
    {
#if defined(RADIO_CFG_AS_ENABLED)
        case PRVLORA_REGION_AS1:
        case PRVLORA_REGION_AS2:
        case PRVLORA_REGION_AS3:
        case PRVLORA_REGION_AS4:
        case PRVLORA_REGION_JP:
        case PRVLORA_REGION_JP_LDC:
#endif
#if defined(RADIO_CFG_EU_ENABLED)
        case PRVLORA_REGION_EU:
#endif
#if defined(RADIO_CFG_US_ENABLED)
        case PRVLORA_REGION_US:
#endif
#if defined(RADIO_CFG_AU_ENABLED)
        case PRVLORA_REGION_AU:
#endif
#if defined(RADIO_CFG_IN_ENABLED)
        case PRVLORA_REGION_IN:
#endif
#if defined(RADIO_CFG_KR_ENABLED)
        case PRVLORA_REGION_KR:
#endif
            status = PRVLORA_STATUS_OK;
            break;

        default:
            break;
    }

    if( status == PRVLORA_STATUS_OK )
    {
        gp_appPrvLoraSettings->region = region;

        // initialize LoRaWAN stack so that parameters related to region can be set
        status = AppPrvLoRaInit();
    }

    if( status == PRVLORA_STATUS_OK )
    {
        // set LoRa mode
        AppSetLoRaMode( APP_LORA_MODE_PRIVATELORA );
    }

    return status;
}

/*!
 * Get Region
 */
PrvLoRaStatus_t AppPrvLoRaGetRegion( PrvLoRaRegion_t *p_region )
{
    PrvLoRaStatus_t status;

    // init
    status = PRVLORA_STATUS_ERROR;

    if( p_region != NULL )
    {
        (*p_region) = gp_appPrvLoraSettings->region;
        status = PRVLORA_STATUS_OK;
    }

    return status;
}

/*!
 * Start PrivateLoRa
 */
PrvLoRaStatus_t AppPrvLoRaStart( void )
{
    PrvLoRaStatus_t     status;

    // Start Private LoRa
    status = PrivateLoRaStart();

    return status;
}

/*!
 * Stop PrivateLoRa
 */
PrvLoRaStatus_t AppPrvLoRaStop( void )
{
    PrvLoRaStatus_t     status;

    // Stop Private LoRa
    status = PrivateLoRaStop();
    PrivateLoRaProcess();

    return status;
}

//--------------------------------------------------------------------------------------------------
// PrivateLoRaGet/SetRequest

/*!
 * Set request: PRVLORA_IB_MACADDR
 */
PrvLoRaStatus_t AppPrvLoRaSetMacAddr( uint8_t *p_macAddr )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibSet;
    int                 compare;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( p_macAddr != NULL )
    {
        memcpy( &(ibSet.macAddr[0]), p_macAddr, sizeof(ibSet.macAddr) );
        status = PrivateLoRaSetRequest( PRVLORA_IB_MACADDR, &ibSet );
    }

    if( status == PRVLORA_STATUS_OK )
    {
        compare = memcmp( &( gp_appPrvLoraSettings->macAddr[ 0 ] ), &( ibSet.macAddr[ 0 ] ), APP_PRVLORA_LEN_MACADDR );
        if( compare != 0 )
        {
            memcpy( &( gp_appPrvLoraSettings->macAddr[ 0 ] ), &( ibSet.macAddr[ 0 ] ), APP_PRVLORA_LEN_MACADDR );
        }
    }

    return status;
}

/*!
 * Get request: PRVLORA_IB_MACADDR
 */
PrvLoRaStatus_t AppPrvLoRaGetMacAddr( uint8_t *p_macAddr )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibGet;
    int                 compare;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( p_macAddr != NULL )
    {
        status = PrivateLoRaGetRequest( PRVLORA_IB_MACADDR, &ibGet );
    }

    if( status == PRVLORA_STATUS_OK )
    {
        compare = memcmp( &( gp_appPrvLoraSettings->macAddr[ 0 ] ), &( ibGet.macAddr[ 0 ] ), APP_PRVLORA_LEN_MACADDR );
        if( compare != 0 )
        {
            memcpy( &( gp_appPrvLoraSettings->macAddr[ 0 ] ), &( ibGet.macAddr[ 0 ] ), APP_PRVLORA_LEN_MACADDR );
        }

        memcpy( p_macAddr, &( ibGet.macAddr[ 0 ] ), APP_PRVLORA_LEN_MACADDR );
    }

    return status;
}

/*!
 * Set request: PRVLORA_IB_CHANNEL_ID
 */
PrvLoRaStatus_t AppPrvLoRaSetChannelId( uint8_t channelId )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibSet;

    ibSet.channelId = channelId;
    status = PrivateLoRaSetRequest( PRVLORA_IB_CHANNEL_ID, &ibSet );
    if( status == PRVLORA_STATUS_OK )
    {
        gp_appPrvLoraSettings->channelId = channelId;
    }

    return status;
}

/*!
 * Get request: PRVLORA_IB_CHANNEL_ID
 */
PrvLoRaStatus_t AppPrvLoRaGetChannelId( uint8_t *p_channelId )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibGet;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( p_channelId != NULL )
    {
        status = PrivateLoRaGetRequest( PRVLORA_IB_CHANNEL_ID, &ibGet );
    }

    if( status == PRVLORA_STATUS_OK )
    {
        (*p_channelId) = ibGet.channelId;
    }

    return status;
}

/*!
 * Set Request: PRVLORA_IB_DR
 */
PrvLoRaStatus_t AppPrvLoRaSetDR( uint8_t drIndex )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibSet;

    ibSet.drIndex = drIndex;
    status = PrivateLoRaSetRequest( PRVLORA_IB_DR, &ibSet );
    if( status == PRVLORA_STATUS_OK )
    {
        gp_appPrvLoraSettings->drIndex = drIndex;
    }

    return status;
}

/*!
 * Get Request: PRVLORA_IB_DR
 */
PrvLoRaStatus_t AppPrvLoRaGetDR( uint8_t *p_drIndex )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibGet;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( p_drIndex != NULL )
    {
        status = PrivateLoRaGetRequest( PRVLORA_IB_DR, &ibGet );
    }

    if( status == PRVLORA_STATUS_OK )
    {
        (*p_drIndex) = ibGet.drIndex;
    }

    return status;
}

/*!
 * Set Request: PRVLORA_IB_TXPOWER
 */
PrvLoRaStatus_t AppPrvLoRaSetTxPower( int8_t txPower )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibSet;

    ibSet.txPower = txPower;
    status = PrivateLoRaSetRequest( PRVLORA_IB_TXPOWER, &ibSet );
    if( status == PRVLORA_STATUS_OK )
    {
        if( gp_appPrvLoraSettings->txPower != txPower )
        {
            gp_appPrvLoraSettings->txPower = txPower;
        }
    }

    return status;
}

/*!
 * Get Request: PRVLORA_IB_TXPOWER
 */
PrvLoRaStatus_t AppPrvLoRaGetTxPower( int8_t *p_txPower )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibGet;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( p_txPower != NULL )
    {
        status = PrivateLoRaGetRequest( PRVLORA_IB_TXPOWER, &ibGet );
    }

    if( status == PRVLORA_STATUS_OK )
    {
        if( gp_appPrvLoraSettings->txPower != ibGet.txPower )
        {
            gp_appPrvLoraSettings->txPower = ibGet.txPower;
        }

        (*p_txPower) = ibGet.txPower;
    }

    return status;
}

/*!
 * Set Request: PRVLORA_IB_RXONWHENIDLE
 */
PrvLoRaStatus_t AppPrvLoRaSetRxOnWhenIdle( bool rxOnWhenIdle )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibSet;

    ibSet.rxOnWhenIdle = rxOnWhenIdle;
    status = PrivateLoRaSetRequest( PRVLORA_IB_RXONWHENIDLE, &ibSet );
    if( status == PRVLORA_STATUS_OK )
    {
        if( gp_appPrvLoraSettings->rxOnWhenIdle != rxOnWhenIdle )
        {
            gp_appPrvLoraSettings->rxOnWhenIdle = rxOnWhenIdle;
        }
    }

    return status;
}

/*!
 * Get Request: PRVLORA_IB_RXONWHENIDLE
 */
PrvLoRaStatus_t AppPrvLoRaGetRxOnWhenIdle( bool *p_rxOnWhenIdle )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibGet;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( p_rxOnWhenIdle != NULL )
    {
        status = PrivateLoRaGetRequest( PRVLORA_IB_RXONWHENIDLE, &ibGet );
    }

    if( status == PRVLORA_STATUS_OK )
    {
        if( gp_appPrvLoraSettings->rxOnWhenIdle != ibGet.rxOnWhenIdle )
        {
            gp_appPrvLoraSettings->rxOnWhenIdle = ibGet.rxOnWhenIdle;
        }

        (*p_rxOnWhenIdle) = ibGet.rxOnWhenIdle;
    }

    return status;
}

/*!
 * Set Request: PRVLORA_IB_KEYREQ_PERMISSION
 */
PrvLoRaStatus_t AppPrvLoRaSetKeyReqPermit( bool permitKeyReq )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibSet;

    ibSet.keyReqPermit = permitKeyReq;
    status = PrivateLoRaSetRequest( PRVLORA_IB_KEYREQ_PERMISSION, &ibSet );
    if( status == PRVLORA_STATUS_OK )
    {
        if( gp_appPrvLoraSettings->permitKeyReq != permitKeyReq )
        {
            gp_appPrvLoraSettings->permitKeyReq = permitKeyReq;
        }
    }

    return status;
}

/*!
 * Get Request: PRVLORA_IB_KEYREQ_PERMISSION
 */
PrvLoRaStatus_t AppPrvLoRaGetKeyReqPermit( bool *p_permitKeyReq )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibGet;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( p_permitKeyReq != NULL )
    {
        status = PrivateLoRaGetRequest( PRVLORA_IB_KEYREQ_PERMISSION, &ibGet );
    }

    if( status == PRVLORA_STATUS_OK )
    {
        if( gp_appPrvLoraSettings->permitKeyReq != ibGet.keyReqPermit )
        {
            gp_appPrvLoraSettings->permitKeyReq = ibGet.keyReqPermit;
        }

        (*p_permitKeyReq) = ibGet.keyReqPermit;
    }

    return status;
}

/*!
 * Set Request: PRVLORA_IB_TXCYCLE_TIME
 */
PrvLoRaStatus_t AppPrvLoRaSetTxCycleTime( uint8_t *p_dstMacAddr, uint32_t txCycleTime )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibSet;

    memcpy( &( ibSet.txCycle.dstMacAddr ), p_dstMacAddr, APP_PRVLORA_LEN_MACADDR );
    ibSet.txCycle.txCycleTime = txCycleTime;
    status = PrivateLoRaSetRequest( PRVLORA_IB_TXCYCLE_TIME, &ibSet );

    return status;
}

/*!
 * Get Request: PRVLORA_IB_TXCYCLE_TIME
 */
PrvLoRaStatus_t AppPrvLoRaGetTxCycleTime( uint8_t *p_dstMacAddr, uint32_t *p_txCycleTime )
{
    PrvLoRaStatus_t     status;
    PrvLoRaIbRequest_t  ibGet;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( ( p_dstMacAddr != NULL ) && ( p_txCycleTime != NULL ) )
    {
        status = PrivateLoRaGetRequest( PRVLORA_IB_TXCYCLE_TIME, &ibGet );
    }

    if( status == PRVLORA_STATUS_OK )
    {
        memcpy( p_dstMacAddr, &( ibGet.txCycle.dstMacAddr ), APP_PRVLORA_LEN_MACADDR );
        (*p_txCycleTime) = ibGet.txCycle.txCycleTime;
    }

    return status;
}


//--------------------------------------------------------------------------------------------------
// PrivateLoRaRegisterRemoteDevice

/*!
 * Set remote device information
 */
PrvLoRaStatus_t AppPrvLoRaSetRemoteDeviceInfo( uint8_t *p_remoteMacAddr, uint8_t *p_psk )
{
    PrvLoRaStatus_t     status;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( ( p_remoteMacAddr != NULL ) && ( p_psk != NULL ) )
    {
        status = PrivateLoRaRegisterRemoteDevice( p_remoteMacAddr, p_psk, NULL, 0, (uint32_t)(-1) );
        if( status == PRVLORA_STATUS_OK )
        {
            AppPrvLoRaRmtDevInfoRegister( p_remoteMacAddr, p_psk, NULL, 0, (uint32_t)(-1) );
        }
    }

    return status;
}

/*!
 * Clear remote device information
 */
PrvLoRaStatus_t AppPrvLoRaClearRemoteDeviceInfo( uint8_t *p_remoteMacAddr )
{
    PrvLoRaStatus_t     status;

    // (note) When p_remoteMacAddr is NULL, all remote device information is cleared.
    status = PrivateLoRaUnregisterRemoteDevice( p_remoteMacAddr );
    if( status == PRVLORA_STATUS_OK )
    {
        AppPrvLoRaRmtDevInfoUnregister( p_remoteMacAddr );
    }

    return status;
}


//--------------------------------------------------------------------------------------------------
// PrivateLoRaMcpsRequest

/*!
 * Set txoptions
 */
PrvLoRaStatus_t AppPrvLoRaSetTxOptions( PrvLoRaTxOptions_t txOptions )
{
    PrvLoRaTxOptions_t  tempTxOpt;

    // init
    tempTxOpt.txOptValue        = txOptions.txOptValue;
    tempTxOpt.options._reserved = 0;

    if( gp_appPrvLoraSettings->txOptions.txOptValue != tempTxOpt.txOptValue )
    {
        gp_appPrvLoraSettings->txOptions.txOptValue = tempTxOpt.txOptValue;
    }

    return PRVLORA_STATUS_OK;
}

/*!
 * Read txoptions
 */
PrvLoRaStatus_t AppPrvLoRaGetTxOptions( PrvLoRaTxOptions_t *p_txOptions )
{
    PrvLoRaStatus_t     status;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( p_txOptions != NULL )
    {
        p_txOptions->txOptValue = gp_appPrvLoraSettings->txOptions.txOptValue;
        status = PRVLORA_STATUS_OK;
    }

    return status;
}

/*!
 * Send data
 */
PrvLoRaStatus_t AppPrvLoRaSendData( uint8_t *p_dstMacAddr, uint8_t *p_data, uint8_t dataSize, uint16_t txHandle )
{
    PrvLoRaStatus_t     status;
    PrvLoRaMcpsReq_t    mcpsReq;

    // initial check (arg)
    if( p_dstMacAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }
    if( ( p_data == NULL ) && ( dataSize > 0 ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( dataSize <= PRVLORA_TXDATA_MAXSIZE )
    {
        memcpy( &(mcpsReq.dstMacAddr[0]), p_dstMacAddr, PRVLORA_MACADDR_SIZE );
        mcpsReq.p_txData             = p_data;
        mcpsReq.txDataSize           = dataSize;
        mcpsReq.txHandle             = txHandle;
        mcpsReq.txOptions.txOptValue = gp_appPrvLoraSettings->txOptions.txOptValue;

        status = PrivateLoRaMcpsRequest( &mcpsReq );

        // Reset/Restart tx cyclic timer
        AppPrvLoRaTxCycleUpdateTimer();
    }

    return status;
}


//--------------------------------------------------------------------------------------------------
// PrivateLoRaMlmeRequest

/*!
 * MlmeRequest - PRVLORA_MLME_KEY
 */
PrvLoRaStatus_t AppPrvLoRaKeyRequest( uint8_t *p_dstMacAddr )
{
    PrvLoRaStatus_t     status;
    PrvLoRaMlmeReq_t    mlmeReq;

    // init
    memset( &mlmeReq, 0x00, sizeof(PrvLoRaMlmeReq_t) );

    mlmeReq.mlmeType = PRVLORA_MLME_KEY;

    if( p_dstMacAddr != NULL )
    {
        memcpy( &(mlmeReq.req.keyReq.dstMacAddr[0]), p_dstMacAddr, PRVLORA_MACADDR_SIZE );
    }
    mlmeReq.req.keyReq.txOptions.txOptValue = gp_appPrvLoraSettings->txOptions.txOptValue;

    status = PrivateLoRaMlmeRequest( &mlmeReq );

    return status;
}

/*!
 * MlmeRequest - PRVLORA_MLME_DEVINFO
 */
PrvLoRaStatus_t AppPrvLoRaDevInfoReq( uint8_t *p_dstMacAddr )
{
    PrvLoRaStatus_t     status;
    PrvLoRaMlmeReq_t    mlmeReq;

    // initial check (arg)
    if( p_dstMacAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    mlmeReq.mlmeType = PRVLORA_MLME_DEVINFO;
    memcpy( &(mlmeReq.req.devInfoReq.dstMacAddr[0]), p_dstMacAddr, PRVLORA_MACADDR_SIZE );
    mlmeReq.req.devInfoReq.txOptions.txOptValue = gp_appPrvLoraSettings->txOptions.txOptValue;

    status = PrivateLoRaMlmeRequest( &mlmeReq );

    return status;
}

/*!
 * MlmeRequest - PRVLORA_MLME_TXCYCLE
 */
PrvLoRaStatus_t AppPrvLoRaTxCycleReq( uint8_t *p_dstMacAddr, uint32_t txCycleTime )
{
    PrvLoRaStatus_t     status;
    PrvLoRaMlmeReq_t    mlmeReq;

    // initial check (arg)
    if( p_dstMacAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    mlmeReq.mlmeType = PRVLORA_MLME_TXCYCLE;
    memcpy( &(mlmeReq.req.txCycleReq.dstMacAddr[0]), p_dstMacAddr, PRVLORA_MACADDR_SIZE );
    mlmeReq.req.txCycleReq.txOptions.txOptValue = gp_appPrvLoraSettings->txOptions.txOptValue;
    mlmeReq.req.txCycleReq.txCycleTime          = txCycleTime;

    status = PrivateLoRaMlmeRequest( &mlmeReq );

    return status;
}

//--------------------------------------------------------------------------------------------------
// PrivateLoRa application

/*!
 * Reset MCU
 */
void AppPrvLoRaMcuReset( void )
{
    BoardResetMcu();
}

/*!
 * Set display RSSI mode
 */
PrvLoRaStatus_t AppPrvLoRaSetRssi( bool dispRssi )
{
    gp_appPrvLoraSettings->dispRssi = dispRssi;

    return PRVLORA_STATUS_OK;
}

/*!
 * Get display RSSI mode
 */
PrvLoRaStatus_t AppPrvLoRaGetRssi( bool *p_dispRssi )
{
    PrvLoRaStatus_t     status;

    // init
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    if( p_dispRssi != NULL )
    {
        (*p_dispRssi) = gp_appPrvLoraSettings->dispRssi;
        status        = PRVLORA_STATUS_OK;
    }

    return status;
}

//----------------------------
// Remote device information
//----------------------------

/*!
 * Remote device info - init
 */
void AppPrvLoRaRmtDevInfoInit( void )
{
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo;
    PrvLoRaStatus_t             funcRet;
    uint8_t                     *p_sessionKey;
    uint8_t                     i, j;

    // init
    p_remoteDevInfo = &( gp_appPrvLoRaRemoteDevInfo[ 0 ] );

    // (mac) remove all entry
    PrivateLoRaUnregisterRemoteDevice( NULL );

    for( i = 0; i < PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM; i++ )
    {
        // init
        p_sessionKey = NULL;

        // check MAC address
        // * check whether remote device info has been read from NVM
        p_remoteDevInfo->isValid = AppPrvLoRaSubCheckMacAddr( p_remoteDevInfo->devMacAddr );

        if( p_remoteDevInfo->isValid == true )
        {
            // check Session Key
            for( j = 0; j < APP_PRVLORA_LEN_SECKEY; j++ )
            {
                if( p_remoteDevInfo->sessionKey[ j ] != 0x00 )
                {
                    p_sessionKey = p_remoteDevInfo->sessionKey;
                }
            }

            // (mac) register entry
            funcRet = PrivateLoRaRegisterRemoteDevice( p_remoteDevInfo->devMacAddr,
                                                       p_remoteDevInfo->psk,
                                                       p_sessionKey,
                                                       p_remoteDevInfo->frameCounterTx,
                                                       p_remoteDevInfo->frameCounterRx );
            if( funcRet != PRVLORA_STATUS_OK )
            {
                AppPrvLoRaRmtDevInfoUnregister( p_remoteDevInfo->devMacAddr );
            }
        }

        // next
        p_remoteDevInfo++;
    }
}

/*!
 * Remote device info - register (or overwrite) device info entry
 */
PrvLoRaStatus_t AppPrvLoRaRmtDevInfoRegister( uint8_t  *p_devMacAddr,
                                              uint8_t  *p_psk,
                                              uint8_t  *p_sessionKey,
                                              uint32_t frameCounterTx,
                                              uint32_t frameCounterRx )
{
    PrvLoRaStatus_t             status;
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo;
    uint8_t                     zeroKey[ APP_PRVLORA_LEN_SECKEY ];
    uint8_t                     index;
    uint32_t                    writeValFlags;
    int                         compare;

    // initial check
    if( p_devMacAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    status = PRVLORA_STATUS_ERROR;
    memset( zeroKey, 0x00, APP_PRVLORA_LEN_SECKEY );
    if( p_psk == NULL )
    {
        p_psk = zeroKey;
    }
    if( p_sessionKey == NULL )
    {
        p_sessionKey = zeroKey;
    }
    writeValFlags = 0;

    // get entry
    p_remoteDevInfo = AppPrvLoRaRmtDevInfoGetFreeEntry( p_devMacAddr, &index );
    if( p_remoteDevInfo != NULL )
    {
        if( p_remoteDevInfo->isValid == false )
        {
            // free entry. set MAC address
            memcpy( p_remoteDevInfo->devMacAddr, p_devMacAddr, APP_PRVLORA_LEN_MACADDR );
            writeValFlags |= APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_MACADDR;
        }
        // PSK
        compare = memcmp( p_remoteDevInfo->psk, p_psk, APP_PRVLORA_LEN_SECKEY );
        if( compare != 0 )
        {
            memcpy( p_remoteDevInfo->psk,  p_psk,  APP_PRVLORA_LEN_SECKEY );
            writeValFlags |= APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_PSK;
        }
        // Session Key
        compare = memcmp( p_remoteDevInfo->sessionKey, p_sessionKey, APP_PRVLORA_LEN_SECKEY );
        if( compare != 0 )
        {
            memcpy( p_remoteDevInfo->sessionKey, p_sessionKey, APP_PRVLORA_LEN_SECKEY );
            writeValFlags |= APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_SESSION_KEY;
        }
        // FrameCounter Tx
        if( p_remoteDevInfo->frameCounterTx != frameCounterTx )
        {
            p_remoteDevInfo->frameCounterTx = frameCounterTx;
            writeValFlags |= APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_FCNTTX;
        }
        // FrameCounter Rx
        if( p_remoteDevInfo->frameCounterRx != frameCounterRx )
        {
            p_remoteDevInfo->frameCounterRx = frameCounterRx;
            writeValFlags |= APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_FCNTRX;
        }

        // Write to NVM
        if( writeValFlags != 0 )
        {
            AppPrvLoRaNvmDataWrite( writeValFlags, index, NULL );
        }

        p_remoteDevInfo->isValid = true;
        status = PRVLORA_STATUS_OK;
    }

    return status;
}

/*!
 * Remote device info - unregister device info entry
 */
PrvLoRaStatus_t AppPrvLoRaRmtDevInfoUnregister( uint8_t *p_devMacAddr )
{
    PrvLoRaStatus_t             status;
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo;
    uint8_t                     index;

    // init
    status = PRVLORA_STATUS_ERROR;

    if( p_devMacAddr != NULL )
    {
        // delete specific entry from application and NVM
        p_remoteDevInfo = AppPrvLoRaRmtDevInfoSearchEntry( p_devMacAddr, &index );
        if( p_remoteDevInfo != NULL )
        {
            // delete TxCycle mng before delete entry (if mac address is same)
            AppPrvLoRaTxCycleClearParameter( p_devMacAddr );

            memset( p_remoteDevInfo, 0x00, sizeof(AppPrvLoRaRemoteDevInfo_t) );
            AppPrvLoRaNvmDataWrite( APP_PRVLORA_NVMDATA_RWFLG_REMOTEDEVICE, index, NULL );

            status = PRVLORA_STATUS_OK;
        }
    }
    else
    {
        // delete all entries from application and NVM
        p_remoteDevInfo = &( gp_appPrvLoRaRemoteDevInfo[ 0 ] );

        for( index = 0; index < PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM; index++ )
        {
            // delete TxCycle mng before delete entry (if mac address is same)
            AppPrvLoRaTxCycleClearParameter( p_remoteDevInfo->devMacAddr );

            memset( p_remoteDevInfo, 0x00, sizeof(AppPrvLoRaRemoteDevInfo_t) );
            AppPrvLoRaNvmDataWrite( APP_PRVLORA_NVMDATA_RWFLG_REMOTEDEVICE, index, NULL );

            // next
            p_remoteDevInfo++;
        }
    }

    return status;
}

/*!
 * Remote device info - update parameter - Session Key
 */
PrvLoRaStatus_t AppPrvLoRaRmtDevInfoUpdateSessionKey( uint8_t *p_devMacAddr, uint8_t *p_sessionKey )
{
    PrvLoRaStatus_t             status;
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo;
    uint8_t                     index;
    int                         compare;

    // initial check
    if( p_devMacAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    status = PRVLORA_STATUS_ERROR;

    // get entry
    p_remoteDevInfo = AppPrvLoRaRmtDevInfoSearchEntry( p_devMacAddr, &index );
    if( p_remoteDevInfo != NULL )
    {
        compare = memcmp( p_remoteDevInfo->sessionKey, p_sessionKey, APP_PRVLORA_LEN_SECKEY );
        if( compare != 0 )
        {
            memcpy( p_remoteDevInfo->sessionKey, p_sessionKey, APP_PRVLORA_LEN_SECKEY );
            AppPrvLoRaNvmDataWrite( APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_SESSION_KEY, index, NULL );
        }

        status = PRVLORA_STATUS_OK;
    }

    return status;
}

/*!
 * Remote device info - update parameter - FrameCounter(Tx)
 */
PrvLoRaStatus_t AppPrvLoRaRmtDevInfoUpdateFrameCounterTx( uint8_t *p_devMacAddr, uint32_t frameCounterTx )
{
    PrvLoRaStatus_t             status;
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo;
    uint8_t                     index;

    // initial check
    if( p_devMacAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    status = PRVLORA_STATUS_ERROR;

    // get entry
    p_remoteDevInfo = AppPrvLoRaRmtDevInfoSearchEntry( p_devMacAddr, &index );
    if( p_remoteDevInfo != NULL )
    {
        if( p_remoteDevInfo->frameCounterTx != frameCounterTx )
        {
            p_remoteDevInfo->frameCounterTx = frameCounterTx;
            AppPrvLoRaNvmDataWrite( APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_FCNTTX, index, NULL );
        }

        status = PRVLORA_STATUS_OK;
    }

    return status;
}

/*!
 * Remote device info - update parameter - FrameCounter(Rx)
 */
PrvLoRaStatus_t AppPrvLoRaRmtDevInfoUpdateFrameCounterRx( uint8_t *p_devMacAddr, uint32_t frameCounterRx )
{
    PrvLoRaStatus_t             status;
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo;
    uint8_t                     index;

    // initial check
    if( p_devMacAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    status = PRVLORA_STATUS_ERROR;

    // get entry
    p_remoteDevInfo = AppPrvLoRaRmtDevInfoSearchEntry( p_devMacAddr, &index );
    if( p_remoteDevInfo != NULL )
    {
        if( p_remoteDevInfo->frameCounterRx != frameCounterRx )
        {
            p_remoteDevInfo->frameCounterRx = frameCounterRx;
            AppPrvLoRaNvmDataWrite( APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_FCNTRX, index, NULL );
        }

        status = PRVLORA_STATUS_OK;
    }

    return status;
}


/*!
 * Remote device info - search entry
 */
static AppPrvLoRaRemoteDevInfo_t *AppPrvLoRaRmtDevInfoSearchEntry( uint8_t *p_devMacAddr, uint8_t *p_index )
{
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo, *p_foundDevInfo;
    uint8_t                     i;
    int                         compare;

    // init
    p_remoteDevInfo = &( gp_appPrvLoRaRemoteDevInfo[ 0 ] );
    p_foundDevInfo  = NULL;

    for( i = 0; i < PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM; i++ )
    {
        if( p_remoteDevInfo->isValid == true )
        {
            compare = memcmp( p_remoteDevInfo->devMacAddr, p_devMacAddr, APP_PRVLORA_LEN_MACADDR );
            if( compare == 0 )
            {
                // found
                p_foundDevInfo = p_remoteDevInfo;
                if( p_index != NULL )
                {
                    (*p_index) = i;
                }

                break;  // exit for(i) loop
            }
        }

        // next
        p_remoteDevInfo++;
    }

    return p_foundDevInfo;
}

/*!
 * Remote device info - get free entry or own entry
 */
static AppPrvLoRaRemoteDevInfo_t *AppPrvLoRaRmtDevInfoGetFreeEntry( uint8_t *p_devMacAddr, uint8_t *p_index )
{
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo, *p_freeDevInfo;
    uint8_t                     i;

    // init
    p_remoteDevInfo = &( gp_appPrvLoRaRemoteDevInfo[ 0 ] );

    // search entry first
    p_freeDevInfo = AppPrvLoRaRmtDevInfoSearchEntry( p_devMacAddr, p_index );
    if( p_freeDevInfo == NULL )
    {
        // search free entry
        for( i = 0; i < PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM; i++ )
        {
            if( p_remoteDevInfo->isValid == false )
            {
                // found
                p_freeDevInfo = p_remoteDevInfo;
                if( p_index != NULL )
                {
                    (*p_index) = i;
                }

                break;  // exit for(i) loop
            }

            // next
            p_remoteDevInfo++;
        }
    }

    return p_freeDevInfo;
}

//---------------
// Tx cycle
//---------------

/*!
 * Tx cycle - init
 */
void AppPrvLoRaTxCycleInit( void )
{
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo;
    AppPrvLoRaTxCycleMng_t      txCycleZeroEntry;
    int                         compare;

    // init
    p_remoteDevInfo = NULL;
    memset( &txCycleZeroEntry, 0x00, sizeof(AppPrvLoRaTxCycleMng_t) );

    // search destination
    // * check whether tx cycle info has been read from NVM
    compare = memcmp( gp_appPrvLoraTxCycleMng, &txCycleZeroEntry, sizeof(AppPrvLoRaTxCycleMng_t) );
    if( compare != 0 )
    {
        // * check whether destination is registered.
        p_remoteDevInfo = AppPrvLoRaRmtDevInfoSearchEntry( gp_appPrvLoraTxCycleMng->dstAddr, NULL );
        if( p_remoteDevInfo != NULL )
        {
            gp_appPrvLoraTxCycleMng->isReady = true;
        }
        else
        {
            // destination is not registered. clear.
            memset( gp_appPrvLoraTxCycleMng, 0x00, sizeof(AppPrvLoRaTxCycleMng_t) );
            AppPrvLoRaNvmDataWrite( APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE, 0, NULL );
        }
    }

    // set txCycleTime to mac
    AppPrvLoRaSetTxCycleTime( gp_appPrvLoraTxCycleMng->dstAddr, gp_appPrvLoraTxCycleMng->period );

    // init timer
    TimerInit( &( gp_appPrvLoraTxCycleMng->txCycleTimer ), AppPrvLoRaCallbackTimerTxCycle );
    if( gp_appPrvLoraTxCycleMng->isReady == true )
    {
        AppPrvLoRaTxCycleUpdateTimer();  // start timer
    }
}

/*!
 * Tx cycle - set parameter
 */
void AppPrvLoRaTxCycleSetParameter( uint8_t *p_srcAddr, uint32_t txCycleTime )
{
    uint32_t    writeValFlags;
    int         compare;

    // initial check
    if( p_srcAddr == NULL )
    {
        return;
    }

    // init
    writeValFlags = 0;

    compare = memcmp( gp_appPrvLoraTxCycleMng->dstAddr, p_srcAddr, APP_PRVLORA_LEN_MACADDR );
    if( compare != 0 )
    {
        memcpy( gp_appPrvLoraTxCycleMng->dstAddr, p_srcAddr, APP_PRVLORA_LEN_MACADDR );
        writeValFlags |= APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE_MACADDR;
    }
    if( gp_appPrvLoraTxCycleMng->period != txCycleTime )
    {
        gp_appPrvLoraTxCycleMng->period = txCycleTime;
        writeValFlags |= APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE_PERIOD;
    }

    // set txCycleTime to mac
    AppPrvLoRaSetTxCycleTime( gp_appPrvLoraTxCycleMng->dstAddr, gp_appPrvLoraTxCycleMng->period );

    // write to NVM
    if( writeValFlags != 0 )
    {
        AppPrvLoRaNvmDataWrite( writeValFlags, 0, NULL );
    }

    gp_appPrvLoraTxCycleMng->isReady = true;
    AppPrvLoRaTxCycleUpdateTimer();
}

/*!
 * Tx cycle - clear parameter (if mac address is same with Mng and argument.)
 */
void AppPrvLoRaTxCycleClearParameter( uint8_t *p_dstAddr )
{
    int     compare;

    if( gp_appPrvLoraTxCycleMng->isReady == true )
    {
        compare = memcmp( p_dstAddr, gp_appPrvLoraTxCycleMng->dstAddr, APP_PRVLORA_LEN_MACADDR );
        if( compare == 0 )
        {
            AppPrvLoRaTxCycleStopTimer();

            memset( gp_appPrvLoraTxCycleMng->dstAddr, 0x00, APP_PRVLORA_LEN_MACADDR );
            gp_appPrvLoraTxCycleMng->period = 0;
            gp_appPrvLoraTxCycleMng->isReady = false;

            AppPrvLoRaNvmDataWrite( APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE, 0, NULL );
        }
    }
}

/*!
 * Tx cycle - set time
 */
void AppPrvLoRaTxCycleUpdateTimer( void )
{
    uint32_t    txCycleMsec;

    if( gp_appPrvLoraTxCycleMng->isReady == true )
    {
        AppPrvLoRaTxCycleStopTimer();
        if( gp_appPrvLoraTxCycleMng->period != 0 )
        {
            txCycleMsec = gp_appPrvLoraTxCycleMng->period * 1000;  // sec -> msec

            TimerSetValue( &( gp_appPrvLoraTxCycleMng->txCycleTimer ), txCycleMsec );
            TimerStart( &( gp_appPrvLoraTxCycleMng->txCycleTimer ) );
        }
    }
}

/*!
 * Tx cycle - stop time
 */
void AppPrvLoRaTxCycleStopTimer( void )
{
    TimerStop( &( gp_appPrvLoraTxCycleMng->txCycleTimer ) );
    gp_appPrvLoraTxCycleMng->isStart = false;
}

/*!
 * Tx cycle - send frame
 */
PrvLoRaStatus_t AppPrvLoRaTxCycleSendFrame( void )
{
    PrvLoRaStatus_t     status;
    PrvLoRaTxOptions_t  bkupTxOptions;

    // initial check
    if( gp_appPrvLoraTxCycleMng->isReady == false )
    {
        return PRVLORA_STATUS_INACTIVE;
    }
    if( gp_appPrvLoraTxCycleMng->isStart == false )
    {
        return PRVLORA_STATUS_OK;  // nothing to do
    }

    gp_appPrvLoraTxCycleMng->isStart = false;

    bkupTxOptions.txOptValue = gp_appPrvLoraSettings->txOptions.txOptValue;
    gp_appPrvLoraSettings->txOptions.options.AckRequest = 0;
    gp_appPrvLoraSettings->txOptions.options.SecEnable  = 0;

    // send
    status = AppPrvLoRaSendData( gp_appPrvLoraTxCycleMng->dstAddr, 
                                 gp_appPrvLoraTxCycleMng->txData,
                                 gp_appPrvLoraTxCycleMng->txDataLen, 0 );

    gp_appPrvLoraSettings->txOptions.txOptValue = bkupTxOptions.txOptValue;

#ifdef DEBUG_PRVLORA
    AppPrvLoRaDebugDispTxCycle( status );
#endif

    return status;
}

//----------------------------
// NVM
//----------------------------
PrvLoRaStatus_t AppPrvLoRaNvmLoadParameters( void )
{
    PrvLoRaStatus_t             status;
    int                         compare;
    uint8_t                     i;
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo;

    // init
    status          = PRVLORA_STATUS_ERROR;
    p_remoteDevInfo = &( gp_appPrvLoRaRemoteDevInfo[ 0 ] );

    memset( gp_appPrvLoRaNvmReadFormatVer, 0x00, APP_PRVLORA_DATA_FORMAT_VERSION_LEN );

    // read PrivateLoRa settings
    AppPrvLoRaNvmDataRead( APP_PRVLORA_NVMDATA_RWFLG_PRVLORASETTINGS, 0, NULL );
    compare = memcmp( gp_appPrvLoRaNvmReadFormatVer, appPrvLoRaNvmFormatVer, APP_PRVLORA_DATA_FORMAT_VERSION_LEN );
    if( compare == 0 )
    {
        // check the number of remote device info in NVM and configuration
        compare = gp_appPrvLoraSettings->maxNumRemoteDev - PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM;
    }

    if( compare == 0 )
    {
        // read TxCycle info
        AppPrvLoRaNvmDataRead( APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE, 0, NULL );

        // read remote device info
        for( i = 0; i < PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM; i++ )
        {
            AppPrvLoRaNvmDataRead( APP_PRVLORA_NVMDATA_RWFLG_REMOTEDEVICE, i, NULL );

            // check MAC address
            p_remoteDevInfo->isValid = AppPrvLoRaSubCheckMacAddr( p_remoteDevInfo->devMacAddr );

            // next
            p_remoteDevInfo++;
        }

        status = PRVLORA_STATUS_OK;
    }

    return status;
}

PrvLoRaStatus_t AppPrvLoRaNvmSaveParameters( uint32_t writeValFlags )
{
    PrvLoRaStatus_t     status;
    uint32_t            writeFlags, writeResultFlags;
    uint8_t             i;

    // init
    status = PRVLORA_STATUS_OK;

    writeFlags = writeValFlags & APP_PRVLORA_NVMDATA_RWFLG_PRVLORASETTINGS;
    if( writeFlags != 0 )
    {
        memcpy( gp_appPrvLoRaNvmReadFormatVer, appPrvLoRaNvmFormatVer, APP_PRVLORA_DATA_FORMAT_VERSION_LEN );
        AppPrvLoRaNvmDataWrite( writeFlags, 0, &writeResultFlags );
    }

    writeFlags = writeValFlags & APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE;
    if( writeFlags != 0 )
    {
        AppPrvLoRaNvmDataWrite( writeFlags, 0, &writeResultFlags );
    }

    writeFlags = writeValFlags & APP_PRVLORA_NVMDATA_RWFLG_REMOTEDEVICE;
    if( writeFlags != 0 )
    {
        for( i = 0; i < PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM; i++ )
        {
            AppPrvLoRaNvmDataWrite( writeFlags, i, &writeResultFlags );
        }
    }

    return status;
}

/*!
 * NVM - Read entries
 */
static void AppPrvLoRaNvmDataRead( uint32_t readValFlags, uint8_t varIndex, uint32_t *p_readResultFlags )
{
    const AppPrvLoRaNvmDataTable_t  *p_appPrvLoRaNvmDataTable;
    uint8_t                         i, funcRet;
    uint32_t                        readResultFlags;

    // init
    readResultFlags          = 0;
    p_appPrvLoRaNvmDataTable = &appPrvLoRaNvmDataTable[ 0 ];

    for( i = 0; i < APP_PRVLORA_NUM_NVMDATA; i++ )
    {
        if( ( p_appPrvLoRaNvmDataTable->rwReqFlag & readValFlags ) != 0 )
        {
            if( ( p_appPrvLoRaNvmDataTable->rwType == APP_PRVLORA_NVMDATA_TYPE_FIXED ) ||
                ( p_appPrvLoRaNvmDataTable->rwType == APP_PRVLORA_NVMDATA_TYPE_VAR( varIndex ) ) )
            {
                // read value(s) from data flash
                funcRet = NvmRead( p_appPrvLoRaNvmDataTable->dataId, 
                                   p_appPrvLoRaNvmDataTable->p_data, 
                                   p_appPrvLoRaNvmDataTable->dataSize );
                if( funcRet == NVM_RESULT_SUCCESS )
                {
                    readResultFlags |= p_appPrvLoRaNvmDataTable->rwReqFlag;
                }

                readValFlags = readValFlags & ~(p_appPrvLoRaNvmDataTable->rwReqFlag);
                if( readValFlags == 0x00000000 )
                {
                    // no more read
                    break;  // exit from for(i) loop
                }
            }
        }

        // next
        p_appPrvLoRaNvmDataTable++;
    }

    if( p_readResultFlags != NULL )
    {
        (*p_readResultFlags) = readResultFlags & readValFlags;
    }
}

/*!
 * NVM - Write entries
 */
static void AppPrvLoRaNvmDataWrite( uint32_t writeValFlags, uint8_t varIndex, uint32_t *p_writeResultFlags )
{
    const AppPrvLoRaNvmDataTable_t  *p_appPrvLoRaNvmDataTable;
    uint8_t                         i, funcRet;
    uint32_t                        writeResultFlags;

    // init
    writeResultFlags = 0;
    p_appPrvLoRaNvmDataTable = &appPrvLoRaNvmDataTable[ 0 ];

    for( i = 0; i < APP_PRVLORA_NUM_NVMDATA; i++ )
    {
        if( ( p_appPrvLoRaNvmDataTable->rwReqFlag & writeValFlags ) != 0 )
        {
            if( ( p_appPrvLoRaNvmDataTable->rwType == APP_PRVLORA_NVMDATA_TYPE_FIXED ) ||
                ( p_appPrvLoRaNvmDataTable->rwType == APP_PRVLORA_NVMDATA_TYPE_VAR( varIndex ) ) )
            {
                // write value(s) to data flash
                funcRet = NvmWrite( p_appPrvLoRaNvmDataTable->dataId, 
                                    p_appPrvLoRaNvmDataTable->p_data, 
                                    p_appPrvLoRaNvmDataTable->dataSize );
                if( funcRet == NVM_RESULT_SUCCESS )
                {
                    writeResultFlags |= p_appPrvLoRaNvmDataTable->rwReqFlag;
                }

                writeValFlags = writeValFlags & ~(p_appPrvLoRaNvmDataTable->rwReqFlag);
                if( writeValFlags == 0x00000000 )
                {
                    // no more write
                    break;  // exit from for(i) loop
                }
            }
        }

        // next
        p_appPrvLoRaNvmDataTable++;
    }

    if( p_writeResultFlags != NULL )
    {
        (*p_writeResultFlags) = writeResultFlags & writeValFlags;
    }
}

//--------------------------------------------------------------------------------------------------
// PrivateLoRa application (sub function)
static bool AppPrvLoRaSubCheckMacAddr( uint8_t *p_macAddr )
{
    bool        bRet;
    int         compare;
    uint8_t     checkMacAddr[ APP_PRVLORA_LEN_MACADDR ];

    // init
    bRet = false;

    // check not all 0x00 / not all 0xFF
    memset( checkMacAddr, 0x00, APP_PRVLORA_LEN_MACADDR );
    compare = memcmp( p_macAddr, checkMacAddr, APP_PRVLORA_LEN_MACADDR );
    if( compare != 0 )
    {
        memset( checkMacAddr, 0xFF, APP_PRVLORA_LEN_MACADDR );
        compare = memcmp( p_macAddr, checkMacAddr, APP_PRVLORA_LEN_MACADDR );
    }

    if( compare != 0 )
    {
        bRet = true;
    }

    return bRet;
}
