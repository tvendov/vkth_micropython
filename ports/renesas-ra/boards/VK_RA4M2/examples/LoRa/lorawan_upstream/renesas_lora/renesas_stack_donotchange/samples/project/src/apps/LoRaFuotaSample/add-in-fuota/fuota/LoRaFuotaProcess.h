/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __LORAFUOTAPROCESS_H__
#define __LORAFUOTAPROCESS_H__

#include "LoRaFuotaConfig.h"

#include "LoRaAppClockSyncProcess.h"
#include "LoRaFragmentProcess.h"
#include "LoRaRemoteMulticastProcess.h"
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#include "LoRaFirmwareManagementProcess.h"
#include "LoRaMultiPackageAccessProcess.h"
#endif

#include "LoRaFuotaStatus.h"


/*** #define ***/
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
//NOTE: FUOTA_UPLINK_BUFFER must be 128 if you use MultiPackageAccess
#endif
#define FUOTA_UPLINK_BUFFER             128


/*** Event; notify to applicaiton ***/
typedef struct {
    void (*FuotaRmtMcSessionSetupIndication)( DeviceClass_t sessionClass, 
                                              uint8_t mcGroupId, 
                                              uint32_t timeToStartSec, 
                                              uint32_t timeoutSec );
    void (*FuotaRmtMcSessionStartIndication)( DeviceClass_t sessionClass, 
                                              uint8_t mcGroupId, 
                                              uint32_t timeoutSec );
    void (*FuotaRmtMcSessionEndIndication)( DeviceClass_t sessionClass, uint8_t mcGroupId );
    FuotaStatus_t (*FuotaFrgmntSessionSetupIndicaiton)( uint8_t fragIndex, uint32_t descriptor );
    void (*FuotaFrgmntDataBlockIndication)( uint8_t fragIndex, uint8_t *p_dataBlk, uint32_t dataBlkSize );
    void (*FuotaFrgmntSessionEndIndication)( uint8_t fragIndex );
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    void (*FuotaFwMngVersionInfoRequest)( uint32_t *p_fwVersion, uint32_t *p_hwVersion );
    FuotaStatus_t (*FuotaFwMngRebootRequestIndication)( uint32_t rebootSec );
    void (*FuotaFwMngRebootCanceledIndication)( void );
    void (*FuotaFwMngRebootExecIndication)( void );
    uint8_t (*FuotaFwMngUpImageStatusRequest)( uint32_t *p_nextFirmwareVersion );
    uint8_t (*FuotaFwMngDeleteImageRequest)( uint32_t fwToDelVersion );
#endif
} FuotaEventCb_t;

/*** IB ***/
// IB (ClockSync) - index
#define FUOTA_IB_CLKSNK     (0x10)
    // period of AppTimeReq transmission
    #define FUOTA_IB_CLKSNC_TIMEREQ_PERIOD_SEC          ( FUOTA_IB_CLKSNK | CLKSNC_IB_TIMEREQ_PERIOD_SEC )
    // [ReadOnly] requested periodicity by server
    #define FUOTA_IB_CLKSNC_TIMEREQ_REQ_PERIODICITY     ( FUOTA_IB_CLKSNK | CLKSNC_IB_TIMEREQ_REQ_PERIODICITY )
    // acceptable periodicity (min)
    #define FUOTA_IB_CLKSNC_TIMEREQ_MIN_PERIODICITY     ( FUOTA_IB_CLKSNK | CLKSNC_IB_TIMEREQ_MIN_PERIODICITY )
    // acceptable periodicity (max)
    #define FUOTA_IB_CLKSNC_TIMEREQ_MAX_PERIODICITY     ( FUOTA_IB_CLKSNK | CLKSNC_IB_TIMEREQ_MAX_PERIODICITY )
    // AnsRequired parameter of AppTimeReq command
    #define FUOTA_IB_CLKSNC_TIMEANS_REQUIRED            ( FUOTA_IB_CLKSNK | CLKSNC_IB_TIMEANS_REQUIRED )
    // period of force-AppTimeReq transmission
    #define FUOTA_IB_CLKSNC_FORCESYNC_PERIOD_SEC        ( FUOTA_IB_CLKSNK | CLKSNC_IB_FORCESYNC_PERIOD_SEC )

