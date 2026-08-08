/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __LORAFIRMWAREMANAGEMENTPROCESS_H__
#define __LORAFIRMWAREMANAGEMENTPROCESS_H__

#ifdef FUOTA_ENABLED
    #ifdef DEBUG_FUOTA
    #define DEBUG_FWMNG
    #endif
#else
    #define DEBUG_FWMNG
#endif

#include "LoRaFirmwareManagementConfig.h"
#ifndef FUOTA_VERSION
//  #define FUOTA_VERSION_1_0_0     (0x01000000)
    #define FUOTA_VERSION_2_0_0     (0x02000000)
    #define FUOTA_VERSION           FUOTA_VERSION_2_0_0
#endif

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)  // Only V2.0.0 is available

// FPort, PackageID and PackageVersion for FirmwareManagement
#define FWMNG_FPORT                             203
#define FWMNG_PACKAGE_IDENTIFIER                4
#define FWMNG_PACKAGE_VERSION                   1

/*----------*/

// status

#ifdef FUOTA_ENABLED
#include "LoRaFuotaStatus.h"
typedef FuotaStatus_t   __fwMngStatus_t;
#else
typedef enum
{
    FUOTA_STATUS_OK = 0,
    FUOTA_STATUS_ERROR,
    FUOTA_STATUS_BUSY,
    FUOTA_STATUS_SERVICE_UNKNOWN,
    FUOTA_STATUS_PARAMETER_INVALID,
    FUOTA_STATUS_IB_READONLY,
    FUOTA_STATUS_LENGTH_ERROR,
    FUOTA_STATUS_COMMAND_ERROR,
    FUOTA_STATUS_PENDING,
} __fwMngStatus_t;
#endif

typedef __fwMngStatus_t FwMngStatus_t;
#define FWMNG_STATUS_OK                         FUOTA_STATUS_OK
#define FWMNG_STATUS_ERROR                      FUOTA_STATUS_ERROR
#define FWMNG_STATUS_BUSY                       FUOTA_STATUS_BUSY
#define FWMNG_STATUS_SERVICE_UNKNOWN            FUOTA_STATUS_SERVICE_UNKNOWN
#define FWMNG_STATUS_PARAMETER_INVALID          FUOTA_STATUS_PARAMETER_INVALID
#define FWMNG_STATUS_IB_READONLY                FUOTA_STATUS_IB_READONLY
#define FWMNG_STATUS_LENGTH_ERROR               FUOTA_STATUS_LENGTH_ERROR
#define FWMNG_STATUS_COMMAND_ERROR              FUOTA_STATUS_COMMAND_ERROR
#define FWMNG_STATUS_PENDING                    FUOTA_STATUS_PENDING

// UpImageStatus (DevUpgradeImageAns parameter)
#define FWMNG_UPIMGSTATUS_NONE                  0x00  // no F/W image
#define FWMNG_UPIMGSTATUS_INVALID               0x01  // wrong F/W image
#define FWMNG_UPIMGSTATUS_HW_NONSUPPORT         0x02  // F/W image is not for own H/W platform
#define FWMNG_UPIMGSTATUS_AVAILABLE             0x03  // F/W image is valid

// DeleteImageStatus (Status parameter in DevDeleteImageAns)
#define FWMNG_DELETEIMG_STATUS_OK               0x00  // F/W image has been deleted
#define FWMNG_DELETEIMG_STATUS_NO_VALID_IMAGE   0x01  // no valid F/W image
#define FWMNG_DELETEIMG_STATUS_INVALID_VERSION  0x02  // version mismatch

/*----------*/

// Event
typedef struct {
    void (*LoRaFwMngVersionReqCb)( uint32_t *p_fwVersion, uint32_t *p_hwVersion );
    uint32_t (*LoRaFwMngCurrentTimeSecReqCb)( void );
    FwMngStatus_t (*LoRaFwMngRebootRequestEventCb)( uint32_t rebootSec );
    void (*LoRaFwMngRebootCancelEventCb)( void );
    void (*LoRaFwMngRebootExecEventCb)( void );
    uint8_t (*LoRaFwMngUpImageStatusRequestCb)( uint32_t *p_nextFirmwareVersion );
    uint8_t (*LoRaFwMngDeleteImageRequestEventCb)( uint32_t fwToDelVersion );
} LoRaFwMngEventCb_t;

/*----------*/

// IB - index
#define FWMNG_IB_XXXX               0x00
// IB - default value
#define RMTMC_IB_INIT_XXXX          0x00

/*----------*/

// functions
extern FwMngStatus_t LoRaFirmwareManagementInit( LoRaFwMngEventCb_t *p_fwMngEventCb );
extern FwMngStatus_t LoRaFirmwareManagementStart( void );
extern void LoRaFirmwareManagementStop( void );
extern FwMngStatus_t LoRaFirmwareManagementMcpsIndication( McpsIndication_t *p_mcpsIndication );
extern void LoRaFirmwareManagementProcessCommand( uint8_t  *p_buffer, 
                                                  uint8_t  *p_payloadLen, 
                                                  uint8_t  bufferMaxSize, 
                                                  uint32_t *p_txDelayMs,
                                                  bool     *p_isRetransEn );
extern void LoRaFirmwareManagementProcessEvent( void );
extern bool LoRaFirmwareManagementIsIdle( void );
extern void LoRaFirmwareManagementSendCompCommand( bool isSuccess );
extern FwMngStatus_t LoRaFirmwareManagementIbGetRequest( uint8_t ib, void *vpVal );
extern FwMngStatus_t LoRaFirmwareManagementIbSetRequest( uint8_t ib, void *vpVal );
extern FwMngStatus_t LoRaFirmwareManagementGetRcvdCmdPayloadLen( uint8_t cid, uint8_t *p_cmdPayloadLen );

#endif
#endif  /* __LORAFIRMWAREMANAGEMENTPROCESS_H__ */
