/*
    (C) 2017 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

/*
 * lorawan_proc.c
 *
 *  Created on: 2017/07/21
 *      Author:
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "lora_sample.h"
#include "lorawan_proc.h"


/* prototypes       */
void AppLoraWanMcpsConfirm( McpsConfirm_t *mcpsConfirm );
void AppLoraWanMcpsIndication( McpsIndication_t *mcpsIndication );
void AppLoraWanMlmeConfirm( MlmeConfirm_t *mlmeConfirm );
void AppLoraWanMlmeIndication( MlmeIndication_t *mlmeIndication );

static void AppLoraWanNvmDataMgmtLoad(void);
static uint32_t AppLoraWanNvmDataMgmtGetSaveFlag(void);

/* global variables */
/* data rate when ADR is on */
static uint8_t appLoRaWanAdrOnDr;

/* functions        */
void AppLoraWanMcpsConfirm( McpsConfirm_t *mcpsConfirm )
{
    AppMcpsConfirmHandler(mcpsConfirm);
}

void AppLoraWanMcpsIndication( McpsIndication_t *mcpsIndication )
{
    AppMcpsIndicationHandler(mcpsIndication);
}

void AppLoraWanMlmeConfirm( MlmeConfirm_t *mlmeConfirm )
{
    AppMlmeConfirmHandler(mlmeConfirm);
}

void AppLoraWanMlmeIndication( MlmeIndication_t *mlmeIndication )
{
    AppMlmeIndicationHandler(mlmeIndication);
}

uint8_t AppLoraWanGetBatteryLevel( void )
{
    return( BoardGetBatteryLevel() );
}

void AppLoraWanNvmContextChange( uint32_t notifyMibFlags )
{
    AppLoraWanNvmDataMgmtSave(notifyMibFlags);
}

void AppLoraWanMacProcessNotify( void )
{

}

void AppLoraWanMacErrorNotify( LoRaMacErrorNotificationStatus_t status )
{
}

/*!
 * initialize LoRaWAN stack and set parameters
 */
LoRaMacStatus_t AppLoraWanInit(LoRaMacPrimitives_t *primitives, LoRaMacCallback_t *callbacks)
{
    LoRaMacStatus_t status;
    static LoRaMacPrimitives_t appLoRaMacPrimitives;    // static: necessary to keep pointers to primitives
    static LoRaMacCallback_t macCallbacks;              // static: necessary to keep pointers to callbacks


    /* initialize mac   */
    if(primitives == NULL)  // set default primitive callback functions
    {
         appLoRaMacPrimitives.MacMcpsConfirm = AppLoraWanMcpsConfirm;
         appLoRaMacPrimitives.MacMcpsIndication = AppLoraWanMcpsIndication;
         appLoRaMacPrimitives.MacMlmeConfirm = AppLoraWanMlmeConfirm;
         appLoRaMacPrimitives.MacMlmeIndication = AppLoraWanMlmeIndication;
    }
    else                    // set specified primitive callback functions
    {
        appLoRaMacPrimitives.MacMcpsConfirm = primitives->MacMcpsConfirm;
        appLoRaMacPrimitives.MacMcpsIndication = primitives->MacMcpsIndication;
        appLoRaMacPrimitives.MacMlmeConfirm = primitives->MacMlmeConfirm;
        appLoRaMacPrimitives.MacMlmeIndication = primitives->MacMlmeIndication;
    }

    if(callbacks == NULL)   // set default other callback functions
    {
        macCallbacks.GetBatteryLevel = AppLoraWanGetBatteryLevel;
        macCallbacks.GetTemperatureLevel = NULL;
        macCallbacks.NvmContextChange = AppLoraWanNvmContextChange;
        macCallbacks.MacProcessNotify = AppLoraWanMacProcessNotify;
        macCallbacks.MacErrorNotify = AppLoraWanMacErrorNotify;
    }
    else                    // set specified callback functions
    {
         macCallbacks.GetBatteryLevel = callbacks->GetBatteryLevel;
         macCallbacks.GetTemperatureLevel = NULL;
         macCallbacks.NvmContextChange = callbacks->NvmContextChange;
         macCallbacks.MacProcessNotify = callbacks->MacProcessNotify;
         macCallbacks.MacErrorNotify = callbacks->MacErrorNotify;
     }

    status = LoRaMacInitialization(&appLoRaMacPrimitives, &macCallbacks, appLoraWanSettings.region);

    if (status == LORAMAC_STATUS_OK) {
        // get initial parameters
        appLoRaWanAdrOnDr = AppLoraWanGetDR();

        // parameters required for ABP mode
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
        AppLoraWanSetDevAddr(appLoraWanSettings.devAddr);       /* DevAddr  */
        AppLoraWanSetNwkSKey(appLoraWanSettings.nwkSKey);       /* NwkSKey  */
        AppLoraWanSetAppSKey(appLoraWanSettings.appSKey);       /* AppSKey  */
        AppLoraWanSetNetID(appLoraWanSettings.netID);           /* NetID    */
#endif

        // parameters required for both ABP/OTAA
        AppLoraWanSetDCycle(appLoraWanSettings.dCycle);             /* Duty cycle */
        AppLoraWanSetDR(appLoraWanSettings.dr);                     /* Data rate in case ADR is disabled */
        AppLoraWanSetAdr(appLoraWanSettings.adr);                   /* ADR      */
        AppLoraWanSetPublicNetwork(appLoraWanSettings.publicNetwork); /* Public Network */
        AppLoraWanSetDeviceClass(appLoraWanSettings.class);         /* class    */
        AppLoraWanSetCertFPortOn(appLoraWanSettings.certFportOn);   /* Certification FPort On/Off */
        AppLoraWanSetSystemMaxRxError( APP_SYSTEM_MAX_RX_ERROR );   /* System Max Rx Error */
#ifdef LORAMAC_CLASSB_ENABLED
        AppLoraWanSetPeriodicity(appLoraWanSettings.periodicity);   /* Ping slot periodicity*/
#endif
#if (LORAMAC_MAX_MC_CTX > 0)
        AppLoraWanSetGenAppKey(appLoraWanSettings.genAppKey);       /* GenAppKey */
#endif
        if(appLoraWanSettings.channelsDefaultMaskEntries) {
            AppLoraWanSetChannelsDefaultMask(appLoraWanSettings.channelsDefaultMask,
                                             appLoraWanSettings.channelsDefaultMaskEntries);
        }

        // restore NVM persistent values
        AppLoraWanNvmDataMgmtRestore();

        LoRaMacStart();
    }

    return status;
}

/*!
 * @fn
 * send join request message
 * @return  result code
 */
LoRaMacStatus_t AppLoraWanJoinReq(TimerTime_t *pDutyCycleWaitTime)
{
    MibRequestConfirm_t mibReq;
    MlmeReq_t mlmeReq;
    LoRaMacStatus_t status;
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    int16_t datarate;

    // get default datarate for region
    getPhy.Attribute = PHY_DEF_TX_DR;
    phyParam = RegionGetPhyParam(appLoraWanSettings.region, &getPhy );
    datarate = phyParam.Value;

    mibReq.Type = MIB_DEV_EUI;
    mibReq.Param.DevEui = appLoraWanSettings.devEUI;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_APP_EUI;
    mibReq.Param.AppEui = appLoraWanSettings.appEUI;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_APP_KEY;
    mibReq.Param.AppKey = appLoraWanSettings.appKey;
    LoRaMacMibSetRequestConfirm( &mibReq );

    /* set mlme request */
    mlmeReq.Type = MLME_JOIN;
    mlmeReq.Req.Join.Datarate = datarate;

    status = LoRaMacMlmeRequest(&mlmeReq);

    if( pDutyCycleWaitTime )
    {
        if( status == LORAMAC_STATUS_DUTYCYCLE_RESTRICTED )
        {
            *pDutyCycleWaitTime = mlmeReq.ReqReturn.DutyCycleWaitTime;
        }
        else
        {
            *pDutyCycleWaitTime = 0;
        }
    }

    return status;
}

/*!
 * @fn
 * send data message
 * @return  result code
 */
LoRaMacStatus_t AppLoraWanSendData(uint8_t *data, uint8_t len, uint8_t fport, Mcps_t type, TimerTime_t *pDutyCycleWaitTime)
{
    McpsReq_t mcpsReq;
    LoRaMacTxInfo_t txInfo;
    LoRaMacStatus_t status;

    // init
    mcpsReq.ReqReturn.DutyCycleWaitTime = 0;

    status = LoRaMacQueryTxPossible(len, &txInfo);
    if (status == LORAMAC_STATUS_LENGTH_ERROR)
    {
        if (txInfo.CurrentPossiblePayloadSize != txInfo.MaxPossibleApplicationDataSize)
        {
            // AppData cannot be stored in the frame due to MAC command(s).
            // Try to send only MAC command(s).
            // Return status of LoRaMacMcpsRequest() will be 'LORAMAC_STATUS_SKIPPED_APP_DATA'.
            status = LORAMAC_STATUS_OK;
        }
    }

    if (status == LORAMAC_STATUS_OK) {
        mcpsReq.Type = type;
        if (type == MCPS_UNCONFIRMED) {
            mcpsReq.Req.Unconfirmed.fPort = fport;
            mcpsReq.Req.Unconfirmed.fBuffer = data;
            mcpsReq.Req.Unconfirmed.fBufferSize = len;
            mcpsReq.Req.Unconfirmed.Datarate = appLoraWanSettings.dr;
        }
        else {  // if (type == MCPS_CONFIRMED)
            mcpsReq.Req.Confirmed.fPort = fport;
            mcpsReq.Req.Confirmed.fBuffer = data;
            mcpsReq.Req.Confirmed.fBufferSize = len;
#if (LORAMAC_VERSION < LORAWAN_VERSION_1_0_4)  // LW1.0.3
            mcpsReq.Req.Confirmed.NbTrials = APP_LORAWAN_DEFAULT_NBTRIALS;      // Maximum nuumber of Tx
#endif
            mcpsReq.Req.Confirmed.Datarate = appLoraWanSettings.dr;
        }

        status = LoRaMacMcpsRequest(&mcpsReq);
    }

    if( pDutyCycleWaitTime )
    {
        if( status == LORAMAC_STATUS_DUTYCYCLE_RESTRICTED )
        {
            *pDutyCycleWaitTime = mcpsReq.ReqReturn.DutyCycleWaitTime;
        }
        else
        {
            *pDutyCycleWaitTime = 0;
        }
    }

    return status;
}

