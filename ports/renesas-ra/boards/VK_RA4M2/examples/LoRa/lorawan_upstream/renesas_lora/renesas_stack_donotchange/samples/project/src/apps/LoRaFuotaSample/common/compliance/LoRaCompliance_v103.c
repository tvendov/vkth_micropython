/*!
 * \file      main.c
 *
 * \brief     LoRaMac classA/B/C device implementation
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
 *              (C)2013-2017 Semtech
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 *
 * \author    Gregory Cristian ( Semtech )
 */
/*
    (C) 2017-2022 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#if defined(APP_COMPLIANCE)

#include <stdio.h>
#include "board.h"

#include "lorawan_proc.h"
#include "LoRaCompliance.h"


#if (LORAMAC_VERSION == LORAWAN_VERSION_1_0_3)

/*!
 * Message type setting during compliance test mode
 */
#define LORA_COMPLIANCE_MESSAGE_TYPE                false

/*!
 * ADR (Adaptive Data Rate) setting during compliance test mode
 */
#define LORA_COMPLIANCE_ADR                         1

/*!
 * Duty cycle limitation setting during compliance test mode
 * it should be disabled to speed up test
 */
#define LORA_COMPLIANCE_DUTYCYCLE                   false

/*!
 * Defines the data transmission duty cycle during compliance test mode. 5s, value in [ms].
 */
#define LORA_COMPLIANCE_TX_DUTYCYCLE                5000

/*!
 * LoRaWAN application port
 */
#define APP_PORT                            2

/*!
 * User application data size
 */
#define APP_DATA_SIZE                       6

/*!
 * User application data buffer size
 */
#define APP_DATA_MAX_SIZE                   242

/*!
 * User application data
 */
 #ifdef LORAMAC_COMPLIANCE_BUFF_SHARE
extern uint8_t appAtOctBuff[APP_AT_OCT_BUFF_SIZE];
uint8_t *AppDataBuffer = appAtOctBuff;
#else
static uint8_t AppDataBuffer[APP_DATA_MAX_SIZE];
#endif

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
 * Initializes the compliance tests with provided parameters
 *
 * \param [IN] params Structure containing the initial compliance
 *                    tests parameters
 */
static void LoRaComplianceInit( void );

/*!
 * Processes the LoRaMac Compliance events.
 */
static void LoRaComplianceProcess( void );

/*!
 * Processes the MCPS confirm
 *
 * \param [IN] mcpsIndication  MCPS confirm primitive data
 */
static void LoRaComplianceOnMcpsConfirm( McpsConfirm_t* mcpsConfirm );

/*!
 * Processes the MCPS Indication
 *
 * \param [IN] mcpsIndication  MCPS indication primitive data
 */
static void LoRaComplianceOnMcpsIndication( McpsIndication_t* mcpsIndication );

/*!
 * Processes the MLME Confirm
 *
 * \param [IN] mlmeConfirm      MLME confirmation primitive data
 */
static void LoRaComplianceOnMlmeConfirm( MlmeConfirm_t *mlmeConfirm );

/*!
 * Processes the MLME Indication
 *
 * \param [IN] mlmeIndication   MLME indication primitive data
 */
static void LoRaComplianceOnMlmeIndication( MlmeIndication_t* mlmeIndication );

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

static void OnTxNextPacketTimerEvent( void );
static void PrepareTxFrame( void );
static void ProcessTxNextPacketTimerEvent( void );

LoRaMacStatus_t AppComplianceJoin( void );
LoRaMacStatus_t AppComplianceSend( LoRaComplianceAppData_t *appData, bool isTxConfirmed );


typedef enum ComplianceSrvCmd_e 
{
    COMPLIANCE_DEACTIVATED_TEST_MODE       = 0x00,
    COMPLIANCE_ACTIVATED_TEST_MODE         = 0x01,
    COMPLIANCE_CONFIRMED_FRAMES            = 0x02,         
    COMPLIANCE_UNCONFIRMED_FRAMES          = 0x03,
    COMPLIANCE_CRYPTOGRAPHY_TESTS          = 0x04,
    COMPLIANCE_LINK_CHECK_REQUEST          = 0x05,
    COMPLIANCE_TRIGGER_JOIN_REQUEST        = 0x06,
    COMPLIANCE_ENABLE_CONTINUOUS_WAVE_MODE = 0x07,
} ComplianceSrvCmd_t;

