/*
    (C) 2017 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

/*
 * lorawan_proc.h
 *
 *  Created on: 2017/07/21
 *      Author:
 */

#ifndef _LORAWAN_PROC_H_
#define _LORAWAN_PROC_H_

#include "timer.h"
#include "LoRaMac.h"
#include "region/Region.h"
#include "lorawan_nvmdata_table.h"
#include "nvm.h"

/* macro    */
/* ----- activation mode    */
#define APP_LORAWAN_ACTMODE_ABP     (0)
#define APP_LORAWAN_ACTMODE_OTAA    (1)

/* ----- length             */
#define APP_LORAWAN_LEN_NETID       (3)
#define APP_LORAWAN_LEN_DEVADDR     (4)
#define APP_LORAWAN_LEN_APPEUI      (8)
#define APP_LORAWAN_LEN_DEVEUI      (8)
#define APP_LORAWAN_LEN_APPKEY      (16)
#define APP_LORAWAN_LEN_APPSKEY     (16)
#define APP_LORAWAN_LEN_NWKSKEY     (16)
#define APP_LORAWAN_LEN_GENAPPKEY   (16)
#define APP_LORAWAN_LEN_MCADDR      (4)
#define APP_LORAWAN_LEN_MCFCNT      (4)
#define APP_LORAWAN_LEN_MCAPPSKEY   (16)
#define APP_LORAWAN_LEN_MCNWKSKEY   (16)
#define APP_LORAWAN_LEN_MCKEYENC    (16)
#define APP_LORAWAN_LEN_DEVNONCE    (2)
#define APP_LORAWAN_LEN_APPNONCE    (3)
#define APP_LORAWAN_LEN_FCNT        (4)

/* ----- RSSI verbose mode  */
#define APP_LORAWAN_RSSI_OFF    (0)
#define APP_LORAWAN_RSSI_ON     (1)

/* ----- default parameters */
#define APP_LORAWAN_DEFAULT_DR          DR_0
#define APP_LORAWAN_DEFAULT_FPORT       (1)
#define APP_LORAWAN_DEFAULT_MTYPE       MCPS_UNCONFIRMED
#if (LORAMAC_VERSION < LORAWAN_VERSION_1_0_4)  // LW1.0.3
#define APP_LORAWAN_DEFAULT_NBTRIALS    (1+1)       // Retry 1 time 
#endif

/* ----- DutyCycle mode */
#define APP_LORAWAN_DCYCLE_OFF      (0)
#define APP_LORAWAN_DCYCLE_ON       (1)

/* ----- number of channels default mask entries */ 
#define APP_LORAWAN_CHANNELS_MASK_ENTRIES    (6)

/* ----- version of data format when to store parameters to data flash */
#define APP_LORAWAN_DATA_FORMAT_VERSION_LEN  (8)
#define APP_LORAWNA_DATA_FORMAT_VERSION     'L','W','F','V','3','.','1','0'  

/* ----- Certification mode */
#define APP_LORAWAN_CERT_OFF        (0)
#define APP_LORAWAN_CERT_ON         (1)

