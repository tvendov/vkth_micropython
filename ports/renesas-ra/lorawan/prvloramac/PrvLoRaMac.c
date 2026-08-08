/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMac.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "board.h"

#include "radio.h"

#include "PrivateLoRa.h"
#include "PrvLoRaMacCrypto.h"
#include "PrvLoRaMacRemoteDev.h"
#include "PrvLoRaMacFrame.h"
#include "PrvLoRaMacIndirectTx.h"
#include "PrvLoRaMacRadio.h"
#include "PrvLoRaMacRegion.h"
#ifdef DEBUG_PRVLORA
#include "PrvLoRaMacDebug.h"
#endif

/*--------*/
/* define */
#define PRVLORA_MAC_TXPARAM_BUFFSIZE            256

#define PRVLORA_MAC_WAITTIME_TX2RX              1000
#define PRVLORA_MAC_WAITTIME_RX2TXRES           1000

#define PRVLORA_MAC_MIN_RXSYMBOL                6

#ifdef RP_USE_RADIO_CFG_CHECK
    #define PRVLORA_DEFAULT_RADIO_CFG_CHECK     true
#else
    #define PRVLORA_DEFAULT_RADIO_CFG_CHECK     false
#endif

/*----------------*/
/* typedef (enum) */
// Internal State of PrivateLoRa
typedef enum _PrvLoRaMacState_t
{
    PRVLORA_MAC_STATE_NONE = 0,         // initial state
    PRVLORA_MAC_STATE_STOPPED,
    _PRVLORA_MAC_STATE_INACTIVE,        // not set it to state (indicate that above state is "inactive")
    PRVLORA_MAC_STATE_IDLE,
    PRVLORA_MAC_STATE_CONTINUOUS_RX,
    _PRVLORA_MAC_STATE_BUSY,            // not set it to state (indicate that following state is "active")
    PRVLORA_MAC_STATE_TX_RUNNING,
    PRVLORA_MAC_STATE_TX_FAILED,
    PRVLORA_MAC_STATE_RX_WAITING,
    PRVLORA_MAC_STATE_RX_RUNNING,
    PRVLORA_MAC_STATE_RX_RCVD,
    PRVLORA_MAC_STATE_RX_FAILED,
    PRVLORA_MAC_STATE_TX_WAITING,
    PRVLORA_MAC_STATE_MLME_CFM_ONLY,
    PRVLORA_MAC_STATE_FAULT,
} PrvLoRaMacState_t;

// Tx result
typedef enum _PrvLoRaMacTxResult_t
{
    PRVLORA_MAC_TX_RESULT_NONE = 0,
    PRVLORA_MAC_TX_RESULT_PENDING,
    PRVLORA_MAC_TX_RESULT_EXEC,
    PRVLORA_MAC_TX_RESULT_OK,
    PRVLORA_MAC_TX_RESULT_TIMEOUT,
    PRVLORA_MAC_TX_RESULT_NOACK,
    PRVLORA_MAC_TX_RESULT_CHANNELBUSY,
    PRVLORA_MAC_TX_RESULT_DUTYCYCLE_RESTRICTED,
    PRVLORA_MAC_TX_RESULT_RADIO_ERROR,
    PRVLORA_MAC_TX_RESULT_MLMECFM_ONLY,  // not sent but want to confirm
} PrvLoRaMacTxResult_t;

/*------------------------*/
/* typedef (struct/union) */

// IB for PrivateLoRa
typedef struct _PrvLoRaIbParams_t
{
    uint8_t                     macAddr[ PRVLORA_MACADDR_SIZE ];
    uint8_t                     channelId;
    uint8_t                     drIndex;
    int8_t                      txPower;
    bool                        rxOnWhenIdle;
    float                       maxEirp;
    uint32_t                    systemMaxRxError;
    bool                        keyReqPermit;
    PrvLoRaIbReqTxCycle_t       txCycle;
    bool                        radioCfgCheckEnable;
} PrvLoRaIbParams_t;

// Tx frame information
typedef struct _PrvLoRaMacTxCfm_t
{
    bool                    isEnableCfm;
    union
    {
        PrvLoRaMcpsCfm_t        mcpsCfm;
        PrvLoRaMlmeCfm_t        mlmeCfm;
    } cfm;
} PrvLoRaMacTxCfm_t;
typedef struct PrvLoRaTxInfo_t
{
    PrvLoRaMacTxResult_t    txStatus;
    PrvLoRaRadioTxParams_t  txParams;
    uint8_t                 txDstAddr[ PRVLORA_MACADDR_SIZE ];
    PrvLoRaFrame_t          txPacket;
    PrvLoRaMacTxCfm_t       txConfirm;
} PrvLoRaTxInfo_t;

// Rx frame information
typedef union _PrvLoRaMacRxInd_t
{
    PrvLoRaMcpsInd_t        mcpsInd;
    PrvLoRaMlmeInd_t        mlmeInd;
} PrvLoRaMacRxInd_t;
typedef struct _PrvLoRaRxInfo_t
{
    PrvLoRaRadioRxParams_t      rxParams;
    PrvLoRaRadioEventsRxDone_t *p_rxDoneInfo;
    // rx frame
    uint8_t                     *p_rxFrame;
    uint8_t                     rxFrameSize;
    // parsed
    PrvLoRaFrameMhdrFrmCtrl_t   rxFrameCtrl;
    uint8_t                     rxSrcAddr[ PRVLORA_MACADDR_SIZE ];
    uint8_t                     rxDstAddr[ PRVLORA_MACADDR_SIZE ];
    uint8_t                     *p_rxPayload;  // reuse radio rx buffer
    uint8_t                     rxPayloadSize;
    uint8_t                     *p_rxCmdPayload;  // PrvLoRaPayloadCmd_t
    PrvLoRaMacRxInd_t           rxIndication;
} PrvLoRaRxInfo_t;

// timer information
typedef union _PrvLoRaTimerEvent_t
{
    uint8_t         evtValue;
    struct
    {
        uint8_t     rxWinStart : 1;
        uint8_t     txRspStart : 1;
        uint8_t     _reserved  : 7;
    } events;
} PrvLoRaTimerEvent_t;

typedef struct _PrvLoRaTimerInfo_t
{
    PrvLoRaTimerEvent_t     timerEvent;
    TimerEvent_t            timerRxWinStart;
    TimerEvent_t            timerTxRspStart;
} PrvLoRaTimerInfo_t;

// Manage private lora
typedef struct _PrvLoRaMacManage_t
{
    PrvLoRaMacState_t       state;
    PrvLoRaPrimitives_t     primitives;
    PrvLoRaIbParams_t       ibParams;
    PrvLoRaRegion_t         region;
    PrvLoRaTxInfo_t         txInfo;
    PrvLoRaRxInfo_t         rxInfo;
    PrvLoRaTimerInfo_t      timerInfo;
} PrvLoRaMacManage_t;

/*-------------------------*/
/* global variable (const) */
const PrvLoRaIbParams_t     PrvLoRaDefaultIb = 
{
    .macAddr             = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    .channelId           = 0x00,  /* (depend on region) */
    .drIndex             = 0,     /* (depend on region) */
    .txPower             = 0,     /* (depend on region) */
    .rxOnWhenIdle        = false,
    .maxEirp             = (float)16.0,
    .systemMaxRxError    = (uint32_t)PRVLORA_CONFIG_SYSTEM_MAX_RX_ERROR,
    .keyReqPermit        = false,
    .txCycle             = { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 0 },
    .radioCfgCheckEnable = PRVLORA_DEFAULT_RADIO_CFG_CHECK,
};

/*-----------------*/
/* global variable */
PrvLoRaMacManage_t   PrvLoRaMacMng = { .state = PRVLORA_MAC_STATE_NONE };

/*--------------------*/
/* function prototype */

// Sub procedure (Tx/Rx parameters)
static PrvLoRaStatus_t PrivateLoRaMacPrepareTxParams( void );
static PrvLoRaStatus_t PrivateLoRaMacPrepareRxParams( void );

// Sub procedure (MLME-Request)
static PrvLoRaStatus_t PrivateLoRaMacMlmeKeyReq( PrvLoRaMlmeReq_t *p_mlmeReq );
static PrvLoRaStatus_t PrivateLoRaMacMlmeDevInfoReq( PrvLoRaMlmeReq_t *p_mlmeReq );
static PrvLoRaStatus_t PrivateLoRaMacMlmeTxCycleReq( PrvLoRaMlmeReq_t *p_mlmeReq );

// Sub procedure (Tx)
static PrvLoRaStatus_t PrivateLoRaMacTxFramePrepare( uint8_t            frameType,
                                                     uint8_t            *p_dstAddr,
                                                     uint8_t            *p_srcAddr,
                                                     uint8_t            *p_txPayload, 
                                                     uint8_t            txPayloadSize,
                                                     bool               isAck,
                                                     PrvLoRaTxOptions_t txOptions );
static PrvLoRaStatus_t PrivateLoRaMacTxFrameSend( void );
static void PrivateLoRaMacTxFrameConfirm( void );

// Sub procedure (TxRes)
static PrvLoRaStatus_t PrivateLoRaMacResponseFramePrepare( void );
static PrvLoRaStatus_t PrivateLoRaMacKeyResFramePrepare( void );
static PrvLoRaStatus_t PrivateLoRaMacDevInfoResFramePrepare( void );
static PrvLoRaStatus_t PrivateLoRaMacTxCycleResFramePrepare( void );

// Sub procedure (Rx)
static PrvLoRaStatus_t PrivateLoRaMacRxCommand( void );
static PrvLoRaStatus_t PrivateLoRaMacRxCommandKeyReq( void );
static PrvLoRaStatus_t PrivateLoRaMacRxCommandKeyRes( void );
static PrvLoRaStatus_t PrivateLoRaMacRxCommandDevInfoReq( void );
static PrvLoRaStatus_t PrivateLoRaMacRxCommandDevInfoRes( void );
static PrvLoRaStatus_t PrivateLoRaMacRxCommandTxCycleReq( void );
static PrvLoRaStatus_t PrivateLoRaMacRxCommandTxCycleRes( void );

// Sub procedure (MLME-Indication)
static PrvLoRaStatus_t PrivateLoRaMacMlmeIndication( void );
static PrvLoRaStatus_t PrivateLoRaMacMlmeKeyInd( void );
static PrvLoRaStatus_t PrivateLoRaMacMlmeTxCycleInd( void );

// Handle/Process radio event
static void PrivateLoRaMacHandleRadioEvents( void );
static void PrivateLoRaMacProcessRadioTxDone( PrvLoRaRadioEventsTxDone_t *p_txDoneInfo );
static void PrivateLoRaMacProcessRadioTxTimeout( void );
static void PrivateLoRaMacProcessRadioRxDone( PrvLoRaRadioEventsRxDone_t *p_rxDoneInfo );
static void PrivateLoRaMacProcessRadioRxErrorTimeout( bool isRxError );

// Handle/Process timer event
static void PrivateLoRaMacTimerCbRxWinStart( void );
static void PrivateLoRaMacTimerCbTxResponseStart( void );
static void PrivateLoRaMacHandleTimerEvents( void );
static void PrivateLoRaMacTimerProcessRxWinStart( void );
static void PrivateLoRaMacTimerProcessTxResponseStart( void );

// Handle/Process private LoRa state
static void PrivateLoRaMacStateProcess( void );
static PrvLoRaStatus_t PrivateLoRaMacStateToIdle( void );

// Notify to upper
static void PrivateLoRaMacNotifyToUpper( void );

//
static int32_t PrivateLoRaMacGetWaitTimeTx2Rx( void );
static int32_t PrivateLoRaMacGetWaitTimeRx2TxRes( void );

//--------------------------------------------------------------------------------------------------

/*!
 * Private LoRa initialization
 */
