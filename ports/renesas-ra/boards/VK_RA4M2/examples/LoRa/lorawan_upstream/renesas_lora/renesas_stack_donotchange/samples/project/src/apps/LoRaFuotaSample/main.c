/*
    (C) 2017 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "board.h"
#include "radio.h"

#include "at_proc.h"
#include "lorawan_proc.h"
#include "lora_sample.h"
#include "dflash.h"
#if defined(FUOTA_ENABLED)
#include "cflash.h"
#include "app_fuota_process.h"
#endif
#if defined(APP_COMPLIANCE)
#include "app_compliance.h"
#endif

/* macro */
#define APP_BEACON_GWSPECIFIC_INFO_SIZE     6

/* const    */
/*!
 * Version information
 */
const Version_t AppFwVersion = { .Value = APP_CONFIG_VERSION_FW };
const Version_t AppHwVersion = { .Value = APP_CONFIG_VERSION_HW };

/*!
 * default LoRaWAN sample application setting
 */
const AppLoraWanSettings_t appLoraWanDefaultSettings = {
    { APP_LORAWNA_DATA_FORMAT_VERSION },  /* "LWFV2.10"       */
#if defined(REGION_EU868)
    LORAMAC_REGION_EU868,       /* operate region   */
#elif defined(REGION_US915)
    LORAMAC_REGION_US915,       /* operate region   */
#elif defined(REGION_AS923)
    LORAMAC_REGION_AS923,       /* operate region   */
#elif defined(REGION_CN779)
    LORAMAC_REGION_CN779,
#elif defined(REGION_EU433)
    LORAMAC_REGION_EU433,
#elif defined(REGION_AU915)
    LORAMAC_REGION_AU915,
#elif defined(REGION_CN470)
    LORAMAC_REGION_CN470,
#elif defined(REGION_KR920)
    LORAMAC_REGION_KR920,
#elif defined(REGION_IN865)
    LORAMAC_REGION_IN865,
#elif defined(REGION_RU864)
    LORAMAC_REGION_RU864,
#else
#error "Please set correct REGION macro."
#endif
    CLASS_A,                    /* operation class  */
    APP_LORAWAN_ACTMODE_OTAA,           /* activation: APP_ACTMODE_ABP / APP_ACTMODE_OTAA   */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },     /* AppKey   */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },     /* DevEUI                       */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },     /* AppEUI                       */
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },     /* NwkSKey  */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },     /* AppSKey  */
#endif
#if (LORAMAC_MAX_MC_CTX > 0)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },     /* GenAppSKey  */
#endif
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    0x00000000,                                             /* DevAddr                      */
#endif
    1,                                                      /* Duty cycle : 1: enabled, 0: disabled */
    MCPS_UNCONFIRMED,                                       /* message type (MCPS_CONFIRMED / MCPS_UNCONFIRMED) */
    APP_LORAWAN_DEFAULT_FPORT,                              /* Fpot */
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    0x000000,                                               /* NetID                        */
#endif
    true,                                                   /* ADR mode                     */
    APP_LORAWAN_RSSI_OFF,                                   /* RSSI verbose mode            */
    DR_0,                                                   /* Default data rate            */
    true,                                                   /* Public network               */
#ifdef LORAMAC_CLASSB_ENABLED
    7,                                                      /* Ping slot periodicity        */
#endif
    0,                                                      /* Certification FPort On       */
    0,                                                      /* number of channels defaulat mask entries (0:N/A) */
    { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },     /* channels default mask        */
#if defined(APP_COMPLIANCE)
    0,                                                      /* Compliance test mode      */
#endif
};