/* ----- Nvm Data Read/Write flag */
#define APP_LORAWAN_NVMDATA_RWFLG_DEV_NONCE           LORAMAC_NVM_MIBFLG_DEV_NONCE
#define APP_LORAWAN_NVMDATA_RWFLG_APP_NONCE           LORAMAC_NVM_MIBFLG_APP_NONCE
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
#define APP_LORAWAN_NVMDATA_RWFLG_DOWNLINK_FCNT       LORAMAC_NVM_MIBFLG_DOWNLINK_FCNT
#define APP_LORAWAN_NVMDATA_RWFLG_UPLINK_FCNT         LORAMAC_NVM_MIBFLG_UPLINK_FCNT
#define APP_LORAWAN_NVMDATA_RWFLG_CHANNELS            LORAMAC_NVM_MIBFLG_CHANNELS
#define APP_LORAWAN_NVMDATA_RWFLG_CHANNELS_MASK       LORAMAC_NVM_MIBFLG_CHANNELS_MASK
#define APP_LORAWAN_NVMDATA_RWFLG_CHANNELS_DATARATE   LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE
#define APP_LORAWAN_NVMDATA_RWFLG_CHANNELS_NB_TRANS   LORAMAC_NVM_MIBFLG_CHANNELS_NB_TRANS
#define APP_LORAWAN_NVMDATA_RWFLG_CHANNELS_TXPOWER    LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER
#define APP_LORAWAN_NVMDATA_RWFLG_MAX_DCYCLE          LORAMAC_NVM_MIBFLG_MAX_DCYCLE
#define APP_LORAWAN_NVMDATA_RWFLG_RX1_DROFFSET        LORAMAC_NVM_MIBFLG_RX1_DROFFSET
#define APP_LORAWAN_NVMDATA_RWFLG_RX2_FREQUENCY       LORAMAC_NVM_MIBFLG_RX2_FREQUENCY
#define APP_LORAWAN_NVMDATA_RWFLG_RX2_DATARATE        LORAMAC_NVM_MIBFLG_RX2_DATARATE
#define APP_LORAWAN_NVMDATA_RWFLG_RECEIVE_DELAY_1     LORAMAC_NVM_MIBFLG_RECEIVE_DELAY_1
#define APP_LORAWAN_NVMDATA_RWFLG_MAX_EIRP            LORAMAC_NVM_MIBFLG_MAX_EIRP
#define APP_LORAWAN_NVMDATA_RWFLG_DOWNLINK_DWELLTIME  LORAMAC_NVM_MIBFLG_DOWNLINK_DWELLTIME
#define APP_LORAWAN_NVMDATA_RWFLG_UPLINK_DWELLTIME    LORAMAC_NVM_MIBFLG_UPLINK_DWELLTIME
#define APP_LORAWAN_NVMDATA_RWFLG_PING_SLOT_DATARATE  LORAMAC_NVM_MIBFLG_PING_SLOT_DATARATE
#endif

#define APP_LORAWAN_NVMDATA_SIZE_DEV_NONCE               ( sizeof(uint16_t) )
#define APP_LORAWAN_NVMDATA_SIZE_APP_NONCE               ( sizeof(uint32_t) )
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
#define APP_LORAWAN_NVMDATA_SIZE_DOWNLINK_FCNT           ( sizeof(uint32_t) )
#define APP_LORAWAN_NVMDATA_SIZE_UPLINK_FCNT             ( sizeof(uint32_t) )
#define APP_LORAWAN_NVMDATA_SIZE_CHANNELS                ( 16 * 10 )                // 10 = sizeof(ChannelParams_t)
#define APP_LORAWAN_NVMDATA_SIZE_CHANNELS_MASK           ( 6 * sizeof(uint16_t) )
#define APP_LORAWAN_NVMDATA_SIZE_CHANNELS_DATARATE       ( sizeof(uint8_t) )
#define APP_LORAWAN_NVMDATA_SIZE_CHANNELS_NB_TRANS       ( sizeof(uint8_t) )
#define APP_LORAWAN_NVMDATA_SIZE_CHANNELS_TXPOWER        ( sizeof(int8_t) )
#define APP_LORAWAN_NVMDATA_SIZE_MAX_DCYCLE              ( sizeof(uint8_t) )
#define APP_LORAWAN_NVMDATA_SIZE_RX1_DROFFSET            ( sizeof(uint8_t) )
#define APP_LORAWAN_NVMDATA_SIZE_RX2_FREQUENCY           ( sizeof(uint32_t) )
#define APP_LORAWAN_NVMDATA_SIZE_RX2_DATARATE            ( sizeof(uint8_t) )
#define APP_LORAWAN_NVMDATA_SIZE_RECEIVE_DELAY_1         ( sizeof(uint16_t) )
#define APP_LORAWAN_NVMDATA_SIZE_MAX_EIRP                ( sizeof(uint8_t) )
#define APP_LORAWAN_NVMDATA_SIZE_DOWNLINK_DWELLTIME      ( sizeof(uint8_t) )
#define APP_LORAWAN_NVMDATA_SIZE_UPLINK_DWELLTIME        ( sizeof(uint8_t) )
#define APP_LORAWAN_NVMDATA_SIZE_PING_SLOT_DATARATE      ( sizeof(uint8_t) )
#endif