/*!
 * @fn
 * send LinkCheckReq command
 * @return  result code
 */
LoRaMacStatus_t AppLoraWanLinkCheck(void)
{
    MlmeReq_t mlmeReq;
    LoRaMacStatus_t status;

    /* set mlme request */
    mlmeReq.Type = MLME_LINK_CHECK;
    status = LoRaMacMlmeRequest(&mlmeReq);

    if (status == LORAMAC_STATUS_OK) {
        status = AppLoraWanSendData( NULL, 0,   // empty frame payload
                                     0,         // fPort is ignored in case frame payload is empty
                                     MCPS_UNCONFIRMED, NULL );
    }

    return status;
}

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * set network joined status
 * @return  result code
 */
LoRaMacStatus_t AppLoraWanSetNetworkJoined(bool isNetworkJoined)
{
    MibRequestConfirm_t mibReq;
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;

    // Tell the MAC layer which network server version we are connecting to.
    mibReq.Type = MIB_ABP_LORAWAN_VERSION;
    mibReq.Param.AbpLrWanVersion.Value = LORAMAC_VERSION;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_NET_ID;
    mibReq.Param.NetID = appLoraWanSettings.netID;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_DEV_ADDR;
    mibReq.Param.DevAddr = appLoraWanSettings.devAddr;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_NWK_SKEY;
    mibReq.Param.NwkSKey = appLoraWanSettings.nwkSKey;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_APP_SKEY;
    mibReq.Param.AppSKey = appLoraWanSettings.appSKey;
    LoRaMacMibSetRequestConfirm( &mibReq );

    mibReq.Type = MIB_NETWORK_ACTIVATION;
    mibReq.Param.NetworkActivation = ACTIVATION_TYPE_ABP;
    LoRaMacMibSetRequestConfirm( &mibReq );

    return status;
}
#endif

/*!
 * @fn
 * set device class
 * @param class [I] device class : CLASS_A/CLASS_B/CLASS_C
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetDeviceClass(DeviceClass_t class)
{
    MibRequestConfirm_t mibReq;
    LoRaMacStatus_t status;

    if ((class == CLASS_A) || (class == CLASS_B) || (class == CLASS_C)) {
        mibReq.Type = MIB_DEVICE_CLASS;
        mibReq.Param.Class = class;
        status = LoRaMacMibSetRequestConfirm(&mibReq);
    }
    else {
        status = LORAMAC_STATUS_PARAMETER_INVALID;
    }

    if (status == LORAMAC_STATUS_OK) {
        appLoraWanSettings.class = class;
    }

    return status;
}

/*!
 * @fn
 * get device class
 * @return device class: CLASS_A / CLASS_C
 */
DeviceClass_t AppLoraWanGetDeviceClass(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_DEVICE_CLASS;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.Class;
}

/*!
 * @fn
 * set AppKey
 * @param appkey [I] AppKey in 16 bytes
 */
void AppLoraWanSetAppKey(uint8_t *appKey)
{
    memcpy1(appLoraWanSettings.appKey, appKey, APP_LORAWAN_LEN_APPKEY);
}
/*!
 * @fn
 * get AppKey
 * @param appkey [O] AppKey in 16 bytes
 */
void AppLoraWanGetAppkey(uint8_t *appKey)
{
    memcpy1(appKey, appLoraWanSettings.appKey, APP_LORAWAN_LEN_APPKEY);
}

/*!
 * @fn
 * set DevEUI
 * @param deveui [I] DevEUI in 16 bytes
 */
void AppLoraWanSetDevEUI(uint8_t *devEUI)
{
    memcpy1(appLoraWanSettings.devEUI, devEUI, APP_LORAWAN_LEN_DEVEUI);
}

/*!
 * @fn
 * get DevEUI
 * @param deveui [O] DevEUI in 16 bytes
 */
void AppLoraWanGetDevEUI(uint8_t *devEUI)
{
    memcpy1(devEUI, appLoraWanSettings.devEUI, APP_LORAWAN_LEN_DEVEUI);
}

/*!
 * @fn
 * set AppEUI
 * @param appeui [I] AppEUI in 8 bytes
 */
void AppLoraWanSetAppEUI(uint8_t *appEUI)
{
    memcpy1(appLoraWanSettings.appEUI, appEUI, APP_LORAWAN_LEN_APPEUI);
}

/*!
 * @fn
 * get AppEUI
 * @param appeui [O] AppEUI in 8 bytes
 */
void AppLoraWanGetAppEUI(uint8_t *appEUI)
{
    memcpy1(appEUI, appLoraWanSettings.appEUI, APP_LORAWAN_LEN_APPEUI);
}

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * set AppSKey
 * @param appskey [I] AppSKey in 16 bytes
 */
LoRaMacStatus_t AppLoraWanSetAppSKey(uint8_t *appSKey)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_APP_SKEY;
    mibReq.Param.AppSKey = appSKey;
    status = LoRaMacMibSetRequestConfirm( &mibReq );

    if (status == LORAMAC_STATUS_OK) {
        memcpy1(appLoraWanSettings.appSKey, appSKey, APP_LORAWAN_LEN_APPSKEY);
    }

    return status;
}

/*!
 * @fn
 * get AppSKey
 * @param appskey [O] AppSKey in 16 bytes
 */
void AppLoraWanGetAppSKey(uint8_t *appSKey)
{
    memcpy1(appSKey, appLoraWanSettings.appSKey, APP_LORAWAN_LEN_APPSKEY);
}

/*!
 * @fn
 * set NwkSKey
 * @param nwkskey [O] NwkSKey in 16 bytes
 */
LoRaMacStatus_t AppLoraWanSetNwkSKey(uint8_t *nwkSKey)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_NWK_SKEY;
    mibReq.Param.NwkSKey = nwkSKey;
    status = LoRaMacMibSetRequestConfirm( &mibReq );

    if (status == LORAMAC_STATUS_OK) {
        memcpy1(appLoraWanSettings.nwkSKey, nwkSKey, APP_LORAWAN_LEN_NWKSKEY);
    }

    return status;
}

/*!
 * @fn
 * get NwkSKey
 * @param nwkskey [O] NwkSKey in 16 bytes
 */
void AppLoraWanGetNwkSKey(uint8_t *nwkSKey)
{
    memcpy1(nwkSKey, appLoraWanSettings.nwkSKey, APP_LORAWAN_LEN_NWKSKEY);
}
#endif

#if (LORAMAC_MAX_MC_CTX > 0)
/*!
 * @fn
 * set GenAppKey
 * @param genAppSKey [I] GenAppKey in 16 bytes
 */
LoRaMacStatus_t AppLoraWanSetGenAppKey(uint8_t *genAppSKey)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_GEN_APP_KEY;
    mibReq.Param.GenAppKey = genAppSKey;
    status = LoRaMacMibSetRequestConfirm( &mibReq );

    if (status == LORAMAC_STATUS_OK) {
        memcpy1(appLoraWanSettings.genAppKey, genAppSKey, APP_LORAWAN_LEN_GENAPPKEY);
    }

    return status;
}

/*!
 * @fn
 * get GenAppKey
 * @param genAppSKey [O] GenAppKey in 16 bytes
 */
void AppLoraWanGetGenAppKey(uint8_t *genAppSKey)
{
    memcpy1(genAppSKey, appLoraWanSettings.genAppKey, APP_LORAWAN_LEN_GENAPPKEY);
}
#endif

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * set DevAddr
 * @param devaddr [I] DevAddr in 4 bytes
 */
LoRaMacStatus_t AppLoraWanSetDevAddr(uint32_t devAddr)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_DEV_ADDR;
    mibReq.Param.DevAddr = devAddr;
    status = LoRaMacMibSetRequestConfirm( &mibReq );

    if (status == LORAMAC_STATUS_OK) {
        appLoraWanSettings.devAddr = devAddr;
    }

    return status;
}
#endif

/*!
 * @fn
 * get DevAddr
 * @return DevAddr in 4 bytes
 */
uint32_t AppLoraWanGetDevAddr(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_DEV_ADDR;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.DevAddr;
}

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * set NetID
 * @param netID [I] NetID in 4 bytes
 */
LoRaMacStatus_t AppLoraWanSetNetID(uint32_t netID)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_NET_ID;
    mibReq.Param.NetID = netID;
    status = LoRaMacMibSetRequestConfirm( &mibReq );

    if (status == LORAMAC_STATUS_OK) {
        appLoraWanSettings.netID = netID;
    }

    return status;
}
#endif

/*!
 * @fn
 * get NetID
 * @retur NetID in 4 bytes
 */
uint32_t AppLoraWanGetNetID(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_NET_ID;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.NetID;
}

/*!
 * @fn
 * set public network mode
 * @param enabled [I] true: public network enabled, false: public netowork disabled
 */
