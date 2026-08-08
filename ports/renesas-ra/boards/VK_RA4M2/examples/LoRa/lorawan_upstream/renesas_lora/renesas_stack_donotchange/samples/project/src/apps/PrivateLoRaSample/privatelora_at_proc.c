/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    privatelora_at_proc.c
  * @author  Renesas Electronics Corporation
  * @brief
**/

#include <stdio.h>
#include "board.h"

#ifdef LORACOMBO_ENABLED
#include "lora_sample.h"
#endif

#include "privatelora_sample.h"
#include "at_proc.h"
#include "privatelora_at_cmdtable.h"  // Command table

/*-----------------*/
/* global variable */
uint8_t *p_appPrvLoraAtOctBuff;
int16_t appPrvLoRaAtOctBuffSize;

uint16_t appPrvLoRaAtCmdIndex[ MAXNUM_APP_PRVLORA_ATCMD_INDEX ];

const char *const appAtPrvLoRaStatusMsg[ MAXNUM_PRVLORA_STATUS + 1 ] =
{
    "STATUS_OK",                    // PRVLORA_STATUS_OK
    "MAC_ERROR",                    // PRVLORA_STATUS_ERROR
    "BUSY",                         // PRVLORA_STATUS_BUSY
    "INACTIVE",                     // PRVLORA_STATUS_INACTIVE
    "PARAMETER_INVALID",            // PRVLORA_STATUS_PARAMETER_INVALID
    "REQUSET_INVALID",              // PRVLORA_STATUS_REQUSET_INVALID
    "NO_REMOTE_DEVICE_ENTRY",       // PRVLORA_STATUS_NO_REMOTE_DEVICE_ENTRY
    "NOT_SUPPORTED",                // PRVLORA_STATUS_NOT_SUPPORTED
    "SERVICE_UNKNOWN",              // PRVLORA_STATUS_SERVICE_UNKNOWN
    "IB_ATTRIBUTE_INVALID",         // PRVLORA_STATUS_IB_ATTRIBUTE_INVALID
    "LENGTH_ERROR",                 // PRVLORA_STATUS_LENGTH_ERROR
    "COMMAND_ERROR",                // PRVLORA_STATUS_COMMAND_ERROR
    "INSUFFICIENT_MEMORY",          // PRVLORA_STATUS_INSUFFICIENT_MEMORY
    "DATARATE_INVALID",             // PRVLORA_STATUS_DATARATE_INVALID
    "CHANNEL_INVALID",              // PRVLORA_STATUS_CHANNEL_INVALID
    "RADIO_ERROR",                  // PRVLORA_STATUS_RADIO_ERROR
    "RADIO_CHANNEL_BUSY",           // PRVLORA_STATUS_RADIO_CHANNEL_BUSY
    "RADIO_DUTYCYCLE_RESTRICTED",   // PRVLORA_STATUS_RADIO_DUTYCYCLE_RESTRICTED
    "RADIO_PARAMETER_INVALID",      // PRVLORA_STATUS_RADIO_PARAMETER_INVALID
    "UNKNOWN_DEVICE",               // PRVLORA_STATUS_UNKNOWN_DEVICE
    "CRYPTO_ERROR",                 // PRVLORA_STATUS_CRYPTO_ERROR
    "UNKNOWN_STATUS",               // (unknown)
};

const char *const appAtPrvLoRaEventInfoStatusMsg[ MAXNUM_PRVLORA_EVENTINFO_STATUS + 1 ] =
{
    "STATUS_OK",                    // PRVLORA_EVENTINFO_STATUS_OK,
    "ERROR",                        // PRVLORA_EVENTINFO_STATUS_ERROR,
    "TX_TIMEOUT",                   // PRVLORA_EVENTINFO_STATUS_TX_TIMEOUT,
    "TX_NOACK",                     // PRVLORA_EVENTINFO_STATUS_TX_NOACK,
    "TX_CANCELED",                  // PRVLORA_EVENTINFO_STATUS_TX_CANCELED,
    "TX_CHANNELBUSY",               // PRVLORA_EVENTINFO_STATUS_TX_CHANNELBUSY,
    "TX_DUTYCYCLE_RESTRICTED",      // PRVLORA_EVENTINFO_STATUS_TX_DUTYCYCLE_RESTRICTED,
    "TX_RADIO_ERROR",               // PRVLORA_EVENTINFO_STATUS_TX_RADIO_ERROR,
    "KEYREQ_FAILED",                // PRVLORA_EVENTINFO_STATUS_KEYREQ_FAILED,
    "UNKONWN_EVENT_INFO_STATUS",    // (unknown)
};

/*--------------------*/
/* function prototype */
extern uint16_t AtCmdGetCurrentCmd(void);

#if defined(LORACOMBO_ENABLED)
// tools prototype
extern int8_t *AppAtGetString( int8_t *str, int16_t *len );
extern void AppAtHexArray2HexStr( uint8_t *hexStr, uint8_t *hexArray, uint16_t len );
extern AtResultCode_t AppAtHexStr2HexDataArrayWithPadding( uint8_t *hexDataArray,
                                                           uint16_t hexDataArrayLen,
                                                           int8_t *hexStr,
                                                           uint16_t hexStrLen );
