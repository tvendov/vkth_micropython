/*!
 * \file      main.c
 *
 * \brief     Performs a periodic uplink
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
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/

#if defined(APP_COMPLIANCE)

#include <stdio.h>


#include "board.h"

#include "lora_sample.h"
#include "lorawan_proc.h"
#include "app_compliance.h"
#if defined(FUOTA_ENABLED)
#include "app_fuota_process.h"
#endif


#if (LORAMAC_VERSION == LORAWAN_VERSION_1_0_4)

/*!
 * Firmware version (Need to change baesd on the version)
 */
#define FIRMWARE_VERSION                            APP_CONFIG_VERSION_FW


/*!
 * Defines the application data transmission duty cycle. 5s, value in [ms].
 */
#define APP_TX_DUTYCYCLE                            5000

/*!
 * Defines a random delay for application data transmission duty cycle. 1s,
 * value in [ms].
 */
#define APP_TX_DUTYCYCLE_RND                        1000

/*!
 * User application data buffer size
 */
#define APP_DATA_BUFFER_MAX_SIZE            242

/*!
 * User application data
 */
#ifdef LORAMAC_COMPLIANCE_BUFF_SHARE
extern uint8_t appAtOctBuff[APP_AT_OCT_BUFF_SIZE];
uint8_t *AppDataBuffer = appAtOctBuff;
#else
static uint8_t AppDataBuffer[APP_DATA_BUFFER_MAX_SIZE];
#endif

/*!
 * User application data structure
 */
static LoRaComplianceAppData_t AppData =
{
#ifdef LORAMAC_COMPLIANCE_BUFF_SHARE
    .Buffer = appAtOctBuff,
#else
    .Buffer = AppDataBuffer,
#endif

    .BufferSize = 0,
    .Port = 0,
};

static LoRaComplianceAppData_t SentData =
{
    .Buffer = NULL,
    .BufferSize = 0,
    .Port = 0,
};


/*!
 * Compliance application parameters 
 */
static TimerEvent_t TxTimer;            // Timer to handle the application data transmission duty cycle
static TimerTime_t DutyCycleWaitTime = 0;
static volatile uint8_t IsTxFramePending = 0;
static volatile uint32_t TxPeriodicity = 0;
static bool IsTxConfirmed = false;
#ifdef LORAMAC_CLASSB_ENABLED
static uint8_t PingSlotPeriodicity = 7;
#endif

/*!
 * Indicates if an uplink is pending upon MAC layer request
 * 
 */
static bool IsUplinkTxPending = false;

/*!
 * Indicates if a switch to Class B operation is pending or not.
 * 
 */
#ifdef LORAMAC_CLASSB_ENABLED
static bool IsClassBSwitchPending = false;
#endif

/*!
 * Indicates if LoRaMacProcess call is pending.
 * 
 * \warning If variable is equal to 0 then the MCU can be set in low power mode
 */


static void McpsConfirm( McpsConfirm_t *mcpsConfirm );
static void McpsIndication( McpsIndication_t *mcpsIndication );
static void MlmeConfirm( MlmeConfirm_t *mlmeConfirm );
static void MlmeIndication( MlmeIndication_t *mlmeIndication );

static void OnMacMcpsRequest( LoRaMacStatus_t status, McpsReq_t *mcpsReq, TimerTime_t nextTxIn );
static void OnMacMlmeRequest( LoRaMacStatus_t status, MlmeReq_t *mlmeReq, TimerTime_t nextTxIn );
static void OnMacMcpsConfirm( McpsConfirm_t *mcpsConfirm );
static void OnMacMlmeConfirm( MlmeConfirm_t *mlmeConfirm );
static void OnMacMcpsIndication( McpsIndication_t *mcpsIndication );
static void OnMacMlmeIndication( MlmeIndication_t *mlmeIndication );

static void OnClassChange( DeviceClass_t deviceClass );
static void OnBeaconStatusChange( MlmeIndication_t *mlmeIndication );
#if( LMH_SYS_TIME_UPDATE_NEW_API == 1 )
static void OnSysTimeUpdate( bool isSynchronized, int32_t timeCorrection );
#else
static void OnSysTimeUpdate( void );
#endif
static void PrepareTxFrame( void );

