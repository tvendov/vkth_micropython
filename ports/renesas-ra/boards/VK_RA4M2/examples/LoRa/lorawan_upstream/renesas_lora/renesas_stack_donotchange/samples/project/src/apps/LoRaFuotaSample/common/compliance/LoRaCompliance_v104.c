/*!
 * \file      LmhpCompliance.c
 *
 * \brief     Implements the LoRa-Alliance certification protocol handling
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
 *              (C)2013-2018 Semtech
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 */
 
 /*
     (C) 2017-2022 Renesas Electronics Corporation.
      This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
 */
#if defined(APP_COMPLIANCE)

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "LoRaMacTest.h"
#include "lorawan_proc.h"
#include "LoRaCompliance.h"
#include "lora_sample.h"
#if defined(FUOTA_ENABLED)
#include "app_fuota_process.h"
#endif

#if (LORAMAC_VERSION == LORAWAN_VERSION_1_0_4)

/*!
 * LoRaWAN compliance certification protocol port number.
 *
 * LoRaWAN Specification V1.x.x, chapter 4.3.2
 */
#define COMPLIANCE_PORT 224

#define COMPLIANCE_ID 6
#define COMPLIANCE_VERSION 1

TimerTime_t AppComplianceGetDutyCycleWaitTime( void );
LoRaMacStatus_t AppComplianceSend( LoRaComplianceAppData_t *appData, bool isTxConfirmed );
LoRaMacStatus_t AppComplianceJoin( void );
LoRaMacStatus_t AppComplianceDeviceTimeReq( void );
LoRaMacStatus_t AppComplianceRequestClass( DeviceClass_t newClass );

#if defined(FUOTA_ENABLED) && (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
extern uint16_t FrgmntsessionCntPrev[ 4 ];  // in LoRaFragmentProcess.c
#endif

typedef struct ClassBStatus_s
{
    bool         IsBeaconRxOn;
    uint8_t      PingSlotPeriodicity;
    uint16_t     BeaconCnt;
    BeaconInfo_t Info;
} ClassBStatus_t;

/*!
 * LoRaWAN compliance tests support data
 */
typedef struct LoRaComplianceTestState_s
{
    bool                Initialized;
    bool                IsTxPending;
    TimerTime_t         TxPendingTimestamp;
    bool                IsTxConfirmed;
    uint8_t             DataBufferMaxSize;
    uint8_t             DataBufferSize;
    uint8_t*            DataBuffer;
    uint16_t            RxAppCnt;
#ifdef LORAMAC_CLASSB_ENABLED
    bool                IsBeaconRxStatusIndOn;
    ClassBStatus_t      ClassBStatus;
#endif
    bool                IsResetCmdPending;
    bool                IsClassReqCmdPending;
    DeviceClass_t       NewClass;
} LoRaComplianceTestState_t;

typedef enum ComplianceMoteCmd_e
{
    COMPLIANCE_PKG_VERSION_ANS      = 0x00,
    COMPLIANCE_ECHO_PAYLOAD_ANS     = 0x08,
    COMPLIANCE_RX_APP_CNT_ANS       = 0x09,
    COMPLIANCE_BEACON_RX_STATUS_IND = 0x41,
    COMPLIANCE_BEACON_CNT_ANS       = 0x43,
    COMPLIANCE_FRAG_SESSION_CNT_ANS = 0x52,
    COMPLIANCE_DUT_VERSION_ANS      = 0x7F,
} ComplianceMoteCmd_t;

