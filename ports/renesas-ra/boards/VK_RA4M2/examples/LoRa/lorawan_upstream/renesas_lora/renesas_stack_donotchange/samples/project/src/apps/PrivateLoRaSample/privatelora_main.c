/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    privatelora_main.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/


#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "board.h"

#include "privatelora_sample.h"
#include "at_proc.h"
#include "dflash.h"

#ifdef LORACOMBO_ENABLED
#include "lora_sample.h"
#endif

/*--------*/
/* define */

#define APP_PRVLORA_MAINSTATE_INACTIVE      0
#define APP_PRVLORA_MAINSTATE_ACTIVE        1

// default PrivateLoRa sample application setting
#if defined(RADIO_CFG_AS_ENABLED)
    #define APP_PRVLORA_DEFAULT_RADIO_CFG   PRVLORA_REGION_AS1
    #define APP_PRVLORA_DEFAULT_DR_INDEX    2
#elif defined(RADIO_CFG_EU_ENABLED)
    #define APP_PRVLORA_DEFAULT_RADIO_CFG   PRVLORA_REGION_EU
    #define APP_PRVLORA_DEFAULT_DR_INDEX    2
#elif defined(RADIO_CFG_US_ENABLED)
    #define APP_PRVLORA_DEFAULT_RADIO_CFG   PRVLORA_REGION_US
    #define APP_PRVLORA_DEFAULT_DR_INDEX    0
#elif defined(RADIO_CFG_AU_ENABLED)
    #define APP_PRVLORA_DEFAULT_RADIO_CFG   PRVLORA_REGION_AU
    #define APP_PRVLORA_DEFAULT_DR_INDEX    0
#elif defined(RADIO_CFG_IN_ENABLED)
    #define APP_PRVLORA_DEFAULT_RADIO_CFG   PRVLORA_REGION_IN
    #define APP_PRVLORA_DEFAULT_DR_INDEX    2
#elif defined(RADIO_CFG_KR_ENABLED)
    #define APP_PRVLORA_DEFAULT_RADIO_CFG   PRVLORA_REGION_KR
    #define APP_PRVLORA_DEFAULT_DR_INDEX    2
#else
#error "Please set correct REGION macro."
#endif

/*-----------------*/
/* global variable */

uint8_t appPrvLoRaMainState = APP_PRVLORA_MAINSTATE_INACTIVE;

/*-------------------------*/
/* global variable (const) */

// Version information
const Version_t appPrvLoRaFwVersion = { .Value = APP_PRVLORA_VERSION_FW };
const Version_t appPrvLoRaHwVersion = { .Value = APP_PRVLORA_VERSION_HW };

// default PrivateLoRa sample application setting
const AppPrvLoRaSettings_t appPrvLoraDefaultSettings = 
{
    .region          = APP_PRVLORA_DEFAULT_RADIO_CFG,
    .maxNumRemoteDev = PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM,
    .macAddr         = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF },
    .channelId       = 0x00,
    .drIndex         = APP_PRVLORA_DEFAULT_DR_INDEX,
    .txPower         = 0,
    .rxOnWhenIdle    = false,
    .permitKeyReq    = false,
    .dispRssi        = false,
};

/*-----------*/
/* functions */


//--------------------------------------------------------------------------------------------------
// init

/*!
 * Initialize
 */
void AppPrvLoRaMainInit( void )
{
    PrvLoRaStatus_t     status;

#ifdef DEBUG_PRVLORA
    // init debug mode
    AppPrvLoRaDebugInit();
#endif

    // load parameters
    AppPrvLoRaMainLoadParams();

    // init PrivateLoRa
    status = AppPrvLoRaInit();
    if( status == PRVLORA_STATUS_NOT_SUPPORTED )
    {
        // Change region to default and re-initialization
        appPrvLoRaNvmParameters.prvLoraSettings.region = appPrvLoraDefaultSettings.region;
        status = AppPrvLoRaInit();
        if( status == PRVLORA_STATUS_OK )
        {
            print( "Changed region setting. (but not saved to non-volatile memory.)" );
            AtPrintTrailer();
        }
    }
    if( status == PRVLORA_STATUS_NOT_SUPPORTED )
    {
        print( "Region is not available." );
        AtPrintTrailer();
    }
    else if ( status == PRVLORA_STATUS_RADIO_ERROR )
    {
        print( "Radio driver initialization is failed." );
        AtPrintTrailer();
    }
    else if ( status == PRVLORA_STATUS_PARAMETER_INVALID )
    {
        print( "Parameters for Mac Initialization are invalid." );
        AtPrintTrailer();
    }
}

/*!
 * Start
 */
