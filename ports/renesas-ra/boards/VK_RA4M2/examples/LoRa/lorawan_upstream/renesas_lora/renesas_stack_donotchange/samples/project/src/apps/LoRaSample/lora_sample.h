/*
    (C) 2017-2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

/*
 * lora_sample.h
 *
 *  Created on: 2017/07/28
 *      Author:
 */

#ifndef _LORA_SAMPLE_H_
#define _LORA_SAMPLE_H_

#include "at_proc.h"
#include "lorawan_proc.h"
#include "utilities.h"

/* macro    */
#define APP_CONFIG_VERSION_FW   0x04900000  // 4Byte (Major-Minor-Patch-Rev)
#define APP_CONFIG_VERSION_HW   0x01000000  // 4Byte (Major-Minor-Patch-Rev)

#define APP_SYSTEM_MAX_RX_ERROR     (10)   // msec, System maximum overall timing error

/* configuration */
// #define APP_AT_KEY_READ_ENABLED          // enables AT commands to read key (AppKey, NwkSKey, AppSKey, GenAppKey) (for debug purpose) 

#if APP_TEST_LOWPOWER
extern uint8_t AppEnableAtCmdInput;
#endif

extern const Version_t AppFwVersion;
extern const Version_t AppHwVersion;

/* prototype    */
LoRaMacStatus_t AppJoinReq(void);

/*! mcps confirm handler */
void AppMcpsConfirmHandler(McpsConfirm_t *mcpsConfirm);

/*! mcps indication handler */
void AppMcpsIndicationHandler(McpsIndication_t *mcpsIndication);

/*! mlme confirm handler    */
void AppMlmeConfirmHandler(MlmeConfirm_t *mlmeConfirm);

/*! mlme indication handler */
void AppMlmeIndicationHandler( MlmeIndication_t *mlmeIndication );

void AppLoadParams(uint8_t mode);
void AppSaveParams(void);
void AppResetParams(void);
void AppFactoryResetParams(void);


#endif /* _LORA_SAMPLE_H_ */