LoRaMacStatus_t AppLoraWanSetPublicNetwork(bool enabled)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_PUBLIC_NETWORK;           /* Public network setting   */
    mibReq.Param.EnablePublicNetwork = enabled;    /* enable public network    */
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    return status;
}

/*!
 * @fn
 * get public network mode
 * @return public network mode: true: public netowrk enabled, false: public netowork disabled
 */
bool AppLoraWanGetPublicNetwork(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_PUBLIC_NETWORK;           /* Public network setting   */
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.EnablePublicNetwork;
}

/*!
 * @fn
 * set region
 * @param region [I] region: LORAMAC_REGION_xxxxx
 */
LoRaMacStatus_t AppLoraWanSetRegion(LoRaMacRegion_t region)
{
    LoRaMacStatus_t status;

    switch (region) {
#if defined(REGION_EU868)
        case LORAMAC_REGION_EU868:
#endif
#if defined(REGION_US915)
        case LORAMAC_REGION_US915:
#endif
#if defined(REGION_AS923)
        case LORAMAC_REGION_AS923:
  #if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
        case LORAMAC_REGION_AS923_2:
        case LORAMAC_REGION_AS923_3:
        case LORAMAC_REGION_AS923_4:
  #endif
        case LORAMAC_REGION_AS923_JPN:
#endif
#if defined(REGION_CN779)
        case LORAMAC_REGION_CN779:
#endif
#if defined(REGION_EU433)
        case LORAMAC_REGION_EU433:
#endif
#if defined(REGION_AU915)
        case LORAMAC_REGION_AU915:
#endif
#if defined(REGION_CN470)
        case LORAMAC_REGION_CN470:
#endif
#if defined(REGION_KR920)
        case LORAMAC_REGION_KR920:
#endif
#if defined(REGION_IN865)
        case LORAMAC_REGION_IN865:
#endif
#if defined(REGION_RU864)
        case LORAMAC_REGION_RU864:
#endif
            status = LORAMAC_STATUS_OK;
            break;
        default:
            status = LORAMAC_STATUS_REGION_NOT_SUPPORTED;
            break;
    }

    if (status == LORAMAC_STATUS_OK) {

        // disable channels default mask setting if region is changed
        if(appLoraWanSettings.region != region) {
            appLoraWanSettings.channelsDefaultMaskEntries = 0;

            AppLoraWanNvmDataMgmtSetRestoreMode(APP_LORAWAN_NVMDATA_RESTORE_REGION_CHG);
        }

        appLoraWanSettings.region = region;

        // initialize LoRaWAN stack so that parameters related to region can be set
        status = AppLoraWanInit(NULL, NULL);
    }

    return status;
}

/*!
 * @fn
 * get current region setting
 * @return current region setting: LORAMAC_REGION_xxxxx
 */
LoRaMacRegion_t AppLoraWanGetRegion(void)
{
    return appLoraWanSettings.region;
}

/*!
 * @fn
 * set activation mode
 * @param actMode [I] activation mode: APP_LORAWAN_ACTMODE_ABP / APP_LORAWAN_ACTMODE_OTAA
 * @return status code : LORAMAC_STATUS_OK / LORAMAC_STATUS_PARAMETER_INVALID
 */
LoRaMacStatus_t AppLoraWanSetActMode(uint8_t actMode)
{
    LoRaMacStatus_t status;
    uint8_t actModePrev;

    actModePrev = appLoraWanSettings.actMode;

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if ((actMode == APP_LORAWAN_ACTMODE_ABP)
        || (actMode == APP_LORAWAN_ACTMODE_OTAA))
#else
    if (actMode == APP_LORAWAN_ACTMODE_OTAA)
#endif
    {
        appLoraWanSettings.actMode = actMode;
        status = LORAMAC_STATUS_OK;
    }
    else {
        status = LORAMAC_STATUS_PARAMETER_INVALID;
    }

    // initialize LoRaWAN stack
    if ((status == LORAMAC_STATUS_OK) && (actModePrev != actMode)) {
        AppLoraWanNvmDataMgmtSetRestoreMode(APP_LORAWAN_NVMDATA_RESTORE_ACTMODE_CHG);
        status = AppLoraWanInit(NULL, NULL);
    }

    return status;
}

/*!
 * @fn
 * get activation mode
 * @return activation mode: APP_LORAWAN_ACTMODE_ABP: ABP / APP_LORAWAN_ACTMODE_OTAA: OTAA
 */
uint8_t AppLoraWanGetActMode(void)
{
    return appLoraWanSettings.actMode;
}

/*!
 * @fn
 * set ADR (adaptive data rate)
 * @param enabled [I] ADR setting: true: ADR enabled, false: ADR disabled
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetAdr(bool enabled)
{
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;
    MibRequestConfirm_t mibReq;

    if (AppLoraWanGetAdr() == enabled)
    {
        return LORAMAC_STATUS_OK;
    }

    if (enabled == true)
    {
        // Set data rate before ADR On
        mibReq.Type = MIB_CHANNELS_DATARATE;
        mibReq.Param.ChannelsDatarate = appLoRaWanAdrOnDr;
        status = LoRaMacMibSetRequestConfirm(&mibReq);
    }

    if (status == LORAMAC_STATUS_OK)
    {
        mibReq.Type = MIB_ADR;
        mibReq.Param.AdrEnable = enabled;
        status = LoRaMacMibSetRequestConfirm(&mibReq);
    }

    if (status == LORAMAC_STATUS_OK) {
        appLoraWanSettings.adr = enabled;

        if (enabled == false)
        {
            // Get current data rate
            mibReq.Type = MIB_CHANNELS_DATARATE;
            LoRaMacMibGetRequestConfirm(&mibReq);
            appLoRaWanAdrOnDr = mibReq.Param.ChannelsDatarate;
        }
    }

    return status;
}

/*!
 * @fn
 * get current ADR setting
 * @return current ADR setting: true: ADR enabled, false: ADR disabled
 */
bool AppLoraWanGetAdr(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_ADR;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.AdrEnable;
}

#ifdef LORAMAC_CLASSB_ENABLED
/*!
 * @fn
 * set Periodicity
 */
LoRaMacStatus_t AppLoraWanSetPeriodicity(uint8_t periodicity)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_PING_SLOT_PERIODICITY;
    mibReq.Param.PingSlotPeriodicity = periodicity;
    status = LoRaMacMibSetRequestConfirm( &mibReq );

    if(status == LORAMAC_STATUS_OK) {
        appLoraWanSettings.periodicity = periodicity;
    }

    return status;
}

 /*!
 * @fn
 * get Periodicity setting
 */
uint8_t AppLoraWanGetPeriodicity(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_PING_SLOT_PERIODICITY;
    LoRaMacMibGetRequestConfirm( &mibGet );

    return mibGet.Param.PingSlotPeriodicity;
}
#endif

/*!
 * @fn
 * set message type for tx data message
 * @param mtype message type: MCPS_CONFIRMED / MCPS_UNCONFIRMED
 */
LoRaMacStatus_t AppLoraWanSetMessageType(Mcps_t mtype)
{
    LoRaMacStatus_t status;

    if ((mtype == MCPS_CONFIRMED) || (mtype == MCPS_UNCONFIRMED)) {
        appLoraWanSettings.mtype = mtype;
        status = LORAMAC_STATUS_OK;
    }
    else {
        status = LORAMAC_STATUS_PARAMETER_INVALID;
    }

    return status;
}

/*!
 * @fn
 * get message type for tx data message
 * @return message type: MCPS_CONFIRMED / MCPS_UNCONFIRMED
 */
Mcps_t AppLoraWanGetMessageType(void)
{
    return appLoraWanSettings.mtype;
}

/*!
 * @fn
 * set FPort value
 * @param fport [I] FPort value
 */
LoRaMacStatus_t AppLoraWanSetFPort(uint8_t fPort)
{
    LoRaMacStatus_t status;

    if ((fPort >= 1) && (fPort <= 224)) {
        appLoraWanSettings.fPort = fPort;
        status = LORAMAC_STATUS_OK;
    }
    else {
        status = LORAMAC_STATUS_PARAMETER_INVALID;
    }

    return status;
}

/*!
 * @fn
 * get Fport value
 * @return Fport value
 */
uint8_t AppLoraWanGetFPort(void)
{
    return appLoraWanSettings.fPort;
}

/*!
 * @fn
 * set Duty Cycle mode
 * @param ebabled [I] 1: duty cycle enabled, 0: duty cycle disabled
 */
LoRaMacStatus_t AppLoraWanSetDCycle(uint8_t enabled)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_DUTY_CYCLE;
    mibReq.Param.DCycleEnabled = enabled;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanSettings.dCycle = enabled;
    }

    return status;
}

/*!
 * @fn
 * get Duty Cycle mode
 * @return Duty Cycle mode : 1: duty cycle enabled, 0: duty cycle disabled
 */
uint8_t AppLoraWanGetDCycle(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_DUTY_CYCLE;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.DCycleEnabled;
}

/*!
 * @fn
 * set RSSI display setting
 * @param rssiMode [I] RSSI display setting: 1: RSSI displayed, 0: RSSI not displayed
 */
void AppLoraWanSetRssi(uint8_t rssiMode)
{
    appLoraWanSettings.rssi = rssiMode;
}

/*!
 * @fn
 * set RSSI display setting
 * @param rssiMode [I] RSSI display setting: 1: RSSI displayed, 0: RSSI not displayed
 */
uint8_t AppLoraWanGetRssi(void)
{
    return appLoraWanSettings.rssi;
}

/*!
 * @fn
 * set delay time for RX window 1 in msec
 * @param delay [I] delay time for RX window 1 in msec. delay time for RX widow 2 is set to (delay + 1000).
 */
