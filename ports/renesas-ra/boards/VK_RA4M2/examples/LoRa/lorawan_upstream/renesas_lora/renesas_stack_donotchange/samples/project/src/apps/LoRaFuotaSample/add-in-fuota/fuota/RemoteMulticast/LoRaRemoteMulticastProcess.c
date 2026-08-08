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

#include "LoRaRemoteMulticastProcess.h"

/*** Remote multicast setup command ***/
/* macros */
#define RMTMC_CID_PACKAGE_VERSION_REQ       0x00
#define RMTMC_CID_PACKAGE_VERSION_ANS       0x00
#define RMTMC_CID_MC_GROUP_STATUS_REQ       0x01
#define RMTMC_CID_MC_GROUP_STATUS_ANS       0x01
#define RMTMC_CID_MC_GROUP_SETUP_REQ        0x02
#define RMTMC_CID_MC_GROUP_SETUP_ANS        0x02
#define RMTMC_CID_MC_GROUP_DELETE_REQ       0x03
#define RMTMC_CID_MC_GROUP_DELETE_ANS       0x03
#define RMTMC_CID_MC_CLASSC_SESSION_REQ     0x04
#define RMTMC_CID_MC_CLASSC_SESSION_ANS     0x04
#define RMTMC_CID_MC_CLASSB_SESSION_REQ     0x05
#define RMTMC_CID_MC_CLASSB_SESSION_ANS     0x05

#define RMTMC_PLEN_PACKAGE_VERSION_REQ      (0)
#define RMTMC_PLEN_PACKAGE_VERSION_ANS      (2)
#define RMTMC_PLEN_MC_GROUP_STATUS_REQ      (1)
#define RMTMC_PLEN_MC_GROUP_SETUP_REQ       (29)
#define RMTMC_PLEN_MC_GROUP_SETUP_ANS       (1)
#define RMTMC_PLEN_MC_GROUP_DELETE_REQ      (1)
#define RMTMC_PLEN_MC_GROUP_DELETE_ANS      (1)
#define RMTMC_PLEN_MC_CLASSC_SESSION_REQ    (10)
#define RMTMC_PLEN_MC_CLASSC_SESSION_ANS    (4)
#define RMTMC_PLEN_MC_CLASSB_SESSION_REQ    (10)
#define RMTMC_PLEN_MC_CLASSB_SESSION_ANS    (4)

#define RMTMC_RXCMD_QUEUENUM                (4)

/* typedef */
typedef struct {
    uint8_t     cmdMask_reqGroupMask;
} RmtMc_StatusReq_t;

typedef struct {
    uint8_t     groupId;
    uint32_t    mcAddr;
    uint8_t     mcKeyEnc[16];
    uint32_t    minFCnt;
    uint32_t    maxFCnt;
} RmtMc_SetupReq_t;

typedef struct {
    uint8_t     groupId;
} RmtMc_DeleteReq_t;

typedef struct {
    uint8_t     groupId;
    uint32_t    sessionTime;
    uint8_t     sessionTimeout;
    uint32_t    frequency;
    uint8_t     dr;
} RmtMc_ClassCSessionReq_t; 

typedef struct {
    uint8_t     groupId;
    uint32_t    sessionTime;
    uint8_t     sessionTimeout;
    uint8_t     periodicity;
    uint32_t    frequency;
    uint8_t     dr;
} RmtMc_ClassBSessionReq_t; 

typedef struct
{
    uint8_t     cid;
    union {
        RmtMc_StatusReq_t        stausReq;
        RmtMc_SetupReq_t         SetupReq;
        RmtMc_DeleteReq_t        deleteReq;
        RmtMc_ClassCSessionReq_t cSessReq;
        RmtMc_ClassBSessionReq_t bSessReq;
    } reqCmdParam;
} RmtMc_RxCmd_t;

typedef struct
{
    RmtMc_RxCmd_t   rxCmdQueue[ RMTMC_RXCMD_QUEUENUM ];
    uint8_t         numCmd;
} RmtMc_RxCmdQueue_t;

/* global variable */
RmtMc_RxCmdQueue_t RmtMcCmdQueue;

/* function prototype */
static RmtMcStatus_t LoRaRmtMcHandlePackageVersionReq( uint8_t *p_buffer, size_t length );
static RmtMcStatus_t LoRaRmtMcHandleMulticastStatusReq( uint8_t *p_buffer, size_t length );
static RmtMcStatus_t LoRaRmtMcHandleMulticastSetupReq( uint8_t *p_buffer, size_t length );
static RmtMcStatus_t LoRaRmtMcHandleMulticastDeleteReq( uint8_t *p_buffer, size_t length );
static RmtMcStatus_t LoRaRmtMcHandleMulticastClassCSessionReq( uint8_t *p_buffer, size_t length );
#ifdef LORAMAC_CLASSB_ENABLED
static RmtMcStatus_t LoRaRmtMcHandleMulticastClassBSessionReq( uint8_t *p_buffer, size_t length );
#endif

static RmtMcStatus_t LoRaRmtMcProcessPackageVersionReq( uint8_t        *p_buffer, 
                                                        uint8_t        *p_payloadLen, 
                                                        uint8_t        bufferMaxSize, 
                                                        uint32_t       *p_txDelayMs,
                                                        RmtMc_RxCmd_t  *p_rcCmd/*nouse*/ );
static RmtMcStatus_t LoRaRmtMcProcessMulticastStatusReq( uint8_t       *p_buffer, 
                                                         uint8_t       *p_payloadLen, 
                                                         uint8_t       bufferMaxSize,
                                                         uint32_t      *p_txDelayMs,
                                                         RmtMc_RxCmd_t *p_rcCmd );
static RmtMcStatus_t LoRaRmtMcProcessMulticastSetupReq( uint8_t         *p_buffer, 
                                                        uint8_t         *p_payloadLen, 
                                                        uint8_t         bufferMaxSize,
                                                        uint32_t        *p_txDelayMs,
                                                        RmtMc_RxCmd_t   *p_rcCmd );
static RmtMcStatus_t LoRaRmtMcProcessMulticastDeleteReq( uint8_t        *p_buffer, 
                                                         uint8_t        *p_payloadLen, 
                                                         uint8_t        bufferMaxSize,
                                                         uint32_t       *p_txDelayMs,
                                                         RmtMc_RxCmd_t  *p_rcCmd );
static RmtMcStatus_t LoRaRmtMcProcessMulticastClassCSessionReq( uint8_t         *p_buffer, 
                                                                uint8_t         *p_payloadLen, 
                                                                uint8_t         bufferMaxSize, 
                                                                uint32_t        *p_txDelayMs,
                                                                RmtMc_RxCmd_t   *p_rcCmd );
#ifdef LORAMAC_CLASSB_ENABLED
static RmtMcStatus_t LoRaRmtMcProcessMulticastClassBSessionReq( uint8_t         *p_buffer, 
                                                                uint8_t         *p_payloadLen, 
                                                                uint8_t         bufferMaxSize,
                                                                uint32_t        *p_txDelayMs,
                                                                RmtMc_RxCmd_t   *p_rcCmd );
#endif

/*** Timer ***/
#define RMTMC_TIMER_EVENT_NONE      0x00
#define RMTMC_TIMER_EVENT_START     0x01
#define RMTMC_TIMER_EVENT_TIMEOUT   0x02

#define RMTMC_TIMER_RETRY_EVENT_MS  50  // msec for retry event procedure

typedef struct {
    uint8_t             event;
    bool                isStart;
    DeviceClass_t       class;
    uint32_t            sessionTimeout;
    TimerEvent_t        RmtMcStart;
    TimerEvent_t        RmtMcTimeout;
    /*---*/
    //store LoRaMAC API parameter
    uint8_t             mcKeyE[16];
    McChannelSetup_t    mcSetup;
    McRxParams_t        rxParams;
    //store device class before starting session
    DeviceClass_t       classBeforeSession;
} RmtMc_TimerMng_t;

RmtMc_TimerMng_t RmtMcTimerMng[ RMTMC_CONFIG_MAX_MC_CTX ];

/* function prototype */
static void LoRaRmtMcOnSessionStartTimerEvent( uint8_t groupId );
static void LoRaRmtMcOnSessionStartTimerEvent_Grp0( void );
#if RMTMC_CONFIG_MAX_MC_CTX >= 2
static void LoRaRmtMcOnSessionStartTimerEvent_Grp1( void );
#endif
#if RMTMC_CONFIG_MAX_MC_CTX >= 3
static void LoRaRmtMcOnSessionStartTimerEvent_Grp2( void );
#endif
#if RMTMC_CONFIG_MAX_MC_CTX == 4
static void LoRaRmtMcOnSessionStartTimerEvent_Grp3( void );
#endif
static void (*pfunc_RmtMcSessionStart[ RMTMC_CONFIG_MAX_MC_CTX ])( void ) = 
{
    LoRaRmtMcOnSessionStartTimerEvent_Grp0,
#if RMTMC_CONFIG_MAX_MC_CTX >= 2
    LoRaRmtMcOnSessionStartTimerEvent_Grp1,
#endif
#if RMTMC_CONFIG_MAX_MC_CTX >= 3
    LoRaRmtMcOnSessionStartTimerEvent_Grp2,
#endif
#if RMTMC_CONFIG_MAX_MC_CTX == 4
    LoRaRmtMcOnSessionStartTimerEvent_Grp3,
#endif
};