LoRaCompliancePackage_t CompliancePackage = {
    .Port                    = COMPLIANCE_PORT,
    .Init                    = LoRaComplianceInit,
    .IsInitialized           = LoRaComplianceIsInitialized,
    .IsTxPending             = LoRaComplianceIsTxPending,
    .Process                 = LoRaComplianceProcess,
    .OnMcpsConfirmProcess    = LoRaComplianceOnMcpsConfirm,
    .OnMcpsIndicationProcess = LoRaComplianceOnMcpsIndication,
    .OnMlmeConfirmProcess    = LoRaComplianceOnMlmeConfirm,
    .OnMlmeIndicationProcess = LoRaComplianceOnMlmeIndication,  

    // Notify to application when to issue requests in compliance protocol
    .OnMacMcpsRequest        = NULL,  // To be initialized by application 
    .OnMacMlmeRequest        = NULL,  // To be initialized by application 
};


static void LoRaComplianceInit( void )
{
    // Initialize ComplianceTestState
    memset( &ComplianceTestState, 0, sizeof(ComplianceTestState) );

    ComplianceTestState.AppDataMaxSize = APP_DATA_MAX_SIZE;
    ComplianceTestState.AppData.Buffer = AppDataBuffer;
    ComplianceTestState.AppData.Port   = AppLoraWanGetFPort();
    ComplianceTestState.IsTxConfirmed = (AppLoraWanGetMessageType() == MCPS_CONFIRMED)? true: false;

    // Backup application settings (Message type, Fport, ADR, Duty cycle).
    ComplianceTestState.Backups.isTxConfirmed = ComplianceTestState.IsTxConfirmed;
    ComplianceTestState.Backups.fPort = ComplianceTestState.AppData.Port;
    ComplianceTestState.Backups.adr = AppLoraWanGetAdr();
    ComplianceTestState.Backups.dCycle = AppLoraWanGetDCycle();

    TimerInit( &ComplianceTestState.TxNextPacketTimer, OnTxNextPacketTimerEvent );

    ComplianceTestState.Initialized = true; 

    // Start join process   
    ComplianceTestState.DeviceState = DEVICE_STATE_JOIN;
}

static bool LoRaComplianceIsInitialized( void )
{
    return ComplianceTestState.Initialized;
}

static bool LoRaComplianceIsTxPending( void )
{
    return ComplianceTestState.IsTxPending;
}

static void LoRaComplianceOnMcpsConfirm( McpsConfirm_t *mcpsConfirm  )
{
    // Update state for next Tx
    ComplianceTestState.DeviceState = DEVICE_STATE_CYCLE;
    ComplianceTestState.WakeUpState = DEVICE_STATE_SEND;
}

void LoRaComplianceProcess( void )
{
    LoRaMacStatus_t status;

    if( ComplianceTestState.DeviceState == DEVICE_STATE_NONE )
    {
        return;
    }

    do
    {
        ProcessTxNextPacketTimerEvent();

        switch( ComplianceTestState.DeviceState )
        {
            case DEVICE_STATE_JOIN:
            {
                // Join network
                AppComplianceJoin();
                break;
            }
            case DEVICE_STATE_SEND:
            {

                PrepareTxFrame( );

                status = AppComplianceSend( &ComplianceTestState.AppData, ComplianceTestState.IsTxConfirmed );

                if( status == LORAMAC_STATUS_OK )
                {
                    ComplianceTestState.DeviceState = DEVICE_STATE_SLEEP;
                    ComplianceTestState.WakeUpState = DEVICE_STATE_SLEEP;
                    // Wait for McpsConfirm
                }
                else
                {
                    ComplianceTestState.DeviceState = DEVICE_STATE_CYCLE;
                    ComplianceTestState.WakeUpState = DEVICE_STATE_SEND;
                }
                break;
            }
            case DEVICE_STATE_CYCLE:
            {
                ComplianceTestState.DeviceState = DEVICE_STATE_SLEEP;
                if( ComplianceTestState.Running == true )
                {
                    // Schedule next packet transmission
                    ComplianceTestState.TxDutyCycleTime = LORA_COMPLIANCE_TX_DUTYCYCLE;
                }
                else
                {
                    // Schedule next packet transmission
                    ComplianceTestState.TxDutyCycleTime = APP_TX_DUTYCYCLE + randr( -APP_TX_DUTYCYCLE_RND, APP_TX_DUTYCYCLE_RND );
                }

                // Schedule next packet transmission
                TimerSetValue( &ComplianceTestState.TxNextPacketTimer, ComplianceTestState.TxDutyCycleTime );
                TimerStart( &ComplianceTestState.TxNextPacketTimer );
                break;
            }
            case DEVICE_STATE_SLEEP:
            {
                // nothing to do. exit do-while loop
                break;
            }
            default:
            {
                ComplianceTestState.DeviceState = DEVICE_STATE_JOIN;
                break;
            }
        }
    }while ( ComplianceTestState.DeviceState != DEVICE_STATE_SLEEP );
}

