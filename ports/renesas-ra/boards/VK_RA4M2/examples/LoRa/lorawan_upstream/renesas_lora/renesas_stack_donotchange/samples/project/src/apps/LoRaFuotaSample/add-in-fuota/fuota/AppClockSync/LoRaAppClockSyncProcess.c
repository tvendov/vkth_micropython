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

#include "LoRaAppClockSyncProcess.h"

/*** Application layer clock synchronization command ***/
/* macros */
#define CLKSNC_CID_PACKAGE_VERSION_REQ          0x00
#define CLKSNC_CID_PACKAGE_VERSION_ANS          0x00
#define CLKSNC_CID_APP_TIME_REQ                 0x01
#define CLKSNC_CID_APP_TIME_ANS                 0x01
#define CLKSNC_CID_APP_TIME_PERIODICITY_REQ     0x02
#define CLKSNC_CID_APP_TIME_PERIODICITY_ANS     0x02
#define CLKSNC_CID_FORCE_RESYNC_CMD             0x03

#define CLKSNC_PLEN_PACKAGE_VERSION_REQ         (0)
#define CLKSNC_PLEN_PACKAGE_VERSION_ANS         (2)
#define CLKSNC_PLEN_APP_TIME_REQ                (5)
#define CLKSNC_PLEN_APP_TIME_ANS                (5)
#define CLKSNC_PLEN_APP_TIME_PERIODICITY_REQ    (1)
#define CLKSNC_PLEN_APP_TIME_PERIODICITY_ANS    (5)
#define CLKSNC_PLEN_FORCE_RESYNC_CMD            (1)

#define CLKSNC_RXCMD_QUEUENUM                   (3)

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/* command parameter */
#define CLKSNC_PARAM_TIMECORRECTION_MAX         ( (int32_t)(0x7FFFFFFF) )
#define CLKSNC_PARAM_TIMECORRECTION_MIN         ( (int32_t)(0x80000000) )
#endif

/* typedef */
typedef struct {
    uint8_t     periodicity;
} ClkSnc_DeviceAppTimePeriodReq_t;

typedef struct {
    uint8_t     nbTrans;
} ClkSnc_ForceDeviceResyncCmd_t;

typedef struct
{
    uint8_t     cid;
    union {
        ClkSnc_DeviceAppTimePeriodReq_t devAppTimePeriodReq;
        ClkSnc_ForceDeviceResyncCmd_t   forceDevResyncCmd;
    } reqCmdParam;
} ClkSnc_RxCmd_t;

typedef struct
{
    ClkSnc_RxCmd_t  rxCmdQueue[ CLKSNC_RXCMD_QUEUENUM ];
    uint8_t         numCmd;
} ClkSnc_RxCmdQueue_t;

/* global variable */
ClkSnc_RxCmdQueue_t ClkSncCmdQueue;

/* function prototype */
static ClkSncStatus_t LoRaClkSncHandlePackageVersionReq( uint8_t *p_buffer, size_t length );
static ClkSncStatus_t LoRaClkSncHandleDeviceAppTimePeriodicityReq( uint8_t *p_buffer, size_t length );
static ClkSncStatus_t LoRaClkSncHandleForceResyncCmd( uint8_t *p_buffer, size_t length );
static ClkSncStatus_t LoRaClkSncHandleAppTimeAns( uint8_t *p_buffer, size_t length );

static ClkSncStatus_t LoRaClkSncProcessPackageVersionReq( uint8_t        *p_buffer, 
                                                          uint8_t        *p_payloadLen, 
                                                          uint8_t        bufferMaxSize, 
                                                          uint32_t       *p_txDelayMs,
                                                          ClkSnc_RxCmd_t *p_rcCmd/*nouse*/ );
static ClkSncStatus_t LoRaClkSncProcessDeviceAppTimePeriodicityReq( uint8_t        *p_buffer, 
                                                                    uint8_t        *p_payloadLen, 
                                                                    uint8_t        bufferMaxSize, 
                                                                    uint32_t       *p_txDelayMs,
                                                                    ClkSnc_RxCmd_t *p_rcCmd );
static ClkSncStatus_t LoRaClkSncProcessForceResyncCmd( uint8_t        *p_buffer/*nouse*/, 
                                                       uint8_t        *p_payloadLen/*nouse*/, 
                                                       uint8_t        bufferMaxSize/*nouse*/, 
                                                       uint32_t       *p_txDelayMs,
                                                       ClkSnc_RxCmd_t *p_rcCmd );

/*** Application Time ***/
typedef struct {
    uint32_t    correctTime;         // AppTime correction
    SysTime_t   appTimeLastReqTime;  // Last request time of AppTime
} ClkSnc_AppTimeMng_t;

ClkSnc_AppTimeMng_t ClkSyncAppTimeMng;

/* function prototype */
static SysTime_t LoRaClkSncGetCurrentSysTime( void );
static void LoRaClkSncUpdateTime( int32_t correctTimeSec );


/*** Timer / Periodic App_Sync_Req ***/
#define CLKSNC_TIMER_EVENT_NONE                     0x00
#define CLKSNC_TIMER_EVENT_APPTIMEREQ_TIMING        0x01

#define CLKSNC_APPTIMEREQ_STATE_INIT                0x00
#define CLKSNC_APPTIMEREQ_STATE_TIMER_EVENT         0x01  // transient state
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define CLKSNC_APPTIMEREQ_STATE_CONT_TIMECORRECT    0x02
#endif
#define CLKSNC_APPTIMEREQ_STATE_SENDING             0x10

typedef struct {
    uint8_t         tokenReq;
    uint8_t         state;
    uint8_t         event;
    /*--*/
    uint32_t        periodTimerSec;
    TimerEvent_t    periodTimerObj;
    /*--*/
    uint8_t         forceNbTrans;
} ClkSnc_AppTimeReqMng_t;

ClkSnc_AppTimeReqMng_t  ClkSyncAppTimeReqMng;