static void StartTxProcess( void );
static void UplinkProcess( void );


static void OnTxPeriodicityChanged( uint32_t periodicity );
static void OnTxFrameCtrlChanged( bool isTxConfirmed );
static void OnPingSlotPeriodicityChanged( uint8_t pingSlotPeriodicity );

/*!
 * Function executed on TxTimer event
 */
static void OnTxTimerEvent( void );

static void SendFrame( void );
LoRaMacStatus_t AppComplianceSend( LoRaComplianceAppData_t *appData, bool isTxConfirmed );
LoRaMacStatus_t AppComplianceJoin( void );
LoRaMacStatus_t AppComplianceJoinStatus( void );
TimerTime_t AppComplianceGetDutyCycleWaitTime( void );
LoRaMacStatus_t AppComplianceDeviceTimeReq( void );
LoRaMacStatus_t AppComplianceRequestClass( DeviceClass_t newClass );


static LoRaComplianceParams_t ComplianceParams =
{
    .FwVersion.Value = FIRMWARE_VERSION,
    .lrwanVersion.Value = LORAMAC_VERSION,
    .lrwanRpVersion.Value = REGION_VERSION,

    .DataBufferMaxSize = APP_DATA_BUFFER_MAX_SIZE,

#ifdef LORAMAC_COMPLIANCE_BUFF_SHARE
    .DataBuffer = appAtOctBuff,
#else
    .DataBuffer = AppDataBuffer,
#endif

    .OnTxPeriodicityChanged = OnTxPeriodicityChanged,
    .OnTxFrameCtrlChanged = OnTxFrameCtrlChanged,
    .OnPingSlotPeriodicityChanged = OnPingSlotPeriodicityChanged,
};


void AppCompliacneInit ( void )
{
    LoRaMacPrimitives_t macPrimitives;

	if( appLoraWanSettings.complianceTestMode == APP_COMPLIANCE_TESTMODE_NONE )
	{
		return;
	}

    DutyCycleWaitTime = 0;
    IsTxFramePending = 0;
    IsTxConfirmed = (AppLoraWanGetMessageType() == MCPS_CONFIRMED)? true: false;
#ifdef LORAMAC_CLASSB_ENABLED
    PingSlotPeriodicity = AppLoraWanGetPeriodicity();
#endif
    IsUplinkTxPending = false;
    TxPeriodicity = APP_TX_DUTYCYCLE + randr( -APP_TX_DUTYCYCLE_RND, APP_TX_DUTYCYCLE_RND );

    macPrimitives.MacMcpsConfirm = McpsConfirm;
    macPrimitives.MacMcpsIndication = McpsIndication;
    macPrimitives.MacMlmeConfirm = MlmeConfirm;
    macPrimitives.MacMlmeIndication = MlmeIndication;

    AppLoraWanInit( &macPrimitives, NULL );

    // Set callback functions for the compliance package to notify to the application layer
    // that McpsRequest or MlmeRequest is called in ompliance package
    CompliancePackage.OnMacMcpsRequest        = OnMacMcpsRequest;
    CompliancePackage.OnMacMlmeRequest        = OnMacMlmeRequest;

    CompliancePackage.Init( &ComplianceParams ); 


    AppComplianceJoin( );

    StartTxProcess( );
}

void AppComplianceProcess( void )
{
    if( appLoraWanSettings.complianceTestMode == APP_COMPLIANCE_TESTMODE_NONE )
    {
        return;
    }

	// Processes the LoRaMac events
    CompliancePackage.Process();

	// Process application uplinks management
	UplinkProcess( );

}


static void OnMacMcpsRequest( LoRaMacStatus_t status, McpsReq_t *mcpsReq, TimerTime_t nextTxIn )
{
    DisplayMacMcpsRequestUpdate( status, mcpsReq, nextTxIn );
}

static void OnMacMlmeRequest( LoRaMacStatus_t status, MlmeReq_t *mlmeReq, TimerTime_t nextTxIn )
{
    DisplayMacMlmeRequestUpdate( status, mlmeReq, nextTxIn );
}