#define APP_LORAWAN_NVMDATA_RESTORE_NORMAL              0
#define APP_LORAWAN_NVMDATA_RESTORE_ACTMODE_CHG         1
#define APP_LORAWAN_NVMDATA_RESTORE_REGION_CHG          2
#define APP_LORAWAN_NVMDATA_RESTORE_RESETNVM            3

/* struct   */
/*!
 * LoRaWAN end-device setting
 */
typedef struct app_lorawan_settings_tag {
    /*! data format version  */
    uint8_t  formatVer[APP_LORAWAN_DATA_FORMAT_VERSION_LEN];
    /*! operate region  */
    LoRaMacRegion_t region;
    /*! operation class */
    DeviceClass_t class;
    /*! activation: APP_LORAWAN_ACTMODE_ABP / APP_LORAWAN_ACTMODE_OTAA  */
    uint8_t actMode;
    /*! AppKey  */
    uint8_t appKey[APP_LORAWAN_LEN_APPKEY];
    /*! DevEUI  */
    uint8_t devEUI[APP_LORAWAN_LEN_DEVEUI];
    /*! AppEUI  */
    uint8_t appEUI[APP_LORAWAN_LEN_APPEUI];
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    /*! NwkSKey         */
    uint8_t nwkSKey[APP_LORAWAN_LEN_NWKSKEY];
    /*! AppSKey         */
    uint8_t appSKey[APP_LORAWAN_LEN_APPSKEY];
#endif
#if (LORAMAC_MAX_MC_CTX > 0)
    /*! GenAppKey         */
    uint8_t genAppKey[APP_LORAWAN_LEN_GENAPPKEY];
#endif
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    /*! DevAddr - end-device Address            */
    uint32_t devAddr;
#endif
    /*! Duty cycle mode */
    uint8_t dCycle;
    /*! Message type - data message type (confirmed/unconfirmed)    */
    Mcps_t mtype;
    /*! Fport */
    uint8_t fPort;
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    /*! NetID - network id                      */
    uint32_t netID;
#endif
    /*! ADR - ADR mode          */
    bool adr;
    /*! RSSI verbose            */
    uint8_t rssi;
    /*! Data rate for tx message in case ADR is off */
    uint8_t dr;
    /*! Public network */
    bool publicNetwork;
#ifdef LORAMAC_CLASSB_ENABLED
    /* Periodicity */
    uint8_t periodicity;
#endif
    /* Cert FPort on */
    uint8_t certFportOn;
    /*! Channels default mask */
    uint8_t channelsDefaultMaskEntries;        // 0: use default value, else: enables changed default value
    uint16_t channelsDefaultMask[APP_LORAWAN_CHANNELS_MASK_ENTRIES];
#if defined(APP_COMPLIANCE)
    /*! Compliance test mode */
    uint8_t complianceTestMode;
#endif
} AppLoraWanSettings_t;

/*!
 * LoRaWAN end-device parameters to store non-volatile memory
 */