extern AtResultCode_t AppAtHexStr2HexDataArray( uint8_t *hexDataArray, int8_t *hexStr, uint16_t hexStrLen );
extern int32_t AppAtStr2Dec( int8_t *str, uint8_t len );
// output resultcode
extern void AppAtOutputResultCode( char *result );
extern void AppAtOutputResultDec( long DecNum, unsigned char Len, unsigned char SupCh );
#endif

static int32_t AppAtPrvLoRaStr2Dec( int8_t *p_str, uint8_t len );

//--------------------------------------------------------------------------------------------------
// Init

void AppAtPrvLoRaAtInit( uint8_t *p_appAtOctBuff, int16_t appAtOctBuffSize )
{
    p_appPrvLoraAtOctBuff   = p_appAtOctBuff;
    appPrvLoRaAtOctBuffSize = appAtOctBuffSize;

#if (AT_EXTCMD_TAB_ARRAYSIZE == 0)
    /* command buffer and at init   */
    AtInit( p_appPrvLoraAtOctBuff,
            appPrvLoRaAtOctBuffSize,
            (AtExtendTab_t *)AppAtPrvLoRaCommands,
            sizeof(AppAtPrvLoRaCommands) / sizeof(AtExtendTab_t) );
#else
    /* command buffer and at init   */
    AtInit( p_appPrvLoraAtOctBuff, appPrvLoRaAtOctBuffSize );

    /* register extended commands (PrivateLoRa-only commands) */
    AtExtendCmdRegist( "+RESET",    AppAtPrvLoRaResetAct,        NULL,                         NULL                   );
    AtExtendCmdRegist( "+VER",      NULL,                        AppAtPrvLoRaVerRead,          NULL                   );
    AtExtendCmdRegist( "+SAVE",     NULL,                        NULL,                         AppAtPrvLoRaVerSaveAct );
    AtExtendCmdRegist( "+LOAD",     AppAtPrvLoRaVerLoadAct,      NULL,                         AppAtPrvLoRaVerLoadAct );
    AtExtendCmdRegist( "+REGION",   AppAtPrvLoRaRegionSet,       AppAtPrvLoRaRegionRead,       NULL                   );
    AtExtendCmdRegist( "+DEVEUI",   AppAtPrvLoRaMacAddrSet,      AppAtPrvLoRaMacAddrRead,      NULL                   );
    AtExtendCmdRegist( "+CHID",     AppAtPrvLoRaChannelIDSet,    AppAtPrvLoRaChannelIDRead,    NULL                   );
    AtExtendCmdRegist( "+DR",       AppAtPrvLoRaDRSet,           AppAtPrvLoRaDRRead,           NULL                   );
    AtExtendCmdRegist( "+TXPOWER",  AppAtPrvLoRaTxPowerSet,      AppAtPrvLoRaTxPowerRead,      NULL                   );
    AtExtendCmdRegist( "+RXON",     AppAtPrvLoRaRxOnWhenIdleSet, AppAtPrvLoRaRxOnWhenIdleRead, NULL                   );
    AtExtendCmdRegist( "+RMTDEV",   AppAtPrvLoRaRemoveDevSet,    NULL,                         NULL                   );
    AtExtendCmdRegist( "+KEYREQ",   AppAtPrvLoRaKeyReqAct,       NULL,                         NULL                   );
    AtExtendCmdRegist( "+KEYRES",   AppAtPrvLoRaKeyResSet,       AppAtPrvLoRaKeyResRead,       NULL                   );
    AtExtendCmdRegist( "+TXOPT",    AppAtPrvLoRaTxOptionsSet,    AppAtPrvLoRaTxOptionsRead,    NULL                   );
    AtExtendCmdRegist( "+SEND",     AppAtPrvLoRaSendAct,         NULL,                         NULL                   );
    AtExtendCmdRegist( "+SENDHEX",  AppAtPrvLoRaSendHexAct,      NULL,                         NULL                   );
    AtExtendCmdRegist( "+DEVINFO",  AppAtPrvLoRaDevInfoAct,      NULL,                         NULL                   );
    AtExtendCmdRegist( "+TXCYCLE",  AppAtPrvLoRaTxCycleAct,      NULL,                         NULL                   );
    AtExtendCmdRegist( "+RSSI",     AppAtPrvLoRaRssiSet,         AppAtPrvLoRaRssiRead,         NULL                   );
#if defined(DEBUG_PRVLORA)
    AtExtendCmdRegist( "+DEBUG",    AppAtPrvLoRaDebugSet,        AppAtPrvLoRaDebugRead,        NULL                   );
#endif
#if defined(LORACOMBO_ENABLED)
    AtExtendCmdRegist( "+LORAMODE", AppAtLoRaModeSet,            AppAtLoRaModeRead,            NULL                   );
#endif

#endif
}

//--------------------------------------------------------------------------------------------------
// Output
void AppAtPrvLoRaStatusResult( PrvLoRaStatus_t status )
{
    const char  *p_statusMsg;

    // this function is called when status in not PRVLORA_STATUS_OK
    if( status != PRVLORA_STATUS_OK )
    {
        if( status < MAXNUM_PRVLORA_STATUS )
        {
            p_statusMsg = appAtPrvLoRaStatusMsg[ status ];
        }
        else
        {
            p_statusMsg = appAtPrvLoRaStatusMsg[ MAXNUM_PRVLORA_STATUS ];
        }

        AppAtOutputResultCode( (char *)p_statusMsg );
    }
}