PrvLoRaStatus_t PrivateLoRaInitialization( PrvLoRaPrimitives_t *p_primitives,
                                           PrvLoRaRegion_t     region )
{
    PrvLoRaStatus_t                 ret;
    PrvLoRaIbParams_t               *p_ibParams;
    PrvLoRaRegionDefaultParams_t    defaultParams;
    PrvLoRaRadioTxParams_t          *p_txParams;
    PrvLoRaRadioRxParams_t          *p_rxParams;
    PrvLoRaTimerInfo_t              *p_timerInfo;

    // initial check (arg)
    if( p_primitives == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }
    if( ( p_primitives->PrvLoRaMacMcpsConfirm    == NULL ) ||
        ( p_primitives->PrvLoRaMacMcpsIndication == NULL ) ||
        ( p_primitives->PrvLoRaMacMlmeConfirm    == NULL ) ||
        ( p_primitives->PrvLoRaMacMlmeIndication == NULL ) ||
        ( p_primitives->PrvLoRaMacNotification   == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }
    if( (uint8_t)region >= (uint8_t)MAXNUM_PRVLORA_REGION )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }
    // initial check (state)
    if( PrvLoRaMacMng.state >= _PRVLORA_MAC_STATE_BUSY )
    {
        return PRVLORA_STATUS_BUSY;
    }

    // init
    ret = PRVLORA_STATUS_OK;
    memset1( (uint8_t *)&PrvLoRaMacMng, 0x00, sizeof(PrvLoRaMacManage_t) );

    // initialize RF
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaRadioInit();
    }

    // initialize region
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaRegionInit( region, &defaultParams );
    }

    // initialize crypto
    if( ret == PRVLORA_STATUS_OK )
    {
        PrivateLoRaCryptoInit();
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        // initialize remote device manager
        PrivateLoRaRemoteDevInit();

        // Mng
        PrvLoRaMacMng.region = region;
        memcpy1( (uint8_t *)&( PrvLoRaMacMng.primitives ), 
                 (const uint8_t *)p_primitives, 
                 sizeof(PrvLoRaPrimitives_t) );

        // IB
        p_ibParams = &( PrvLoRaMacMng.ibParams );
        memcpy1( (uint8_t *)p_ibParams, 
                 (const uint8_t *)&PrvLoRaDefaultIb, 
                 sizeof(PrvLoRaIbParams_t) );
        p_ibParams->channelId = defaultParams.channelId;
        p_ibParams->drIndex   = defaultParams.drIndex;
        p_ibParams->txPower   = defaultParams.txPower;

        // Tx
        p_txParams          = &( PrvLoRaMacMng.txInfo.txParams );
        p_txParams->txPower = p_ibParams->txPower;
        PrivateLoRaRegionGetDataRate( p_ibParams->drIndex, 
                                      &( p_txParams->modem ), 
                                      &( p_txParams->dataRate ), 
                                      &( p_txParams->bandWidth ) );
        PrivateLoRaRegionGetFrequency( p_ibParams->channelId, 
                                       p_ibParams->drIndex, 
                                       &( p_txParams->frequency ) );
        PrivateLoRaRegionGetMaxFrameSize( p_ibParams->drIndex, 
                                          &( p_txParams->maxFrameSize ), 
                                          &( p_txParams->txTimeout ) );

        // Rx
        p_rxParams            = &( PrvLoRaMacMng.rxInfo.rxParams );
        p_rxParams->modem     = p_txParams->modem;
        p_rxParams->frequency = p_txParams->frequency;
        p_rxParams->dataRate  = p_txParams->dataRate;
        p_rxParams->bandWidth = p_txParams->bandWidth;
        PrivateLoRaRegionGetMaxRxWindow( &( p_rxParams->maxRxWindow ) );
        PrivateLoRaRegionGetRxWindowParams( p_ibParams->drIndex,
                                            PRVLORA_MAC_MIN_RXSYMBOL,
                                            p_ibParams->systemMaxRxError,
                                            &( p_rxParams->windowTimeout ),
                                            &( p_rxParams->windowOffset ) );
        PrivateLoRaRegionGetMaxFrameSize( p_ibParams->drIndex, &( p_rxParams->maxFrameSize ), NULL );

        // Timer
        p_timerInfo = &( PrvLoRaMacMng.timerInfo );
        p_timerInfo->timerEvent.evtValue = 0;
        TimerInit( &( p_timerInfo->timerRxWinStart ), PrivateLoRaMacTimerCbRxWinStart );
        TimerInit( &( p_timerInfo->timerTxRspStart ), PrivateLoRaMacTimerCbTxResponseStart );

#ifdef DEBUG_PRVLORA
        // debug mode
        PrivateLoRaDebugInit();
#endif

        // set radio to sleep
        PrivateLoRaRadioSleepCold();

        // go to next state
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_STOPPED;
    }

    return ret;
}

/*!
 * Start private LoRa
 */
PrvLoRaStatus_t PrivateLoRaStart( void )
{
    PrvLoRaStatus_t     ret;
    PrvLoRaIbParams_t   *p_ibParams;

    // init
    ret        = PRVLORA_STATUS_ERROR;
    p_ibParams = &( PrvLoRaMacMng.ibParams );

    // Start private LoRa
    if( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_STOPPED )
    {
        // set private LoRa to RadioWrapper (if used)
        PrivateLoRaRadioSetLoRaMode();

        // set radio config
        PrivateLoRaRegionSetRadioCfg( p_ibParams->radioCfgCheckEnable );

        // update state
        ret = PrivateLoRaMacStateToIdle();
        if( ret != PRVLORA_STATUS_OK )
        {
            // cannot start. cannot set state to idle.
            PrvLoRaMacMng.state = PRVLORA_MAC_STATE_STOPPED;
        }

#ifdef DEBUG_PRVLORA
        if( ret == PRVLORA_STATUS_OK )
        {
            // debug mode On
            PrivateLoRaDebugSetOnOff( PRVLORA_DEBUGSWITCH_ON );
        }
#endif
    }

    return ret;
}

/*!
 * Stop private LoRa
 */
PrvLoRaStatus_t PrivateLoRaStop( void )
{
    PrvLoRaStatus_t     ret;

    // init
    ret = PRVLORA_STATUS_BUSY;

    switch( PrvLoRaMacMng.state )
    {
        case PRVLORA_MAC_STATE_NONE:
            // error: not initialized
            ret = PRVLORA_STATUS_ERROR;
            break;

        case PRVLORA_MAC_STATE_CONTINUOUS_RX:
            // set radio to sleep
            PrivateLoRaRadioSleepCold();

            /* no break */

        case PRVLORA_MAC_STATE_IDLE:
        case PRVLORA_MAC_STATE_STOPPED:
            // go to next state
            PrvLoRaMacMng.state = PRVLORA_MAC_STATE_STOPPED;
            ret = PRVLORA_STATUS_OK;

#ifdef DEBUG_PRVLORA
            // debug mode Off
            PrivateLoRaDebugSetOnOff( PRVLORA_DEBUGSWITCH_OFF );
#endif
            break;

        // case PRVLORA_MAC_STATE_TX_RUNNING:
        // case PRVLORA_MAC_STATE_TX_FAILED:
        // case PRVLORA_MAC_STATE_RX_WAITING:
        // case PRVLORA_MAC_STATE_RX_RCVD:
        // case PRVLORA_MAC_STATE_RX_FAILED:
        // case PRVLORA_MAC_STATE_FAULT:
        default:
            break;
    }

    return ret;
}

/*!
 * Process private LoRa
 */
void PrivateLoRaProcess( void )
{
    // initial check (state)
    if( PrvLoRaMacMng.state <= _PRVLORA_MAC_STATE_INACTIVE )
    {
        return;  // private lora is not running
    }

    // Check radio event
    PrivateLoRaRadioIrqProcess();

    // Process radio event
    PrivateLoRaMacHandleRadioEvents();

    // Process timer event
    PrivateLoRaMacHandleTimerEvents();

    // Process for private LoRa state
    PrivateLoRaMacStateProcess();

    // notify to upper
    PrivateLoRaMacNotifyToUpper();
}

/*!
 * Get IB request from application
 */
PrvLoRaStatus_t PrivateLoRaGetRequest( PrvLoRaIb_t ibId, PrvLoRaIbRequest_t *p_ibGet )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIbParams_t       *p_ibParams;
    PrvLoRaRadioIbReq_t     radioIbGet;

    // initial check (arg)
    if( p_ibGet == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }
    // initial check (state)
    if( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_NONE )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // init
    ret        = PRVLORA_STATUS_OK;
    p_ibParams = &( PrvLoRaMacMng.ibParams );

    // get IB
    switch( ibId )
    {
        case PRVLORA_IB_MACADDR:
            memcpy1( (uint8_t *)p_ibGet->macAddr, 
                     (const uint8_t *)p_ibParams->macAddr, 
                     sizeof(p_ibGet->macAddr) );
            break;

        case PRVLORA_IB_CHANNEL_ID:
            p_ibGet->channelId = p_ibParams->channelId;
            break;

        case PRVLORA_IB_DR:
            p_ibGet->drIndex = p_ibParams->drIndex;
            break;

        case PRVLORA_IB_TXPOWER:
            p_ibGet->txPower = p_ibParams->txPower;
            break;

        case PRVLORA_IB_RXONWHENIDLE:
            p_ibGet->rxOnWhenIdle = p_ibParams->rxOnWhenIdle;
            break;

        case PRVLORA_IB_KEYREQ_PERMISSION:
            p_ibGet->keyReqPermit = p_ibParams->keyReqPermit;
            break;

        case PRVLORA_IB_TXCYCLE_TIME:
            memcpy1( (uint8_t *)&( p_ibGet->txCycle ), 
                     (uint8_t *)&( p_ibParams->txCycle ), 
                     sizeof(PrvLoRaIbReqTxCycle_t) );
            break;

        case PRVLORA_IB_RADIO_CFG_CHECK_ENABLE:
            ret = PrivateLoRaRadioGetRequest( PRVLORA_RADIO_IB_CFG_CHECK_ENABLE, &radioIbGet );
            if( ret == PRVLORA_STATUS_OK )
            {
                // fail-safe
                if( p_ibParams->radioCfgCheckEnable != radioIbGet.radioCfgCheckEnable )
                {
                    p_ibParams->radioCfgCheckEnable = radioIbGet.radioCfgCheckEnable;
                }

                p_ibGet->radioCfgCheckEnable = p_ibParams->radioCfgCheckEnable;
            }
            break;

        default:
            ret = PRVLORA_STATUS_SERVICE_UNKNOWN;
            break;
    }

    return ret;
}

/*!
 * Set IB request from application
 */
PrvLoRaStatus_t PrivateLoRaSetRequest( PrvLoRaIb_t ibId, PrvLoRaIbRequest_t *p_ibSet )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIbParams_t       *p_ibParams;
    PrvLoRaRadioIbReq_t     radioIbSet;

    // initial check (arg)
    if( p_ibSet == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }
    // initial check (state)
    if( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_NONE )
    {
        return PRVLORA_STATUS_ERROR;
    }
    if( PrvLoRaMacMng.state >= _PRVLORA_MAC_STATE_BUSY )
    {
        // allow only TxCycleTime
        if( ibId != PRVLORA_IB_TXCYCLE_TIME )
        {
            return PRVLORA_STATUS_BUSY;
        }
    }

    // init
    ret        = PRVLORA_STATUS_OK;
    p_ibParams = &( PrvLoRaMacMng.ibParams );

    // set IB
    switch( ibId )
    {
        case PRVLORA_IB_MACADDR:
            memcpy1( (uint8_t *)p_ibParams->macAddr, 
                     (const uint8_t *)p_ibSet->macAddr, 
                     sizeof(p_ibParams->macAddr) );
            break;

        case PRVLORA_IB_CHANNEL_ID:
            if( p_ibParams->rxOnWhenIdle == false )
            {
                p_ibParams->channelId = p_ibSet->channelId;
            }
            else
            {
                ret = PRVLORA_STATUS_BUSY;
            }
            break;

        case PRVLORA_IB_DR:
            if( p_ibParams->rxOnWhenIdle == false )
            {
                p_ibParams->drIndex = p_ibSet->drIndex;
            }
            else
            {
                ret = PRVLORA_STATUS_BUSY;
            }
            break;

        case PRVLORA_IB_TXPOWER:
            p_ibParams->txPower = p_ibSet->txPower;
            break;

        case PRVLORA_IB_RXONWHENIDLE:
            if( p_ibSet->rxOnWhenIdle == true )
            {
                ret = PrivateLoRaMacPrepareRxParams();
                if( ret == PRVLORA_STATUS_OK )
                {
                    ret = PrivateLoRaMacPrepareTxParams();  // for response tx
                }
            }

            if( ret == PRVLORA_STATUS_OK )
            {
                p_ibParams->rxOnWhenIdle = p_ibSet->rxOnWhenIdle;
                if( PrvLoRaMacMng.state > _PRVLORA_MAC_STATE_INACTIVE )
                {
                    ret = PrivateLoRaMacStateToIdle();
                    if( ret != PRVLORA_STATUS_OK )
                    {
                        // an error occurs only if rxOnWhenIdle is true
                        p_ibParams->rxOnWhenIdle = false;
                        PrivateLoRaMacStateToIdle();
                    }
                }
            }
            break;

        case PRVLORA_IB_KEYREQ_PERMISSION:
            p_ibParams->keyReqPermit = p_ibSet->keyReqPermit;
            break;

        case PRVLORA_IB_TXCYCLE_TIME:
            memcpy1( (uint8_t *)&( p_ibParams->txCycle ), 
                    (uint8_t *)&( p_ibSet->txCycle ), 
                    sizeof(PrvLoRaIbReqTxCycle_t) );
            break;

        case PRVLORA_IB_RADIO_CFG_CHECK_ENABLE:
            radioIbSet.radioCfgCheckEnable = p_ibSet->radioCfgCheckEnable;
            ret = PrivateLoRaRadioSetRequest( PRVLORA_RADIO_IB_CFG_CHECK_ENABLE, &radioIbSet );
            if( ret == PRVLORA_STATUS_OK )
            {
                p_ibParams->radioCfgCheckEnable = p_ibSet->radioCfgCheckEnable;
            }
            break;

        default:
            ret = PRVLORA_STATUS_SERVICE_UNKNOWN;
            break;
    }

    return ret;
}

/*!
 * Register the set of remote device and security key
 */