LoRaMacStatus_t AppLoraWanSetRx1Delay(uint16_t delay)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_RECEIVE_DELAY_1;
    mibReq.Param.ReceiveDelay1 = delay;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    if (status ==  LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
        appLoraWanNvmData.rx1Delay = delay;
#endif

        mibReq.Type = MIB_RECEIVE_DELAY_2;
        mibReq.Param.ReceiveDelay2 = delay + 1000;
        status = LoRaMacMibSetRequestConfirm(&mibReq);
    }

    return status;
}

/*!
 * @fn
 * get delay time for RX window 1 in msec
 * @return delay time for RX window 1
 */
uint16_t AppLoraWanGetRx1Delay(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_RECEIVE_DELAY_1;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.ReceiveDelay1;
}

/*!
 * @fn
 * set data rate used in case ADR is disabled
 * @param dr [I] data rate
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetDR(uint8_t dr)
{
    MibRequestConfirm_t mibReq;
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;

    if (appLoraWanSettings.adr == false)  // ADR Off
    {
        mibReq.Type = MIB_CHANNELS_DATARATE;
        mibReq.Param.ChannelsDatarate = dr;
        status = LoRaMacMibSetRequestConfirm(&mibReq);

        if (status == LORAMAC_STATUS_OK)
        {
            appLoraWanSettings.dr = dr;
        }
    }
    else
    {
        // skip data rate setting if ADR is ON.
    }

    return status;
}

/*!
 * @fn
 * get data rate used in case ADR is disabled
 * @return data rate value
 */
uint8_t AppLoraWanGetDR(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_CHANNELS_DATARATE;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.ChannelsDatarate;
}

/*!
 * @fn
 * set data rate used in case ADR is enabled (to restore settings from NVM on initialization)
 * @param dr [I] data rate
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetAdrOnDR(uint8_t dr)
{
    MibRequestConfirm_t mibReq;
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;

    if (appLoraWanSettings.adr == true)  // ADR On
    {
        mibReq.Type = MIB_CHANNELS_DATARATE;
        mibReq.Param.ChannelsDatarate = dr;
        status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
        if (status == LORAMAC_STATUS_OK)
        {
            appLoraWanNvmData.dataRate = dr;
        }
#endif
    }

    return status;
}

/*!
 * @fn
 * add a new channel
 * @param chID [I] new channel ID
 * @param freq [I] frequency in Hz for the ID
 * @return status code
 */
LoRaMacStatus_t AppLoraWanAddCh(uint8_t chID, uint32_t freq)
{
    LoRaMacStatus_t status;
    ChannelParams_t chParams;
    MibRequestConfirm_t mibGet;

    /* frequency    */
    chParams.Frequency = freq;
#ifdef LORAMAC_SET_CH_RX1FREQ_ENABLED
    chParams.Rx1Frequency = 0;
#endif

    /* max tx dr    */
    mibGet.Type = MIB_MAX_TX_DR;
    LoRaMacMibGetRequestConfirm(&mibGet);
    chParams.DrRange.Fields.Max = mibGet.Param.MaxTxDr;

    /* min tx dr    */
    mibGet.Type = MIB_MIN_TX_DR;
    LoRaMacMibGetRequestConfirm(&mibGet);
    chParams.DrRange.Fields.Min = mibGet.Param.MinTxDr;

    /* add new channel  */
    status = LoRaMacChannelAdd(chID, chParams);

    return status;
}

/*!
 * @fn
 * check low power transition condition
 * @return low power entry permission (true = low power allowed)
 */
bool AppLoraWanLowPowerAllowed( void )
{
    bool ret = false;

    if( appLoraWanSettings.class == CLASS_A ) {
        ret = true;
    }

    return ret;
}

/*!
 * @fn
 * send DeviceTimeReq command
 */
LoRaMacStatus_t AppLoraWanDeviceTime( void )
{
    MlmeReq_t mlmeReq;
    LoRaMacStatus_t status;

    /* set mlme request*/
    mlmeReq.Type = MLME_DEVICE_TIME;
    status = LoRaMacMlmeRequest(&mlmeReq);

    if(status == LORAMAC_STATUS_OK) {
        status = AppLoraWanSendData( NULL, 0,   // empty frame payload
                                     0,         // fPort is ignored in case frame payload is empty
                                     MCPS_UNCONFIRMED, NULL );
    }

    return status;
}

#ifdef LORAMAC_CLASSB_ENABLED
/*!
 * @fn
 * send BeaconAcquisition command
 */
LoRaMacStatus_t AppLoraWanBeaconAcquisition( void )
{
    MlmeReq_t mlmeReq;
    LoRaMacStatus_t status;

    /* set mlme request */
    mlmeReq.Type = MLME_BEACON_ACQUISITION;
    status = LoRaMacMlmeRequest(&mlmeReq);

    return status;
}

/*!
 * @fn
 * send PingSlotInfoReq command
 */
LoRaMacStatus_t AppLoraWanPingSlotInfo( uint8_t Psltinfo )
{
    MlmeReq_t mlmeReq;
    LoRaMacStatus_t status;

    /* set mlme request */
    mlmeReq.Type = MLME_PING_SLOT_INFO;
    mlmeReq.Req.PingSlotInfo.PingSlot.Fields.Periodicity = Psltinfo;
    mlmeReq.Req.PingSlotInfo.PingSlot.Fields.RFU = 0;
    status = LoRaMacMlmeRequest(&mlmeReq);

    if(status == LORAMAC_STATUS_OK) {
        status = AppLoraWanSendData( NULL, 0,   // empty frame payload
                                     0,         // fPort is ignored in case frame payload is empty
                                     MCPS_UNCONFIRMED, NULL );
    }

    return status;
}

/*!
 * @fn
 * send BeaconTimingReq command
 */
LoRaMacStatus_t AppLoraWanBeaconTiming( void )
{
    MlmeReq_t mlmeReq;
    LoRaMacStatus_t status;

    /* set mlme request */
    mlmeReq.Type = MLME_BEACON_TIMING;
    status = LoRaMacMlmeRequest(&mlmeReq);

    if(status == LORAMAC_STATUS_OK) {
        status = AppLoraWanSendData( NULL, 0,   // empty frame payload
                                     0,         // fPort is ignored in case frame payload is empty
                                     MCPS_UNCONFIRMED, NULL );
    }

    return status;
}
#endif

/*!
 * @fn
 * set System max rx error
 * @param systemMaxRxError : max error in msec
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetSystemMaxRxError( uint8_t systemMaxRxError )
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_SYSTEM_MAX_RX_ERROR;
    mibReq.Param.SystemMaxRxError = (uint32_t)systemMaxRxError;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    return status;
}

/*!
 * @fn
 * set channels default mask
 * @param channelsDefaultMask [I] pointer to channels default mask table
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetChannelsDefaultMask(uint16_t *channelsDefaultMask, uint8_t channelsDefaultMaskEntries)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_CHANNELS_DEFAULT_MASK;
    mibReq.Param.ChannelsDefaultMask = channelsDefaultMask;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    // channels default mask setting (not 0: change default value)
    appLoraWanSettings.channelsDefaultMaskEntries = channelsDefaultMaskEntries;
    memcpy1((uint8_t *)appLoraWanSettings.channelsDefaultMask, (uint8_t *)channelsDefaultMask,
                                                    channelsDefaultMaskEntries * sizeof(uint16_t));

    return status;
}

/*!
 * @fn
 * get channels default mask and number of entries
 * @param channelsDefaultMask [O] pointer to channels default mask table
 * @param channelsDefaultMaskEntries [O] pointer to number of channels default mask table entries
 * @return status code
 */
void AppLoraWanGetChannelsDefaultMask(uint16_t *channelsDefaultMask, uint8_t *channelsDefaultMaskEntries)
{
    MibRequestConfirm_t mibGet;
    uint8_t            entries;

    mibGet.Type = MIB_CHANNELS_DEFAULT_MASK;
    LoRaMacMibGetRequestConfirm(&mibGet);

    entries = AppLoraWanGetChannelsMaskEntries();

    memcpy1((uint8_t *)channelsDefaultMask, (uint8_t *)mibGet.Param.ChannelsDefaultMask,
                entries * sizeof(uint16_t));
    *channelsDefaultMaskEntries = entries;

    return;
}

/*!
 * @fn
 * set channels mask
 * @param channelsMask [I] pointer to channels mask table
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetChannelsMask(uint16_t *p_channelsMask)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_CHANNELS_MASK;
    mibReq.Param.ChannelsMask = p_channelsMask;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    return status;
}

/*!
 * @fn
 * get channels mask and number of entries
 * @param channelsMask [O] pointer to channels mask table
 * @param channelsMaskEntries [O] pointer to number of channels mask table entries
 */
void AppLoraWanGetChannelsMask(uint16_t *p_channelsMask, uint8_t *p_channelsMaskEntries)
{
    MibRequestConfirm_t mibGet;
    uint8_t            entries;

    mibGet.Type = MIB_CHANNELS_MASK;
    LoRaMacMibGetRequestConfirm(&mibGet);

    entries = AppLoraWanGetChannelsMaskEntries();

    memcpy1((uint8_t *)p_channelsMask, (uint8_t *)mibGet.Param.ChannelsMask,
            entries * sizeof(uint16_t));
    *p_channelsMaskEntries = entries;

    return;
}

