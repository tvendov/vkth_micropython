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

#include "LoRaFirmwareManagementProcess.h"

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)  // Only V2.0.0 is available

/*** Firmware management protocol command ***/
/* macros */
#define FWMNG_CID_PACKAGE_VERSION_REQ       0x00
#define FWMNG_CID_PACKAGE_VERSION_ANS       0x00
#define FWMNG_CID_DEV_VERSION_REQ           0x01
#define FWMNG_CID_DEV_VERSION_ANS           0x01
#define FWMNG_CID_DEV_REBOOT_TIME_REQ       0x02
#define FWMNG_CID_DEV_REBOOT_TIME_ANS       0x02
#define FWMNG_CID_DEV_REBOOT_COUNTDOWN_REQ  0x03
#define FWMNG_CID_DEV_REBOOT_COUNTDOWN_ANS  0x03
#define FWMNG_CID_DEV_UPGRADE_IMAGE_REQ     0x04
#define FWMNG_CID_DEV_UPGRADE_IMAGE_ANS     0x04
#define FWMNG_CID_DEV_DELETE_IMAGE_REQ      0x05
#define FWMNG_CID_DEV_DELETE_IMAGE_ANS      0x05

#define FWMNG_PLEN_PACKAGE_VERSION_REQ      (0)
#define FWMNG_PLEN_PACKAGE_VERSION_ANS      (2)
#define FWMNG_PLEN_DEV_VERSION_REQ          (0)
#define FWMNG_PLEN_DEV_VERSION_ANS          (8)
#define FWMNG_PLEN_DEV_REBOOT_TIME_REQ      (4)
#define FWMNG_PLEN_DEV_REBOOT_TIME_ANS      (4)
#define FWMNG_PLEN_DEV_REBOOT_COUNTDOWN_REQ (3)
#define FWMNG_PLEN_DEV_REBOOT_COUNTDOWN_ANS (3)
#define FWMNG_PLEN_DEV_UPGRADE_IMAGE_REQ    (0)
#define FWMNG_PLEN_DEV_UPGRADE_IMAGE_ANS    (5)
#define FWMNG_PLEN_DEV_DELETE_IMAGE_REQ     (4)
#define FWMNG_PLEN_DEV_DELETE_IMAGE_ANS     (1)

#define FWMNG_RXCMD_QUEUENUM                (2)

/* typedef */
typedef struct {
    uint32_t    rebootTime;
} FwMng_DevRebootTimeReq_t;

typedef struct {
    uint32_t    countdown;
} FwMng_DevRebootCountdownReq_t;

typedef struct {
    uint32_t    fwToDeleteVersion;
} FwMng_DevDeleteImageReq_t;

typedef struct
{
    uint8_t     cid;
    union {
        FwMng_DevRebootTimeReq_t        devRbtTimeReq;
        FwMng_DevRebootCountdownReq_t   devRbtCntdwnReq;
        FwMng_DevDeleteImageReq_t       devDelImgReq;
    } reqCmdParam;
} FwMng_RxCmd_t;

typedef struct
{
    FwMng_RxCmd_t   rxCmdQueue[ FWMNG_RXCMD_QUEUENUM ];
    uint8_t         numCmd;
} FwMng_RxCmdQueue_t;

/* global variable */
FwMng_RxCmdQueue_t FwMngCmdQueue;

/* function prototype */
static FwMngStatus_t LoRaFwMngHandlePackageVersionReq( uint8_t *p_buffer, size_t length );
static FwMngStatus_t LoRaFwMngHandleDevVersionReq( uint8_t *p_buffer, size_t length );
static FwMngStatus_t LoRaFwMngHandleDevRebootTimeReq( uint8_t *p_buffer, size_t length );
static FwMngStatus_t LoRaFwMngHandleDevRebootCountdownReq( uint8_t *p_buffer, size_t length );
static FwMngStatus_t LoRaFwMngHandleDevUpgradeImageReq( uint8_t *p_buffer, size_t length );
static FwMngStatus_t LoRaFwMngHandleDevDeleteImageReq( uint8_t *p_buffer, size_t length );

static FwMngStatus_t LoRaFwMngProcessPackageVersionReq( uint8_t        *p_buffer, 
                                                        uint8_t        *p_payloadLen, 
                                                        uint8_t        bufferMaxSize, 
                                                        uint32_t       *p_txDelayMs,
                                                        FwMng_RxCmd_t  *p_rcCmd/*nouse*/ );
static FwMngStatus_t LoRaFwMngProcessDevVersionReq( uint8_t       *p_buffer, 
                                                    uint8_t       *p_payloadLen, 
                                                    uint8_t       bufferMaxSize,
                                                    uint32_t      *p_txDelayMs,
                                                    FwMng_RxCmd_t *p_rcCmd );
static FwMngStatus_t LoRaFwMngProcessDevRebootTimeReq( uint8_t         *p_buffer, 
                                                       uint8_t         *p_payloadLen, 
                                                       uint8_t         bufferMaxSize,
                                                       uint32_t        *p_txDelayMs,
                                                       FwMng_RxCmd_t   *p_rcCmd );
static FwMngStatus_t LoRaFwMngProcessDevRebootCountdownReq( uint8_t        *p_buffer, 
                                                            uint8_t        *p_payloadLen, 
                                                            uint8_t        bufferMaxSize,
                                                            uint32_t       *p_txDelayMs,
                                                            FwMng_RxCmd_t  *p_rcCmd );
static FwMngStatus_t LoRaFwMngProcessDevUpgradeImageReq( uint8_t         *p_buffer, 
                                                         uint8_t         *p_payloadLen, 
                                                         uint8_t         bufferMaxSize, 
                                                         uint32_t        *p_txDelayMs,
                                                         FwMng_RxCmd_t   *p_rcCmd );