PrvLoRaStatus_t PrivateLoRaRegisterRemoteDevice( uint8_t  *p_remoteMacAddr,
                                                 uint8_t  *p_psk,
                                                 uint8_t  *p_sessionKey,
                                                 uint32_t initFrameCounterTx,
                                                 uint32_t initFrameCounterRx )
{
    PrvLoRaStatus_t     ret;

    // initial check (arg)
    if( p_remoteMacAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }
    // initial check (state)
    if( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_NONE )
    {
        return PRVLORA_STATUS_ERROR;
    }
    if( PrvLoRaMacMng.state >= _PRVLORA_MAC_STATE_BUSY )
    {
        return PRVLORA_STATUS_BUSY;
    }

    // regiter the remote device info
    ret = PrivateLoRaRemoteDevRegister( p_remoteMacAddr, 
                                        p_psk,
                                        p_sessionKey,
                                        initFrameCounterTx,
                                        initFrameCounterRx );

    // register device to indirect tx
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaIndTxRegisterRemoteDevice( p_remoteMacAddr );
        if( ret != PRVLORA_STATUS_OK )
        {
            PrivateLoRaRemoteDevUnregister( p_remoteMacAddr );
        }
    }

    return ret;
}

/*!
 * Unregister the set of remote device and security key
 * - all information of remote devices are cleared if p_remoteMacAddr is NULL.
 */
PrvLoRaStatus_t PrivateLoRaUnregisterRemoteDevice( uint8_t *p_remoteMacAddr )
{
    // initial check (state)
    if( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_NONE )
    {
        return PRVLORA_STATUS_ERROR;
    }
    if( PrvLoRaMacMng.state >= _PRVLORA_MAC_STATE_BUSY )
    {
        return PRVLORA_STATUS_BUSY;
    }

    if( p_remoteMacAddr != NULL )
    {
        // unregister the specific entry
        PrivateLoRaRemoteDevUnregister( p_remoteMacAddr );
        PrivateLoRaIndTxUnregisterRemoteDevice( p_remoteMacAddr );
    }
    else
    {
        // unregister all entries
        PrivateLoRaRemoteDevUnregisterAll();
        PrivateLoRaIndTxInit();
    }

    return PRVLORA_STATUS_OK;
}

/*!
 * Send data
 */
PrvLoRaStatus_t PrivateLoRaMcpsRequest( PrvLoRaMcpsReq_t *p_mcpsReq )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIbParams_t       *p_ibParams;
    PrvLoRaRadioTxParams_t  *p_txParams;
    PrvLoRaTxInfo_t         *p_txInfo;
    PrvLoRaRxInfo_t         *p_rxInfo;
    PrvLoRaMcpsCfm_t        *p_mcpsCfm;
    int                     compare;
    uint8_t                 frameSize;
    bool                    isAck;

    // (init for initial check)
    p_txInfo = &( PrvLoRaMacMng.txInfo );

    // initial check (arg)
    if( p_mcpsReq == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }
    // initial check (state)
    if( PrvLoRaMacMng.state <= _PRVLORA_MAC_STATE_INACTIVE )
    {
        return PRVLORA_STATUS_INACTIVE;
    }
    if( PrvLoRaMacMng.state >= _PRVLORA_MAC_STATE_BUSY )
    {
        // PRVLORA_MAC_STATE_RX_RCVD; 
        //   allow this api to be called in mcps confirm or indication callback.
        if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
        {
            return PRVLORA_STATUS_BUSY;
        }
    }
    // initial check (another tx requst)
    if( p_txInfo->txStatus != PRVLORA_MAC_TX_RESULT_NONE )
    {
        return PRVLORA_STATUS_BUSY;
    }

    //----------

    // init
    p_ibParams = &( PrvLoRaMacMng.ibParams );
    p_txParams = &( p_txInfo->txParams );
    p_txInfo   = &( PrvLoRaMacMng.txInfo );
    p_rxInfo   = &( PrvLoRaMacMng.rxInfo );
    isAck      = false;

    // prepare tx/rx parameters
    ret = PrivateLoRaMacPrepareTxParams();
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaMacPrepareRxParams();
    }

    // check destination address
    if( ret == PRVLORA_STATUS_OK )
    {
        if( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_RX_RCVD )
        {
            // PRVLORA_MAC_STATE_RX_RCVD; allow sending only as response
            compare = memcmp( p_mcpsReq->dstMacAddr, p_rxInfo->rxSrcAddr, PRVLORA_MACADDR_SIZE );
            if( compare != 0 )
            {
                ret = PRVLORA_STATUS_BUSY;
            }
        }
        else
        {
            ret = PrivateLoRaRemoteDevSearchDevice( p_mcpsReq->dstMacAddr );
        }
    }

    // check frame size
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaFrameGetTxFrameSize( &frameSize, p_mcpsReq->txDataSize, p_mcpsReq->txOptions );
        if( ret == PRVLORA_STATUS_OK )
        {
            if( frameSize > p_txParams->maxFrameSize )
            {
                ret = PRVLORA_STATUS_PARAMETER_INVALID;
            }
        }
    }

    // in case direct tx
    if( p_mcpsReq->txOptions.options.IndirectTx == 0 )
    {
        // make frame & length check
        if( ret == PRVLORA_STATUS_OK )
        {
            if( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_RX_RCVD )
            {
                // PRVLORA_MAC_STATE_RX_RCVD; set ack bit if sender requests the ACK
                if( p_rxInfo->rxFrameCtrl.ackRequest == 1 )
                {
                    isAck = true;
                }
            }

            p_mcpsReq->txOptions.options._reserved = 0;
            ret = PrivateLoRaMacTxFramePrepare( PRVLORA_FRAME_TYPE_DATA,
                                                p_mcpsReq->dstMacAddr,
                                                p_ibParams->macAddr,
                                                p_mcpsReq->p_txData,
                                                p_mcpsReq->txDataSize,
                                                isAck,
                                                p_mcpsReq->txOptions );
            if( ret == PRVLORA_STATUS_OK )
            {
                // set txHandle to McpsCfm at here
                p_mcpsCfm = &( p_txInfo->txConfirm.cfm.mcpsCfm );
                p_mcpsCfm->txHandle = p_mcpsReq->txHandle;
            }
        }

        // Send frame (in idle state only)
        if( ret == PRVLORA_STATUS_OK )
        {
            if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
            {
                ret = PrivateLoRaMacTxFrameSend();
            }
            else
            {
                // PRVLORA_MAC_STATE_RX_RCVD;
                //   cancel tx request if the response waiting timer has expired.
                if( PrvLoRaMacMng.timerInfo.timerEvent.events.txRspStart == 1 )
                {
                    ret = PRVLORA_STATUS_BUSY;
                }
            }
        }

        // go to next state
        if( ret == PRVLORA_STATUS_OK )
        {
            if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
            {
                PrvLoRaMacMng.state = PRVLORA_MAC_STATE_TX_RUNNING;
            }
        }
        else
        {
            p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_NONE;
            if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
            {
                PrivateLoRaMacStateToIdle();
            }
        }

    }
    // in case indirect tx
    else  // if( p_mcpsReq->txOptions.options.IndirectTx == 1 )
    {
        if( ret == PRVLORA_STATUS_OK )
        {
            ret = PrivateLoRaIndTxEnqueueMcpsReq( p_mcpsReq );
        }
    }

    return ret;
}

/*!
 * MLME request
 */
PrvLoRaStatus_t PrivateLoRaMlmeRequest( PrvLoRaMlmeReq_t *p_mlmeReq )
{
    PrvLoRaStatus_t     ret;

    // initial check (arg)
    if( p_mlmeReq == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }
    // initial check (state)
    if( PrvLoRaMacMng.state <= _PRVLORA_MAC_STATE_INACTIVE )
    {
        return PRVLORA_STATUS_INACTIVE;
    }

    // prepare tx/rx parameters
    ret = PrivateLoRaMacPrepareTxParams();
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaMacPrepareRxParams();
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        switch( p_mlmeReq->mlmeType )
        {
            case PRVLORA_MLME_KEY:
                ret = PrivateLoRaMacMlmeKeyReq( p_mlmeReq );
                break;

            case PRVLORA_MLME_DEVINFO:
                ret = PrivateLoRaMacMlmeDevInfoReq( p_mlmeReq );
                break;

            case PRVLORA_MLME_TXCYCLE:
                ret = PrivateLoRaMacMlmeTxCycleReq( p_mlmeReq );
                break;

            default:
                ret = PRVLORA_STATUS_SERVICE_UNKNOWN;
                break;
        }
    }

    return ret;
}

/*!
 * Set mcu low power
 */
PrvLoRaStatus_t PrivateLoRaSetLowPower( void )
{
    PrvLoRaStatus_t     ret;
    PrvLoRaTimerInfo_t  *p_timerInfo;
    bool                bRet;

    // initial check (state)
    if( ( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_CONTINUOUS_RX ) ||
        ( PrvLoRaMacMng.state >= _PRVLORA_MAC_STATE_BUSY ) )
    {
        return PRVLORA_STATUS_BUSY;
    }

    // init
    ret         = PRVLORA_STATUS_OK;
    p_timerInfo = &( PrvLoRaMacMng.timerInfo );

    /* START disabling interrupts in this function */
    BoardDisableAllIrq();
    {
        // check radio event
        bRet = PrivateLoRaRadioIsReadyProcess();
        if( bRet == true )
        {
            ret = PRVLORA_STATUS_BUSY;
        }

        // check timer event
        if( p_timerInfo->timerEvent.evtValue != 0 )
        {
            ret = PRVLORA_STATUS_BUSY;
        }

        // MCU low power
        if( ret == PRVLORA_STATUS_OK )
        {

            SetLowPower();
        }
    }
    /* END of disabling interrupts in this function */
    BoardEnableAllIrq();

    return ret;
}

//--------------------------------------------------------------------------------------------------
// Sub procedure (Tx/Rx parameters)

/*!
 * Tx/Rx parameters - Tx
 */
static PrvLoRaStatus_t PrivateLoRaMacPrepareTxParams( void )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIbParams_t       *p_ibParams;
    PrvLoRaRadioTxParams_t  *p_txParams;
    uint32_t                frequency;
    uint32_t                bandWidth;
    uint8_t                 modem, dataRate;

    // init
    p_ibParams = &( PrvLoRaMacMng.ibParams );
    p_txParams = &( PrvLoRaMacMng.txInfo.txParams );

    // get data rate
    ret = PrivateLoRaRegionGetDataRate( p_ibParams->drIndex, 
                                        &modem, 
                                        &dataRate, 
                                        &bandWidth );
    if( ret != PRVLORA_STATUS_OK )
    {
        ret = PRVLORA_STATUS_DATARATE_INVALID;
    }

    // get frequency
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaRegionGetFrequency( p_ibParams->channelId, 
                                             p_ibParams->drIndex, 
                                             &frequency );
        if( ret != PRVLORA_STATUS_OK )
        {
            ret = PRVLORA_STATUS_CHANNEL_INVALID;
        }
    }

    // set tx params
    if( ret == PRVLORA_STATUS_OK )
    {
        p_txParams->modem     = modem;
        p_txParams->dataRate  = dataRate;
        p_txParams->bandWidth = bandWidth;
        p_txParams->frequency = frequency;
        p_txParams->txPower   = p_ibParams->txPower;
        PrivateLoRaRegionGetMaxFrameSize( p_ibParams->drIndex, 
                                          &( p_txParams->maxFrameSize ), 
                                          &( p_txParams->txTimeout ) );
    }

    return ret;
}

/*!
 * Tx/Rx parameters - Rx
 */