uint8_t AppLoraWanGetChannelsMaskEntries(void)
{
    uint16_t    entries;

    switch(appLoraWanSettings.region) {
#if defined(REGION_EU868)
        case LORAMAC_REGION_EU868:
            entries = 1;
            break;
#endif
#if defined(REGION_US915)
        case LORAMAC_REGION_US915:
            entries = 6;
            break;
#endif
#if defined(REGION_AS923)
        case LORAMAC_REGION_AS923:
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
        case LORAMAC_REGION_AS923_2:
        case LORAMAC_REGION_AS923_3:
        case LORAMAC_REGION_AS923_4:
#endif
        case LORAMAC_REGION_AS923_JPN:
            entries = 1;
            break;
#endif
//
#if defined(REGION_CN779)
        case LORAMAC_REGION_CN779:
            entries = 1;
            break;
#endif
#if defined(REGION_EU433)
        case LORAMAC_REGION_EU433:
            entries = 1;
            break;
#endif
#if defined(REGION_AU915)
        case LORAMAC_REGION_AU915:
            entries = 6;
            break;
#endif
#if defined(REGION_CN470)
        case LORAMAC_REGION_CN470:
            entries = 6;
            break;
#endif
#if defined(REGION_KR920)
        case LORAMAC_REGION_KR920:
            entries = 1;
            break;
#endif
#if defined(REGION_IN865)
        case LORAMAC_REGION_IN865:
            entries = 1;
            break;
#endif
#if defined(REGION_RU864)
        case LORAMAC_REGION_RU864:
            entries = 1;
            break;
#endif
        default:
            // not supported
            entries = 0;
            break;
    }

    return entries;
}

/*!
 * @fn
 * set Certification fport on/off
 * @param enabled [I] 1: certification fport enabled, 0: certification fport disabled
 */
LoRaMacStatus_t AppLoraWanSetCertFPortOn(uint8_t enabled)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_IS_CERT_FPORT_ON;
    mibReq.Param.IsCertPortOn = enabled;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanSettings.certFportOn = enabled;
    }

    return status;
}

/*!
 * @fn
 * get Certification fport on/off
 * @return enabled : 1: certification fport enabled, 0: certifiation fport disabled
 */
uint8_t AppLoraWanGetCertFPortOn(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_IS_CERT_FPORT_ON;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.IsCertPortOn;
}

/*!
 * @fn
 * set devNonce
 * @param devNonce [I]
 */
LoRaMacStatus_t AppLoraWanSetDevNonce(uint16_t devNonce)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_DEV_NONCE;
    mibReq.Param.devNonce = devNonce;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.devNonce = devNonce;
    }

    return status;
}

/*!
 * @fn
 * get devNonce
 * @return devNonce
 */
uint16_t AppLoraWanGetDevNonce(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_DEV_NONCE;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.devNonce;
}

/*!
 * @fn
 * set appNonce
 * @param appNonce [I]
 */
LoRaMacStatus_t AppLoraWanSetAppNonce(uint32_t appNonce)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_APP_NONCE;
    mibReq.Param.appNonce = appNonce;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.appNonce = appNonce;
    }

    return status;
}

/*!
 * @fn
 * get appNonce
 * @return appNonce
 */
uint32_t AppLoraWanGetAppNonce(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_APP_NONCE;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.appNonce;
}

/*!
 * @fn
 * set nbTrans
 * @param nbTrans [I]
 */
LoRaMacStatus_t AppLoraWanSetNbTrans(uint8_t nbTrans)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_CHANNELS_NB_TRANS;
    mibReq.Param.ChannelsNbTrans = nbTrans;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.nbTrans = nbTrans;
    }
#endif

    return status;
}

/*!
 * @fn
 * get nbTrans
 * @return nbTrans
 */
uint8_t AppLoraWanGetNbTrans(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_CHANNELS_NB_TRANS;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.ChannelsNbTrans;
}

/*!
 * @fn
 * set(add/del) Channel
 * @param startChId [I]
 * @param numChSet [I]
 * @param *p_channels [I]
 */
void AppLoraWanSetChannels(uint8_t startChId, uint8_t numChSet, ChannelParams_t *p_channels)
{
    uint8_t i, chId, minChId, maxChId;

    switch (appLoraWanSettings.region) {
#if defined(REGION_EU868)
        case LORAMAC_REGION_EU868:
            minChId = 3;   // 0,1,2 : default
            maxChId = 15;  // nbChannels = 16
            break;
#endif
#if defined(REGION_AS923)
        case LORAMAC_REGION_AS923:
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
        case LORAMAC_REGION_AS923_2:
        case LORAMAC_REGION_AS923_3:
        case LORAMAC_REGION_AS923_4:
#endif
        case LORAMAC_REGION_AS923_JPN:
            minChId = 2;   // 0,1 : default
            maxChId = 15;  // nbChannels = 16
            break;
#endif
#if defined(REGION_CN779)
        case LORAMAC_REGION_CN779:
            minChId = 3;   // 0,1,2 : default
            maxChId = 15;  // nbChannels = 16
            break;
#endif
#if defined(REGION_EU433)
        case LORAMAC_REGION_EU433:
            minChId = 3;   // 0,1,2 : default
            maxChId = 15;  // nbChannels = 16
            break;
#endif
#if defined(REGION_KR920)
        case LORAMAC_REGION_KR920:
            minChId = 3;   // 0,1,2 : default
            maxChId = 15;  // nbChannels = 16
            break;
#endif
#if defined(REGION_IN865)
        case LORAMAC_REGION_IN865:
            minChId = 3;   // 0,1,2 : default
            maxChId = 15;  // nbChannels = 16
            break;
#endif
#if defined(REGION_RU864)
        case LORAMAC_REGION_RU864:
            minChId = 2;   // 0,1 : default
            maxChId = 7;   // nbChannels = 8
            break;
#endif
#if defined(REGION_US915)
        case LORAMAC_REGION_US915:
#endif
#if defined(REGION_AU915)
        case LORAMAC_REGION_AU915:
#endif
#if defined(REGION_CN470)
        case LORAMAC_REGION_CN470:
#endif
        default:
            // cannot add/del channel(s)
            minChId  = 0;
            maxChId  = 0;
            numChSet = 0;
            break;
    }

    for (i = 0; i < numChSet; i++) {
        chId = startChId + i;
        if (chId > maxChId) {
            break;   // exit for(i) loop
        }

        if (chId >= minChId) {
            if( p_channels->Frequency != 0 ) {
                LoRaMacChannelAdd(chId, *p_channels);
            }
            else {
                LoRaMacChannelRemove(chId);
            }
        }
        p_channels++;  // next channel
    }
}

/*!
 * @fn
 * get channels
 * @param startChId [I]
 * @param maxNumChGet [I]
 * @param *p_channels [O]
 * @return number of channels
 */
uint8_t AppLoraWanGetChannels(uint8_t startChId, uint8_t maxNumChGet, ChannelParams_t *p_channels)
{
    MibRequestConfirm_t mibGet;
    ChannelParams_t *p_chSrc, *p_chDst;
    uint8_t maxChId, numGetChs;

    if ((maxNumChGet == 0) || (p_channels == NULL)) {
        return 0;
    }

    switch (appLoraWanSettings.region) {
#if defined(REGION_EU868)
        case LORAMAC_REGION_EU868:
            maxChId = (16 - 1);
            break;
#endif
#if defined(REGION_AS923)
        case LORAMAC_REGION_AS923:
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
        case LORAMAC_REGION_AS923_2:
        case LORAMAC_REGION_AS923_3:
        case LORAMAC_REGION_AS923_4:
#endif
        case LORAMAC_REGION_AS923_JPN:
            maxChId = (16 - 1);
            break;
#endif
#if defined(REGION_US915)
        case LORAMAC_REGION_US915:
            maxChId = (72 - 1);
            break;
#endif
//
#if defined(REGION_CN779)
        case LORAMAC_REGION_CN779:
            maxChId = 15;  // nbChannels = 16
            break;
#endif
#if defined(REGION_EU433)
        case LORAMAC_REGION_EU433:
            maxChId = 15;  // nbChannels = 16
            break;
#endif
#if defined(REGION_AU915)
        case LORAMAC_REGION_AU915:
            maxChId = 71;  // nbChannels = 72
            break;
#endif
#if defined(REGION_CN470)
        case LORAMAC_REGION_CN470:
            maxChId = 95;   // nbChannels = 96
            break;
#endif
#if defined(REGION_KR920)
        case LORAMAC_REGION_KR920:
            maxChId = 15;  // nbChannels = 16
            break;
#endif
#if defined(REGION_IN865)
        case LORAMAC_REGION_IN865:
            maxChId = 15;  // nbChannels = 16
            break;
#endif
#if defined(REGION_RU864)
        case LORAMAC_REGION_RU864:
            maxChId = 7;  // nbChannels = 8
            break;
#endif
        default:
            maxChId = (uint8_t)(-1);
            break;
    }

    if ((maxChId != (uint8_t)(-1)) && (startChId <= maxChId)) {
        mibGet.Type = MIB_CHANNELS;
        LoRaMacMibGetRequestConfirm(&mibGet);

        p_chSrc   = &(mibGet.Param.ChannelList[startChId]);
        p_chDst   = p_channels;
        numGetChs = maxChId - startChId + 1;
        if (numGetChs > maxNumChGet) {
            numGetChs = maxNumChGet;
        }

        memcpy1((uint8_t *)p_chDst, (uint8_t *)p_chSrc,
                numGetChs * sizeof(ChannelParams_t));
    }
    else {
        // non-support or invalid-param
        numGetChs = 0;
    }

    return numGetChs;
}

/*!
 * @fn
 * set txPower
 * @param txPower [I]
 */
LoRaMacStatus_t AppLoraWanSetTxPower(int8_t txPower)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_CHANNELS_TX_POWER;
    mibReq.Param.ChannelsTxPower = txPower;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.txPower = txPower;
    }
#endif

    return status;
}

/*!
 * @fn
 * get txPower
 * @return txPower
 */
int8_t AppLoraWanGetTxPower(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_CHANNELS_TX_POWER;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.ChannelsTxPower;
}