typedef enum ComplianceSrvCmd_e
{
    COMPLIANCE_PKG_VERSION_REQ              = 0x00,
    COMPLIANCE_DUT_RESET_REQ                = 0x01,
    COMPLIANCE_DUT_JOIN_REQ                 = 0x02,
    COMPLIANCE_SWITCH_CLASS_REQ             = 0x03,
    COMPLIANCE_ADR_BIT_CHANGE_REQ           = 0x04,
    COMPLIANCE_REGIONAL_DUTY_CYCLE_CTRL_REQ = 0x05,
    COMPLIANCE_TX_PERIODICITY_CHANGE_REQ    = 0x06,
    COMPLIANCE_TX_FRAMES_CTRL_REQ           = 0x07,
    COMPLIANCE_ECHO_PAYLOAD_REQ             = 0x08,
    COMPLIANCE_RX_APP_CNT_REQ               = 0x09,
    COMPLIANCE_RX_APP_CNT_RESET_REQ         = 0x0A,
    COMPLIANCE_LINK_CHECK_REQ               = 0x20,
    COMPLIANCE_DEVICE_TIME_REQ              = 0x21,
    COMPLIANCE_PING_SLOT_INFO_REQ           = 0x22,
    COMPLIANCE_BEACON_RX_STATUS_IND_CTRL    = 0x40,
    COMPLIANCE_BEACON_CNT_REQ               = 0x42,
    COMPLIANCE_BEACON_CNT_RESET_REQ         = 0x44,
    COMPLIANCE_FRAG_SESSION_CNT_REQ         = 0x52,
    COMPLIANCE_TX_CW_REQ                    = 0x7D,
    COMPLIANCE_DUT_FPORT_224_DISABLE_REQ    = 0x7E,
    COMPLIANCE_DUT_VERSION_REQ              = 0x7F,
} ComplianceSrvCmd_t;

/*!
 * Holds the compliance test current context
 */
static LoRaComplianceTestState_t ComplianceTestState = {
    .Initialized           = false,
    .IsTxPending           = false,
    .TxPendingTimestamp    = 0,
    .IsTxConfirmed         = false,     // Unconfirmed message
    .DataBufferMaxSize     = 0,
    .DataBufferSize        = 0,
    .DataBuffer            = NULL,
    .RxAppCnt              = 0,
#ifdef LORAMAC_CLASSB_ENABLED
    .ClassBStatus          = { 0 },
    .IsBeaconRxStatusIndOn = false,
#endif
    .IsResetCmdPending     = false,
    .IsClassReqCmdPending  = false,
    .NewClass              = CLASS_A,
};

/*!
 * LoRaWAN compliance tests protocol handler parameters
 */
static LoRaComplianceParams_t* ComplianceParams;

/*!
 * Reset Beacon status structure
 */
#ifdef LORAMAC_CLASSB_ENABLED
static inline void ClassBStatusReset( void )
{
    memset1( ( uint8_t* ) &ComplianceTestState.ClassBStatus, 0, sizeof( ClassBStatus_t ) / sizeof( uint8_t ) );
}
#endif

/*!
 * Initializes the compliance tests with provided parameters
 *
 * \param [IN] params Structure containing the initial compliance
 *                    tests parameters.
 * \param [IN] dataBuffer        Pointer to main application buffer
 * \param [IN] dataBufferMaxSize Application buffer maximum size
 */
static void LoRaComplianceInit( void* params );

/*!
 * Returns the current compliance certification protocol initialization status.
 *
 * \retval status Compliance certification protocol initialization status
 *                [true: Initialized, false: Not initialized]
 */
static bool LoRaComplianceIsInitialized( void );

/*!
 * Returns if a package transmission is pending or not.
 *
 * \retval status Package transmission status
 *                [true: pending, false: Not pending]
 */
static bool LoRaComplianceIsTxPending( void );

/*!
 * Processes the LoRaMac Compliance events.
 */
void LoRaComplianceProcess( void );

/*!
 * Processes the MCPS Confirm
 *
 * \param [IN] mcpsConfirm     MCPS confirm primitive data
 */
static void LoRaComplianceOnMcpsConfirm( McpsConfirm_t *mcpsConfirm  );

/*!
 * Processes the MCPS Indication
 *
 * \param [IN] mcpsIndication     MCPS indication primitive data
 */
void LoRaComplianceOnMcpsIndication( McpsIndication_t* mcpsIndication );

/*!
 * Processes the MLME Confirm
 *
 * \param [IN] mlmeConfirm MLME confirmation primitive data
 */
void LoRaComplianceOnMlmeConfirm( MlmeConfirm_t *mlmeConfirm );

/*!
 * Processes the MLME Indication
 *
 * \param [IN] mlmeIndication     MLME indication primitive data
 */
void LoRaComplianceOnMlmeIndication( MlmeIndication_t* mlmeIndication );

