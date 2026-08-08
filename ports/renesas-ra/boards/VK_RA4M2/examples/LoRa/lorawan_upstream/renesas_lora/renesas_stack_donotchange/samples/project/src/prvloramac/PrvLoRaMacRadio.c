/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacRadio.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "board.h"

#include "PrivateLoRa.h"
#include "PrvLoRaMacRadio.h"


/*--------*/
/* define */
#ifdef LORACOMBO_ENABLED
    #include "RadioWrapper.h"
    #define PRVLORA_RADIO_INIT( p_radioEvt )        RadioWrapperInit( RADIOWRAP_LORAMODE_PRIVATELORA, (p_radioEvt) )
    #define PRVLORA_RADIO_SET_LORAMODE()            RadioWrapperSetLoRaMode( RADIOWRAP_LORAMODE_PRIVATELORA );
#else
    #define PRVLORA_RADIO_INIT( p_radioEvt )        Radio.Init( (p_radioEvt) )
    #define PRVLORA_RADIO_SET_LORAMODE()            RADIO_SUCCESS /* nothing to do */
#endif

/*----------------*/
/* typedef (enum) */

/*------------------------*/
/* typedef (struct/union) */
typedef struct _PrvLoRaRadioTimeInfo_t
{
    TimerTime_t     lastTxDoneTime;
    TimerTime_t     lastRxDoneTime;
} PrvLoRaRadioTimeInfo_t;

/*-------------------*/
/* external variable */
extern volatile bool IrqFired;  // radio.c

/*-------------------------*/
/* global variable (const) */

/*-----------------*/
/* global variable */

RadioEvents_t           PrvLoRaRadioCbFuncs;
PrvLoRaRadioEvents_t    PrvLoRaRadioEvents;

/*--------------------*/
/* function prototype */

// Callback from radio
static void PrivateLoRaRadioOnTxDone( void );
static void PrivateLoRaRadioOnTxTimeout( void );
static void PrivateLoRaRadioOnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );
static void PrivateLoRaRadioOnRxError( void );
static void PrivateLoRaRadioOnRxTimeout( void );


//--------------------------------------------------------------------------------------------------
// Init

/*!
 * Initialization
 */
PrvLoRaStatus_t PrivateLoRaRadioInit( void )
{
    PrvLoRaStatus_t     ret;
    RadioResult_t       radioRet;

    // init
    ret = PRVLORA_STATUS_RADIO_ERROR;

    // initialize radio
    PrvLoRaRadioCbFuncs.TxDone    = PrivateLoRaRadioOnTxDone;
    PrvLoRaRadioCbFuncs.RxDone    = PrivateLoRaRadioOnRxDone;
    PrvLoRaRadioCbFuncs.RxError   = PrivateLoRaRadioOnRxError;
    PrvLoRaRadioCbFuncs.TxTimeout = PrivateLoRaRadioOnTxTimeout;
    PrvLoRaRadioCbFuncs.RxTimeout = PrivateLoRaRadioOnRxTimeout;

    radioRet = PRVLORA_RADIO_INIT( &PrvLoRaRadioCbFuncs );
    if( radioRet == RADIO_SUCCESS )
    {
        // Random seed initialization
        srand1( Radio.Random() );

        memset1( (uint8_t *)&PrvLoRaRadioEvents, 0x00, sizeof(PrvLoRaRadioEvents) );
        ret = PRVLORA_STATUS_OK;
    }

    return ret;
}

/*!
 * SetLoRaMode
 */