/* prototypes       */
void AppInit(void);
void AppProcMlmeJoinOnJoinReq(MlmeConfirm_t *mlmeConfirm);
void AppProcMlmeLinkChkOnSentUnconf(MlmeConfirm_t *mlmeConfirm);
void AppProcMcpsConfirmOnSentConf(McpsConfirm_t *mcpsConfirm);
void AppProcMcpsConfirmOnSentUnconf(McpsConfirm_t *mcpsConfirm);
void AppProcMlmeDevTimeReqConfirm(MlmeConfirm_t *mlmeConfirm);
#ifdef LORAMAC_CLASSB_ENABLED
void AppProcMlmePinSlInfReqConfirm(MlmeConfirm_t *mlmeConfirm);
void AppProcMlmeBconAcqReqConfirm(MlmeConfirm_t *mlmeConfirm);
void AppProcMlmeBconTimReqConfirm(MlmeConfirm_t *mlmeConfirm);
void AppMlmeIndicationBeaconLock(MlmeIndication_t *mlmeIndication);
void AppMlmeIndicationBeaconLost(MlmeIndication_t *mlmeIndication);
#endif
bool AppBoardIsLowPowerAllowed(void);

/* global variables */

/*! application settings    */
AppLoraWanSettings_t    appLoraWanSettings;
AppLoraWanNvmData_t     appLoraWanNvmData = { .restoreMode = APP_LORAWAN_NVMDATA_RESTORE_NORMAL };

/*
 * APP_TEST_LOWPOWER
 *   When a button is pushed, MCU low power operation is disallowed
 *   until an AT command is input.
 */
#if APP_TEST_LOWPOWER
uint8_t AppEnableAtCmdInput = false;
#endif

/* functions        */
void AppInit(void)
{
    LoRaMacStatus_t status;

    AppAtInit();            /* init AT command  */

    BoardSetIsLowPowerAllowedCallback(AppBoardIsLowPowerAllowed); // Set callback function for BoardIsLowPowerAllowed()

#if defined(DEBUG_LORAMAC)
    LoRaMacDebugInit();
#endif

    AppLoadParams(0);   /* 0:load parameters from data flash    */

    status = AppLoraWanInit(NULL, NULL);  /* init LoRaWan     */
    if (status == LORAMAC_STATUS_REGION_NOT_SUPPORTED)
    {
        print("Available region have to be set with +REGION command.");
        AtPrintTrailer();
    }
    else if (status == LORAMAC_STATUS_RADIO_FAIL)
    {
        print("Radio driver initialization is failed.");
        AtPrintTrailer();
    }
    else if (status == LORAMAC_STATUS_PARAMETER_INVALID) {
        print("Parameters for Mac Initialization are invalid.");
        AtPrintTrailer();
    }
}

/*!
 * @fn
 * activate LoRaWAN stack according to activation mode (ABP/OTAA)
 */
LoRaMacStatus_t AppJoinReq(void)
{
    LoRaMacStatus_t status;

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_ABP) {
        // Set network jointed to true
        status = AppLoraWanSetNetworkJoined(true);
    }
    else // if (appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_OTAA)
#endif
    {
        status = AppLoraWanJoinReq(NULL);
    }

    return status;
}

void AppMcpsConfirmHandler(McpsConfirm_t *mcpsConfirm)
{
#if defined(FUOTA_ENABLED)
    // if the last message sent from FUOTA,
    // do not display the status of McpsConfirm.
    if (appAtLastSentMessage == APP_AT_LAST_SENT_MESSAGE_NONE) {
        return;
    }
#endif
    // if the last message sent from application is MAC command only,
    // do not display the status of McpsConfirm. (i.e. AT+LINKCHK)
    if (appAtLastSentMessage == APP_AT_LAST_SENT_MESSAGE_CMD) {
        return;
    }

    // in case some AT commands are issued before completion of
    // non-blocking AT commands (AT+SEND, AT+SENDHEX)
    AtCmdSetCurrentCmd(appAtLastAtCmdIndexData);

    switch(mcpsConfirm->McpsRequest) {
        case MCPS_UNCONFIRMED:
            AppProcMcpsConfirmOnSentUnconf(mcpsConfirm);
            break;
        case MCPS_CONFIRMED:
            AppProcMcpsConfirmOnSentConf(mcpsConfirm);
            break;
        default:
            // not supported
            break;
    }
#if defined(FUOTA_ENABLED)
    appAtLastSentMessage = APP_AT_LAST_SENT_MESSAGE_NONE;
#endif
}

