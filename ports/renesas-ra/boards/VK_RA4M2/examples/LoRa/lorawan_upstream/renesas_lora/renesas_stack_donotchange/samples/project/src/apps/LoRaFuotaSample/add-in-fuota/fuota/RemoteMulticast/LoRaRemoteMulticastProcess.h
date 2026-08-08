/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __LORAREMOTEMULTICASTPROCESS_H__
#define __LORAREMOTEMULTICASTPROCESS_H__

#ifdef FUOTA_ENABLED
    #ifdef DEBUG_FUOTA
    #define DEBUG_RMTMC
    #endif
#else
    #define DEBUG_RMTMC
#endif

#include "LoRaRemoteMulticastConfig.h"
#ifndef FUOTA_VERSION
    #define FUOTA_VERSION_1_0_0     (0x01000000)
    #define FUOTA_VERSION_2_0_0     (0x02000000)
    #define FUOTA_VERSION           FUOTA_VERSION_1_0_0
#endif

// FPort, PackageID and PackageVersion for RemoteMulticast
#define RMTMC_FPORT                             200
#define RMTMC_PACKAGE_IDENTIFIER                2
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define RMTMC_PACKAGE_VERSION                   2
#else
#define RMTMC_PACKAGE_VERSION                   1
#endif

/*----------*/

// status

#ifdef FUOTA_ENABLED
#include "LoRaFuotaStatus.h"
typedef FuotaStatus_t   __rmtMcStatus_t;
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
} __rmtMcStatus_t;
#endif

typedef __rmtMcStatus_t RmtMcStatus_t;
#define RMTMC_STATUS_OK                         FUOTA_STATUS_OK
#define RMTMC_STATUS_ERROR                      FUOTA_STATUS_ERROR
#define RMTMC_STATUS_BUSY                       FUOTA_STATUS_BUSY
#define RMTMC_STATUS_SERVICE_UNKNOWN            FUOTA_STATUS_SERVICE_UNKNOWN
#define RMTMC_STATUS_PARAMETER_INVALID          FUOTA_STATUS_PARAMETER_INVALID
#define RMTMC_STATUS_IB_READONLY                FUOTA_STATUS_IB_READONLY
#define RMTMC_STATUS_LENGTH_ERROR               FUOTA_STATUS_LENGTH_ERROR
#define RMTMC_STATUS_COMMAND_ERROR              FUOTA_STATUS_COMMAND_ERROR
#define RMTMC_STATUS_PENDING                    FUOTA_STATUS_PENDING

/*----------*/

// Event
typedef struct {
    uint32_t (*LoRaRmtMcCurrentTimeSecReqCb)( void );
    void (*LoRaRmtMcSessionSetupIndication)( DeviceClass_t sessionClass, 
                                             uint8_t mcGroupId, 
                                             uint32_t timeToStartSec, 
                                             uint32_t timeoutSec );
    void (*LoRaRmtMcSessionStartIndication)( DeviceClass_t sessionClass, 
                                             uint8_t mcGroupId, 
                                             uint32_t timeoutSec );
    void (*LoRaRmtMcSessionEndIndication)( DeviceClass_t sessionClass, uint8_t mcGroupId );
} LoRaRmtMcEventCb_t;

/*----------*/

// IB - index
// IB - default value

/*----------*/

// functions
extern RmtMcStatus_t LoRaRemoteMulticastInit( LoRaRmtMcEventCb_t *p_rmtMcEventCb );
extern RmtMcStatus_t LoRaRemoteMulticastStart( void );
extern void LoRaRemoteMulticastStop( void );

extern RmtMcStatus_t LoRaRemoteMulticastMcpsIndication( McpsIndication_t *p_mcpsIndication );
extern void LoRaRemoteMulticastProcessCommand( uint8_t  *p_buffer, 
                                               uint8_t  *p_payloadLen, 
                                               uint8_t  bufferMaxSize, 
                                               uint32_t *p_txDelayMs,
                                               bool     *p_isRetransEn );
extern void LoRaRemoteMulticastProcessEvent( void );
extern bool LoRaRemoteMulticastIsIdle( void );
extern void LoRaRemoteMulticastSendCompCommand( bool isSuccess );
extern RmtMcStatus_t LoRaRemoteMulticastIbGetRequest( uint8_t ib, void *vpVal );
extern RmtMcStatus_t LoRaRemoteMulticastIbSetRequest( uint8_t ib, void *vpVal );
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
extern RmtMcStatus_t LoRaRemoteMulticastGetRcvdCmdPayloadLen( uint8_t cid, uint8_t *p_cmdPayloadLen );
#endif


#endif  /* __LORAREMOTEMULTICASTPROCESS_H__ */
