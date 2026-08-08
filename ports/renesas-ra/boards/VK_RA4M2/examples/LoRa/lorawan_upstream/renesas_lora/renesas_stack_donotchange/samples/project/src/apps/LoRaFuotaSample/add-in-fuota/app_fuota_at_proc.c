/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "timer.h"
#include "LoRaMac.h"
#include "at_proc.h"

#include "app_fuota_process.h"
#include "app_fuota_at_proc.h"

#define FUOTA_MCPSIND_PORT				    0
#define FUOTA_MCPSIND_BUFFER			    1
#define FUOTA_MCPSIND_OPTIONAL			    2

#define FUOTA_INDICATION_RMTMC_SETUP        0
#define FUOTA_INDICATION_RMTMC_START        1
#define FUOTA_INDICATION_RMTMC_END          2
#define FUOTA_INDICATION_FWUPDT_RDY         128
#define FUOTA_INDICATION_ERROR_FWUPDT_RDY   255
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define FUOTA_INDICATION_FWIMG_REMOVED      129
#define FUOTA_INDICATION_REBOOT_REQ         240
#define FUOTA_INDICATION_REBOOT_TIMING      241
#endif

#define FUOTA_OCT_BUFF_SIZE				    256
#define FUOTA_OCT_SETHEX_SIZE			    2

#define FUOTA_DEBUG_FPORT_MCCONTROL		    200

#define PRINT_MCPS_UNCONFIRMED			    0
#define PRINT_MCPS_CONFIRMED			    1
#define PRINT_MCPS_MULTICAST			    2

#define CODE_CHANGE_FUOTASTART			    1

//Global value
uint8_t GL_fuota_Status;
bool GL_Get_Status_Check;

// tools prototypes (from at_proc.c)
extern int32_t AppAtStr2Dec(int8_t *str, uint8_t len);
extern AtResultCode_t AppAtHexStr2HexDataArray(uint8_t *hexDataArray, int8_t *hexStr, uint16_t hexStrLen);
extern AtResultCode_t AppAtHexStr2HexDataArrayWithPadding(uint8_t *hexDataArray, uint16_t hexDataArrayLen, int8_t *hexStr, uint16_t hexStrLen);

#if (AT_EXTCMD_TAB_ARRAYSIZE > 0)
/*!
 * @fn
 * AT+FUOTASTART: Start FUOTA
 */
AtResultCode_t AppAtFuotaStartAct( void *p );

/*!
 * @fn
 * AT+FUOTASTART: Stop FUOTA
 */
AtResultCode_t AppAtFuotaStopAct( void *p );

/*!
 * @fn
 * AT+FUOTAGET: Get FUOTA IB
 */
AtResultCode_t AppAtFuotaIbGet( void *p );

/*!
 * @fn
 * AT+FUOTASET: Set FUOTA IB
 */
AtResultCode_t AppAtFuotaIbSet( void *p );

/*!
 * @fn
 * AT+FUOTAUPDT: Start F/W update
 */
AtResultCode_t AppAtFuotaUpdateAct( void *p );

#ifdef DEBUG_FUOTA
/*!
 * @fn
 * at+FUOTACMD: Input FUOTA frame for debug
 *              AT+FUOTACMD=(FPort),(Command|HexStr),(Optional Parameter|Type)
 */
AtResultCode_t AppAtFuotaCommandInput( void *p );

/*!
 * @fn
 * at+FUOTACMD: Get result of input frame for debug
 * Check FUOTA_Status
 */
AtResultCode_t AppAtFuotaCommandResult( void *p );
#endif  // DEBUG_FUOTA
#endif  // AT_EXTCMD_TAB_ARRAYSIZE

//------------------------------------------------------------------------