void AppProcMcpsConfirmOnSentUnconf(McpsConfirm_t *mcpsConfirm)
{
    switch (mcpsConfirm->Status) {
        case LORAMAC_EVENT_INFO_STATUS_OK:
            AppAtOutputResultCode("OK");
            break;
        default:
            AppAtMacEventResult(mcpsConfirm->Status);
            break;
    }
}

void AppProcMcpsConfirmOnSentConf(McpsConfirm_t *mcpsConfirm)
{
    if ((mcpsConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK)
        && (mcpsConfirm->AckReceived == true)) {
        AppAtOutputResultCode("ACK_RECEIVED");
    }
    else {
        AppAtOutputResultCode("NO_ACK");
    }
}

void AppMcpsIndicationHandler(McpsIndication_t *mcpsIndication)
{
    uint8_t msgBuff[3];
    uint8_t i;

#if defined(FUOTA_ENABLED)
    FuotaStatus_t fuotaProc;
    fuotaProc = AppFuotaMcpsIndication( mcpsIndication );
    if (fuotaProc == FUOTA_STATUS_OK)
    {
        // McpsIndication has been processed in FUOTA.
        return;
    }
#endif
    switch (mcpsIndication->McpsIndication) {
        case MCPS_UNCONFIRMED:
        case MCPS_CONFIRMED:
#if (LORAMAC_MAX_MC_CTX > 0)
        case MCPS_MULTICAST:
#endif
            if (mcpsIndication->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
                if (mcpsIndication->BufferSize != 0) {
                    // data
                    AtPrintCmdHeader("+RCVD");
                    print(" ");
                    for (i = 0 ; i < mcpsIndication->BufferSize ; i++) {
                        AppAtHexArray2HexStr(msgBuff, &(mcpsIndication->Buffer[i]), 1);
                        print((char *)msgBuff);
                    }
                    print(",");
                    print_dec(mcpsIndication->Port, 3, '\0');
                    print(",");
                    print_dec(mcpsIndication->RxSlot, 1, '\0');
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
                    print(",");
                    print_dec( mcpsIndication->ResponseTimeout, 10, '\0' );
#endif
                    AtPrintTrailer();
                }

                // RSSI
                if (appLoraWanSettings.rssi == APP_LORAWAN_RSSI_ON) {
                    AtPrintCmdHeader("+RSSI");
                    print(" ");
                    print_dec(mcpsIndication->Rssi, 3, '\0');
                    print(",");
                    print_dec(mcpsIndication->Snr, 3, '\0');
                    AtPrintTrailer();
                }
            }
            //else {
            //    AppAtMacEventResult(mcpsIndication->Status);
            //}
            break;
        default:
            // Proprietary (MCPS_PROPRIETARY) is not supported
            // Not supported for this sample application
            break;
    }
}

void AppMlmeConfirmHandler(MlmeConfirm_t *mlmeConfirm)
{
#if defined(FUOTA_ENABLED)
    AppFuotaMlmeConfirm( mlmeConfirm );
#endif
    switch (mlmeConfirm->MlmeRequest) {
        case MLME_JOIN:
            AppProcMlmeJoinOnJoinReq(mlmeConfirm);
            break;
        case MLME_LINK_CHECK:
            AppProcMlmeLinkChkOnSentUnconf(mlmeConfirm);
            break;
        case MLME_DEVICE_TIME:
            AppProcMlmeDevTimeReqConfirm(mlmeConfirm);
            break;
#ifdef LORAMAC_CLASSB_ENABLED
        case MLME_PING_SLOT_INFO:
            AppProcMlmePinSlInfReqConfirm(mlmeConfirm);
            break;
        case MLME_BEACON_ACQUISITION:
            AppProcMlmeBconAcqReqConfirm(mlmeConfirm);
            break;
        case MLME_BEACON_TIMING:
            AppProcMlmeBconTimReqConfirm(mlmeConfirm);
            break;
#endif
        default:
            break;
    }
#if defined(FUOTA_ENABLED)
   appAtLastSentMessage = APP_AT_LAST_SENT_MESSAGE_NONE;
#endif
}

