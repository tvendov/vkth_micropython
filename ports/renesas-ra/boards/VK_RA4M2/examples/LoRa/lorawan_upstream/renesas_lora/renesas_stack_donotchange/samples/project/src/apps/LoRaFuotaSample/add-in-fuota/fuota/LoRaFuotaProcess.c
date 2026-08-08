/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifdef DEBUG_FUOTA
#define DEBUG_FUOTAPROC
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "timer.h"
#include "LoRaMac.h"

#include "LoRaFuotaProcess.h"

#ifdef DEBUG_FUOTAPROC
#include  "app_fuota_at_proc.h"
#endif

/*** process commands ***/
#define FUOTA_FPORT_CLOCKSYNC           CLKSNC_FPORT
#define FUOTA_FPORT_FRAGSESSION         FRGMNT_FPORT
#define FUOTA_FPORT_MCCONTROL           RMTMC_FPORT
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define FUOTA_FPORT_FIRMMANAGE          FWMNG_FPORT
#define FUOTA_FPORT_MLPKGACCESS         MLPKG_FPORT
#endif

#define FUOTA_PACKAGEID_CLOCKSYNC       CLKSNC_PACKAGE_IDENTIFIER
#define FUOTA_PACKAGEID_FRAGSESSION     FRGMNT_PACKAGE_IDENTIFIER
#define FUOTA_PACKAGEID_MCCONTROL       RMTMC_PACKAGE_IDENTIFIER
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define FUOTA_PACKAGEID_FIRMMANAGE      FWMNG_PACKAGE_IDENTIFIER
#define FUOTA_PACKAGEID_MLPKGACCESS     MLPKG_PACKAGE_IDENTIFIER
#endif

#define FUOTA_UPLINK_NBRETRY            3       // num retransmission
#define FUOTA_UPLINK_RETRYINTERVAL_MS   4000    // retransmission interval (unit:msec)

typedef enum {
    FUOTA_PROCSTATE_NONE = 0,
    FUOTA_PROCSTATE_INITIALIZED,
    /*--*/
    FUOTA_PROCSTATE_IDLE,
    FUOTA_PROCSTATE_IND_OCCUR,
    FUOTA_PROCSTATE_WAITING_SENDCMD,
    FUOTA_PROCSTATE_SENDCMD,
    FUOTA_PROCSTATE_WAITING_RETRANSCMD,
    FUOTA_PROCSTATE_RETRANSCMD,
    FUOTA_PROCSTATE_WAITING_IND_RETRY,
    FUOTA_PROCSTATE_WAITING_APPTIMEREQ_RETRY,
    FUOTA_PROCSTATE_APPTIMEREQ_RETRY,
} FuotaProcState_t;

/*** event ***/
#define FUOTA_TIMER_EVENT_NONE          0x00
#define FUOTA_TIMER_EVENT_WAIT_SEND     0x01
#define FUOTA_TIMER_EVENT_POLLING       0x02
#define FUOTA_TIMER_EVENT_JITTER        0x04

typedef struct {
    FuotaProcState_t    state;
    uint8_t             fport;
    uint8_t             buffer[ FUOTA_UPLINK_BUFFER ];
    uint8_t             length;
    uint8_t             nbretry;
    uint8_t             isDisableRetrans;
    TimerEvent_t        fuotaWaitTimer;
    TimerEvent_t        fuotaPollingTimer;
    TimerEvent_t        fuotaJitterTimer;
    uint8_t             rmtMcNumActive;
    uint8_t             stateSuspendPolling;
    bool                isAppTimeReq;
    uint8_t             event;
} FuotaProcMng_t;

FuotaProcMng_t FuotaProcMng = { .state = FUOTA_PROCSTATE_NONE };

static FuotaStatus_t FuotaHandleMcpsIndication( McpsIndication_t *p_mcpsIndication );

static void FuotaProcessIndOccur( void );
static void FuotaProcessSendCommands( void );
static void FuotaProcessRcvdEventProc( void );

static TimerTime_t FuotaCommandUplink( uint8_t fPort, uint8_t *p_buffer, uint8_t length );
static void FuotaSendCompCommand( uint8_t fPort, bool isSuccess );

static void FuotaWaitSendSetTimer( uint32_t timeMs );
static void FuotaWaitSendStopTimer( void );
static void FuotaOnWaitSendTimerEvent( void );

// for polling uplink
static void FuotaPollingSetTimer( void );
static void FuotaPollingStopTimer( void );
static void FuotaOnPollingTimerEvent( void );

// jitter
static void FuotaJitterSetTimerMs( uint32_t baseTimeMs );
static void FuotaOnJitterTimerEvent( void );

static void FuotaProcessTimerEvent( void );

/*** Event from own and each package ***/
#define FUOTA_EVENTRCVD_NONE                0x00
#define FUOTA_EVENTRCVD_CLKSNC_APPTIMEREQ   0x01
#define FUOTA_EVENTRCVD_PROC_POLLING        0x02

#define FUOTA_EVENTRCVD_QUEUE_NUM           6

#define FUOTA_TIMER_RETRY_EVENT_MS          1000  // msec to retry event procedure

typedef struct {
    uint8_t     eventQ[ FUOTA_EVENTRCVD_QUEUE_NUM ];
    uint8_t     eventQPos;
    uint8_t     numEvent;
} FuotaRcvdEventMng_t;

FuotaRcvdEventMng_t FuotaRcvdEventMng;

static void FuotaRcvdEventSet( uint8_t event );
static uint8_t FuotaRcvdEventGet( void );
static uint8_t FuotaRcvdEventGetNumSet( void );

/* callback functions which are called from each package */

// ClockSync notifies the AppTimeReq transmit timing
static void FuotaClockSyncEvent_AppTimeReq( bool isForceResync );

// RemoteMulticast requests current AppTime
static uint32_t FuotaRemoteMulticastEvent_GetCurrentAppTime( void );

// RemoteMulticast notifies the start/end multicast session
static void FuotaRemoteMulticastEvent_McSessionSetupInd( DeviceClass_t sessionClass, 
                                                         uint8_t mcGroupId, 
                                                         uint32_t timeToStartSec,
                                                         uint32_t timeoutSec );
static void FuotaRemoteMulticastEvent_McSessionStartInd( DeviceClass_t sessionClass, 
                                                         uint8_t mcGroupId, 
                                                         uint32_t timeoutSec );
static void FuotaRemoteMulticastEvent_McSessionEndInd( DeviceClass_t sessionClass, 
                                                       uint8_t mcGroupId );

// Fragment notifies the start fragment session (request decriptor check)
static FuotaStatus_t FuotaFragmentEvent_SessionSetupInd( uint8_t fragIndex, 
                                                         uint32_t descriptor );

// Fragment indicates the data block
static void FuotaFragmentEvent_DataBlockIndication( uint8_t fragIndex, 
                                                    uint8_t *p_dataBlk, 
                                                    uint32_t dataBlkSize );

// Fragment notifies the end fragment session
static void FuotaFragmentEvent_SessionEndInd( uint8_t fragIndex );

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
// FirmwareManagement requests version information
static void FuotaFirmwareManagementEvent_GetVersion( uint32_t *p_fwVersion, uint32_t *p_hwVersion );

// FirmwareManagement requests curent time
static uint32_t FuotaFirmwareManagementEvent_GetCurrentTime( void );

// FirmwareManagement notifies the reboot request
static FuotaStatus_t FuotaFirmwareManagementEvent_RebootRequestInd( uint32_t rebootSec );
static void FuotaFirmwareManagementEvent_RebootCancelInd( void );
static void FuotaFirmwareManagementEvent_RebootExecInd( void );

// FirmwareManagement requests about the F/W image
static uint8_t FuotaFirmwareManagementEvent_UpImageStatusRequestInd( uint32_t *p_nextFirmwareVersion );
static uint8_t FuotaFirmwareManagementEvent_DeleteImageRequestInd( uint32_t fwToDelVersion );

// MultiPackageAccess requests command payload length
static FuotaStatus_t FuotaMultiPackageAccessEvent_PackageCmdPayloadLenReq( uint8_t packageId, 
                                                                           uint8_t cid, 
                                                                           uint8_t *p_cmdPayloadLen );
// MultiPackageAccess requests packages list
static void FuotaMultiPackageAccessEvent_PackagesListReq( uint8_t                         *p_nbPackages, 
                                                          MlPkg_DevPackageElement_t **pp_listPackages );
#endif

/*** Polling uplink ***/
#define FUOTA_POLLING_PAYLOAD       0x00  // 1Byte