static PrvLoRaStatus_t PrivateLoRaMacPrepareRxParams( void )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIbParams_t       *p_ibParams;
    PrvLoRaRadioRxParams_t  *p_rxParams;
    uint32_t                frequency;
    uint32_t                bandWidth;
    uint8_t                 modem, dataRate;

    // init
    p_ibParams = &( PrvLoRaMacMng.ibParams );
    p_rxParams = &( PrvLoRaMacMng.rxInfo.rxParams );

    // get data rate
    ret = PrivateLoRaRegionGetDataRate( p_ibParams->drIndex, 
                                        &modem, 
                                        &dataRate, 
                                        &bandWidth );
    if( ret != PRVLORA_STATUS_OK )
    {
        ret = PRVLORA_STATUS_DATARATE_INVALID;
    }

    // get frequency
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaRegionGetFrequency( p_ibParams->channelId, 
                                             p_ibParams->drIndex, 
                                             &frequency );
        if( ret != PRVLORA_STATUS_OK )
        {
            ret = PRVLORA_STATUS_CHANNEL_INVALID;
        }
    }

    // set rx params
    if( ret == PRVLORA_STATUS_OK )
    {
        p_rxParams->modem     = modem;
        p_rxParams->dataRate  = dataRate;
        p_rxParams->bandWidth = bandWidth;
        p_rxParams->frequency = frequency;
        PrivateLoRaRegionGetRxWindowParams( p_ibParams->drIndex,
                                            PRVLORA_MAC_MIN_RXSYMBOL,
                                            p_ibParams->systemMaxRxError,
                                            &( p_rxParams->windowTimeout ),
                                            &( p_rxParams->windowOffset ) );
        PrivateLoRaRegionGetMaxFrameSize( p_ibParams->drIndex, &( p_rxParams->maxFrameSize ), NULL );
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
// Sub procedure (MLME-Request)

/*!
 * MLME-Request - PRVLORA_MLME_KEY
 */
static PrvLoRaStatus_t PrivateLoRaMacMlmeKeyReq( PrvLoRaMlmeReq_t *p_mlmeReq )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaMlmeKeyReq_t     *p_keyReq;
    PrvLoRaIbParams_t       *p_ibParams;
    PrvLoRaTxInfo_t         *p_txInfo;
    PrvLoRaRxInfo_t         *p_rxInfo;
    PrvLoRaMlmeCfm_t        *p_mlmeCfm;
    PrvLoRaMlmeKeyCfm_t     *p_keyCfm;
    uint8_t                 initiatorNonce[ PRVLORA_CRYPTO_NONCE_SIZE ];
    bool                    isAck;

    // (init for initial check)
    p_keyReq = &( p_mlmeReq->req.keyReq );
    p_txInfo = &( PrvLoRaMacMng.txInfo );

    // initial check (state)
    if( PrvLoRaMacMng.state >= _PRVLORA_MAC_STATE_BUSY )
    {
        // PRVLORA_MAC_STATE_RX_RCVD; 
        //   allow this api to be called in mcps confirm or indication callback.
        if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
        {
            return PRVLORA_STATUS_BUSY;
        }
    }
    // initial check (another tx requst)
    if( p_txInfo->txStatus != PRVLORA_MAC_TX_RESULT_NONE )
    {
        return PRVLORA_STATUS_BUSY;
    }

    //----------

    // init
    ret        = PRVLORA_STATUS_OK;
    p_keyReq   = &( p_mlmeReq->req.keyReq );
    p_ibParams = &( PrvLoRaMacMng.ibParams );
    p_txInfo   = &( PrvLoRaMacMng.txInfo );
    p_rxInfo   = &( PrvLoRaMacMng.rxInfo );
    p_mlmeCfm  = &( p_txInfo->txConfirm.cfm.mlmeCfm );
    p_keyCfm   = &( p_mlmeCfm->cfm.keyCfm );
    isAck      = false;

    // check destination
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaRemoteDevSearchDevice( p_keyReq->dstMacAddr );
    }

    // in case direct tx
    if( p_keyReq->txOptions.options.IndirectTx == 0 )
    {
        // make frame
        if( ret == PRVLORA_STATUS_OK )
        {
            if( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_RX_RCVD )
            {
                // PRVLORA_MAC_STATE_RX_RCVD; set ack bit if sender requests the ACK
                if( p_rxInfo->rxFrameCtrl.ackRequest == 1 )
                {
                    isAck = true;
                }
            }

            // Initiator Nonce (random)
            PrivateLoRaCryptoMakeNonce( initiatorNonce, sizeof(initiatorNonce) );

            ret = PrivateLoRaFrameMakeKeyReq( &( p_txInfo->txPacket ),
                                              p_keyReq->dstMacAddr,
                                              p_ibParams->macAddr,
                                              initiatorNonce,
                                              isAck );
        }

        // send frame (in idle state only)
        if( ret == PRVLORA_STATUS_OK )
        {
            if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
            {
                // send
                ret = PrivateLoRaMacTxFrameSend();
            }
            else
            {
                // PRVLORA_MAC_STATE_RX_RCVD;
                //   cancel tx request if the response waiting timer has expired.
                if( PrvLoRaMacMng.timerInfo.timerEvent.events.txRspStart == 0 )
                {
                    p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_PENDING;
                }
                else
                {
                    ret = PRVLORA_STATUS_BUSY;
                }
            }
        }

        // go to next state
        if( ret == PRVLORA_STATUS_OK )
        {
            // regist initiator nonce
            PrivateLoRaRemoteDevSetInitiatorNonce( p_keyReq->dstMacAddr, initiatorNonce );

            // for MLME-Confirm
            p_txInfo->txConfirm.isEnableCfm = true;

            p_mlmeCfm->mlmeType = PRVLORA_MLME_KEY;
            p_keyCfm->status    = PRVLORA_EVENTINFO_STATUS_KEYREQ_FAILED;
            memcpy1( p_keyCfm->dstMacAddr, p_keyReq->dstMacAddr, PRVLORA_MACADDR_SIZE );

            // go to next state (idle->tx)
            if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
            {
                PrvLoRaMacMng.state = PRVLORA_MAC_STATE_TX_RUNNING;
            }
        }
        else
        {
            p_txInfo->txConfirm.isEnableCfm = false;  // fail-safe; cleanup

            p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_NONE;
            if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
            {
                PrivateLoRaMacStateToIdle();
            }
        }
    }
    // in case indirect tx
    else // if( p_keyReq->txOptions.options.IndirectTx == 1 )
    {
        if( ret == PRVLORA_STATUS_OK )
        {
            ret = PrivateLoRaIndTxEnqueueMlmeReq( p_mlmeReq );
        }
    }

    return ret;
}

/*!
 * MLME-Request - PRVLORA_MLME_DEVINFO
 */
static PrvLoRaStatus_t PrivateLoRaMacMlmeDevInfoReq( PrvLoRaMlmeReq_t *p_mlmeReq )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaMlmeDevInfoReq_t     *p_devInfoReq;
    PrvLoRaIbParams_t           *p_ibParams;
    PrvLoRaTxInfo_t             *p_txInfo;
    PrvLoRaRxInfo_t             *p_rxInfo;
    PrvLoRaMlmeCfm_t            *p_mlmeCfm;
    PrvLoRaMlmeDevInfoCfm_t     *p_devInfoCfm;
    bool                        isAck;

    // (init for initial check)
    p_txInfo = &( PrvLoRaMacMng.txInfo );

    // initial check (state)
    if( PrvLoRaMacMng.state >= _PRVLORA_MAC_STATE_BUSY )
    {
        // PRVLORA_MAC_STATE_RX_RCVD; 
        //   allow this api to be called in mcps confirm or indication callback.
        if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
        {
            return PRVLORA_STATUS_BUSY;
        }
    }
    // initial check (another tx requst)
    if( p_txInfo->txStatus != PRVLORA_MAC_TX_RESULT_NONE )
    {
        return PRVLORA_STATUS_BUSY;
    }

    //----------

    // init
    ret          = PRVLORA_STATUS_OK;
    p_devInfoReq = &( p_mlmeReq->req.devInfoReq );
    p_ibParams   = &( PrvLoRaMacMng.ibParams );
    p_txInfo     = &( PrvLoRaMacMng.txInfo );
    p_rxInfo     = &( PrvLoRaMacMng.rxInfo );
    p_mlmeCfm    = &( p_txInfo->txConfirm.cfm.mlmeCfm );
    p_devInfoCfm = &( p_mlmeCfm->cfm.devInfoCfm );
    isAck        = false;

    // check destination
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaRemoteDevSearchDevice( p_devInfoReq->dstMacAddr );
    }

    // in case direct tx
    if( p_devInfoReq->txOptions.options.IndirectTx == 0 )
    {
        // DevInfoReq frame
        if( ret == PRVLORA_STATUS_OK )
        {
            // make frame
            if( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_RX_RCVD )
            {
                // PRVLORA_MAC_STATE_RX_RCVD; set ack bit if sender requests the ACK
                if( p_rxInfo->rxFrameCtrl.ackRequest == 1 )
                {
                    isAck = true;
                }
            }

            ret = PrivateLoRaFrameMakeDevInfoReq( &( p_txInfo->txPacket ), 
                                                  p_devInfoReq->dstMacAddr,
                                                  p_ibParams->macAddr,
                                                  isAck,
                                                  &( p_devInfoReq->txOptions ) );

            // send frame (in idle state only)
            if( ret == PRVLORA_STATUS_OK )
            {
                if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
                {
                    ret = PrivateLoRaMacTxFrameSend();
                }
                else
                {
                    // PRVLORA_MAC_STATE_RX_RCVD;
                    if( PrvLoRaMacMng.timerInfo.timerEvent.events.txRspStart == 0 )
                    {
                        p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_PENDING;
                    }
                    else
                    {
                        // cancel tx request if the response waiting timer has expired.
                        ret = PRVLORA_STATUS_BUSY;
                    }
                }
            }
        }

        // go to next state
        if( ret == PRVLORA_STATUS_OK )
        {
            // for MLME-Confirm
            p_txInfo->txConfirm.isEnableCfm = true;

            p_mlmeCfm->mlmeType  = PRVLORA_MLME_DEVINFO;
            p_devInfoCfm->status = PRVLORA_EVENTINFO_STATUS_ERROR;

            // go to next state (idle->tx)
            if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
            {
                PrvLoRaMacMng.state = PRVLORA_MAC_STATE_TX_RUNNING;
            }
        }
        else
        {
            p_txInfo->txConfirm.isEnableCfm = false;  // fail-safe; cleanup

            p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_NONE;
            if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
            {
                PrivateLoRaMacStateToIdle();
            }
        }
    }
    // in case indirect tx
    else  // if( p_devInfoReq->txOptions.options.IndirectTx == 1 )
    {
        if( ret == PRVLORA_STATUS_OK )
        {
            ret = PrivateLoRaIndTxEnqueueMlmeReq( p_mlmeReq );
        }
    }

    return ret;
}

/*!
 * MLME-Request - PRVLORA_MLME_TXCYCLE
 */
static PrvLoRaStatus_t PrivateLoRaMacMlmeTxCycleReq( PrvLoRaMlmeReq_t *p_mlmeReq )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaMlmeTxCycleReq_t     *p_txCycleReq;
    PrvLoRaIbParams_t           *p_ibParams;
    PrvLoRaTxInfo_t             *p_txInfo;
    PrvLoRaRxInfo_t             *p_rxInfo;
    PrvLoRaMlmeCfm_t            *p_mlmeCfm;
    PrvLoRaMlmeTxCycleCfm_t     *p_txCycleCfm;
    bool                        isAck;

    // (init for initial check)
    p_txInfo = &( PrvLoRaMacMng.txInfo );

    // initial check (state)
    if( PrvLoRaMacMng.state >= _PRVLORA_MAC_STATE_BUSY )
    {
        // PRVLORA_MAC_STATE_RX_RCVD; 
        //   allow this api to be called in mcps confirm or indication callback.
        if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
        {
            return PRVLORA_STATUS_BUSY;
        }
    }
    // initial check (another tx requst)
    if( p_txInfo->txStatus != PRVLORA_MAC_TX_RESULT_NONE )
    {
        return PRVLORA_STATUS_BUSY;
    }

    //----------

    // init
    ret          = PRVLORA_STATUS_OK;
    p_txCycleReq = &( p_mlmeReq->req.txCycleReq );
    p_ibParams   = &( PrvLoRaMacMng.ibParams );
    p_txInfo     = &( PrvLoRaMacMng.txInfo );
    p_rxInfo     = &( PrvLoRaMacMng.rxInfo );
    p_mlmeCfm    = &( p_txInfo->txConfirm.cfm.mlmeCfm );
    p_txCycleCfm = &( p_mlmeCfm->cfm.txCycleCfm );
    isAck        = false;

    // check destination
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaRemoteDevSearchDevice( p_txCycleReq->dstMacAddr );
    }

    // check parameter - TxCycleTime
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_txCycleReq->txCycleTime != 0 )
        {
            if( ( p_txCycleReq->txCycleTime < PRVLORA_FRAME_PRMMIN_TXCYCLETIME ) ||
                ( p_txCycleReq->txCycleTime > PRVLORA_FRAME_PRMMAX_TXCYCLETIME ) )
            {
                ret = PRVLORA_STATUS_PARAMETER_INVALID;
            }
        }
    }

    // in case direct tx
    if( p_txCycleReq->txOptions.options.IndirectTx == 0 )
    {
        // TxCycleReq frame
        if( ret == PRVLORA_STATUS_OK )
        {
            // make frame
            if( PrvLoRaMacMng.state == PRVLORA_MAC_STATE_RX_RCVD )
            {
                // PRVLORA_MAC_STATE_RX_RCVD; set ack bit if sender requests the ACK
                if( p_rxInfo->rxFrameCtrl.ackRequest == 1 )
                {
                    isAck = true;
                }
            }

            ret = PrivateLoRaFrameMakeTxCycleReq( &( p_txInfo->txPacket ),
                                                p_txCycleReq->dstMacAddr,
                                                p_ibParams->macAddr,
                                                p_txCycleReq->txCycleTime,
                                                isAck,
                                                &( p_txCycleReq->txOptions ) );

            // send frame (in idle state only)
            if( ret == PRVLORA_STATUS_OK )
            {
                if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
                {
                    ret = PrivateLoRaMacTxFrameSend();
                }
                else
                {
                    // PRVLORA_MAC_STATE_RX_RCVD;
                    if( PrvLoRaMacMng.timerInfo.timerEvent.events.txRspStart == 0 )
                    {
                        p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_PENDING;
                    }
                    else
                    {
                        // cancel tx request if the response waiting timer has expired.
                        ret = PRVLORA_STATUS_BUSY;
                    }
                }
            }
        }

        // go to next state
        if( ret == PRVLORA_STATUS_OK )
        {
            // for MLME-Confirm
            p_txInfo->txConfirm.isEnableCfm = true;

            p_mlmeCfm->mlmeType  = PRVLORA_MLME_TXCYCLE;
            p_txCycleCfm->status = PRVLORA_EVENTINFO_STATUS_ERROR;

            // go to next state (idle->tx)
            if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
            {
                PrvLoRaMacMng.state = PRVLORA_MAC_STATE_TX_RUNNING;
            }
        }
        else
        {
            p_txInfo->txConfirm.isEnableCfm = false;  // fail-safe; cleanup

            p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_NONE;
            if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RCVD )
            {
                PrivateLoRaMacStateToIdle();
            }
        }
    }
    // in case indirect tx
    else  // if( p_txCycleReq->txOptions.options.IndirectTx == 1 )
    {
        if( ret == PRVLORA_STATUS_OK )
        {
            ret = PrivateLoRaIndTxEnqueueMlmeReq( p_mlmeReq );
        }
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
// Sub procedure (Tx)

/*!
 * Sub procedure (Tx) - make Tx frame
 */
static PrvLoRaStatus_t PrivateLoRaMacTxFramePrepare( uint8_t            frameType,
                                                     uint8_t            *p_dstAddr,
                                                     uint8_t            *p_srcAddr,
                                                     uint8_t            *p_txPayload, 
                                                     uint8_t            txPayloadSize,
                                                     bool               isAck,
                                                     PrvLoRaTxOptions_t txOptions )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaTxInfo_t         *p_txInfo;
    PrvLoRaRadioTxParams_t  *p_txParams;

    // init
    p_txInfo   = &( PrvLoRaMacMng.txInfo );
    p_txParams = &( p_txInfo->txParams );

    // make frame & length check
    ret = PrivateLoRaFrameMakeTx( &( p_txInfo->txPacket ), 
                                  frameType,
                                  isAck,
                                  p_dstAddr,
                                  p_srcAddr,
                                  p_txPayload,
                                  txPayloadSize,
                                  txOptions );
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_txInfo->txPacket.frameLength > p_txParams->maxFrameSize )
        {
            ret = PRVLORA_STATUS_PARAMETER_INVALID;
        }
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_PENDING;
    }

    return ret;
}

