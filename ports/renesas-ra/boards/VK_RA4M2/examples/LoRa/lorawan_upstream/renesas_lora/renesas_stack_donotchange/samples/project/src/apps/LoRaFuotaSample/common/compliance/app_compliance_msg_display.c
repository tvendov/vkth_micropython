/*!
 * \file      LmHandlerMsgDisplay.h
 *
 * \brief     Common set of functions to display default messages from
 *            LoRaMacHandler.
 *
 * \copyright Revised BSD License, see section \ref LICENSE.
 *
 * \code
 *                ______                              _
 *               / _____)             _              | |
 *              ( (____  _____ ____ _| |_ _____  ____| |__
 *               \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 *               _____) ) ____| | | || |_| ____( (___| | | |
 *              (______/|_____)_|_|_| \__)_____)\____)_| |_|
 *              (C)2013-2019 Semtech
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 */
/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/

#if defined(APP_COMPLIANCE)

#include "board.h"
#include "lorawan_proc.h"

#include "LoRaCompliance.h"
#include "app_compliance_msg_display.h"

#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
/*!
 * MAC status strings
 */
const char* MacStatusStrings[] =
{
    "OK",                            // LORAMAC_STATUS_OK
    "Busy",                          // LORAMAC_STATUS_BUSY
    "Service unknown",               // LORAMAC_STATUS_SERVICE_UNKNOWN
    "Parameter invalid",             // LORAMAC_STATUS_PARAMETER_INVALID
    "Frequency invalid",             // LORAMAC_STATUS_FREQUENCY_INVALID
    "Datarate invalid",              // LORAMAC_STATUS_DATARATE_INVALID
    "Frequency or datarate invalid", // LORAMAC_STATUS_FREQ_AND_DR_INVALID
    "No network joined",             // LORAMAC_STATUS_NO_NETWORK_JOINED
    "Length error",                  // LORAMAC_STATUS_LENGTH_ERROR
    "Region not supported",          // LORAMAC_STATUS_REGION_NOT_SUPPORTED
    "Skipped APP data",              // LORAMAC_STATUS_SKIPPED_APP_DATA
    "Duty-cycle restricted",         // LORAMAC_STATUS_DUTYCYCLE_RESTRICTED
    "No channel found",              // LORAMAC_STATUS_NO_CHANNEL_FOUND
    "No free channel found",         // LORAMAC_STATUS_NO_FREE_CHANNEL_FOUND
    "Busy beacon reserved time",     // LORAMAC_STATUS_BUSY_BEACON_RESERVED_TIME
    "Busy ping-slot window time",    // LORAMAC_STATUS_BUSY_PING_SLOT_WINDOW_TIME
    "Busy uplink collision",         // LORAMAC_STATUS_BUSY_UPLINK_COLLISION
    "Crypto error",                  // LORAMAC_STATUS_CRYPTO_ERROR
    "FCnt handler error",            // LORAMAC_STATUS_FCNT_HANDLER_ERROR
    "MAC command error",             // LORAMAC_STATUS_MAC_COMMAD_ERROR
    "ClassB error",                  // LORAMAC_STATUS_CLASS_B_ERROR
    "Confirm queue error",           // LORAMAC_STATUS_CONFIRM_QUEUE_ERROR
    "Multicast group undefined",     // LORAMAC_STATUS_MC_GROUP_UNDEFINED
    "Unknown error",                 // LORAMAC_STATUS_ERROR
    "Radio fail",                    // LORAMAC_STATUS_RADIO_FAIL
    "Radio parameter invalid"        // LORAMAC_STATUS_RADIO_PARAMETER_INVALID
};

/*!
 * MAC event info status strings.
 */
const char *const EventInfoStatusStrings[] =
{ 
    "OK",                            // LORAMAC_EVENT_INFO_STATUS_OK
    "Error",                         // LORAMAC_EVENT_INFO_STATUS_ERROR
    "Tx timeout",                    // LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT
    "Rx 1 timeout",                  // LORAMAC_EVENT_INFO_STATUS_RX1_TIMEOUT
    "Rx 2 timeout",                  // LORAMAC_EVENT_INFO_STATUS_RX2_TIMEOUT
    "Rx1 error",                     // LORAMAC_EVENT_INFO_STATUS_RX1_ERROR
    "Rx2 error",                     // LORAMAC_EVENT_INFO_STATUS_RX2_ERROR
    "Join failed",                   // LORAMAC_EVENT_INFO_STATUS_JOIN_FAIL
    "Join nonce failed",             // LORAMAC_EVENT_INFO_STATUS_JOIN_NONCE_FAIL
    "Downlink repeated",             // LORAMAC_EVENT_INFO_STATUS_DOWNLINK_REPEATED
    "Tx DR payload size error",      // LORAMAC_EVENT_INFO_STATUS_TX_DR_PAYLOAD_SIZE_ERROR
#if (LORAMAC_VERSION < LORAWAN_VERSION_1_0_4)  // LW1.0.3
    "Downlink too many frames loss", // LORAMAC_EVENT_INFO_STATUS_DOWNLINK_TOO_MANY_FRAMES_LOSS
#endif
    "Address fail",                  // LORAMAC_EVENT_INFO_STATUS_ADDRESS_FAIL
    "MIC fail",                      // LORAMAC_EVENT_INFO_STATUS_MIC_FAIL
    "Multicast fail",                // LORAMAC_EVENT_INFO_STATUS_MULTICAST_FAIL
    "Beacon locked",                 // LORAMAC_EVENT_INFO_STATUS_BEACON_LOCKED
    "Beacon lost",                   // LORAMAC_EVENT_INFO_STATUS_BEACON_LOST
    "Beacon not found"               // LORAMAC_EVENT_INFO_STATUS_BEACON_NOT_FOUND
};

