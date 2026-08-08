/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    main.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/


#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "board.h"

#include "main.h"
#include "privatelora_sample.h"
#include "at_proc.h"


/*--------*/
/* define */
#define APP_INITIAL_LORA_MODE       APP_LORA_MODE_PRIVATELORA

/*-----------------*/
/* global variable */
uint8_t appLoRaMode    = APP_LORA_MODE_NONE;
uint8_t appLoRaModeSet = APP_LORA_MODE_NONE;

/*-----------*/
/* functions */
static void AppInit( void );

static bool AppCallbackBoardIsLowPowerAllowed( void );
static void AppMainLoRaProcess( void );
static void AppMainLowPower( void );


//--------------------------------------------------------------------------------------------------
// init

/*!
 * Initialize
 */
static void AppInit( void )
{
    // Set callback function for BoardIsLowPowerAllowed()
    BoardSetIsLowPowerAllowedCallback( AppCallbackBoardIsLowPowerAllowed );

    // init PrivateLoRa application
    AppPrvLoRaMainInit();

    // start LoRa application
    if( AppSetLoRaMode( APP_INITIAL_LORA_MODE ) == 1 )
    {
        appLoRaMode = appLoRaModeSet;
        AppAtInit();  /* re-init AT command  */
    }
}

//--------------------------------------------------------------------------------------------------
// mode
uint8_t AppSetLoRaMode( uint8_t loraMode )
{
    uint8_t             status;
    uint8_t             prvloraRet;

    // init
    status     = 0;  // init: LoRa mode is not changed
    prvloraRet = APP_PRVLORA_STATUS_ERROR;

    switch( loraMode )
    {
        //---------------------------------
        // Change LoRa mode - Private LoRa
        case APP_LORA_MODE_PRIVATELORA:
            AppPrvLoRaMainStop();  // fail-safe for if PrivateLoRa is started
            prvloraRet = AppPrvLoRaMainStart();
            break;

        //---------------------------------
        // Change LoRa mode - (unknown)
        default:
            break;
    }

    if( prvloraRet == APP_PRVLORA_STATUS_OK )
    {
        appLoRaModeSet = loraMode;
        status         = 1;  // changed LoRa mode
    }

    return status;
}

uint8_t AppGetLoRaMode( void )
{
    return appLoRaMode;
}


//--------------------------------------------------------------------------------------------------
// event from MCU/Board

/*!
 * Ask if it is possible to enter low power mode
 */
static bool AppCallbackBoardIsLowPowerAllowed( void )
{
    bool    ret;
    bool    isActive;

    // init
    ret      = false;
    isActive = false;

    if( isActive == false )
    {
        isActive = AppPrvLoRaMainIsActive();
        if( isActive == true )
        {
            ret = AppPrvLoRaCallbackBoardIsLowPowerAllowed();
        }
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
// main

/*!
 * Main procedure (called from main loop)
 */
static void AppMainLoRaProcess( void )
{
    // process event/iterrupt from PrivateLoRa stack/radio
    AppPrvLoRaMainProcess();
}

/*!
 * Low power
 */
static void AppMainLowPower( void )
{
    uint8_t     funcRet;

    // init
    funcRet = APP_PRVLORA_STATUS_ERROR;

    if( funcRet != APP_PRVLORA_STATUS_OK )
    {
        funcRet = AppPrvLoRaMainLowPower();
    }
}


/*!
 * Main application entry point.
 */
int app_main( void )
{
    // Target board initialization
    BoardInitMcu();

    // Application Initialization (AT command, LoRa, ...)
    AppInit();

    while( 1 )
    {
        if( AtNotifyCommandReceived() )
        {
            AtCmdParse();

            if( appLoRaMode != appLoRaModeSet )
            {
                /* LoRa mode is changed. re-init AT command  */
                appLoRaMode = appLoRaModeSet;
                AppAtInit();
            }
        }

        // process event/iterrupt from LoRa stack/radio
        AppMainLoRaProcess();

        // Check whether MCU can be set to low power mode
        if( BoardIsLowPowerAllowed() )
        {

            AppMainLowPower();
        }
    }
}