void LoRaComplianceOnMcpsIndication( McpsIndication_t* mcpsIndication )
{
    MlmeReq_t           mlmeReq;
    LoRaMacStatus_t     status;
    uint8_t             i;

    // The server signals that it has pending data to be sent.
    // We schedule an uplink as soon as possible to flush the server.
    if( mcpsIndication->FramePending == true )
    {
        if( ComplianceTestState.DeviceState == DEVICE_STATE_SLEEP )
        {
            OnTxNextPacketTimerEvent( );
        }

    }

    if( ComplianceTestState.Running == true )
    {
        ComplianceTestState.DownLinkCounter++;
    }

    if( mcpsIndication->Port != COMPLIANCE_PORT )
    {
        return;
    }

    if( mcpsIndication->RxData == false )
    {
        return;
    }

    if( ComplianceTestState.Running == false )
    {
        switch( mcpsIndication->Buffer[0] )
        {
            case COMPLIANCE_ACTIVATED_TEST_MODE: // Aactivate test mode
                // Check if activated test mode command (i)
                if( ( mcpsIndication->BufferSize == 4 ) &&
                    ( mcpsIndication->Buffer[1] == 0x01 ) &&
                    ( mcpsIndication->Buffer[2] == 0x01 ) &&
                    ( mcpsIndication->Buffer[3] == 0x01 ) )
                {
                    ComplianceTestState.Running = true;

                    ComplianceTestState.DownLinkCounter = 0;
                    ComplianceTestState.LinkCheck = false;
                    ComplianceTestState.DemodMargin = 0;
                    ComplianceTestState.NbGateways = 0;
                    ComplianceTestState.State = 1;

                    ComplianceTestState.AppData.Port = COMPLIANCE_PORT;
                    ComplianceTestState.IsTxConfirmed = LORA_COMPLIANCE_MESSAGE_TYPE;

                    AppLoraWanSetAdr( LORA_COMPLIANCE_ADR );            // Force to enable ducy cycle
                    AppLoraWanSetDCycle( LORA_COMPLIANCE_DUTYCYCLE);    // Force to disable ducy cycle
        
                    ComplianceTestState.DeviceState = DEVICE_STATE_SEND;
                }
                break;
            default:
                break;
        }
    }
    else // if( ComplianceTestState.Running == true )
    {     
        ComplianceTestState.State = mcpsIndication->Buffer[0];
        switch( mcpsIndication->Buffer[0] )
        {
            case COMPLIANCE_DEACTIVATED_TEST_MODE: // Deactivate test mode (ii)
                ComplianceTestState.IsTxConfirmed = ComplianceTestState.Backups.isTxConfirmed;
                ComplianceTestState.AppData.Port = ComplianceTestState.Backups.fPort;
                ComplianceTestState.AppData.BufferSize = 0;
                ComplianceTestState.DownLinkCounter = 0;
                ComplianceTestState.Running = false;

                AppLoraWanSetDCycle( ComplianceTestState.Backups.adr );     // Restore applicatin settings
                AppLoraWanSetDCycle( ComplianceTestState.Backups.dCycle );  // Restore applicatin settings
                break;
            case COMPLIANCE_ACTIVATED_TEST_MODE: // (iii, iv)
                ComplianceTestState.State = 1;
                break;
            case COMPLIANCE_CONFIRMED_FRAMES: // Enable confirmed messages (v)
                ComplianceTestState.IsTxConfirmed = true;
                ComplianceTestState.State = 1;
                break;
            case COMPLIANCE_UNCONFIRMED_FRAMES:  // Disable confirmed messages (vi)
                ComplianceTestState.IsTxConfirmed = false;
                ComplianceTestState.State = 1;
                break;
            case COMPLIANCE_CRYPTOGRAPHY_TESTS: // (vii)
                ComplianceTestState.AppData.BufferSize = mcpsIndication->BufferSize;

                ComplianceTestState.AppData.Buffer[0] = COMPLIANCE_CRYPTOGRAPHY_TESTS;
                for( i = 1; i < R_MIN( ComplianceTestState.AppData.BufferSize, ComplianceTestState.AppDataMaxSize ); i++ )
                {
                    ComplianceTestState.AppData.Buffer[i] = mcpsIndication->Buffer[i] + 1;
                }
                break;
            case COMPLIANCE_LINK_CHECK_REQUEST: // (viii)
                {
                    mlmeReq.Type = MLME_LINK_CHECK;
                    status = LoRaMacMlmeRequest( &mlmeReq );

                    // Notify to the applicatoin
                    CompliancePackage.OnMacMlmeRequest( status, &mlmeReq, mlmeReq.ReqReturn.DutyCycleWaitTime );
                }
                break;
            case COMPLIANCE_TRIGGER_JOIN_REQUEST: // (ix)
                {
                    // Disable TestMode and revert back to normal operation
                    ComplianceTestState.IsTxConfirmed = ComplianceTestState.Backups.isTxConfirmed;
                    ComplianceTestState.AppData.Port = ComplianceTestState.Backups.fPort;
                    ComplianceTestState.AppData.BufferSize = 0;
                    ComplianceTestState.DownLinkCounter = 0;
                    ComplianceTestState.Running = false;

                    AppLoraWanSetDCycle( ComplianceTestState.Backups.adr );
                    AppLoraWanSetDCycle( ComplianceTestState.Backups.dCycle );

                    AppComplianceJoin( );
                }
                break;
            case COMPLIANCE_ENABLE_CONTINUOUS_WAVE_MODE: // (x)
                {
                    if( mcpsIndication->BufferSize == 7 )
                    {
                        mlmeReq.Type = MLME_TXCW;
                        mlmeReq.Req.TxCw.Timeout = ( ((uint16_t)mcpsIndication->Buffer[1] << 8) | 
                                                      (uint16_t)mcpsIndication->Buffer[2] );
                        mlmeReq.Req.TxCw.Frequency = ( ((uint32_t)mcpsIndication->Buffer[3] << 16) | 
                                                       ((uint32_t)mcpsIndication->Buffer[4] << 8) | 
                                                        (uint32_t)mcpsIndication->Buffer[5] ) * 100;
                        mlmeReq.Req.TxCw.Power = mcpsIndication->Buffer[6];
                        status = LoRaMacMlmeRequest( &mlmeReq );

                        CompliancePackage.OnMacMlmeRequest( status, &mlmeReq,
                                                                mlmeReq.ReqReturn.DutyCycleWaitTime );
                    }
                    ComplianceTestState.State = 1;
                }
                break;
        default:
            break;
        }
    }
}