typedef struct app_lorawan_nvmpdata_tag {
    /* load flag */
    uint32_t flgLoadValsFromNvm;
    /* restore mode */
    uint8_t restoreMode;

    /*-----*/

    /*! DevNonce */
    uint16_t devNonce;
    /*! App/JoinNonce */
    uint32_t appNonce;
    /*! DownlinkFCnt */
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    uint32_t downlinkFCnt;
    /*! UplinkFCnt */
    uint32_t uplinkFCnt;
    /*! Channels (for AS923,EU868) */
    ChannelParams_t channelsList[16];  // 16 = number of channels
    /*! ChannelMask (for US915) */
    uint16_t channelsMask[6];
    /*! RX2Frequency */
    uint32_t rx2Freq;
    /*! Rx1 delay in msec */
    uint16_t rx1Delay;
    /*! DataRate */
    uint8_t dataRate;
    /*! NbTrans */
    uint8_t nbTrans;
    /*! TxPower */
    int8_t txPower;
    /*! MaxDutyCycle */
    uint8_t maxDCycle;
    /*! RX1DROffset */
    uint8_t rx1DrOffset;
    /*! RX2DataRate */
    uint8_t rx2Dr;
    /*! MaxEIRP */
    uint8_t maxEirp;
    /*! DownlinkDwellTime */
    uint8_t downlinkDwellTime;
    /*! UplinkDwellTime */
    uint8_t uplinkDwellTime;
    /*! PingSlotDataRate */
    uint8_t pingSlotDr;
#endif
} AppLoraWanNvmData_t;

typedef struct app_lorawan_nvmdata_table_tag
{
    uint32_t        rwReqFlag;
    void            *vp_Data;
    uint8_t const   dataSize;
    uint8_t         dataId;
} AppLoraWanNvmDataTable_t;

/* global variables */
extern const AppLoraWanSettings_t appLoraWanDefaultSettings;
extern const AppLoraWanNvmDataTable_t appLoraWanNvmDataTable[];
extern AppLoraWanSettings_t         appLoraWanSettings;
extern AppLoraWanNvmData_t          appLoraWanNvmData;

/* prototypes       */
/*!
 * initialize LoRaWAN stack and set parameters
 */
LoRaMacStatus_t AppLoraWanInit(LoRaMacPrimitives_t *primitives, LoRaMacCallback_t *callbacks);

/*!
 * Start LoRaWAN
 */
LoRaMacStatus_t AppLoraWanStart( void );

/*!
 * Stop LoRaWAN
 */
LoRaMacStatus_t AppLoraWanStop( void );

/*!
 * @fn
 * send join request message
 * @return  result code
 */
LoRaMacStatus_t AppLoraWanJoinReq(TimerTime_t *pDutyCycleWaitTime);

/*!
 * @fn
 * send data message
 * @return  result code
 */
LoRaMacStatus_t AppLoraWanSendData(uint8_t *data, uint8_t len, uint8_t fport, Mcps_t type, TimerTime_t *pDutyCycleWaitTime);

/*!
 * @fn
 * send LinkCheckReq command
 * @return  result code
 */
LoRaMacStatus_t AppLoraWanLinkCheck(void);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * set network joined status
 * @return  result code
 */
LoRaMacStatus_t AppLoraWanSetNetworkJoined(bool isNetworkJoined);
#endif

/*!
 * @fn
 * set device class
 * @param class [I] device class : CLASS_A/CLASS_C
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetDeviceClass(DeviceClass_t class);

/*!
 * @fn
 * set AppKey
 * @param appkey [I] AppKey in 16 bytes
 */
void AppLoraWanSetAppKey(uint8_t *appkey);

/*!
 * @fn
 * get AppKey
 * @param appkey [O] AppKey in 16 bytes
 */
void AppLoraWanGetAppkey(uint8_t *appKey);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * set AppSKey
 * @param appskey [I] AppSKey in 16 bytes
 */
LoRaMacStatus_t AppLoraWanSetAppSKey(uint8_t *appSKey);

/*!
 * @fn
 * get AppSKey
 * @param appskey [O] AppSKey in 16 bytes
 */
void AppLoraWanGetAppSKey(uint8_t *appSKey);

/*!
 * @fn
 * set NwkSKey
 * @param nwkskey [O] NwkSKey in 16 bytes
 */
LoRaMacStatus_t AppLoraWanSetNwkSKey(uint8_t *nwkSKey);

/*!
 * @fn
 * get NwkSKey
 * @param nwkskey [O] NwkSKey in 16 bytes
 */
void AppLoraWanGetNwkSKey(uint8_t *nwkSKey);
#endif