/*!
 * MAC primitive strings.
 */
static const char *const MlmePrimitiveStrings[] =
{
    "JOIN",
    "REJOIN_0",     // V1.1.x
    "REJOIN_1",     // V1.1.x
    "LINK_CHECK",
    "TXCW",
    "SCHEDULE_UPLINK",
    "DERIVE_MC_KE_KEY",
    "DERIVE_MC_KEY_PAIR",
    "DEVICE_TIME",
    "BEACON",
    "BEACON_ACQUISITION",
    "PING_SLOT_INFO",
    "BEACON_TIMING",
    "BEACON_LOST"
};
#endif

#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
/*!
 * Prints the provided buffer in HEX
 * 
 * \param buffer Buffer to be printed
 * \param size   Buffer size to be printed
 */
void PrintHexBuffer( uint8_t *buffer, uint8_t size )
{
    uint8_t newline = 0;

    for( uint8_t i = 0; i < size; i++ )
    {
        if( newline != 0 )
        {
            print_newline();
            newline = 0;
        }

        print_hex( buffer[i], 2 );

        if( ( ( i + 1 ) % 16 ) == 0 )
        {
            newline = 1;
        }
    }
    print_newline();
}
#endif


void DisplayMacMcpsRequestUpdate( LoRaMacStatus_t status, McpsReq_t *mcpsReq, TimerTime_t nextTxIn )
{
#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
    print( "[MCPS-Request(" );
    print( ((mcpsReq->Type==MCPS_CONFIRMED)? "CONFIRMED": (mcpsReq->Type==MCPS_UNCONFIRMED)? "UNCONFIRMED": "ERROR") );
    print( ")]" );
    print_newline();
    print( "STATUS      : " );
    print( (char *)MacStatusStrings[status] );
    print_newline();

    if( status == LORAMAC_STATUS_DUTYCYCLE_RESTRICTED )
    {
        print( "Next Tx in  : " );
        print_dec( (uint32_t)nextTxIn, 10, '\0' );
        print( " [ms]" );
        print_newline();
    }
    print_newline();
#endif
}


void DisplayMacMlmeRequestUpdate( LoRaMacStatus_t status, MlmeReq_t *mlmeReq, TimerTime_t nextTxIn )
{
#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
    print( "[MLME-Request(" );
    print( (char *)MlmePrimitiveStrings[mlmeReq->Type] );
    print( ")]" );
    print_newline();
    print( "STATUS      : " );
    print( (char *)MacStatusStrings[status] );
    print_newline();

    if( status == LORAMAC_STATUS_DUTYCYCLE_RESTRICTED )
    {
        print( "Next Tx in  : " );
        print_dec( (uint32_t)nextTxIn, 10, '\0' );
        print( " [ms]" );
        print_newline();
    }
    print_newline();
#endif
}

void DisplayMacMlmeJoinConfirmUpdate( MlmeConfirm_t *mlmeConfirm )
{
#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
    print( "[MLME-Confirm(" );
    print( (char *)MlmePrimitiveStrings[mlmeConfirm->MlmeRequest] );
    print( ")]" );
    print_newline();
    print( "STATUS      : " );
    print( (char *)EventInfoStatusStrings[mlmeConfirm->Status] );
    print_newline();

    if( AppLoraWanGetActMode() == APP_LORAWAN_ACTMODE_OTAA )
    {
        if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK )
        {
            print( "OTAA" );
            print_newline();
            print( "DevAddr     : " );
            print_hex( AppLoraWanGetDevAddr( ), 8 );
            print_newline();
            print( "DATA RATE   : DR_" );
            print_dec( AppLoraWanGetDR( ), 3, '\0' );
            print_newline();
        }
    }