// IB (RemoteMulticast) - index
#define FUOTA_IB_RMTMC      (0x20)
    // GenAppKey
    #define FUOTA_IB_RMTMC_GENAPPKEY    ( FUOTA_IB_RMTMC | RMTMC_IB_GENAPPKEY )

// IB (Fragment) - index
#define FUOTA_IB_FLGMNT     (0x30)

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
// IB (FirmwareManagement) - index
#define FUOTA_IB_FWMNG      (0x40)

// IB (MultiPackageAccess) - index
#define FUOTA_IB_MLPKG      (0x50)
#endif

// IB (FUOTA)
#define FUOTA_IB_PROC       (0xF0)
    /** index **/
    // period of polling uplink in seconds
    #define FUOTA_IB_PROC_POLLING_PERIOD_SEC    ( FUOTA_IB_PROC | 0x00 )
    // FPort value of polling uplink
    #define FUOTA_IB_PROC_POLLING_FPORT         ( FUOTA_IB_PROC | 0x01 )
    /** default value **/
    #define FUOTA_IB_INIT_PROC_POLLING_PERIOD_SEC   0
    #define FUOTA_IB_INIT_PROC_POLLING_FPORT        223
    
// IB mask
#define FUOTA_IBMASK_GETPKG( ib )           ( (ib) & 0xF0 )

/*** LowPower / wakeup trigger flag ***/
#define  FUOTA_WAKEUP_TRIGGER_FUOTAEVENT    (0x01000000UL)

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*** F/W Image Status (FirmwareManagement) ***/
#define FUOTA_FWIMG_STATUS_NONE             FWMNG_UPIMGSTATUS_NONE           // no F/W image
#define FUOTA_FWIMG_STATUS_INVALID          FWMNG_UPIMGSTATUS_INVALID        // wrong F/W image
#define FUOTA_FWIMG_STATUS_HW_NONSUPPORT    FWMNG_UPIMGSTATUS_HW_NONSUPPORT  // F/W image is not for own H/W platform
#define FUOTA_FWIMG_STATUS_AVAILABLE        FWMNG_UPIMGSTATUS_AVAILABLE      // F/W image is valid
#endif

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*** Delete F/W Image Status (FirmwareManagement) ***/
#define FUOTA_FWIMG_DELETEIMG_STATUS_OK                 FWMNG_DELETEIMG_STATUS_OK               // F/W image has been deleted
#define FUOTA_FWIMG_DELETEIMG_STATUS_NO_VALID_IMAGE     FWMNG_DELETEIMG_STATUS_NO_VALID_IMAGE   // no valid F/W image
#define FUOTA_FWIMG_DELETEIMG_STATUS_INVALID_VERSION    FWMNG_DELETEIMG_STATUS_INVALID_VERSION  // version mismatch
#endif

/*** API ***/
extern FuotaStatus_t FuotaInit( FuotaEventCb_t *p_fuotaEventCb );
extern void FuotaStart( void );
extern void FuotaStop( void );

extern void FuotaMcpsConfirm( McpsConfirm_t *p_mcpsConfirm );
extern FuotaStatus_t FuotaMcpsIndication( McpsIndication_t *p_mcpsIndication );
extern void FuotaMlmeConfirm( MlmeConfirm_t *p_mlmeConfirm );
extern void FuotaMlmeIndication( MlmeIndication_t *p_mlmeIndication );
extern void FuotaProcess( void );

extern FuotaStatus_t FuotaIbGetRequest( uint8_t ib, void *vpVal );
extern FuotaStatus_t FuotaIbSetRequest( uint8_t ib, void *vpVal );

extern bool FuotaIsLowPowerAllowed( void );

#endif  /* __LORAFUOTAPROCESS_H__ */