PrvLoRaStatus_t PrivateLoRaRadioSetLoRaMode( void )
{
    PrvLoRaStatus_t     ret;
    RadioResult_t       radioRet;

    // init
    ret = PRVLORA_STATUS_RADIO_ERROR;

    radioRet = PRVLORA_RADIO_SET_LORAMODE();
    if( radioRet == RADIO_SUCCESS )
    {
        ret = PRVLORA_STATUS_OK;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
// PIB

/*
 * Get PIB
 */
PrvLoRaStatus_t PrivateLoRaRadioGetRequest( PrvLoRaRadioIb_t ibId, PrvLoRaRadioIbReq_t *p_ibGet )
{
    PrvLoRaStatus_t     ret;

    // initial check
    if( p_ibGet == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    ret = PRVLORA_STATUS_OK;

    switch( ibId )
    {
#ifdef RP_USE_RADIO_CFG_CHECK
        case PRVLORA_RADIO_IB_CFG_REGION:
            Radio.GetPib( PIB_RADIO_CFG_REGION, 
                          (uint8_t *)&( p_ibGet->radioCfg ) );
            break;
#endif  // RP_USE_RADIO_CFG_CHECK

        case PRVLORA_RADIO_IB_CFG_CHECK_ENABLE:
            Radio.GetPib( PIB_RADIO_CFG_CHECK_ENABLE, 
                          (uint8_t *)&( p_ibGet->radioCfgCheckEnable ) );
            break;

#ifdef RP_USE_RADIO_CFG_CHECK
        case PRVLORA_RADIO_IB_CFG_FREQ_HOPPING_USED:
            Radio.GetPib( PIB_RADIO_CFG_FREQ_HOPPING_USED, 
                          (uint8_t *)&( p_ibGet->radioCfgFreqHoppingUsed ) );
            break;
#endif  // RP_USE_RADIO_CFG_CHECK

        default:
            ret = PRVLORA_STATUS_SERVICE_UNKNOWN;
            break;
    }

    return ret;
}

/*
 * Set PIB
 */
PrvLoRaStatus_t PrivateLoRaRadioSetRequest( PrvLoRaRadioIb_t ibId, PrvLoRaRadioIbReq_t *p_ibSet )
{
    PrvLoRaStatus_t     ret;

    // initial check
    if( p_ibSet == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    ret = PRVLORA_STATUS_OK;

    switch( ibId )
    {
#ifdef RP_USE_RADIO_CFG_CHECK
        case PRVLORA_RADIO_IB_CFG_REGION:
            Radio.SetPib( PIB_RADIO_CFG_REGION, 
                          (uint8_t *)&( p_ibSet->radioCfg ) );
            break;
#endif  // RP_USE_RADIO_CFG_CHECK

        case PRVLORA_RADIO_IB_CFG_CHECK_ENABLE:
            Radio.SetPib( PIB_RADIO_CFG_CHECK_ENABLE, 
                          (uint8_t *)&( p_ibSet->radioCfgCheckEnable ) );
            break;

#ifdef RP_USE_RADIO_CFG_CHECK
        case PRVLORA_RADIO_IB_CFG_FREQ_HOPPING_USED:
            Radio.SetPib( PIB_RADIO_CFG_FREQ_HOPPING_USED, 
                          (uint8_t *)&( p_ibSet->radioCfgFreqHoppingUsed ) );
            break;
#endif  // RP_USE_RADIO_CFG_CHECK

        default:
            ret = PRVLORA_STATUS_SERVICE_UNKNOWN;
            break;
    }

    return ret;
}


//--------------------------------------------------------------------------------------------------
// Tx

/*!
 * Send frame
 */
PrvLoRaStatus_t PrivateLoRaRadioSendTx( PrvLoRaRadioTxParams_t *p_txParam, 
                                        uint8_t                *p_txPacket, 
                                        uint8_t                txPktSize )
{
    PrvLoRaStatus_t     ret;
    RadioState_t        radioState;
    RadioResult_t       radioRet;
    RadioModems_t       modem;
    uint32_t            bandWidth;

    // initial check
    if( ( p_txParam == NULL ) || ( p_txPacket == NULL ) || ( txPktSize == 0 ) )
    {
        return PRVLORA_STATUS_RADIO_ERROR;
    }

    radioState = Radio.GetStatus();
    if( radioState != RF_IDLE )
    {
        return PRVLORA_STATUS_BUSY;
    }

    // init
    ret = PRVLORA_STATUS_OK;

    // set tx config
    switch( p_txParam->modem )
    {
        case PRVLORA_MODEM_LORA:
            switch( p_txParam->bandWidth )
            {
                case 250000:
                    bandWidth = PRVLORA_BANDWIDTH_LORA250KHZ;
                    break;
                case 500000:
                    bandWidth = PRVLORA_BANDWIDTH_LORA500KHZ;
                    break;
                case 125000:
                default:
                    bandWidth = PRVLORA_BANDWIDTH_LORA125KHZ;
                    break;
            }

            modem = MODEM_LORA;
            Radio.SetTxConfig( modem, 
                               p_txParam->txPower, 
                               0, 
                               bandWidth, 
                               p_txParam->dataRate, 
                               1, 8, false, true, 0, 0, false, 
                               p_txParam->txTimeout );
            break;

        case PRVLORA_MODEM_FSK:
            modem = MODEM_FSK;
            Radio.SetTxConfig( modem, 
                               p_txParam->txPower, 
                               25000, 
                               p_txParam->bandWidth, 
                               (uint32_t)p_txParam->dataRate * 1000UL, 
                               0, 5, false, true, 0, 0, false, 
                               p_txParam->txTimeout );
            break;

        // case PRVLORA_MODEM_NONE:
        default:
            ret = PRVLORA_STATUS_PARAMETER_INVALID;
            break;
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        // Setup the radio frequency
        Radio.SetChannel( p_txParam->frequency );

        // Setup maximum payload length of the radio driver
        Radio.SetMaxPayloadLength( modem, txPktSize );

        // Send
        radioRet = Radio.Send( p_txPacket, txPktSize );
        if( radioRet != RADIO_SUCCESS )
        {
            if( radioRet == RADIO_CHECK_FAIL_TX_CHANNEL_BUSY )
            { 
                // Channel is busy
                ret = PRVLORA_STATUS_RADIO_CHANNEL_BUSY;
            }
            else if( radioRet == RADIO_CHECK_FAIL_TX_DUTY_CYCLE )
            {
                // Duty cycle restriction
                ret = PRVLORA_STATUS_RADIO_DUTYCYCLE_RESTRICTED;
            }
            else
            {
                // RF parameter error: Current RF parameter setting is not supported
                ret = PRVLORA_STATUS_RADIO_PARAMETER_INVALID;
            }
        }
    }

#ifdef DEBUG_PRVLORA
    if( ret == PRVLORA_STATUS_OK )
    {
        PrivateLoRaDebugDispRadioTxParams( (void *)p_txParam );
    }
#endif

    return ret;
}

//--------------------------------------------------------------------------------------------------
// Rx

/*!
 * Start Rx
 */
PrvLoRaStatus_t PrivateLoRaRadioStartRx( PrvLoRaRadioRxParams_t *p_rxParam, bool isContinuous )
{
    PrvLoRaStatus_t     ret;
    RadioState_t        radioState;
    RadioResult_t       radioRet;
    RadioModems_t       modem;
    uint32_t            bandWidth;
    uint32_t            maxRxWindow;

    // initial check
    if( p_rxParam == NULL )
    {
        return PRVLORA_STATUS_RADIO_ERROR;
    }

    radioState = Radio.GetStatus();
    if( radioState != RF_IDLE )
    {
        return PRVLORA_STATUS_BUSY;
    }

    // init
    ret = PRVLORA_STATUS_OK;

    // set rx config
    switch( p_rxParam->modem )
    {
        case PRVLORA_MODEM_LORA:
            switch( p_rxParam->bandWidth )
            {
                case 250000:
                    bandWidth = PRVLORA_BANDWIDTH_LORA250KHZ;
                    break;
                case 500000:
                    bandWidth = PRVLORA_BANDWIDTH_LORA500KHZ;
                    break;
                case 125000:
                default:
                    bandWidth = PRVLORA_BANDWIDTH_LORA125KHZ;
                    break;
            }

            modem = MODEM_LORA;
            Radio.SetRxConfig( modem, 
                               bandWidth,
                               p_rxParam->dataRate, 
                               1, 0, 8, 
                               p_rxParam->windowTimeout, 
                               false, 0, true, 0, 0, false, 
                               isContinuous );
            break;

        case PRVLORA_MODEM_FSK:
            modem = MODEM_FSK;
            Radio.SetRxConfig( modem, 
                               50000, 
                               (uint32_t)p_rxParam->dataRate * 1000UL, 
                               0, 83333, 5, 
                               p_rxParam->windowTimeout, 
                               false, 0, true, 0, 0, false, 
                               isContinuous );
            break;

        // case PRVLORA_MODEM_NONE:
        default:
            ret = PRVLORA_STATUS_PARAMETER_INVALID;
            break;
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        // Setup the radio frequency
        Radio.SetChannel( p_rxParam->frequency );

        // Setup maximum payload length of the radio driver
        Radio.SetMaxPayloadLength( modem, p_rxParam->maxFrameSize );

        // RxON
        if( isContinuous == true )
        {
            maxRxWindow = 0;  // Continuous mode
        }
        else
        {
            maxRxWindow = p_rxParam->maxRxWindow;
        }
        radioRet = Radio.Rx( maxRxWindow );
        if( radioRet != RADIO_SUCCESS )
        {
            if( radioRet == RADIO_CHECK_FAIL_RX_CFG )
            {
                ret = PRVLORA_STATUS_RADIO_PARAMETER_INVALID;
            }
            else
            {
                ret = PRVLORA_STATUS_RADIO_ERROR;
            }
        }
    }

#ifdef DEBUG_PRVLORA
    if( ret == PRVLORA_STATUS_OK )
    {
        PrivateLoRaDebugDispRadioRxParams( (void *)p_rxParam, maxRxWindow );
    }
#endif

    return ret;
}


//--------------------------------------------------------------------------------------------------
// Callback from radio

/*!
 * Radio event callback; TxDone
 */
static void PrivateLoRaRadioOnTxDone( void )
{
    PrvLoRaRadioEventsTxDone_t  *p_txDoneInfo;

    p_txDoneInfo = &( PrvLoRaRadioEvents.eventInfo.txDoneInfo );
    p_txDoneInfo->txDoneTime = TimerGetCurrentTime();

    PrvLoRaRadioEvents.radioEvent.events.TxDone = 1;
}

/*!
 * Radio event callback; TxTimeout
 */
static void PrivateLoRaRadioOnTxTimeout( void )
{
    PrvLoRaRadioEvents.radioEvent.events.TxTimeout = 1;
}

/*!
 * Radio event callback; RxDone
 */
static void PrivateLoRaRadioOnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    PrvLoRaRadioEventsRxDone_t  *p_rxDoneInfo;

    p_rxDoneInfo = &( PrvLoRaRadioEvents.eventInfo.rxDoneInfo );
    p_rxDoneInfo->rxDoneTime = TimerGetCurrentTime();
    p_rxDoneInfo->p_payload  = payload;
    p_rxDoneInfo->size       = size;
    p_rxDoneInfo->rssi       = rssi;
    p_rxDoneInfo->snr        = snr;

    PrvLoRaRadioEvents.radioEvent.events.RxDone = 1;
}

/*!
 * Radio event callback; RxError
 */
static void PrivateLoRaRadioOnRxError( void )
{
    PrvLoRaRadioEvents.radioEvent.events.RxError = 1;
}

/*!
 * Radio event callback; RxTimeout
 */
static void PrivateLoRaRadioOnRxTimeout( void )
{
    PrvLoRaRadioEvents.radioEvent.events.RxTimeout = 1;
}

//--------------------------------------------------------------------------------------------------
// Radio event

/*!
 * Check radio IRQ
 */
void PrivateLoRaRadioIrqProcess( void )
{
    // Check radio event
    if( Radio.IrqProcess != NULL )
    {
        Radio.IrqProcess();
    }
}

/*!
 * Get radio event (and clear)
 */
void PrivateLoRaRadioGetEvents( PrvLoRaRadioEvents_t *p_radioEvt )
{
    PrvLoRaRadioEvents_t    radioEvt;

    CRITICAL_SECTION_BEGIN();
    memcpy1( (uint8_t *)&radioEvt, (const uint8_t *)&PrvLoRaRadioEvents, sizeof(PrvLoRaRadioEvents_t) );
    memset1( (uint8_t *)&PrvLoRaRadioEvents, 0x00, sizeof(PrvLoRaRadioEvents) );
    CRITICAL_SECTION_END();

    if( p_radioEvt != NULL )
    {
        memcpy1( (uint8_t *)p_radioEvt, (const uint8_t *)&radioEvt, sizeof(PrvLoRaRadioEvents_t) );
    }
}

/*!
 * Check radio event (and clear)
 */
bool PrivateLoRaRadioIsReadyProcess( void )
{
    bool    ret;

    // init
    ret = IrqFired;

    if( PrvLoRaRadioEvents.radioEvent.evtValue != 0 )
    {
        ret = true;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
// Sleep / Wakeup

/*!
 * Sleep (cold)
 */
void PrivateLoRaRadioSleepCold( void )
{
    RadioState_t    radioState;

    radioState = Radio.GetStatus();

    if( radioState != RF_COLD_SLEEP )
    {
        if( radioState == RF_WARM_SLEEP )
        {
            Radio.WakeUp();
        }
        Radio.Standby();
        Radio.SleepCold();
    }
}

/*!
 * Sleep (warm)
 */
void PrivateLoRaRadioSleepWarm( void )
{
    RadioState_t    radioState;

    radioState = Radio.GetStatus();

    if( radioState != RF_WARM_SLEEP )
    {
        if( radioState == RF_COLD_SLEEP )
        {
            Radio.WakeUp();
        }
        Radio.Standby();
        Radio.SleepWarm();
    }
}

/*!
 *  Wake up (standby)
 */
void PrivateLoRaRadioWakeup( void )
{
    RadioState_t    radioState;

    radioState = Radio.GetStatus();

    if( radioState != RF_IDLE )
    {
        if( ( radioState == RF_COLD_SLEEP ) ||
            ( radioState == RF_WARM_SLEEP ) )
        {
            Radio.WakeUp();
        }
        Radio.Standby();
        Radio.SetPublicNetwork( false );
    }
}