static void OnMacMcpsConfirm( McpsConfirm_t *mcpsConfirm )
{
    // Display Tx parameters
    DisplayMacMcpsConfirmUpdate( &SentData, mcpsConfirm );
}

static void OnMacMlmeConfirm( MlmeConfirm_t* mlmeConfirm )
{
#if defined(FUOTA_ENABLED)
    AppFuotaMlmeConfirm( mlmeConfirm );
#endif
    DisplayMacMlmeConfirmUpdate( mlmeConfirm );
}


static void OnMacMcpsIndication( McpsIndication_t *mcpsIndication )
{
    DisplayMacMcpsIndicationUpdate( mcpsIndication );
#if defined(FUOTA_ENABLED)
    FuotaStatus_t fuotaProc;
    fuotaProc = AppFuotaMcpsIndication( mcpsIndication );
    if (fuotaProc == FUOTA_STATUS_OK )
    {
        print("Fuota Indication\r\n");
    }
#endif
}
static void OnMacMlmeIndication( MlmeIndication_t *mlmeIndication )
{
     DisplayMacMlmeIndicationUpdate( mlmeIndication );
}

static void OnClassChange( DeviceClass_t deviceClass )
{
    DisplayClassUpdate( deviceClass );

    // Inform the server as soon as possible that the end-device has switched to ClassB
    LoRaComplianceAppData_t appData =
    {
        .Buffer = NULL,
        .BufferSize = 0,
        .Port = 0,
    };
    AppComplianceSend( &appData, false );
}

static void OnBeaconStatusChange( MlmeIndication_t *mlmeIndication )
{
    DisplayBeaconUpdate( mlmeIndication );
}

#if( LMH_SYS_TIME_UPDATE_NEW_API == 1 )
static void OnSysTimeUpdate( bool isSynchronized, int32_t timeCorrection )
{

}
#else
static void OnSysTimeUpdate( void )
{

}
#endif

/*!
 * Prepares the payload of the frame and transmits it.
 */
static void PrepareTxFrame( void )
{
    AppData.Port = AppLoraWanGetFPort();   
    AppData.BufferSize = 6;
    AppData.Buffer[0] = 'b';
    AppData.Buffer[1] = 'a';
    AppData.Buffer[2] = 'r';
    AppData.Buffer[3] = 'f';
    AppData.Buffer[4] = 'o';
    AppData.Buffer[5] = 'o';
}

static void SendFrame( void )
{
    if( LoRaMacIsBusy( ) == true )
    {
        return;
    }

    if( CompliancePackage.IsTxPending() == true )
    {
        return;
    }

    PrepareTxFrame( );

    if( AppComplianceSend( &AppData, IsTxConfirmed ) == LORAMAC_STATUS_OK )
    {
    }
}


static void StartTxProcess( void )
{
    // Schedule 1st packet transmission
    TimerInit( &TxTimer, OnTxTimerEvent );
    TimerSetValue( &TxTimer, TxPeriodicity );

    OnTxTimerEvent();
}


static void UplinkProcess( void )
{
    uint8_t isPending = 0;
    CRITICAL_SECTION_BEGIN( );
    isPending = IsTxFramePending;
    IsTxFramePending = 0;
    CRITICAL_SECTION_END( );
    if( isPending == 1 )
    {
        SendFrame( );
    }
}

static void OnTxPeriodicityChanged( uint32_t periodicity )
{
    TxPeriodicity = periodicity;

    if( TxPeriodicity == 0 )
    { // Revert to application default periodicity
        TxPeriodicity = APP_TX_DUTYCYCLE + randr( -APP_TX_DUTYCYCLE_RND, APP_TX_DUTYCYCLE_RND );
    }
}

static void OnTxFrameCtrlChanged( bool isTxConfirmed )
{
    IsTxConfirmed = isTxConfirmed;
}

static void OnPingSlotPeriodicityChanged( uint8_t pingSlotPeriodicity )
{
#ifdef LORAMAC_CLASSB_ENABLED
    PingSlotPeriodicity = pingSlotPeriodicity;
#endif
}

/*!
 * Function executed on TxTimer event
 */