#define FUOTA_POLLING_SUSPEND_NONE          0x00
#define FUOTA_POLLING_SUSPEND_APPTIMEREQ    0x01
#define FUOTA_POLLING_SUSPEND_MULTICAST     0x02
#define FUOTA_POLLING_SUSPEND_WAITSEND      0x04

/*** Event; notify to application ***/
FuotaEventCb_t FuotaEventCbFuncs = {0};

/*** IB ***/
typedef struct
{
    uint32_t    procPollingPeriodSec;
    uint8_t     procPollingFPort;
} Fuota_IB_params_t;

Fuota_IB_params_t  FuotaIBParams;

/*** package list ***/
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
typedef MlPkg_DevPackageElement_t   FuotaPackageList_t;
#else  // FUOTA V1.0.0
typedef struct {
    uint8_t     packageId;
    uint8_t     packageVersion;
    uint8_t     fport;
} FuotaPackageList_t;
#endif

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define FUOTA_NUM_SUPPORT_PACKAGES     5
#else  // FUOTA V1.0.0
#define FUOTA_NUM_SUPPORT_PACKAGES     3
#endif
const FuotaPackageList_t FuotaPackagesList[ FUOTA_NUM_SUPPORT_PACKAGES ] = 
{
    { CLKSNC_PACKAGE_IDENTIFIER, CLKSNC_PACKAGE_VERSION, CLKSNC_FPORT },
    { FRGMNT_PACKAGE_IDENTIFIER, FRGMNT_PACKAGE_VERSION, FRGMNT_FPORT },
    { RMTMC_PACKAGE_IDENTIFIER,  RMTMC_PACKAGE_VERSION,  RMTMC_FPORT  },
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    { FWMNG_PACKAGE_IDENTIFIER,  FWMNG_PACKAGE_VERSION,  FWMNG_FPORT  },
    { MLPKG_PACKAGE_IDENTIFIER,  MLPKG_PACKAGE_VERSION,  MLPKG_FPORT  },
#endif
};

/*!
 * FUOTA initialization
 */
FuotaStatus_t FuotaInit( FuotaEventCb_t *p_fuotaEventCb )
{
    LoRaClkSncEventCb_t     clkSyncCallbacks;
    LoRaRmtMcEventCb_t      rmtMcCallbacks;
    LoRaFrgmntEventCb_t     frgmntCallbacks;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    LoRaFwMngEventCb_t      fwMngCallbacks;
    LoRaMlPkgEventCb_t      mlPkgCallbacks;
#endif
    // check
    if( p_fuotaEventCb == NULL )
    {
        return FUOTA_STATUS_PARAMETER_INVALID;
    }
    if( ( p_fuotaEventCb->FuotaRmtMcSessionSetupIndication == NULL ) ||
        ( p_fuotaEventCb->FuotaRmtMcSessionStartIndication == NULL ) ||
        ( p_fuotaEventCb->FuotaRmtMcSessionEndIndication == NULL ) ||
        ( p_fuotaEventCb->FuotaFrgmntSessionSetupIndicaiton == NULL ) ||
        ( p_fuotaEventCb->FuotaFrgmntDataBlockIndication == NULL ) ||
        ( p_fuotaEventCb->FuotaFrgmntSessionEndIndication == NULL ) )
    {
        return FUOTA_STATUS_PARAMETER_INVALID;
    }
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    if( ( p_fuotaEventCb->FuotaFwMngVersionInfoRequest == NULL ) ||
        ( p_fuotaEventCb->FuotaFwMngRebootRequestIndication == NULL ) ||
        ( p_fuotaEventCb->FuotaFwMngRebootCanceledIndication == NULL ) ||
        ( p_fuotaEventCb->FuotaFwMngRebootExecIndication == NULL ) ||
        ( p_fuotaEventCb->FuotaFwMngUpImageStatusRequest == NULL ) ||
        ( p_fuotaEventCb->FuotaFwMngDeleteImageRequest == NULL ) )
    {
        return FUOTA_STATUS_PARAMETER_INVALID;
    }
#endif

    /*** FUOTA process management ***/
    if( FuotaProcMng.state != FUOTA_PROCSTATE_NONE )
    {
        TimerStop( &( FuotaProcMng.fuotaWaitTimer ) );
        TimerStop( &( FuotaProcMng.fuotaPollingTimer ) );
        TimerStop( &( FuotaProcMng.fuotaJitterTimer ) );
    }
    memset1( (uint8_t *)&FuotaProcMng, 0x00, sizeof(FuotaProcMng_t) );
    FuotaProcMng.nbretry = FUOTA_UPLINK_NBRETRY;  // reset(init) down-count
    TimerInit( &( FuotaProcMng.fuotaWaitTimer ), FuotaOnWaitSendTimerEvent );
    TimerInit( &( FuotaProcMng.fuotaPollingTimer ), FuotaOnPollingTimerEvent );
    TimerInit( &( FuotaProcMng.fuotaJitterTimer ), FuotaOnJitterTimerEvent );

    /*** Event from each package ***/
    memset1( (uint8_t *)&FuotaRcvdEventMng, 0x00, sizeof(FuotaRcvdEventMng_t) );

    /*** Event; notify to application ***/
    if( p_fuotaEventCb != &FuotaEventCbFuncs )
    {
        memcpy1( (uint8_t *)&FuotaEventCbFuncs, (uint8_t *)p_fuotaEventCb, sizeof(FuotaEventCb_t) );
    }

    /*** initialize each package ***/
    if( p_fuotaEventCb != &FuotaEventCbFuncs )  // means application requested initialization
    {
        // ClockSync
        clkSyncCallbacks.LoRaClkSncAppTimeReqCb = FuotaClockSyncEvent_AppTimeReq;
        LoRaClockSyncInit( &clkSyncCallbacks );
        
        // RemoteMulticast
        rmtMcCallbacks.LoRaRmtMcCurrentTimeSecReqCb    = FuotaRemoteMulticastEvent_GetCurrentAppTime;
        rmtMcCallbacks.LoRaRmtMcSessionSetupIndication = FuotaRemoteMulticastEvent_McSessionSetupInd;
        rmtMcCallbacks.LoRaRmtMcSessionStartIndication = FuotaRemoteMulticastEvent_McSessionStartInd;
        rmtMcCallbacks.LoRaRmtMcSessionEndIndication   = FuotaRemoteMulticastEvent_McSessionEndInd;
        LoRaRemoteMulticastInit( &rmtMcCallbacks );

        // Fragment
        frgmntCallbacks.LoRaFrgmntSessionSetupIndication = FuotaFragmentEvent_SessionSetupInd;
        frgmntCallbacks.LoRaFrgmntDataBlockIndication    = FuotaFragmentEvent_DataBlockIndication;
        frgmntCallbacks.LoRaFrgmntSessionEndIndication   = FuotaFragmentEvent_SessionEndInd;
        LoRaFragmentInit( &frgmntCallbacks );

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        // FirmwareManagement
        fwMngCallbacks.LoRaFwMngVersionReqCb              = FuotaFirmwareManagementEvent_GetVersion;
        fwMngCallbacks.LoRaFwMngCurrentTimeSecReqCb       = FuotaFirmwareManagementEvent_GetCurrentTime;
        fwMngCallbacks.LoRaFwMngRebootRequestEventCb      = FuotaFirmwareManagementEvent_RebootRequestInd;
        fwMngCallbacks.LoRaFwMngRebootCancelEventCb       = FuotaFirmwareManagementEvent_RebootCancelInd;
        fwMngCallbacks.LoRaFwMngRebootExecEventCb         = FuotaFirmwareManagementEvent_RebootExecInd;
        fwMngCallbacks.LoRaFwMngUpImageStatusRequestCb    = FuotaFirmwareManagementEvent_UpImageStatusRequestInd;
        fwMngCallbacks.LoRaFwMngDeleteImageRequestEventCb = FuotaFirmwareManagementEvent_DeleteImageRequestInd;
        LoRaFirmwareManagementInit( &fwMngCallbacks );

        //MultiPackageAccess
        mlPkgCallbacks.LoRaMlPkgPackageCmdPayloadLenReqCb = FuotaMultiPackageAccessEvent_PackageCmdPayloadLenReq;
        mlPkgCallbacks.LoRaMlPkgPackageListReqCb          = FuotaMultiPackageAccessEvent_PackagesListReq;
        LoRaMultiPackageAccessInit( &mlPkgCallbacks );
#endif
    }

    /* IB */
    memset1( (uint8_t *)&FuotaIBParams, 0x00, sizeof(Fuota_IB_params_t) );
    FuotaIBParams.procPollingPeriodSec = FUOTA_IB_INIT_PROC_POLLING_PERIOD_SEC;
    FuotaIBParams.procPollingFPort     = FUOTA_IB_INIT_PROC_POLLING_FPORT;

    FuotaProcMng.state = FUOTA_PROCSTATE_INITIALIZED;
    return FUOTA_STATUS_OK;
}