void AppMlmeIndicationHandler( MlmeIndication_t *mlmeIndication )
{
    /*
     * This indication will be called when MAC would have commands should be
     * send to Network. (mlmeIndication->MlmeIndication == MLME_SCHEDULE_UPLINK)
     * Application have to send any uplink message to send the commands.
     */

    switch( mlmeIndication->MlmeIndication ){
#ifdef LORAMAC_CLASSB_ENABLED
        case MLME_BEACON_LOST:
            AppMlmeIndicationBeaconLost(mlmeIndication);
            break;
        case MLME_BEACON:
            AppMlmeIndicationBeaconLock(mlmeIndication);
            break;
#endif
        default:
            break;
    }
}

void AppProcMlmeJoinOnJoinReq(MlmeConfirm_t *mlmeConfirm)
{
    AtPrintCmdHeader("+JOIN");
    print(" ");
    if (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
        // join successed
        print("JOIN_ACCEPTED");
    }
    else {
        // join failed
        if (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_JOIN_NONCE_FAIL) {
            print("JOIN_NONCE_FAILED");
        }
        else {
            print("JOIN_FAILED");
        }
    }
    AtPrintTrailer();
}

void AppProcMlmeLinkChkOnSentUnconf(MlmeConfirm_t *mlmeConfirm)
{
    if ((appAtMlmeHandle & APP_AT_MLME_HANDLE_LINKCHK) == APP_AT_MLME_HANDLE_LINKCHK) {
        AtPrintCmdHeader("+LINKCHK");
        print(" ");
        switch (mlmeConfirm->Status) {
            case LORAMAC_EVENT_INFO_STATUS_OK:
                print_dec(mlmeConfirm->DemodMargin, 3, '\0');
                print(",");
                print_dec(mlmeConfirm->NbGateways, 3, '\0');
                break;
            default:
                print("NO_RESPONSE");
                break;
        }
        AtPrintTrailer();

        appAtMlmeHandle &= ~(APP_AT_MLME_HANDLE_LINKCHK);
    }
}

void AppLoadParams(uint8_t mode)
{
    const uint8_t formatVer[] = { APP_LORAWNA_DATA_FORMAT_VERSION };

    if (mode == 0) {    // load settings from data flash. If the data flash is empty, set default settings
        // load parameters into "appLoraWanSettings".
        DfLoadDFlash2Array((uint8_t *)&appLoraWanSettings, sizeof(AppLoraWanSettings_t), (uint8_t *)&appLoraWanDefaultSettings);

        if (strncmp((const char *)appLoraWanSettings.formatVer, (const char *)formatVer,
                                                    APP_LORAWAN_DATA_FORMAT_VERSION_LEN) == 0) {
            AppLoraWanNvmDataMgmtSetRestoreMode(APP_LORAWAN_NVMDATA_RESTORE_NORMAL);
        }
        else {
            // If the data format of the loaded parameters is not the one expected, reset parameters
            AppFactoryResetParams();
        }
    }
    else {
        AppResetParams();
    }
}

void AppSaveParams(void)
{
    // get current settings from MIB
    appLoraWanSettings.class = AppLoraWanGetDeviceClass();
    appLoraWanSettings.dCycle = AppLoraWanGetDCycle();
    appLoraWanSettings.adr = AppLoraWanGetAdr();
    appLoraWanSettings.publicNetwork = AppLoraWanGetPublicNetwork();
#ifdef LORAMAC_CLASSB_ENABLED
    appLoraWanSettings.periodicity = AppLoraWanGetPeriodicity();
#endif
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    appLoraWanSettings.devAddr = AppLoraWanGetDevAddr();
    appLoraWanSettings.netID = AppLoraWanGetNetID();
#endif

    // following parameters are already set to appLoraWanSettings
    //   region;
    //   actMode;
    //   appKey[APP_LORAWAN_LEN_APPKEY];
    //   devEUI[APP_LORAWAN_LEN_DEVEUI];
    //   appEUI[APP_LORAWAN_LEN_APPEUI];
    //   mtype;
    //   fPort;
    //   rssi;
    //   dr;

    // save parameters in "appLoraWanSettings" to Flash.
    DfSaveArray2DFlash((uint8_t *)&appLoraWanSettings, sizeof(AppLoraWanSettings_t));

    // save NVM persistent values
    AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_ALL);

}