static void LoRaRmtMcOnSessionTimeoutTimerEvent( uint8_t groupId );
static void LoRaRmtMcOnSessionTimeoutTimerEvent_Grp0( void );
#if RMTMC_CONFIG_MAX_MC_CTX >= 2
static void LoRaRmtMcOnSessionTimeoutTimerEvent_Grp1( void );
#endif
#if RMTMC_CONFIG_MAX_MC_CTX >= 3
static void LoRaRmtMcOnSessionTimeoutTimerEvent_Grp2( void );
#endif
#if RMTMC_CONFIG_MAX_MC_CTX == 4
static void LoRaRmtMcOnSessionTimeoutTimerEvent_Grp3( void );
#endif
static void (*pfunc_RmtMcSessionTimeout[ RMTMC_CONFIG_MAX_MC_CTX ])( void ) = 
{
    LoRaRmtMcOnSessionTimeoutTimerEvent_Grp0,
#if RMTMC_CONFIG_MAX_MC_CTX >= 2
    LoRaRmtMcOnSessionTimeoutTimerEvent_Grp1,
#endif
#if RMTMC_CONFIG_MAX_MC_CTX >= 3
    LoRaRmtMcOnSessionTimeoutTimerEvent_Grp2,
#endif
#if RMTMC_CONFIG_MAX_MC_CTX == 4
    LoRaRmtMcOnSessionTimeoutTimerEvent_Grp3,
#endif
};

/*** Event; notify to upper ***/
LoRaRmtMcEventCb_t  RmtMcEventCbFuncs = {0};

/*** IB ***/
typedef struct
{
    uint32_t    __reserved;
} RmtMc_IB_params_t;

RmtMc_IB_params_t   RmtMcIBParams;

/*** RemoteMulticast state ***/
#define RMTMC_STATE_NONE            0x00
#define RMTMC_STATE_INITIALIZED     0x01
#define RMTMC_STATE_RUNNING         0x02
uint8_t RmtMcState = RMTMC_STATE_NONE;

/*** else ***/
static void LoRaRmtMcSwitchDeviceClass( DeviceClass_t classChg, DeviceClass_t *p_classBeforeChg );


/*!
 * RemoteMulticast initialization
 */
RmtMcStatus_t LoRaRemoteMulticastInit( LoRaRmtMcEventCb_t *p_rmtMcEventCb )
{
    uint8_t     i;

    // check
    if( p_rmtMcEventCb == NULL )
    {
        return RMTMC_STATUS_PARAMETER_INVALID;
    }
    if( ( p_rmtMcEventCb->LoRaRmtMcCurrentTimeSecReqCb == NULL ) ||
        ( p_rmtMcEventCb->LoRaRmtMcSessionSetupIndication == NULL ) ||
        ( p_rmtMcEventCb->LoRaRmtMcSessionStartIndication == NULL ) || 
        ( p_rmtMcEventCb->LoRaRmtMcSessionEndIndication == NULL ) )
    {
        return RMTMC_STATUS_PARAMETER_INVALID;
    }

    /* IB */
    memset1( (uint8_t *)&RmtMcIBParams, 0x00, sizeof(RmtMc_IB_params_t) );

    /* for command */
    memset1( (uint8_t *)&RmtMcCmdQueue, 0x00, sizeof(RmtMc_RxCmdQueue_t) );

    /* for timer */
    if( RmtMcState != RMTMC_STATE_NONE )
    {
        for( i = 0; i < RMTMC_CONFIG_MAX_MC_CTX; i++ )
        {
            TimerStop( &( RmtMcTimerMng[i].RmtMcStart ) );
            TimerStop( &( RmtMcTimerMng[i].RmtMcTimeout ) );
        }
    }
    memset1( (uint8_t *)RmtMcTimerMng, 0x00, RMTMC_CONFIG_MAX_MC_CTX * sizeof(RmtMc_TimerMng_t) );
    for( i = 0; i < RMTMC_CONFIG_MAX_MC_CTX; i++ )
    {
        TimerInit( &( RmtMcTimerMng[i].RmtMcStart ), pfunc_RmtMcSessionStart[i] );
        TimerInit( &( RmtMcTimerMng[i].RmtMcTimeout ), pfunc_RmtMcSessionTimeout[i] );
    }

    /* event */
    if( p_rmtMcEventCb != &RmtMcEventCbFuncs )  // means upper-layer requests initialization
    {
        memcpy1( (uint8_t *)&RmtMcEventCbFuncs, (uint8_t *)p_rmtMcEventCb, sizeof(LoRaRmtMcEventCb_t) );
    }

    RmtMcState = RMTMC_STATE_INITIALIZED;

    return RMTMC_STATUS_OK;
}

/*!
 * RemoteMulticast start
 */
RmtMcStatus_t LoRaRemoteMulticastStart( void )
{
    RmtMcStatus_t  res;

    // init
    res = RMTMC_STATUS_ERROR;

    if( RmtMcState == RMTMC_STATE_INITIALIZED )
    {
        RmtMcState = RMTMC_STATE_RUNNING;
        res = RMTMC_STATUS_OK;
    }

    return res;
}

/*!
 * RemoteMulticast stop
 */
void LoRaRemoteMulticastStop( void )
{
    if( RmtMcState != RMTMC_STATE_NONE )
    {
        LoRaRemoteMulticastInit( &RmtMcEventCbFuncs );
    }
}

/*!
 * MCPS-Indication event function for RemoteMulticast
 */