void AppAtPrvLoRaEventResult( PrvLoRaEventInfoStatus_t status )
{
    const char  *p_statusMsg;

    // this function is called when status in not PRVLORA_EVENTINFO_STATUS_OK
    if( status != PRVLORA_EVENTINFO_STATUS_OK )
    {
        if( status < MAXNUM_PRVLORA_EVENTINFO_STATUS )
        {
            p_statusMsg = appAtPrvLoRaEventInfoStatusMsg[ status ];
        }
        else
        {
            p_statusMsg = appAtPrvLoRaEventInfoStatusMsg[ MAXNUM_PRVLORA_EVENTINFO_STATUS ];
        }

        AppAtOutputResultCode( (char *)p_statusMsg );
    }
}

//--------------------------------------------------------------------------------------------------
// Tools for input
/*!
 * @fn
 * convert numeric string to number
 * @param str   numeric string
 * @param len   length of the numeric string
 * @return  decimal number
 */
static int32_t AppAtPrvLoRaStr2Dec( int8_t *p_str, uint8_t len )
{
    int32_t     ret, valsigned;
    uint8_t     i, j;

    ret       = 0;
    valsigned = 1;

    for( i = 0; i < len; i++ )
    {
        j = p_str[ i ];

        if( ( i == 0 ) && ( j == '-' ) )
        {
            valsigned = -1;
        }
        else
        {
            if( ( j >= '0' ) && ( j <= '9' ) )
            {
                ret = ret * 10;
                ret = ret + ( j - '0' );
            }
            else {
                break;
            }
        }
    }
    ret = ret * valsigned;

    return ret;
}

//--------------------------------------------------------------------------------------------------
// AT commands

//--------------------
// AT+RESET
//--------------------
/*!
 * AT+RESET; Execute reset
 */
AtResultCode_t AppAtPrvLoRaResetAct( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    PrvLoRaRegion_t     region;

    // init
    ret = AT_RC_ERR;

    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        if( argvLen == 1 )
        {
            switch( argv[0] )
            {
                case '0':   // Reset PrivateLoRaMac only
                    AppPrvLoRaGetRegion( &region );
                    AppPrvLoRaSetRegion( region );
                    ret = AT_RC_OK;
                    break;

                case '7':   // S/W reset with data flash initialization
#ifdef LORACOMBO_ENABLED
                    AppFactoryResetParams();
                    AppPrvLoRaMainFactoryResetParams( true );  // must be true
#else
                    AppPrvLoRaMainFactoryResetParams( false );  // must be false
#endif
                    /* no break */

                case '1':   // S/W reset
                    print( "OK" );
                    AtPrintTrailer();
                    AppPrvLoRaMcuReset();   // S/W reset
                    break;

                default:
                    break;
            }
        }
    }

    return ret;
}

//--------------------
// AT+VER
//--------------------
/*!
 * AT+VER; Read version
 */
AtResultCode_t AppAtPrvLoRaVerRead( void *p )
{
    AtPrintHeader();
    print( " PrivateLoRa Sample App Ver." );
    print_hex( appPrvLoRaFwVersion.Fields.Major, 2 );
    print( "." );
    print_hex( appPrvLoRaFwVersion.Fields.Minor, 2 );
    AtPrintTrailer();

    return AT_RC_OK;
}

//--------------------
// AT+SAVE
//--------------------
/*!
 * AT+SAVE; Save parameters
 */
AtResultCode_t AppAtPrvLoRaVerSaveAct( void *p )
{
    AppPrvLoRaNvmSaveParameters( APP_PRVLORA_NVMDATA_RWFLG_PRVLORASETTINGS );
    return AT_RC_OK;
}

//--------------------
// AT+LOAD
//--------------------
/*!
 * AT+LOAD; Load parameters
 */
AtResultCode_t AppAtPrvLoRaVerLoadAct( void *p )
{
    AtResultCode_t          ret;     /* result code */
    uint8_t                 argc;    /* number of argument */
    int8_t                  *argv;   /* argument */
    int16_t                 argvLen; /* length of an argument */
    PrvLoRaStatus_t         status, funcRet;
    uint8_t                 loadMode;  // 0 = load from NVM / 1 = set default
    AppPrvLoRaSettings_t    *p_appPrvLoraSettings;

    // init
    ret                  = AT_RC_ERR;
    status               = PRVLORA_STATUS_PARAMETER_INVALID;
    loadMode             = 0xFF;  // (init) invalid
    p_appPrvLoraSettings = &( appPrvLoRaNvmParameters.prvLoraSettings );

    argc = AtParseListLen();
    if( argc == 0 )
    {
        loadMode = 0;  // load parameters from NVM
    }
    else if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        loadMode = AppAtStr2Dec( argv, argvLen );
    }
    else
    {
        // nothing to do
    }

    switch( loadMode )
    {
        // load parameters from NVM
        case 0:
            funcRet = AppPrvLoRaNvmLoadParameters();
            if( funcRet != PRVLORA_STATUS_OK )
            {
                // parameters in NVM may be corrupted. factory reset NVM.
#ifdef LORACOMBO_ENABLED
                AppFactoryResetParams();
                AppPrvLoRaMainFactoryResetParams( true );  // must be true
#else
                AppPrvLoRaMainFactoryResetParams( false );  // must be false
#endif
            }

            status = PRVLORA_STATUS_OK;
            break;

        case 1:
            // set default parameters
            memcpy( p_appPrvLoraSettings, &appPrvLoraDefaultSettings, sizeof(AppPrvLoRaSettings_t) );
            status = PRVLORA_STATUS_OK;
            break;

        default:
            break;
    }

    if( status == PRVLORA_STATUS_OK )
    {
        // init PrivateLoRa
        status = AppPrvLoRaInit();
        if( status == PRVLORA_STATUS_OK )
        {
            // set LoRa mode
            AppSetLoRaMode( APP_LORA_MODE_PRIVATELORA );
        }
    }

    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+REGION