/*!
 * Sub procedure (Tx) - send Tx frame
 */
static PrvLoRaStatus_t PrivateLoRaMacTxFrameSend( void )
{
    PrvLoRaStatus_t     ret;
    PrvLoRaTxInfo_t     *p_txInfo;

    // init
    p_txInfo = &( PrvLoRaMacMng.txInfo );

    // wakeup radio
    PrivateLoRaRadioWakeup();

    // send frame
    ret = PrivateLoRaRadioSendTx( &( p_txInfo->txParams ), 
                                    p_txInfo->txPacket.frame.frameBuffer,
                                    p_txInfo->txPacket.frameLength );
    if( ret == PRVLORA_STATUS_OK )
    {
        p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_EXEC;
    }

    return ret;
}

/*!
 * Sub procedure (Tx) - MCPS/MLME confirm
 */
static void PrivateLoRaMacTxFrameConfirm( void )
{
    PrvLoRaTxInfo_t             *p_txInfo;
    PrvLoRaEventInfoStatus_t    eventStatus;
    PrvLoRaPrimitives_t         *p_primitives;
    PrvLoRaMcpsCfm_t            *p_mcpsCfm;
    PrvLoRaMlmeCfm_t            *p_mlmeCfm;
    uint8_t                     frameType;

    // init
    p_txInfo     = &( PrvLoRaMacMng.txInfo );
    p_primitives = &( PrvLoRaMacMng.primitives );
    frameType    = p_txInfo->txPacket.frame.frameMhdr.frameControl.frameType;

    // set tx result
    eventStatus = PRVLORA_EVENTINFO_STATUS_OK;  // init
    switch( p_txInfo->txStatus )
    {
        case PRVLORA_MAC_TX_RESULT_OK:
            // eventStatus = PRVLORA_EVENTINFO_STATUS_OK;
            break;

        case PRVLORA_MAC_TX_RESULT_EXEC:
        case PRVLORA_MAC_TX_RESULT_PENDING:
            // abort tx
            eventStatus = PRVLORA_EVENTINFO_STATUS_TX_CANCELED;
            break;

        case PRVLORA_MAC_TX_RESULT_TIMEOUT:
            eventStatus = PRVLORA_EVENTINFO_STATUS_TX_TIMEOUT;
            break;

        case PRVLORA_MAC_TX_RESULT_NOACK:
            eventStatus = PRVLORA_EVENTINFO_STATUS_TX_NOACK;
            break;

        case PRVLORA_MAC_TX_RESULT_CHANNELBUSY:
            eventStatus = PRVLORA_EVENTINFO_STATUS_TX_CHANNELBUSY;
            break;

        case PRVLORA_MAC_TX_RESULT_DUTYCYCLE_RESTRICTED:
            eventStatus = PRVLORA_EVENTINFO_STATUS_TX_DUTYCYCLE_RESTRICTED;
            break;

        case PRVLORA_MAC_TX_RESULT_RADIO_ERROR:
            eventStatus = PRVLORA_EVENTINFO_STATUS_TX_RADIO_ERROR;
            break;

        case PRVLORA_MAC_TX_RESULT_MLMECFM_ONLY:
            // not sent but want to confirm
            frameType = PRVLORA_FRAME_TYPE_MACCMD;
            break;

        // case PRVLORA_MAC_TX_RESULT_NONE:
        default:
            // no mlme-cfm and mcps-cfm. nothing to do in the function.
            return;
    }

    switch( frameType )
    {
        case PRVLORA_FRAME_TYPE_DATA:
            if( ( p_txInfo->txPacket.frame.frameMhdr.frameControl.ack == 1 ) &&
                ( p_txInfo->txPacket.frameLength == PRVLORA_FRAME_SIZE_ACKONLY ) )
            {
                // Sent ACK only
                break;  // exit from switch-case
            }

            // init
            p_mcpsCfm = &( p_txInfo->txConfirm.cfm.mcpsCfm );

            p_mcpsCfm->eventStatus = eventStatus;
            p_primitives->PrvLoRaMacMcpsConfirm( p_mcpsCfm );
            break;

        case PRVLORA_FRAME_TYPE_MACCMD:
            if( p_txInfo->txConfirm.isEnableCfm == true )
            {
                // init
                p_mlmeCfm = &( p_txInfo->txConfirm.cfm.mlmeCfm );
                p_primitives->PrvLoRaMacMlmeConfirm( p_mlmeCfm );
            }
            break;

        default:
            break;
    }

    // cleanup
    p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_NONE;
    memset1( (uint8_t *)&( p_txInfo->txConfirm ), 0x00, sizeof(PrvLoRaMacTxCfm_t) );
}


//--------------------------------------------------------------------------------------------------
// Sub procedure (TxRes)

/*!
 * Sub procedure (TxRes) - make response frame of MAC command
 */
static PrvLoRaStatus_t PrivateLoRaMacResponseFramePrepare( void )
{
    PrvLoRaStatus_t     ret;
    PrvLoRaRxInfo_t     *p_rxInfo;
    uint8_t             cid;

    // init
    ret      = PRVLORA_STATUS_ERROR;
    p_rxInfo = &( PrvLoRaMacMng.rxInfo );

    // initial check
    if( p_rxInfo->p_rxCmdPayload == NULL )
    {
        // no response
        return PRVLORA_STATUS_OK;
    }

    cid = p_rxInfo->p_rxCmdPayload[ 0 ];

    switch( cid )
    {
        case PRVLORA_FRAME_MACCMD_CID_KEYREQ:
            // make KeyRes frame
            ret = PrivateLoRaMacKeyResFramePrepare();
            break;

        case PRVLORA_FRAME_MACCMD_CID_DEVINFOREQ:
            // make DevInfoRes frame
            ret = PrivateLoRaMacDevInfoResFramePrepare();
            break;

        case PRVLORA_FRAME_MACCMD_CID_TXCYCLEREQ:
            // make TxCycleRes frame
            ret = PrivateLoRaMacTxCycleResFramePrepare();
            break;

        default:
            break;
    }

    return ret;
}

/*!
 * Sub procedure (TxRes) - Make response frame of MAC command - KeyRes
 */
static PrvLoRaStatus_t PrivateLoRaMacKeyResFramePrepare( void )
{
    PrvLoRaStatus_t     ret;
    PrvLoRaIbParams_t   *p_ibParams;
    PrvLoRaRxInfo_t     *p_rxInfo;
    PrvLoRaTxInfo_t     *p_txInfo;
    uint8_t             responderNonce[ PRVLORA_CRYPTO_NONCE_SIZE ];

    // init
    p_ibParams = &( PrvLoRaMacMng.ibParams );
    p_rxInfo   = &( PrvLoRaMacMng.rxInfo );
    p_txInfo   = &( PrvLoRaMacMng.txInfo );

    ret = PrivateLoRaRemoteDevGetResponderNonce( p_rxInfo->rxSrcAddr, responderNonce );
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaFrameMakeKeyRes( &( p_txInfo->txPacket ), 
                                          p_rxInfo->rxSrcAddr,
                                          p_ibParams->macAddr,
                                          responderNonce );
        if( ret == PRVLORA_STATUS_OK )
        {
            // clear frame counter
            // (last frame counter is already set to the response)
            PrivateLoRaRemoteDevSetFrameCounterTx( p_rxInfo->rxSrcAddr, (uint32_t)0 );
            PrivateLoRaRemoteDevSetFrameCounterRx( p_rxInfo->rxSrcAddr, (uint32_t)(-1) );

            p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_PENDING;
        }
    }

    return ret;
}

/*!
 * Sub procedure (TxRes) - make response frame of MAC command - DevInfoRes
 */
static PrvLoRaStatus_t PrivateLoRaMacDevInfoResFramePrepare( void )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIbParams_t       *p_ibParams;
    PrvLoRaRxInfo_t         *p_rxInfo;
    PrvLoRaTxInfo_t         *p_txInfo;
    PrvLoRaTxOptions_t      txOptions;
    int                     compare;
    uint32_t                txCycleTime;

    // init
    p_ibParams           = &( PrvLoRaMacMng.ibParams );
    p_rxInfo             = &( PrvLoRaMacMng.rxInfo );
    p_txInfo             = &( PrvLoRaMacMng.txInfo );
    txOptions.txOptValue = 0x00;
    txCycleTime          = (uint32_t)0;

    // TxOptions
    if( p_rxInfo->rxFrameCtrl.secEnabled == 1 )
    {
        txOptions.options.SecEnable = 1;
    }
    if( p_rxInfo->rxFrameCtrl.ackRequest == 1 )
    {
        txOptions.options.ack = 1;
    }

    // (txCycle) check source address
    compare = memcmp( p_rxInfo->rxSrcAddr, p_ibParams->txCycle.dstMacAddr, PRVLORA_MACADDR_SIZE );
    if( compare == 0 )
    {
        txCycleTime = p_ibParams->txCycle.txCycleTime;
    }

    // make response frame
    ret = PrivateLoRaFrameMakeDevInfoRes( &( p_txInfo->txPacket ),
                                          p_rxInfo->rxSrcAddr,
                                          p_ibParams->macAddr,
                                          p_rxInfo->p_rxDoneInfo->snr,
                                          p_ibParams->txPower,
                                          txCycleTime,
                                          &txOptions );
    if( ret == PRVLORA_STATUS_OK )
    {
        p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_PENDING;
    }

    return ret;
}

/*!
 * Sub procedure (TxRes) - make response frame of MAC command - TxCycleRes
 */