static void OnTxTimerEvent( void )
{
  #if defined(FUOTA_ENABLED)
    if( AppFuotaIsLowPowerAllowed() == true )  // set pending bit when FUOTA does not send frame
  #endif
    {
        IsTxFramePending = 1;
    }

    // Schedule next transmission
    TimerSetValue( &TxTimer, TxPeriodicity );
    TimerStart( &TxTimer );
}


static void McpsConfirm( McpsConfirm_t *mcpsConfirm )
{
    OnMacMcpsConfirm( mcpsConfirm );

    CompliancePackage.OnMcpsConfirmProcess( mcpsConfirm );
}

static void McpsIndication( McpsIndication_t *mcpsIndication )
{
    if( mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK )
    {
        return;
    }

    OnMacMcpsIndication( mcpsIndication );

    if( mcpsIndication->DeviceTimeAnsReceived == true )
    {
#if( LMH_SYS_TIME_UPDATE_NEW_API == 1 )
        // Provide fix values. DeviceTimeAns is accurate
        OnSysTimeUpdate( true, 0 );
#else
        OnSysTimeUpdate( );
#endif
    }
        // Call compliance packages RxProcess function
    if( CompliancePackage.OnMcpsIndicationProcess != NULL )
    {
        CompliancePackage.OnMcpsIndicationProcess( mcpsIndication );
    }

    if( ( ( mcpsIndication->FramePending == true ) && ( AppLoraWanGetDeviceClass( ) == CLASS_A ) ) ||
        ( mcpsIndication->ResponseTimeout > 0 ) )
    {
        // The server signals that it has pending data to be sent.
        // We schedule an uplink as soon as possible to flush the server.
        IsUplinkTxPending = true;

        // Send an empty message
        LoRaComplianceAppData_t appData =
        {
            .Buffer = NULL,
            .BufferSize = 0,
            .Port = 0,
        };
        AppComplianceSend( &appData, IsTxConfirmed );
    }
}

static void MlmeConfirm( MlmeConfirm_t *mlmeConfirm )
{
#ifdef LORAMAC_CLASSB_ENABLED
    LoRaMacStatus_t status;
    MlmeReq_t mlmeReq;
#endif

    OnMacMlmeConfirm( mlmeConfirm );
    CompliancePackage.OnMlmeConfirmProcess( mlmeConfirm );

    switch( mlmeConfirm->MlmeRequest )
    {
    case MLME_JOIN:
        {
            if( mlmeConfirm->Status != LORAMAC_EVENT_INFO_STATUS_OK )
            {
                AppComplianceJoin();
            }
  #if defined(FUOTA_ENABLED) && (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
            else  // if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK )
            {
                if( appLoraWanSettings.complianceTestMode == APP_COMPLIANCE_TESTMODE_FUOTA200 )
                {
                    uint8_t         ib;
                    uint32_t        ibParam32;
                    uint8_t         ibParam8;
                
                    // ClockSync: turn off periodic transmission of AppTimeReq
                    ib        = FUOTA_IB_CLKSNC_TIMEREQ_PERIOD_SEC;
                    ibParam32 = 0;
                    (void)AppFuotaIbSetRequest( ib, (void *)&ibParam32 );
            
                    // ClockSync: set turn off periodic transmission of AppTimeReq
                    ib       = FUOTA_IB_CLKSNC_FORCESYNC_PERIOD_SEC;
                    ibParam8 = 10;
                    (void)AppFuotaIbSetRequest( ib, (void *)&ibParam8 );

                    // start FUOTA
                    AppFuotaStart();
                }
            }
  #endif
        }
        break;
    case MLME_LINK_CHECK:
        {
            // Check DemodMargin
            // Check NbGateways
        }
        break;
        
#ifdef LORAMAC_CLASSB_ENABLED
    case MLME_DEVICE_TIME:
        {
            if( IsClassBSwitchPending == true )
            {
                LoRaMacStatus_t status;
                MlmeReq_t mlmeReq;

                status = AppLoraWanBeaconAcquisition( );
                
                mlmeReq.Type = MLME_BEACON_ACQUISITION;
                OnMacMlmeRequest( status, &mlmeReq, 0 );
            }
        }
        break;
    case MLME_BEACON_ACQUISITION:
        {
            if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK )
            {
                // Beacon has been acquired
                // Request server for ping slot
                status = AppLoraWanPingSlotInfo( PingSlotPeriodicity );

                mlmeReq.Type = MLME_PING_SLOT_INFO;
                OnMacMlmeRequest( status, &mlmeReq, 0 );
            }
            else
            {
                // Beacon not acquired
                // Request Device Time again.
                status = AppLoraWanDeviceTime();

                mlmeReq.Type = MLME_DEVICE_TIME;
                OnMacMlmeRequest( status, &mlmeReq, 0 );
            }
        }
        break;
    case MLME_PING_SLOT_INFO:
        {
            if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK )
            {
                // Class B is now activated
                status = AppLoraWanSetDeviceClass( CLASS_B );
                if ( status == LORAMAC_STATUS_OK )
                {
                    OnClassChange( CLASS_B );
                                
                    IsClassBSwitchPending = false;
                }
            }
            else
            {
                status = AppLoraWanPingSlotInfo( PingSlotPeriodicity );

                mlmeReq.Type = MLME_PING_SLOT_INFO;
                OnMacMlmeRequest( status, &mlmeReq, 0 );
            }
        }
        break;