//--------------------
/*!
 * AT+REGION; Set region
 */
AtResultCode_t AppAtPrvLoRaRegionSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    PrvLoRaStatus_t     status;
    uint8_t             inRegion;
    PrvLoRaRegion_t     region;

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        status = PRVLORA_STATUS_OK;  // re-init

        AtParsePopOne( &argv, &argvLen );
        inRegion = AppAtStr2Dec( argv, argvLen );
        switch( inRegion )
        {
            case 0:
                region = PRVLORA_REGION_EU;
                break;
            case 1:
                region = PRVLORA_REGION_US;
                break;
            case 4:
                region = PRVLORA_REGION_AU;
                break;
            case 6:
                region = PRVLORA_REGION_AS1;
                break;
#if 0  // (reserved)
            case 21:
                region = PRVLORA_REGION_AS1;
                break;
#endif
            case 22:
                region = PRVLORA_REGION_AS2;
                break;
            case 23:
                region = PRVLORA_REGION_AS3;
                break;
            case 24:
                region = PRVLORA_REGION_AS4;
                break;
            case 30:
                region = PRVLORA_REGION_JP;
                break;
            case 31:
                region = PRVLORA_REGION_JP_LDC;
                break;
            case 7:
                region = PRVLORA_REGION_KR;
                break;
            case 8:
                region = PRVLORA_REGION_IN;
                break;
            default:
                status = PRVLORA_STATUS_PARAMETER_INVALID;
                break;
        }
    }

    // Set region
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSetRegion( region );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

/*!
 * AT+REGION; Read region
 */
AtResultCode_t AppAtPrvLoRaRegionRead( void *p )
{
    AtResultCode_t      ret;     /* result code */
    PrvLoRaRegion_t     region;
    PrvLoRaStatus_t     status;

    // init
    ret = AT_RC_ERR;

    status = AppPrvLoRaGetRegion( &region );
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;  // re-init

        switch( region )
        {
#if defined(RADIO_CFG_EU_ENABLED)
            case PRVLORA_REGION_EU:
                AppAtOutputResultCode( "0:EU" );
                break;
#endif
#if defined(RADIO_CFG_US_ENABLED)
            case PRVLORA_REGION_US:
                AppAtOutputResultCode( "1:US" );
                break;
#endif
#if defined(RADIO_CFG_AU_ENABLED)
            case PRVLORA_REGION_AU:
                AppAtOutputResultCode( "4:AU" );
                break;
#endif
#if defined(RADIO_CFG_AS_ENABLED)
            case PRVLORA_REGION_AS1:
                AppAtOutputResultCode( "6:AS1" );
                break;

            case PRVLORA_REGION_AS2:
                AppAtOutputResultCode( "22:AS2" );
                break;

            case PRVLORA_REGION_AS3:
                AppAtOutputResultCode( "23:AS3" );
                break;

            case PRVLORA_REGION_AS4:
                AppAtOutputResultCode( "24:AS4" );
                break;

            case PRVLORA_REGION_JP:
                AppAtOutputResultCode( "30:JP" );
                break;

            case PRVLORA_REGION_JP_LDC:
                AppAtOutputResultCode( "31:JP_LDC" );
                break;
#endif
#if defined(RADIO_CFG_KR_ENABLED)
            case PRVLORA_REGION_KR:
                AppAtOutputResultCode( "7:KR" );
                break;
#endif
#if defined(RADIO_CFG_IN_ENABLED)
            case PRVLORA_REGION_IN:
                AppAtOutputResultCode( "8:IN" );
                break;
#endif
            default:
                AppAtOutputResultCode( "??:(unknown region)" );
                ret = AT_RC_ERR;
                break;
        }
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+DEVEUI
//--------------------
/*!
 * AT+DEVEUI; Set MAC address
 */
AtResultCode_t AppAtPrvLoRaMacAddrSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    AtResultCode_t      funcRet;
    PrvLoRaStatus_t     status;
    uint8_t             macAddr[ APP_PRVLORA_LEN_MACADDR ];

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        funcRet = AppAtHexStr2HexDataArrayWithPadding( macAddr, APP_PRVLORA_LEN_MACADDR, argv, argvLen );
        if( funcRet == AT_RC_OK )
        {
            status = PRVLORA_STATUS_OK;
        }
    }

    // set Mac address
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSetMacAddr( macAddr );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

/*!
 * AT+DEVEUI; Read MAC address
 */