static PrvLoRaStatus_t PrivateLoRaMacTxCycleResFramePrepare( void )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIbParams_t       *p_ibParams;
    PrvLoRaRxInfo_t         *p_rxInfo;
    PrvLoRaTxInfo_t         *p_txInfo;
    PrvLoRaTxOptions_t      txOptions;

    // init
    p_ibParams           = &( PrvLoRaMacMng.ibParams );
    p_rxInfo             = &( PrvLoRaMacMng.rxInfo );
    p_txInfo             = &( PrvLoRaMacMng.txInfo );
    txOptions.txOptValue = 0x00;

    // TxOptions
    if( p_rxInfo->rxFrameCtrl.secEnabled == 1 )
    {
        txOptions.options.SecEnable = 1;
    }
    if( p_rxInfo->rxFrameCtrl.ackRequest == 1 )
    {
        txOptions.options.ack = 1;
    }

    // make response frame
    ret = PrivateLoRaFrameMakeTxCycleRes( &( p_txInfo->txPacket ),
                                          p_rxInfo->rxSrcAddr,
                                          p_ibParams->macAddr,
                                          &txOptions );
    if( ret == PRVLORA_STATUS_OK )
    {
        p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_PENDING;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
// Sub procedure (Rx)

/*
 * Sub procedure (Rx) - Process rx command
 */
static PrvLoRaStatus_t PrivateLoRaMacRxCommand( void )
{
    PrvLoRaStatus_t     ret;
    PrvLoRaRxInfo_t     *p_rxInfo;
    PrvLoRaIbParams_t   *p_ibParams;
    uint8_t             cid;

    // init
    ret        = PRVLORA_STATUS_ERROR;
    p_rxInfo   = &( PrvLoRaMacMng.rxInfo );
    p_ibParams = &( PrvLoRaMacMng.ibParams );
    cid        = p_rxInfo->p_rxPayload[ 0 ];

    // Process rx command
    switch( cid )
    {
        case PRVLORA_FRAME_MACCMD_CID_KEYREQ:
            // received KeyReq command
            if( p_ibParams->keyReqPermit == true )
            {
                ret = PrivateLoRaMacRxCommandKeyReq();
            }
            break;

        case PRVLORA_FRAME_MACCMD_CID_KEYRES:
            // received KeyRes command
            ret = PrivateLoRaMacRxCommandKeyRes();
            break;

        case PRVLORA_FRAME_MACCMD_CID_DEVINFOREQ:
            // received DevInfoReq command
            ret = PrivateLoRaMacRxCommandDevInfoReq();
            break;

        case PRVLORA_FRAME_MACCMD_CID_DEVINFORES:
            // received DevInfoRes command
            ret = PrivateLoRaMacRxCommandDevInfoRes();
            break;

        case PRVLORA_FRAME_MACCMD_CID_TXCYCLEREQ:
            ret = PrivateLoRaMacRxCommandTxCycleReq();
            break;

        case PRVLORA_FRAME_MACCMD_CID_TXCYCLERES:
            ret = PrivateLoRaMacRxCommandTxCycleRes();
            break;

        default:
            // received ??? command
            break;
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        // keep rx command
        p_rxInfo->p_rxCmdPayload = p_rxInfo->p_rxPayload;
    }
    else
    {
        // discard rx command
        p_rxInfo->p_rxCmdPayload = NULL;
    }

    return ret;
}

/*
 * Sub procedure (Rx) - Process rx command - KeyReq
 */
static PrvLoRaStatus_t PrivateLoRaMacRxCommandKeyReq( void )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaIbParams_t           *p_ibParams;
    PrvLoRaRxInfo_t             *p_rxInfo;
    PrvLoRaPayloadCmdKeyReq_t   rxCmdKeyReq, *p_rxCmdKeyReq;
    uint8_t                     responderNonce[ PRVLORA_CRYPTO_NONCE_SIZE ];

    // init
    ret           = PRVLORA_STATUS_OK;
    p_ibParams    = &( PrvLoRaMacMng.ibParams );
    p_rxInfo      = &( PrvLoRaMacMng.rxInfo );
    p_rxCmdKeyReq = &rxCmdKeyReq;

    // command length
    if( p_rxInfo->rxPayloadSize >= ( 1 + sizeof(PrvLoRaPayloadCmdKeyReq_t) ) )  // 1 = CID
    {
        memcpy1( (uint8_t *)p_rxCmdKeyReq, 
                 &( p_rxInfo->p_rxPayload[ 1 ] ), 
                 sizeof(PrvLoRaPayloadCmdKeyReq_t) );
    }
    else
    {
        ret = PRVLORA_STATUS_LENGTH_ERROR;
    }

    // make security key
    if( ret == PRVLORA_STATUS_OK )
    {
        // make responder nonce
        PrivateLoRaCryptoMakeNonce( responderNonce, sizeof(responderNonce) );

        ret = PrivateLoRaCryptoMakeSessionKey( p_rxInfo->rxSrcAddr,
                                               p_ibParams->macAddr,
                                               responderNonce,
                                               p_rxCmdKeyReq->initiatorNonce,
                                               false );
    }

    return ret;
}

/*
 * Sub procedure (Rx) - Process rx command - KeyRes
 */
static PrvLoRaStatus_t PrivateLoRaMacRxCommandKeyRes( void )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaIbParams_t           *p_ibParams;
    PrvLoRaRxInfo_t             *p_rxInfo;
    PrvLoRaTxInfo_t             *p_txInfo;
    PrvLoRaMlmeCfm_t            *p_mlmeCfm;
    PrvLoRaMlmeKeyCfm_t         *p_keyCfm;
    PrvLoRaPayloadCmdKeyRes_t   rxCmdKeyRes, *p_rxCmdKeyRes;
    uint8_t                     initiatorNonce[ PRVLORA_CRYPTO_NONCE_SIZE ];

    // init
    ret           = PRVLORA_STATUS_OK;
    p_ibParams    = &( PrvLoRaMacMng.ibParams );
    p_rxInfo      = &( PrvLoRaMacMng.rxInfo );
    p_txInfo      = &( PrvLoRaMacMng.txInfo );
    p_mlmeCfm     = &( p_txInfo->txConfirm.cfm.mlmeCfm );
    p_keyCfm      = &( p_mlmeCfm->cfm.keyCfm );
    p_rxCmdKeyRes = &rxCmdKeyRes;

    // command length
    if( p_rxInfo->rxPayloadSize >= ( 1 + sizeof(PrvLoRaPayloadCmdKeyRes_t) ) )  // 1 = CID
    {
        memcpy1( (uint8_t *)p_rxCmdKeyRes, 
                 &( p_rxInfo->p_rxPayload[ 1 ] ), 
                 sizeof(PrvLoRaPayloadCmdKeyRes_t) );
    }
    else
    {
        ret = PRVLORA_STATUS_LENGTH_ERROR;
    }

    // make security key
    if( ret == PRVLORA_STATUS_OK )
    {
        // get initiator nonce
        ret = PrivateLoRaRemoteDevGetInitiatorNonce( p_rxInfo->rxSrcAddr, initiatorNonce );
        if( ret == PRVLORA_STATUS_OK )
        {
            ret = PrivateLoRaCryptoMakeSessionKey( p_rxInfo->rxSrcAddr,
                                                   p_ibParams->macAddr,
                                                   p_rxCmdKeyRes->responderNonce,
                                                   initiatorNonce,
                                                   true );
        }
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        // clear frame counter
        PrivateLoRaRemoteDevSetFrameCounterTx( p_rxInfo->rxSrcAddr, (uint32_t)0 );
        PrivateLoRaRemoteDevSetFrameCounterRx( p_rxInfo->rxSrcAddr, (uint32_t)(-1) );

        // update key-cfm
        p_keyCfm->status = PRVLORA_EVENTINFO_STATUS_OK;
    }

    return ret;
}

/*
 * Sub procedure (Rx) - Process rx command - DevInfoReq
 */
static PrvLoRaStatus_t PrivateLoRaMacRxCommandDevInfoReq( void )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRxInfo_t             *p_rxInfo;

    // init
    ret            = PRVLORA_STATUS_OK;
    p_rxInfo       = &( PrvLoRaMacMng.rxInfo );

    // command length  (no command parameter)
    if( p_rxInfo->rxPayloadSize < 1 )  // 1 = CID
    {
        ret = PRVLORA_STATUS_LENGTH_ERROR;
    }

    return ret;
}

/*
 * Sub procedure (Rx) - Process rx command - DevInfoRes
 */
static PrvLoRaStatus_t PrivateLoRaMacRxCommandDevInfoRes( void )
{
    PrvLoRaStatus_t                 ret;
    PrvLoRaRxInfo_t                 *p_rxInfo;
    PrvLoRaTxInfo_t                 *p_txInfo;
    PrvLoRaMlmeCfm_t                *p_mlmeCfm;
    PrvLoRaMlmeDevInfoCfm_t         *p_devInfoCfm;
    PrvLoRaPayloadCmdDevInfoRes_t   rxCmdDevInfoRes, *p_rxCmdDevInfoRes;
    uint32_t                        tmp32;
    uint8_t                         i;

    // init
    ret               = PRVLORA_STATUS_OK;
    p_rxInfo          = &( PrvLoRaMacMng.rxInfo );
    p_txInfo          = &( PrvLoRaMacMng.txInfo );
    p_mlmeCfm         = &( p_txInfo->txConfirm.cfm.mlmeCfm );
    p_devInfoCfm      = &( p_mlmeCfm->cfm.devInfoCfm );
    p_rxCmdDevInfoRes = &rxCmdDevInfoRes;

    // command length
    if( p_rxInfo->rxPayloadSize >= ( 1 + sizeof(PrvLoRaPayloadCmdDevInfoRes_t) ) )  // 1 = CID
    {
        memcpy1( (uint8_t *)p_rxCmdDevInfoRes, 
                 &( p_rxInfo->p_rxPayload[ 1 ] ), 
                 sizeof(PrvLoRaPayloadCmdDevInfoRes_t) );
    }
    else
    {
        ret = PRVLORA_STATUS_LENGTH_ERROR;
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        // update devInfo-cfm
        p_devInfoCfm->snr         = PRVLORA_FRAME_PRMGET_SNR( p_rxCmdDevInfoRes->snr );
        p_devInfoCfm->txPower     = p_rxCmdDevInfoRes->txPower;
        p_devInfoCfm->txCycleTime = 0;
        for( i = 0; i < PRVLORA_FRAME_PRMSIZE_TXCYCLETIME; i++ )
        {
            tmp32 = (uint32_t)p_rxCmdDevInfoRes->txCycleTime[ i ];
            tmp32 = (uint32_t)( tmp32 << (8 * i) );
            p_devInfoCfm->txCycleTime |= tmp32;
        }
        if( ( p_devInfoCfm->txCycleTime == 0 ) ||
            ( ( p_devInfoCfm->txCycleTime >= PRVLORA_FRAME_PRMMIN_TXCYCLETIME ) &&
              ( p_devInfoCfm->txCycleTime <= PRVLORA_FRAME_PRMMAX_TXCYCLETIME ) ) )
        {
            p_devInfoCfm->status = PRVLORA_EVENTINFO_STATUS_OK;
        }
        else
        {
            ret = PRVLORA_STATUS_PARAMETER_INVALID;
        }
    }

    return ret;
}

/*
 * Sub procedure (Rx) - Process rx command - TxCycleReq
 */
static PrvLoRaStatus_t PrivateLoRaMacRxCommandTxCycleReq( void )
{
    PrvLoRaStatus_t                 ret;
    PrvLoRaRxInfo_t                 *p_rxInfo;
    PrvLoRaPayloadCmdTxCycleReq_t   rxCmdTxCycleReq, *p_rxCmdTxCycleReq;
    uint32_t                        txCycleTime, tmp32;
    uint8_t                         i;

    // init
    ret               = PRVLORA_STATUS_OK;
    p_rxInfo          = &( PrvLoRaMacMng.rxInfo );
    p_rxCmdTxCycleReq = &rxCmdTxCycleReq;

    // command length
    if( p_rxInfo->rxPayloadSize >= ( 1 + sizeof(PrvLoRaPayloadCmdTxCycleReq_t) ) )  // 1 = CID
    {
        memcpy1( (uint8_t *)p_rxCmdTxCycleReq, 
                 &( p_rxInfo->p_rxPayload[ 1 ] ), 
                 sizeof(PrvLoRaPayloadCmdTxCycleReq_t) );
    }
    else
    {
        ret = PRVLORA_STATUS_LENGTH_ERROR;
    }

    // get/check TxCycleTime
    if( ret == PRVLORA_STATUS_OK )
    {
        txCycleTime = 0;
        for( i = 0; i < PRVLORA_FRAME_PRMSIZE_TXCYCLETIME; i++ )
        {
            tmp32 = (uint32_t)p_rxCmdTxCycleReq->txCycleTime[ i ];
            tmp32 = (uint32_t)( tmp32 << (8 * i) );
            txCycleTime |= tmp32;
        }
        if( txCycleTime != 0 )
        {
            if( ( txCycleTime < PRVLORA_FRAME_PRMMIN_TXCYCLETIME ) ||
                ( txCycleTime > PRVLORA_FRAME_PRMMAX_TXCYCLETIME ) )
            {
                ret = PRVLORA_STATUS_PARAMETER_INVALID;
            }
        }
    }

    return ret;
}

/*
 * Sub procedure (Rx) - Process rx command - TxCycleRes
 */