/*!
 * Helper function to send the BeaconRxStatusInd message
 *
 * \param [IN] isBeaconRxStatusIndOn Indicates if the beacon status info is sent or not
 */
#ifdef LORAMAC_CLASSB_ENABLED
static void SendBeaconRxStatusInd( bool isBeaconRxStatusIndOn );
#endif

LoRaCompliancePackage_t CompliancePackage = {
    .Port                    = COMPLIANCE_PORT,
    .Init                    = LoRaComplianceInit,
    .IsInitialized           = LoRaComplianceIsInitialized,
    .IsTxPending             = LoRaComplianceIsTxPending,
    .Process                 = LoRaComplianceProcess,
    .OnMcpsConfirmProcess    = LoRaComplianceOnMcpsConfirm,  // Currently not used in this package
    .OnMcpsIndicationProcess = LoRaComplianceOnMcpsIndication,
    .OnMlmeConfirmProcess    = LoRaComplianceOnMlmeConfirm,
    .OnMlmeIndicationProcess = LoRaComplianceOnMlmeIndication,
    .OnMacMcpsRequest        = NULL,  // To be initialized by application
    .OnMacMlmeRequest        = NULL,  // To be initialized by application
};


static void LoRaComplianceInit( void* params )
{
    if( params != NULL )
    {
        ComplianceParams                      = ( LoRaComplianceParams_t* ) params;
        ComplianceTestState.DataBuffer        = ComplianceParams->DataBuffer;
        ComplianceTestState.DataBufferMaxSize = ComplianceParams->DataBufferMaxSize;
        ComplianceTestState.Initialized       = true;

        ComplianceTestState.RxAppCnt = 0;
#ifdef LORAMAC_CLASSB_ENABLED
        ClassBStatusReset( );
#endif
        ComplianceTestState.IsTxPending = false;
#ifdef LORAMAC_CLASSB_ENABLED
        ComplianceTestState.IsBeaconRxStatusIndOn = false;
#endif
        ComplianceTestState.IsResetCmdPending = false;
        ComplianceTestState.IsClassReqCmdPending = false;

        ComplianceTestState.IsTxConfirmed = false;
        ComplianceTestState.TxPendingTimestamp = 0;
        ComplianceTestState.DataBufferSize = 0;
        ComplianceTestState.NewClass = CLASS_A;
    }
    else
    {
        ComplianceParams                = NULL;
        ComplianceTestState.Initialized = false;
    }
}

static bool LoRaComplianceIsInitialized( void )
{
    return ComplianceTestState.Initialized;
}

static bool LoRaComplianceIsTxPending( void )
{
    return ComplianceTestState.IsTxPending;
}

void LoRaComplianceProcess( void )
{
    if( ComplianceTestState.IsResetCmdPending == true )
    {
        ComplianceTestState.IsResetCmdPending = false;

        // Call platform MCU reset API
        BoardResetMcu( );
    }

    if( ComplianceTestState.IsTxPending == true )
    {
        TimerTime_t now = TimerGetCurrentTime( );
        if( now > ( ComplianceTestState.TxPendingTimestamp + AppComplianceGetDutyCycleWaitTime( ) ) )
        {
            if( ComplianceTestState.DataBufferSize != 0 )
            {
                // Answer commands
                LoRaComplianceAppData_t appData = {
                    .Buffer     = ComplianceTestState.DataBuffer,
                    .BufferSize = ComplianceTestState.DataBufferSize,
                    .Port       = COMPLIANCE_PORT,
                };

                if( AppComplianceSend( &appData, ComplianceTestState.IsTxConfirmed ) != LORAMAC_STATUS_OK )
                {
                    // try to send the message again
                    ComplianceTestState.IsTxPending = true;
                }
                else
                {
                    ComplianceTestState.IsTxPending = false;
                }
                ComplianceTestState.TxPendingTimestamp = now;
            }
        }
    }
    else
    { // If no Tx is pending process other commands
        if( ComplianceTestState.IsClassReqCmdPending == true )
        {
            ComplianceTestState.IsClassReqCmdPending = false;
            AppComplianceRequestClass( ComplianceTestState.NewClass );
        }
    }

}

