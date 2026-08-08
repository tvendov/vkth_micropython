/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __APP_FUOTA_FWUPDATE_H__
#define __APP_FUOTA_FWUPDATE_H__

#include "LoRaFuotaConfig.h"
#include "app_fwupdate_area.h"

/*----------*/
// status
#include "LoRaFuotaStatus.h"
typedef FuotaStatus_t   __fuotaUpdateStatus_t;

typedef __fuotaUpdateStatus_t   FuotaUpdateStatus_t;
#define FUOTAUPDT_STATUS_OK             FUOTA_STATUS_OK
#define FUOTAUPDT_STATUS_ERROR          FUOTA_STATUS_ERROR
#define FUOTAUPDT_STATUS_BUSY           FUOTA_STATUS_BUSY

/*----------*/
// statement
#define FUOTAUPDT_STATE_NONE                    0x00
#define FUOTAUPDT_STATE_INITIAL                 0x01
#define FUOTAUPDT_STATE_RUNNING                 0x02
#define FUOTAUPDT_STATE_SUCCESS_WAITING_READY   0x03  // (bank update only)
#define FUOTAUPDT_STATE_SUCCESS                 0x04
#define FUOTAUPDT_STATE_FAILED                  0x80
#define FUOTAUPDT_STATE_VERIFY_ERR              0x83
#define FUOTAUPDT_STATE_FATAL_ERR               0xFF  // FWUpdateProgram is not found or invalid

/*----------*/
// F/W image
/*--- F/W image header ---*/
typedef struct {
    uint32_t    index;
} FuotaUpdtImageHeader_t;

#define FUOTAUPDT_SIZEOF_FWIMGHDR      (sizeof(FuotaUpdtImageHeader_t))

/*--- F/W image information ---*/
typedef struct {
    uint8_t     imageBlockNum;
    uint8_t     imageBlockIndex;  // must be 0
    uint8_t     __pad32bit[2];    //*** caution; explicitly define the padding
    uint32_t    imageVersion;
    uint32_t    imageSize;
    uint8_t     imagePriority;
    uint8_t     _reserved;        // for alignment
    uint8_t     imageVerify[32];
    // (must be even size)
} FuotaUpdtImageInfo_t;

#define FUOTAUPDT_SIZEOF_FWIMGINFO          (44)
#define FUOTAUPDT_FWIMGINFO_IMGVERIFY_AREA  (12)
#define FUOTAUPDT_FWIMGINFO_IMGVERIFY_SIZE  (32)

/*--- F/W update; image blocks ---*/
typedef struct {
    uint32_t    _imageBlockStartAddr;   // it is not included in image block format
    uint8_t     imageBlockNum;
    uint8_t     imageBlockIndex;
    uint8_t     __pad32bit[2];    //*** caution; explicitly define the padding. MUST BE 0
    uint32_t    codeAddress;
    uint32_t    codeSize;
    uint8_t     *p_code;  // access code by pointer
} FuotaUpdtImageBlock_t;

/*** Event; notify to app_fuota_process.c ***/
typedef struct {
    void (*AppFuotaUpdateReadyIndication)( void );
    void (*AppFuotaUpdateErrorIndication)( uint8_t result );
    void (*AppFuotaUpdateFinishedIndication)( bool bIsSuccess );
} FuotaUpdateEventCb_t;

/*----------*/
// result of the preparation of FW update
#define FUOTAUPDT_UPDATE_READY_OK                   0   // (BootSwap) FWImage is ready / (BankSwap) finish writeing updated FW to rewrite-bank
#define FUOTAUPDT_UPDATE_READY_ERR_INVALID_FWIMG    1   // received FWImage is invalid
#define FUOTAUPDT_UPDATE_READY_ERR_FWIMG_STORED     2   // storing FWImage is failed
#define FUOTAUPDT_UPDATE_READY_ERR_UPDATE_FAILED    3   // (BankSwap only) Failed to write updated FW to rewrite-bank

/*----------*/
// functions
extern FuotaUpdateStatus_t AppFuotaUpdateInitialization( FuotaUpdateEventCb_t *p_appFuotaUpdtEventCb );
extern void AppFuotaUpdateProcess( void );

extern FuotaUpdateStatus_t AppFuotaUpdateStoreFwImage( uint8_t *p_dataBlk, uint16_t dataSize );
extern void AppFuotaUpdate_GetNextImageBlock( FuotaUpdtImageBlock_t **pp_nextBlock );

extern uint8_t AppFuotaUpdateGetStatus( void );
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
extern void AppFuotaUpdateReset( void );
extern uint32_t AppFuotaUpdateGetFirmwareImageVersion( void );
#endif

typedef void (*AppFuotaUpdatePre_t)( void );
extern FuotaUpdateStatus_t AppFuotaUpdateStartFwUpdate( AppFuotaUpdatePre_t p_preUpdateCbFunc );

extern bool AppFuotaUpdateIsLowPowerAllowed( void );


#endif  // __APP_FUOTA_FWUPDATE_H__