static FwMngStatus_t LoRaFwMngProcessDevDeleteImageReq( uint8_t         *p_buffer, 
                                                        uint8_t         *p_payloadLen, 
                                                        uint8_t         bufferMaxSize,
                                                        uint32_t        *p_txDelayMs,
                                                        FwMng_RxCmd_t   *p_rcCmd );

/*** Timer ***/
#define FWMNG_TIMER_EVENT_NONE          0x00
#define FWMNG_TIMER_EVENT_REBOOT_TIME   0x01

#define FWMNG_TIMER_RETRY_EVENT_MS  50  // msec for retry event procedure

typedef struct {
    uint8_t         event;
    bool            isWaitTimeToReboot;
    TimerEvent_t    rebootTimerObj;
} FwMng_TimerMng_t;

FwMng_TimerMng_t FwMngTimerMng;

/* function prototype */
static void LoRaFwMngOnRebootTimerEvent( void );

/*** Event; notify to upper ***/
LoRaFwMngEventCb_t  FwMngEventCbFuncs = {0};

/*** IB ***/
typedef struct
{
    uint32_t    __reserved;
} FwMng_IB_params_t;

FwMng_IB_params_t   FwMngIBParams;

/*** Firmware management state ***/
#define FWMNG_STATE_NONE            0x00
#define FWMNG_STATE_INITIALIZED     0x01
#define FWMNG_STATE_RUNNING         0x02
uint8_t FwMngState = FWMNG_STATE_NONE;

/*** else ***/


/*!
 * FirmwareManagement initialization
 */
FwMngStatus_t LoRaFirmwareManagementInit( LoRaFwMngEventCb_t *p_fwMngEventCb )
{
    // check
    if( p_fwMngEventCb == NULL )
    {
        return FWMNG_STATUS_PARAMETER_INVALID;
    }
    if( ( p_fwMngEventCb->LoRaFwMngVersionReqCb == NULL ) ||
        ( p_fwMngEventCb->LoRaFwMngCurrentTimeSecReqCb == NULL ) ||
        ( p_fwMngEventCb->LoRaFwMngRebootRequestEventCb == NULL ) ||
        ( p_fwMngEventCb->LoRaFwMngRebootCancelEventCb == NULL ) ||
        ( p_fwMngEventCb->LoRaFwMngRebootExecEventCb == NULL ) ||
        ( p_fwMngEventCb->LoRaFwMngUpImageStatusRequestCb == NULL ) ||
        ( p_fwMngEventCb->LoRaFwMngDeleteImageRequestEventCb == NULL ) )
    {
        return FWMNG_STATUS_PARAMETER_INVALID;
    }

    /* IB */
    memset1( (uint8_t *)&FwMngIBParams, 0x00, sizeof(FwMng_IB_params_t) );

    /* for command */
    memset1( (uint8_t *)&FwMngCmdQueue, 0x00, sizeof(FwMng_RxCmdQueue_t) );

    /* for timer */
    if( FwMngState != FWMNG_STATE_NONE )
    {
        TimerStop( &( FwMngTimerMng.rebootTimerObj ) );
    }
    memset1( (uint8_t *)&FwMngTimerMng, 0x00, sizeof(FwMng_TimerMng_t) );
    TimerInit( &( FwMngTimerMng.rebootTimerObj ), LoRaFwMngOnRebootTimerEvent );

    /* event */
    if( p_fwMngEventCb != &FwMngEventCbFuncs )  // means upper-layer requests initialization
    {
        memcpy1( (uint8_t *)&FwMngEventCbFuncs, (uint8_t *)p_fwMngEventCb, sizeof(LoRaFwMngEventCb_t) );
    }

    FwMngState = FWMNG_STATE_INITIALIZED;

    return FWMNG_STATUS_OK;
}

/*!
 * FirmwareManagement start
 */
FwMngStatus_t LoRaFirmwareManagementStart( void )
{
    FwMngStatus_t  res;

    // init
    res = FWMNG_STATUS_ERROR;

    if( FwMngState == FWMNG_STATE_INITIALIZED )
    {
        FwMngState = FWMNG_STATE_RUNNING;
        res = FWMNG_STATUS_OK;
    }

    return res;
}

/*!
 * FirmwareManagement stop
 */
void LoRaFirmwareManagementStop( void )
{
    if( FwMngState != FWMNG_STATE_NONE )
    {
        LoRaFirmwareManagementInit( &FwMngEventCbFuncs );
    }
}

/*!
 * MCPS-Indication event function for FirmwareManagement
 */