static void LoRaComplianceOnMcpsConfirm( McpsConfirm_t *mcpsConfirm  )
{
}

void LoRaComplianceOnMcpsIndication( McpsIndication_t* mcpsIndication )
{
    uint8_t cmdIndex        = 0;

    if( ComplianceTestState.Initialized == false )
    {
        return;
    }

    // Increment the compliance certification protocol downlink counter
    // Not counting downlinks on FPort 0
    if( ( mcpsIndication->Port > 0 ) || ( mcpsIndication->AckReceived == true ) )
    {
        ComplianceTestState.RxAppCnt++;
    }

    if( mcpsIndication->RxData == false )
    {
        return;
    }

    if( mcpsIndication->Port != COMPLIANCE_PORT )
    {
        return;
    }

    ComplianceTestState.DataBufferSize = 0;

    switch( mcpsIndication->Buffer[cmdIndex++] )
    {
    case COMPLIANCE_PKG_VERSION_REQ:
    {
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = COMPLIANCE_PKG_VERSION_ANS;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = COMPLIANCE_ID;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = COMPLIANCE_VERSION;
        break;
    }
    case COMPLIANCE_DUT_RESET_REQ:
    {
        ComplianceTestState.IsResetCmdPending = true;
        break;
    }
    case COMPLIANCE_DUT_JOIN_REQ:
    {
  #if defined(FUOTA_ENABLED) && (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        FuotaStop();
  #endif
        AppComplianceJoin( );
        break;
    }
    case COMPLIANCE_SWITCH_CLASS_REQ:
    {
        // CLASS_A = 0, CLASS_B = 1, CLASS_C = 2
        ComplianceTestState.NewClass = ( DeviceClass_t ) mcpsIndication->Buffer[cmdIndex++];
        ComplianceTestState.IsClassReqCmdPending = true;
        break;
    }
    case COMPLIANCE_ADR_BIT_CHANGE_REQ:
    {
        AppLoraWanSetAdrOnDR( mcpsIndication->Buffer[cmdIndex++] );
        break;
    }
    case COMPLIANCE_REGIONAL_DUTY_CYCLE_CTRL_REQ:
    {
        AppLoraWanSetDCycle( mcpsIndication->Buffer[cmdIndex++] );
        break;
    }
    case COMPLIANCE_TX_PERIODICITY_CHANGE_REQ:
    {
        // Periodicity in milli-seconds
        uint32_t periodicity[] = { 0, 5000, 10000, 20000, 30000, 40000, 50000, 60000, 120000, 240000, 480000 };
        uint8_t  index         = mcpsIndication->Buffer[cmdIndex++];

        if( index < ( sizeof( periodicity ) / sizeof( uint32_t ) ) )
        {
            if( ComplianceParams->OnTxPeriodicityChanged != NULL )
            {
                ComplianceParams->OnTxPeriodicityChanged( periodicity[index] );
            }
        }
        break;
    }
    case COMPLIANCE_TX_FRAMES_CTRL_REQ:
    {
        uint8_t frameType = mcpsIndication->Buffer[cmdIndex++];

        if( ( frameType == 1 ) || ( frameType == 2 ) )
        {
            ComplianceTestState.IsTxConfirmed = ( frameType != 1 ) ? true : false;
            ComplianceParams->OnTxFrameCtrlChanged( ComplianceTestState.IsTxConfirmed );
        }
        break;
    }
    case COMPLIANCE_ECHO_PAYLOAD_REQ:
    {
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = COMPLIANCE_ECHO_PAYLOAD_ANS;
        for( uint8_t i = 1; i < R_MIN( mcpsIndication->BufferSize, ComplianceTestState.DataBufferMaxSize );
                 i++ )
        {
            ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = mcpsIndication->Buffer[cmdIndex++] + 1;
        }
        break;
    }
    case COMPLIANCE_RX_APP_CNT_REQ:
    {
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = COMPLIANCE_RX_APP_CNT_ANS;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceTestState.RxAppCnt;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceTestState.RxAppCnt >> 8;
        break;
    }
    case COMPLIANCE_RX_APP_CNT_RESET_REQ:
    {
        ComplianceTestState.RxAppCnt = 0;
        break;
    }
    case COMPLIANCE_LINK_CHECK_REQ:
    {
        MlmeReq_t mlmeReq;
        mlmeReq.Type = MLME_LINK_CHECK;

        CompliancePackage.OnMacMlmeRequest( LoRaMacMlmeRequest( &mlmeReq ), &mlmeReq,
                                            mlmeReq.ReqReturn.DutyCycleWaitTime );
        break;
    }
    case COMPLIANCE_DEVICE_TIME_REQ:
    {
        AppComplianceDeviceTimeReq( );
        break;
    }
#ifdef LORAMAC_CLASSB_ENABLED
    case COMPLIANCE_PING_SLOT_INFO_REQ:
    {
        ComplianceTestState.ClassBStatus.PingSlotPeriodicity = mcpsIndication->Buffer[cmdIndex++];
        ComplianceParams->OnPingSlotPeriodicityChanged( ComplianceTestState.ClassBStatus.PingSlotPeriodicity );
        break;
    }
    case COMPLIANCE_BEACON_RX_STATUS_IND_CTRL:
    {
        ComplianceTestState.IsBeaconRxStatusIndOn = ( bool ) mcpsIndication->Buffer[cmdIndex++];
        break;
    }
    case COMPLIANCE_BEACON_CNT_REQ:
    {
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = COMPLIANCE_BEACON_CNT_ANS;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceTestState.ClassBStatus.BeaconCnt;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceTestState.ClassBStatus.BeaconCnt >> 8;
        break;
    }
    case COMPLIANCE_BEACON_CNT_RESET_REQ:
    {
        ComplianceTestState.ClassBStatus.BeaconCnt = 0;
        break;
    }
#endif
#if defined(FUOTA_ENABLED) && (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    case COMPLIANCE_FRAG_SESSION_CNT_REQ:
    {
        uint8_t     fragIndex;
        uint16_t    sessionCnt;

        fragIndex  = mcpsIndication->Buffer[cmdIndex] & 0x03;  // bit 1:0
        sessionCnt = FrgmntsessionCntPrev[fragIndex];
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = COMPLIANCE_FRAG_SESSION_CNT_ANS;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = fragIndex;  // bit2: 'Session is not supported' is 0
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = (uint8_t)( sessionCnt );
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = (uint8_t)( sessionCnt >> 8 );
        break;
    }
#endif
    case COMPLIANCE_TX_CW_REQ:
    {
        MlmeReq_t mlmeReq;
        if( mcpsIndication->BufferSize == 7 )
        {
            mlmeReq.Type = MLME_TXCW;
            mlmeReq.Req.TxCw.Timeout =
                ( uint16_t )( mcpsIndication->Buffer[cmdIndex] | ( mcpsIndication->Buffer[cmdIndex + 1] << 8 ) );
            cmdIndex += 2;
            mlmeReq.Req.TxCw.Frequency =
                ( (uint32_t)(mcpsIndication->Buffer[cmdIndex]) | ((uint32_t)(mcpsIndication->Buffer[cmdIndex + 1]) << 8) |
                               ((uint32_t)(mcpsIndication->Buffer[cmdIndex + 2]) << 16) ) *
                100;
            cmdIndex += 3;
            mlmeReq.Req.TxCw.Power = mcpsIndication->Buffer[cmdIndex++];

            CompliancePackage.OnMacMlmeRequest( LoRaMacMlmeRequest( &mlmeReq ), &mlmeReq,
                                                mlmeReq.ReqReturn.DutyCycleWaitTime );
        }
        break;
    }
    case COMPLIANCE_DUT_FPORT_224_DISABLE_REQ:
    {
        uint8_t complianceTestMode;
        complianceTestMode = appLoraWanSettings.complianceTestMode;  // != 0
        AppLoadParams( 0 );                // Load parameters in data flash not to overwrite with temporary parameters used in compliance test
        AppLoraWanSetCertFPortOn( false ); // disaable port 224 for certification
        appLoraWanSettings.complianceTestMode = complianceTestMode;
        AppSaveParams();
        ComplianceTestState.IsResetCmdPending = true;
        break;
    }
    case COMPLIANCE_DUT_VERSION_REQ:
    {
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = COMPLIANCE_DUT_VERSION_ANS;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->FwVersion.Fields.Major;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->FwVersion.Fields.Minor;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->FwVersion.Fields.Patch;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->FwVersion.Fields.Revision;

        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->lrwanVersion.Fields.Major;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->lrwanVersion.Fields.Minor;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->lrwanVersion.Fields.Patch;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->lrwanVersion.Fields.Revision;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->lrwanRpVersion.Fields.Major;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->lrwanRpVersion.Fields.Minor;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->lrwanRpVersion.Fields.Patch;
        ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceParams->lrwanRpVersion.Fields.Revision;
        break;
    }
    default:
    {
        break;
    }
    }

    if( ComplianceTestState.DataBufferSize != 0 )
    {
        ComplianceTestState.IsTxPending = true;
    }
    else
    {
        // Abort any pending Tx as a new command has been processed
        ComplianceTestState.IsTxPending = false;
    }
}