void AppResetParams(void)
{
    // set default parameters into "appLoraWanSettings"
    memcpy(&appLoraWanSettings, &appLoraWanDefaultSettings, sizeof(AppLoraWanSettings_t));

    AppLoraWanNvmDataMgmtSetRestoreMode(APP_LORAWAN_NVMDATA_RESTORE_RESETNVM);
}

void AppFactoryResetParams(void)
{
    // Initialize data flash area used for appLoraWanSettings and appLoraWanNvmData
    RpMcuEepromFormat();

    AppResetParams();
}

void AppProcMlmeDevTimeReqConfirm(MlmeConfirm_t *mlmeConfirm)
{
    if ((appAtMlmeHandle & APP_AT_MLME_HANDLE_DEVTIME) == APP_AT_MLME_HANDLE_DEVTIME) {
        AtPrintCmdHeader("+DEVTIME");
        print(" ");
        switch (mlmeConfirm->Status) {
            case LORAMAC_EVENT_INFO_STATUS_OK:
                print("OK");
                break;
            default:
                print("NO_RESPONSE");
                break;
        }
        AtPrintTrailer();

        appAtMlmeHandle &= ~(APP_AT_MLME_HANDLE_DEVTIME);
    }
}

#ifdef LORAMAC_CLASSB_ENABLED
void AppProcMlmePinSlInfReqConfirm(MlmeConfirm_t *mlmeConfirm)
{
    if ((appAtMlmeHandle & APP_AT_MLME_HANDLE_PNGSLINFO) == APP_AT_MLME_HANDLE_PNGSLINFO) {
        AtPrintCmdHeader("+PNGSLINFO");
        print(" ");
        switch (mlmeConfirm->Status) {
            case LORAMAC_EVENT_INFO_STATUS_OK:
                print("OK");
                break;
            default:
                print("NO_RESPONSE");
                break;
        }
        AtPrintTrailer();

        appAtMlmeHandle &= ~(APP_AT_MLME_HANDLE_PNGSLINFO);
    }
}

void AppProcMlmeBconAcqReqConfirm(MlmeConfirm_t *mlmeConfirm)
{
    AtPrintCmdHeader("+BCONACQ");
    print(" ");
    switch (mlmeConfirm->Status) {
        case LORAMAC_EVENT_INFO_STATUS_OK:
            print("OK");
            break;
        default:
            print("BEACON_NOT_FOUND");
            break;
    }
    AtPrintTrailer();
}

void AppProcMlmeBconTimReqConfirm(MlmeConfirm_t *mlmeConfirm)
{
   if ((appAtMlmeHandle & APP_AT_MLME_HANDLE_BCONTIM) == APP_AT_MLME_HANDLE_BCONTIM) {
        AtPrintCmdHeader("+BCONTIM");
        print(" ");
        switch (mlmeConfirm->Status) {
           case LORAMAC_EVENT_INFO_STATUS_OK:
               print("OK");
               break;
           default:
               print("NO_RESPONSE");
               break;
        }
        AtPrintTrailer();

        appAtMlmeHandle &= ~(APP_AT_MLME_HANDLE_BCONTIM);
   }
}