/* function prototype */
static void LoRaClkSncAppTimeReqPeriodTimerSet( void );
static void LoRaClkSncOnAppTimeReqPeriodTimerEvent( void );

static void LoRaClkSncAppTimeReqForceTimerSet( void );

/*** Event; notify to upper ***/
LoRaClkSncEventCb_t ClkSncEventCbFuncs = {0};

/*** IB ***/
typedef struct
{
    uint32_t    timeReqPeriodSec;
    uint8_t     timeReqReqPeriodicity;
    uint8_t     timeReqMinPeriodicity;
    uint8_t     timeReqMaxPeriodicity;
    uint8_t     timeAnsRequired;
    uint8_t     forceSyncPeriodSec;
} ClkSnc_IB_params_t;

ClkSnc_IB_params_t  ClkSncIBParams;

/*** ClockSync state ***/
#define CLKSNC_STATE_NONE           0x00
#define CLKSNC_STATE_INITIALIZED    0x01
#define CLKSNC_STATE_RUNNING_NOSYNC 0x02
#define CLKSNC_STATE_RUNNING        0x03
uint8_t ClkSncState = CLKSNC_STATE_NONE;


/*!
 * ClockSync initialization
 */
ClkSncStatus_t LoRaClockSyncInit( LoRaClkSncEventCb_t *p_clkSncEventCb )
{
    // check
    if( p_clkSncEventCb == NULL )
    {
        return CLKSNC_STATUS_PARAMETER_INVALID;
    }
    if( p_clkSncEventCb->LoRaClkSncAppTimeReqCb == NULL )
    {
        return CLKSNC_STATUS_PARAMETER_INVALID;
    }

    /* IB */
    memset1( (uint8_t *)&ClkSncIBParams, 0x00, sizeof(ClkSnc_IB_params_t) );
    ClkSncIBParams.timeReqPeriodSec      = CLKSNC_IB_INIT_TIMEREQ_PERIOD_SEC;
    ClkSncIBParams.timeReqReqPeriodicity = (uint8_t)CLKSNC_IB_INIT_TIMEREQ_REQ_PERIODICITY;
    ClkSncIBParams.timeReqMinPeriodicity = CLKSNC_IB_INIT_TIMEREQ_MIN_PERIODICITY;
    ClkSncIBParams.timeReqMaxPeriodicity = CLKSNC_IB_INIT_TIMEREQ_MAX_PERIODICITY;
    ClkSncIBParams.timeAnsRequired       = CLKSNC_IB_INIT_TIMEANS_REQUIRED;
    ClkSncIBParams.forceSyncPeriodSec    = CLKSNC_IB_INIT_FORCESYNC_PERIOD_SEC;

    /* for command */
    memset1( (uint8_t *)&ClkSncCmdQueue, 0x00, sizeof(ClkSnc_RxCmdQueue_t) );

    /* for timer / Periodic App_Sync_Req */
    if( ClkSncState != CLKSNC_STATE_NONE )
    {
        TimerStop( &( ClkSyncAppTimeReqMng.periodTimerObj ) );
    }

    memset1( (uint8_t *)&ClkSyncAppTimeReqMng, 0x00, sizeof(ClkSnc_AppTimeReqMng_t) );
    TimerInit( &( ClkSyncAppTimeReqMng.periodTimerObj ), LoRaClkSncOnAppTimeReqPeriodTimerEvent );

    if( ClkSncState == CLKSNC_STATE_NONE )
    {
        LoRaClockSyncResetCorrectTime();  // init time correction (note: no need to init appTimeLastReqTime)
    }

    /* event */
    if( p_clkSncEventCb != &ClkSncEventCbFuncs )  // means upper-layer requests initialization
    {
        memcpy1( (uint8_t *)&ClkSncEventCbFuncs, (uint8_t *)p_clkSncEventCb, sizeof(LoRaClkSncEventCb_t) );
    }

    ClkSncState = CLKSNC_STATE_INITIALIZED;
    return CLKSNC_STATUS_OK;
}

/*!
 * ClockSync start
 */
ClkSncStatus_t LoRaClockSyncStart( void )
{
    ClkSncStatus_t  res;

    // init
    res = CLKSNC_STATUS_ERROR;

    if( ClkSncState == CLKSNC_STATE_INITIALIZED )
    {
        // notify to the upper
        (*ClkSncEventCbFuncs.LoRaClkSncAppTimeReqCb)( false );
        ClkSncState = CLKSNC_STATE_RUNNING_NOSYNC;  // Run ClockSync, no sync

        res = CLKSNC_STATUS_OK;
    }

    return res;
}

/*!
 * ClockSync stop
 */
void LoRaClockSyncStop( void )
{
    ClkSnc_IB_params_t  ibParams;

    if( ClkSncState != CLKSNC_STATE_NONE )
    {
        memcpy1( (uint8_t *)&ibParams, (uint8_t *)&ClkSncIBParams, sizeof(ClkSnc_IB_params_t) );
        LoRaClockSyncInit( &ClkSncEventCbFuncs );
        memcpy1( (uint8_t *)&ClkSncIBParams, (uint8_t *)&ibParams, sizeof(ClkSnc_IB_params_t) );
    }
}

/*!
 * MCPS-Indication event function for ClockSync
 */