void LoRaComplianceOnMlmeConfirm( MlmeConfirm_t *mlmeConfirm )
{
#ifdef LORAMAC_CLASSB_ENABLED
    switch( mlmeConfirm->MlmeRequest )
    {
    case MLME_BEACON_ACQUISITION:
    {
        if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK )
        {
            ClassBStatusReset( );
            ComplianceTestState.ClassBStatus.IsBeaconRxOn = true;
        }
        else
        {
            ComplianceTestState.ClassBStatus.IsBeaconRxOn = false;
        }
        break;
    }
    default:
        break;
    }
#endif
}

void LoRaComplianceOnMlmeIndication( MlmeIndication_t* mlmeIndication )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( ComplianceTestState.Initialized == false )
    {
        return;
    }

    switch( mlmeIndication->MlmeIndication )
    {
    case MLME_BEACON_LOST:
    {
        ClassBStatusReset( );
        SendBeaconRxStatusInd( ComplianceTestState.IsBeaconRxStatusIndOn );
        break;
    }
    case MLME_BEACON:
    {
        if( mlmeIndication->Status == LORAMAC_EVENT_INFO_STATUS_BEACON_LOCKED )
        {
            // As we received a beacon ensure that IsBeaconRxOn is set to true
            if( ComplianceTestState.ClassBStatus.IsBeaconRxOn == false )
            {
                ComplianceTestState.ClassBStatus.IsBeaconRxOn = true;
            }
            ComplianceTestState.ClassBStatus.BeaconCnt++;
        }
        ComplianceTestState.ClassBStatus.Info = mlmeIndication->BeaconInfo;
        SendBeaconRxStatusInd( ComplianceTestState.IsBeaconRxStatusIndOn );
        break;
    }
    default:
        break;
    }