FwMngStatus_t LoRaFirmwareManagementMcpsIndication( McpsIndication_t *p_mcpsIndication )
{
    FwMngStatus_t   status, funcRet;
    uint8_t         *p_buffer;
    uint8_t         bufferSize;

    // FirmwareManagement is not running
    if( FwMngState != FWMNG_STATE_RUNNING )
    {
        return FWMNG_STATUS_ERROR;
    }

    // Reject error indication
    if ( p_mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK )
    {
        return FWMNG_STATUS_ERROR;
    }
    // FirmwareManagement messages SHALL NOT be sent using multicast.
    if( (p_mcpsIndication->McpsIndication != MCPS_UNCONFIRMED) &&
        (p_mcpsIndication->McpsIndication != MCPS_CONFIRMED) )
    {
        return FWMNG_STATUS_COMMAND_ERROR;
    }

    // init
    status     = FWMNG_STATUS_OK;
    p_buffer   = p_mcpsIndication->Buffer;
    bufferSize = p_mcpsIndication->BufferSize;

    memset1( (uint8_t *)&FwMngCmdQueue, 0x00, sizeof(FwMng_RxCmdQueue_t) );

    // Get FirmwareManagement commands
    while( bufferSize > 0 )
    {
        switch( *p_buffer )  //= CID
        {
            case FWMNG_CID_PACKAGE_VERSION_REQ:
                funcRet = LoRaFwMngHandlePackageVersionReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == FWMNG_STATUS_OK )
                {
                    bufferSize -= (FWMNG_PLEN_PACKAGE_VERSION_REQ + 1);  // +1 = CID length
                    p_buffer   += (FWMNG_PLEN_PACKAGE_VERSION_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case FWMNG_CID_DEV_VERSION_REQ:
                funcRet = LoRaFwMngHandleDevVersionReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == FWMNG_STATUS_OK )
                {
                    bufferSize -= (FWMNG_PLEN_DEV_VERSION_REQ + 1);  // +1 = CID length
                    p_buffer   += (FWMNG_PLEN_DEV_VERSION_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case FWMNG_CID_DEV_REBOOT_TIME_REQ:
                funcRet = LoRaFwMngHandleDevRebootTimeReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == FWMNG_STATUS_OK )
                {
                    bufferSize -= (FWMNG_PLEN_DEV_REBOOT_TIME_REQ + 1);  // +1 = CID length
                    p_buffer   += (FWMNG_PLEN_DEV_REBOOT_TIME_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case FWMNG_CID_DEV_REBOOT_COUNTDOWN_REQ:
                funcRet = LoRaFwMngHandleDevRebootCountdownReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == FWMNG_STATUS_OK )
                {
                    bufferSize -= (FWMNG_PLEN_DEV_REBOOT_COUNTDOWN_REQ + 1);  // +1 = CID length
                    p_buffer   += (FWMNG_PLEN_DEV_REBOOT_COUNTDOWN_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case FWMNG_CID_DEV_UPGRADE_IMAGE_REQ:
                funcRet = LoRaFwMngHandleDevUpgradeImageReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == FWMNG_STATUS_OK )
                {
                    bufferSize -= (FWMNG_PLEN_DEV_UPGRADE_IMAGE_REQ + 1);  // +1 = CID length
                    p_buffer   += (FWMNG_PLEN_DEV_UPGRADE_IMAGE_REQ + 1);
                }
                else
                {
                    bufferSize = 0;  // exit from while() loop
                }
                break;

            case FWMNG_CID_DEV_DELETE_IMAGE_REQ:
                funcRet = LoRaFwMngHandleDevDeleteImageReq( &( p_buffer[1] ), (bufferSize - 1) );
                if( funcRet == FWMNG_STATUS_OK )
                {
                    bufferSize -= (FWMNG_PLEN_DEV_DELETE_IMAGE_REQ + 1);  // +1 = CID length
                    p_buffer   += (FWMNG_PLEN_DEV_DELETE_IMAGE_REQ + 1);
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

    if( FwMngCmdQueue.numCmd == 0 )
    {
        status = FWMNG_STATUS_COMMAND_ERROR;
    }

    return status;
}

/*!
 * Process event of FirmwareManagement
 */
void LoRaFirmwareManagementProcessCommand( uint8_t  *p_buffer, 
                                           uint8_t  *p_payloadLen, 
                                           uint8_t  bufferMaxSize, 
                                           uint32_t *p_txDelayMs,
                                           bool     *p_isRetransEn )
{
    FwMngStatus_t   funcRet;
    FwMng_RxCmd_t   *p_rxCmd;
    uint8_t         payloadLen, payloadLenTotal;
    uint32_t        txDelayMs, tmpTxDelayMs;
    uint8_t         i;

    // FirmwareManagement is not running
    if( FwMngState != FWMNG_STATE_RUNNING )
    {
        return;
    }

    // init
    payloadLenTotal = 0;
    txDelayMs       = 0;

    for( i = 0; i < FwMngCmdQueue.numCmd; i++ )
    {
        // init (loop)
        p_rxCmd     = &( FwMngCmdQueue.rxCmdQueue[ i ] );
        tmpTxDelayMs = 0;

        switch( p_rxCmd->cid )
        {
            case FWMNG_CID_PACKAGE_VERSION_REQ:
                funcRet = LoRaFwMngProcessPackageVersionReq( p_buffer, 
                                                             &payloadLen, 
                                                             bufferMaxSize, 
                                                             &tmpTxDelayMs,
                                                             p_rxCmd );
                break;

            case FWMNG_CID_DEV_VERSION_REQ:
                funcRet = LoRaFwMngProcessDevVersionReq( p_buffer, 
                                                         &payloadLen, 
                                                         bufferMaxSize,
                                                         &tmpTxDelayMs,
                                                         p_rxCmd );
                break;

            case FWMNG_CID_DEV_REBOOT_TIME_REQ:
                funcRet = LoRaFwMngProcessDevRebootTimeReq( p_buffer, 
                                                            &payloadLen, 
                                                            bufferMaxSize, 
                                                            &tmpTxDelayMs,
                                                            p_rxCmd );
                break;

            case FWMNG_CID_DEV_REBOOT_COUNTDOWN_REQ:
                funcRet = LoRaFwMngProcessDevRebootCountdownReq( p_buffer, 
                                                                 &payloadLen, 
                                                                 bufferMaxSize,
                                                                 &tmpTxDelayMs,
                                                                 p_rxCmd );
                break;

            case FWMNG_CID_DEV_UPGRADE_IMAGE_REQ:
                funcRet = LoRaFwMngProcessDevUpgradeImageReq( p_buffer, 
                                                              &payloadLen, 
                                                              bufferMaxSize,
                                                              &tmpTxDelayMs,
                                                              p_rxCmd );
                break;

            case FWMNG_CID_DEV_DELETE_IMAGE_REQ:
                funcRet = LoRaFwMngProcessDevDeleteImageReq( p_buffer, 
                                                             &payloadLen, 
                                                             bufferMaxSize,
                                                             &tmpTxDelayMs,
                                                             p_rxCmd );
                break;

            default:
                funcRet = FWMNG_STATUS_COMMAND_ERROR;
                break;
        }

        if( funcRet == FWMNG_STATUS_OK )
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
    (*p_isRetransEn) = true;
}

/*!
 * Process timer interrupt of FirmwareManagement
 */
void LoRaFirmwareManagementProcessEvent( void )
{
    // FirmwareManagement is not running
    if( FwMngState != FWMNG_STATE_RUNNING )
    {
        return;
    }

    switch( FwMngTimerMng.event )
    {
        case FWMNG_TIMER_EVENT_REBOOT_TIME:
#ifdef DEBUG_FWMNG
            print( "*FWMNG:Reboot time has come" );
            print_newline();
#endif
            // notify to upper
            (*FwMngEventCbFuncs.LoRaFwMngRebootExecEventCb)();

            // clear event
            FwMngTimerMng.event = FWMNG_TIMER_EVENT_NONE;
            break;

        case FWMNG_TIMER_EVENT_NONE:
        default:
            break;
    }
}

/*!
 * Returns whether FirmwareManagement is idle
 * If FirmwareManagement is not idle (return false), LoRaFirmwareManagementProcessEvent() call is required
 */
bool LoRaFirmwareManagementIsIdle( void )
{
    bool    bRet;

    // init
    bRet = true;  // (init) idle - function call is not required

    if( FwMngState == FWMNG_STATE_RUNNING )
    {
        if( FwMngTimerMng.event != FWMNG_TIMER_EVENT_NONE )
        {
            bRet = false;  // not idle - function call is required
        }
    }

    return bRet;
}

/*!
 * Uplink result notification from upper layer
 */
void LoRaFirmwareManagementSendCompCommand( bool isSuccess )
{
    // nothing to do
}

/*!
 * IB Get Request
 */
FwMngStatus_t LoRaFirmwareManagementIbGetRequest( uint8_t ib, void *vpVal )
{
#if 0  // Currently nothing to do. If you want to add operations, please check the 'FwMngState'.
    if( FwMngState == FWMNG_STATE_NONE )
    {
        return FWMNG_STATUS_ERROR;
    }
    
    if( vpVal == NULL )
    {
        return FWMNG_STATUS_PARAMETER_INVALID;
    }
#endif

    // no IB
    return FWMNG_STATUS_SERVICE_UNKNOWN;
}

/*!
 * IB Set Request
 */
FwMngStatus_t LoRaFirmwareManagementIbSetRequest( uint8_t ib, void *vpVal )
{
#if 0  // Currently nothing to do. If you want to add operations, please check the 'FwMngState'.
    if( FwMngState == FWMNG_STATE_NONE )
    {
        return FWMNG_STATUS_ERROR;
    }
    
    if( vpVal == NULL )
    {
        return FWMNG_STATUS_PARAMETER_INVALID;
    }
#endif

    // no IB
    return FWMNG_STATUS_SERVICE_UNKNOWN;
}

/*!
 * Get FirmwareManagement command payload size
 */
FwMngStatus_t LoRaFirmwareManagementGetRcvdCmdPayloadLen( uint8_t cid, uint8_t *p_cmdPayloadLen )
{
    FwMngStatus_t   ret;

    // init
    ret = FWMNG_STATUS_OK;

    switch( cid )
    {
        case FWMNG_CID_PACKAGE_VERSION_REQ:
            (*p_cmdPayloadLen) = FWMNG_PLEN_PACKAGE_VERSION_REQ;
            break;

        case FWMNG_CID_DEV_VERSION_REQ:
            (*p_cmdPayloadLen) = FWMNG_PLEN_DEV_VERSION_REQ;
            break;

        case FWMNG_CID_DEV_REBOOT_TIME_REQ:
            (*p_cmdPayloadLen) = FWMNG_PLEN_DEV_REBOOT_TIME_REQ;
            break;

        case FWMNG_CID_DEV_REBOOT_COUNTDOWN_REQ:
            (*p_cmdPayloadLen) = FWMNG_PLEN_DEV_REBOOT_COUNTDOWN_REQ;
            break;

        case FWMNG_CID_DEV_UPGRADE_IMAGE_REQ:
            (*p_cmdPayloadLen) = FWMNG_PLEN_DEV_UPGRADE_IMAGE_REQ;
            break;

        case FWMNG_CID_DEV_DELETE_IMAGE_REQ:
            (*p_cmdPayloadLen) = FWMNG_PLEN_DEV_DELETE_IMAGE_REQ;
            break;

        default:
            ret = FWMNG_STATUS_COMMAND_ERROR;
            break;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------

/*!
 * check received command; PackageVersionReq
 */
static FwMngStatus_t LoRaFwMngHandlePackageVersionReq( uint8_t *p_buffer, size_t length )
{
    FwMng_RxCmd_t    *p_rxCmd;

    // payload length check
    //  ... it has no payload

    // command queue check
    if( FwMngCmdQueue.numCmd >= FWMNG_RXCMD_QUEUENUM )
    {
        return FWMNG_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FwMngCmdQueue.rxCmdQueue[ FwMngCmdQueue.numCmd ] );

    // get parameter
    p_rxCmd->cid = FWMNG_CID_PACKAGE_VERSION_REQ;

    FwMngCmdQueue.numCmd++;

#ifdef DEBUG_FWMNG
    print( "*FWMNG:PackageVersionReq" );
    print_newline();
#endif

    return FWMNG_STATUS_OK;
}

/*!
 * check received command; DevVersionReq
 */
static FwMngStatus_t LoRaFwMngHandleDevVersionReq( uint8_t *p_buffer, size_t length )
{
    FwMng_RxCmd_t        *p_rxCmd;

    // payload length check
    //  ... it has no payload

    // command queue check
    if( FwMngCmdQueue.numCmd >= FWMNG_RXCMD_QUEUENUM )
    {
        return FWMNG_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FwMngCmdQueue.rxCmdQueue[ FwMngCmdQueue.numCmd ] );

    // get parameter
    p_rxCmd->cid = FWMNG_CID_DEV_VERSION_REQ;

    FwMngCmdQueue.numCmd++;

#ifdef DEBUG_FWMNG
    print( "*FWMNG:DevVersionReq" );
    print_newline();
#endif

    return FWMNG_STATUS_OK;
}


/*!
 * check received command; DevRebootTimeReq
 */
static FwMngStatus_t LoRaFwMngHandleDevRebootTimeReq( uint8_t *p_buffer, size_t length )
{
    FwMng_RxCmd_t               *p_rxCmd;
    FwMng_DevRebootTimeReq_t    *p_cmdDevRbtTimeReq;

    // payload length check
    if( length < FWMNG_PLEN_DEV_REBOOT_TIME_REQ )
    {
        return FWMNG_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( FwMngCmdQueue.numCmd >= FWMNG_RXCMD_QUEUENUM )
    {
        return FWMNG_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FwMngCmdQueue.rxCmdQueue[ FwMngCmdQueue.numCmd ] );
    p_cmdDevRbtTimeReq = (FwMng_DevRebootTimeReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = FWMNG_CID_DEV_REBOOT_TIME_REQ;

    p_cmdDevRbtTimeReq->rebootTime = ((uint32_t)p_buffer[3] << 24) | ((uint32_t)p_buffer[2] << 16) |
                                     ((uint32_t)p_buffer[1] << 8)  |  (uint32_t)p_buffer[0];
    //p_buffer += 4;

    FwMngCmdQueue.numCmd++;
    
#ifdef DEBUG_FWMNG
    print( "*FWMNG:DevRebootTimeReq" );
    print_newline();
    print( "*FWMNG:  RebootTime=0x" );
    print_hex( p_cmdDevRbtTimeReq->rebootTime, 8 );
    print_newline();
#endif

    return FWMNG_STATUS_OK;
}

/*!
 * check received command; DevRebootCountdownReq
 */
static FwMngStatus_t LoRaFwMngHandleDevRebootCountdownReq( uint8_t *p_buffer, size_t length )
{
    FwMng_RxCmd_t                   *p_rxCmd;
    FwMng_DevRebootCountdownReq_t   *p_cmdDevRbtCntdwnReq;

    // payload length check
    if( length < FWMNG_PLEN_DEV_REBOOT_COUNTDOWN_REQ )
    {
        return FWMNG_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( FwMngCmdQueue.numCmd >= FWMNG_RXCMD_QUEUENUM )
    {
        return FWMNG_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FwMngCmdQueue.rxCmdQueue[ FwMngCmdQueue.numCmd ] );
    p_cmdDevRbtCntdwnReq = (FwMng_DevRebootCountdownReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = FWMNG_CID_DEV_REBOOT_COUNTDOWN_REQ;

    p_cmdDevRbtCntdwnReq->countdown = ((uint32_t)p_buffer[2] << 16) |
                                      ((uint32_t)p_buffer[1] << 8)  |
                                       (uint32_t)p_buffer[0];
    //p_buffer += 3;

    FwMngCmdQueue.numCmd++;

#ifdef DEBUG_FWMNG
    print( "*FWMNG:DevRebootCountdownReq" );
    print_newline();
    print( "*FWMNG:  Countdown=0x" );
    print_hex( p_cmdDevRbtCntdwnReq->countdown, 6 );
    print_newline();
#endif
    
    return FWMNG_STATUS_OK;
}

/*!
 * check received command; DevUpgradeImageReq
 */
static FwMngStatus_t LoRaFwMngHandleDevUpgradeImageReq( uint8_t *p_buffer, size_t length )
{
    FwMng_RxCmd_t        *p_rxCmd;

    // payload length check
    //  ... it has no payload

    // command queue check
    if( FwMngCmdQueue.numCmd >= FWMNG_RXCMD_QUEUENUM )
    {
        return FWMNG_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FwMngCmdQueue.rxCmdQueue[ FwMngCmdQueue.numCmd ] );

    // get parameter
    p_rxCmd->cid = FWMNG_CID_DEV_UPGRADE_IMAGE_REQ;

    FwMngCmdQueue.numCmd++;

#ifdef DEBUG_FWMNG
    print( "*FWMNG:DevUpgradeImageReq" );
    print_newline();
#endif

    return FWMNG_STATUS_OK;
}

/*!
 * check received command; DevDeleteImageReq
 */
static FwMngStatus_t LoRaFwMngHandleDevDeleteImageReq( uint8_t *p_buffer, size_t length )
{
    FwMng_RxCmd_t               *p_rxCmd;
    FwMng_DevDeleteImageReq_t   *p_cmdDevDelImgReq;

    // payload length check
    if( length < FWMNG_PLEN_DEV_DELETE_IMAGE_REQ )
    {
        return FWMNG_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( FwMngCmdQueue.numCmd >= FWMNG_RXCMD_QUEUENUM )
    {
        return FWMNG_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FwMngCmdQueue.rxCmdQueue[ FwMngCmdQueue.numCmd ] );
    p_cmdDevDelImgReq = (FwMng_DevDeleteImageReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = FWMNG_CID_DEV_DELETE_IMAGE_REQ;

    p_cmdDevDelImgReq->fwToDeleteVersion = ((uint32_t)p_buffer[3] << 24) | 
                                           ((uint32_t)p_buffer[2] << 16) | 
                                           ((uint32_t)p_buffer[1] << 8)  |  
                                            (uint32_t)p_buffer[0];
    //p_buffer += 4;

    FwMngCmdQueue.numCmd++;
    
#ifdef DEBUG_FWMNG
    print( "*FWMNG:DevDeleteImageReq" );
    print_newline();
    print( "*FWMNG:  FirmwareToDeleteVersion=0x" );
    print_hex( p_cmdDevDelImgReq->fwToDeleteVersion, 8 );
    print_newline();
#endif

    return FWMNG_STATUS_OK;
}


//--------------------------------------------------------------------------------------------------

/*!
 * Process received command; PackageVersionReq
 */
static FwMngStatus_t LoRaFwMngProcessPackageVersionReq( uint8_t        *p_buffer, 
                                                        uint8_t        *p_payloadLen, 
                                                        uint8_t        bufferMaxSize, 
                                                        uint32_t       *p_txDelayMs,
                                                        FwMng_RxCmd_t  *p_rcCmd/*nouse*/ )
{
    // length check
    if( bufferMaxSize < (FWMNG_PLEN_PACKAGE_VERSION_ANS + 1) )  // +1 = CID length
    {
        return FWMNG_STATUS_LENGTH_ERROR;
    }

    // make PackageVersionAns
    p_buffer[0] = FWMNG_CID_PACKAGE_VERSION_ANS;
    p_buffer[1] = FWMNG_PACKAGE_IDENTIFIER;
    p_buffer[2] = FWMNG_PACKAGE_VERSION;

    (*p_payloadLen) = FWMNG_PLEN_PACKAGE_VERSION_ANS + 1;  // +1 = CID length

#ifdef DEBUG_FWMNG
    print( "*FWMNG:PackageVersionAns" );
    print_newline();
    print( "*FWMNG:  PackageIdentifier=" );
    print_dec( p_buffer[1], 3, '\0' );
    print( ", PackageVersion=" );
    print_dec( p_buffer[2], 3, '\0' );
    print_newline();
#endif

    return FWMNG_STATUS_OK;
}

/*!
 * Process received command; DevVersionReq
 */
static FwMngStatus_t LoRaFwMngProcessDevVersionReq( uint8_t       *p_buffer, 
                                                    uint8_t       *p_payloadLen, 
                                                    uint8_t       bufferMaxSize,
                                                    uint32_t      *p_txDelayMs,
                                                    FwMng_RxCmd_t *p_rcCmd )
{
    uint32_t    fwVersion, hwVersion;

    // length check
    if( bufferMaxSize < (FWMNG_PLEN_DEV_VERSION_ANS + 1) )  // +1 = CID length
    {
        return FWMNG_STATUS_LENGTH_ERROR;
    }

    // process
    // ask the application
    (*FwMngEventCbFuncs.LoRaFwMngVersionReqCb)( &fwVersion, &hwVersion );

    // make DevVersionAns
    p_buffer[0] = FWMNG_CID_DEV_VERSION_ANS;
    p_buffer[1] = (uint8_t)(  fwVersion & 0x000000FF );
    p_buffer[2] = (uint8_t)( (fwVersion & 0x0000FF00) >> 8 ); 
    p_buffer[3] = (uint8_t)( (fwVersion & 0x00FF0000) >> 16 );
    p_buffer[4] = (uint8_t)( (fwVersion & 0xFF000000) >> 24 );
    p_buffer[5] = (uint8_t)(  hwVersion & 0x000000FF );
    p_buffer[6] = (uint8_t)( (hwVersion & 0x0000FF00) >> 8 ); 
    p_buffer[7] = (uint8_t)( (hwVersion & 0x00FF0000) >> 16 );
    p_buffer[8] = (uint8_t)( (hwVersion & 0xFF000000) >> 24 );

    (*p_payloadLen) = FWMNG_PLEN_DEV_VERSION_ANS + 1;  // +1 = CID length

#ifdef DEBUG_FWMNG
    print( "*FWMNG:DevVersionAns" );
    print_newline();
    print( "*FWMNG:  FWversion=0x" );
    print_hex( fwVersion, 8 );
    print( ", HWversion=0x" );
    print_hex( hwVersion, 8 );
    print_newline();
#endif

    return FWMNG_STATUS_OK;
}

/*!
 * Process received command; DevRebootTimeReq
 */
static FwMngStatus_t LoRaFwMngProcessDevRebootTimeReq( uint8_t         *p_buffer, 
                                                       uint8_t         *p_payloadLen, 
                                                       uint8_t         bufferMaxSize,
                                                       uint32_t        *p_txDelayMs,
                                                       FwMng_RxCmd_t   *p_rcCmd )
{
    FwMng_DevRebootTimeReq_t    *p_cmdDevRbtTimeReq;
    uint32_t                    rebootTime;
    uint32_t                    timeToRebootSec, curTimeSec;
    FwMngStatus_t               funcRes;

    // length check
    if( bufferMaxSize < (FWMNG_PLEN_DEV_REBOOT_TIME_ANS + 1) )  // +1 = CID length
    {
        return FWMNG_STATUS_LENGTH_ERROR;
    }

    // init
    p_cmdDevRbtTimeReq = (FwMng_DevRebootTimeReq_t *)&( p_rcCmd->reqCmdParam );
    rebootTime         = 0;

    // Cancel a currently programmed reboot
    if( FwMngTimerMng.isWaitTimeToReboot == true )
    {
        TimerStop( &( FwMngTimerMng.rebootTimerObj ) );
        FwMngTimerMng.isWaitTimeToReboot = false;
    
        (*FwMngEventCbFuncs.LoRaFwMngRebootCancelEventCb)();  // notify to upper
    }

    // process
    if( p_cmdDevRbtTimeReq->rebootTime == 0 )
    {
        // Reboot as soon as possible
        TimerStop( &( FwMngTimerMng.rebootTimerObj ) );
        FwMngTimerMng.isWaitTimeToReboot = false;

        // ask the application
        funcRes = (*FwMngEventCbFuncs.LoRaFwMngRebootRequestEventCb)( 0 );
        if( funcRes == FWMNG_STATUS_OK )
        {
            (*FwMngEventCbFuncs.LoRaFwMngRebootExecEventCb)();
        }
    }
    else
    {
        // prepare to reboot at specified time
        if( p_cmdDevRbtTimeReq->rebootTime != 0xFFFFFFFF )
        {
            funcRes         = FWMNG_STATUS_ERROR;  // init = not OK
            curTimeSec      = (*FwMngEventCbFuncs.LoRaFwMngCurrentTimeSecReqCb)();
            timeToRebootSec = p_cmdDevRbtTimeReq->rebootTime - curTimeSec;
            if( ( curTimeSec != 0 ) && ( (int32_t)timeToRebootSec >= 0 ) )
            {
                // ask the application
                funcRes = (*FwMngEventCbFuncs.LoRaFwMngRebootRequestEventCb)( timeToRebootSec );
            }
            
            // DevRebootTimeAns parameter
            if( funcRes == FWMNG_STATUS_OK )
            {
                rebootTime = timeToRebootSec;
            
                // start timer
                TimerSetValue( &( FwMngTimerMng.rebootTimerObj ), ( rebootTime * 1000 ) );
                TimerStart( &( FwMngTimerMng.rebootTimerObj ) );
                FwMngTimerMng.isWaitTimeToReboot = true;
            }
            else
            {
                // indicates the inability of the end-device to reboot at the requested time.
                rebootTime = 0;
            }
        }
        else  // if( p_cmdDevRbtTimeReq->rebootTime = 0xFFFFFFFF )
        {
            // DevRebootTimeAns parameter
            // acknowledges the cancellation of a currently programmed reboot
            rebootTime = 0xFFFFFFFF;
        }
    }

    // make DevRebootTimeAns
    if( p_cmdDevRbtTimeReq->rebootTime != 0 )
    {
        p_buffer[0] = FWMNG_CID_DEV_REBOOT_TIME_ANS;
        p_buffer[1] = (uint8_t)(  rebootTime & 0x000000FF );
        p_buffer[2] = (uint8_t)( (rebootTime & 0x0000FF00) >> 8 ); 
        p_buffer[3] = (uint8_t)( (rebootTime & 0x00FF0000) >> 16 );
        p_buffer[4] = (uint8_t)( (rebootTime & 0xFF000000) >> 24 );

        (*p_payloadLen) = FWMNG_PLEN_DEV_REBOOT_TIME_ANS + 1;  // +1 = CID length

#ifdef DEBUG_FWMNG
        print( "*FWMNG:DevRebootTimeAns" );
        print_newline();
        print( "*FWMNG:  RebootTime=0x" );
        print_hex( rebootTime, 8 );
        print_newline();
#endif
    }
    else
    {
        // MUST NOT answer
        (*p_payloadLen) = 0;
    }

    return FWMNG_STATUS_OK;
}

/*!
 * Process received command; DevRebootCountdownReq
 */
static FwMngStatus_t LoRaFwMngProcessDevRebootCountdownReq( uint8_t        *p_buffer, 
                                                            uint8_t        *p_payloadLen, 
                                                            uint8_t        bufferMaxSize,
                                                            uint32_t       *p_txDelayMs,
                                                            FwMng_RxCmd_t  *p_rcCmd )
{
    FwMng_DevRebootCountdownReq_t   *p_cmdDevRbtCntDwnReq;
    uint32_t                        rebootTime;
    uint32_t                        timeToRebootSec;
    FwMngStatus_t                   funcRes;

    // length check
    if( bufferMaxSize < (FWMNG_PLEN_DEV_REBOOT_COUNTDOWN_ANS + 1) )  // +1 = CID length
    {
        return FWMNG_STATUS_LENGTH_ERROR;
    }

    // init
    p_cmdDevRbtCntDwnReq = (FwMng_DevRebootCountdownReq_t *)&( p_rcCmd->reqCmdParam );

    // Cancel a currently programmed reboot
    if( FwMngTimerMng.isWaitTimeToReboot == true )
    {
        TimerStop( &( FwMngTimerMng.rebootTimerObj ) );
        FwMngTimerMng.isWaitTimeToReboot = false;
        
        (*FwMngEventCbFuncs.LoRaFwMngRebootCancelEventCb)();  // notify to upper
    }
    
    // process
    timeToRebootSec = p_cmdDevRbtCntDwnReq->countdown;
    if( timeToRebootSec == 0 )
    {
        // ask the application
        funcRes = (*FwMngEventCbFuncs.LoRaFwMngRebootRequestEventCb)( timeToRebootSec );
        if( funcRes == FWMNG_STATUS_OK )
        {
            // Reboot as soon as possible
            (*FwMngEventCbFuncs.LoRaFwMngRebootExecEventCb)();
        }
    }
    else
    {
        // prepare to reboot at specified time
        if( timeToRebootSec < 0x00FFFFFF )  // 24bit
        {
            // ask the application
            funcRes = (*FwMngEventCbFuncs.LoRaFwMngRebootRequestEventCb)( timeToRebootSec );
            
            // DevRebootTimeAns parameter
            if( funcRes == FWMNG_STATUS_OK )
            {
                rebootTime = timeToRebootSec;
            
                // start timer
                TimerSetValue( &( FwMngTimerMng.rebootTimerObj ), ( rebootTime * 1000 ) );
                TimerStart( &( FwMngTimerMng.rebootTimerObj ) );
                FwMngTimerMng.isWaitTimeToReboot = true;
            }
            else
            {
                // indicates the inability of the end-device to reboot at the requested time.
                rebootTime = 0;
            }
        }
        else
        {
            // DevRebootCountdownAns parameter
            // acknowledges the cancellation of a currently programmed reboot
            rebootTime = 0x00FFFFFF;  // 24bit
        }
    }

    // make DevRebootCountdownAns
    if( timeToRebootSec != 0 )
    {
        p_buffer[0] = FWMNG_CID_DEV_REBOOT_COUNTDOWN_ANS;
        p_buffer[1] = (uint8_t)(  rebootTime & 0x000000FF );
        p_buffer[2] = (uint8_t)( (rebootTime & 0x0000FF00) >> 8 ); 
        p_buffer[3] = (uint8_t)( (rebootTime & 0x00FF0000) >> 16 );

        (*p_payloadLen) = FWMNG_PLEN_DEV_REBOOT_COUNTDOWN_ANS + 1;  // +1 = CID length

#ifdef DEBUG_FWMNG
        print( "*FWMNG:DevRebootCountdownAns" );
        print_newline();
        print( "*FWMNG:  Countdown=0x" );
        print_hex( rebootTime, 6 );
        print_newline();
#endif
    }
    else
    {
        // MUST NOT answer
        (*p_payloadLen) = 0;
    }

    return FWMNG_STATUS_OK;
}

/*!
 * Process received command; DevUpgradeImageReq
 */
static FwMngStatus_t LoRaFwMngProcessDevUpgradeImageReq( uint8_t         *p_buffer, 
                                                         uint8_t         *p_payloadLen, 
                                                         uint8_t         bufferMaxSize, 
                                                         uint32_t        *p_txDelayMs,
                                                         FwMng_RxCmd_t   *p_rcCmd )
{
    uint8_t     upImageStatus;
    uint32_t    nextFirmwareVersion;

    // length check
    if( bufferMaxSize < (FWMNG_PLEN_DEV_UPGRADE_IMAGE_ANS + 1) )  // +1 = CID length
    {
        return FWMNG_STATUS_LENGTH_ERROR;
    }

    // process
    // Get F/W image information from upper
    upImageStatus = (*FwMngEventCbFuncs.LoRaFwMngUpImageStatusRequestCb)( &nextFirmwareVersion );
    if( (upImageStatus & 0xFC) != 0 )  // unknown status (upImageStatus is 2bit[bit1-0])
    {
        // set (overwrite) status to none because end-device must answer
        upImageStatus = FWMNG_UPIMGSTATUS_NONE;
    }

    // make DevVersionAns
    p_buffer[0] = FWMNG_CID_DEV_UPGRADE_IMAGE_ANS;
    p_buffer[1] = upImageStatus;
    if( upImageStatus == FWMNG_UPIMGSTATUS_AVAILABLE )
    {
        p_buffer[2] = (uint8_t)(  nextFirmwareVersion & 0x000000FF );
        p_buffer[3] = (uint8_t)( (nextFirmwareVersion & 0x0000FF00) >> 8 ); 
        p_buffer[4] = (uint8_t)( (nextFirmwareVersion & 0x00FF0000) >> 16 );
        p_buffer[5] = (uint8_t)( (nextFirmwareVersion & 0xFF000000) >> 24 );

        (*p_payloadLen) = FWMNG_PLEN_DEV_UPGRADE_IMAGE_ANS + 1;  // +1 = CID length
    }
    else
    {
        (*p_payloadLen) = 1 + 1;  // +1 = CID length
    }

#ifdef DEBUG_FWMNG
    print( "*FWMNG:DevUpgradeImageAns" );
    print_newline();
    print( "*FWMNG:  UpImageStatus=" );
    print_dec( upImageStatus, 3, '\0' );
    if( upImageStatus == FWMNG_UPIMGSTATUS_AVAILABLE )
    {
        print( ", nextFirmwareVersion=0x" );
        print_hex( nextFirmwareVersion, 8 );
    }
    print_newline();
#endif

    return FWMNG_STATUS_OK;
}

/*!
 * Process received command; DevDeleteImageReq
 */
static FwMngStatus_t LoRaFwMngProcessDevDeleteImageReq( uint8_t         *p_buffer, 
                                                        uint8_t         *p_payloadLen, 
                                                        uint8_t         bufferMaxSize,
                                                        uint32_t        *p_txDelayMs,
                                                        FwMng_RxCmd_t   *p_rcCmd )
{
    FwMng_DevDeleteImageReq_t   *p_cmdDevDelImgReq;
    uint8_t                     status;

    // length check
    if( bufferMaxSize < (FWMNG_PLEN_DEV_DELETE_IMAGE_ANS + 1) )  // +1 = CID length
    {
        return FWMNG_STATUS_LENGTH_ERROR;
    }

    // init
    p_cmdDevDelImgReq = (FwMng_DevDeleteImageReq_t *)&( p_rcCmd->reqCmdParam );

    // process
    status = (*FwMngEventCbFuncs.LoRaFwMngDeleteImageRequestEventCb)( p_cmdDevDelImgReq->fwToDeleteVersion );
    if( (status & 0xFC) != 0 )  // unknown status (Status is 2bit[bit1-0])
    {
        // set (overwrite) status to none because end-device must answer
        status = FWMNG_DELETEIMG_STATUS_NO_VALID_IMAGE;
    }

    // make DevDeleteImageAns
    p_buffer[0] = FWMNG_CID_DEV_DELETE_IMAGE_ANS;
    p_buffer[1] = status;
    (*p_payloadLen) = FWMNG_PLEN_DEV_DELETE_IMAGE_ANS + 1;  // +1 = CID length

#ifdef DEBUG_FWMNG
    print( "*FWMNG:DevDeleteImageAns" );
    print_newline();
    print( "*FWMNG:  Status=0x" );
    print_hex( status, 2 );
    print_newline();
#endif

    return FWMNG_STATUS_OK;
}

//--------------------------------------------------------------------------------------------------

/*!
 * Timer interrupt; reboot time has come
 */
static void LoRaFwMngOnRebootTimerEvent( void )
{
    FwMngTimerMng.event              = FWMNG_TIMER_EVENT_REBOOT_TIME;
    FwMngTimerMng.isWaitTimeToReboot = false;
}

#endif  // FUOTA_VERSION