RmtMcStatus_t LoRaRemoteMulticastMcpsIndication( McpsIndication_t *p_mcpsIndication )
{
    RmtMcStatus_t   status, funcRet;
    uint8_t         *p_buffer;
    uint8_t         bufferSize;

    // RemoteMulticast is not running
    if( RmtMcState != RMTMC_STATE_RUNNING )
    {
        return RMTMC_STATUS_ERROR;
    }

    // Reject error indication
    if ( p_mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK )
    {
        return RMTMC_STATUS_ERROR;
    }
    // RemoteMulticastSetup messages SHALL NOT be sent using multicast.
    if( (p_mcpsIndication->McpsIndication != MCPS_UNCONFIRMED) &&
        (p_mcpsIndication->McpsIndication != MCPS_CONFIRMED) )
    {
        return RMTMC_STATUS_COMMAND_ERROR;
    }

    // init
    status     = RMTMC_STATUS_OK;
    p_buffer   = p_mcpsIndication->Buffer;
    bufferSize = p_mcpsIndication->BufferSize;

    memset1( (uint8_t *)&RmtMcCmdQueue, 0x00, sizeof(RmtMc_RxCmdQueue_t) );

    // Get RemoteMulticastSetup commands
    while( bufferSize > 0 )
    {
        switch( *p_buffer )  //= CID
        {
            case RMTMC_CID_PACKAGE_VERSION_REQ:
                funcRet = LoRaRmtMcHandlePackageVersionReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == RMTMC_STATUS_OK )
                {
                    bufferSize -= (RMTMC_PLEN_PACKAGE_VERSION_REQ + 1);  // +1 = CID length
                    p_buffer   += (RMTMC_PLEN_PACKAGE_VERSION_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case RMTMC_CID_MC_GROUP_STATUS_REQ:
                funcRet = LoRaRmtMcHandleMulticastStatusReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == RMTMC_STATUS_OK )
                {
                    bufferSize -= (RMTMC_PLEN_MC_GROUP_STATUS_REQ + 1);  // +1 = CID length
                    p_buffer   += (RMTMC_PLEN_MC_GROUP_STATUS_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case RMTMC_CID_MC_GROUP_SETUP_REQ:
                funcRet = LoRaRmtMcHandleMulticastSetupReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == RMTMC_STATUS_OK )
                {
                    bufferSize -= (RMTMC_PLEN_MC_GROUP_SETUP_REQ + 1);  // +1 = CID length
                    p_buffer   += (RMTMC_PLEN_MC_GROUP_SETUP_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case RMTMC_CID_MC_GROUP_DELETE_REQ:
                funcRet = LoRaRmtMcHandleMulticastDeleteReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == RMTMC_STATUS_OK )
                {
                    bufferSize -= (RMTMC_PLEN_MC_GROUP_DELETE_REQ + 1);  // +1 = CID length
                    p_buffer   += (RMTMC_PLEN_MC_GROUP_DELETE_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case RMTMC_CID_MC_CLASSC_SESSION_REQ:
                funcRet = LoRaRmtMcHandleMulticastClassCSessionReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == RMTMC_STATUS_OK )
                {
                    bufferSize -= (RMTMC_PLEN_MC_CLASSC_SESSION_REQ + 1);  // +1 = CID length
                    p_buffer   += (RMTMC_PLEN_MC_CLASSC_SESSION_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

#ifdef LORAMAC_CLASSB_ENABLED
            case RMTMC_CID_MC_CLASSB_SESSION_REQ:
                funcRet = LoRaRmtMcHandleMulticastClassBSessionReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == RMTMC_STATUS_OK )
                {
                    bufferSize -= (RMTMC_PLEN_MC_CLASSB_SESSION_REQ + 1);  // +1 = CID length
                    p_buffer   += (RMTMC_PLEN_MC_CLASSB_SESSION_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;
#endif
            default:
                bufferSize = 0;  // exit from while() loop
                break;
        }
    }

    if( RmtMcCmdQueue.numCmd == 0 )
    {
        status = RMTMC_STATUS_COMMAND_ERROR;
    }

    return status;
}

/*!
 * Process event of FUOTA
 */
void LoRaRemoteMulticastProcessCommand( uint8_t  *p_buffer, 
                                        uint8_t  *p_payloadLen, 
                                        uint8_t  bufferMaxSize, 
                                        uint32_t *p_txDelayMs,
                                        bool     *p_isRetransEn )
{
    RmtMcStatus_t   funcRet;
    RmtMc_RxCmd_t   *p_rxCmd;
    uint8_t         payloadLen, payloadLenTotal;
    uint32_t        txDelayMs, tmpTxDelayMs;
    uint8_t         i;
    bool            isRetransEn;

    // RemoteMulticast is not running
    if( RmtMcState != RMTMC_STATE_RUNNING )
    {
        return;
    }

    // init
    payloadLenTotal = 0;
    txDelayMs       = 0;
    isRetransEn     = true;  // enable to retrans command response

    for( i = 0; i < RmtMcCmdQueue.numCmd; i++ )
    {
        // init (loop)
        p_rxCmd     = &( RmtMcCmdQueue.rxCmdQueue[ i ] );
        tmpTxDelayMs = 0;

        switch( p_rxCmd->cid )
        {
            case RMTMC_CID_PACKAGE_VERSION_REQ:
                funcRet = LoRaRmtMcProcessPackageVersionReq( p_buffer, 
                                                             &payloadLen, 
                                                             bufferMaxSize, 
                                                             &tmpTxDelayMs,
                                                             p_rxCmd );
                break;

            case RMTMC_CID_MC_GROUP_STATUS_REQ:
                funcRet = LoRaRmtMcProcessMulticastStatusReq( p_buffer, 
                                                              &payloadLen, 
                                                              bufferMaxSize,
                                                              &tmpTxDelayMs,
                                                              p_rxCmd );
                break;

            case RMTMC_CID_MC_GROUP_SETUP_REQ:
                funcRet = LoRaRmtMcProcessMulticastSetupReq( p_buffer, 
                                                             &payloadLen, 
                                                             bufferMaxSize, 
                                                             &tmpTxDelayMs,
                                                             p_rxCmd );
                break;

            case RMTMC_CID_MC_GROUP_DELETE_REQ:
                funcRet = LoRaRmtMcProcessMulticastDeleteReq( p_buffer, 
                                                              &payloadLen, 
                                                              bufferMaxSize,
                                                              &tmpTxDelayMs,
                                                              p_rxCmd );
                break;

            case RMTMC_CID_MC_CLASSC_SESSION_REQ:
                funcRet = LoRaRmtMcProcessMulticastClassCSessionReq( p_buffer, 
                                                                     &payloadLen, 
                                                                     bufferMaxSize,
                                                                     &tmpTxDelayMs,
                                                                     p_rxCmd );
                if( funcRet == RMTMC_STATUS_OK )
                {
                    isRetransEn = false;  // disable retransmission because time information is included
                }
                break;

#ifdef LORAMAC_CLASSB_ENABLED
            case RMTMC_CID_MC_CLASSB_SESSION_REQ:
                funcRet = LoRaRmtMcProcessMulticastClassBSessionReq( p_buffer, 
                                                                     &payloadLen, 
                                                                     bufferMaxSize,
                                                                     &tmpTxDelayMs,
                                                                     p_rxCmd );
                if( funcRet == RMTMC_STATUS_OK )
                {
                    isRetransEn = false;  // disable retransmission because time information is included
                }
                break;
#endif
            default:
                funcRet = RMTMC_STATUS_COMMAND_ERROR;
                break;
        }

        if( funcRet == RMTMC_STATUS_OK )
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
 * Process timer interrupt of FUOTA
 */
void LoRaRemoteMulticastProcessEvent( void )
{
    RmtMc_TimerMng_t    *p_rmtMcTimerMng;
    uint8_t             i;
    LoRaMacStatus_t     macStatus;
    uint8_t             mcStatus;

    // RemoteMulticast is not running
    if( RmtMcState != RMTMC_STATE_RUNNING )
    {
        return;
    }

    for( i = 0; i < RMTMC_CONFIG_MAX_MC_CTX; i++ )  // i = groupId
    {
        p_rmtMcTimerMng = &( RmtMcTimerMng[ i ] );
        switch( p_rmtMcTimerMng->event )
        {
            case RMTMC_TIMER_EVENT_START:
                // start multicast
                macStatus = LoRaMacMcChannelSetupRxParams( (AddressIdentifier_t)i, 
                                                           p_rmtMcTimerMng->class, 
                                                           &(p_rmtMcTimerMng->rxParams), 
                                                           &mcStatus );
                if( macStatus == LORAMAC_STATUS_BUSY )
                {
                    // set RmtMcStart timer again to retry.
                    TimerSetValue( &( p_rmtMcTimerMng->RmtMcStart ), RMTMC_TIMER_RETRY_EVENT_MS );
                    TimerStart( &( p_rmtMcTimerMng->RmtMcStart ) );
                }
                else
                {
                    // switch class; keep current class.
                    LoRaRmtMcSwitchDeviceClass( p_rmtMcTimerMng->class, 
                                                &(p_rmtMcTimerMng->classBeforeSession) );

                    TimerSetValue( &( p_rmtMcTimerMng->RmtMcTimeout ), p_rmtMcTimerMng->sessionTimeout );
                    TimerStart( &( p_rmtMcTimerMng->RmtMcTimeout ) );
                    p_rmtMcTimerMng->isStart = true;

                    // notify to upper
                    (*RmtMcEventCbFuncs.LoRaRmtMcSessionStartIndication)( p_rmtMcTimerMng->class,
                                                                          i, 
                                                                          p_rmtMcTimerMng->sessionTimeout / (uint32_t)1000 );

#ifdef DEBUG_RMTMC
                    print( "*RMTMC:Start session (GroupID=" );
                    print_dec( i, 3, '\0' );
                    print( ")" );
                    print_newline();
#endif
                }

                // clear event
                p_rmtMcTimerMng->event = RMTMC_TIMER_EVENT_NONE;
                break;

            case RMTMC_TIMER_EVENT_TIMEOUT:
                // reset McChannel
                macStatus = LoRaMacMcChannelDelete( (AddressIdentifier_t)i );
                if( macStatus == LORAMAC_STATUS_BUSY )
                {
                    // set RmtMcTimeout timer again to retry.
                    TimerSetValue( &( p_rmtMcTimerMng->RmtMcTimeout ), RMTMC_TIMER_RETRY_EVENT_MS );
                    TimerStart( &( p_rmtMcTimerMng->RmtMcTimeout ) );
                }
                else
                {
                    LoRaMacMcChannelSetup( &( p_rmtMcTimerMng->mcSetup ) );

                    // change class to the previous
                    LoRaRmtMcSwitchDeviceClass( p_rmtMcTimerMng->classBeforeSession, NULL );

                    // notify to upper
                    (*RmtMcEventCbFuncs.LoRaRmtMcSessionEndIndication)( p_rmtMcTimerMng->class, i );

                    p_rmtMcTimerMng->sessionTimeout = 0;  // clear it

#ifdef DEBUG_RMTMC
                    print( "*RMTMC:Timeout session (GroupID=" );
                    print_dec( i, 3, '\0' );  // i = groupId
                    print( ")" );
                    print_newline();
#endif
                }

                // clear event
                p_rmtMcTimerMng->event = RMTMC_TIMER_EVENT_NONE;
                break;

            case RMTMC_TIMER_EVENT_NONE:
            default:
                break;
        }
    }
}

/*!
 * Returns whether RemoteMulticast is idle
 * If RemoteMulticast is not idle (return false), LoRaRemoteMulticastProcessEvent() call is required
 */
bool LoRaRemoteMulticastIsIdle( void )
{
    bool    bRet;
    uint8_t i;

    // init
    bRet = true;  // (init) idle - function call is not required

    if( RmtMcState == RMTMC_STATE_RUNNING )
    {
        for( i = 0; i < RMTMC_CONFIG_MAX_MC_CTX; i++ )
        {
            if( RmtMcTimerMng[ i ].event != RMTMC_TIMER_EVENT_NONE )
            {
                bRet = false;  // not idle - function call is required
                break;  // exit from for(i) loop
            }
        }
    }

    return bRet;
}

/*!
 * Uplink result notification from upper layer
 */
void LoRaRemoteMulticastSendCompCommand( bool isSuccess )
{
    // nothing to do
}

/*!
 * IB Get Request
 */
RmtMcStatus_t LoRaRemoteMulticastIbGetRequest( uint8_t ib, void *vpVal )
{
#if 0  // Currently nothing to do. If you want to add operations, please check the 'RmtMcState'.
    if( RmtMcState == RMTMC_STATE_NONE )
    {
        // RemoteMulticast is not initialized
        return RMTMC_STATUS_ERROR;
    }
#endif

    // no IB
    return RMTMC_STATUS_SERVICE_UNKNOWN;
}

/*!
 * IB Set Request
 */
RmtMcStatus_t LoRaRemoteMulticastIbSetRequest( uint8_t ib, void *vpVal )
{
#if 0  // Currently nothing to do. If you want to add operations, please check the 'RmtMcState'.
    if( RmtMcState == RMTMC_STATE_NONE )
    {
        // RemoteMulticast is not initialized
        return RMTMC_STATUS_ERROR;
    }
#endif

    // no IB
    return RMTMC_STATUS_SERVICE_UNKNOWN;
}

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*!
 * Get RemoteMulticast command payload size
 */
RmtMcStatus_t LoRaRemoteMulticastGetRcvdCmdPayloadLen( uint8_t cid, uint8_t *p_cmdPayloadLen )
{
    RmtMcStatus_t   ret;

    // init
    ret = RMTMC_STATUS_OK;

    switch( cid )
    {
        case RMTMC_CID_PACKAGE_VERSION_REQ:
            (*p_cmdPayloadLen) = RMTMC_PLEN_PACKAGE_VERSION_REQ;
            break;
    
        case RMTMC_CID_MC_GROUP_STATUS_REQ:
            (*p_cmdPayloadLen) = RMTMC_PLEN_MC_GROUP_STATUS_REQ;
            break;
    
        case RMTMC_CID_MC_GROUP_SETUP_REQ:
            (*p_cmdPayloadLen) = RMTMC_PLEN_MC_GROUP_SETUP_REQ;
            break;
    
        case RMTMC_CID_MC_GROUP_DELETE_REQ:
            (*p_cmdPayloadLen) = RMTMC_PLEN_MC_GROUP_DELETE_REQ;
            break;
    
        case RMTMC_CID_MC_CLASSC_SESSION_REQ:
            (*p_cmdPayloadLen) = RMTMC_PLEN_MC_CLASSC_SESSION_REQ;
            break;
    
        case RMTMC_CID_MC_CLASSB_SESSION_REQ:
            (*p_cmdPayloadLen) = RMTMC_PLEN_MC_CLASSB_SESSION_REQ;
            break;
    
        default:
            ret = RMTMC_STATUS_COMMAND_ERROR;
            break;
    }

    return ret;
}
#endif

//--------------------------------------------------------------------------------------------------

/*!
 * check received command; PackageVersionReq
 */
static RmtMcStatus_t LoRaRmtMcHandlePackageVersionReq( uint8_t *p_buffer, size_t length )
{
    RmtMc_RxCmd_t    *p_rxCmd;

    // payload length check
    //  ... it has no payload

    // command queue check
    if( RmtMcCmdQueue.numCmd >= RMTMC_RXCMD_QUEUENUM )
    {
        return RMTMC_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( RmtMcCmdQueue.rxCmdQueue[ RmtMcCmdQueue.numCmd ] );

    // get parameter
    p_rxCmd->cid = RMTMC_CID_PACKAGE_VERSION_REQ;

    RmtMcCmdQueue.numCmd++;

#ifdef DEBUG_RMTMC
    print( "*RMTMC:PackageVersionReq" );
    print_newline();
#endif

    return RMTMC_STATUS_OK;
}

/*!
 * check received command; MulticastStatusReq
 */
static RmtMcStatus_t LoRaRmtMcHandleMulticastStatusReq( uint8_t *p_buffer, size_t length )
{
    RmtMc_RxCmd_t        *p_rxCmd;
    RmtMc_StatusReq_t    *p_cmdStatusReq;

    // payload length check
    if( length < RMTMC_PLEN_MC_GROUP_STATUS_REQ )
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( RmtMcCmdQueue.numCmd >= RMTMC_RXCMD_QUEUENUM )
    {
        return RMTMC_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( RmtMcCmdQueue.rxCmdQueue[ RmtMcCmdQueue.numCmd ] );
    p_cmdStatusReq = (RmtMc_StatusReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = RMTMC_CID_MC_GROUP_STATUS_REQ;
    p_cmdStatusReq->cmdMask_reqGroupMask = (*p_buffer) & 0x0F;  // bit7-4 = RFU
    //p_buffer++;

    RmtMcCmdQueue.numCmd++;

#ifdef DEBUG_RMTMC
    print( "*RMTMC:MulticastStatusReq" );
    print_newline();
    print( "*RMTMC:  ReqGroupMask=0x" );
    print_hex( p_cmdStatusReq->cmdMask_reqGroupMask, 2 );
    print_newline();
#endif

    return RMTMC_STATUS_OK;
}


/*!
 * check received command; MulticastSetupReq
 */
static RmtMcStatus_t LoRaRmtMcHandleMulticastSetupReq( uint8_t *p_buffer, size_t length )
{
    RmtMc_RxCmd_t        *p_rxCmd;
    RmtMc_SetupReq_t     *p_cmdSetupReq;

    // payload length check
    if( length < RMTMC_PLEN_MC_GROUP_SETUP_REQ )
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( RmtMcCmdQueue.numCmd >= RMTMC_RXCMD_QUEUENUM )
    {
        return RMTMC_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( RmtMcCmdQueue.rxCmdQueue[ RmtMcCmdQueue.numCmd ] );
    p_cmdSetupReq = (RmtMc_SetupReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = RMTMC_CID_MC_GROUP_SETUP_REQ;

    p_cmdSetupReq->groupId = (*p_buffer) & 0x03;  // bit7-2 = RFU
    p_buffer++;

    p_cmdSetupReq->mcAddr = ((uint32_t)p_buffer[3] << 24) | ((uint32_t)p_buffer[2] << 16) |
                            ((uint32_t)p_buffer[1] << 8)  |  (uint32_t)p_buffer[0];
    p_buffer += 4;

    memcpy1( p_cmdSetupReq->mcKeyEnc, p_buffer, 16 );
    p_buffer += 16;

    p_cmdSetupReq->minFCnt = ((uint32_t)p_buffer[3] << 24) | ((uint32_t)p_buffer[2] << 16) |
                             ((uint32_t)p_buffer[1] << 8)  |  (uint32_t)p_buffer[0];
    p_buffer += 4;

    p_cmdSetupReq->maxFCnt = ((uint32_t)p_buffer[3] << 24) | ((uint32_t)p_buffer[2] << 16) |
                             ((uint32_t)p_buffer[1] << 8)  |  (uint32_t)p_buffer[0];
    //p_buffer += 4;

    RmtMcCmdQueue.numCmd++;
    
#ifdef DEBUG_RMTMC
    print( "*RMTMC:MulticastSetupReq" );
    print_newline();
    print( "*RMTMC:  McGroupID=" );
    print_dec( p_cmdSetupReq->groupId, 3, '\0' );
    print( ", McAddr=0x" );
    print_hex( p_cmdSetupReq->mcAddr, 8 );
    print_newline();
    print( "*RMTMC:  McKeyE=");
    for( uint8_t _i = 0; _i < 16; _i++ )
    {
        print_hex( p_cmdSetupReq->mcKeyEnc[_i], 2 );
    }
    print_newline();
    print( "*RMTMC:  minMcFCount=0x" );
    print_hex( p_cmdSetupReq->minFCnt, 8 );
    print( ", maxMcFCount=0x" );
    print_hex( p_cmdSetupReq->maxFCnt, 8 );
    print_newline();
#endif

    return RMTMC_STATUS_OK;
}

/*!
 * check received command; MulticastDeleteReq
 */
static RmtMcStatus_t LoRaRmtMcHandleMulticastDeleteReq( uint8_t *p_buffer, size_t length )
{
    RmtMc_RxCmd_t        *p_rxCmd;
    RmtMc_DeleteReq_t    *p_cmdDeleteReq;

    // payload length check
    if( length < RMTMC_PLEN_MC_GROUP_DELETE_REQ )
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( RmtMcCmdQueue.numCmd >= RMTMC_RXCMD_QUEUENUM )
    {
        return RMTMC_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( RmtMcCmdQueue.rxCmdQueue[ RmtMcCmdQueue.numCmd ] );
    p_cmdDeleteReq = (RmtMc_DeleteReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = RMTMC_CID_MC_GROUP_DELETE_REQ;

    p_cmdDeleteReq->groupId = (*p_buffer) & 0x03;  // bit7-2 = RFU
    //p_buffer++;

    RmtMcCmdQueue.numCmd++;

#ifdef DEBUG_RMTMC
    print( "*RMTMC:MulticastDeleteReq" );
    print_newline();
    print( "*RMTMC:  McGroupID=" );
    print_dec( p_cmdDeleteReq->groupId, 3, '\0' );
    print_newline();
#endif
    
    return RMTMC_STATUS_OK;
}

/*!
 * check received command; MulticastClassCSessionReq
 */
static RmtMcStatus_t LoRaRmtMcHandleMulticastClassCSessionReq( uint8_t *p_buffer, size_t length )
{
    RmtMc_RxCmd_t            *p_rxCmd;
    RmtMc_ClassCSessionReq_t *p_cmdCSessReq;

    // payload length check
    if( length < RMTMC_PLEN_MC_CLASSC_SESSION_REQ )
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( RmtMcCmdQueue.numCmd >= RMTMC_RXCMD_QUEUENUM )
    {
        return RMTMC_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( RmtMcCmdQueue.rxCmdQueue[ RmtMcCmdQueue.numCmd ] );
    p_cmdCSessReq = (RmtMc_ClassCSessionReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = RMTMC_CID_MC_CLASSC_SESSION_REQ;

    p_cmdCSessReq->groupId = (*p_buffer) & 0x03;  // bit7-2 = RFU
    p_buffer++;

    p_cmdCSessReq->sessionTime = ((uint32_t)p_buffer[3] << 24) | ((uint32_t)p_buffer[2] << 16) |
                                 ((uint32_t)p_buffer[1] << 8)  |  (uint32_t)p_buffer[0];
    p_buffer += 4;

    p_cmdCSessReq->sessionTimeout = (*p_buffer) & 0x0F;  // bit7-4 = RFU
    p_buffer++;

    p_cmdCSessReq->frequency = ((uint32_t)p_buffer[2] << 16) |
                               ((uint32_t)p_buffer[1] << 8)  |  (uint32_t)p_buffer[0];
    p_cmdCSessReq->frequency *= 100;
    p_buffer += 3;

    p_cmdCSessReq->dr = (*p_buffer);
    //p_buffer++;

    RmtMcCmdQueue.numCmd++;
        
#ifdef DEBUG_RMTMC
    print( "*RMTMC:MulticastClassCSessionReq" );
    print_newline();
    print( "*RMTMC:  McGroupID=" );
    print_dec( p_cmdCSessReq->groupId, 3, '\0' );
    print_newline();
    print( "*RMTMC:  SessionTime=0x" );
    print_hex( p_cmdCSessReq->sessionTime, 8 );
    print( ", SessionTimeout=" );
    print_dec( p_cmdCSessReq->sessionTimeout, 3, '\0' );
    print_newline();
    print( "*RMTMC:  DLFrequ(*100)=" );
    print_dec( p_cmdCSessReq->frequency, 10, '\0' );
    print( ", DR=" );
    print_dec( p_cmdCSessReq->dr, 3, '\0' );
    print_newline();
#endif
    
    return RMTMC_STATUS_OK;
}

#ifdef LORAMAC_CLASSB_ENABLED
/*!
 * check received command; MulticastClassBSessionReq
 */
static RmtMcStatus_t LoRaRmtMcHandleMulticastClassBSessionReq( uint8_t *p_buffer, size_t length )
{
    RmtMc_RxCmd_t            *p_rxCmd;
    RmtMc_ClassBSessionReq_t *p_cmdBSessReq;

    // payload length check
    if( length < RMTMC_PLEN_MC_CLASSB_SESSION_REQ )
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( RmtMcCmdQueue.numCmd >= RMTMC_RXCMD_QUEUENUM )
    {
        return RMTMC_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( RmtMcCmdQueue.rxCmdQueue[ RmtMcCmdQueue.numCmd ] );
    p_cmdBSessReq = (RmtMc_ClassBSessionReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = RMTMC_CID_MC_CLASSB_SESSION_REQ;

    p_cmdBSessReq->groupId = (*p_buffer) & 0x03;  // bit7-2 = RFU
    p_buffer++;

    p_cmdBSessReq->sessionTime = ((uint32_t)p_buffer[3] << 24) | ((uint32_t)p_buffer[2] << 16) |
                                 ((uint32_t)p_buffer[1] << 8)  |  (uint32_t)p_buffer[0];
    p_buffer += 4;

    p_cmdBSessReq->sessionTimeout = (*p_buffer) & 0x0F;         // bit0-3
    p_cmdBSessReq->periodicity    = ((*p_buffer) & 0x70) >> 4;  // bit6-4
    p_buffer++;

    p_cmdBSessReq->frequency = ((uint32_t)p_buffer[2] << 16) |
                               ((uint32_t)p_buffer[1] << 8)  |  (uint32_t)p_buffer[0];
    p_cmdBSessReq->frequency *= 100;
    p_buffer += 3;

    p_cmdBSessReq->dr = (*p_buffer);
    //p_buffer++;

    RmtMcCmdQueue.numCmd++;

#ifdef DEBUG_RMTMC
    print( "*RMTMC:MulticastClassBSessionReq" );
    print_newline();
    print( "*RMTMC:  McGroupID=" );
    print_dec( p_cmdBSessReq->groupId, 3, '\0' );
    print_newline();
    print( "*RMTMC:  SessionTime=0x" );
    print_hex( p_cmdBSessReq->sessionTime, 8 );
    print( ", SessionTimeout=" );
    print_dec( p_cmdBSessReq->sessionTimeout, 3, '\0' );
    print( ", Periodicity=" );
    print_dec( p_cmdBSessReq->periodicity, 3, '\0' );
    print_newline();
    print( "*RMTMC:  DLFrequ(*100)=" );
    print_dec( p_cmdBSessReq->frequency, 10, '\0' );
    print( ", DR=" );
    print_dec( p_cmdBSessReq->dr, 3, '\0' );
    print_newline();
#endif

    return RMTMC_STATUS_OK;
}
#endif

//--------------------------------------------------------------------------------------------------

/*!
 * Process received command; PackageVersionReq
 */
static RmtMcStatus_t LoRaRmtMcProcessPackageVersionReq( uint8_t       *p_buffer, 
                                                        uint8_t       *p_payloadLen, 
                                                        uint8_t       bufferMaxSize, 
                                                        uint32_t      *p_txDelayMs,
                                                        RmtMc_RxCmd_t *p_rcCmd/*nouse*/ )
{
    // length check
    if( bufferMaxSize < (RMTMC_PLEN_PACKAGE_VERSION_ANS + 1) )  // +1 = CID length
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }

    // make PackageVersionAns
    p_buffer[0] = RMTMC_CID_PACKAGE_VERSION_ANS;
    p_buffer[1] = RMTMC_PACKAGE_IDENTIFIER;
    p_buffer[2] = RMTMC_PACKAGE_VERSION;

    (*p_payloadLen) = RMTMC_PLEN_PACKAGE_VERSION_ANS + 1;  // +1 = CID length

#ifdef DEBUG_RMTMC
    print( "*RMTMC:PackageVersionAns" );
    print_newline();
    print( "*RMTMC:  PackageIdentifier=" );
    print_dec( p_buffer[1], 3, '\0' );
    print( ", PackageVersion=" );
    print_dec( p_buffer[2], 3, '\0' );
    print_newline();
#endif

    return RMTMC_STATUS_OK;
}

/*!
 * Process received command; MulticastStatusReq
 */
static RmtMcStatus_t LoRaRmtMcProcessMulticastStatusReq( uint8_t       *p_buffer, 
                                                         uint8_t       *p_payloadLen, 
                                                         uint8_t       bufferMaxSize, 
                                                         uint32_t      *p_txDelayMs,
                                                         RmtMc_RxCmd_t *p_rcCmd )
{
    uint8_t         reqGroupMask;
    LoRaMacStatus_t macStatus;
    uint8_t         StatusAnsLen;
    uint8_t         totalGroups, ansGroupMask;
    uint32_t        mcAddress[ RMTMC_CONFIG_MAX_MC_CTX ];
    uint8_t         *p_optList;
    uint8_t         i;

    // init
    reqGroupMask = p_rcCmd->reqCmdParam.stausReq.cmdMask_reqGroupMask;
    totalGroups  = 0;
    ansGroupMask = 0x00;

    // process
    for( i = 0; i < RMTMC_CONFIG_MAX_MC_CTX; i++ )
    {
        if( (reqGroupMask & 0x01) == 0x01 )
        {
            macStatus = LoRaMacMcChannelGetAddress( (AddressIdentifier_t)i, &( mcAddress[i] ) );
            if( macStatus == LORAMAC_STATUS_OK )
            {
                totalGroups++;
                ansGroupMask |= (1 << i);
            }
        }

        reqGroupMask >>= 1;  // for next
    }

    // length check  (variable length)
    StatusAnsLen = 2 + totalGroups * 5;  // 2 = CID + status,  5 = GroupID + McAddr
    if( bufferMaxSize < StatusAnsLen )
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }

    // make MulticastGroupStatusAns
    p_buffer[0] = RMTMC_CID_MC_GROUP_STATUS_ANS;
    p_buffer[1] = (totalGroups << 4) | ansGroupMask;  // bit6-4 = NbTotalGroups, bit3-0 = AnsGroupMask

    p_optList = &( p_buffer[2] );
    for( i = 0; i < RMTMC_CONFIG_MAX_MC_CTX; i++ )
    {
        if( (ansGroupMask & (1 << i)) != 0x00 )
        {
            p_optList[0] = i;
            p_optList[1] = (uint8_t)(  mcAddress[i] & 0x000000FF );
            p_optList[2] = (uint8_t)( (mcAddress[i] & 0x0000FF00) >> 8 );
            p_optList[3] = (uint8_t)( (mcAddress[i] & 0x00FF0000) >> 16 );
            p_optList[4] = (uint8_t)( (mcAddress[i] & 0xFF000000) >> 24 );

            p_optList += 5;  // for next
        }
    }

    (*p_payloadLen) = StatusAnsLen;

#ifdef DEBUG_RMTMC
    print( "*RMTMC:MulticastStatusAns" );
    print_newline();
    print( "*RMTMC:  NbTotalGroups=" );
    print_dec( totalGroups, 3, '\0' );
    print( ", AnsGroupMask=0x" );
    print_hex( ansGroupMask, 2 );
    print_newline();
    for( i = 0; i < RMTMC_CONFIG_MAX_MC_CTX; i++ )
    {
        if( (ansGroupMask & (1 << i)) != 0x00 )
        {
            print( "*RMTMC:  [ McGroupId=" );
            print_dec( i, 3, '\0' );
            print( ", McAddr=0x" );
            print_hex( mcAddress[i], 8 );
            print( " ]" );
            print_newline();
        }
    }
#endif

    return RMTMC_STATUS_OK;
}

/*!
 * Process received command; MulticastSetupReq
 */
static RmtMcStatus_t LoRaRmtMcProcessMulticastSetupReq( uint8_t         *p_buffer, 
                                                        uint8_t         *p_payloadLen, 
                                                        uint8_t         bufferMaxSize, 
                                                        uint32_t        *p_txDelayMs,
                                                        RmtMc_RxCmd_t   *p_rcCmd )
{
    RmtMc_SetupReq_t    *p_cmdSetupReq;
    LoRaMacStatus_t     macStatus;
    McChannelSetup_t    mcSetup;
    RmtMc_TimerMng_t    *p_rmtMcTimerMng;

    // length check
    if( bufferMaxSize < (RMTMC_PLEN_MC_GROUP_SETUP_ANS + 1) )  // +1 = CID length
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }

    // init
    p_cmdSetupReq = (RmtMc_SetupReq_t *)&( p_rcCmd->reqCmdParam );

    // try to delete multicast channel
    if( p_cmdSetupReq->groupId < RMTMC_CONFIG_MAX_MC_CTX )
    {
        macStatus = LoRaMacMcChannelDelete( (AddressIdentifier_t)( p_cmdSetupReq->groupId ) );
        if( macStatus == LORAMAC_STATUS_OK )
        {
            p_rmtMcTimerMng = &( RmtMcTimerMng[ p_cmdSetupReq->groupId ] );
            TimerStop( &( p_rmtMcTimerMng->RmtMcStart ) );
            TimerStop( &( p_rmtMcTimerMng->RmtMcTimeout ) );

            if( ( p_rmtMcTimerMng->isStart == true ) &&
                ( p_rmtMcTimerMng->sessionTimeout != 0 ) )
            {
                // Start-session has been notified to upper, but end-session is not.
                LoRaRmtMcSwitchDeviceClass( p_rmtMcTimerMng->classBeforeSession, NULL );
                (*RmtMcEventCbFuncs.LoRaRmtMcSessionEndIndication)( p_rmtMcTimerMng->class, 
                                                                    p_cmdSetupReq->groupId );
            }
        }
    }

    // check
    if( p_cmdSetupReq->groupId < RMTMC_CONFIG_MAX_MC_CTX )
    {
        mcSetup.GroupID   = (AddressIdentifier_t)( p_cmdSetupReq->groupId );
        mcSetup.Address   = p_cmdSetupReq->mcAddr;
        mcSetup.McKeyE    = &( p_cmdSetupReq->mcKeyEnc[0] );
        mcSetup.FCountMin = p_cmdSetupReq->minFCnt;
        mcSetup.FCountMax = p_cmdSetupReq->maxFCnt;
        macStatus = LoRaMacMcChannelSetup( &mcSetup );
        if( macStatus == LORAMAC_STATUS_OK )
        {
            // init (stop) timer
            p_rmtMcTimerMng = &( RmtMcTimerMng[ p_cmdSetupReq->groupId ] );
            TimerStop( &( p_rmtMcTimerMng->RmtMcStart ) );
            TimerStop( &( p_rmtMcTimerMng->RmtMcTimeout ) );
        
            p_rmtMcTimerMng->event          = RMTMC_TIMER_EVENT_NONE;
            p_rmtMcTimerMng->class          = CLASS_A;  // init
            p_rmtMcTimerMng->sessionTimeout = 0;
            p_rmtMcTimerMng->isStart        = false;
        
            // store LoRaMacMcChannelSetup() parameter
            memcpy1( (uint8_t *)&(p_rmtMcTimerMng->mcSetup), (uint8_t *)&mcSetup, sizeof(McChannelSetup_t) );
            memcpy1( &(p_rmtMcTimerMng->mcKeyE[0]), &( p_cmdSetupReq->mcKeyEnc[0] ), 16 );
            p_rmtMcTimerMng->mcSetup.McKeyE = &(p_rmtMcTimerMng->mcKeyE[0]);
        }
    }
    else
    {
        // unsupported groupId
        macStatus = LORAMAC_STATUS_MC_GROUP_UNDEFINED;
    }

    // make McGroupSetupAns
    if( ( macStatus == LORAMAC_STATUS_OK ) || 
        ( macStatus == LORAMAC_STATUS_MC_GROUP_UNDEFINED ) )
    {
        p_buffer[0] = RMTMC_CID_MC_GROUP_SETUP_ANS;
        p_buffer[1] = p_cmdSetupReq->groupId & 0x03;
        if( macStatus == LORAMAC_STATUS_MC_GROUP_UNDEFINED )
        {
            p_buffer[1] |= 0x04;  // bit2: IDerror
        }

        (*p_payloadLen) = RMTMC_PLEN_MC_GROUP_SETUP_ANS + 1;  // +1 = CID length

#ifdef DEBUG_RMTMC
        print( "*RMTMC:MulticastSetupAns" );
        print_newline();
        print( "*RMTMC:  McGroupID=" );
        print_dec( p_cmdSetupReq->groupId, 3, '\0' );
        print( ", IDerror=0x" );
        print_hex( (p_buffer[1] & 0xFC), 2 );
        print_newline();
#endif
    }
    else
    {
        (*p_payloadLen) = 0;
    }

    return RMTMC_STATUS_OK;
}

/*!
 * Process received command; MulticastDeleteReq
 */
static RmtMcStatus_t LoRaRmtMcProcessMulticastDeleteReq( uint8_t        *p_buffer, 
                                                         uint8_t        *p_payloadLen, 
                                                         uint8_t        bufferMaxSize, 
                                                         uint32_t       *p_txDelayMs,
                                                         RmtMc_RxCmd_t  *p_rcCmd )
{
    uint8_t             groupId;
    LoRaMacStatus_t     macStatus;
    RmtMc_TimerMng_t    *p_rmtMcTimerMng;

    // length check
    if( bufferMaxSize < (RMTMC_PLEN_MC_GROUP_DELETE_ANS + 1) )  // +1 = CID length
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }

    // init
    groupId = p_rcCmd->reqCmdParam.deleteReq.groupId;

    // check
    if( groupId < RMTMC_CONFIG_MAX_MC_CTX )
    {
        macStatus = LoRaMacMcChannelDelete( (AddressIdentifier_t)groupId );
        if( macStatus == LORAMAC_STATUS_OK )
        {
            // init (stop) timer
            p_rmtMcTimerMng = &( RmtMcTimerMng[ groupId ] );
            TimerStop( &( p_rmtMcTimerMng->RmtMcStart ) );
            TimerStop( &( p_rmtMcTimerMng->RmtMcTimeout ) );
        
            if( ( p_rmtMcTimerMng->isStart == true ) &&
                ( p_rmtMcTimerMng->sessionTimeout != 0 ) )
            {
                // Start-session has been notified to upper, but end-session is not.
                LoRaRmtMcSwitchDeviceClass( p_rmtMcTimerMng->classBeforeSession, NULL );
                (*RmtMcEventCbFuncs.LoRaRmtMcSessionEndIndication)( p_rmtMcTimerMng->class,
                                                                    groupId );
            }
        
            p_rmtMcTimerMng->event          = RMTMC_TIMER_EVENT_NONE;
            p_rmtMcTimerMng->class          = CLASS_A;  // init
            p_rmtMcTimerMng->sessionTimeout = 0;
            p_rmtMcTimerMng->isStart        = false;
        }
    }
    else
    {
        // unsupported groupId
        macStatus = LORAMAC_STATUS_MC_GROUP_UNDEFINED;
    }

    // make McGroupDeleteAns
    if( ( macStatus == LORAMAC_STATUS_OK ) || 
        ( macStatus == LORAMAC_STATUS_MC_GROUP_UNDEFINED ) )
    {
        p_buffer[0] = RMTMC_CID_MC_GROUP_DELETE_ANS;
        p_buffer[1] = groupId & 0x03;
        if( macStatus == LORAMAC_STATUS_MC_GROUP_UNDEFINED )
        {
            p_buffer[1] |= 0x04;  // bit2: MCGroupUndefined
        }

        (*p_payloadLen) = RMTMC_PLEN_MC_GROUP_DELETE_ANS + 1;  // +1 = CID length
    }
    else
    {
        (*p_payloadLen) = 0;
    }

#ifdef DEBUG_RMTMC
    print( "*RMTMC:MulticastDeleteAns" );
    print_newline();
    print( "*RMTMC:  McGroupID=" );
    print_dec( groupId, 3, '\0' );
    print( ", MCGroupUndefined=0x" );
    print_hex( (p_buffer[1] & 0xFC), 2 );
    print_newline();
#endif

    return RMTMC_STATUS_OK;
}

/*!
 * Process received command; MulticastClassCSessionReq
 */
static RmtMcStatus_t LoRaRmtMcProcessMulticastClassCSessionReq( uint8_t         *p_buffer, 
                                                                uint8_t         *p_payloadLen, 
                                                                uint8_t         bufferMaxSize, 
                                                                uint32_t        *p_txDelayMs,
                                                                RmtMc_RxCmd_t   *p_rcCmd )
{
    RmtMc_ClassCSessionReq_t    *p_cmdCSessReq;
    LoRaMacStatus_t             macStatus;
    McRxParams_t                rxParams;
    uint32_t                    timeToStart, curTime;
    uint8_t                     mcStatus;
    RmtMc_TimerMng_t            *p_rmtMcTimerMng;

    // length check
    if( bufferMaxSize < (RMTMC_PLEN_MC_CLASSC_SESSION_ANS + 1) )  // +1 = CID length
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }

    // init
    (*p_payloadLen) = 0;
    p_cmdCSessReq   = (RmtMc_ClassCSessionReq_t *)&( p_rcCmd->reqCmdParam );
    p_rmtMcTimerMng = NULL;

    // check
    rxParams.ClassC.Frequency = p_cmdCSessReq->frequency;
    rxParams.ClassC.Datarate  = p_cmdCSessReq->dr;

    curTime     = (*RmtMcEventCbFuncs.LoRaRmtMcCurrentTimeSecReqCb)();
    timeToStart = p_cmdCSessReq->sessionTime - curTime;  // Maximum acceptable difference is 0x7FFFFF (sec)
    if( ((int32_t)timeToStart < 0) || (curTime == 0) )
    {
        // SessionTime is past time
        // or AppTime is not synchronized
        timeToStart = (uint32_t)(-1);
    }

    if( timeToStart == (uint32_t)(-1) )
    {
        // SessionTime is past time
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        mcStatus  = 0x20;  // bit5: StartMissed
        mcStatus |= (p_cmdCSessReq->groupId & 0x03);
        macStatus = LORAMAC_STATUS_OK;
#else  // FUOTA V1.0.0
        return RMTMC_STATUS_PARAMETER_INVALID;  // cannot reply
#endif
    }
    else if( timeToStart > 0x00FFFFFF )
    {
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        // Set 0x00FFFFFF If TimeToStart is greater than 16,777,215 seconds ((2^24)-1).
        // This will inform the Application Server that the end-device clock is out of synchronization.
        timeToStart = 0x00FFFFFF;

        mcStatus  = 0x00;
        mcStatus |= (p_cmdCSessReq->groupId & 0x03);
        macStatus = LORAMAC_STATUS_OK;
#else  // FUOTA V1.0.0
        return RMTMC_STATUS_PARAMETER_INVALID;  // cannot reply
#endif
    }
    else
    {
        if( p_cmdCSessReq->groupId < RMTMC_CONFIG_MAX_MC_CTX )
        {
            macStatus = LoRaMacMcChannelSetupRxParams( (AddressIdentifier_t)( p_cmdCSessReq->groupId ), 
                                                       CLASS_C, &rxParams, &mcStatus );
            if( macStatus == LORAMAC_STATUS_OK )
            {
                p_rmtMcTimerMng = &( RmtMcTimerMng[ p_cmdCSessReq->groupId ] );

                // reset McChannel not to start now.
                LoRaMacMcChannelDelete( (AddressIdentifier_t)( p_cmdCSessReq->groupId ) );
                LoRaMacMcChannelSetup( &( p_rmtMcTimerMng->mcSetup ) );
                // store LoRaMacMcChannelSetupRxParams() parameter
                memcpy1( (uint8_t *)&(p_rmtMcTimerMng->rxParams), (uint8_t *)&rxParams, sizeof(McRxParams_t) );

                // init (restart) timer
                TimerStop( &( p_rmtMcTimerMng->RmtMcStart ) );
                TimerStop( &( p_rmtMcTimerMng->RmtMcTimeout ) );

                if( ( p_rmtMcTimerMng->isStart == true ) &&
                    ( p_rmtMcTimerMng->sessionTimeout != 0 ) )
                {
                    // Start-session has been notified to upper, but end-session is not.
                    LoRaRmtMcSwitchDeviceClass( p_rmtMcTimerMng->classBeforeSession, NULL );
                    (*RmtMcEventCbFuncs.LoRaRmtMcSessionEndIndication)( p_rmtMcTimerMng->class, 
                                                                        p_cmdCSessReq->groupId );
                }

                // Prepare timer
                //   max sessionTimeout = 1000 * 2^15 = 0x01F40000 (msec)
                p_rmtMcTimerMng->class = CLASS_C;
                p_rmtMcTimerMng->sessionTimeout  = (uint32_t)1 << p_cmdCSessReq->sessionTimeout;
                p_rmtMcTimerMng->sessionTimeout *= (uint32_t)1000;
                // Start timer
                p_rmtMcTimerMng->event   = RMTMC_TIMER_EVENT_NONE;
                p_rmtMcTimerMng->isStart = false;
                TimerSetValue( &( p_rmtMcTimerMng->RmtMcStart ), ( timeToStart * 1000 ) );
                TimerStart( &( p_rmtMcTimerMng->RmtMcStart ) );
            }
        }
        else
        {
            // undefined groupID
            macStatus = LORAMAC_STATUS_MC_GROUP_UNDEFINED;
            mcStatus  = 0x10 | p_cmdCSessReq->groupId;  // bit4: McGroupUndefined
        }
   }

    // make McClassCSessionAns
    if( macStatus != LORAMAC_STATUS_BUSY )
    {
        p_buffer[0] = RMTMC_CID_MC_CLASSC_SESSION_ANS;
        p_buffer[1] = mcStatus;
        if( (p_rmtMcTimerMng != NULL) && ((mcStatus & 0xFC) == 0x00) )
        {
            p_buffer[2] = (  timeToStart & 0x000000FF );
            p_buffer[3] = ( (timeToStart & 0x0000FF00) >> 8 );
            p_buffer[4] = ( (timeToStart & 0x00FF0000) >> 16 );

            (*p_payloadLen) = RMTMC_PLEN_MC_CLASSC_SESSION_ANS + 1;  // +1 = CID length

            // notify pre-start session to upper
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
            if( timeToStart < 0x00FFFFFF )
#endif
            {
                (*RmtMcEventCbFuncs.LoRaRmtMcSessionSetupIndication)( p_rmtMcTimerMng->class,
                                                                      p_cmdCSessReq->groupId,
                                                                      timeToStart,
                                                                      p_rmtMcTimerMng->sessionTimeout / (uint32_t)1000 );
            }
        }
        else
        {
            // McClassCSessionAns does not have TimeToStart
            (*p_payloadLen) = RMTMC_PLEN_MC_CLASSC_SESSION_ANS - 2;  // -2 = CID(1) - TimeToStart(3)
        }

#ifdef DEBUG_RMTMC
        print( "*RMTMC:MulticastClassCSessionAns" );
        print_newline();
        print( "*RMTMC:  McGroupID=" );
        print_dec( (mcStatus & 0x03), 3, '\0' );
        print( ", Status=0x" );
        print_hex( (mcStatus & 0xFC), 2 );
        print( ", TimeToStart=" );
        print_dec( timeToStart, 10, '\0' );
        print_newline();
#endif
    }

    return RMTMC_STATUS_OK;
}

#ifdef LORAMAC_CLASSB_ENABLED
/*!
 * Process received command; MulticastClassBSessionReq
 */
static RmtMcStatus_t LoRaRmtMcProcessMulticastClassBSessionReq( uint8_t         *p_buffer, 
                                                                uint8_t         *p_payloadLen, 
                                                                uint8_t         bufferMaxSize, 
                                                                uint32_t        *p_txDelayMs,
                                                                RmtMc_RxCmd_t   *p_rcCmd )
{
    RmtMc_ClassBSessionReq_t    *p_cmdBSessReq;
    LoRaMacStatus_t             macStatus;
    McRxParams_t                rxParams;
    uint32_t                    timeToStart, curTime;
    uint8_t                     mcStatus;
    RmtMc_TimerMng_t            *p_rmtMcTimerMng;

    // length check
    if( bufferMaxSize < (RMTMC_PLEN_MC_CLASSB_SESSION_ANS + 1) )  // +1 = CID length
    {
        return RMTMC_STATUS_LENGTH_ERROR;
    }

    // init
    (*p_payloadLen) = 0;
    p_cmdBSessReq   = (RmtMc_ClassBSessionReq_t *)&( p_rcCmd->reqCmdParam );
    p_rmtMcTimerMng = NULL;

    // check
    rxParams.ClassB.Frequency   = p_cmdBSessReq->frequency;
    rxParams.ClassB.Datarate    = p_cmdBSessReq->dr;
    rxParams.ClassB.Periodicity = p_cmdBSessReq->periodicity;

    curTime     = (*RmtMcEventCbFuncs.LoRaRmtMcCurrentTimeSecReqCb)();
    timeToStart = p_cmdBSessReq->sessionTime - curTime;  // Maximum acceptable difference is 0x007FFFFF (sec)
    if( ((int32_t)timeToStart < 0) || (curTime == 0) )
    {
        // SessionTime is past time
        // or AppTime is not synchronized
        timeToStart = (uint32_t)(-1);
    }

    if( timeToStart == (uint32_t)(-1) )
    {
        // SessionTime is past time
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        mcStatus  = 0x20;  // bit5: StartMissed
        mcStatus |= (p_cmdBSessReq->groupId & 0x03);
        macStatus = LORAMAC_STATUS_OK;
#else  // FUOTA V1.0.0
        return RMTMC_STATUS_PARAMETER_INVALID;  // cannot reply
#endif
    }
    else if( timeToStart > 0x00FFFFFF )
    {
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        // Set 0x00FFFFFF If TimeToStart is greater than 16,777,215 seconds ((2^24)-1).
        // This will inform the Application Server that the end-device clock is out of synchronization.
        timeToStart = 0x00FFFFFF;

        mcStatus  = 0x00;
        mcStatus |= (p_cmdBSessReq->groupId & 0x03);
        macStatus = LORAMAC_STATUS_OK;
#else  // FUOTA V1.0.0
        return RMTMC_STATUS_PARAMETER_INVALID;  // cannot reply
#endif
    }
    else
    {
        if( p_cmdBSessReq->groupId < RMTMC_CONFIG_MAX_MC_CTX )
        {
            macStatus = LoRaMacMcChannelSetupRxParams( (AddressIdentifier_t)p_cmdBSessReq->groupId, 
                                                       CLASS_B, &rxParams, &mcStatus );
            if( macStatus == LORAMAC_STATUS_OK )
            {
                p_rmtMcTimerMng = &( RmtMcTimerMng[ p_cmdBSessReq->groupId ] );

                // reset McChannel not to start now.
                LoRaMacMcChannelDelete( (AddressIdentifier_t)( p_cmdBSessReq->groupId ) );
                LoRaMacMcChannelSetup( &( p_rmtMcTimerMng->mcSetup ) );
                // store LoRaMacMcChannelSetupRxParams() parameter
                memcpy1( (uint8_t *)&(p_rmtMcTimerMng->rxParams), (uint8_t *)&rxParams, sizeof(McRxParams_t) );

                // init (restart) timer
                TimerStop( &( p_rmtMcTimerMng->RmtMcStart ) );
                TimerStop( &( p_rmtMcTimerMng->RmtMcTimeout ) );

                if( ( p_rmtMcTimerMng->isStart == true ) &&
                    ( p_rmtMcTimerMng->sessionTimeout != 0 ) )
                {
                    // Start-session has been notified to upper, but end-session is not.
                    LoRaRmtMcSwitchDeviceClass( p_rmtMcTimerMng->classBeforeSession, NULL );
                    (*RmtMcEventCbFuncs.LoRaRmtMcSessionEndIndication)( p_rmtMcTimerMng->class, 
                                                                        p_cmdBSessReq->groupId );
                }

                // Prepare timer
                //   max sessionTimeout = 128000 * 2^15 = 0xFA000000 (msec)
                p_rmtMcTimerMng->class = CLASS_B;
                p_rmtMcTimerMng->sessionTimeout  = (uint32_t)1 << p_cmdBSessReq->sessionTimeout;
                p_rmtMcTimerMng->sessionTimeout *= (uint32_t)128000;  // 128sec
                // Start timer
                p_rmtMcTimerMng->event   = RMTMC_TIMER_EVENT_NONE;
                p_rmtMcTimerMng->isStart = false;
                TimerSetValue( &( p_rmtMcTimerMng->RmtMcStart ), ( timeToStart * 1000 ) );
                TimerStart( &( p_rmtMcTimerMng->RmtMcStart ) );
            }
        }
        else
        {
            // undefined groupID
            macStatus = LORAMAC_STATUS_MC_GROUP_UNDEFINED;
            mcStatus  = 0x10 | p_cmdBSessReq->groupId;  // bit4: McGroupUndefined
        }
    }

    // make McClassCSessionAns
    if( macStatus != LORAMAC_STATUS_BUSY )
    {
        p_buffer[0] = RMTMC_CID_MC_CLASSB_SESSION_ANS;
        p_buffer[1] = mcStatus;
        if( (p_rmtMcTimerMng != NULL) && ((mcStatus & 0xFC) == 0x00) )
        {
            p_buffer[2] = (  timeToStart & 0x000000FF );
            p_buffer[3] = ( (timeToStart & 0x0000FF00) >> 8 );
            p_buffer[4] = ( (timeToStart & 0x00FF0000) >> 16 );

            (*p_payloadLen) = RMTMC_PLEN_MC_CLASSB_SESSION_ANS + 1;  // +1 = CID length

            // notify pre-start session to upper
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
            if( timeToStart < 0x00FFFFFF )
#endif
            {
                (*RmtMcEventCbFuncs.LoRaRmtMcSessionSetupIndication)( p_rmtMcTimerMng->class,
                                                                      p_cmdBSessReq->groupId,
                                                                      timeToStart,
                                                                      p_rmtMcTimerMng->sessionTimeout / (uint32_t)1000 );
            }
        }
        else
        {
            // McClassBSessionAns does not have TimeToStart
            (*p_payloadLen) = RMTMC_PLEN_MC_CLASSB_SESSION_ANS - 2;  // -2 = CID(1) - TimeToStart(3)
        }

#ifdef DEBUG_RMTMC
        print( "*RMTMC:MulticastClassBSessionAns" );
        print_newline();
        print( "*RMTMC:  McGroupID=" );
        print_dec( (mcStatus & 0x03), 3, '\0' );
        print( ", Status=0x" );
        print_hex( (mcStatus & 0xFC), 2 );
        print( ", TimeToStart=" );
        print_dec( timeToStart, 10, '\0' );
        print_newline();
#endif
    }

    return RMTMC_STATUS_OK;
}
#endif

//--------------------------------------------------------------------------------------------------

/*!
 * Timer interrupt; start multicast
 * It isn't necessary for context to disable interrupt for accessing "p_rmtMcTimerMng->event".
 */
static void LoRaRmtMcOnSessionStartTimerEvent( uint8_t groupId )
{
    RmtMc_TimerMng_t *p_rmtMcTimerMng;

    // init
    p_rmtMcTimerMng = &( RmtMcTimerMng[ groupId ] );

    p_rmtMcTimerMng->event = RMTMC_TIMER_EVENT_START;
}

static void LoRaRmtMcOnSessionStartTimerEvent_Grp0( void )
{
    LoRaRmtMcOnSessionStartTimerEvent( 0 );
}
#if RMTMC_CONFIG_MAX_MC_CTX >= 2
static void LoRaRmtMcOnSessionStartTimerEvent_Grp1( void )
{
    LoRaRmtMcOnSessionStartTimerEvent( 1 );
}
#endif
#if RMTMC_CONFIG_MAX_MC_CTX >= 3
static void LoRaRmtMcOnSessionStartTimerEvent_Grp2( void )
{
    LoRaRmtMcOnSessionStartTimerEvent( 2 );
}
#endif
#if RMTMC_CONFIG_MAX_MC_CTX == 4
static void LoRaRmtMcOnSessionStartTimerEvent_Grp3( void )
{
    LoRaRmtMcOnSessionStartTimerEvent( 3 );
}
#endif
/*!
 * Timer interrupt; stop/timeout multicast
 * It isn't necessary for context to disable interrupt for accessing "p_rmtMcTimerMng->event".
 */
static void LoRaRmtMcOnSessionTimeoutTimerEvent( uint8_t groupId )
{
    RmtMc_TimerMng_t *p_rmtMcTimerMng;

    // init
    p_rmtMcTimerMng = &( RmtMcTimerMng[ groupId ] );

    p_rmtMcTimerMng->event = RMTMC_TIMER_EVENT_TIMEOUT;
}

static void LoRaRmtMcOnSessionTimeoutTimerEvent_Grp0( void )
{
    LoRaRmtMcOnSessionTimeoutTimerEvent( 0 );
}
#if RMTMC_CONFIG_MAX_MC_CTX >= 2
static void LoRaRmtMcOnSessionTimeoutTimerEvent_Grp1( void )
{
    LoRaRmtMcOnSessionTimeoutTimerEvent( 1 );
}
#endif
#if RMTMC_CONFIG_MAX_MC_CTX >= 3
static void LoRaRmtMcOnSessionTimeoutTimerEvent_Grp2( void )
{
    LoRaRmtMcOnSessionTimeoutTimerEvent( 2 );
}
#endif
#if RMTMC_CONFIG_MAX_MC_CTX == 4
static void LoRaRmtMcOnSessionTimeoutTimerEvent_Grp3( void )
{
    LoRaRmtMcOnSessionTimeoutTimerEvent( 3 );
}
#endif

//--------------------------------------------------------------------------------------------------

static void LoRaRmtMcSwitchDeviceClass( DeviceClass_t classChg, DeviceClass_t *p_classBeforeChg )
{
    MibRequestConfirm_t mibReq;
    mibReq.Type = MIB_DEVICE_CLASS;
    LoRaMacMibGetRequestConfirm( &mibReq );

    // store device class before change
    if( p_classBeforeChg != NULL )
    {
        (*p_classBeforeChg) = mibReq.Param.Class;
    }

    if( mibReq.Param.Class != classChg )
    {
        // ClassB<->C : Can't switch directly.
       if( ( mibReq.Param.Class != CLASS_A ) && ( classChg != CLASS_A ) )
        {
            mibReq.Param.Class = CLASS_A;
            LoRaMacMibSetRequestConfirm( &mibReq );
        }

       mibReq.Param.Class = classChg;
       LoRaMacMibSetRequestConfirm( &mibReq );
    }
    else
    {
        // nothing to do : same device class
    }
}