static PrvLoRaStatus_t PrivateLoRaMacRxCommandTxCycleRes( void )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRxInfo_t             *p_rxInfo;
    PrvLoRaTxInfo_t             *p_txInfo;
    PrvLoRaMlmeCfm_t            *p_mlmeCfm;
    PrvLoRaMlmeTxCycleCfm_t     *p_txCycleCfm;

    // init
    ret          = PRVLORA_STATUS_OK;
    p_rxInfo     = &( PrvLoRaMacMng.rxInfo );
    p_txInfo     = &( PrvLoRaMacMng.txInfo );
    p_mlmeCfm    = &( p_txInfo->txConfirm.cfm.mlmeCfm );
    p_txCycleCfm = &( p_mlmeCfm->cfm.txCycleCfm );

    // command length
    if( p_rxInfo->rxPayloadSize < 1 )  // 1 = CID
    {
        ret = PRVLORA_STATUS_LENGTH_ERROR;
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        // update txCyce-cfm
        p_txCycleCfm->status = PRVLORA_EVENTINFO_STATUS_OK;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
// Sub procedure (MLME-Indication)

/*
 * Sub procedure (MLME-Indication) - MLME-Indication
 */
static PrvLoRaStatus_t PrivateLoRaMacMlmeIndication( void )
{
    PrvLoRaStatus_t     ret;
    PrvLoRaPrimitives_t *p_primitives;
    PrvLoRaRxInfo_t     *p_rxInfo;
    PrvLoRaMlmeInd_t    *p_mlmeIndication;
    uint8_t             cid;

    // init
    ret              = PRVLORA_STATUS_OK;
    p_primitives     = &( PrvLoRaMacMng.primitives );
    p_rxInfo         = &( PrvLoRaMacMng.rxInfo );
    p_mlmeIndication = &( p_rxInfo->rxIndication.mlmeInd );
    cid              = p_rxInfo->p_rxPayload[ 0 ];

    if( p_rxInfo->p_rxCmdPayload == NULL )
    {
        ret = PRVLORA_STATUS_ERROR;
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PRVLORA_STATUS_ERROR;  // re-init

        switch( cid )
        {
            case PRVLORA_FRAME_MACCMD_CID_KEYREQ:
                // Key indication
                ret = PrivateLoRaMacMlmeKeyInd();
                break;

            case PRVLORA_FRAME_MACCMD_CID_TXCYCLEREQ:
                // TxCycle indication
                ret = PrivateLoRaMacMlmeTxCycleInd();
                break;

            default:
                break;
        }
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        p_primitives->PrvLoRaMacMlmeIndication( p_mlmeIndication );
    }

    return ret;
}

/*
 * Sub procedure (MLME-Indication) - MLME-Indication - PRVLORA_MLME_KEY
 */
static PrvLoRaStatus_t PrivateLoRaMacMlmeKeyInd( void )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaRxInfo_t         *p_rxInfo;
    PrvLoRaMlmeInd_t        *p_mlmeIndication;
    PrvLoRaMlmeKeyInd_t     *p_mlmeKeyInd;

    // init
    ret              = PRVLORA_STATUS_OK;
    p_rxInfo         = &( PrvLoRaMacMng.rxInfo );
    p_mlmeIndication = &( p_rxInfo->rxIndication.mlmeInd );
    p_mlmeKeyInd     = &( p_mlmeIndication->ind.keyInd );

    // make mlme-indication
    p_mlmeIndication->mlmeType = PRVLORA_MLME_KEY;
    memcpy1( p_mlmeKeyInd->srcMacAddr, p_rxInfo->rxSrcAddr, PRVLORA_MACADDR_SIZE );

    return ret;
}

/*
 * Sub procedure (MLME-Indication) - MLME-Indication - PRVLORA_MLME_TXCYCLE
 */
static PrvLoRaStatus_t PrivateLoRaMacMlmeTxCycleInd( void )
{
    PrvLoRaStatus_t                 ret;
    PrvLoRaRxInfo_t                 *p_rxInfo;
    PrvLoRaMlmeInd_t                *p_mlmeIndication;
    PrvLoRaMlmeTxCycleInd_t         *p_mlmeTxCycleInd;

    PrvLoRaPayloadCmdTxCycleReq_t   rxCmdTxCycleReq, *p_rxCmdTxCycleReq;
    uint32_t                        txCycleTime, tmp32;
    uint8_t                         i;

    // init
    ret               = PRVLORA_STATUS_OK;
    p_rxInfo          = &( PrvLoRaMacMng.rxInfo );
    p_mlmeIndication  = &( p_rxInfo->rxIndication.mlmeInd );
    p_mlmeTxCycleInd  = &( p_mlmeIndication->ind.txCycleInd );
    p_rxCmdTxCycleReq = &rxCmdTxCycleReq;

    memcpy1( (uint8_t *)p_rxCmdTxCycleReq, 
             &( p_rxInfo->p_rxPayload[ 1 ] ), 
             sizeof(PrvLoRaPayloadCmdTxCycleReq_t) );

    txCycleTime = 0;
    for( i = 0; i < PRVLORA_FRAME_PRMSIZE_TXCYCLETIME; i++ )
    {
        tmp32 = (uint32_t)p_rxCmdTxCycleReq->txCycleTime[ i ];
        tmp32 = (uint32_t)( tmp32 << (8 * i) );
        txCycleTime |= tmp32;
    }

    // make mlme-indication
    p_mlmeIndication->mlmeType = PRVLORA_MLME_TXCYCLE;
    memcpy1( p_mlmeTxCycleInd->srcMacAddr, p_rxInfo->rxSrcAddr, PRVLORA_MACADDR_SIZE );
    p_mlmeTxCycleInd->txCycleTime = txCycleTime;
    p_mlmeTxCycleInd->isSecurity  = (bool)( p_rxInfo->rxFrameCtrl.secEnabled );

    return ret;
}

//--------------------------------------------------------------------------------------------------
// Handle/Process radio event

/*!
 * Handle radio event
 */
static void PrivateLoRaMacHandleRadioEvents( void )
{
    PrvLoRaRadioEvents_t    radioEvents;

    // get radio event
    PrivateLoRaRadioGetEvents( &radioEvents );

    // handle radio evnet
    if( radioEvents.radioEvent.evtValue != 0 )
    {
        if( radioEvents.radioEvent.events.TxDone != 0 )
        {
            PrivateLoRaMacProcessRadioTxDone( &( radioEvents.eventInfo.txDoneInfo ) );
        }
        if( radioEvents.radioEvent.events.TxTimeout != 0 )
        {
            PrivateLoRaMacProcessRadioTxTimeout();
        }
        if( radioEvents.radioEvent.events.RxDone != 0 )
        {
            PrivateLoRaMacProcessRadioRxDone( &( radioEvents.eventInfo.rxDoneInfo ) );
        }
        if( radioEvents.radioEvent.events.RxError != 0 )
        {
            PrivateLoRaMacProcessRadioRxErrorTimeout( true );  // true = RxError
        }
        if( radioEvents.radioEvent.events.RxTimeout != 0 )
        {
            PrivateLoRaMacProcessRadioRxErrorTimeout( false );  // false = RxTimeout
        }

#ifdef DEBUG_PRVLORA
        PrivateLoRaDebugDispRadioEvent( &radioEvents );
#endif
    }
}

/*!
 * Process radio event; TxDone
 */
static void PrivateLoRaMacProcessRadioTxDone( PrvLoRaRadioEventsTxDone_t *p_txDoneInfo )
{
    PrvLoRaTxInfo_t     *p_txInfo;
    TimerEvent_t        *p_timerEvent;
    TimerTime_t         timeOffset;
    int32_t             rxWinStartTime;

    // initial check (state)
    if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_TX_RUNNING )
    {
        // Unexpected radio event
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_FAULT;
        return;
    }

    // init
    p_txInfo           = &( PrvLoRaMacMng.txInfo );
    p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_OK;
    p_timerEvent       = &( PrvLoRaMacMng.timerInfo.timerRxWinStart );

    // Prepare to receive response
    PrivateLoRaRadioSleepWarm();

    // set timer
    rxWinStartTime = PrivateLoRaMacGetWaitTimeTx2Rx();
    timeOffset  = TimerGetCurrentTime();
    timeOffset -= p_txDoneInfo->txDoneTime;
    rxWinStartTime = rxWinStartTime - (int32_t)timeOffset;
    if( rxWinStartTime < (int32_t)0 )
    {
        // fail-safe to avoid waiting an huge amount of time
        rxWinStartTime = 0;
    }

    TimerSetValue( p_timerEvent, (uint32_t)rxWinStartTime );
    TimerStart( p_timerEvent );

    // go to next state
    PrvLoRaMacMng.state = PRVLORA_MAC_STATE_RX_WAITING;
}

/*!
 * Process radio event; TxTimeout
 */
static void PrivateLoRaMacProcessRadioTxTimeout( void )
{
    PrvLoRaTxInfo_t     *p_txInfo;

    // initial check (state)
    if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_TX_RUNNING )
    {
        // Unexpected radio event
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_FAULT;
        return;
    }

    // update state
    PrvLoRaMacMng.state = PRVLORA_MAC_STATE_TX_FAILED;

    //
    p_txInfo = &( PrvLoRaMacMng.txInfo );
    p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_TIMEOUT;
}

/*!
 * Process radio event; RxDone
 */
static void PrivateLoRaMacProcessRadioRxDone( PrvLoRaRadioEventsRxDone_t *p_rxDoneInfo )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRxInfo_t             *p_rxInfo;
    PrvLoRaTxInfo_t             *p_txInfo;
    PrvLoRaIbParams_t           *p_ibParams;
    PrvLoRaPrimitives_t         *p_primitives;
    PrvLoRaMcpsInd_t            *p_mcpsIndication;
    PrvLoRaTimerInfo_t          *p_timerInfo;

    uint8_t                     maxFrameSize;
    int                         compare;

    PrvLoRaTxOptions_t          txOptions;
    TimerTime_t                 timeOffset;
    int32_t                     txRspStartTime;

    // initial check (state)
    if( ( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RUNNING ) &&
        ( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_CONTINUOUS_RX ) )
    {
        // Unexpected radio event
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_FAULT;
        return;
    }

    // init
    p_rxInfo     = &( PrvLoRaMacMng.rxInfo );
    p_txInfo     = &( PrvLoRaMacMng.txInfo );
    p_ibParams   = &( PrvLoRaMacMng.ibParams );
    p_primitives = &( PrvLoRaMacMng.primitives );
    p_timerInfo  = &( PrvLoRaMacMng.timerInfo );
    // init rxInfo
    p_rxInfo->p_rxDoneInfo = p_rxDoneInfo; 
    p_rxInfo->p_rxFrame    = p_rxDoneInfo->p_payload;
    p_rxInfo->rxFrameSize  = p_rxDoneInfo->size;
    memset1( (uint8_t *)&( p_rxInfo->rxFrameCtrl ), 0x00, sizeof(PrvLoRaFrameMhdrFrmCtrl_t) );
    p_rxInfo->p_rxPayload    = p_rxDoneInfo->p_payload;  // reuse radio rx buffer
    p_rxInfo->rxPayloadSize  = 0;
    p_rxInfo->p_rxCmdPayload = NULL;
    memset1( (uint8_t *)&( p_rxInfo->rxIndication ), 0x00, sizeof(PrvLoRaMacRxInd_t) );

    // sleep radio
    PrivateLoRaRadioSleepCold();

    // update state
    PrvLoRaMacMng.state = PRVLORA_MAC_STATE_RX_RCVD;

    //-----------------------
    // check rx frame
    ret = PrivateLoRaRegionGetMaxFrameSize( p_ibParams->drIndex, &maxFrameSize, NULL );
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaFramePerserRx( maxFrameSize,
                                        p_rxInfo->p_rxFrame,
                                        p_rxInfo->rxFrameSize,
                                        &( p_rxInfo->rxFrameCtrl ),
                                        p_rxInfo->rxSrcAddr,
                                        p_rxInfo->rxDstAddr,
                                        p_rxInfo->p_rxPayload,
                                        &( p_rxInfo->rxPayloadSize ) );

#ifdef DEBUG_PRVLORA
        if( ret == PRVLORA_STATUS_OK )
        {
            PrivateLoRaDebugDispRxFrame( &(p_rxInfo->rxFrameCtrl),
                                         p_rxInfo->rxSrcAddr,
                                         p_rxInfo->rxDstAddr,
                                         p_rxInfo->p_rxPayload,
                                         p_rxInfo->rxPayloadSize );
        }