#endif
}

#ifdef LORAMAC_CLASSB_ENABLED
static void SendBeaconRxStatusInd( bool isBeaconRxStatusIndOn )
{
    if( isBeaconRxStatusIndOn == false )
    {
        return;
    }
    uint32_t frequency = ComplianceTestState.ClassBStatus.Info.Frequency / 100;

    ComplianceTestState.DataBufferSize = 0;
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = COMPLIANCE_BEACON_RX_STATUS_IND;
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( ComplianceTestState.ClassBStatus.IsBeaconRxOn == true ) ? 1 : 0;
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.BeaconCnt );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.BeaconCnt >> 8 );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( frequency );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( frequency >> 8 );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( frequency >> 16 );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ComplianceTestState.ClassBStatus.Info.Datarate;
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.Rssi );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.Rssi >> 8 );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.Snr );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.Param );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.Time.Seconds );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.Time.Seconds >> 8 );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.Time.Seconds >> 16 );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.Time.Seconds >> 24 );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.GwSpecific.InfoDesc );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.GwSpecific.Info[0] );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.GwSpecific.Info[1] );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.GwSpecific.Info[2] );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.GwSpecific.Info[3] );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.GwSpecific.Info[4] );
    ComplianceTestState.DataBuffer[ComplianceTestState.DataBufferSize++] = ( uint8_t )( ComplianceTestState.ClassBStatus.Info.GwSpecific.Info[5] );

    ComplianceTestState.IsTxPending = true;
}
#endif

#endif
#endif