/*!
 * FUOTA start
 */
void FuotaStart( void )
{
    if( FuotaProcMng.state == FUOTA_PROCSTATE_INITIALIZED )
    {
        LoRaClockSyncStart();
        LoRaFragmentStart();
        LoRaRemoteMulticastStart();
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        LoRaFirmwareManagementStart();
        LoRaMultiPackageAccessStart();
#endif
        FuotaProcMng.state = FUOTA_PROCSTATE_IDLE;

        // start polling timer
        FuotaPollingSetTimer();
    }
}

/*!
 * FUOTA stop
 */
void FuotaStop( void )
{
    Fuota_IB_params_t   ibParams;

    if( FuotaProcMng.state != FUOTA_PROCSTATE_NONE )
    {
        LoRaClockSyncStop();
        LoRaFragmentStop();
        LoRaRemoteMulticastStop();
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        LoRaFirmwareManagementStop();
        LoRaMultiPackageAccessStop();
#endif

        memcpy1( (uint8_t *)&ibParams, (uint8_t *)&FuotaIBParams, sizeof(Fuota_IB_params_t) );
        FuotaInit( &FuotaEventCbFuncs );
        memcpy1( (uint8_t *)&FuotaIBParams, (uint8_t *)&ibParams, sizeof(Fuota_IB_params_t) );
    }
}

/*!
 * MCPS-Confirm event function for FUOTA
 */
void FuotaMcpsConfirm( McpsConfirm_t *p_mcpsConfirm )
{
    // Reserved; nothing to do now.
}

/*!
 * MCPS-Indication event function for FUOTA
 */
FuotaStatus_t FuotaMcpsIndication( McpsIndication_t *p_mcpsIndication )
{
    FuotaStatus_t       ret;
    McpsIndication_t    *p_splitMcpsInd;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    bool                bIsMultiPackBufferReqCmd;
    McpsIndication_t    splitMcpsInd;
#endif

    // check
    if( p_mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK )
    {
        return FUOTA_STATUS_ERROR;
    }

    if( FuotaProcMng.state <= FUOTA_PROCSTATE_INITIALIZED )
    {
        return FUOTA_STATUS_ERROR;  // not started
    }
    else
    {
        switch( FuotaProcMng.state )
        {
            case FUOTA_PROCSTATE_IND_OCCUR:
            case FUOTA_PROCSTATE_WAITING_SENDCMD:
            case FUOTA_PROCSTATE_SENDCMD:
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
                if( p_mcpsIndication->Port == FUOTA_FPORT_MLPKGACCESS )
                {
                    bIsMultiPackBufferReqCmd = LoRaMultiPackageAccessIsMultiPackBufferReqCommand( p_mcpsIndication );
                    if( bIsMultiPackBufferReqCmd == true )
                    {
                        FuotaProcMng.state                = FUOTA_PROCSTATE_IDLE;
                        FuotaProcMng.stateSuspendPolling &= ~(FUOTA_POLLING_SUSPEND_APPTIMEREQ); 
                        LoRaMultiPackageAccesResetState();
                        break;
                    }
                }
#endif
                // previous FUOTA command(s) are not processed yet.
                // previous FUOTA command(s) are not tried to send yet.
                return FUOTA_STATUS_BUSY;

            case FUOTA_PROCSTATE_WAITING_APPTIMEREQ_RETRY:
            case FUOTA_PROCSTATE_APPTIMEREQ_RETRY:
                // cancel AppTimeReq uplink
                FuotaSendCompCommand( FUOTA_FPORT_CLOCKSYNC, false );
                // no break // go to the following
            case FUOTA_PROCSTATE_WAITING_RETRANSCMD:
            case FUOTA_PROCSTATE_RETRANSCMD:
            case FUOTA_PROCSTATE_WAITING_IND_RETRY:
                // FUOTA command(s) are processed but response uplink is not sent yet.
                // --> cancel and flush FUOTA uplink
                FuotaProcMng.state                = FUOTA_PROCSTATE_IDLE;
                FuotaProcMng.stateSuspendPolling &= ~(FUOTA_POLLING_SUSPEND_APPTIMEREQ); 
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
                LoRaMultiPackageAccesResetState();
#endif
                break;

            //case FUOTA_PROCSTATE_IDLE:
            default:
                break;
        }
    }

    if( FuotaProcMng.state == FUOTA_PROCSTATE_IDLE )
    {
        FuotaWaitSendStopTimer();
        FuotaProcMng.nbretry          = FUOTA_UPLINK_NBRETRY;  // reset down-count
        FuotaProcMng.isAppTimeReq     = false;
        FuotaProcMng.isDisableRetrans = false;
    }
    else
    {
        return FUOTA_STATUS_ERROR;  // (fail-safe)
    }

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    p_splitMcpsInd = &splitMcpsInd;
    ret            = LoRaMultiPackageAccessSplitMcpsIndication( p_mcpsIndication, p_splitMcpsInd );
#else  // FUOTA V1.0.0
    p_splitMcpsInd = p_mcpsIndication;
    ret            = FUOTA_STATUS_OK;
#endif

    if( ret == FUOTA_STATUS_OK )
    {
        ret = FuotaHandleMcpsIndication( p_splitMcpsInd );
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        if( ret != FUOTA_STATUS_OK )
        {
            // send reply in AnsBuffer if AnsBuffer is not empty
            FuotaStatus_t   funcRet;
            uint8_t         remainedSize;
            funcRet = LoRaMultiPackageAccessGetRemainedAnsBufferSize( &remainedSize );
            if( ( funcRet == FUOTA_STATUS_OK ) && ( remainedSize < FUOTA_UPLINK_BUFFER ) )
            {
                FuotaProcMng.state = FUOTA_PROCSTATE_SENDCMD;
                ret = FUOTA_STATUS_OK;
            }
        }
#endif
    }

    return ret;
}

/*!
 * MCPS-Indication event function for FUOTA; handle McpsIndication to each package
 */
static FuotaStatus_t FuotaHandleMcpsIndication( McpsIndication_t *p_mcpsIndication )
{
    FuotaStatus_t       ret;

    // init
    ret = FUOTA_STATUS_ERROR;

    switch( p_mcpsIndication->Port )
    {
        case FUOTA_FPORT_CLOCKSYNC:
            ret = LoRaClockSyncMcpsIndication( p_mcpsIndication );
            break;

        case FUOTA_FPORT_FRAGSESSION:
            ret = LoRaFragmentMcpsIndication( p_mcpsIndication );
            break;

        case FUOTA_FPORT_MCCONTROL:
            ret = LoRaRemoteMulticastMcpsIndication( p_mcpsIndication );
            break;

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        case FUOTA_FPORT_FIRMMANAGE:
            ret = LoRaFirmwareManagementMcpsIndication( p_mcpsIndication );
            break;

        case FUOTA_FPORT_MLPKGACCESS:
            ret = LoRaMultiPackageAccessMcpsIndication( p_mcpsIndication );
            break;
#endif

        default:
            break;
    }

    if( ret == FUOTA_STATUS_OK )
    {
        FuotaProcMng.state = FUOTA_PROCSTATE_IND_OCCUR;
        FuotaProcMng.fport = p_mcpsIndication->Port;
    }

#ifdef DEBUG_FUOTA
    // Get Status
    AppAtFuotaDebugGetMcpsIndResult( (uint8_t)ret );
#endif

    return ret;
}


/*!
 * MLME-Confirm event function for FUOTA
 */
void FuotaMlmeConfirm( MlmeConfirm_t *p_mlmeConfirm )
{
    if( p_mlmeConfirm->MlmeRequest == MLME_DEVICE_TIME )
    {
        if( p_mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK )
        {
            // ClockSync - reset correct time for AppTime
            LoRaClockSyncResetCorrectTime();
        }
    }
}

/*!
 * MLME-Indication event function for FUOTA
 */
void FuotaMlmeIndication( MlmeIndication_t *p_mlmeIndication )
{
    // Reserved; nothing to do now.
}

/*!
 * IB Get Request
 */