/*!
 * @fn
 * set maxDutyCycle
 * @param maxDutyCycle [I]
 */
LoRaMacStatus_t AppLoraWanSetMaxDutyCycle(uint8_t maxDCycle)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_MAX_DCYCLE;
    mibReq.Param.maxDcycle = maxDCycle;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.maxDCycle = maxDCycle;
    }
#endif

    return status;
}

/*!
 * @fn
 * get maxDutyCycle
 * @return maxDutyCycle
 */
uint8_t AppLoraWanGetMaxDutyCycle(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_MAX_DCYCLE;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.maxDcycle;
}

/*!
 * @fn
 * set RX1DROffset
 * @param RX1DROffset [I]
 */
LoRaMacStatus_t AppLoraWanSetRx1DrOffset(uint8_t Rx1DrOffset)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_RX1_DROFFSET;
    mibReq.Param.rx1DrOffset = Rx1DrOffset;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.rx1DrOffset = Rx1DrOffset;
    }
#endif

    return status;
}

/*!
 * @fn
 * get RX1DROffset
 * @return RX1DROffset
 */
uint8_t AppLoraWanGetRx1DrOffset(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_RX1_DROFFSET;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.rx1DrOffset;
}

/*!
 * @fn
 * set RX2Freq
 * @param RX2Freq [I]
 */
LoRaMacStatus_t AppLoraWanSetRX2Freq(uint32_t rx2Freq)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_RX2_CHANNEL;
    LoRaMacMibGetRequestConfirm(&mibReq);

    mibReq.Param.Rx2Channel.Frequency = rx2Freq;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    if (status ==  LORAMAC_STATUS_OK) {
        mibReq.Type = MIB_RXC_CHANNEL;
        LoRaMacMibGetRequestConfirm(&mibReq);

        mibReq.Param.RxCChannel.Frequency = rx2Freq;
        status = LoRaMacMibSetRequestConfirm(&mibReq);
    }

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.rx2Freq = rx2Freq;
    }
#endif

    return status;
}

/*!
 * @fn
 * get RX2Freq
 * @return RX2Freq
 */
uint32_t AppLoraWanGetRX2Freq(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_RX2_CHANNEL;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.Rx2Channel.Frequency;
}

/*!
 * @fn
 * set RX2DataRate
 * @param RX2DataRate [I]
 */
LoRaMacStatus_t AppLoraWanSetRX2DataRate(uint8_t rx2Dr)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_RX2_CHANNEL;
    LoRaMacMibGetRequestConfirm(&mibReq);

    mibReq.Param.Rx2Channel.Datarate = rx2Dr;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

    if (status ==  LORAMAC_STATUS_OK) {
        mibReq.Type = MIB_RXC_CHANNEL;
        LoRaMacMibGetRequestConfirm(&mibReq);

        mibReq.Param.RxCChannel.Datarate = rx2Dr;
        status = LoRaMacMibSetRequestConfirm(&mibReq);
    }

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.rx2Dr = rx2Dr;
    }
#endif

    return status;
}

/*!
 * @fn
 * get RX2DataRate
 * @return RX2DataRate
 */
uint8_t AppLoraWanGetRX2DataRate(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_RX2_CHANNEL;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.Rx2Channel.Datarate;
}

/*!
 * @fn
 * set MaxEIRP
 * @param MaxEIRP [I]
 */
LoRaMacStatus_t AppLoraWanSetMaxEIRP(uint8_t maxEirp)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_MAX_EIRP;
    mibReq.Param.maxEirp = maxEirp;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.maxEirp = maxEirp;
    }
#endif

    return status;
}

/*!
 * @fn
 * get MaxEIRP
 * @return MaxEIRP
 */
uint8_t AppLoraWanGetMaxEIRP(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_MAX_EIRP;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.maxEirp;
}


/*!
 * @fn
 * set DownlinkDwellTime
 * @param DownlinkDwellTime [I]
 */
LoRaMacStatus_t AppLoraWanSetDownlinkDwellTime(uint8_t downlinkDwellTime)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_DOWNLINK_DWELLTIME;
    mibReq.Param.downlinkDwellTime = downlinkDwellTime;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.downlinkDwellTime = downlinkDwellTime;
    }
#endif

    return status;
}

/*!
 * @fn
 * get DownlinkDwellTime
 * @return DownlinkDwellTime
 */
uint8_t AppLoraWanGetDownlinkDwellTime(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_DOWNLINK_DWELLTIME;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.downlinkDwellTime;
}

/*!
 * @fn
 * set UplinkDwellTime
 * @param UplinkDwellTime [I]
 */
LoRaMacStatus_t AppLoraWanSetUplinkDwellTime(uint8_t uplinkDwellTime)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_UPLINK_DWELLTIME;
    mibReq.Param.uplinkDwellTime = uplinkDwellTime;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.uplinkDwellTime = uplinkDwellTime;
    }
#endif

    return status;
}

/*!
 * @fn
 * get UplinkDwellTime
 * @return UplinkDwellTime
 */
uint8_t AppLoraWanGetUplinkDwellTime(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_UPLINK_DWELLTIME;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.uplinkDwellTime;
}

/*!
 * @fn
 * set PingSlotDataRate
 * @param PingSlotDataRate [I]
 */
LoRaMacStatus_t AppLoraWanSetPingSlotDataRate( uint8_t PsltDr )
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_PING_SLOT_DATARATE;
    mibReq.Param.PingSlotDatarate = PsltDr;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.pingSlotDr = PsltDr;
    }
#endif

    return status;
}

/*!
 * @fn
 * get PingSlotDataRate
 * @return PingSlotDataRate
 */
uint8_t AppLoraWanGetPingSlotDataRate(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_PING_SLOT_DATARATE;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.PingSlotDatarate;
}

/*!
 * @fn
 * set DownlinkFCnt
 * @param DownlinkFCnt [I]
 */
LoRaMacStatus_t AppLoraWanSetDownlinkFCnt(uint32_t downlinkFCnt)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_DOWNLINK_FCNT;
    mibReq.Param.downlinkFCnt = downlinkFCnt;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.downlinkFCnt = downlinkFCnt;
    }
#endif

    return status;
}

/*!
 * @fn
 * get DownlinkFCnt
 * @return DownlinkFCnt
 */
uint32_t AppLoraWanGetDownlinkFCnt(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_DOWNLINK_FCNT;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.downlinkFCnt;
}

/*!
 * @fn
 * set UplinkFCnt
 * @param UplinkFCnt [I]
 */
LoRaMacStatus_t AppLoraWanSetUplinkFCnt(uint32_t uplinkFCnt)
{
    LoRaMacStatus_t status;
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_UPLINK_FCNT;
    mibReq.Param.uplinkFCnt = uplinkFCnt;
    status = LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if (status ==  LORAMAC_STATUS_OK) {
        appLoraWanNvmData.uplinkFCnt = uplinkFCnt;
    }
#endif

    return status;
}

/*!
 * @fn
 * get UplinkFCnt
 * @return UplinkFCnt
 */
uint32_t AppLoraWanGetUplinkFCnt(void)
{
    MibRequestConfirm_t mibGet;

    mibGet.Type = MIB_UPLINK_FCNT;
    LoRaMacMibGetRequestConfirm(&mibGet);

    return mibGet.Param.uplinkFCnt;
}

void AppLoraWanNvmDataMgmtSetRestoreMode(uint8_t restoreMode)
{
    appLoraWanNvmData.restoreMode = restoreMode;
}

static void AppLoraWanNvmDataMgmtLoad(void)
{
    LoRaMacRegion_t region;
    uint32_t readFlg, readResultFlg;

    region = appLoraWanSettings.region;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    if( ( region >= LORAMAC_REGION_AS923_JPN ) && ( region < LORAMAC_REGION_AS923_MAXGROUP ) )  // JPN, AS923_2,3,...
#else
    if( ( region == LORAMAC_REGION_AS923_JPN ) )  // JPN
#endif
    {
        region = LORAMAC_REGION_AS923;
    }

    readFlg = 0;
    switch (appLoraWanNvmData.restoreMode) {
        /*-- in case of startup --*/
        case APP_LORAWAN_NVMDATA_RESTORE_NORMAL:
            //-- read from NVM --
            if (appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_OTAA) {
                // (in case of OTAA)
                //   DevNonce, App/JoinNonce
                readFlg = LORAMAC_NVM_MIBFLG_DEV_NONCE | LORAMAC_NVM_MIBFLG_APP_NONCE;
            }
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
            else {
                // (in case of ABP)
                //   FrameCounter(downlink), FrameCounter(uplink)
                //   Channels(AS923,EU868), ChannelMask, DataRate, NbTrans, TxPower, MaxDutycycle,
                //   Rx1DROffset, Rx2Channels(Freq,DR), RxTimingDelay, MaxEIRP,
                readFlg = LORAMAC_NVM_MIBFLG_DOWNLINK_FCNT     | LORAMAC_NVM_MIBFLG_UPLINK_FCNT        |
                          LORAMAC_NVM_MIBFLG_CHANNELS          | LORAMAC_NVM_MIBFLG_CHANNELS_MASK      |
                          LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE | LORAMAC_NVM_MIBFLG_CHANNELS_NB_TRANS  |
                          LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER  | LORAMAC_NVM_MIBFLG_MAX_DCYCLE         |
                          LORAMAC_NVM_MIBFLG_RX1_DROFFSET      | LORAMAC_NVM_MIBFLG_RX2_FREQUENCY      |
                          LORAMAC_NVM_MIBFLG_RX2_DATARATE      | LORAMAC_NVM_MIBFLG_RECEIVE_DELAY_1    |
                          LORAMAC_NVM_MIBFLG_MAX_EIRP          | LORAMAC_NVM_MIBFLG_DOWNLINK_DWELLTIME |
                          LORAMAC_NVM_MIBFLG_UPLINK_DWELLTIME  | LORAMAC_NVM_MIBFLG_PING_SLOT_DATARATE;

                if ((region != LORAMAC_REGION_AS923) && (region != LORAMAC_REGION_EU868)) {
                    readFlg &= ~LORAMAC_NVM_MIBFLG_CHANNELS;
                }
            }
#endif
            break;

        /*-- in case when region is changed --*/
        case APP_LORAWAN_NVMDATA_RESTORE_REGION_CHG:
            //-- read from NVM --
            if (appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_OTAA) {
                // (in case of OTAA)
                //   DevNonce, App/JoinNonce
                readFlg = LORAMAC_NVM_MIBFLG_DEV_NONCE | LORAMAC_NVM_MIBFLG_APP_NONCE;

            }
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
            if (appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_ABP) {
                // (in case of ABP)
                //   FrameCounter(downlink), FrameCounter(uplink)
                readFlg = LORAMAC_NVM_MIBFLG_DOWNLINK_FCNT | LORAMAC_NVM_MIBFLG_UPLINK_FCNT;
            }
#endif
            break;

        /*-- in case when ActMode(OTAA/ABP) is changed --*/
        /*-- in case of reset all parameters --*/
        case APP_LORAWAN_NVMDATA_RESTORE_ACTMODE_CHG:
        case APP_LORAWAN_NVMDATA_RESTORE_RESETNVM:
            // readFlg = 0;
            break;

        default:
            break;
    }

    // read parameter from NVM
    appLoraWanNvmData.flgLoadValsFromNvm = 0;  // clear
    if (readFlg != 0) {
        AppLoraWanNvmDataRead(readFlg, &readResultFlg);
        appLoraWanNvmData.flgLoadValsFromNvm = readResultFlg;
    }
}