static void LoRaComplianceOnMlmeConfirm( MlmeConfirm_t *mlmeConfirm )
{
    switch( mlmeConfirm->MlmeRequest )
    {
        case MLME_JOIN:
        {
            if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK )
            {
                // Status is OK, node has joined the network
                ComplianceTestState.DeviceState = DEVICE_STATE_CYCLE;
                ComplianceTestState.WakeUpState = DEVICE_STATE_SEND;
            }
            else
            {
                // Join was not successful. Try to join again
                ComplianceTestState.DeviceState = DEVICE_STATE_CYCLE;
                ComplianceTestState.WakeUpState = DEVICE_STATE_JOIN;
            }
            break;
        }
        case MLME_LINK_CHECK:
        {
            if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK )
            {
                // Check DemodMargin
                // Check NbGateways
                if( ComplianceTestState.Running == true )
                {
                    ComplianceTestState.LinkCheck = true;
                    ComplianceTestState.DemodMargin = mlmeConfirm->DemodMargin;
                    ComplianceTestState.NbGateways = mlmeConfirm->NbGateways;
                }
            }
            break;
        }
        default:
            break;
    }  
}

static void LoRaComplianceOnMlmeIndication( MlmeIndication_t* mlmeIndication )
{
    switch( mlmeIndication->MlmeIndication )
    {
        case MLME_SCHEDULE_UPLINK:
        {
            // The MAC signals that we shall provide an uplink as soon as possible
            if( ComplianceTestState.DeviceState == DEVICE_STATE_SLEEP )
            {
                OnTxNextPacketTimerEvent( );
            }
            break;
        }
        default:
            break;
    }
}