void AppMlmeIndicationBeaconLock(MlmeIndication_t *mlmeIndication)
{
    uint8_t  rssiMode;
    uint8_t  i;

    switch (mlmeIndication->Status) {
        case LORAMAC_EVENT_INFO_STATUS_BEACON_LOCKED:
            rssiMode = AppLoraWanGetRssi();

            AtPrintCmdHeader("+BCONRCVD");
            print(" ");
            print_dec( mlmeIndication->BeaconInfo.Time.Seconds, 10, '\0' );
            print_dec( mlmeIndication->BeaconInfo.Time.SubSeconds, 3, '0' );
            if(mlmeIndication->BeaconInfo.isValidGwSpecific == true){
                print(",");
                print_hex(mlmeIndication->BeaconInfo.GwSpecific.InfoDesc, 2);
                print(",");
                for(i = 0; i < APP_BEACON_GWSPECIFIC_INFO_SIZE ; i++){
                    print_hex(mlmeIndication->BeaconInfo.GwSpecific.Info[i], 2);
                }
            }
            AtPrintTrailer();

            if(rssiMode == APP_LORAWAN_RSSI_ON){
                AtPrintCmdHeader("+RSSI");
                print(" ");
                print_dec(mlmeIndication->BeaconInfo.Rssi, 5, '\0');
                print(",");
                print_dec(mlmeIndication->BeaconInfo.Snr, 3, '\0');
                AtPrintTrailer();
            }
            break;
        case LORAMAC_EVENT_INFO_STATUS_BEACON_LOST:
            AtPrintCmdHeader("+BCONNORCVD");
            AtPrintTrailer();
            break;
        default:
            break;
    }
}

void AppMlmeIndicationBeaconLost(MlmeIndication_t *mlmeIndication)
{
    AtPrintCmdHeader("+BCONLOST");
    AtPrintTrailer();
}
#endif

#if APP_TEST_LOWPOWER
void AppUserSw1InterruptHandler(external_irq_callback_args_t *p_args)
{
    AppEnableAtCmdInput = true;
}
#endif


bool AppBoardIsLowPowerAllowed( void )
{
    /* return true if users want MCU to low power. */
    bool retLowPwrAllowed = true;
    
#if defined(FUOTA_ENABLED)
    if (AppFuotaIsLowPowerAllowed() == false)
    {
        retLowPwrAllowed = false;
    }
#endif

    /* at-command (UART) */
    if (AtGetStateIsCommandReceived() == true)
    {
        retLowPwrAllowed = false;
    }

#if APP_TEST_LOWPOWER
    if (AppEnableAtCmdInput)
    {
        retLowPwrAllowed = false;  // disallowed MCU to transit to low power mode
    }
#endif

    return(retLowPwrAllowed);
}

/**
 * Main application entry point.
 */
int app_main( void )
{
#if defined(FUOTA_ENABLED)
#endif
    // Target board initialization
    BoardInitMcu( );

    // Application Initialization (AT command, LoRaWAN, ...)
    AppInit();

#if APP_TEST_LOWPOWER
    R_ICU_ExternalIrqOpen(&g_external_irq_user_sw1_ctrl, &g_external_irq_user_sw1_cfg);
    R_ICU_ExternalIrqEnable(&g_external_irq_user_sw1_ctrl);
#endif

#if defined(FUOTA_ENABLED)
    AppFuotaInit();
#endif
#ifdef APP_COMPLIANCE
    AppCompliacneInit();
#endif

    while (1)
    {
        if (AtNotifyCommandReceived())
        {
#if APP_TEST_LOWPOWER
            BoardDisableAllIrq();
            AppEnableAtCmdInput = false;
            BoardEnableAllIrq();
#endif
            AtCmdParse();
        }

        // process event/iterrupt from LoRaWAN stack/radio
        LoRaMacProcess( );
#if defined(FUOTA_ENABLED)
        AppFuotaProcess();
#endif

#ifdef APP_COMPLIANCE
        // Processes the LoRaMac events
        AppComplianceProcess( );
#endif

        // Check whether MCU can be set to low power mode
        if( BoardIsLowPowerAllowed() )
        {
            LoRaMacSetLowPower();
        }
    }
}