ClkSncStatus_t LoRaClockSyncMcpsIndication( McpsIndication_t *p_mcpsIndication )
{
    ClkSncStatus_t  status, funcRet;
    uint8_t         *p_buffer;
    uint8_t         bufferSize;
    bool            isProcAppTimeAns;

    // ClockSync is not running
    if( ClkSncState <= CLKSNC_STATE_INITIALIZED )
    {
        return CLKSNC_STATUS_ERROR;
    }

    // Reject error indication
    if ( p_mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK )
    {
        return CLKSNC_STATUS_ERROR;
    }
    // RemoteMulticastSetup messages SHALL NOT be sent using multicast.
    if( (p_mcpsIndication->McpsIndication != MCPS_UNCONFIRMED) &&
        (p_mcpsIndication->McpsIndication != MCPS_CONFIRMED) )
    {
        return CLKSNC_STATUS_COMMAND_ERROR;
    }

    // init
    status     = CLKSNC_STATUS_OK;
    p_buffer   = p_mcpsIndication->Buffer;
    bufferSize = p_mcpsIndication->BufferSize;

    memset1( (uint8_t *)&ClkSncCmdQueue, 0x00, sizeof(ClkSnc_RxCmdQueue_t) );

    isProcAppTimeAns = false;

    // Get RemoteMulticastSetup commands
    while( bufferSize > 0 )
    {
        switch( *p_buffer )  //= CID
        {
            //-- Request / force commands -----

            case CLKSNC_CID_PACKAGE_VERSION_REQ:
                funcRet = LoRaClkSncHandlePackageVersionReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == CLKSNC_STATUS_OK )
                {
                    bufferSize -= (CLKSNC_PLEN_PACKAGE_VERSION_REQ + 1);  // +1 = CID length
                    p_buffer   += (CLKSNC_PLEN_PACKAGE_VERSION_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case CLKSNC_CID_APP_TIME_PERIODICITY_REQ:
                funcRet = LoRaClkSncHandleDeviceAppTimePeriodicityReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == CLKSNC_STATUS_OK )
                {
                    bufferSize -= (CLKSNC_PLEN_APP_TIME_PERIODICITY_REQ + 1);  // +1 = CID length
                    p_buffer   += (CLKSNC_PLEN_APP_TIME_PERIODICITY_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case CLKSNC_CID_FORCE_RESYNC_CMD:
                funcRet = LoRaClkSncHandleForceResyncCmd( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == CLKSNC_STATUS_OK )
                {
                    bufferSize -= (CLKSNC_PLEN_FORCE_RESYNC_CMD + 1);  // +1 = CID length
                    p_buffer   += (CLKSNC_PLEN_FORCE_RESYNC_CMD + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            //-- answer command -----

            case CLKSNC_CID_APP_TIME_ANS:
                funcRet = LoRaClkSncHandleAppTimeAns( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == CLKSNC_STATUS_OK )
                {
                    bufferSize -= (CLKSNC_PLEN_APP_TIME_ANS + 1);  // +1 = CID length
                    p_buffer   += (CLKSNC_PLEN_APP_TIME_ANS + 1);

                    isProcAppTimeAns = true;  // AppTimeAns was processed.
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            default:
                bufferSize = 0;  // exit from while() loop
                break;
        }
    }

    if( ( ClkSncCmdQueue.numCmd == 0 ) && ( isProcAppTimeAns == false ) )
    {
        status = CLKSNC_STATUS_COMMAND_ERROR;
    }

    return status;
}

/*!
 * Process event of ClockSync
 */
void LoRaClockSyncProcessCommand( uint8_t  *p_buffer, 
                                  uint8_t  *p_payloadLen, 
                                  uint8_t  bufferMaxSize, 
                                  uint32_t *p_txDelayMs,
                                  bool     *p_isRetransEn )
{
    ClkSncStatus_t  funcRet;
    ClkSnc_RxCmd_t  *p_rxCmd;
    uint8_t         payloadLen, payloadLenTotal;
    uint32_t        txDelayMs, tmpTxDelayMs;
    uint8_t         i;
    bool            isRetransEn;

    // ClockSync is not running
    if( ClkSncState <= CLKSNC_STATE_INITIALIZED )
    {
        return;
    }

    // init
    payloadLenTotal = 0;
    txDelayMs       = 0;
    isRetransEn     = true;  // enable to retrans command response

    // Response(Answer)
    for( i = 0; i < ClkSncCmdQueue.numCmd; i++ )
    {
        // init (loop)
        p_rxCmd      = &( ClkSncCmdQueue.rxCmdQueue[ i ] );
        tmpTxDelayMs = 0;

        switch( p_rxCmd->cid )
        {
            case CLKSNC_CID_PACKAGE_VERSION_REQ:
                funcRet = LoRaClkSncProcessPackageVersionReq( p_buffer, 
                                                              &payloadLen, 
                                                              bufferMaxSize, 
                                                              &tmpTxDelayMs,
                                                              p_rxCmd );
                break;

            case CLKSNC_CID_APP_TIME_PERIODICITY_REQ:
                funcRet = LoRaClkSncProcessDeviceAppTimePeriodicityReq( p_buffer, 
                                                                        &payloadLen, 
                                                                        bufferMaxSize, 
                                                                        &tmpTxDelayMs,
                                                                        p_rxCmd );
                if( funcRet == CLKSNC_STATUS_OK )
                {
                    isRetransEn = false;  // disable retransmission because current time is included in it
                }
                break;

            case CLKSNC_CID_FORCE_RESYNC_CMD:
                funcRet = LoRaClkSncProcessForceResyncCmd( p_buffer, 
                                                           &payloadLen, 
                                                           bufferMaxSize, 
                                                           &tmpTxDelayMs,
                                                           p_rxCmd );
                if( funcRet == CLKSNC_STATUS_OK )
                {
                    isRetransEn = false;  // disable retransmission because current time is included in it
                }
                break;

            default:
                funcRet = CLKSNC_STATUS_COMMAND_ERROR;
                break;
        }

        if( funcRet == CLKSNC_STATUS_OK )
        {
            p_buffer        += payloadLen;
            payloadLenTotal += payloadLen;
            bufferMaxSize   -= payloadLen;

            if( txDelayMs < tmpTxDelayMs )
            {
                txDelayMs = tmpTxDelayMs;
            }
        }
        else
        {
            break;  // exit for(i) loop
        }
    }

    (*p_payloadLen)  = payloadLenTotal;
    (*p_txDelayMs)   = txDelayMs;
    (*p_isRetransEn) = isRetransEn;
}

/*!
 * Process timer interrupt of ClockSync
 */
void LoRaClockSyncProcessEvent( void )
{
    bool        isForceResync;
    uint8_t     event;

    // ClockSync is not running
    if( ClkSncState <= CLKSNC_STATE_INITIALIZED )
    {
        return;
    }

    CRITICAL_SECTION_BEGIN();
    event = ClkSyncAppTimeReqMng.event;
    ClkSyncAppTimeReqMng.event = CLKSNC_TIMER_EVENT_NONE;
    CRITICAL_SECTION_END();

    if( event == CLKSNC_TIMER_EVENT_APPTIMEREQ_TIMING )
    {
        if( ClkSyncAppTimeReqMng.state == CLKSNC_APPTIMEREQ_STATE_INIT )
        {
            ClkSyncAppTimeReqMng.state = CLKSNC_APPTIMEREQ_STATE_TIMER_EVENT;
        }
    }

    switch( ClkSyncAppTimeReqMng.state )
    {
        case CLKSNC_APPTIMEREQ_STATE_TIMER_EVENT:
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        case CLKSNC_APPTIMEREQ_STATE_CONT_TIMECORRECT:
#endif
            ClkSyncAppTimeReqMng.state = CLKSNC_APPTIMEREQ_STATE_INIT;

            // notify to the upper
            isForceResync = false;
            if( ClkSyncAppTimeReqMng.forceNbTrans > 0 )
            {
                isForceResync = true;
            }
            (*ClkSncEventCbFuncs.LoRaClkSncAppTimeReqCb)( isForceResync );
            break;

        // case CLKSNC_APPTIMEREQ_STATE_INIT:
        // case CLKSNC_APPTIMEREQ_STATE_SENDING:
        default:
            break;
    }
}

/*!
 * Returns whether ClockSync is idle
 * If ClockSync is not idle (return false), LoRaClockSyncProcessEvent() call is required
 */
bool LoRaClockSyncIsIdle( void )
{
    bool    bRet;

    // init 
    bRet = true;  // (init) idle - function call is not required

    if( ClkSncState > CLKSNC_STATE_INITIALIZED )
    {
        if( ClkSyncAppTimeReqMng.event != CLKSNC_TIMER_EVENT_NONE )
        {
            bRet = false;  // not idle - function call is required
        }
    }

    return bRet;
}

/*!
 * Process AppTimeReq
 */
ClkSncStatus_t LoRaClockSyncAppTimeReq( uint8_t *p_buffer, uint8_t *p_payloadLen, uint8_t bufferMaxSize )
{
    uint32_t    deviceTimeSec;
    uint8_t     ansRequired;
#ifdef DEBUG_CLKSNC
    bool        dbgIsForce = false;
#endif

    // ClockSync is not running
    if( ClkSncState <= CLKSNC_STATE_INITIALIZED )
    {
        return CLKSNC_STATUS_ERROR;
    }

    // length check
    if( bufferMaxSize < (CLKSNC_PLEN_APP_TIME_ANS + 1) )  // +1 = CID length
    {
        return CLKSNC_STATUS_LENGTH_ERROR;
    }

    // process
    ClkSyncAppTimeMng.appTimeLastReqTime = LoRaClkSncGetCurrentSysTime();  // keep request time
    deviceTimeSec = ClkSyncAppTimeMng.appTimeLastReqTime.Seconds;

    if( ClkSyncAppTimeReqMng.forceNbTrans > 0 )
    {
        ansRequired = 0;  // only out of synchronization
#ifdef DEBUG_CLKSNC
        dbgIsForce = true;
#endif
    }
    else
    {
        if( ClkSncState == CLKSNC_STATE_RUNNING_NOSYNC )
        {
            ansRequired = 1;  // force to request answer if AppTime is not synchronized yet.
        }
        else
        {
            ansRequired = ClkSncIBParams.timeAnsRequired;
        }
    }

    // make AppTimeReq
    p_buffer[0] = CLKSNC_CID_APP_TIME_REQ;
    p_buffer[1] = (  deviceTimeSec & 0x000000FF );
    p_buffer[2] = ( (deviceTimeSec & 0x0000FF00) >> 8 );
    p_buffer[3] = ( (deviceTimeSec & 0x00FF0000) >> 16 );
    p_buffer[4] = ( (deviceTimeSec & 0xFF000000) >> 24 );
    p_buffer[5] = ( ansRequired << 4 ) |                     // bit4   AnsRequired
                  ( ClkSyncAppTimeReqMng.tokenReq & 0x0F );  // bit3-0 TokenReq

    (*p_payloadLen) = CLKSNC_PLEN_APP_TIME_ANS + 1;  // +1 = CID length

    ClkSyncAppTimeReqMng.state = CLKSNC_APPTIMEREQ_STATE_SENDING;

#ifdef DEBUG_CLKSNC
    print( "*CLKSNC:AppTimeReq" );
    if( dbgIsForce == true )
    {
        print( "(force)" );
    }
    print_newline();
    print( "*CLKSNC:  DeviceTime=0x" );
    print_hex( deviceTimeSec, 8 );
    print( ", AnsRequired=" );
    print_dec( ansRequired, 3, '\0' );
    print( ", TokenReq=0x" );
    print_hex( ClkSyncAppTimeReqMng.tokenReq, 2 );
    print_newline();
#endif

    return CLKSNC_STATUS_OK;
}

/*!
 * Uplink result notification from upper layer
 */
void LoRaClockSyncSendCompCommand( bool isSuccess )
{
    if( ClkSyncAppTimeReqMng.state == CLKSNC_APPTIMEREQ_STATE_SENDING )
    {
        ClkSyncAppTimeReqMng.state = CLKSNC_APPTIMEREQ_STATE_INIT;

        if( isSuccess == true )
        {
            if( ClkSyncAppTimeReqMng.forceNbTrans > 0 )
            {
                ClkSyncAppTimeReqMng.forceNbTrans--;
            }
        }

        if( ClkSyncAppTimeReqMng.forceNbTrans > 0 )
        {
            LoRaClkSncAppTimeReqForceTimerSet();
        }
        else
        {
            LoRaClkSncAppTimeReqPeriodTimerSet();
        }
    }
}

/*!
 * IB Get Request
 */
ClkSncStatus_t LoRaClockSyncIbGetRequest( uint8_t ib, void *vpVal )
{
    ClkSncStatus_t  ret;

    // ClockSync is not initialized
    if( ClkSncState == CLKSNC_STATE_NONE )
    {
        return CLKSNC_STATUS_ERROR;
    }

    if( vpVal == NULL )
    {
        return CLKSNC_STATUS_PARAMETER_INVALID;
    }

    // init
    ret = CLKSNC_STATUS_OK;

    switch( ib )
    {
        case CLKSNC_IB_TIMEREQ_PERIOD_SEC:
            *( (uint32_t *)vpVal ) = ClkSncIBParams.timeReqPeriodSec;
            break;

        case CLKSNC_IB_TIMEREQ_REQ_PERIODICITY:
            *( (uint8_t *)vpVal ) = ClkSncIBParams.timeReqReqPeriodicity;
            break;

        case CLKSNC_IB_TIMEREQ_MIN_PERIODICITY:
            *( (uint8_t *)vpVal ) = ClkSncIBParams.timeReqMinPeriodicity;
            break;

        case CLKSNC_IB_TIMEREQ_MAX_PERIODICITY:
            *( (uint8_t *)vpVal ) = ClkSncIBParams.timeReqMaxPeriodicity;
            break;

        case CLKSNC_IB_TIMEANS_REQUIRED:
            *( (uint8_t *)vpVal ) = ClkSncIBParams.timeAnsRequired;
            break;

        case CLKSNC_IB_FORCESYNC_PERIOD_SEC:
            *( (uint8_t *)vpVal ) = ClkSncIBParams.forceSyncPeriodSec;
            break;

        default:
            ret = CLKSNC_STATUS_SERVICE_UNKNOWN;
            break;
    }

    return ret;
}

/*!
 * IB Set Request
 */
ClkSncStatus_t LoRaClockSyncIbSetRequest( uint8_t ib, void *vpVal )
{
    ClkSncStatus_t  ret;
    uint32_t        tmpVal32;
    uint8_t         tmpVal8;

    // ClockSync is not initialized
    if( ClkSncState == CLKSNC_STATE_NONE )
    {
        return CLKSNC_STATUS_ERROR;
    }

    if( vpVal == NULL )
    {
        return CLKSNC_STATUS_PARAMETER_INVALID;
    }

    // init
    ret = CLKSNC_STATUS_OK;

    switch( ib )
    {
        case CLKSNC_IB_TIMEREQ_PERIOD_SEC:
            tmpVal32 = *( (uint32_t *)vpVal );
            if( ( tmpVal32 == 0 ) ||
                ( ( tmpVal32 >= 10 ) && ( tmpVal32 <= (uint32_t)0x00418930 ) ) )  // 0x418930 * 1000(msec) = 0xFFFFE380
            {
                ClkSncIBParams.timeReqPeriodSec = tmpVal32;
                // restart timer
                ClkSncIBParams.timeReqReqPeriodicity = (uint8_t)CLKSNC_IB_INIT_TIMEREQ_REQ_PERIODICITY;
                LoRaClkSncAppTimeReqPeriodTimerSet();
            }
            else
            {
                ret = CLKSNC_STATUS_PARAMETER_INVALID;
            }
            break;

        case CLKSNC_IB_TIMEREQ_MIN_PERIODICITY:
            tmpVal8 = *( (uint8_t *)vpVal );
            if( ( tmpVal8 <= ClkSncIBParams.timeReqMaxPeriodicity ) &&
                ( tmpVal8 < 0x10 ) )  // Period is 4bit
            {
                ClkSncIBParams.timeReqMinPeriodicity = tmpVal8;
            }
            else
            {
                ret = CLKSNC_STATUS_PARAMETER_INVALID;
            }
            break;

        case CLKSNC_IB_TIMEREQ_MAX_PERIODICITY:
            tmpVal8 = *( (uint8_t *)vpVal );
            if( ( tmpVal8 >= ClkSncIBParams.timeReqMinPeriodicity ) &&
                ( tmpVal8 < 0x10 ) )  // Period is 4bit
            {
                ClkSncIBParams.timeReqMaxPeriodicity = tmpVal8;
            }
            else
            {
                ret = CLKSNC_STATUS_PARAMETER_INVALID;
            }
            break;

        case CLKSNC_IB_TIMEANS_REQUIRED:
            tmpVal8 = *( (uint8_t *)vpVal );
            if( tmpVal8 != 0 )
            {
                ClkSncIBParams.timeAnsRequired = 1;
            }
            else
            {
                ClkSncIBParams.timeAnsRequired = 0;
            }
            break;

        case CLKSNC_IB_FORCESYNC_PERIOD_SEC:
            tmpVal8 = *( (uint8_t *)vpVal );
            if( tmpVal8 >= 10 )
            {
                ClkSncIBParams.forceSyncPeriodSec = tmpVal8;
            }
            else
            {
                ret = CLKSNC_STATUS_PARAMETER_INVALID;
            }
            break;

        // read only
        case CLKSNC_IB_TIMEREQ_REQ_PERIODICITY:
            ret = CLKSNC_STATUS_IB_READONLY;
            break;

        default:
            ret = CLKSNC_STATUS_SERVICE_UNKNOWN;
            break;
    }

    return ret;
}

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*!
 * Get ClockSync command payload size
 */
ClkSncStatus_t LoRaClockSyncGetRcvdCmdPayloadLen( uint8_t cid, uint8_t *p_cmdPayloadLen )
{
    ClkSncStatus_t  ret;

    // init
    ret = CLKSNC_STATUS_OK;

    switch( cid )
    {
        //-- Request / force commands -----
        case CLKSNC_CID_PACKAGE_VERSION_REQ:
            (*p_cmdPayloadLen) = CLKSNC_PLEN_PACKAGE_VERSION_REQ;
            break;
    
        case CLKSNC_CID_APP_TIME_PERIODICITY_REQ:
            (*p_cmdPayloadLen) = CLKSNC_PLEN_APP_TIME_PERIODICITY_REQ;
            break;
    
        case CLKSNC_CID_FORCE_RESYNC_CMD:
            (*p_cmdPayloadLen) = CLKSNC_PLEN_FORCE_RESYNC_CMD;
            break;
    
        //-- answer command -----
        case CLKSNC_CID_APP_TIME_ANS:
            (*p_cmdPayloadLen) = CLKSNC_PLEN_APP_TIME_ANS;
            break;
    
        default:
            ret = CLKSNC_STATUS_COMMAND_ERROR;
            break;
    }

    return ret;
}
#endif

/*!
 * Reset correct time for AppTime 
 *   it is used when DeviceTimeAns has received 
 *   after periodic AppTimeReq transmission.
 */
void LoRaClockSyncResetCorrectTime( void )
{
    ClkSyncAppTimeMng.correctTime = 0;

    if( ClkSncState == CLKSNC_STATE_RUNNING_NOSYNC )
    {
        ClkSncState = CLKSNC_STATE_RUNNING;   // change state before setting timer
        LoRaClkSncAppTimeReqPeriodTimerSet();
    }
}

/*!
 * Get AppTime
 */
uint32_t LoRaClockSyncGetAppTimeSec( void )
{
    SysTime_t   currentTime;

    // init
    currentTime.Seconds = 0;

    if( ClkSncState == CLKSNC_STATE_RUNNING )
    {
        currentTime = LoRaClkSncGetCurrentSysTime();
    }

    return currentTime.Seconds;
}

//--------------------------------------------------------------------------------------------------

/*!
 * check received command; PackageVersionReq
 */
static ClkSncStatus_t LoRaClkSncHandlePackageVersionReq( uint8_t *p_buffer, size_t length )
{
    ClkSnc_RxCmd_t   *p_rxCmd;

    // payload length check
    //  ... it has no payload

    // command queue check
    if( ClkSncCmdQueue.numCmd >= CLKSNC_RXCMD_QUEUENUM )
    {
        return CLKSNC_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( ClkSncCmdQueue.rxCmdQueue[ ClkSncCmdQueue.numCmd ] );

    // get parameter
    p_rxCmd->cid = CLKSNC_CID_PACKAGE_VERSION_REQ;

    ClkSncCmdQueue.numCmd++;

#ifdef DEBUG_CLKSNC
    print( "*CLKSNC:PackageVersionReq" );
    print_newline();
#endif

    return CLKSNC_STATUS_OK;
}

/*!
 * check received command; DeviceAppTimePeriodicityReq
 */
static ClkSncStatus_t LoRaClkSncHandleDeviceAppTimePeriodicityReq( uint8_t *p_buffer, size_t length )
{
    ClkSnc_RxCmd_t                  *p_rxCmd;
    ClkSnc_DeviceAppTimePeriodReq_t *p_cmdDevAppTimePeriodReq;

    // payload length check
    if( length < CLKSNC_PLEN_APP_TIME_PERIODICITY_REQ )
    {
        return CLKSNC_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( ClkSncCmdQueue.numCmd >= CLKSNC_RXCMD_QUEUENUM )
    {
        return CLKSNC_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( ClkSncCmdQueue.rxCmdQueue[ ClkSncCmdQueue.numCmd ] );
    p_cmdDevAppTimePeriodReq = (ClkSnc_DeviceAppTimePeriodReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = CLKSNC_CID_APP_TIME_PERIODICITY_REQ;
    p_cmdDevAppTimePeriodReq->periodicity = (*p_buffer) & 0x0F;  // bit7-4 = RFU
    //p_buffer++;

    ClkSncCmdQueue.numCmd++;

#ifdef DEBUG_CLKSNC
    print( "*CLKSNC:DeviceAppTimePeriodicityReq" );
    print_newline();
    print( "*CLKSNC:  Period=" );
    print_dec( p_cmdDevAppTimePeriodReq->periodicity, 3, '\0' );
    print_newline();
#endif

    return CLKSNC_STATUS_OK;
}

/*!
 * check received command; ForceDeviceResyncCmd
 */
static ClkSncStatus_t LoRaClkSncHandleForceResyncCmd( uint8_t *p_buffer, size_t length )
{
    ClkSnc_RxCmd_t                  *p_rxCmd;
    ClkSnc_ForceDeviceResyncCmd_t   *p_cmdForceDevResyncCmd;

    // payload length check
    if( length < CLKSNC_PLEN_FORCE_RESYNC_CMD )
    {
        return CLKSNC_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( ClkSncCmdQueue.numCmd >= CLKSNC_RXCMD_QUEUENUM )
    {
        return CLKSNC_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( ClkSncCmdQueue.rxCmdQueue[ ClkSncCmdQueue.numCmd ] );
    p_cmdForceDevResyncCmd = (ClkSnc_ForceDeviceResyncCmd_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = CLKSNC_CID_FORCE_RESYNC_CMD;
    p_cmdForceDevResyncCmd->nbTrans = (*p_buffer) & 0x07;  // bit7-3 = RFU
    //p_buffer++;

    ClkSncCmdQueue.numCmd++;

#ifdef DEBUG_CLKSNC
    print( "*CLKSNC:ForceDeviceResyncCmd" );
    print_newline();
    print( "*CLKSNC:  NbTransmissions=" );
    print_dec( p_cmdForceDevResyncCmd->nbTrans, 3, '\0' );
    print_newline();
#endif

    return CLKSNC_STATUS_OK;
}

/*!
 * check received command; AppTimeAns
 */
static ClkSncStatus_t LoRaClkSncHandleAppTimeAns( uint8_t *p_buffer, size_t length )
{
    int32_t     timeCorrection;
    uint8_t     tokenAns;

    // payload length check
    if( length < CLKSNC_PLEN_APP_TIME_ANS )
    {
        return CLKSNC_STATUS_LENGTH_ERROR;
    }

    // get parameter
    timeCorrection = ((uint32_t)p_buffer[3] << 24) | ((uint32_t)p_buffer[2] << 16) |
                     ((uint32_t)p_buffer[1] << 8)  |  (uint32_t)p_buffer[0];
    p_buffer += 4;

    tokenAns = (*p_buffer) & 0x0F;  // bit7-4 = RFU
    //p_buffer++;

    if( ClkSyncAppTimeReqMng.tokenReq == tokenAns )
    {
        // correct AppTime
        LoRaClkSncUpdateTime( timeCorrection );

        // update token
        ClkSyncAppTimeReqMng.tokenReq = ( ClkSyncAppTimeReqMng.tokenReq + 1 ) & 0x0F;  //bit7-4 RFU

        // Stop force-AppTimeReq retransmission
        ClkSyncAppTimeReqMng.forceNbTrans = 0;

        if( ClkSncState == CLKSNC_STATE_RUNNING_NOSYNC )
        {
            ClkSncState = CLKSNC_STATE_RUNNING;   // change state before setting timer
            LoRaClkSncAppTimeReqPeriodTimerSet();
        }

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        /* prepare to send AppTimeReq again when timeCorrection is max(0x7FFFFFFF) or min(0x80000000) */
        if( ( timeCorrection == CLKSNC_PARAM_TIMECORRECTION_MAX ) ||
            ( timeCorrection == CLKSNC_PARAM_TIMECORRECTION_MIN ) )
        {
            TimerStop( &( ClkSyncAppTimeReqMng.periodTimerObj ) );
            ClkSyncAppTimeReqMng.state = CLKSNC_APPTIMEREQ_STATE_CONT_TIMECORRECT;
        }
#endif
    }
    else
    {
        // discard it
    }

#ifdef DEBUG_CLKSNC
    print( "*CLKSNC:AppTimeAns" );
    print_newline();
    print( "*CLKSNC:  TimeCorrection=0x" );
    print_hex( timeCorrection, 8 );
    print( ", TokenAns=0x" );
    print_hex( tokenAns, 2 );
    print_newline(); 
#endif

    return CLKSNC_STATUS_OK;
}

//--------------------------------------------------------------------------------------------------

/*!
 * Process received command; PackageVersionReq
 */
static ClkSncStatus_t LoRaClkSncProcessPackageVersionReq( uint8_t        *p_buffer, 
                                                          uint8_t        *p_payloadLen, 
                                                          uint8_t        bufferMaxSize, 
                                                          uint32_t       *p_txDelayMs,
                                                          ClkSnc_RxCmd_t *p_rcCmd/*nouse*/ )
{
    // length check
    if( bufferMaxSize < (CLKSNC_PLEN_PACKAGE_VERSION_ANS + 1) )  // +1 = CID length
    {
        return CLKSNC_STATUS_LENGTH_ERROR;
    }

    // make PackageVersionAns
    p_buffer[0] = CLKSNC_CID_PACKAGE_VERSION_ANS;
    p_buffer[1] = CLKSNC_PACKAGE_IDENTIFIER;
    p_buffer[2] = CLKSNC_PACKAGE_VERSION;

    (*p_payloadLen) = CLKSNC_PLEN_PACKAGE_VERSION_ANS + 1;  // +1 = CID length

#ifdef DEBUG_CLKSNC
    print( "*CLKSNC:PackageVersionAns" );
    print_newline();
    print( "*CLKSNC:  PackageIdentifier=" );
    print_dec( p_buffer[1], 3, '\0' );
    print( ", PackageVersion=" );
    print_dec( p_buffer[2], 3, '\0' );
    print_newline();
#endif

    return CLKSNC_STATUS_OK;
}

/*!
 * Process received command; DeviceAppTimePeriodicityReq
 */
static ClkSncStatus_t LoRaClkSncProcessDeviceAppTimePeriodicityReq( uint8_t        *p_buffer, 
                                                                    uint8_t        *p_payloadLen, 
                                                                    uint8_t        bufferMaxSize, 
                                                                    uint32_t       *p_txDelayMs,
                                                                    ClkSnc_RxCmd_t *p_rcCmd )
{
    uint8_t     periodicity;
    uint8_t     status;
    SysTime_t   deviceTime;
    uint32_t    deviceTimeSec;

    // length check
    if( bufferMaxSize < (CLKSNC_PLEN_APP_TIME_PERIODICITY_ANS + 1) )  // +1 = CID length
    {
        return CLKSNC_STATUS_LENGTH_ERROR;
    }

    // init
    periodicity = p_rcCmd->reqCmdParam.devAppTimePeriodReq.periodicity;

    // process
    if( ( periodicity >= ClkSncIBParams.timeReqMinPeriodicity ) &&
        ( periodicity <= ClkSncIBParams.timeReqMaxPeriodicity ) )
    {
        status = 0x00;
        ClkSncIBParams.timeReqReqPeriodicity = periodicity;
    }
    else
    {
        status = 0x01;  // bit0 = NotSupported
    }
    deviceTime = LoRaClkSncGetCurrentSysTime();
    deviceTimeSec = deviceTime.Seconds;

    // make AppTimePeriodicityAns
    p_buffer[0] = CLKSNC_CID_APP_TIME_PERIODICITY_ANS;
    p_buffer[1] = status;
    p_buffer[2] = (  deviceTimeSec & 0x000000FF );
    p_buffer[3] = ( (deviceTimeSec & 0x0000FF00) >> 8 );
    p_buffer[4] = ( (deviceTimeSec & 0x00FF0000) >> 16 );
    p_buffer[5] = ( (deviceTimeSec & 0xFF000000) >> 24 );

    (*p_payloadLen) = CLKSNC_PLEN_APP_TIME_PERIODICITY_ANS + 1;  // +1 = CID length

    // set timer
    if( status == 0x00 )  // supported periodicity
    {
        LoRaClkSncAppTimeReqPeriodTimerSet();
    }

#ifdef DEBUG_CLKSNC
    print( "*CLKSNC:DeviceAppTimePeriodicityAns" );
    print_newline();
    print( "*CLKSNC:  Status=0x" );
    print_hex( status, 2 );
    print( ", Time=0x" );
    print_hex( deviceTimeSec, 8 );
    print_newline();
#endif

    return CLKSNC_STATUS_OK;
}

/*!
 * Process received command; ForceDeviceResyncCmd
 */
static ClkSncStatus_t LoRaClkSncProcessForceResyncCmd( uint8_t        *p_buffer, 
                                                       uint8_t        *p_payloadLen, 
                                                       uint8_t        bufferMaxSize, 
                                                       uint32_t       *p_txDelayMs,
                                                       ClkSnc_RxCmd_t *p_rcCmd )
{
    uint8_t     nbTrans;

    // length check
    if( bufferMaxSize < (CLKSNC_PLEN_APP_TIME_ANS + 1) )  // +1 = CID length
    {
        return CLKSNC_STATUS_LENGTH_ERROR;
    }

    // init
    nbTrans = p_rcCmd->reqCmdParam.forceDevResyncCmd.nbTrans;

    if( nbTrans > 0 )
    {
        ClkSyncAppTimeReqMng.forceNbTrans = nbTrans;

        TimerStop( &( ClkSyncAppTimeReqMng.periodTimerObj ) );
        LoRaClockSyncAppTimeReq( p_buffer, p_payloadLen, bufferMaxSize );  // CLKSNC_STATUS_OK will be returned
        LoRaClockSyncSendCompCommand( true/*Don'tCare*/ );
    }
    else
    {
        // discard it
    }

    return CLKSNC_STATUS_OK;
}

//--------------------------------------------------------------------------------------------------

/*!
 * Application time; get current time
 */
static SysTime_t LoRaClkSncGetCurrentSysTime( void )
{
    SysTime_t   currentTime;

    currentTime = SysTimeGet();
    currentTime.Seconds += ClkSyncAppTimeMng.correctTime;
    currentTime.Seconds -= UNIX_GPS_EPOCH_OFFSET; // unix -> epoch

    return currentTime;
}

/*!
 * Application time; set/update correct time
 */
static void LoRaClkSncUpdateTime( int32_t correctTimeSec )
{
    // update correct time
    ClkSyncAppTimeMng.correctTime += correctTimeSec;
}

//--------------------------------------------------------------------------------------------------

/*!
 * Timer - periodic AppTimeReq ; set timer
 */
static void LoRaClkSncAppTimeReqPeriodTimerSet( void )
{
    TimerStop( &( ClkSyncAppTimeReqMng.periodTimerObj ) );

    if( ClkSncIBParams.timeReqReqPeriodicity == (uint8_t)CLKSNC_IB_INIT_TIMEREQ_REQ_PERIODICITY )
    {
        if( ( ClkSncState == CLKSNC_STATE_RUNNING_NOSYNC ) && 
            ( ClkSncIBParams.timeReqPeriodSec == 0 ) )
        {
            // fixed period time (config) until AppTime is synchronized
            ClkSyncAppTimeReqMng.periodTimerSec = CLKSNC_CONFIG_INIT_SYNC_PERIOD_SEC * (uint32_t)1000;
        }
        else
        {
            // period time = user defined
            ClkSyncAppTimeReqMng.periodTimerSec = ClkSncIBParams.timeReqPeriodSec * (uint32_t)1000;
        }
    }
    else
    {
        // period time = 128 * 2^period + rand(30) sec
        ClkSyncAppTimeReqMng.periodTimerSec  = (uint32_t)1 << ClkSncIBParams.timeReqReqPeriodicity;
        ClkSyncAppTimeReqMng.periodTimerSec *= (uint32_t)128000;
        ClkSyncAppTimeReqMng.periodTimerSec += randr( -30000, 30000 );

        // update IB
        ClkSncIBParams.timeReqPeriodSec = ClkSyncAppTimeReqMng.periodTimerSec / (uint32_t)1000;
    }

    if( ClkSncState > CLKSNC_STATE_INITIALIZED )
    {
        if( ClkSyncAppTimeReqMng.periodTimerSec != 0 )
        {
            TimerSetValue( &( ClkSyncAppTimeReqMng.periodTimerObj ), ClkSyncAppTimeReqMng.periodTimerSec );
            TimerStart( &( ClkSyncAppTimeReqMng.periodTimerObj ) );
        }
    }
}

/*!
 * Timer - force AppTimeReq ; set timer
 * Periodicity of AppTimeReq timer is too long (10sec minimum),
 * and this timer event will be handled soon enough.
 * So it isn't necessary for context to disable interrupt for accessing "ClkSyncAppTimeReqMng.state".
 */
static void LoRaClkSncAppTimeReqForceTimerSet( void )
{
    uint32_t    forceTimeSec;

    TimerStop( &( ClkSyncAppTimeReqMng.periodTimerObj ) );

    forceTimeSec = (uint32_t)( ClkSncIBParams.forceSyncPeriodSec ) * (uint32_t)1000;

    TimerSetValue( &( ClkSyncAppTimeReqMng.periodTimerObj ), forceTimeSec );
    TimerStart( &( ClkSyncAppTimeReqMng.periodTimerObj ) );
}


/*!
 * Timer - periodic and force AppTimeReq ; timer interrupt
 * Periodicity of AppTimeReq timer is too long (10sec minimum),
 * and this timer event will be handled soon enough.
 * So it isn't necessary for context to disable interrupt for accessing "ClkSyncAppTimeReqMng.state".
 */
static void LoRaClkSncOnAppTimeReqPeriodTimerEvent( void )
{
    TimerStop( &( ClkSyncAppTimeReqMng.periodTimerObj ) );
    ClkSyncAppTimeReqMng.event = CLKSNC_TIMER_EVENT_APPTIMEREQ_TIMING;
}