#if 0   // In case of ABP, this function will not be called because MlmeConfirm(Join) is not available for ABP
    else
    {
        print( "ABP" );
        print_newline();
        print( "DevAddr     : " );
        print_hex( AppLoraWanGetDevAddr( ), 8 );
        print_newline();
    }
#endif
    print_newline();
#endif
}



void DisplayMacMcpsConfirmUpdate( LoRaComplianceAppData_t *appData, McpsConfirm_t *mcpsConfirm )
{
#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
    MibRequestConfirm_t mibGet;

    print( "[MCPS-Confirm]" );
    print_newline();
    print( "STATUS      : " );
    print( (char *)EventInfoStatusStrings[mcpsConfirm->Status] );
    print_newline();
    print( "UPLINK COUNTER: " );
    print_dec( mcpsConfirm->UpLinkCounter, 10, '\0' );
    print_newline();
    print( "CLASS       : " );
    switch( AppLoraWanGetDeviceClass( ) )
    {
        case CLASS_A:
            print( "A" );
            break;
        case CLASS_B:
            print( "B" );
            break;
        case CLASS_C:
            print( "C" );
            break;
        default:
            break;
    }
    print_newline();
    print( "TX PORT     : " );
    print_dec( appData->Port, 3, '\0' );
    print_newline();

    if( mcpsConfirm->McpsRequest == MCPS_CONFIRMED )
    {
        print( "CONFIRMED - " );
        print( ( mcpsConfirm->AckReceived != 0 ) ? "ACK" : "NACK" );
        print_newline();
    }
    else
    {
        print( "UNCONFIRMED" );
        print_newline();
    }
    if( appData->BufferSize != 0 )
    {
        print( "TX DATA     : " );
        PrintHexBuffer( appData->Buffer, appData->BufferSize );
    } 

    print( "DATA RATE   : DR_" );
    print_dec( mcpsConfirm->Datarate, 3, '\0' );
    print_newline();


    mibGet.Type  = MIB_CHANNELS;
    if( LoRaMacMibGetRequestConfirm( &mibGet ) == LORAMAC_STATUS_OK )
    {
        print( "U/L FREQ    : " );
        print_dec( mibGet.Param.ChannelList[mcpsConfirm->Channel].Frequency, 10, '\0' );
        print_newline();
    }
    print( "TX POWER    : " );
    print_dec( mcpsConfirm->TxPower, 3, '\0' );
    print_newline();

    mibGet.Type  = MIB_CHANNELS_MASK;
    if( LoRaMacMibGetRequestConfirm( &mibGet ) == LORAMAC_STATUS_OK )
    {
        print("CHANNEL MASK: ");
 
        switch( AppLoraWanGetRegion( ) )
        {
            case LORAMAC_REGION_AS923:
            case LORAMAC_REGION_CN779:
            case LORAMAC_REGION_EU868:
            case LORAMAC_REGION_IN865:
            case LORAMAC_REGION_KR920:
            case LORAMAC_REGION_EU433:
            case LORAMAC_REGION_RU864:
            {
                print_hex( mibGet.Param.ChannelsMask[0], 4 );
                break;
            }
            case LORAMAC_REGION_AU915:
            case LORAMAC_REGION_CN470:
            case LORAMAC_REGION_US915:
            {
                for( uint8_t i = 0; i < 5; i++)
                {
                    print_hex( mibGet.Param.ChannelsMask[i], 4 );
                }
                break;
            }
            default:
            {
                break;
            }
        }
        print_newline();
    }

    print_newline();
#endif
}

void DisplayMacMlmeConfirmUpdate( MlmeConfirm_t *mlmeConfirm )
{
#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
    print( "[MLME-Confirm(" );
    print( (char *)MlmePrimitiveStrings[mlmeConfirm->MlmeRequest] );
    print( ")]" );
    print_newline();
    print( "STATUS      : " );
    print( (char *)EventInfoStatusStrings[mlmeConfirm->Status] );
    print_newline();

    // MlmeConfirm(Join) is not avillable for ABP but this function is commonly used to display join result for both OTAA and ABP
    if ( mlmeConfirm->MlmeRequest == MLME_JOIN )
    {
        if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK )
        {
            if( AppLoraWanGetActMode() == APP_LORAWAN_ACTMODE_OTAA )
            {
                print( "OTAA" );
                print_newline();
                print( "DevAddr     : " );
                print_hex( AppLoraWanGetDevAddr( ), 8 );
                print_newline();
                print( "DATA RATE   : DR_" );
                print_dec( AppLoraWanGetDR( ), 3, '\0' );
                print_newline();
            }
            else // if( AppLoraWanGetActMode() == APP_LORAWAN_ACTMODE_ABP )
            {
                print( "ABP" );
                print_newline();
                print( "DevAddr     : " );
                print_hex( AppLoraWanGetDevAddr( ), 8 );
                print_newline();
            }
        }
    }
    print_newline();
