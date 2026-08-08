/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/

#if defined(APP_COMPLIANCE)

#include "LoRacompliance.h"
#include "app_compliance_msg_display.h"

#define APP_COMPLIANCE_TESTMODE_NONE        0  // disable certification test program
#define APP_COMPLIANCE_TESTMODE_LORAWAN     1  // enable certification test program (LoRaWAN)
#define APP_COMPLIANCE_TESTMODE_FUOTA200    2  // enable certification test program (FUOTA200)

void AppCompliacneInit( void );
void AppComplianceProcess( void );

void AppComplianceAtExtendCmdRegist( void );

#endif