void AppLoraWanNvmDataMgmtRestore(void)
{
    LoRaMacRegion_t region;
    uint32_t saveInitValFlg = 0;

    region = appLoraWanSettings.region;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    if( ( region >= LORAMAC_REGION_AS923_JPN ) && ( region < LORAMAC_REGION_AS923_MAXGROUP ) )  // JPN, AS923_2,3,...
#else
    if( ( region == LORAMAC_REGION_AS923_JPN ) )  // JPN
#endif
    {
        region = LORAMAC_REGION_AS923;
    }

    //--------------------------
    // read parameters from NVM
    AppLoraWanNvmDataMgmtLoad();

    //------------------------------------------------------------
    // restore parameters to loramac (required for OTAA)
    if (appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_OTAA) {
        /* DevNonce */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_DEV_NONCE) != 0) {
            AppLoraWanSetDevNonce(appLoraWanNvmData.devNonce);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_DEV_NONCE;
        }
        /* App/JoinNonce */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_APP_NONCE) != 0) {
            AppLoraWanSetAppNonce(appLoraWanNvmData.appNonce);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_APP_NONCE;
        }
    }
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    //--------------------------------------------------------
    // restore parameters to loramac (required for ABP)
    else {
        /* FrameCounter(downlink) */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_DOWNLINK_FCNT) != 0) {
            AppLoraWanSetDownlinkFCnt(appLoraWanNvmData.downlinkFCnt);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_DOWNLINK_FCNT;
        }
        /* FrameCounter(uplink) */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_UPLINK_FCNT) != 0) {
            AppLoraWanSetUplinkFCnt(appLoraWanNvmData.uplinkFCnt);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_UPLINK_FCNT;
        }
        /* Channels (AS923,EU868,IN865,KR920) */
        if ((region == LORAMAC_REGION_AS923) || (region == LORAMAC_REGION_EU868) ||
            (region == LORAMAC_REGION_IN865) || (region == LORAMAC_REGION_KR920)) {
            if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_CHANNELS) != 0) {
               AppLoraWanSetChannels(0, 16, appLoraWanNvmData.channelsList);  // id=0,1,...,15
            }
            else {
                // get from loramac and store to NVM
                saveInitValFlg |= LORAMAC_NVM_MIBFLG_CHANNELS;
           }
        }
        /* ChannelMask */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_CHANNELS_MASK) != 0) {
            AppLoraWanSetChannelsMask(appLoraWanNvmData.channelsMask);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_CHANNELS_MASK;
        }
        /* DataRate */
        if (appLoraWanSettings.adr == true) {
            if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE) != 0) {
                AppLoraWanSetAdrOnDR(appLoraWanNvmData.dataRate);
            }
            else {
                // get from loramac and store to NVM
                saveInitValFlg |= LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE;
            }
        }
        /* NbTrans */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_CHANNELS_NB_TRANS) != 0) {
            AppLoraWanSetNbTrans(appLoraWanNvmData.nbTrans);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_CHANNELS_NB_TRANS;
        }
        /* TxPower */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER) != 0) {
            AppLoraWanSetTxPower(appLoraWanNvmData.txPower);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER;
        }
        /* MaxDutycycle */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_MAX_DCYCLE) != 0) {
            AppLoraWanSetMaxDutyCycle(appLoraWanNvmData.maxDCycle);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_MAX_DCYCLE;
        }
        /* Rx1DROffset */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_RX1_DROFFSET) != 0) {
            AppLoraWanSetRx1DrOffset(appLoraWanNvmData.rx1DrOffset);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_RX1_DROFFSET;
        }
        /* Rx2Frequency */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_RX2_FREQUENCY) != 0) {
            AppLoraWanSetRX2Freq(appLoraWanNvmData.rx2Freq);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_RX2_FREQUENCY;
        }
        /* Rx2Datarate */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_RX2_DATARATE) != 0) {
            AppLoraWanSetRX2DataRate(appLoraWanNvmData.rx2Dr);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_RX2_DATARATE;
        }
        /* RxTimingDelay */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_RECEIVE_DELAY_1) != 0) {
            AppLoraWanSetRx1Delay(appLoraWanNvmData.rx1Delay);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_RECEIVE_DELAY_1;
        }
        /* MaxEIRP */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_MAX_EIRP) != 0) {
            AppLoraWanSetMaxEIRP(appLoraWanNvmData.maxEirp);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_MAX_EIRP;
        }
        /* DownlinkDwellTime */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_DOWNLINK_DWELLTIME) != 0) {
            AppLoraWanSetDownlinkDwellTime(appLoraWanNvmData.downlinkDwellTime);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_DOWNLINK_DWELLTIME;
        }
        /* UplinkDwellTime */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_UPLINK_DWELLTIME) != 0) {
            AppLoraWanSetUplinkDwellTime(appLoraWanNvmData.uplinkDwellTime);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_UPLINK_DWELLTIME;
        }
        /* PingSlotDataRate */
        if ((appLoraWanNvmData.flgLoadValsFromNvm & LORAMAC_NVM_MIBFLG_PING_SLOT_DATARATE) != 0) {
            AppLoraWanSetPingSlotDataRate(appLoraWanNvmData.pingSlotDr);
        }
        else {
            // get from loramac and store to NVM
            saveInitValFlg |= LORAMAC_NVM_MIBFLG_PING_SLOT_DATARATE;
        }
    }
#endif

    if (saveInitValFlg != 0) {
        AppLoraWanNvmDataMgmtSave(saveInitValFlg);
    }

    appLoraWanNvmData.restoreMode = APP_LORAWAN_NVMDATA_RESTORE_NORMAL;    // reset mode
}

static uint32_t AppLoraWanNvmDataMgmtGetSaveFlag(void)
{
    uint32_t    retFlg = 0;
    LoRaMacRegion_t region;

    region = appLoraWanSettings.region;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    if( ( region >= LORAMAC_REGION_AS923_JPN ) && ( region < LORAMAC_REGION_AS923_MAXGROUP ) )  // JPN, AS923_2,3,...
#else
    if( ( region == LORAMAC_REGION_AS923_JPN ) )  // JPN
#endif
    {
        region = LORAMAC_REGION_AS923;
    }

    if (appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_OTAA) {
        // (in case of OTAA)
        //   DevNonce, App/JoinNonce
        retFlg = LORAMAC_NVM_MIBFLG_DEV_NONCE | LORAMAC_NVM_MIBFLG_APP_NONCE;
    }
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    else {
        // (in case of ABP)
        //   FrameCounter(downlink), FrameCounter(uplink)
        //   Channels(AS923,EU868), ChannelMask, DataRate, NbTrans, TxPower, MaxDutycycle,
        //   Rx1DROffset, Rx2Channels(Freq,DR), RxTimingDelay, MaxEIRP,
        retFlg = LORAMAC_NVM_MIBFLG_DOWNLINK_FCNT     | LORAMAC_NVM_MIBFLG_UPLINK_FCNT        |
                 LORAMAC_NVM_MIBFLG_CHANNELS          | LORAMAC_NVM_MIBFLG_CHANNELS_MASK      |
                 LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE | LORAMAC_NVM_MIBFLG_CHANNELS_NB_TRANS  |
                 LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER  | LORAMAC_NVM_MIBFLG_MAX_DCYCLE         |
                 LORAMAC_NVM_MIBFLG_RX1_DROFFSET      | LORAMAC_NVM_MIBFLG_RX2_FREQUENCY      |
                 LORAMAC_NVM_MIBFLG_RX2_DATARATE      | LORAMAC_NVM_MIBFLG_RECEIVE_DELAY_1    |
                 LORAMAC_NVM_MIBFLG_MAX_EIRP          | LORAMAC_NVM_MIBFLG_DOWNLINK_DWELLTIME |
                 LORAMAC_NVM_MIBFLG_UPLINK_DWELLTIME  | LORAMAC_NVM_MIBFLG_PING_SLOT_DATARATE;

        if ((region != LORAMAC_REGION_AS923) && (region != LORAMAC_REGION_EU868)) {
            retFlg &= ~LORAMAC_NVM_MIBFLG_CHANNELS;
        }
    }
#endif

    return retFlg;
}

