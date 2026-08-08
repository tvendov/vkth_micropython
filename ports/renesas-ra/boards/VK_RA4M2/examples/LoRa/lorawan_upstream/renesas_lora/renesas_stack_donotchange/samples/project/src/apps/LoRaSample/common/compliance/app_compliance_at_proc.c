/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/

#if defined(APP_COMPLIANCE)
    
#include <stdio.h>
#include "board.h"
    
#include "LoRaMac.h"
#include "lorawan_proc.h"
#include "at-command.h"
#include "at-parser.h"
#include "at_proc.h"
#include "app_compliance.h"

#if (AT_EXTCMD_TAB_ARRAYSIZE > 0)
AtResultCode_t AppComplianceAtModeAct(void *p);
AtResultCode_t AppComplianceAtModeSet(void *p);
AtResultCode_t AppComplianceAtModeRead(void *p);
#endif

void AppComplianceAtExtendCmdRegist( void )
{
#if (AT_EXTCMD_TAB_ARRAYSIZE > 0)
    AtExtendCmdRegist("+COMPLIANCE", AppComplianceAtModeSet, AppComplianceAtModeRead, AppComplianceAtModeAct);
#endif
}


AtResultCode_t AppComplianceAtModeAct(void *p)
{
    LoRaMacStatus_t status;
    uint8_t         complianceTestModeEnabled;

    complianceTestModeEnabled = 1;
    status = AppLoraWanSetCertFPortOn( complianceTestModeEnabled );
    if (status != LORAMAC_STATUS_OK)
    {
        AppAtMacStatusResult(status);
        return AT_RC_ERR;
    }

    // response OK before starting compliance test
    print("OK");
    AtPrintTrailer();

    // force to enable complianceTestMode
    if( appLoraWanSettings.complianceTestMode == APP_COMPLIANCE_TESTMODE_NONE )
    {
        appLoraWanSettings.complianceTestMode = APP_COMPLIANCE_TESTMODE_LORAWAN;
    }

    // start compliance test
    AppCompliacneInit();

    return AT_RC_NOANS;
}

AtResultCode_t AppComplianceAtModeSet(void *p)
{
    uint8_t argc;                           // number of arguments
    int8_t *argv;                           // argument
    int16_t argvLen;                        // length of an argument
    AtResultCode_t ret = AT_RC_ERR;         // result code
    LoRaMacStatus_t status;
    uint8_t certFportOn = 0;

    argc = AtParseListLen();
    if (argc == 1)
    {
        AtParsePopOne(&argv, &argvLen);
        if (argvLen == 1)
        {
            if (argv[0] == '0')
            {
                appLoraWanSettings.complianceTestMode = APP_COMPLIANCE_TESTMODE_NONE;
                ret = AT_RC_OK; 
            }
            else if (argv[0] == '1')
            {
                appLoraWanSettings.complianceTestMode = APP_COMPLIANCE_TESTMODE_LORAWAN;
                certFportOn = 1;
                ret = AT_RC_OK; 
            }
        }

        if (ret == AT_RC_OK) {
            status = AppLoraWanSetCertFPortOn( certFportOn );
            if (status != LORAMAC_STATUS_OK) {
                AppAtMacStatusResult(status);
                ret = AT_RC_ERR;
            }
        }
    }

    return ret;
  
}

AtResultCode_t AppComplianceAtModeRead(void *p)
{
    AtResultCode_t ret = AT_RC_OK;		// result code
    uint8_t		   complianceTestMode;

    complianceTestMode = appLoraWanSettings.complianceTestMode;
    if (complianceTestMode == APP_COMPLIANCE_TESTMODE_NONE)
    {
        AppAtOutputResultCode("0:COMPLIANCE_TEST_DISABLED");
    }
    else if (complianceTestMode == APP_COMPLIANCE_TESTMODE_LORAWAN)
    {
        AppAtOutputResultCode("1:LORAWAN_COMPLIANCE_TEST_ENABLED");
    }
    else
    {
        // in case parameter stored in data flash is wrong
        ret = AT_RC_ERR;
    }

    return ret;
}
#endif

