/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __LORAAPPCLOCKSYNCPROCESS_H__
#define __LORAAPPCLOCKSYNCPROCESS_H__

#ifdef FUOTA_ENABLED
    #ifdef DEBUG_FUOTA
    #define DEBUG_CLKSNC
    #endif
#else
    #define DEBUG_CLKSNC
#endif

#include "LoRaAppClockSyncConfig.h"
#ifndef FUOTA_VERSION
    #define FUOTA_VERSION_1_0_0     (0x01000000)
    #define FUOTA_VERSION_2_0_0     (0x02000000)
    #define FUOTA_VERSION           FUOTA_VERSION_1_0_0
#endif

// FPort, PackageID and PackageVersion for ClockSync
#define CLKSNC_FPORT                            202
#define CLKSNC_PACKAGE_IDENTIFIER               1
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define CLKSNC_PACKAGE_VERSION                  2
#else
#define CLKSNC_PACKAGE_VERSION                  1
#endif

/*----------*/

// status

#ifdef FUOTA_ENABLED
#include "LoRaFuotaStatus.h"
typedef FuotaStatus_t   __clkSncStatus_t;
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
} __clkSncStatus_t;
#endif

typedef __clkSncStatus_t    ClkSncStatus_t;
#define CLKSNC_STATUS_OK                        FUOTA_STATUS_OK
#define CLKSNC_STATUS_ERROR                     FUOTA_STATUS_ERROR
#define CLKSNC_STATUS_BUSY                      FUOTA_STATUS_BUSY
#define CLKSNC_STATUS_SERVICE_UNKNOWN           FUOTA_STATUS_SERVICE_UNKNOWN
#define CLKSNC_STATUS_PARAMETER_INVALID         FUOTA_STATUS_PARAMETER_INVALID
#define CLKSNC_STATUS_IB_READONLY               FUOTA_STATUS_IB_READONLY
#define CLKSNC_STATUS_LENGTH_ERROR              FUOTA_STATUS_LENGTH_ERROR
#define CLKSNC_STATUS_COMMAND_ERROR             FUOTA_STATUS_COMMAND_ERROR
#define CLKSNC_STATUS_PENDING                   FUOTA_STATUS_PENDING

/*----------*/

// Event
typedef struct {
    void (*LoRaClkSncAppTimeReqCb)( bool isForceResync );
} LoRaClkSncEventCb_t;

/*----------*/

// IB - index
#define CLKSNC_IB_TIMEREQ_PERIOD_SEC            0x00  // period of AppTimeReq transmission
#define CLKSNC_IB_TIMEREQ_REQ_PERIODICITY       0x01  // [ReadOnly] requested periodicity by server
#define CLKSNC_IB_TIMEREQ_MIN_PERIODICITY       0x02  // acceptable periodicity (min)
#define CLKSNC_IB_TIMEREQ_MAX_PERIODICITY       0x03  // acceptable periodicity (max)
#define CLKSNC_IB_TIMEANS_REQUIRED              0x04  // AnsRequired parameter of AppTimeReq command
#define CLKSNC_IB_FORCESYNC_PERIOD_SEC          0x05  // period of force-AppTimeReq transmission
// IB - default value
#define CLKSNC_IB_INIT_TIMEREQ_PERIOD_SEC       256   // seconds
#define CLKSNC_IB_INIT_TIMEREQ_REQ_PERIODICITY  (-1)  // Don'tEdit; requested/in active periodicity by AS
#define CLKSNC_IB_INIT_TIMEREQ_MIN_PERIODICITY  0     // 0...15
#define CLKSNC_IB_INIT_TIMEREQ_MAX_PERIODICITY  15    // 0...15
#define CLKSNC_IB_INIT_TIMEANS_REQUIRED         0     // 0 = only out of synchronization / 1 = always
#define CLKSNC_IB_INIT_FORCESYNC_PERIOD_SEC     60    // 1...255 The delay between consecutive transmissions of the AppTimeReq

/*----------*/

// functions
extern ClkSncStatus_t LoRaClockSyncInit( LoRaClkSncEventCb_t *p_clkSncEventCb );
extern ClkSncStatus_t LoRaClockSyncStart( void );
extern void LoRaClockSyncStop( void );

extern ClkSncStatus_t LoRaClockSyncMcpsIndication( McpsIndication_t *p_mcpsIndication );
extern void LoRaClockSyncProcessCommand( uint8_t  *p_buffer, 
                                         uint8_t  *p_payloadLen, 
                                         uint8_t  bufferMaxSize, 
                                         uint32_t *p_txDelayMs,
                                         bool     *p_isRetransEn );
extern void LoRaClockSyncSendCompCommand( bool isSuccess );
extern ClkSncStatus_t LoRaClockSyncAppTimeReq( uint8_t *p_buffer, uint8_t *p_paylaodLen, uint8_t bufferMaxSize );
extern void LoRaClockSyncProcessEvent( void );
extern bool LoRaClockSyncIsIdle( void );
extern ClkSncStatus_t LoRaClockSyncIbGetRequest( uint8_t ib, void *vpVal );
extern ClkSncStatus_t LoRaClockSyncIbSetRequest( uint8_t ib, void *vpVal );
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
extern ClkSncStatus_t LoRaClockSyncGetRcvdCmdPayloadLen( uint8_t cid, uint8_t *p_cmdPayloadLen );
#endif
extern void LoRaClockSyncResetCorrectTime( void );
extern uint32_t LoRaClockSyncGetAppTimeSec( void );


#endif  /* __LORAAPPCLOCKSYNCPROCESS_H__ */