#endif
    default:
        break;
    }
}

static void MlmeIndication( MlmeIndication_t *mlmeIndication )
{
    if( mlmeIndication->Status != LORAMAC_EVENT_INFO_STATUS_BEACON_LOCKED )
    {
        OnMacMlmeIndication( mlmeIndication );
    }

    // Call Compliacne packages MlmeIndication function
     CompliancePackage.OnMlmeIndicationProcess( mlmeIndication );

    switch( mlmeIndication->MlmeIndication )
    {
    case MLME_SCHEDULE_UPLINK:
        {// The MAC signals that we shall provide an uplink as soon as possible
            // Send an empty message
            LoRaComplianceAppData_t appData =
            {
                .Buffer = NULL,
                .BufferSize = 0,
                .Port = 0,
            };

            AppComplianceSend( &appData, IsTxConfirmed );
        }
        break;
    case MLME_BEACON_LOST:
        {
            OnBeaconStatusChange( mlmeIndication );

            OnClassChange( CLASS_A );
            AppComplianceDeviceTimeReq( );
        }
        break;
    case MLME_BEACON:
        {
            OnBeaconStatusChange( mlmeIndication );
        }
        break;
     default:
        break;
    }
}


LoRaMacStatus_t AppComplianceJoin( void )
{
    LoRaMacStatus_t status;

    if( appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_OTAA )
    {  
        status = AppLoraWanJoinReq( &DutyCycleWaitTime );
        
        // Notify to the application MlmeRequest(Join) is issued
        MlmeReq_t mlmeReq;
        mlmeReq.Type = MLME_JOIN;
        OnMacMlmeRequest( status, &mlmeReq, DutyCycleWaitTime );  
    }
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    else // if( appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_ABP )
    {
        // Set network jointed to true
        status = AppLoraWanSetNetworkJoined( true );
        DutyCycleWaitTime = 0;
    }
#else
    else
    {
        status = LORAMAC_STATUS_SERVICE_UNKNOWN;
    }
#endif

    return( status );
}

LoRaMacStatus_t AppComplianceJoinStatus( void )
{
    MibRequestConfirm_t mibReq;
    LoRaMacStatus_t status;

    mibReq.Type = MIB_NETWORK_ACTIVATION;
    status = LoRaMacMibGetRequestConfirm( &mibReq );

    if( status == LORAMAC_STATUS_OK )
    {
        if( mibReq.Param.NetworkActivation == ACTIVATION_TYPE_NONE )
        {
            status = LORAMAC_STATUS_NO_NETWORK_JOINED;
        }
    }

    return status;
}