#if (LORAMAC_MAX_MC_CTX > 0)
/*!
 * @fn
 * set GenAppKey
 * @param genAppSKey [I] GenAppKey in 16 bytes
 */
LoRaMacStatus_t AppLoraWanSetGenAppKey(uint8_t *genAppSKey);

/*!
 * @fn
 * get GenAppKey
 * @param genAppSKey [O] GenAppKey in 16 bytes
 */
void AppLoraWanGetGenAppKey(uint8_t *genAppSKey);
#endif

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * set DevAddr
 * @param devaddr [I] DevAddr in 4 bytes
 */
LoRaMacStatus_t AppLoraWanSetDevAddr(uint32_t devAddr);
#endif

/*!
 * @fn
 * get DevAddr
 * @return DeAddr in 4 bytes
 */
uint32_t AppLoraWanGetDevAddr(void);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * set NetID
 * @param netID [I] NetID in 4 bytes
 */
LoRaMacStatus_t AppLoraWanSetNetID(uint32_t netId);
#endif

/*!
 * @fn
 * get NetID
 * @retur NetID in 4 bytes
 */
uint32_t AppLoraWanGetNetID(void);

/*!
 * @fn
 * set activation mode
 * @param actMode [I] activation mode: APP_LORAWAN_ACTMODE_ABP / APP_LORAWAN_ACTMODE_OTAA
 * @return status code : LORAMAC_STATUS_OK / LORAMAC_STATUS_PARAMETER_INVALID
 */
LoRaMacStatus_t AppLoraWanSetActMode(uint8_t actMode);

/*!
 * @fn
 * get activation mode
 * @return activation mode: APP_LORAWAN_ACTMODE_ABP: ABP / APP_LORAWAN_ACTMODE_OTAA: OTAA
 */
uint8_t AppLoraWanGetActMode(void);

/*!
 * @fn
 * set message type for tx data message
 * @param mtype message type: MCPS_CONFIRMED / MCPS_UNCONFIRMED
 */
LoRaMacStatus_t AppLoraWanSetMessageType(Mcps_t mtype);

/*!
 * @fn
 * get message type for tx data message
 * @return message type: MCPS_CONFIRMED / MCPS_UNCONFIRMED
 */
Mcps_t AppLoraWanGetMessageType(void);

/*!
 * @fn
 * set FPort value
 * @param fport [I] FPort value
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetFPort(uint8_t fport);

/*!
 * @fn
 * get FPort value
 * @return FPort value
 */
uint8_t AppLoraWanGetFPort(void);

/*!
 * @fn
 * set ADR (adaptive data rate)
 * @param enabled [I] ADR setting: true: ADR enabled, false: ADR disabled
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetAdr(bool enabled);

/*!
 * @fn
 * get current ADR setting
 * @return current ADR setting: true: ADR enabled, false: ADR disabled
 */
bool AppLoraWanGetAdr(void);

#ifdef LORAMAC_CLASSB_ENABLED
/*!
 * @fn
 * set Periodicity 
 */
LoRaMacStatus_t AppLoraWanSetPeriodicity(uint8_t periodicity);

 /*!
 * @fn
 * get Periodicity setting
 */
uint8_t AppLoraWanGetPeriodicity(void);
#endif

/*!
 * @fn
 * set delay time for RX window 1 in msec
 * @param delay [I] delay time for RX window 1 in msec. delay time for RX widow 2 is set to (delay + 1000).
 */
LoRaMacStatus_t AppLoraWanSetRx1Delay(uint16_t);

/*!
 * @fn
 * get delay time for RX window 1 in msec
 * @return delay time for RX window 1
 */
uint16_t AppLoraWanGetRx1Delay(void);

/*!
 * @fn
 * set Duty Cycle mode
 * @param ebabled [I] 1: duty cycle enabled, 0: duty cycle disabled
 */
LoRaMacStatus_t AppLoraWanSetDCycle(uint8_t enabled);

/*!
 * @fn
 * get Duty Cycle mode
 * @return Duty Cycle mode : 1: duty cycle enabled, 0: duty cycle disabled
 */