/* Commands for FUOTA */
void AppAtFuotaExtendCmdRegist( void )
{
#if (AT_EXTCMD_TAB_ARRAYSIZE > 0)
    AtExtendCmdRegist("+FUOTASTART", NULL, NULL, AppAtFuotaStartAct);
    AtExtendCmdRegist("+FUOTASTOP", NULL, NULL, AppAtFuotaStopAct);
    AtExtendCmdRegist("+FUOTAGET", AppAtFuotaIbGet, NULL, NULL);
    AtExtendCmdRegist("+FUOTASET", AppAtFuotaIbSet, NULL, NULL);
    AtExtendCmdRegist("+FUOTAUPDT", NULL, NULL, AppAtFuotaUpdateAct);

#ifdef DEBUG_FUOTA
    AtExtendCmdRegist("+FUOTACMD", AppAtFuotaCommandInput, AppAtFuotaCommandResult, NULL);
#endif
#endif
}

AtResultCode_t AppAtFuotaStartAct( void *p )
{
    AppFuotaStart();

    return AT_RC_OK;
}

AtResultCode_t AppAtFuotaStopAct( void *p )
{
    AppFuotaStop();

    return AT_RC_OK;
}

AtResultCode_t AppAtFuotaIbGet( void *p )
{
    AtResultCode_t  ret;
    uint8_t         argc;
    int8_t          *argv;
    int16_t         argvLen;

    FuotaStatus_t   funcRet;
    uint8_t         ib;
    uint32_t        ibParam32;
    uint8_t         ibParam8_16[16];
    void            *vpVal;

    // init
    ret = AT_RC_ERR;

    argc = AtParseListLen();
    if( argc == 1 ) {
        // get IB
        AtParsePopOne( &argv, &argvLen );

        //ib = AppAtStr2Dec( argv, argvLen );
        AppAtHexStr2HexDataArray( &ib, argv, 2 );

        switch( ib )
        {
            case FUOTA_IB_CLKSNC_TIMEREQ_PERIOD_SEC:
            case FUOTA_IB_PROC_POLLING_PERIOD_SEC:
                vpVal = (void *)&ibParam32;
                break;

            default:
                vpVal = (void *)ibParam8_16;
                break;
        }

        funcRet = AppFuotaIbGetRequest( ib, vpVal );

        if( funcRet == FUOTA_STATUS_OK )
        {
            switch( ib )
            {
                case FUOTA_IB_CLKSNC_TIMEREQ_PERIOD_SEC:
                case FUOTA_IB_PROC_POLLING_PERIOD_SEC:
                    AppAtOutputResultDec( ibParam32, 10, '\0' );
                    break;

                default:
                    AppAtOutputResultDec( ibParam8_16[0], 3, '\0' );
                    break;
            }

            ret = AT_RC_OK;
        }
    }

    return ret;
}