LoRaMacStatus_t AppComplianceSend( LoRaComplianceAppData_t *appData, bool isTxConfirmed )
{
    LoRaMacStatus_t status;
    McpsReq_t mcpsReq;
    LoRaMacTxInfo_t txInfo;

    if( AppComplianceJoinStatus( ) == LORAMAC_STATUS_NO_NETWORK_JOINED )
    {
        // The network isn't joined, try again.
        AppComplianceJoin( );
        return LORAMAC_STATUS_NO_NETWORK_JOINED;
    }


    if( LoRaMacQueryTxPossible( appData->BufferSize, &txInfo ) != LORAMAC_STATUS_OK )
    {
        // Send empty frame in order to flush MAC commands
        // or notify the test tool that the end device cannot send frame due to uplink size limitation
        isTxConfirmed = false;
        appData->Buffer = NULL;
        appData->BufferSize = 0;
        appData->Port = 0;
    }

    status = AppLoraWanSendData( appData->Buffer, appData->BufferSize, appData->Port, 
                                    (isTxConfirmed? MCPS_CONFIRMED: MCPS_UNCONFIRMED),
                                    &DutyCycleWaitTime);

    mcpsReq.Type = (isTxConfirmed? MCPS_CONFIRMED: MCPS_UNCONFIRMED); 
    OnMacMcpsRequest( status, &mcpsReq, DutyCycleWaitTime );

    // Save parameters to display sent data in McpsConfirm later
    SentData.Buffer = appData->Buffer;
    SentData.BufferSize = appData->BufferSize;
    SentData.Port = appData->Port;
    if( status == LORAMAC_STATUS_SKIPPED_APP_DATA )
    {
        SentData.BufferSize = 0;
        SentData.Port = 0;
    }

    if( (status == LORAMAC_STATUS_OK) || (status == LORAMAC_STATUS_SKIPPED_APP_DATA) )
    {
        IsUplinkTxPending = false;
    }

    return status;
}

LoRaMacStatus_t AppComplianceDeviceTimeReq( void )
{
    LoRaMacStatus_t status;
    MlmeReq_t mlmeReq;

    mlmeReq.Type = MLME_DEVICE_TIME;

    status = LoRaMacMlmeRequest( &mlmeReq );
    OnMacMlmeRequest( status, &mlmeReq, mlmeReq.ReqReturn.DutyCycleWaitTime );
    DutyCycleWaitTime = mlmeReq.ReqReturn.DutyCycleWaitTime;

    return status;
}


LoRaMacStatus_t AppComplianceRequestClass( DeviceClass_t newClass )
{
    MibRequestConfirm_t mibReq;
    DeviceClass_t currentClass;
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;

    mibReq.Type = MIB_DEVICE_CLASS;
    LoRaMacMibGetRequestConfirm( &mibReq );
    currentClass = mibReq.Param.Class;

    // Attempt to switch only if class update
    if( currentClass != newClass )
    {
        switch( newClass )
        {
        case CLASS_A:
            {
                status = AppLoraWanSetDeviceClass( CLASS_A );
                if( status == LORAMAC_STATUS_OK )
                {
                    // Switch is instantaneous
                    OnClassChange( CLASS_A );
                }
            }
            break;
#ifdef LORAMAC_CLASSB_ENABLED
        case CLASS_B:
            {
                if( currentClass != CLASS_A )
                {
                    status = LORAMAC_STATUS_SERVICE_UNKNOWN;
                }
                // Beacon must first be acquired
                status = AppComplianceDeviceTimeReq( );
                IsClassBSwitchPending = true;
            }
            break;
#endif
        case CLASS_C:
            {
                if( currentClass != CLASS_A )
                {
                    status = LORAMAC_STATUS_SERVICE_UNKNOWN;
                }
                // Switch is instantaneous
                status = AppLoraWanSetDeviceClass( CLASS_C );
                if( status == LORAMAC_STATUS_OK )
                {
                    // Switch is instantaneous
                    OnClassChange( CLASS_C );
                }
            }
            break;
        default:
            break;
        }
    }
    return status;
}

TimerTime_t AppComplianceGetDutyCycleWaitTime( void )
{
    return DutyCycleWaitTime;
}


#endif // if (LORAMAC_VERSION == LORAWAN_VERSION_1_0_4)
#endif // defined(APP_COMPLIANCE)