#endif
    }

    //------------------------------------------
    // check destination address (= my address)
    if( ret == PRVLORA_STATUS_OK )
    {
        compare = memcmp( p_rxInfo->rxDstAddr, p_ibParams->macAddr, PRVLORA_MACADDR_SIZE );
        if( compare != 0 )
        {
            ret = PRVLORA_STATUS_UNKNOWN_DEVICE;
        }
    }
    if( ret != PRVLORA_STATUS_OK )
    {
        // clear FrameControl bit for the process below.
        p_rxInfo->rxFrameCtrl.ack        = 0;
        p_rxInfo->rxFrameCtrl.ackRequest = 0;

        // update state
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_RX_FAILED;
    }

    //-----------------------
    // Check received command
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_rxInfo->rxFrameCtrl.frameType == PRVLORA_FRAME_TYPE_MACCMD )
        {
            PrivateLoRaMacRxCommand();
        }
    }

    //-----------------------
    // Mcps/MlmeReq confirm
    if( p_txInfo->txStatus == PRVLORA_MAC_TX_RESULT_OK )
    {
        // check ACK
        if( p_rxInfo->rxFrameCtrl.ack == 0 )
        {
            if( p_txInfo->txPacket.frame.frameMhdr.frameControl.ackRequest == 1 )
            {
                p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_NOACK;
            }
        }
        // Note: application can call PrivateLoRaMcpsRequest() in the following function.
        PrivateLoRaMacTxFrameConfirm();
    }

    //----------------------
    // make comand response
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_rxInfo->rxFrameCtrl.frameType == PRVLORA_FRAME_TYPE_MACCMD )
        {
            PrivateLoRaMacResponseFramePrepare();
        }
    }

    //-----------------------
    // Mcps/Mlme indication
    if( ret == PRVLORA_STATUS_OK )
    {
        switch( p_rxInfo->rxFrameCtrl.frameType )
        {
            case PRVLORA_FRAME_TYPE_DATA:
                // init
                p_mcpsIndication = &( p_rxInfo->rxIndication.mcpsInd );

                p_mcpsIndication->eventStatus  = PRVLORA_EVENTINFO_STATUS_OK;
                p_mcpsIndication->p_srcMacAddr = p_rxInfo->rxSrcAddr;
                p_mcpsIndication->p_rxData     = p_rxInfo->p_rxPayload;
                p_mcpsIndication->rxDataSize   = p_rxInfo->rxPayloadSize;
                p_mcpsIndication->rssi         = p_rxDoneInfo->rssi;
                p_mcpsIndication->snr          = p_rxDoneInfo->snr;
                p_mcpsIndication->isAck        = (bool)( p_rxInfo->rxFrameCtrl.ack );
                p_mcpsIndication->isSecurity   = (bool)( p_rxInfo->rxFrameCtrl.secEnabled );

                // Note: application can call PrivateLoRaMcpsRequest() in the following function.
                p_primitives->PrvLoRaMacMcpsIndication( p_mcpsIndication );
                break;

            case PRVLORA_FRAME_TYPE_MACCMD:
                PrivateLoRaMacMlmeIndication();
                break;

            default:
                // (never comes here)
                break;
        }
    }

    //-----------------------
    // prepare to response
    if( ret == PRVLORA_STATUS_OK )
    {
        switch( p_txInfo->txStatus )
        {
            case PRVLORA_MAC_TX_RESULT_NONE:

                // dequeue indirect tx
                PrivateLoRaIndTxDequeueAndSend( p_rxInfo->rxSrcAddr );

                if( p_txInfo->txStatus == PRVLORA_MAC_TX_RESULT_NONE )
                {
                    // (no indirect tx entry)

                    if( p_rxInfo->rxFrameCtrl.ackRequest == 1)
                    {
                        // ACK only
                        txOptions.txOptValue = 0;
                        PrivateLoRaMacTxFramePrepare( PRVLORA_FRAME_TYPE_DATA,
                                                      p_rxInfo->rxSrcAddr,
                                                      p_ibParams->macAddr,
                                                      NULL,
                                                      0,
                                                      true,
                                                      txOptions );
                    }
                }
                break;

            case PRVLORA_MAC_TX_RESULT_PENDING:
                // application has requested tx in confirm/indication callback functions.
                break;

            default:
                // nothing to do
                break;
        }

        if( p_txInfo->txStatus == PRVLORA_MAC_TX_RESULT_PENDING )
        {
            // start timer for waiting response timing
            PrivateLoRaRadioSleepWarm();

            // set timer
            txRspStartTime = PrivateLoRaMacGetWaitTimeRx2TxRes();
            timeOffset  = TimerGetCurrentTime();
            timeOffset -= p_rxDoneInfo->rxDoneTime;
            txRspStartTime = txRspStartTime - (int32_t)timeOffset;
            if( txRspStartTime < (int32_t)0 )
            {
                // fail-safe to avoid waiting an huge amount of time
                txRspStartTime = 0;
            }
            TimerSetValue( &( p_timerInfo->timerTxRspStart ), (uint32_t)txRspStartTime );
            TimerStart( &( p_timerInfo->timerTxRspStart ) );

            // go to next state
            PrvLoRaMacMng.state = PRVLORA_MAC_STATE_TX_WAITING;
        }
    }

    // cleanup
    memset1( (uint8_t *)&( p_rxInfo->rxFrameCtrl ), 0x00, sizeof(PrvLoRaFrameMhdrFrmCtrl_t) );
}

/*!
 * Process radio event; RxError/RxTimeout
 */
static void PrivateLoRaMacProcessRadioRxErrorTimeout( bool isRxError )
{
    PrvLoRaTxInfo_t     *p_txInfo;

    // initial check (state)
    if( ( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_RUNNING ) &&
        ( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_CONTINUOUS_RX ) )
    {
        // Unexpected radio event
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_FAULT;
        return;
    }

    // init
    p_txInfo = &( PrvLoRaMacMng.txInfo );

    // update state
    PrvLoRaMacMng.state = PRVLORA_MAC_STATE_RX_FAILED;

    // check ack request
    if( p_txInfo->txPacket.frame.frameMhdr.frameControl.ackRequest == 1 )
    {
        p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_NOACK;
    }
}

//--------------------------------------------------------------------------------------------------
// Handle/Process timer event

/*!
 * Timer callback : time to start RxWin
 */
static void PrivateLoRaMacTimerCbRxWinStart( void )
{
    PrvLoRaMacMng.timerInfo.timerEvent.events.rxWinStart = 1;
}

/*!
 * Timer callback : time to start TxResponse
 */
static void PrivateLoRaMacTimerCbTxResponseStart( void )
{
    PrvLoRaMacMng.timerInfo.timerEvent.events.txRspStart = 1;
}

/*!
 * Handle timer event
 */
static void PrivateLoRaMacHandleTimerEvents( void )
{
    PrvLoRaTimerInfo_t      *p_timerInfo;
    PrvLoRaTimerEvent_t     timerEvent;

    p_timerInfo = &( PrvLoRaMacMng.timerInfo );

    CRITICAL_SECTION_BEGIN();
    timerEvent.evtValue = p_timerInfo->timerEvent.evtValue;
    p_timerInfo->timerEvent.evtValue = 0;
    CRITICAL_SECTION_END();

    if( timerEvent.evtValue != 0 )
    {
        if( timerEvent.events.rxWinStart != 0 )
        {
            PrivateLoRaMacTimerProcessRxWinStart();
        }
        if( timerEvent.events.txRspStart != 0 )
        {
            PrivateLoRaMacTimerProcessTxResponseStart();
        }
    }
}

/*!
 * Process timer event; start RxWin
 */
static void PrivateLoRaMacTimerProcessRxWinStart( void )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaRadioRxParams_t  *p_rxParams;

    // initial check (state)
    if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_RX_WAITING )
    {
        // Unexpected radio event
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_FAULT;
        return;
    }

    // init
    p_rxParams = &( PrvLoRaMacMng.rxInfo.rxParams );

    // RxON
    PrivateLoRaRadioWakeup();
    ret = PrivateLoRaRadioStartRx( p_rxParams, false );
    if( ret == PRVLORA_STATUS_OK )
    {
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_RX_RUNNING;
    }
    else
    {
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_FAULT;
    }
}

/*!
 * Process timer event; start TxResponse
 */
static void PrivateLoRaMacTimerProcessTxResponseStart( void )
{
    PrvLoRaStatus_t     ret;
    PrvLoRaTxInfo_t     *p_txInfo;

    // initial check (state)
    if( PrvLoRaMacMng.state != PRVLORA_MAC_STATE_TX_WAITING )
    {
        // Unexpected radio event
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_FAULT;
        return;
    }

    // init
    p_txInfo = &( PrvLoRaMacMng.txInfo );

    // send frame
    ret = PrivateLoRaMacTxFrameSend();

    // go to next state
    if( ret == PRVLORA_STATUS_OK )
    {
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_TX_RUNNING;
    }
    else
    {
        switch( ret )
        {
            case PRVLORA_STATUS_RADIO_CHANNEL_BUSY:
                p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_CHANNELBUSY;
                break;

            case PRVLORA_STATUS_RADIO_DUTYCYCLE_RESTRICTED:
                p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_DUTYCYCLE_RESTRICTED;
                break;

            default:
                p_txInfo->txStatus = PRVLORA_MAC_TX_RESULT_RADIO_ERROR;
                break;
        }
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_TX_FAILED;
    }
}

//--------------------------------------------------------------------------------------------------
// Handle/Process private LoRa state

/*
 * Post process for each state
 */
static void PrivateLoRaMacStateProcess( void )
{
    bool                isBeIdle;
    PrvLoRaTxInfo_t     *p_txInfo;
    PrvLoRaRxInfo_t     *p_rxInfo;

    // init
    isBeIdle = false;
    p_txInfo = &( PrvLoRaMacMng.txInfo );
    p_rxInfo = &( PrvLoRaMacMng.rxInfo );

    switch( PrvLoRaMacMng.state )
    {
        case PRVLORA_MAC_STATE_TX_FAILED:
        case PRVLORA_MAC_STATE_RX_RCVD:
        case PRVLORA_MAC_STATE_RX_FAILED:
        case PRVLORA_MAC_STATE_MLME_CFM_ONLY:
        case PRVLORA_MAC_STATE_FAULT:
            if( p_txInfo->txStatus != PRVLORA_MAC_TX_RESULT_NONE )
            {
                PrivateLoRaMacTxFrameConfirm();
            }
            // cleanup rx
            p_rxInfo->p_rxCmdPayload = NULL;

            isBeIdle = true;
            break;

        default:
            break;
    }

    if( isBeIdle == true )
    {
        PrivateLoRaMacStateToIdle();
    }
}

/*!
 * Update state to idle or continuous-rx
 */
static PrvLoRaStatus_t PrivateLoRaMacStateToIdle( void )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIbParams_t       *p_ibParams;
    PrvLoRaRadioRxParams_t  *p_rxParams;

    // init
    ret        = PRVLORA_STATUS_OK;
    p_ibParams = &( PrvLoRaMacMng.ibParams );
    p_rxParams = &( PrvLoRaMacMng.rxInfo.rxParams );

    // set state to idle
    if( p_ibParams->rxOnWhenIdle == true )
    {
        // set radio to continuous RX
        PrivateLoRaRadioWakeup();
        ret = PrivateLoRaRadioStartRx( p_rxParams, true );
        if( ret != PRVLORA_STATUS_OK )
        {
            // fail-safe; set radio to sleep
            PrivateLoRaRadioSleepCold();
        }

        // update state
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_CONTINUOUS_RX;
    }
    else
    {
        // set radio to sleep
        PrivateLoRaRadioSleepCold();

        // update state
        PrvLoRaMacMng.state = PRVLORA_MAC_STATE_IDLE;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
//

/*
 * Notify to upper (when idle)
 */
static void PrivateLoRaMacNotifyToUpper( void )
{
    PrvLoRaPrimitives_t         *p_primitives;
    PrvLoRaStatus_t             funcRet;
    PrvLoRaNotification_t       notify, *p_notify;
    PrvLoRaRemoteDevUpdated_t   *p_updtRmtDevNty;

    // init for check
    p_primitives = &( PrvLoRaMacMng.primitives );

    // initial check
    if( p_primitives->PrvLoRaMacNotification == NULL )
    {
        return;  // fail-safe
    }
    // initial check (state)
    if( ( PrvLoRaMacMng.state <= _PRVLORA_MAC_STATE_INACTIVE ) ||
        ( PrvLoRaMacMng.state >= _PRVLORA_MAC_STATE_BUSY ) )
    {
        return;  // nothing to do
    }

    // init
    // (note; same with PrvLoRaRemoteDevUpdated_t and PrvLoRaNotifyUpdatedRemoteDev_t)
    p_updtRmtDevNty = (PrvLoRaRemoteDevUpdated_t *)&( notify.nty.updtRemoteDevNty );
    p_primitives    = &( PrvLoRaMacMng.primitives );
    p_notify        = NULL;

    // check updating remote dev info
    funcRet = PrivateLoRaRemoteDevGetUpdatedElement( p_updtRmtDevNty );
    if( funcRet == PRVLORA_STATUS_OK )
    {
        p_notify             = &notify;
        p_notify->notifyType = PRVLORA_NOTIFY_UPDATE_REMOTEDEV;
    }

    if( p_notify != NULL )
    {
        p_primitives->PrvLoRaMacNotification( p_notify );
    }
}

//--------------------------------------------------------------------------------------------------
//

static int32_t PrivateLoRaMacGetWaitTimeTx2Rx( void )
{
    int32_t                 retVal;
    uint8_t                 radioClkSel;
    PrvLoRaRadioRxParams_t  *p_rxParams;

    // init
    p_rxParams = &( PrvLoRaMacMng.rxInfo.rxParams );

    retVal  = (int32_t)PRVLORA_MAC_WAITTIME_TX2RX + p_rxParams->windowOffset;
    retVal -= (int32_t)PRVLORA_CONFIG_TRXADJUST_TX2RX;

    radioClkSel = SX126xGetClockSelect();
    if ( radioClkSel == RADIO_CLOCK_TCXO_SEL )
    {
        retVal -= (int32_t)( ( RP_TCXO_STAB_TIME * 15.625 ) / 1000.0 );
    }

    return retVal;
}

static int32_t PrivateLoRaMacGetWaitTimeRx2TxRes( void )
{
    int32_t             retVal, tmpVal;
    uint8_t             radioClkSel;
    PrvLoRaTxInfo_t     *p_txInfo;

    // init
    p_txInfo = &( PrvLoRaMacMng.txInfo );
    tmpVal   = (int32_t)0;

    // calculate adjust time depending on tx data length
    tmpVal  = (int32_t)( (int32_t)PRVLORA_CONFIG_TRXADJUST_RX2TXRES_SLOPE * (int32_t)p_txInfo->txPacket.frameLength );
    tmpVal += (int32_t)( PRVLORA_CONFIG_TRXADJUST_RX2TXRES_INTERCEPT );
    tmpVal  = (int32_t)( tmpVal / PRVLORA_CONFIG_TRXADJUST_RX2TXRES_RATIO );

    retVal  = PRVLORA_MAC_WAITTIME_RX2TXRES;
    retVal -= PRVLORA_CONFIG_TRXADJUST_RX2TXRES_FIXED;
    retVal -= tmpVal;

    radioClkSel = SX126xGetClockSelect();
    if ( radioClkSel == RADIO_CLOCK_TCXO_SEL )
    {
        retVal -= (int32_t)( ( RP_TCXO_STAB_TIME * 15.625 ) / 1000.0 );
    }

    return retVal;
}