uint8_t AppLoraWanGetDCycle(void);

/*!
 * @fn
 * add a new channel
 * @param chID [I] new channel ID
 * @param freq [I] frequency in Hz for the ID
 * @return status code
 */
LoRaMacStatus_t AppLoraWanAddCh(uint8_t chID, uint32_t freq);

/*!
 * @fn
 * set DevEUI
 * @param devEUI [I] DevEUI in 16 bytes
 */
void AppLoraWanSetDevEUI(uint8_t *devEUI);

/*!
 * @fn
 * get DevEUI
 * @param devEUI [O] DevEUI in 16 bytes
 */
void AppLoraWanGetDevEUI(uint8_t *devEUI);

/*!
 * @fn
 * set AppEUI
 * @param appEUI [I] AppEUI in 8 bytes
 */
void AppLoraWanSetAppEUI(uint8_t *appEUI);

/*!
 * @fn
 * get AppEUI
 * @param appEUI [O] AppEUI in 8 bytes
 */
void AppLoraWanGetAppEUI(uint8_t *appEUI);

/*!
 * @fn
 * get device class
 * @return device class: CLASS_A / CLASS_C
 */
DeviceClass_t AppLoraWanGetDeviceClass(void);

/*!
 * @fn
 * set region
 * @param region [I] region: LORAMAC_REGION_EU868/LORAMAC_REGION_US915/LORAMAC_REGION_AS920
 */
LoRaMacStatus_t AppLoraWanSetRegion(LoRaMacRegion_t region);

/*!
 * @fn
 * get current region setting
 * @return current region setting: LORAMAC_REGION_EU868/LORAMAC_REGION_US915/LORAMAC_REGION_AS920
 */
LoRaMacRegion_t AppLoraWanGetRegion(void);

/*!
 * @fn
 * set data rate used in case ADR is disabled
 * @param dr [I] data rate
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetDR(uint8_t defDR);

/*!
 * @fn
 * get data rate used in case ADR is disabled
 * @return data rate value
 */
uint8_t AppLoraWanGetDR(void);

/*!
 * @fn
 * set data rate used in case ADR is enabled (to restore settings from NVM on initialization)
 * @param dr [I] data rate
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetAdrOnDR(uint8_t dr);

/*!
 * @fn
 * set RSSI display setting
 * @param rssiMode [I] RSSI display setting: 1: RSSI displayed, 0: RSSI not displayed
 */
void AppLoraWanSetRssi(uint8_t rssiMode);

/*!
 * @fn
 * get RSSI display setting
 * @return current RSSI display setting: 1: RSSI displayed, 0: RSSI not displayed
 */
uint8_t AppLoraWanGetRssi(void);

/*!
 * @fn
 * set public network mode
 * @param enabled [I] true: public network enabled, false: public netowork disabled
 */
LoRaMacStatus_t AppLoraWanSetPublicNetwork(bool enabled);

/*!
 * @fn
 * get public network mode
 * @return public network mode: true: public netowrk enabled, false: public netowork disabled
 */
bool AppLoraWanGetPublicNetwork(void);

/*!
 * @fn
 * check low power transition condition 
 * @return low power entry permission (true = low power allowed)
 */
bool AppLoraWanLowPowerAllowed( void );

/*
*send DeviceTimeReq command
*/
LoRaMacStatus_t AppLoraWanDeviceTime( void );

#ifdef LORAMAC_CLASSB_ENABLED
/*
*send BeaconAcquisition command
*/
LoRaMacStatus_t AppLoraWanBeaconAcquisition( void );

/*
*send PingSlotInfoReq command
*/
LoRaMacStatus_t AppLoraWanPingSlotInfo( uint8_t Psltinfo );

/*
*send BeaconTimingReq command
*/
LoRaMacStatus_t AppLoraWanBeaconTiming( void );
#endif

/*
 * set System max rx error
 */
LoRaMacStatus_t AppLoraWanSetSystemMaxRxError( uint8_t systemMaxRxError );