void AppLoraWanNvmDataMgmtSave(uint32_t notifyMibFlags)
{
    LoRaMacRegion_t region;
    uint32_t        filterFlags;
    uint32_t        saveFlg;

    // get available flag
    filterFlags = AppLoraWanNvmDataMgmtGetSaveFlag();
    notifyMibFlags &= filterFlags;  // filtering

    if (notifyMibFlags == 0) {
        return;  // nothing to do when this flag is 0
    }

    region = appLoraWanSettings.region;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    if( ( region >= LORAMAC_REGION_AS923_JPN ) && ( region < LORAMAC_REGION_AS923_MAXGROUP ) )  // JPN, AS923_2,3,...
#else
    if( ( region == LORAMAC_REGION_AS923_JPN ) )  // JPN
#endif
    {
        region = LORAMAC_REGION_AS923;
    }

    // init
    saveFlg = 0;

    //--------------------------------------------------------
    // parameters in non-volatile memory (required for OTAA)
    /* DevNonce is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_DEV_NONCE) != 0) {
        appLoraWanNvmData.devNonce = AppLoraWanGetDevNonce();
        saveFlg |= LORAMAC_NVM_MIBFLG_DEV_NONCE;
    }
    /* App/JoinNonce is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_APP_NONCE) != 0) {
        appLoraWanNvmData.appNonce = AppLoraWanGetAppNonce();
        saveFlg |= LORAMAC_NVM_MIBFLG_APP_NONCE;
    }

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    //--------------------------------------------------------
    // parameters in non-volatile memory (required for ABP)
    /* FrameCounter(downlink) is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_DOWNLINK_FCNT) != 0) {
        appLoraWanNvmData.downlinkFCnt = AppLoraWanGetDownlinkFCnt();
        saveFlg |= LORAMAC_NVM_MIBFLG_DOWNLINK_FCNT;
    }
    /* FrameCounter(uplink) is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_UPLINK_FCNT) != 0) {
        appLoraWanNvmData.uplinkFCnt = AppLoraWanGetUplinkFCnt();
        saveFlg |= LORAMAC_NVM_MIBFLG_UPLINK_FCNT;
    }
    /* Channels is changed by LoRaWAN stack (AS923,EU868,IN865,KR920)*/
    if ((region == LORAMAC_REGION_AS923) || (region == LORAMAC_REGION_EU868) ||
        (region == LORAMAC_REGION_IN865) || (region == LORAMAC_REGION_KR920)) {
        if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_CHANNELS) != 0) {
            AppLoraWanGetChannels(0, 16, appLoraWanNvmData.channelsList);  // id=0,1,...,15
            saveFlg |= LORAMAC_NVM_MIBFLG_CHANNELS;
        }
    }
    /* ChannelMask is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_CHANNELS_MASK) != 0) {
        uint8_t entries;
        AppLoraWanGetChannelsMask(appLoraWanNvmData.channelsMask, &entries);
        saveFlg |= LORAMAC_NVM_MIBFLG_CHANNELS_MASK;
    }
    /* DataRate is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE) != 0) {
        if (appLoraWanSettings.adr == true) {  // ADR On
            appLoraWanNvmData.dataRate = AppLoraWanGetDR();
            saveFlg |= LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE;
        }
    }
    /* NbTrans is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_CHANNELS_NB_TRANS) != 0) {
        appLoraWanNvmData.nbTrans = AppLoraWanGetNbTrans();
        saveFlg |= LORAMAC_NVM_MIBFLG_CHANNELS_NB_TRANS;
    }
    /* TxPower is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER) != 0) {
        appLoraWanNvmData.txPower = AppLoraWanGetTxPower();
        saveFlg |= LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER;
    }
    /* MaxDutycycle is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_MAX_DCYCLE) != 0) {
        appLoraWanNvmData.maxDCycle = AppLoraWanGetMaxDutyCycle();
        saveFlg |= LORAMAC_NVM_MIBFLG_MAX_DCYCLE;
    }
    /* Rx1DROffset is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_RX1_DROFFSET) != 0) {
        appLoraWanNvmData.rx1DrOffset = AppLoraWanGetRx1DrOffset();
        saveFlg |= LORAMAC_NVM_MIBFLG_RX1_DROFFSET;
    }
    /* Rx2Frequency is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_RX2_FREQUENCY) != 0) {
        appLoraWanNvmData.rx2Freq = AppLoraWanGetRX2Freq();
        saveFlg |= LORAMAC_NVM_MIBFLG_RX2_FREQUENCY;
    }
    /* Rx2Datarate is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_RX2_DATARATE) != 0) {
        appLoraWanNvmData.rx2Dr = AppLoraWanGetRX2DataRate();
        saveFlg |= LORAMAC_NVM_MIBFLG_RX2_DATARATE;
    }
    /* RxTimingDelay is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_RECEIVE_DELAY_1) != 0) {
        appLoraWanNvmData.rx1Delay = AppLoraWanGetRx1Delay();
        saveFlg |= LORAMAC_NVM_MIBFLG_RECEIVE_DELAY_1;
    }
    /* MaxEIRP is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_MAX_EIRP) != 0) {
        appLoraWanNvmData.maxEirp = AppLoraWanGetMaxEIRP();
        saveFlg |= LORAMAC_NVM_MIBFLG_MAX_EIRP;
    }
    /* DownlinkDwellTime is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_DOWNLINK_DWELLTIME) != 0) {
        appLoraWanNvmData.downlinkDwellTime = AppLoraWanGetDownlinkDwellTime();
        saveFlg |= LORAMAC_NVM_MIBFLG_DOWNLINK_DWELLTIME;
    }
    /* UplinkDwellTime is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_UPLINK_DWELLTIME) != 0) {
        appLoraWanNvmData.uplinkDwellTime = AppLoraWanGetUplinkDwellTime();
        saveFlg |= LORAMAC_NVM_MIBFLG_UPLINK_DWELLTIME;
    }
    /* PingSlotDataRate is changed by LoRaWAN stack */
    if ((notifyMibFlags & LORAMAC_NVM_MIBFLG_PING_SLOT_DATARATE) != 0) {
        appLoraWanNvmData.pingSlotDr = AppLoraWanGetPingSlotDataRate();
        saveFlg |= LORAMAC_NVM_MIBFLG_PING_SLOT_DATARATE;
    }
#endif

    if (saveFlg != 0) {
        // store it to NVM
        AppLoraWanNvmDataWrite(saveFlg, &saveFlg);
        appLoraWanNvmData.flgLoadValsFromNvm |= saveFlg;
    }
}

/***********************************************************************
 * function name  : AppLoraWanNvmDataRead
 * description    :
 * parameters     :
 * return value   : none
 **********************************************************************/
void AppLoraWanNvmDataRead( uint32_t readValFlags, uint32_t *p_readResultFlags )
{
    uint8_t     i, funcRet;
    uint32_t    readResultFlags;
    const AppLoraWanNvmDataTable_t   *p_appLoraWanNvmDataTable;

    readResultFlags = 0;
    p_appLoraWanNvmDataTable = &appLoraWanNvmDataTable[0];
    for( i = 0; i < APP_LORAWAN_NUM_NVMDATA; i++ )
    {
        if( (p_appLoraWanNvmDataTable->rwReqFlag & readValFlags) != 0 )
        {
            // read value(s) from data flash
            funcRet = NvmRead( p_appLoraWanNvmDataTable->dataId,
                                     (uint8_t *)p_appLoraWanNvmDataTable->vp_Data,
                                     p_appLoraWanNvmDataTable->dataSize );
            if( funcRet == NVM_RESULT_SUCCESS )
            {
                readResultFlags |= p_appLoraWanNvmDataTable->rwReqFlag;
            }
        }

        // next
        p_appLoraWanNvmDataTable++;
    }

    if( p_readResultFlags != NULL )
    {
        (*p_readResultFlags) = readResultFlags & readValFlags;
    }
}

/***********************************************************************
 * function name  : AppLoRaWanNvmDataWrite
 * description    :
 * parameters     :
 * return value   : none
 **********************************************************************/
void AppLoraWanNvmDataWrite( uint32_t writeValFlags, uint32_t *p_writeResultFlags )
{
    uint8_t     i, funcRet;
    uint32_t    writeResultFlags;
    const AppLoraWanNvmDataTable_t   *p_appLoRaWanNvmDataTable;

    writeResultFlags = 0;
    p_appLoRaWanNvmDataTable = &appLoraWanNvmDataTable[0];
    for( i = 0; i < APP_LORAWAN_NUM_NVMDATA; i++ )
    {
        if( (p_appLoRaWanNvmDataTable->rwReqFlag & writeValFlags) != 0 )
        {
            // write value(s) to data flash
            funcRet = NvmWrite( p_appLoRaWanNvmDataTable->dataId,
                                      (uint8_t *)p_appLoRaWanNvmDataTable->vp_Data,
                                      p_appLoRaWanNvmDataTable->dataSize );
            if( funcRet == NVM_RESULT_SUCCESS )
            {
                writeResultFlags |= p_appLoRaWanNvmDataTable->rwReqFlag;
            }
        }

        // next
        p_appLoRaWanNvmDataTable++;
    }

    if( p_writeResultFlags != NULL )
    {
        (*p_writeResultFlags) = writeResultFlags & writeValFlags;
    }
}