#endif
}

void DisplayMacMcpsIndicationUpdate( McpsIndication_t *mcpsIndication )
{
#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
    const char *slotStrings[] = { "1", "2", "C", "C Multicast", "B Ping-Slot", "B Multicast Ping-Slot" };

    print( "[MCPS-Indication]" );
    print_newline();
    print( "STATUS      : " );
    print( (char *)EventInfoStatusStrings[mcpsIndication->Status] );
    print_newline();
    print( "DOWNLINK COUNTER: " );
    print_dec( mcpsIndication->DownLinkCounter, 10, '\0' );
    print_newline();
    print( "RX WINDOW   : " );
    print( (char *)slotStrings[mcpsIndication->RxSlot] );
    print_newline();
    print( "RX PORT     : " );
    print_dec( mcpsIndication->Port, 3, '\0' );
    print_newline();
    if( mcpsIndication->BufferSize != 0 )
    {
        print( "RX DATA     : " );
        PrintHexBuffer( mcpsIndication->Buffer, mcpsIndication->BufferSize );
    }
    print( "DATA RATE   : DR_" );
    print_dec( mcpsIndication->RxDatarate, 3, '\0' );
    print_newline();
    print( "RX RSSI     : " );
    print_dec( mcpsIndication->Rssi, 3, '\0' );
    print_newline();
    print( "RX SNR      : " );
    print_dec( mcpsIndication->Snr, 3, '\0' );
    print_newline();
    print_newline();
#endif
}

void DisplayMacMlmeIndicationUpdate( MlmeIndication_t *mlmeIndication )
{
#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
    print( "[MLME-Indication(" );
    print( (char *)MlmePrimitiveStrings[mlmeIndication->MlmeIndication] );
    print( ")]" );
    print_newline();
    print( "STATUS      : " );
    print( (char *)EventInfoStatusStrings[mlmeIndication->Status] );
    print_newline();
    print_newline();
#endif
}

void DisplayBeaconUpdate( MlmeIndication_t *mlmeIndication )
{
#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
    switch( mlmeIndication->MlmeIndication )
    {
        case MLME_BEACON_LOST:
        {
            print( "[BEACON LOST]" );
            print_newline();
            break;
        }
        case MLME_BEACON:
        {
            if( mlmeIndication->Status == LORAMAC_EVENT_INFO_STATUS_BEACON_LOCKED )
            {
                print( "[BEACON " );
                print_dec( mlmeIndication->BeaconInfo.Time.Seconds, 10, '\0' );
                print( "]" );
                print_newline();
                if( mlmeIndication->BeaconInfo.isValidGwSpecific )
                {
                    print( "GW DESC     : " );
                    print_dec( mlmeIndication->BeaconInfo.GwSpecific.InfoDesc, 3, '\0' );
                    print_newline();
                    print( "GW INFO     : " );
                    PrintHexBuffer( mlmeIndication->BeaconInfo.GwSpecific.Info, 6 );
                    print_newline();
                }
                print( "FREQ        : " );
                print_dec( mlmeIndication->BeaconInfo.Frequency, 10, '\0' );
                print_newline();
                print( "DATA RATE   : DR_" );
                print_dec( mlmeIndication->BeaconInfo.Datarate, 3, '\0' );
                print_newline();
                print( "RX RSSI     : " );
                print_dec( mlmeIndication->BeaconInfo.Rssi, 5, '\0' );
                print_newline();
                print( "RX SNR      : " );
                print_dec( mlmeIndication->BeaconInfo.Snr, 3, '\0' );
                print_newline();
            }
            else
            {
                print( "[BEACON NOT RECEIVED]" );
                print_newline();
            }
            break;
        }
        default:
            break;
    }
    print_newline();
#endif
}

void DisplayClassUpdate( DeviceClass_t deviceClass )
{
#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
    print( "[Switch to Class " );
    switch( deviceClass )
    {
        case CLASS_A:
            print( "A" );
            break;
        case CLASS_B:
            print( "B" );
            break;
        case CLASS_C:
            print( "C" );
            break;
        default:
            break;
    }
    print( "]" );
    print_newline();
#endif
}

void DisplayAppInfo( const Version_t* appVersion )
{
#ifdef DEBUG_PRINT_COMPLIANCE_ENABLED
    print( "[AppInfo]" );
    print_newline();
    print( "Application version: " );
    print_dec( appVersion->Fields.Major, 3, '\0' );
    print( "." );
    print_dec( appVersion->Fields.Minor, 3, '\0' );
    print( "." );
    print_dec( appVersion->Fields.Revision, 3, '\0' );
    print_newline();
    print_newline();
#endif
}

#endif