/*!
 * @fn
 * set channels default mask
 * @param channelsDefaultMask [I] pointer to channels default mask table
 * @param channelsDefaultMaskEntries [I] number of channels default mask table entries
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetChannelsDefaultMask( uint16_t *channelsDefaultMask, uint8_t channelsDefaultMaskEntries );

/*!
 * @fn
 * get channels default mask
 * @param channelsDefaultMask [O] pointer to channels default mask table
 * @param channelsDefaultMaskEntries [O] pointer to number of channels default mask table entries
 * @return status code
 */
void AppLoraWanGetChannelsDefaultMask( uint16_t *channelsDefaultMask, uint8_t *channelsDefaultMaskEntries );

/*!
 * @fn
 * set channels mask
 * @param channelsMask [I] pointer to channels mask table
 * @return status code
 */
LoRaMacStatus_t AppLoraWanSetChannelsMask(uint16_t *p_channelsMask);

/*!
 * @fn
 * get channels mask and number of entries
 * @param channelsMask [O] pointer to channels mask table
 * @param channelsMaskEntries [O] pointer to number of channels mask table entries
 */
void AppLoraWanGetChannelsMask(uint16_t *p_channelsMask, uint8_t *p_channelsMaskEntries);

/*!
 * @fn
 * get number of channels mask entries
 * @return number of channels mask entries
 */
uint8_t AppLoraWanGetChannelsMaskEntries( void );

/*!
 * @fn
 * set Certification fport on/off
 * @param enabled [I] 1: certification fport enabled, 0: certification fport disabled
 */
LoRaMacStatus_t AppLoraWanSetCertFPortOn(uint8_t enabled);

/*!
 * @fn
 * get Certification fport on/off
 * @return enabled : 1: certification fport enabled, 0: certifiation fport disabled
 */
uint8_t AppLoraWanGetCertFPortOn(void);

/*!
 * @fn
 * set devNonce
 * @param devNonce [I]
 */
LoRaMacStatus_t AppLoraWanSetDevNonce(uint16_t devNonce);

/*!
 * @fn
 * get devNonce
 * @return devNonce
 */
uint16_t AppLoraWanGetDevNonce(void);

/*!
 * @fn
 * set appNonce
 * @param appNonce [I]
 */
LoRaMacStatus_t AppLoraWanSetAppNonce(uint32_t appNonce);

/*!
 * @fn
 * get appNonce
 * @return appNonce
 */
uint32_t AppLoraWanGetAppNonce(void);

/*!
 * @fn
 * set nbTrans
 * @param nbTrans [I]
 */
LoRaMacStatus_t AppLoraWanSetNbTrans(uint8_t nbTrans);

/*!
 * @fn
 * get nbTrans
 * @return nbTrans
 */
uint8_t AppLoraWanGetNbTrans(void);

/*!
 * @fn
 * set(add/del) Channel
 * @param startChId [I]
 * @param numChSet [I]
 * @param *p_channels [I]
 */
void AppLoraWanSetChannels(uint8_t startChId, uint8_t numChSet, ChannelParams_t *p_channels);

/*!
 * @fn
 * get channels
 * @param startChId [I]
 * @param maxNumChGet [I]
 * @param *p_channels [O]
 * @return number of channels
 */
uint8_t AppLoraWanGetChannels(uint8_t startChId, uint8_t maxNumChGet, ChannelParams_t *p_channels);

/*!
 * @fn
 * set txPower
 * @param txPower [I]
 */
LoRaMacStatus_t AppLoraWanSetTxPower(int8_t txPower);

/*!
 * @fn
 * get txPower
 * @return txPower
 */
int8_t AppLoraWanGetTxPower(void);

/*!
 * @fn
 * set maxDutyCycle
 * @param maxDutyCycle [I]
 */
LoRaMacStatus_t AppLoraWanSetMaxDutyCycle(uint8_t maxDCycle);

/*!
 * @fn
 * get maxDutyCycle
 * @return maxDutyCycle
 */