AtResultCode_t AppAtFuotaIbSet( void *p )
{
    AtResultCode_t  ret;
    uint8_t         argc;
    int8_t          *argv;
    int16_t         argvLen;

    FuotaStatus_t   funcRet;
    uint8_t         ib;
    uint32_t        ibParam32;
    uint8_t         ibParam8_16[16];
    void            *vpVal;

    // init
    ret   = AT_RC_ERR;
    vpVal = NULL;

    argc = AtParseListLen();
    if( argc == 2 ) {
        // get IB
        AtParsePopOne( &argv, &argvLen );

        //ib = AppAtStr2Dec( argv, argvLen );
        AppAtHexStr2HexDataArray( &ib, argv, 2 );

        // get value
        AtParsePopOne( &argv, &argvLen );
        switch( ib )
        {
            case FUOTA_IB_CLKSNC_TIMEREQ_PERIOD_SEC:
            case FUOTA_IB_PROC_POLLING_PERIOD_SEC:
                ibParam32 = AppAtStr2Dec( argv, argvLen );
                vpVal = (void *)&ibParam32;
                ret = AT_RC_OK;
                break;

            default:
                ibParam8_16[0] = (uint8_t)AppAtStr2Dec( argv, argvLen );
                vpVal = (void *)ibParam8_16;
                ret = AT_RC_OK;
                break;
        }

        if( ret == AT_RC_OK )
        {
            funcRet = AppFuotaIbSetRequest( ib, vpVal );
            if( funcRet != FUOTA_STATUS_OK )
            {
                ret = AT_RC_ERR;
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * AT+FUOTAUPDT: Start F/W update
 */
AtResultCode_t AppAtFuotaUpdateAct( void *p )
{
    FuotaStatus_t   res;

    res = AppFuotaStartFirmwareUpdate();

    // only come here in case of error.
    AppAtFuotaUpdateActResult( res );

    return AT_RC_NOANS;
}

void AppAtFuotaUpdateActResult( FuotaStatus_t result )
{
    AtResultCode_t  rc;

    switch( result )
    {
        case FUOTA_STATUS_OK:
            rc = AT_RC_OK;
            break;

        case FUOTA_STATUS_BUSY:
            rc = AT_RC_BUSY;
            break;

        default:
            rc = AT_RC_ERR;
            break;
    }

    AtCmdSetRcStatus(rc);
    AtPrintResultCode();
}

void AppAtFuotaUpdateActConfirm( FuotaStatus_t result )
{
    AtPrintCmdHeader( "+FUOTAUPDT" );
    print(" ");
    if( result == FUOTA_STATUS_OK )
    {
        print( "SUCCESS" );
    }
    else
    {
        print( "FAILED" );
    }
    AtPrintTrailer();
}

#ifdef DEBUG_FUOTA
AtResultCode_t AppAtFuotaCommandInput( void *p )
{
    uint8_t argc;                   /* number of argument */
    int8_t *argv;                   /* argument           */
    int32_t SetParams;
    uint8_t Set_buffer[FUOTA_OCT_BUFF_SIZE];
    int16_t len, loop_cnt;
    uint8_t set_cnt, print_loop_cnt;
    uint8_t param_set_cnt;
    McpsIndication_t mcpsInd, *p_mcpsInd;
    MibRequestConfirm_t mibGet;
    FuotaStatus_t    fuotaProc;
    AtResultCode_t ret = AT_RC_ERR;

    //init
    param_set_cnt = 0;
    set_cnt = 0;
    fuotaProc = FUOTA_STATUS_OK;
    mcpsInd.McpsIndication = MCPS_UNCONFIRMED;
    mcpsInd.Status = LORAMAC_EVENT_INFO_STATUS_OK;
	GL_fuota_Status = FUOTA_STATUS_OK;
	GL_Get_Status_Check = true;

    mibGet.Type          = MIB_DEV_ADDR;
    mibGet.Param.DevAddr = 0;
    LoRaMacMibGetRequestConfirm( &mibGet );
    mcpsInd.DevAddress = mibGet.Param.DevAddr;

    argc = AtParseListLen();
    if( 1 <= argc )
    {
        param_set_cnt = 1;
        while( param_set_cnt == 1 )
        {
            SetParams = 0;
            AtParsePopOne(&argv, &len);
            
            for( loop_cnt = 0; loop_cnt < len; loop_cnt++ )
            {
                if( (argv[loop_cnt] < '0') || ('9' < argv[loop_cnt]) )
                {
                    if( (('A' <= argv[loop_cnt]) && (argv[loop_cnt] <= 'F')) || 
                        (('a' <= argv[loop_cnt]) && (argv[loop_cnt] <= 'f')) )
                    {
                        //no
                    }
                    else
                    {
                        ret = AT_RC_ERR;
                        param_set_cnt = 0;
                        return ret;
                    }
                }
            }
            if( set_cnt == FUOTA_MCPSIND_BUFFER )
            {
                ret = AppAtHexStr2HexDataArrayWithPadding(Set_buffer, len / 2, argv, len);
            }
            else
            {
                SetParams = AppAtStr2Dec(argv, len);
            }
            
            if( set_cnt == argc )
            {
                param_set_cnt = 0;
            }
            else
            {
                switch( set_cnt )
                {
                    case FUOTA_MCPSIND_PORT:
                    {
                        mcpsInd.Port = SetParams;
                        break;
                    }
                    case FUOTA_MCPSIND_BUFFER:
                    {
                        mcpsInd.Buffer = Set_buffer;
                        mcpsInd.BufferSize = len / 2;
                        break;
                    }
                    case FUOTA_MCPSIND_OPTIONAL:
                    {
                        mcpsInd.McpsIndication = (Mcps_t)SetParams;
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
            set_cnt+=1;
        }
        print( " ** FUOTA: Value to pass to McpsIndication" );
        print_newline();
        print( "           Type         = 0x" );
        print_hex( mcpsInd.McpsIndication, 2 );
        switch( mcpsInd.McpsIndication )
        {
            case PRINT_MCPS_UNCONFIRMED:
            {
                print( "(MCPS_UNCONFIRMED)" );
                print_newline();
                break;
            }
            case PRINT_MCPS_CONFIRMED:
            {
                print( "(MCPS_CONFIRMED)" );
                print_newline();
                break;
            }
            case PRINT_MCPS_MULTICAST:
            {
                print( "(MCPS_MULTICAST)" );
                print_newline();
                break;
            }
            default:
            {
                print_newline();
                break;
            }
        }
        print( "           Status       = 0x" );
        print_hex( mcpsInd.Status, 2 );
        print_newline();
        print( "           Port         = " );
        print_dec( mcpsInd.Port, 3, '\0' );
        print_newline();
        print( "           Buffer       = 0x" );
        for(print_loop_cnt = 0; print_loop_cnt < mcpsInd.BufferSize; print_loop_cnt++ )
        {
            print_hex( Set_buffer[print_loop_cnt], 2 );
        }
        print_newline();
        print( "           BufferSize   = " );
        print_dec( mcpsInd.BufferSize, 3, '\0' );
        print_newline();
        
        p_mcpsInd = &mcpsInd;
        
        fuotaProc = AppFuotaMcpsIndication( p_mcpsInd );
        
        if( fuotaProc != FUOTA_STATUS_OK )
        {
            ret = AT_RC_OK;
        }
    }
    return ret;
}

AtResultCode_t AppAtFuotaCommandResult( void *p )
{
    print( " ** FUOTA_STATUS: " );
    switch( GL_fuota_Status )
    {
        case FUOTA_STATUS_OK:
        {
            print( "OK" );
            break;
        }
        case FUOTA_STATUS_LENGTH_ERROR:
        {
            print( "LENGTH_ERROR" );
            break;
        }
        case FUOTA_STATUS_COMMAND_ERROR:
        {
            print( "COMMAND_ERROR" );
            break;
        }
        case FUOTA_STATUS_ERROR:
        {
            print( "ERROR" );
            break;
        }
        case FUOTA_STATUS_PARAMETER_INVALID:
        {
            print( "PARAMETER_INVALID" );
            break;
        }
        default:
        {
            break;
        }
    }
    print( "(0x" );
    print_hex( GL_fuota_Status, 2 );
    print( ")" );
    print_newline();
    return AT_RC_OK;
}
#endif  // DEBUG_FUOTA

//------------------------------------------------------------------------
// indication

/*!
 * @fn
 * +FUOTAIND:0 ... Remote multicast setup indicaiton
 */
void AppAtFuotaRmtMcSessionSetupIndication( DeviceClass_t sessionClass, 
                                            uint8_t       mcGroupId, 
                                            uint32_t      timeToStartSec,
                                            uint32_t      timeoutSec )
{
    AtPrintCmdHeader( "+FUOTAIND" );
    print(" ");
    print_dec( FUOTA_INDICATION_RMTMC_SETUP, 3, '\0' );
    print(",");
    print_dec( (long)sessionClass, 3, '\0' );
    print(",");
    print_dec( mcGroupId, 1, '\0' );
    print(",");
    print_dec( timeToStartSec, 10, '\0' );
    print(",");
    print_dec( timeoutSec, 10, '\0' );
    AtPrintTrailer();
 }

/*!
 * @fn
 * +FUOTAIND:1 ... Start remote multicast session
 */
 void AppAtFuotaRmtMcSessionStartIndication( DeviceClass_t sessionClass, 
                                             uint8_t       mcGroupId, 
                                             uint32_t      timeoutSec )
{
    AtPrintCmdHeader( "+FUOTAIND" );
    print(" ");
    print_dec( FUOTA_INDICATION_RMTMC_START, 3, '\0' );
    print(",");
    print_dec( (long)sessionClass, 3, '\0' );
    print(",");
    print_dec( mcGroupId, 1, '\0' );
    print(",");
    print_dec( timeoutSec, 10, '\0' );
    AtPrintTrailer();
}

/*!
 * @fn
 * +FUOTAIND:2 ... End remote multicast session
 */
void AppAtFuotaRmtMcSessionEndIndication( DeviceClass_t sessionClass, uint8_t mcGroupId )
{
    AtPrintCmdHeader( "+FUOTAIND" );
    print(" ");
    print_dec( FUOTA_INDICATION_RMTMC_END, 3, '\0' );
    print(",");
    print_dec( (long)sessionClass, 3, '\0' );
    print(",");
    print_dec( mcGroupId, 1, '\0' );
    AtPrintTrailer();
}

/*!
 * @fn
 * +FUOTAIND:128 ... F/W update is ready.
 */
void AppAtFuotaUpdateReadyIndication( void )
{
    AtPrintCmdHeader( "+FUOTAIND" );
    print(" ");
    print_dec( FUOTA_INDICATION_FWUPDT_RDY, 3, '\0' );
    AtPrintTrailer();
}

/*!
 * @fn
 * +FUOTAIND:255 ... Error is occurred during update preparation.
 */
void AppAtFuotaUpdateErrorIndication( uint8_t result )
{
    AtPrintCmdHeader( "+FUOTAIND" );
    print(" ");
    print_dec( FUOTA_INDICATION_ERROR_FWUPDT_RDY, 3, '\0' );
    print(",-");
    print_dec( result, 2, '\0' );
    AtPrintTrailer();
}

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*!
 * @fn
 * +FUOTAIND:129 ... F/W image is removed.
 */
void AppAtFuotaUpdateDeleteFwImageIndication( uint32_t fwImageVersion )
{
    AtPrintCmdHeader( "+FUOTAIND" );
    print(" ");
    print_dec( FUOTA_INDICATION_FWIMG_REMOVED, 3, '\0' );
    print(",");
    print_hex( fwImageVersion, 8 );
    AtPrintTrailer();
}

/*!
 * @fn
 * +FUOTAIND:240 ... server request the reboot
 */
void AppAtFuotaUpdateTimeToRebootSecIndication( uint32_t rebootTimeSec )
{
    AtPrintCmdHeader( "+FUOTAIND" );
    print(" ");
    print_dec( FUOTA_INDICATION_REBOOT_REQ, 3, '\0' );
    print(",");
    print_dec( rebootTimeSec, 8, '\0' );
    AtPrintTrailer();
}

/*!
 * @fn
 * +FUOTAIND:241 ... reboot time has come
 */
void AppAtFuotaUpdateRebootTimingIndication( void )
{
    AtPrintCmdHeader( "+FUOTAIND" );
    print(" ");
    print_dec( FUOTA_INDICATION_REBOOT_TIMING, 3, '\0' );
    AtPrintTrailer();
}
#endif  // FUOTA_VERSION

#ifdef DEBUG_FUOTA
/*!
 * FUOTA: Debug print( Command Uplink )
 */
void AppAtFuotaDebugPrintUplink( uint8_t fport, uint8_t *p_buffer, uint8_t length )
{
    uint8_t loop_cnt;

    AtPrintCmdHeader("+FUOTAUPLINK");
    print( " " );
    print_dec( fport, 3, '\0' );
    print( "," );
    for( loop_cnt = 0; loop_cnt < length; loop_cnt++ )
    {
        print_hex( p_buffer[loop_cnt], 2 );
    }
    print( "," );
    print_dec( length, 3, '\0' );
    AtPrintTrailer();
}

/*!
 *
 */
void AppAtFuotaDebugGetMcpsIndResult( uint8_t fuotaStatus )
{
	if( GL_Get_Status_Check == true )
	{
		GL_fuota_Status = fuotaStatus;
		GL_Get_Status_Check = false;
	} 
}
#endif  // DEBUG_FUOTA
