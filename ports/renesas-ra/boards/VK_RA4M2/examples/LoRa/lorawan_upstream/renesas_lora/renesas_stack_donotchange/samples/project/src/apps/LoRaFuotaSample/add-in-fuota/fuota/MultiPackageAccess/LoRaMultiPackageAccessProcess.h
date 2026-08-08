/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __LORAMULTIPACKAGEACCESSPROCESS_H__
#define __LORAMULTIPACKAGEACCESSPROCESS_H__

#ifdef FUOTA_ENABLED
    #ifdef DEBUG_FUOTA
    #define DEBUG_MLPKG
    #endif
#else
    #define DEBUG_MLPKG
#endif

#include "LoRaMultiPackageAccessConfig.h"
#ifndef FUOTA_VERSION
//  #define FUOTA_VERSION_1_0_0     (0x01000000)
    #define FUOTA_VERSION_2_0_0     (0x02000000)
    #define FUOTA_VERSION           FUOTA_VERSION_2_0_0
#endif

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)  // Only V2.0.0 is available

// FPort, PackageID and PackageVersion for MultiPackage
#define MLPKG_FPORT                             225
#define MLPKG_PACKAGE_IDENTIFIER                0
#define MLPKG_PACKAGE_VERSION                   1

/*----------*/

// status

#ifdef FUOTA_ENABLED
#include "LoRaFuotaStatus.h"
typedef FuotaStatus_t   __mlPkgStatus_t;
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
} __mlPkgStatus_t;
#endif

typedef __mlPkgStatus_t MlPkgStatus_t;
#define MLPKG_STATUS_OK                         FUOTA_STATUS_OK
#define MLPKG_STATUS_ERROR                      FUOTA_STATUS_ERROR
#define MLPKG_STATUS_BUSY                       FUOTA_STATUS_BUSY
#define MLPKG_STATUS_SERVICE_UNKNOWN            FUOTA_STATUS_SERVICE_UNKNOWN
#define MLPKG_STATUS_PARAMETER_INVALID          FUOTA_STATUS_PARAMETER_INVALID
#define MLPKG_STATUS_IB_READONLY                FUOTA_STATUS_IB_READONLY
#define MLPKG_STATUS_LENGTH_ERROR               FUOTA_STATUS_LENGTH_ERROR
#define MLPKG_STATUS_COMMAND_ERROR              FUOTA_STATUS_COMMAND_ERROR
#define MLPKG_STATUS_PENDING                    FUOTA_STATUS_PENDING

/*----------*/

// typedef
typedef struct {
    uint8_t     packageId;
    uint8_t     packageVersion;
    uint8_t     fport;
} MlPkg_DevPackageElement_t;

/*----------*/

// Event
typedef struct {
    MlPkgStatus_t (*LoRaMlPkgPackageCmdPayloadLenReqCb)( uint8_t packageId, 
                                                         uint8_t cid, 
                                                         uint8_t *p_cmdPayloadLen );
    void (*LoRaMlPkgPackageListReqCb)( uint8_t                         *p_nbPackages, 
                                       MlPkg_DevPackageElement_t **pp_listPackages );
} LoRaMlPkgEventCb_t;

/*----------*/

// IB - index
#define MLPKG_IB_XXXX               0x00
// IB - default value
#define MLPKG_IB_INIT_XXXX          0x00

/*----------*/

// functions
extern MlPkgStatus_t LoRaMultiPackageAccessInit( LoRaMlPkgEventCb_t *p_mlPkgEventCb );
extern MlPkgStatus_t LoRaMultiPackageAccessStart( void );
extern void LoRaMultiPackageAccessStop( void );

extern MlPkgStatus_t LoRaMultiPackageAccessMcpsIndication( McpsIndication_t *p_mcpsIndication );
extern void LoRaMultiPackageAccessProcessCommand( uint8_t  *p_buffer, 
                                                  uint8_t  *p_payloadLen, 
                                                  uint8_t  bufferMaxSize, 
                                                  uint32_t *p_txDelayMs,
                                                  bool     *p_isRetransEn );
extern void LoRaMultiPackageAccessProcessEvent( void );
extern void LoRaMultiPackageAccessSendCompCommand( bool isSuccess );
extern MlPkgStatus_t LoRaMultiPackageAccessIbGetRequest( uint8_t ib, void *vpVal );
extern MlPkgStatus_t LoRaMultiPackageAccessIbSetRequest( uint8_t ib, void *vpVal );

extern bool LoRaMultiPackageAccessIsMultiPackBufferReqCommand( McpsIndication_t *p_mcpsInd );
extern MlPkgStatus_t LoRaMultiPackageAccessSplitMcpsIndication( McpsIndication_t *p_srcMcpsInd, 
                                                                McpsIndication_t *p_dstMcpsInd );
extern MlPkgStatus_t LoRaMultiPackageAccessCreateAnsUplink( uint8_t srcFport,    uint8_t *p_srcPayload, uint8_t srcLength, 
                                                            uint8_t *p_dstFport, uint8_t *p_dstPayload, uint8_t *p_dstLength,
                                                            uint8_t dstLenMax );
extern MlPkgStatus_t LoRaMultiPackageAccessGetNextMcpsInd( McpsIndication_t **pp_nextMcpsInd );

extern MlPkgStatus_t LoRaMultiPackageAccessGetRemainedAnsBufferSize( uint8_t *p_remainedSize );
extern bool LoRaMultiPackageAccessIsRemainedBufferFrag( void );
extern void LoRaMultiPackageAccesResetState( void );

#endif
#endif  /* __LORAMULTIPACKAGEACCESSPROCESS_H__ */