uint8_t AppLoraWanGetMaxDutyCycle(void);

/*!
 * @fn
 * set RX1DROffset
 * @param RX1DROffset [I]
 */
LoRaMacStatus_t AppLoraWanSetRx1DrOffset(uint8_t Rx1DrOffset);

/*!
 * @fn
 * get RX1DROffset
 * @return RX1DROffset
 */
uint8_t AppLoraWanGetRx1DrOffset(void);

/*!
 * @fn
 * set RX2Freq
 * @param RX2Freq [I]
 */
LoRaMacStatus_t AppLoraWanSetRX2Freq(uint32_t rx2Freq);

/*!
 * @fn
 * get RX2Freq
 * @return RX2Freq
 */
uint32_t AppLoraWanGetRX2Freq(void);

/*!
 * @fn
 * set RX2DataRate
 * @param RX2DataRate [I]
 */
LoRaMacStatus_t AppLoraWanSetRX2DataRate(uint8_t rx2Dr);

/*!
 * @fn
 * get RX2DataRate
 * @return RX2DataRate
 */
uint8_t AppLoraWanGetRX2DataRate(void);

/*!
 * @fn
 * set MaxEIRP
 * @param MaxEIRP [I]
 */
LoRaMacStatus_t AppLoraWanSetMaxEIRP(uint8_t maxEirp);

/*!
 * @fn
 * get MaxEIRP
 * @return MaxEIRP
 */
uint8_t AppLoraWanGetMaxEIRP(void);

/*!
 * @fn
 * set DownlinkDwellTime
 * @param DownlinkDwellTime [I]
 */
LoRaMacStatus_t AppLoraWanSetDownlinkDwellTime(uint8_t downlinkDwellTime);

/*!
 * @fn
 * get DownlinkDwellTime
 * @return DownlinkDwellTime
 */
uint8_t AppLoraWanGetDownlinkDwellTime(void);

/*!
 * @fn
 * set UplinkDwellTime
 * @param UplinkDwellTime [I]
 */
LoRaMacStatus_t AppLoraWanSetUplinkDwellTime(uint8_t uplinkDwellTime);

/*!
 * @fn
 * get UplinkDwellTime
 * @return UplinkDwellTime
 */
uint8_t AppLoraWanGetUplinkDwellTime(void);

/*!
 * @fn
 * set PingSlotDataRate
 * @param PingSlotDataRate [I]
 */
LoRaMacStatus_t AppLoraWanSetPingSlotDataRate( uint8_t PsltDr );

/*!
 * @fn
 * get PingSlotDataRate
 * @return PingSlotDataRate
 */
uint8_t AppLoraWanGetPingSlotDataRate(void);

/*!
 * @fn
 * set DownlinkFCnt
 * @param DownlinkFCnt [I]
 */
LoRaMacStatus_t AppLoraWanSetDownlinkFCnt(uint32_t downlinkFCnt);

/*!
 * @fn
 * get DownlinkFCnt
 * @return DownlinkFCnt
 */
uint32_t AppLoraWanGetDownlinkFCnt(void);

/*!
 * @fn
 * set UplinkFCnt
 * @param UplinkFCnt [I]
 */
LoRaMacStatus_t AppLoraWanSetUplinkFCnt(uint32_t uplinkFCnt);

/*!
 * @fn
 * get UplinkFCnt
 * @return UplinkFCnt
 */
uint32_t AppLoraWanGetUplinkFCnt(void);

void AppLoraWanNvmDataMgmtSetRestoreMode(uint8_t restoreMode);
void AppLoraWanNvmDataMgmtRestore(void);
void AppLoraWanNvmDataMgmtSave(uint32_t notifyMibFlags);

void AppLoraWanNvmDataRead( uint32_t readValFlags, uint32_t *p_readResultFlags );
void AppLoraWanNvmDataWrite( uint32_t writeValFlags, uint32_t *p_writeResultFlags );

#endif /* _LORAWAN_PROC_H_ */