AtResultCode_t AppAtPrvLoRaMacAddrRead( void *p )
{
    uint8_t macAddr[ APP_PRVLORA_LEN_MACADDR ];
    uint8_t macAddrStr[ APP_PRVLORA_LEN_MACADDR * 2 + 1 ];

    AppPrvLoRaGetMacAddr( macAddr );
    AppAtHexArray2HexStr( macAddrStr, macAddr, APP_PRVLORA_LEN_MACADDR );

    AppAtOutputResultCode( (char *)macAddrStr );

    return AT_RC_OK;
}

//--------------------
// AT+CHID
//--------------------
/*!
 * AT+CHID; Set Channel ID
 */
AtResultCode_t AppAtPrvLoRaChannelIDSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    PrvLoRaStatus_t     status;
    uint8_t             channelId;

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        if( argvLen <= 2 )
        {
            channelId = AppAtStr2Dec( argv, argvLen );
            status    = PRVLORA_STATUS_OK;
        }
    }

    // Set channel ID
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSetChannelId( channelId );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

/*!
 * AT+CHID; Read Channel ID
 */
AtResultCode_t AppAtPrvLoRaChannelIDRead( void *p )
{
    AtResultCode_t      ret;     /* result code */
    PrvLoRaStatus_t     status;
    uint8_t             channelId;

    // init
    ret = AT_RC_ERR;

    status = AppPrvLoRaGetChannelId( &channelId );
    if( status == PRVLORA_STATUS_OK )
    {
        AppAtOutputResultDec( (long)channelId, 2, '\0' );
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+DR
//--------------------
/*!
 * AT+DR; Set DR index
 */
AtResultCode_t AppAtPrvLoRaDRSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    PrvLoRaStatus_t     status;
    uint8_t             drIndex;

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        if( argvLen <= 2 )
        {
            drIndex = AppAtStr2Dec( argv, argvLen );
            status  = PRVLORA_STATUS_OK;
        }
    }

    // Set DR
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSetDR( drIndex );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

/*!
 * AT+DR; Read DR index
 */
AtResultCode_t AppAtPrvLoRaDRRead( void *p )
{
    AtResultCode_t      ret;     /* result code */
    PrvLoRaStatus_t     status;
    uint8_t             drIndex;

    // init
    ret = AT_RC_ERR;

    status = AppPrvLoRaGetDR( &drIndex );
    if( status == PRVLORA_STATUS_OK )
    {
        AppAtOutputResultDec( (long)drIndex, 2, '\0' );
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+TXPOWER
//--------------------
/*!
 * AT+TXPOWER; Set TX power
 */
AtResultCode_t AppAtPrvLoRaTxPowerSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    PrvLoRaStatus_t     status;
    int16_t             txPower;

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        if( argvLen <= 4 )
        {
            txPower = AppAtPrvLoRaStr2Dec( argv, argvLen );
            if( ( -128 <= txPower ) && ( txPower <= 127 ) )
            {
                status = PRVLORA_STATUS_OK;
            }
        }
    }

    // Set tx power
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSetTxPower( (int8_t)txPower );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

/*!
 * AT+TXPOWER; Read TX power
 */
AtResultCode_t AppAtPrvLoRaTxPowerRead( void *p )
{
    AtResultCode_t      ret;     /* result code */
    PrvLoRaStatus_t     status;
    int8_t              txPower;

    // init
    ret = AT_RC_ERR;

    status = AppPrvLoRaGetTxPower( &txPower );
    if( status == PRVLORA_STATUS_OK )
    {
        AppAtOutputResultDec( (long)txPower, 4, '\0' );
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+RXON
//--------------------
/*!
 * AT+RXON; Set enable/disable of RxOnWhenIdle
 */
AtResultCode_t AppAtPrvLoRaRxOnWhenIdleSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    PrvLoRaStatus_t     status;
    bool                rxOnWhenIdle;

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        if( argvLen == 1 )
        {
            switch( argv[0] )
            {
                case '0':
                    rxOnWhenIdle = false;
                    status       = PRVLORA_STATUS_OK;
                    break;

                case '1':
                    rxOnWhenIdle = true;
                    status       = PRVLORA_STATUS_OK;
                    break;

                default:
                    break;
            }
        }
    }

    // Set enable/disable of RxOnWhenIdle
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSetRxOnWhenIdle( rxOnWhenIdle );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

/*!
 * AT+RXON; Read enable/disable of RxOnWhenIdle
 */
AtResultCode_t AppAtPrvLoRaRxOnWhenIdleRead( void *p )
{
    AtResultCode_t      ret;     /* result code */
    PrvLoRaStatus_t     status;
    bool                rxOnWhenIdle;

    // init
    ret = AT_RC_ERR;

    status = AppPrvLoRaGetRxOnWhenIdle( &rxOnWhenIdle );
    if( status == PRVLORA_STATUS_OK )
    {
        if( rxOnWhenIdle == false )
        {
            AppAtOutputResultCode( "0:RXONWHENIDLE_DISABLED" );
        }
        else // if ( rxOnWhenIdle == true )
        {
            AppAtOutputResultCode( "1:RXONWHENIDLE_ENABLED" );
        }

        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+RMTDEV
//--------------------
/*!
 * AT+RMTDEV; Set or clear remote device info
 */
AtResultCode_t AppAtPrvLoRaRemoveDevSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    AtResultCode_t      funcRet;
    PrvLoRaStatus_t     status;
    uint8_t             macAddr[ APP_PRVLORA_LEN_MACADDR ], *p_macAddr;
    uint8_t             psk[ APP_PRVLORA_LEN_SECKEY ];
    uint8_t             i;

    // init
    ret       = AT_RC_ERR;
    status    = PRVLORA_STATUS_PARAMETER_INVALID;
    p_macAddr = NULL;

    // Parse command
    argc = AtParseListLen();
    if( ( argc == 1 ) || ( argc == 2 ) )
    {
        // get MAC address
        AtParsePopOne( &argv, &argvLen );
        funcRet = AppAtHexStr2HexDataArrayWithPadding( macAddr, APP_PRVLORA_LEN_MACADDR, argv, argvLen );
        if( funcRet == AT_RC_OK )
        {
            // check whether macAddr is all 0xFF
            //  - treat macAddr = NULL if macAddr is all 0xFF.
            for( i = 0; i < APP_PRVLORA_LEN_MACADDR; i++ )
            {
                if( macAddr[ i ] != 0xFF )
                {
                    p_macAddr = macAddr;
                    break;  // exit from for(i) loop
                }
            }
        }

        // get PSK (if exist)
        if( argc == 2 )
        {
            if( funcRet == AT_RC_OK )
            {
                AtParsePopOne( &argv, &argvLen );
                funcRet = AppAtHexStr2HexDataArrayWithPadding( psk, APP_PRVLORA_LEN_SECKEY, argv, argvLen );
            }
        }

        if( funcRet == AT_RC_OK )
        {
            status = PRVLORA_STATUS_OK;
        }
    }

    // Set or clear remote device information
    if( status == PRVLORA_STATUS_OK )
    {
        switch( argc )
        {
            // set remote device information
            case 2:
                status = AppPrvLoRaSetRemoteDeviceInfo( p_macAddr, psk );
                break;

            // clear remote device information
            case 1:
                status = AppPrvLoRaClearRemoteDeviceInfo( p_macAddr );
                break;

            default:
                break;  // (never comes here)
        }
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+KEYREQ
//--------------------
/*!
 * AT+KEYREQ; Key request
 */
AtResultCode_t AppAtPrvLoRaKeyReqAct( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    AtResultCode_t      funcRet;
    PrvLoRaStatus_t     status;
    uint8_t             dstMacAddr[ APP_PRVLORA_LEN_MACADDR ];

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        // Destination MAC address
        AtParsePopOne( &argv, &argvLen );
        funcRet = AppAtHexStr2HexDataArrayWithPadding( dstMacAddr, APP_PRVLORA_LEN_MACADDR, argv, argvLen );
        if( funcRet == AT_RC_OK )
        {
            status = PRVLORA_STATUS_OK;
        }
    }

    // Key request
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaKeyRequest( dstMacAddr );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        appPrvLoRaAtCmdIndex[ APP_PRVLORA_ATCMD_INDEX_KEYREQ ] = AtCmdGetCurrentCmd();

        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+KEYRES
//--------------------
/*!
 * AT+KEYRES; Set accept of key request
 */
AtResultCode_t AppAtPrvLoRaKeyResSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    PrvLoRaStatus_t     status;
    bool                permitKeyReq;

    // init
    ret          = AT_RC_ERR;
    status       = PRVLORA_STATUS_PARAMETER_INVALID;
    permitKeyReq = false;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        if( argvLen == 1 )
        {
            switch( argv[0] )
            {
                case '0':
                    permitKeyReq = false;
                    status       = PRVLORA_STATUS_OK;
                    break;

                case '1':
                    permitKeyReq = true;
                    status       = PRVLORA_STATUS_OK;
                    break;

                default:
                    break;
            }
        }
    }

    // Set accept of key request
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSetKeyReqPermit( permitKeyReq );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

/*!
 * AT+KEYRES; Read accept of key request
 */
AtResultCode_t AppAtPrvLoRaKeyResRead( void *p )
{
    AtResultCode_t      ret;     /* result code */
    PrvLoRaStatus_t     status;
    bool                permitKeyReq;

    // init
    ret = AT_RC_ERR;

    status = AppPrvLoRaGetKeyReqPermit( &permitKeyReq );
    if( status == PRVLORA_STATUS_OK )
    {
        if( permitKeyReq == false )
        {
            AppAtOutputResultCode( "0:KEYREQPERMIT_DISABLED" );
        }
        else // if ( permitKeyReq == true )
        {
            AppAtOutputResultCode( "1:KEYREQPERMIT_ENABLED" );
        }

        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}


//--------------------
// AT+TXOPT
//--------------------
/*!
 * AT+TXOPT; Set txoption
 */
AtResultCode_t AppAtPrvLoRaTxOptionsSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    AtResultCode_t      funcRet;
    PrvLoRaStatus_t     status;
    PrvLoRaTxOptions_t  txOptions;

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        funcRet = AppAtHexStr2HexDataArray( &( txOptions.txOptValue ), argv, 2 );
        if( funcRet == AT_RC_OK )
        {
            status = PRVLORA_STATUS_OK;
        }
    }

    // Set txoption
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSetTxOptions( txOptions );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

/*!
 * AT+TXOPT; Read txoption
 */
AtResultCode_t AppAtPrvLoRaTxOptionsRead( void *p )
{
    AtResultCode_t      ret;     /* result code */
    PrvLoRaStatus_t     status;
    PrvLoRaTxOptions_t  txOptions;
    uint8_t             hexStr[ 2 + 1 ];  // +1 = end of string

    // init
    ret = AT_RC_ERR;
    memset1( hexStr, 0x00, sizeof(hexStr) );

    status = AppPrvLoRaGetTxOptions( &txOptions );
    if( status == PRVLORA_STATUS_OK )
    {
        AppAtHexArray2HexStr( hexStr, &( txOptions.txOptValue ), 1 );
        AppAtOutputResultCode( (char *)hexStr );

        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+SEND
//--------------------
/*!
 * AT+SEND; Send data
 */
AtResultCode_t AppAtPrvLoRaSendAct( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    AtResultCode_t      funcRet;
    PrvLoRaStatus_t     status;
    uint8_t             *p_data;
    uint8_t             dataLen;
    uint8_t             dstMacAddr[ APP_PRVLORA_LEN_MACADDR ];

    // init
    ret     = AT_RC_ERR;
    status  = PRVLORA_STATUS_PARAMETER_INVALID;
    dataLen = 0;
    p_data  = NULL;

    // Parse command
    argc = AtParseListLen();
    if( ( argc == 1 ) || ( argc == 2 ) )
    {
        // Destination MAC address
        AtParsePopOne( &argv, &argvLen );
        funcRet = AppAtHexStr2HexDataArrayWithPadding( dstMacAddr, APP_PRVLORA_LEN_MACADDR, argv, argvLen );
        if( funcRet == AT_RC_OK )
        {
            status = PRVLORA_STATUS_OK;
        }

        // Data
        if( status == PRVLORA_STATUS_OK )
        {
            if( argc == 2 )
            {
                AtParsePopOne( &argv, &argvLen );
                p_data = (uint8_t *)AppAtGetString( argv, &argvLen );

                if( p_data != NULL )
                {
                    if( argvLen < 0x0100 )
                    {
                        dataLen = (uint8_t)argvLen;
                    }
                    else
                    {
                        status = PRVLORA_STATUS_PARAMETER_INVALID;
                    }
                }
                else
                {
                    status = PRVLORA_STATUS_PARAMETER_INVALID;
                }
            }
        }
    }

    // Send data
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSendData( dstMacAddr, p_data, dataLen, APP_PRVLORA_MCPSHANDLE_ATSEND );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        appPrvLoRaAtCmdIndex[ APP_PRVLORA_ATCMD_INDEX_SEND ] = AtCmdGetCurrentCmd();

        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+SENDHEX
//--------------------
/*!
 * AT+SENDHEX; Send data
 */
AtResultCode_t AppAtPrvLoRaSendHexAct( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    AtResultCode_t      funcRet;
    PrvLoRaStatus_t     status;
    uint8_t             *p_data;
    uint8_t             dataLen;
    uint8_t             dstMacAddr[ APP_PRVLORA_LEN_MACADDR ];

    // init
    ret     = AT_RC_ERR;
    status  = PRVLORA_STATUS_PARAMETER_INVALID;
    dataLen = 0;
    p_data  = NULL;

    // Parse command
    argc = AtParseListLen();
    if( ( argc == 1 ) || ( argc == 2 ) )
    {
        // Destination MAC address
        AtParsePopOne( &argv, &argvLen );
        funcRet = AppAtHexStr2HexDataArrayWithPadding( dstMacAddr, APP_PRVLORA_LEN_MACADDR, argv, argvLen );
        if( funcRet == AT_RC_OK )
        {
            status = PRVLORA_STATUS_OK;
        }

        // Data
        if( status == PRVLORA_STATUS_OK )
        {
            if( argc == 2 )
            {
                AtParsePopOne( &argv, &argvLen );
                funcRet = AppAtHexStr2HexDataArray( p_appPrvLoraAtOctBuff, argv, argvLen );
                if( funcRet == AT_RC_OK )
                {
                    p_data  = p_appPrvLoraAtOctBuff;
                    dataLen = (uint8_t)( argvLen / 2 );
                }
                else
                {
                    status = PRVLORA_STATUS_PARAMETER_INVALID;
                }
            }
        }
    }

    // Send data
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSendData( dstMacAddr, p_data, dataLen, APP_PRVLORA_MCPSHANDLE_ATSENDHEX );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        appPrvLoRaAtCmdIndex[ APP_PRVLORA_ATCMD_INDEX_SENDHEX ] = AtCmdGetCurrentCmd();
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+DEVINFO
//--------------------
/*!
 * AT+DEVINFO; Send DevInfoReq
 */
AtResultCode_t AppAtPrvLoRaDevInfoAct( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    AtResultCode_t      funcRet;
    PrvLoRaStatus_t     status;
    uint8_t             dstMacAddr[ APP_PRVLORA_LEN_MACADDR ];

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        // Destination MAC address
        AtParsePopOne( &argv, &argvLen );
        funcRet = AppAtHexStr2HexDataArrayWithPadding( dstMacAddr, APP_PRVLORA_LEN_MACADDR, argv, argvLen );
        if( funcRet == AT_RC_OK )
        {
            status = PRVLORA_STATUS_OK;
        }
    }

    // Send DevInfoReq
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaDevInfoReq( dstMacAddr );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        appPrvLoRaAtCmdIndex[ APP_PRVLORA_ATCMD_INDEX_DEVINFO ] = AtCmdGetCurrentCmd();
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+TXCYCLE
//--------------------
/*!
 * AT+TXCYCLE; Send TxCycleReq
 */
AtResultCode_t AppAtPrvLoRaTxCycleAct( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    AtResultCode_t      funcRet;
    PrvLoRaStatus_t     status;
    uint8_t             dstMacAddr[ APP_PRVLORA_LEN_MACADDR ];
    uint32_t            txCycleTime;

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 2 )
    {
        // Destination MAC address
        AtParsePopOne( &argv, &argvLen );
        funcRet = AppAtHexStr2HexDataArrayWithPadding( dstMacAddr, APP_PRVLORA_LEN_MACADDR, argv, argvLen );
        if( funcRet == AT_RC_OK )
        {
            status = PRVLORA_STATUS_OK;
        }

        // TxCycleTime
        if( status == PRVLORA_STATUS_OK )
        {
            AtParsePopOne( &argv, &argvLen );
            txCycleTime = AppAtStr2Dec( argv, argvLen );
        }
    }

    // Send TxCycleReq
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaTxCycleReq( dstMacAddr, txCycleTime );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        appPrvLoRaAtCmdIndex[ APP_PRVLORA_ATCMD_INDEX_TXCYCLE ] = AtCmdGetCurrentCmd();

        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

//--------------------
// AT+RSSI
//--------------------
/*!
 * AT+RSSI; Set display RSSI
 */
AtResultCode_t AppAtPrvLoRaRssiSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    PrvLoRaStatus_t     status;
    bool                dispRssi;

    // init
    ret    = AT_RC_ERR;
    status = PRVLORA_STATUS_PARAMETER_INVALID;

    // Parse command
    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        if( argvLen == 1 )
        {
            switch( argv[0] )
            {
                case '0':
                    dispRssi = false;
                    status   = PRVLORA_STATUS_OK;
                    break;

                case '1':
                    dispRssi = true;
                    status   = PRVLORA_STATUS_OK;
                    break;

                default:
                    break;
            }
        }
    }

    // Set display RSSI
    if( status == PRVLORA_STATUS_OK )
    {
        status = AppPrvLoRaSetRssi( dispRssi );
    }

    // result
    if( status == PRVLORA_STATUS_OK )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

/*!
 * AT+RSSI; Get display RSSI
 */
AtResultCode_t AppAtPrvLoRaRssiRead( void *p )
{
    AtResultCode_t      ret;     /* result code */
    PrvLoRaStatus_t     status;
    bool                dispRssi;

    // init
    ret = AT_RC_ERR;

    status = AppPrvLoRaGetRssi( &dispRssi );
    if( status == PRVLORA_STATUS_OK )
    {
        if( dispRssi == false )
        {
            AppAtOutputResultCode( "0:RSSI_DISABLED" );
        }
        else // if ( dispRssi == true )
        {
            AppAtOutputResultCode( "1:RSSI_ENABLED" );
        }

        ret = AT_RC_OK;
    }
    else
    {
        AppAtPrvLoRaStatusResult( status );
    }

    return ret;
}

#if defined(DEBUG_PRVLORA)
//--------------------
// AT+DEBUG
//--------------------
/*!
 * AT+DEBUG; Set debug mode
 */
AtResultCode_t AppAtPrvLoRaDebugSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    uint32_t            debugMode;
    uint8_t             tmpDebugMode[ 4 ];  // 4 = sizeof(uint32_t)

    // init
    ret       = AT_RC_ERR;
    debugMode = 0;

    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        ret = AppAtHexStr2HexDataArrayWithPadding( tmpDebugMode, sizeof(uint32_t), argv, argvLen );
        if (ret == AT_RC_OK)
        {
            debugMode  = (uint32_t)tmpDebugMode[ 0 ] << 24;
            debugMode |= (uint32_t)tmpDebugMode[ 1 ] << 16;
            debugMode |= (uint32_t)tmpDebugMode[ 2 ] <<  8;
            debugMode |= (uint32_t)tmpDebugMode[ 3 ];

            AppPrvLoRaDebugSetMode( debugMode );
        }
    }

    return ret;
}

/*!
 * AT+DEBUG; Read debug mode
 */
AtResultCode_t AppAtPrvLoRaDebugRead( void *p )
{
    uint32_t    debugMode;
    uint8_t     tmpDebugMode[ 4 ];        // 4 = sizeof(uint32_t)
    uint8_t     strDebugMode[ 4*2 + 1 ];  // 4*2 = string size of uint32_t hex value,  +1 = end of string

    debugMode = AppPrvLoRaDebugGetMode();

    tmpDebugMode[ 0 ] = (uint8_t)( debugMode >> 24 );
    tmpDebugMode[ 1 ] = (uint8_t)( debugMode >> 16 );
    tmpDebugMode[ 2 ] = (uint8_t)( debugMode >>  8 );
    tmpDebugMode[ 3 ] = (uint8_t)( debugMode       );

    AppAtHexArray2HexStr( strDebugMode, tmpDebugMode, sizeof(uint32_t) );

    AppAtOutputResultCode( (char *)strDebugMode );

    return AT_RC_OK;
}
#endif
