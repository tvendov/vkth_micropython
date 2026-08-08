/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __APP_FUOTA_PROCESS_H__
#define __APP_FUOTA_PROCESS_H__

#include "LoRaFuotaProcess.h"

/* result of the preparation of FW update */
#include "app_fuota_fwupdate.h"
#define APP_FWUPDT_RESULT_OK_READY              FUOTAUPDT_UPDATE_READY_OK
#define APP_FWUPDT_RESULT_ERR_INVALID_FWIMG     FUOTAUPDT_UPDATE_READY_ERR_INVALID_FWIMG
#define APP_FWUPDT_RESULT_ERR_FWIMG_STORED      FUOTAUPDT_UPDATE_READY_ERR_FWIMG_STORED
#define APP_FWUPDT_RESULT_ERR_UPDATE_FAILED     FUOTAUPDT_UPDATE_READY_ERR_UPDATE_FAILED

/*----------*/
// functions
extern void AppFuotaInit( void );
extern void AppFuotaStart( void );
extern void AppFuotaStop( void );

extern FuotaStatus_t AppFuotaIbGetRequest( uint8_t ib, void *vpVal );
extern FuotaStatus_t AppFuotaIbSetRequest( uint8_t ib, void *vpVal );

extern FuotaStatus_t AppFuotaMcpsIndication( McpsIndication_t *p_mcpsIndication );
extern void AppFuotaMlmeConfirm( MlmeConfirm_t *p_mlmeConfirm );

extern void AppFuotaProcess( void );

extern FuotaStatus_t AppFuotaStartFirmwareUpdate( void );

extern bool AppFuotaIsLowPowerAllowed( void );

#endif  // __APP_FUOTA_PROCESS_H__