/*!
 * \brief Function executed on TxNextPacket Timeout event
 */
static void OnTxNextPacketTimerEvent( void )
{
    ComplianceTestState.Events.TxNextPacketTimer = 1;
}

static void ProcessTxNextPacketTimerEvent( void )
{
    uint8_t txNextPacketTimer;
    MibRequestConfirm_t mibReq;
    LoRaMacStatus_t status;

    CRITICAL_SECTION_BEGIN();
    txNextPacketTimer = ComplianceTestState.Events.TxNextPacketTimer;
    ComplianceTestState.Events.TxNextPacketTimer = 0;
    CRITICAL_SECTION_END();

    
    if( txNextPacketTimer != 0 )
    {
        // TxNextPacketTimer needs to be stopped in case OnTxNextPacketTimerEvent is called
        // before the timeout. (
        TimerStop( &ComplianceTestState.TxNextPacketTimer );  

        mibReq.Type = MIB_NETWORK_ACTIVATION;
        status = LoRaMacMibGetRequestConfirm( &mibReq );

        if( status == LORAMAC_STATUS_OK )
        {
            if( mibReq.Param.NetworkActivation == ACTIVATION_TYPE_NONE )
            {
                // Network not joined yet. Try to join again
                AppComplianceJoin( );
            }
            else
            {
                if( ComplianceTestState.DeviceState == DEVICE_STATE_SLEEP )
                {
                    ComplianceTestState.DeviceState = ComplianceTestState.WakeUpState;
                }
            }
        }
    }
}

/*!
 * \brief   Prepares the payload of the frame
 */
static void PrepareTxFrame( void )
{
    if( ComplianceTestState.AppData.Port == COMPLIANCE_PORT )
    {
        if( ComplianceTestState.LinkCheck == true )
        {
            ComplianceTestState.LinkCheck = false;
            ComplianceTestState.AppData.BufferSize = 3;
            ComplianceTestState.AppData.Buffer[0] = COMPLIANCE_LINK_CHECK_REQUEST;
            ComplianceTestState.AppData.Buffer[1] = ComplianceTestState.DemodMargin;
            ComplianceTestState.AppData.Buffer[2] = ComplianceTestState.NbGateways;
            ComplianceTestState.State = 1;
        }
        else
        {
            switch( ComplianceTestState.State )
            {
            case 4:
                ComplianceTestState.State = 1;
                break;
            case 1:
                ComplianceTestState.AppData.BufferSize = 2;
                ComplianceTestState.AppData.Buffer[0] = ComplianceTestState.DownLinkCounter >> 8;
                ComplianceTestState.AppData.Buffer[1] = ComplianceTestState.DownLinkCounter;
                break;
            }
        }
    }
    else if( ComplianceTestState.AppData.Port == ComplianceTestState.Backups.fPort )
    {
        ComplianceTestState.AppData.BufferSize = 6;
        ComplianceTestState.AppData.Buffer[0] = 'b';
        ComplianceTestState.AppData.Buffer[1] = 'a';
        ComplianceTestState.AppData.Buffer[2] = 'r';
        ComplianceTestState.AppData.Buffer[3] = 'f';
        ComplianceTestState.AppData.Buffer[4] = 'o';
        ComplianceTestState.AppData.Buffer[5] = 'o';
    }
}

#endif
#endif

