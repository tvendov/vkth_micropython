/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __LORAFRAGMENTPROCESS_H__
#define __LORAFRAGMENTPROCESS_H__

#ifdef FUOTA_ENABLED
    #ifdef DEBUG_FUOTA
    #define DEBUG_FRGMNT
    #endif
#else
    #define DEBUG_FRGMNT
#endif

#include "LoRaFragmentConfig.h"
#ifndef FUOTA_VERSION
    #define FUOTA_VERSION_1_0_0     (0x01000000)
    #define FUOTA_VERSION_2_0_0     (0x02000000)
    #define FUOTA_VERSION           FUOTA_VERSION_1_0_0
#endif

// FPort, PackageID and PackageVersion for FragmentDataBlock
#define FRGMNT_FPORT                            201
#define FRGMNT_PACKAGE_IDENTIFIER               3
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define FRGMNT_PACKAGE_VERSION                  2
#else
#define FRGMNT_PACKAGE_VERSION                  1
#endif

/*----------*/

// status

#ifdef FUOTA_ENABLED
#include "LoRaFuotaStatus.h"
typedef FuotaStatus_t   __frgmntStatus_t;
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
} __frgmntStatus_t;
#endif

typedef __frgmntStatus_t    FrgmntStatus_t;
#define FRGMNT_STATUS_OK                        FUOTA_STATUS_OK
#define FRGMNT_STATUS_ERROR                     FUOTA_STATUS_ERROR
#define FRGMNT_STATUS_BUSY                      FUOTA_STATUS_BUSY
#define FRGMNT_STATUS_SERVICE_UNKNOWN           FUOTA_STATUS_SERVICE_UNKNOWN
#define FRGMNT_STATUS_PARAMETER_INVALID         FUOTA_STATUS_PARAMETER_INVALID
#define FRGMNT_STATUS_IB_READONLY               FUOTA_STATUS_IB_READONLY
#define FRGMNT_STATUS_LENGTH_ERROR              FUOTA_STATUS_LENGTH_ERROR
#define FRGMNT_STATUS_COMMAND_ERROR             FUOTA_STATUS_COMMAND_ERROR
#define FRGMNT_STATUS_PENDING                   FUOTA_STATUS_PENDING

/*----------*/

// Event
typedef struct {
    FrgmntStatus_t (*LoRaFrgmntSessionSetupIndication)( uint8_t fragIndex, uint32_t descriptor );
    void (*LoRaFrgmntDataBlockIndication)( uint8_t fragIndex, uint8_t *p_dataBlk, uint32_t dataBlkSize );
    void (*LoRaFrgmntSessionEndIndication)( uint8_t fragIndex );
} LoRaFrgmntEventCb_t;

/*----------*/

// IB - index
// IB - default value

/*----------*/

// functions
extern FrgmntStatus_t LoRaFragmentInit( LoRaFrgmntEventCb_t *p_frgmntEventCb );
extern FrgmntStatus_t LoRaFragmentStart( void );
extern void LoRaFragmentStop( void );

extern FrgmntStatus_t LoRaFragmentMcpsIndication( McpsIndication_t *p_mcpsIndication );
extern void LoRaFragmentProcessCommand( uint8_t  *p_buffer, 
                                        uint8_t  *p_payloadLen, 
                                        uint8_t  bufferMaxSize, 
                                        uint32_t *p_txDelayMs,
                                        bool     *p_isRetransEn );
extern void LoRaFragmentProcessEvent( void );
extern bool LoRaFragmentIsIdle( void );
extern void LoRaFragmentSendCompCommand( bool isSuccess );
extern FrgmntStatus_t LoRaFragmentIbGetRequest( uint8_t ib, void *vpVal );
extern FrgmntStatus_t LoRaFragmentIbSetRequest( uint8_t ib, void *vpVal );
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
extern FrgmntStatus_t LoRaFragmentGetRcvdCmdPayloadLen( uint8_t cid, uint8_t *p_cmdPayloadLen );
#endif

#endif  /* __LORAFRAGMENTPROCESS_H__ */