uint8_t AppPrvLoRaMainStart( void )
{
    uint8_t             ret;
    PrvLoRaStatus_t     funcRet;

    // init
    ret = APP_PRVLORA_STATUS_OK;

    if( appPrvLoRaMainState == APP_PRVLORA_MAINSTATE_INACTIVE )
    {
        funcRet = AppPrvLoRaStart();
        if( funcRet == PRVLORA_STATUS_OK )
        {
            // (re)start Tx cycle
            AppPrvLoRaTxCycleUpdateTimer();

            appPrvLoRaMainState = APP_PRVLORA_MAINSTATE_ACTIVE;
        }
        else
        {
            ret = APP_PRVLORA_STATUS_ERROR;
        }
    }

    return ret;
}

/*!
 * Stop
 */
uint8_t AppPrvLoRaMainStop( void )
{
    uint8_t             ret;
    PrvLoRaStatus_t     funcRet;

    // init
    ret = APP_PRVLORA_STATUS_OK;

    if( appPrvLoRaMainState == APP_PRVLORA_MAINSTATE_ACTIVE )
    {
        // stop Tx cycle
        AppPrvLoRaTxCycleStopTimer();

        funcRet = AppPrvLoRaStop();
        if( funcRet == PRVLORA_STATUS_OK )
        {
            appPrvLoRaMainState = APP_PRVLORA_MAINSTATE_INACTIVE;
        }
        else
        {
            ret = APP_PRVLORA_STATUS_ERROR;
        }
    }

    return ret;
}

/*!
 * Get active
 */
bool AppPrvLoRaMainIsActive( void )
{
    bool    status;

    if( appPrvLoRaMainState == APP_PRVLORA_MAINSTATE_ACTIVE )
    {
        status = true;
    }
    else
    {
        status = false;
    }

    return status;
}

//--------------------------------------------------------------------------------------------------
// parameter

/*!
 * Reset parameter
 */
void AppPrvLoRaMainResetParams( void )
{
    AppPrvLoRaSettings_t        *p_appPrvLoraSettings;
    AppPrvLoRaRemoteDevInfo_t   *p_remoteDevInfo;
    AppPrvLoRaTxCycleMng_t      *p_txCycleMng;

    // init
    p_appPrvLoraSettings = &( appPrvLoRaNvmParameters.prvLoraSettings );
    p_remoteDevInfo      = &( appPrvLoRaNvmParameters.remoteDevInfo[ 0 ] );
    p_txCycleMng         = &( appPrvLoRaNvmParameters.txCycleMng );

    // set default parameters
    memcpy( p_appPrvLoraSettings, &appPrvLoraDefaultSettings, sizeof(AppPrvLoRaSettings_t) );
    memset( &( p_remoteDevInfo[ 0 ] ), 0x00, PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM * sizeof(AppPrvLoRaRemoteDevInfo_t) );
    memset( p_txCycleMng, 0x00, sizeof(AppPrvLoRaTxCycleMng_t) );
}

/*!
 * Factory reset parameter
 */
void AppPrvLoRaMainFactoryResetParams( bool isFormatted )
{

    if( isFormatted == false )
    {
        // Initialize data flash area used for appLoraWanSettings and appLoraWanNvmData
        RpMcuEepromFormat();
    }

    AppPrvLoRaMainResetParams();
    AppPrvLoRaMainSaveParams();
}

/*!
 * Load parameter
 */
void AppPrvLoRaMainLoadParams( void )
{
    PrvLoRaStatus_t     status;

    status = AppPrvLoRaNvmLoadParameters();
    if( status != PRVLORA_STATUS_OK )
    {
        AppPrvLoRaMainFactoryResetParams( false );
    }
}

/*!
 * Save parameter
 */
void AppPrvLoRaMainSaveParams( void )
{
    AppPrvLoRaNvmSaveParameters( APP_PRVLORA_NVMDATA_RWFLG_ALL );
}


//--------------------------------------------------------------------------------------------------
// main

/*!
 * Main procedure (called from main loop)
 */
void AppPrvLoRaMainProcess( void )
{
    if( appPrvLoRaMainState == APP_PRVLORA_MAINSTATE_ACTIVE )
    {
        // tx cycle: send data
        AppPrvLoRaTxCycleSendFrame();

        // process event/iterrupt from PrivateLoRa stack/radio
        PrivateLoRaProcess();
    }
}

/*!
 * Low power
 */
uint8_t AppPrvLoRaMainLowPower( void )
{
    uint8_t     ret;

    // init
    ret = APP_PRVLORA_STATUS_ERROR;  // does not call low power function

    if( appPrvLoRaMainState == APP_PRVLORA_MAINSTATE_ACTIVE )
    {
        PrivateLoRaSetLowPower();
        ret = APP_PRVLORA_STATUS_OK;
   }

    return ret;
}