FuotaStatus_t FuotaIbGetRequest( uint8_t ib, void *vpVal )
{
    FuotaStatus_t   ret;
    uint8_t         ibPkg;
    uint8_t         ibPkgId;

    if( FuotaProcMng.state == FUOTA_PROCSTATE_NONE )
    {
        return FUOTA_STATUS_ERROR;  // not initialized
    }

    if( vpVal == NULL )
    {
        return FUOTA_STATUS_PARAMETER_INVALID;
    }

    // init
    ret     = FUOTA_STATUS_PARAMETER_INVALID;
    ibPkg   = FUOTA_IBMASK_GETPKG( ib );
    ibPkgId = ib & (~ibPkg);

    switch( ibPkg )
    {
        case FUOTA_IB_CLKSNK:
            ret = LoRaClockSyncIbGetRequest( ibPkgId, vpVal );
            break;

        case FUOTA_IB_FLGMNT:
            ret = LoRaFragmentIbGetRequest( ibPkgId, vpVal );
            break;

        case FUOTA_IB_RMTMC:
            ret = LoRaRemoteMulticastIbGetRequest( ibPkgId, vpVal );
            break;

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        case FUOTA_IB_FWMNG:
            ret = LoRaFirmwareManagementIbGetRequest( ibPkgId, vpVal );
            break;

        case FUOTA_IB_MLPKG:
            ret = LoRaMultiPackageAccessIbGetRequest( ibPkgId, vpVal );
            break;
#endif
        case FUOTA_IB_PROC:
            switch( ib )
            {
                case FUOTA_IB_PROC_POLLING_PERIOD_SEC:
                    *( (uint32_t *)vpVal ) = FuotaIBParams.procPollingPeriodSec;
                    ret = FUOTA_STATUS_OK;
                    break;

                case FUOTA_IB_PROC_POLLING_FPORT:
                    *( (uint8_t *)vpVal ) = FuotaIBParams.procPollingFPort;
                    ret = FUOTA_STATUS_OK;
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return ret;
}

/*!
 * IB Set Request
 */
FuotaStatus_t FuotaIbSetRequest( uint8_t ib, void *vpVal )
{
    FuotaStatus_t   ret;
    uint8_t         ibPkg;
    uint8_t         ibPkgId;
    uint32_t        tmpVal32;
    uint8_t         tmpVal8;
    uint8_t         i;

    if( FuotaProcMng.state == FUOTA_PROCSTATE_NONE )
    {
        return FUOTA_STATUS_ERROR;  // not initialized
    }

    if( vpVal == NULL )
    {
        return FUOTA_STATUS_PARAMETER_INVALID;
    }

    // init
    ret     = FUOTA_STATUS_SERVICE_UNKNOWN;
    ibPkg   = FUOTA_IBMASK_GETPKG( ib );
    ibPkgId = ib & (~ibPkg);

    switch( ibPkg )
    {
        case FUOTA_IB_CLKSNK:
            ret = LoRaClockSyncIbSetRequest( ibPkgId, vpVal );
            break;

        case FUOTA_IB_FLGMNT:
            ret = LoRaFragmentIbSetRequest( ibPkgId, vpVal );
            break;

        case FUOTA_IB_RMTMC:
            ret = LoRaRemoteMulticastIbSetRequest( ibPkgId, vpVal );
            break;

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        case FUOTA_IB_FWMNG:
            ret = LoRaFirmwareManagementIbSetRequest( ibPkgId, vpVal );
            break;

        case FUOTA_IB_MLPKG:
            ret = LoRaMultiPackageAccessIbSetRequest( ibPkgId, vpVal );
            break;
#endif
        case FUOTA_IB_PROC:
            switch( ib )
            {
                case FUOTA_IB_PROC_POLLING_PERIOD_SEC:
                    tmpVal32 = *( (uint32_t *)vpVal );
                    if( ( tmpVal32 == 0 ) || 
                        (( tmpVal32 >= 10 ) && (tmpVal32 <= (uint32_t)0x00418930)) )  // 0x418930 * 1000(msec) = 0xFFFFE380
                    {
                        FuotaIBParams.procPollingPeriodSec = tmpVal32;
                        
                        // restart polling timer
                        FuotaPollingSetTimer();

                        ret = FUOTA_STATUS_OK;
                    }
                    else
                    {
                        ret = FUOTA_STATUS_PARAMETER_INVALID;
                    }
                    break;

                case FUOTA_IB_PROC_POLLING_FPORT:
                    ret = FUOTA_STATUS_PARAMETER_INVALID;  // init

                    tmpVal8 = *( (uint8_t *)vpVal );
                    if( (tmpVal8 >= 1) && (tmpVal8 <= 223) )
                    {
                        for( i = 0; i < FUOTA_NUM_SUPPORT_PACKAGES; i++ )
                        {
                            if( tmpVal8 == FuotaPackagesList[ i ].fport )
                            {
                                // cannot use FUOTA FPort for polling
                                break;  // exit from for(i) loop
                            }
                        }
                        if( i == FUOTA_NUM_SUPPORT_PACKAGES )
                        {
                            ret = FUOTA_STATUS_OK;  // check OK
                        }
                    }

                    if( ret == FUOTA_STATUS_OK )
                    {
                        FuotaIBParams.procPollingFPort = tmpVal8;
                    }
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return ret;
}

/*!
 * Process event/interrupt of FUOTA
 */
void FuotaProcess( void )
{
    bool    isMacBusy;

    if( FuotaProcMng.state <= FUOTA_PROCSTATE_INITIALIZED )
    {
        return;  // not started
    }

    // skip FUOTA process during MAC is busy.
    isMacBusy = LoRaMacIsBusy();
    if( isMacBusy == true )
    {
        return;
    }

    // Process timer irq
    FuotaProcessTimerEvent();

    // Received event from package
    FuotaProcessRcvdEventProc();

    // Command
    if( FuotaProcMng.state == FUOTA_PROCSTATE_IND_OCCUR )
    {
        FuotaProcessIndOccur();
    }
    if( ( FuotaProcMng.state == FUOTA_PROCSTATE_SENDCMD ) ||
        ( FuotaProcMng.state == FUOTA_PROCSTATE_RETRANSCMD ) )
    {
        FuotaProcessSendCommands();
    }
}


/*!
 * Low Power
 */
bool FuotaIsLowPowerAllowed( void )
{
    bool    bRet;
    bool    isMacBusy;

    isMacBusy = LoRaMacIsBusy();
    if( isMacBusy == true )
    {
        // allow low power during MAC is busy even if FUOTA is active.
        return true;
    }

    bRet = true;  // (init) allowed MCU to transit to low power mode

    if( FuotaRcvdEventGetNumSet() > 0 )
    {
        bRet = false;  // disallowed MCU to transit to low power mode
#ifdef DEBUG_FUOTAPROC
        print( "*FUOTA: disallow LowPower (event)" );
        print_newline();
#endif
    }
    else
    {
        // check FuotaProcess
        //   disallowed MCU to transit to low power mode if FuotaProcess is executable.
        if( ( FuotaProcMng.state == FUOTA_PROCSTATE_SENDCMD ) ||
            ( FuotaProcMng.state == FUOTA_PROCSTATE_RETRANSCMD ) ||
            ( FuotaProcMng.state == FUOTA_PROCSTATE_IND_OCCUR ) )
        {
            bRet = false;  // disallowed MCU to transit to low power mode
#ifdef DEBUG_FUOTAPROC
            print( "*FUOTA: disallow LowPower (executable state)" );
            print_newline();
#endif
        }

        //   disallowed MCU to transit to low power mode if wait timer for transmit has been expired
        if( bRet == true )
        {
            if( FuotaProcMng.event != FUOTA_TIMER_EVENT_NONE )
            {
                bRet = false;  // disallowed MCU to transit to low power mode
#ifdef DEBUG_FUOTAPROC
                print( "*FUOTA: disallow LowPower (timer has been expired)" );
                print_newline();
#endif
            }
        }

        // check each package
        //   each pakage returns false if event handling is required.
        //   i.e. disallowed MCU to transit to low power mode in this case.
        if( bRet == true )
        {
            // ClockSync
            bRet = LoRaClockSyncIsIdle();
#ifdef DEBUG_FUOTAPROC
            if( bRet == false )
            {
                print( "*FUOTA: disallow LowPower (ClockSync is not idle)" );
                print_newline();
            }
#endif
        }
        if( bRet == true )
        {
            // RemoteMulticast
            bRet = LoRaRemoteMulticastIsIdle();
#ifdef DEBUG_FUOTAPROC
            if( bRet == false )
            {
                print( "*FUOTA: disallow LowPower (RemoteMulticast is not idle)" );
                print_newline();
            }
#endif
        }
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        if( bRet == true )
        {
            // FirmwareManagement
            bRet = LoRaFirmwareManagementIsIdle();
  #ifdef DEBUG_FUOTAPROC
            if( bRet == false )
            {
                print( "*FUOTA: disallow LowPower (FirmwareManagement is not idle)" );
                print_newline();
            }
  #endif
        }
#endif
    }

    return bRet;
}

//--------------------------------------------------------------------------------------------------

/*!
 * FuotaProcess: Process received command
 */
static void FuotaProcessIndOccur( void )
{
    LoRaMacTxInfo_t     txInfo;
    uint32_t            txDelayMs;
    uint8_t             txMaxPayload;
    bool                isRetransEn;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    FuotaStatus_t       funcRetMlPkgActive;
#endif

    // init
    FuotaProcMng.length = 0;
    txDelayMs           = 0;

    // check/get max uplink length
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    funcRetMlPkgActive = LoRaMultiPackageAccessGetRemainedAnsBufferSize( &txMaxPayload );
    if( funcRetMlPkgActive != FUOTA_STATUS_OK )
#endif
    {
        memset1( (uint8_t *)&txInfo, 0x00, sizeof(LoRaMacTxInfo_t) );
        LoRaMacQueryTxPossible( 0, &txInfo );

        if( txInfo.CurrentPossiblePayloadSize <= FUOTA_UPLINK_BUFFER )
        {
            txMaxPayload = txInfo.CurrentPossiblePayloadSize;
        }
        else
        {
            txMaxPayload = FUOTA_UPLINK_BUFFER;
        }
    }

    switch( FuotaProcMng.fport )
    {
        case FUOTA_FPORT_CLOCKSYNC:
            LoRaClockSyncProcessCommand( FuotaProcMng.buffer, 
                                         &(FuotaProcMng.length), 
                                         txMaxPayload,
                                         &txDelayMs,
                                         &isRetransEn );
            break;

        case FUOTA_FPORT_FRAGSESSION:
            LoRaFragmentProcessCommand( FuotaProcMng.buffer, 
                                        &(FuotaProcMng.length), 
                                        txMaxPayload,
                                        &txDelayMs,
                                        &isRetransEn );
            break;

        case FUOTA_FPORT_MCCONTROL:
            LoRaRemoteMulticastProcessCommand( FuotaProcMng.buffer, 
                                               &(FuotaProcMng.length), 
                                               txMaxPayload,
                                               &txDelayMs,
                                               &isRetransEn );
            break;

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        case FUOTA_FPORT_FIRMMANAGE:
            LoRaFirmwareManagementProcessCommand( FuotaProcMng.buffer, 
                                                  &(FuotaProcMng.length), 
                                                  txMaxPayload,
                                                  &txDelayMs,
                                                  &isRetransEn );
            break;

        case FUOTA_FPORT_MLPKGACCESS:
            LoRaMultiPackageAccessProcessCommand( FuotaProcMng.buffer, 
                                                  &(FuotaProcMng.length), 
                                                  txMaxPayload,
                                                  &txDelayMs,
                                                  &isRetransEn );
            break;
#endif
        default:
            break;
    }

    if( FuotaProcMng.length > 0 )
    {
        FuotaProcMng.isDisableRetrans = false;
        if( isRetransEn != true )
        {
            FuotaProcMng.isDisableRetrans = true;
        }

        if( txDelayMs > 0 )
        {
            FuotaProcMng.state = FUOTA_PROCSTATE_WAITING_SENDCMD;
            FuotaWaitSendSetTimer( txDelayMs );
        }
        else
        {
            FuotaProcMng.state = FUOTA_PROCSTATE_SENDCMD;
        }
    }
    else
    {
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        if( funcRetMlPkgActive == FUOTA_STATUS_OK )
        {
            FuotaProcMng.state = FUOTA_PROCSTATE_SENDCMD;
        }
        else
#endif
        {
            FuotaProcMng.state = FUOTA_PROCSTATE_IDLE;
        }
    }
}

/*!
 * FuotaProcess: send/respond command
 */
static void FuotaProcessSendCommands( void )
{
    TimerTime_t         dutyCycleWaitTime;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    LoRaMacTxInfo_t     txInfo;
    FuotaStatus_t       funcRet;
    McpsIndication_t    *p_nextMcpsInd;
#endif

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    if( FuotaProcMng.state == FUOTA_PROCSTATE_SENDCMD )
    {
        // check/get max uplink length
        memset1( (uint8_t *)&txInfo, 0x00, sizeof(LoRaMacTxInfo_t) );
        LoRaMacQueryTxPossible( 0, &txInfo );
        
        if( txInfo.CurrentPossiblePayloadSize > FUOTA_UPLINK_BUFFER )
        {
            txInfo.CurrentPossiblePayloadSize = FUOTA_UPLINK_BUFFER;
        }
        
        // uplink
        funcRet = LoRaMultiPackageAccessCreateAnsUplink( FuotaProcMng.fport,    FuotaProcMng.buffer, FuotaProcMng.length, 
                                                         &(FuotaProcMng.fport), FuotaProcMng.buffer, &(FuotaProcMng.length),
                                                         txInfo.CurrentPossiblePayloadSize );
        if( funcRet == FUOTA_STATUS_PENDING )
        {
            FuotaProcMng.state = FUOTA_PROCSTATE_IDLE;
            funcRet = LoRaMultiPackageAccessGetNextMcpsInd( &p_nextMcpsInd );
            if( funcRet == FUOTA_STATUS_OK )
            {
                FuotaMcpsIndication( p_nextMcpsInd );
                FuotaWaitSendSetTimer( 1 );
            }
            return;
        }
    }
#endif

    dutyCycleWaitTime = FuotaCommandUplink( FuotaProcMng.fport, FuotaProcMng.buffer, FuotaProcMng.length );
    if( dutyCycleWaitTime == (TimerTime_t)0 )
    {
        // success
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        if( LoRaMultiPackageAccessIsRemainedBufferFrag() == true )
        {
            FuotaProcMng.state = FUOTA_PROCSTATE_WAITING_SENDCMD;
            FuotaWaitSendSetTimer( 3000 );
        }
        else
#endif
        {
            FuotaProcMng.state   = FUOTA_PROCSTATE_IDLE;
            FuotaProcMng.nbretry = FUOTA_UPLINK_NBRETRY;  // reset down-count
            if( FuotaProcMng.isAppTimeReq == true )
            {
                FuotaProcMng.stateSuspendPolling &= ~(FUOTA_POLLING_SUSPEND_APPTIMEREQ);
            }
            FuotaProcMng.isAppTimeReq     = false;  // clear
            FuotaProcMng.isDisableRetrans = false;

            FuotaSendCompCommand( FuotaProcMng.fport, true );
            FuotaPollingSetTimer();
        }
    }
    else
    {
        // Not success; retry
        if( FuotaProcMng.nbretry > 0 )
        {
            FuotaProcMng.nbretry--;  // down-count;
        }
        if( FuotaProcMng.nbretry > 0 )
        {
            if( FuotaProcMng.state == FUOTA_PROCSTATE_SENDCMD )
            {
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
                if( FuotaProcMng.fport == FUOTA_FPORT_MLPKGACCESS )
                {
                    FuotaProcMng.isDisableRetrans = false;
                }
#endif
                if( FuotaProcMng.isDisableRetrans == true )
                {
                    if( FuotaProcMng.isAppTimeReq == true )
                    {
                        FuotaProcMng.isAppTimeReq = false;
                        FuotaProcMng.state = FUOTA_PROCSTATE_WAITING_APPTIMEREQ_RETRY;
                    }
                    else
                    {
                        FuotaProcMng.state = FUOTA_PROCSTATE_WAITING_IND_RETRY;
                    }
                }
                else
                {
                    FuotaProcMng.state = FUOTA_PROCSTATE_WAITING_RETRANSCMD;
                }
            }
            else
            {
                FuotaProcMng.state = FUOTA_PROCSTATE_WAITING_RETRANSCMD;
            }

            // set retrans timer
            FuotaWaitSendSetTimer( dutyCycleWaitTime );

#ifdef DEBUG_FUOTAPROC
            print( "*FUOTA: cannot send command or polling. retry. (wait " );
            print_dec( (uint32_t)dutyCycleWaitTime, 10, '\0' );
            print( " ms)" );
            print_newline();
#endif
        }
        else
        {
            // failed
            FuotaSendCompCommand( FuotaProcMng.fport, false );

            FuotaProcMng.state                = FUOTA_PROCSTATE_IDLE;
            FuotaProcMng.nbretry              = FUOTA_UPLINK_NBRETRY;  // reset down-count
            FuotaProcMng.stateSuspendPolling &= ~(FUOTA_POLLING_SUSPEND_APPTIMEREQ); 
            FuotaProcMng.isAppTimeReq         = false;
            FuotaProcMng.isDisableRetrans     = false;

            FuotaPollingSetTimer();

#ifdef DEBUG_FUOTAPROC
            print( "*FUOTA: cannot send command or polling. TIMEOUT/NoRETRANS. (DutyCycleWaitTime=" );
            print_dec( (uint32_t)dutyCycleWaitTime, 10, '\0' );
            print( ")" );
            print_newline();
#endif
        }
    }
}

/*!
 * FuotaProcess: process event from each package
 */
static void FuotaProcessRcvdEventProc( void )
{
    LoRaMacTxInfo_t     txInfo;
    uint8_t             event;
    uint8_t             i, numEvent;
    bool                isPollingEvent;
    bool                isPollingNow;
    uint32_t            curAppTime;   // for check whether AppTime is synchronized

    LoRaRemoteMulticastProcessEvent();
    LoRaClockSyncProcessEvent();
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    LoRaFirmwareManagementProcessEvent();
#endif

    isPollingNow = false;  //init
    if( ( FuotaProcMng.state >= FUOTA_PROCSTATE_WAITING_SENDCMD ) &&
        ( FuotaProcMng.fport == FuotaIBParams.procPollingFPort ) )
    {
        isPollingNow = true;
    }

    if( ( FuotaProcMng.state == FUOTA_PROCSTATE_IDLE ) || 
        ( FuotaProcMng.state == FUOTA_PROCSTATE_APPTIMEREQ_RETRY ) ||
        ( isPollingNow == true ) )
    {
        if( FuotaRcvdEventGetNumSet() > 0 )
        {
            // check/get max uplink length
            memset1( (uint8_t *)&txInfo, 0x00, sizeof(LoRaMacTxInfo_t) );
            LoRaMacQueryTxPossible( 0, &txInfo );
    
            if( txInfo.CurrentPossiblePayloadSize > FUOTA_UPLINK_BUFFER )
            {
                txInfo.CurrentPossiblePayloadSize = FUOTA_UPLINK_BUFFER;
            }
    
            // init
            isPollingEvent = false;
            numEvent       = FuotaRcvdEventGetNumSet();
    
            for( i = 0; i < numEvent; i++ )
            {
                event = FuotaRcvdEventGet();
                switch( event )
                {
                    case FUOTA_EVENTRCVD_CLKSNC_APPTIMEREQ:
                        if( (FuotaProcMng.stateSuspendPolling & FUOTA_POLLING_SUSPEND_MULTICAST) == 0 )
                        {
                            LoRaClockSyncAppTimeReq( FuotaProcMng.buffer, 
                                                     &(FuotaProcMng.length), 
                                                     txInfo.CurrentPossiblePayloadSize );
                            
                            if( FuotaProcMng.length > 0 )
                            {
                                if( isPollingNow == true )
                                {
                                    FuotaWaitSendStopTimer();
                                }

                                FuotaProcMng.fport            = FUOTA_FPORT_CLOCKSYNC;
                                FuotaProcMng.state            = FUOTA_PROCSTATE_SENDCMD;
                                FuotaProcMng.isAppTimeReq     = true;
                                FuotaProcMng.isDisableRetrans = true;
                                isPollingEvent = false;  // polling uplink is not needed
                            }
                        }
                        break;
    
                    case FUOTA_EVENTRCVD_PROC_POLLING:
                        if( FuotaProcMng.stateSuspendPolling == FUOTA_POLLING_SUSPEND_NONE )
                        {
                            if( FuotaProcMng.state == FUOTA_PROCSTATE_IDLE )
                            {
                                curAppTime = LoRaClockSyncGetAppTimeSec();
                                if( curAppTime > 0 )
                                {
                                    isPollingEvent = true;
                                }
                                else
                                {
                                    // nothing to do
                                    // Discard ppolling uplink event until until AppTime is synchronized.
                                    // (AppTimeReq will be sent periodically until AppTime is synchronized.)
                                }
                            }
                        }
                        break;
    
                    default:
                        break;
                }
            }
    
            if( isPollingEvent == true )
            {
                FuotaProcMng.fport            = FuotaIBParams.procPollingFPort;
                FuotaProcMng.state            = FUOTA_PROCSTATE_SENDCMD;
                FuotaProcMng.nbretry          = FUOTA_UPLINK_NBRETRY;
                FuotaProcMng.isAppTimeReq     = false;
                FuotaProcMng.isDisableRetrans = false;
                FuotaProcMng.length           = 1;
                FuotaProcMng.buffer[0]        = FUOTA_POLLING_PAYLOAD;
#ifdef DEBUG_FUOTAPROC
                print( "*FUOTA: polling uplink (FPort=" );
                print_dec( FuotaIBParams.procPollingFPort, 3, '\0' );
                print( ")" );
                print_newline();
#endif
            }
        }
    }
}

/*!
 * FuotaProcess: MCPS-Request function for FUOTA
 */
static TimerTime_t FuotaCommandUplink( uint8_t fPort, uint8_t *p_buffer, uint8_t length )
{
    TimerTime_t         ret;
    MibRequestConfirm_t mibGet;
    McpsReq_t           mcpsReq;
    LoRaMacStatus_t     funcRet;

    // init
    ret = FUOTA_UPLINK_RETRYINTERVAL_MS;  // retransmission interval

    mibGet.Type = MIB_CHANNELS_DATARATE;
    LoRaMacMibGetRequestConfirm( &mibGet );

    mcpsReq.Type = MCPS_UNCONFIRMED; 
    mcpsReq.Req.Unconfirmed.fPort       = fPort;
    mcpsReq.Req.Unconfirmed.fBuffer     = p_buffer;
    mcpsReq.Req.Unconfirmed.fBufferSize = length;
    mcpsReq.Req.Unconfirmed.Datarate    = mibGet.Param.ChannelsDatarate;
    funcRet = LoRaMacMcpsRequest( &mcpsReq );
    if( funcRet == LORAMAC_STATUS_OK )
    {
        ret = (TimerTime_t)0;

#ifdef DEBUG_FUOTA
        AppAtFuotaDebugPrintUplink( fPort, p_buffer, length );
#endif
    }
    else if( funcRet == LORAMAC_STATUS_DUTYCYCLE_RESTRICTED )
    {
        ret = mcpsReq.ReqReturn.DutyCycleWaitTime;
    }

    return ret;
}

/*!
 * FuotaProcess: command has been sent. notify it to the packages
 */
static void FuotaSendCompCommand( uint8_t fPort, bool isSuccess )
{
    switch( fPort )
    {
        case FUOTA_FPORT_CLOCKSYNC:
            LoRaClockSyncSendCompCommand( isSuccess );
            break;

        case FUOTA_FPORT_FRAGSESSION:
            LoRaFragmentSendCompCommand( isSuccess );
            break;

        case FUOTA_FPORT_MCCONTROL:
            LoRaRemoteMulticastSendCompCommand( isSuccess );
            break;

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        case FUOTA_FPORT_FIRMMANAGE:
            LoRaFirmwareManagementSendCompCommand( isSuccess );
            break;

        case FUOTA_FPORT_MLPKGACCESS:
            LoRaMultiPackageAccessSendCompCommand( isSuccess );
            break;
#endif
        default:
            break;
    }
}

//--------------------------------------------------------------------------------------------------

/*!
 * set received event
 */
static void FuotaRcvdEventSet( uint8_t event )
{
    uint8_t queuePos;
    
    CRITICAL_SECTION_BEGIN();
    if( FuotaRcvdEventMng.numEvent < FUOTA_EVENTRCVD_QUEUE_NUM )
    {
        queuePos = ( FuotaRcvdEventMng.eventQPos + FuotaRcvdEventMng.numEvent ) % FUOTA_EVENTRCVD_QUEUE_NUM;
        FuotaRcvdEventMng.eventQ[ queuePos ] = event;
        FuotaRcvdEventMng.numEvent++;
    }
    else
    {
        // queue is full. event cannot be set.
    }
    CRITICAL_SECTION_END();
}

/*!
 * get received event
 */
static uint8_t FuotaRcvdEventGet( void )
{
    uint8_t     res;

    // init
    res = FUOTA_EVENTRCVD_NONE;

    CRITICAL_SECTION_BEGIN();
    if( FuotaRcvdEventMng.numEvent > 0 )
    {
        res = FuotaRcvdEventMng.eventQ[ FuotaRcvdEventMng.eventQPos ];

        FuotaRcvdEventMng.eventQ[ FuotaRcvdEventMng.eventQPos ] = FUOTA_EVENTRCVD_NONE;
        FuotaRcvdEventMng.eventQPos = (FuotaRcvdEventMng.eventQPos + 1 ) % FUOTA_EVENTRCVD_QUEUE_NUM;
        FuotaRcvdEventMng.numEvent--;
    }
    CRITICAL_SECTION_END();

    return res;
}

/*!
 * check received event
 */
static uint8_t FuotaRcvdEventGetNumSet( void )
{
    uint8_t retNumEvt;

    CRITICAL_SECTION_BEGIN();
    retNumEvt = FuotaRcvdEventMng.numEvent;
    CRITICAL_SECTION_END();

    return retNumEvt;
}

//--------------------------------------------------------------------------------------------------

/*!
 * Set timer for retransmission
 */
static void FuotaWaitSendSetTimer( uint32_t timeMs )
{
    TimerStop( &( FuotaProcMng.fuotaWaitTimer ) );

    FuotaProcMng.stateSuspendPolling |= FUOTA_POLLING_SUSPEND_WAITSEND;

    TimerSetValue( &( FuotaProcMng.fuotaWaitTimer ), timeMs );
    TimerStart( &( FuotaProcMng.fuotaWaitTimer ) );
}

/*!
 * Stop timer for retransmission
 */
static void FuotaWaitSendStopTimer( void )
{
    TimerStop( &( FuotaProcMng.fuotaWaitTimer ) );
    FuotaProcMng.stateSuspendPolling &= ~(FUOTA_POLLING_SUSPEND_WAITSEND);
}

/*!
 * Timer interrupt for retransmission
 */
static void FuotaOnWaitSendTimerEvent( void )
{
    TimerStop( &( FuotaProcMng.fuotaWaitTimer ) );
    FuotaProcMng.event |= FUOTA_TIMER_EVENT_WAIT_SEND;
}

/*!
 * Set timer for polling uplink
 */
static void FuotaPollingSetTimer( void )
{
    uint32_t    pollingTimerMsec;

    if( FuotaProcMng.state >= FUOTA_PROCSTATE_IDLE )
    {
        // (re)start polling timer
        TimerStop( &(FuotaProcMng.fuotaPollingTimer) );
        if( FuotaIBParams.procPollingPeriodSec > 0 )
        {
            pollingTimerMsec = FuotaIBParams.procPollingPeriodSec * 1000;
            TimerSetValue( &(FuotaProcMng.fuotaPollingTimer), pollingTimerMsec );
            TimerStart( &(FuotaProcMng.fuotaPollingTimer) );
        }
    }
}

/*!
 * Stop timer for polling uplink
 */
static void FuotaPollingStopTimer( void )
{
    TimerStop( &(FuotaProcMng.fuotaPollingTimer) );
}

/*!
 * Timer interrupt for polling update
 */
static void FuotaOnPollingTimerEvent( void )
{
    TimerStop( &(FuotaProcMng.fuotaPollingTimer) );
    FuotaProcMng.event |= FUOTA_TIMER_EVENT_POLLING;
}

/*!
 * Set timer for jitter (AppTimeReq)
 */
static void FuotaJitterSetTimerMs( uint32_t baseTimeMs )
{
    uint32_t    jtrTimeMs;

    if( baseTimeMs > 0x028F5C28 )   // 0x028F5C28 * 100 = 0xFFFFFFA0 < uint32_t
    {
        jtrTimeMs  = baseTimeMs / 100;
        jtrTimeMs *= randr( 1, 100 );
    }
    else
    {
        jtrTimeMs  = baseTimeMs * randr( 1, 100 );
        jtrTimeMs /= 100;
    }

    TimerSetValue( &(FuotaProcMng.fuotaJitterTimer), jtrTimeMs );
    TimerStart( &(FuotaProcMng.fuotaJitterTimer) );
}

/*!
 * Timer interrupt for jitter (AppTimeReq)
 */
static void FuotaOnJitterTimerEvent( void )
{
    TimerStop( &(FuotaProcMng.fuotaJitterTimer) );
    FuotaProcMng.event |= FUOTA_TIMER_EVENT_JITTER;
}

static void FuotaProcessTimerEvent( void )
{
    uint8_t     procEvent;

    CRITICAL_SECTION_BEGIN();
    procEvent          = FuotaProcMng.event;
    FuotaProcMng.event = FUOTA_TIMER_EVENT_NONE;
    CRITICAL_SECTION_END();

    if( procEvent != FUOTA_TIMER_EVENT_NONE )
    {
        if( (procEvent & FUOTA_TIMER_EVENT_WAIT_SEND) == FUOTA_TIMER_EVENT_WAIT_SEND )
        {
            switch( FuotaProcMng.state )
            {
                case FUOTA_PROCSTATE_WAITING_SENDCMD:
                    FuotaProcMng.state = FUOTA_PROCSTATE_SENDCMD;
                    break;

                case FUOTA_PROCSTATE_WAITING_RETRANSCMD:
                    FuotaProcMng.state = FUOTA_PROCSTATE_RETRANSCMD;
                    break;

                case FUOTA_PROCSTATE_WAITING_IND_RETRY:
                    FuotaProcMng.state = FUOTA_PROCSTATE_IND_OCCUR;
                    break;

                case FUOTA_PROCSTATE_WAITING_APPTIMEREQ_RETRY:
                    FuotaProcMng.state = FUOTA_PROCSTATE_APPTIMEREQ_RETRY;
                    FuotaRcvdEventSet( FUOTA_EVENTRCVD_CLKSNC_APPTIMEREQ );
                    break;

                default:
                    break;
            }

            FuotaProcMng.stateSuspendPolling &= ~(FUOTA_POLLING_SUSPEND_WAITSEND);
        }

        if( (procEvent & FUOTA_TIMER_EVENT_POLLING) == FUOTA_TIMER_EVENT_POLLING )
        {
            if( FuotaProcMng.stateSuspendPolling == FUOTA_POLLING_SUSPEND_NONE )
            {
                FuotaRcvdEventSet( FUOTA_EVENTRCVD_PROC_POLLING );
            }
        }

        if( (procEvent & FUOTA_TIMER_EVENT_JITTER) == FUOTA_TIMER_EVENT_JITTER )
        {
            FuotaClockSyncEvent_AppTimeReq( false );
        }
    }
}

//--------------------------------------------------------------------------------------------------

/*!
 * Event from ClockSync; AppTimeReq transmit timing
 */
static void FuotaClockSyncEvent_AppTimeReq( bool isForceResync )
{
    FuotaProcMng.stateSuspendPolling |= FUOTA_POLLING_SUSPEND_APPTIMEREQ;
    FuotaRcvdEventSet( FUOTA_EVENTRCVD_CLKSNC_APPTIMEREQ );
}

/*!
 * Event from RemoteMulticast; Request current AppTime
 */
static uint32_t FuotaRemoteMulticastEvent_GetCurrentAppTime( void )
{
    uint32_t    currentTimeSec;

    currentTimeSec = LoRaClockSyncGetAppTimeSec();

    return currentTimeSec;
}

/*!
 * Event from RemoteMulticast; Notify the pre-start of multicast session
 */
static void FuotaRemoteMulticastEvent_McSessionSetupInd( DeviceClass_t sessionClass, 
                                                         uint8_t mcGroupId, 
                                                         uint32_t timeToStartSec,
                                                         uint32_t timeoutSec )
{

    (*FuotaEventCbFuncs.FuotaRmtMcSessionSetupIndication)( sessionClass, 
                                                           mcGroupId, 
                                                           timeToStartSec,
                                                           timeoutSec );
}

/*!
 * Event from RemoteMulticast; Notify the start of multicast session
 */
static void FuotaRemoteMulticastEvent_McSessionStartInd( DeviceClass_t sessionClass, 
                                                         uint8_t mcGroupId, 
                                                         uint32_t timeoutSec )
{
    // stop polling uplink
    FuotaPollingStopTimer();
    FuotaProcMng.stateSuspendPolling |= FUOTA_POLLING_SUSPEND_MULTICAST;

    FuotaProcMng.rmtMcNumActive++;  // count active multicast session

    // notify to upper
    (*FuotaEventCbFuncs.FuotaRmtMcSessionStartIndication)( sessionClass, mcGroupId, timeoutSec );

#ifdef DEBUG_FUOTAPROC
    print( "*FUOTA[multicast]: start session. (" );
    switch( sessionClass )
    {
        case CLASS_B:
            print( "ClassB: " );
            break;
        case CLASS_C:
            print( "ClassC: " );
            break;
        default:
            print( "ClassA???: " );
            break;
    }
    print( "GroupId=" );
    print_dec( mcGroupId, 3, '\0' );
    print( ", timeout=0x" );
    print_hex( timeoutSec, 8 );
    print( ")" );
    print_newline();
#endif
}

/*!
 * Event from RemoteMulticast; Notify the end of multicast session
 */
static void FuotaRemoteMulticastEvent_McSessionEndInd( DeviceClass_t sessionClass, 
                                                       uint8_t mcGroupId )
{
    uint32_t    appTimeReqPeriodMsec;

    FuotaProcMng.rmtMcNumActive--;
    if( FuotaProcMng.rmtMcNumActive == 0 )
    {
        // AppTimeReq
        LoRaClockSyncIbGetRequest( CLKSNC_IB_TIMEREQ_PERIOD_SEC, (void *)&appTimeReqPeriodMsec );
        appTimeReqPeriodMsec *= 1000;
        FuotaJitterSetTimerMs( appTimeReqPeriodMsec );

        // restart polling uplink
        FuotaProcMng.stateSuspendPolling &= ~(FUOTA_POLLING_SUSPEND_MULTICAST);
        FuotaPollingSetTimer();
    }

    // notify to upper
    (*FuotaEventCbFuncs.FuotaRmtMcSessionEndIndication)( sessionClass, mcGroupId );

#ifdef DEBUG_FUOTAPROC
    print( "*FUOTA[multicast]: end session. (" );
    switch( sessionClass )
    {
        case CLASS_B:
            print( "ClassB: " );
            break;
        case CLASS_C:
            print( "ClassC: " );
            break;
        default:
            print( "ClassA???: " );
            break;
    }
    print( "GroupId=" );
    print_dec( mcGroupId, 3, '\0' );
    print( ")" );
    print_newline();
#endif
}

/*!
 * Event from Frament; start fragment session (request decriptor check)
 */
static FuotaStatus_t FuotaFragmentEvent_SessionSetupInd( uint8_t fragIndex, 
                                                         uint32_t descriptor )
{
    FuotaStatus_t   cbFuncRet;

    cbFuncRet = (*FuotaEventCbFuncs.FuotaFrgmntSessionSetupIndicaiton)( fragIndex, descriptor );

#ifdef DEBUG_FUOTAPROC
    print( "*FUOTA[fragment]: pre-start session. (fragIndex=" );
    print_dec( fragIndex, 3, '\0' );
    print( ")" );
    print_newline();
    print( "*FUOTA[fragment]:   Descriptor=0x" );
    print_hex( descriptor, 8 );
    if( cbFuncRet == FUOTA_STATUS_OK )
    {
        print( " => OK." );
        print_newline();
    }
    else
    {
        print( " => NG." );
        print_newline();
    }
#endif

    return cbFuncRet;
}

/*!
 * Event from Frament; indicates the data block
 */
static void FuotaFragmentEvent_DataBlockIndication( uint8_t fragIndex, 
                                                    uint8_t *p_dataBlk, 
                                                    uint32_t dataBlkSize )
{
    (*FuotaEventCbFuncs.FuotaFrgmntDataBlockIndication)( fragIndex, p_dataBlk, dataBlkSize );

#ifdef DEBUG_FUOTAPROC
    print( "*FUOTA[fragment]: Data block (fragIndex=" );
    print_dec( fragIndex, 3, '\0' );
    print( ", size=0x" );
    print_hex( dataBlkSize, 8 );
    print( ")" );
    for( uint32_t i = 0; i < dataBlkSize; i++ )
    {
        if( ( i % 16 ) == 0 )
        {
            print_newline();
            print( "*FUOTA[fragment]:   " );
        }
        print_hex( (*p_dataBlk++), 2 );
    }
    print_newline();
#endif
}

/*!
 * Event from Frament; end fragment session
 */
static void FuotaFragmentEvent_SessionEndInd( uint8_t fragIndex )
{
    (*FuotaEventCbFuncs.FuotaFrgmntSessionEndIndication)( fragIndex );

#ifdef DEBUG_FUOTAPROC
    print( "*FUOTA[fragment]: delete session. (fragIndex=" );
    print_dec( fragIndex, 3, '\0' );
    print( ")" );
    print_newline();
#endif
}

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*!
 * Event from FirmwareManagement; requests version information
 */
static void FuotaFirmwareManagementEvent_GetVersion( uint32_t *p_fwVersion, uint32_t *p_hwVersion )
{
    (*FuotaEventCbFuncs.FuotaFwMngVersionInfoRequest)( p_fwVersion, p_hwVersion );
}

/*!
 * Event from FirmwareManagement; requests curent time
 */
static uint32_t FuotaFirmwareManagementEvent_GetCurrentTime( void )
{
    uint32_t    currentTimeSec;

    currentTimeSec = LoRaClockSyncGetAppTimeSec();

    return currentTimeSec;
}

/*!
 * Event from FirmwareManagement; receive the reboot request from AS
 */
static FuotaStatus_t FuotaFirmwareManagementEvent_RebootRequestInd( uint32_t rebootSec )
{
    FuotaStatus_t   res;

    // ask to upper
    res = (*FuotaEventCbFuncs.FuotaFwMngRebootRequestIndication)( rebootSec );

#ifdef DEBUG_FUOTAPROC
    print( "*FUOTA[firmMng]: get the reboot request. (" );
    print_dec( rebootSec, 10, '\0' );
    print( " seconds later)" );
    print_newline();
    if( res == FUOTA_STATUS_OK )
    {
        print( "*FUOTA[firmMng]:   -> Accepted by the application" );
        print_newline();
    }
    else
    {
        print( "*FUOTA[firmMng]:   -> Rejected by the application" );
        print_newline();
    }
#endif

    return res;
}

/*!
 * Event from FirmwareManagement; cancel the reboot request
 */
static void FuotaFirmwareManagementEvent_RebootCancelInd( void )
{
    // notify to upper
    FuotaEventCbFuncs.FuotaFwMngRebootCanceledIndication();

#ifdef DEBUG_FUOTAPROC
    print( "*FUOTA[firmMng]: previous reboot request is canceled." );
    print_newline();
#endif
}

/*!
 * Event from FirmwareManagement; reboot time has come (or immediate)
 */
static void FuotaFirmwareManagementEvent_RebootExecInd( void )
{
#ifdef DEBUG_FUOTAPROC
    print( "*FUOTA[firmMng]: reboot time has come." );
    print_newline();
#endif

    // request to upper
    FuotaEventCbFuncs.FuotaFwMngRebootExecIndication();
}

/*!
 * Event from FirmwareManagement; request the version of F/W image
 */
static uint8_t FuotaFirmwareManagementEvent_UpImageStatusRequestInd( uint32_t *p_nextFirmwareVersion )
{
    uint8_t status;

    // ask to upper
    status = FuotaEventCbFuncs.FuotaFwMngUpImageStatusRequest( p_nextFirmwareVersion );

    return status;
}

/*!
 * Event from FirmwareManagement; request to delete the F/W image
 */
static uint8_t FuotaFirmwareManagementEvent_DeleteImageRequestInd( uint32_t fwToDelVersion )
{
    uint8_t status;

    // request to upper
    status = FuotaEventCbFuncs.FuotaFwMngDeleteImageRequest( fwToDelVersion );

    return status;
}

// Event from MultiPackageAccess: requests command payload length
static FuotaStatus_t FuotaMultiPackageAccessEvent_PackageCmdPayloadLenReq( uint8_t packageId, 
                                                                           uint8_t cid, 
                                                                           uint8_t *p_cmdPayloadLen )
{
    FuotaStatus_t   res;

    switch( packageId )
    {
        case CLKSNC_PACKAGE_IDENTIFIER:
            res = LoRaClockSyncGetRcvdCmdPayloadLen( cid, p_cmdPayloadLen );
            break;

        case RMTMC_PACKAGE_IDENTIFIER:
            res = LoRaRemoteMulticastGetRcvdCmdPayloadLen( cid, p_cmdPayloadLen );
            break;

        case FRGMNT_PACKAGE_IDENTIFIER:
            res = LoRaFragmentGetRcvdCmdPayloadLen( cid, p_cmdPayloadLen );
            break;

        case FWMNG_PACKAGE_IDENTIFIER:
            res = LoRaFirmwareManagementGetRcvdCmdPayloadLen( cid, p_cmdPayloadLen );
            break;

        default:
            res = FUOTA_STATUS_COMMAND_ERROR;
            break;
    }

    return res;
}

// Event from MultiPackageAccess requests packages list
static void FuotaMultiPackageAccessEvent_PackagesListReq( uint8_t                         *p_nbPackages, 
                                                          MlPkg_DevPackageElement_t **pp_listPackages )
{
    (*p_nbPackages)    = FUOTA_NUM_SUPPORT_PACKAGES;
    (*pp_listPackages) = (MlPkg_DevPackageElement_t *)FuotaPackagesList;
}
#endif

