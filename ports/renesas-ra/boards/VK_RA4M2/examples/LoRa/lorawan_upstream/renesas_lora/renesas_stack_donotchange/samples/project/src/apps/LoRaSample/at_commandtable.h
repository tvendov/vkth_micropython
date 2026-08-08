/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#include "at-command.h"

//-----------------------------------------------------
// prototypes: at_proc.c
//-----------------------------------------------------
/*!
 * @fn
 * at+reset: reset
 */
extern AtResultCode_t AppAtResetAct(void *p);

/*!
 * @fn
 * at+ver: read version
 */
extern AtResultCode_t AppAtVerRead(void *p);

/*!
 * @fn
 * at+save: save settings
 */
extern AtResultCode_t AppAtSaveAct(void *p);

/*!
 * @fn
 * at+load: load settings
 */
extern AtResultCode_t AppAtLoadAct(void *p);

/*!
 * @fn
 * at+join: request activateion (OTAA/ABP)
 */
extern AtResultCode_t AppAtJoinAct(void *p);

/*!
 * @fn
 * at+send: send text data
 */
extern AtResultCode_t AppAtSendAct(void *p);

/*!
 * @fn
 * at+mtype: set MType for tx message
 */
extern AtResultCode_t AppAtMtypeSet(void *p);

/*!
 * @fn
 * at+mtype: read MType
 */
extern AtResultCode_t AppAtMtypeRead(void *p);

/*!
 * @fn
 * at+devuei: set DevEUI
 */
extern AtResultCode_t AppAtDevEUISet(void *p);

/*!
 * @fn
 * at+devuei: read DevEUI
 */
extern AtResultCode_t AppAtDevEUIRead(void *p);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * at+devaddr: set DevAddr
 */
extern AtResultCode_t AppAtDevAddrSet(void *p);
#else
#define AppAtDevAddrSet     NULL
#endif

/*!
 * @fn
 * at+devaddr: read DevAddr
 */
extern AtResultCode_t AppAtDevAddrRead(void *p);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * at+netid: set NetID
 */
extern AtResultCode_t AppAtNetIDSet(void *p);
#else
#define AppAtNetIDSet       NULL
#endif

/*!
 * @fn
 * at+netid: read NetID
 */
extern AtResultCode_t AppAtNetIDRead(void *p);

/*!
 * @fn
 * at+appeui: set AppEUI
 */
extern AtResultCode_t AppAtAppEUISet(void *p);

/*!
 * @fn
 * at+appeui: read AppEUI
 */
extern AtResultCode_t AppAtAppEUIRead(void *p);

/*!
 * @fn
 * at+appkey: set AppKey
 */
extern AtResultCode_t AppAtAppKeySet(void *p);

/*!
 * @fn
 * at+appkey: read AppKey
 */
extern AtResultCode_t AppAtAppKeyRead(void *p);

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * at+nwkskey: set NwkSKey
 */
extern AtResultCode_t AppAtNwkSKeySet(void *p);

/*!
 * @fn
 * at+nwkskey: read NwkSKey
 */
extern AtResultCode_t AppAtNwkSKeyRead(void *p);

/*!
 * @fn
 * at+appskey: set AppSKey
 */
extern AtResultCode_t AppAtAppSKeySet(void *p);

/*!
 * @fn
 * at+appskey: read AppSKey
 */
extern AtResultCode_t AppAtAppSKeyRead(void *p);
#endif

/*!
 * @fn
 * at+actmode: set activation mode
 */
extern AtResultCode_t AppAtActModeSet(void *p);

/*!
 * @fn
 * at+actmode: set activation mode
 */
extern AtResultCode_t AppAtActModeRead(void *p);

/*!
 * @fn
 * at+class: set operation class
 */
extern AtResultCode_t AppAtClassSet(void *p);

/*!
 * @fn
 * at+class: read current operation class
 */
extern AtResultCode_t AppAtClassRead(void *p);

/*!
 * @fn
 * at+region: set region
 */
extern AtResultCode_t AppAtRegionSet(void *p);

/*!
 * @fn
 * at+region: read current region
 */
extern AtResultCode_t AppAtRegionRead(void *p);

/*!
 * @fn
 * at+hexsend: send hex data
 */
extern AtResultCode_t AppAtSendHexAct(void *p);

/*!
 * @fn
 * at+adr: set ADR mode
 */
extern AtResultCode_t AppAtAdrSet(void *p);

/*!
 * @fn
 * at+adr: read current ADR mode
 */
extern AtResultCode_t AppAtAdrRead(void *p);

/*!
 * @fn
 * at+rssi: set RSSI mode
 */
extern AtResultCode_t AppAtRssiSet(void *p);

/*!
 * @fn
 * at+rssi: read current RSSI mode
 */
extern AtResultCode_t AppAtRssiRead(void *p);

/*!
 * @fn
 * at+rx1delay: set delay for rx window 1
 */
extern AtResultCode_t AppAtRx1DelaySet(void *p);

/*!
 * @fn
 * at+rx1delay: read delay for rx window 1
 */
extern AtResultCode_t AppAtRx1DelayRead(void *p);

/*!
 * @fn
 * at+dr: set data rate for tx messages
 */
extern AtResultCode_t AppAtDRSet(void *p);

/*!
 * @fn
 * at+dr: read data rate for tx message
 */
extern AtResultCode_t AppAtDRRead(void *p);

/*!
 * @fn
 * at+linkchk: require send LinkCheckReq
 */
extern AtResultCode_t AppAtLinkchkAct(void *p);

/*!
 * @fn
 * at+fport: set FPort to send data messages
 */
extern AtResultCode_t AppAtFPortSet(void *p);

/*!
 * @fn
 * at+fport: read FPort to send data messages
 */
extern AtResultCode_t AppAtFPortRead(void *p);

/*!
 * @fn
 * at+dcycle: set duty cycle mode
 */
extern AtResultCode_t AppAtDCycleSet(void *p);

/*!
 * @fn
 * at+dcycle: read duty cycle mode
 */
extern AtResultCode_t AppAtDCycleRead(void *p);

/*!
 * @fn
 * at+devtime: request send DeviceTimeReq
 */
extern AtResultCode_t AppAtDevTimeAct(void *p);

#ifdef LORAMAC_CLASSB_ENABLED
/*!
 * @fn
 * at+devtime: request send BeaconAcquisition
 */
extern AtResultCode_t AppAtBconAcqAct(void *p);

/*!
 * @fn
 * at+devtime: request send PingSlotInfoReq
 */
extern AtResultCode_t AppAtPngSlInfoAct(void *p);

/*!
 * @fn
 * at+devtime: request send BeaconTimingReq
 */
extern AtResultCode_t AppAtBconTimAct(void *p);

/*!
 * @fn
 * at+pngperiod: set Periodicity 
 */
extern AtResultCode_t AppAtPngSlPeriodSet(void *p);

/*!
 * @fn
 * at+pngperiod: read Periodicity 
 */
extern AtResultCode_t AppAtPngSlPeriodRead(void *p);
#endif

/*!
 * @fn
 * at+cert: set certification mode
 */
extern AtResultCode_t AppAtCertModeSet(void *p);

/*!
 * @fn
 * at+cert: read certification mode
 */
extern AtResultCode_t AppAtCertModeRead(void *p);

/*!
 * @fn
 * at+chdefmask: set channels default mask
 */
extern AtResultCode_t AppAtChannelsDefaultMaskSet(void *p);

/*!
 * @fn
 * at+chdefmask: read channels default mask
 */
extern AtResultCode_t AppAtChannelsDefaultMaskRead(void *p);

#if (LORAMAC_MAX_MC_CTX > 0)
/*!
 * @fn
 * at+genappkey: set GenAppKey
 */
extern AtResultCode_t AppAtGenAppKeySet(void *p);

/*!
 * @fn
 * at+genappkey: read GenAppKey
 */
extern AtResultCode_t AppAtGenAppKeyRead(void *p);
#endif

#if defined(DEBUG_LORAMAC)
extern AtResultCode_t AppAtDebugSet(void *p);
extern AtResultCode_t AppAtDebugRead(void *p);
#endif

/*!
 * @fn
 * at+devnonce: set DevNonce
 */
extern AtResultCode_t AppAtDevNonceSet(void *p);

/*!
 * @fn
 * at+devnonce: reada DevNonce 
 */
extern AtResultCode_t AppAtDevNonceRead(void *p);

/*!
 * @fn
 * at+appnonce: set AppNonce
 */
extern AtResultCode_t AppAtAppNonceSet(void *p);

/*!
 * @fn
 * at+appnonce: read AppNonce
 */
extern AtResultCode_t AppAtAppNonceRead(void *p);


#if defined(DEBUG_AT_COMMAND_EXPERIMENTAL)
/*!
 * @fn
 * at+ch: read channels
 */
extern AtResultCode_t AppAtAppChannelsRead(void *p);

/*!
 * @fn
 * at+chmask: read channel mask
 */
extern AtResultCode_t AppAtAppChannelMaskRead(void *p);

/*!
 * @fn
 * at+nbtrans: set NbTrans 
 */
extern AtResultCode_t AppAtNbTransSet(void *p);

/*!
 * @fn
 * at+nbtrans: read NbTrans 
 */
extern AtResultCode_t AppAtNbTransRead(void *p);

/*!
 * @fn
 * at+txpower: set TxPower 
 */
extern AtResultCode_t AppAtTxPowerSet(void *p);

/*!
 * @fn
 * at+txpower: read TxPower 
 */
extern AtResultCode_t AppAtTxPowerRead(void *p);

/*!
 * @fn
 * at+maxdcycle: set MaxDutyCycle
 */
extern AtResultCode_t AppAtMaxDCycleSet(void *p);

/*!
 * @fn
 * at+maxdcycle: read MaxDutyCycle
 */
extern AtResultCode_t AppAtMaxDCycleRead(void *p);

/*!
 * @fn
 * at+rx1droffset set Rx1DROffset
 */
extern AtResultCode_t AppAtRx1DrOffsetSet(void *p);

/*!
 * @fn
 * at+rx1droffset: read Rx1DROffset
 */
extern AtResultCode_t AppAtRx1DrOffsetRead(void *p);

/*!
 * @fn
 * at+rx2freq set Rx2Freq
 */
extern AtResultCode_t AppAtRx2FreqSet(void *p);

/*!
 * @fn
 * at+rx2freq: read Rx2Freq
 */
extern AtResultCode_t AppAtRx2FreqRead(void *p);

/*!
 * @fn
 * at+rx2dr set Rx2DataRate
 */
extern AtResultCode_t AppAtRx2DrSet(void *p);

/*!
 * @fn
 * at+rx2dr: read Rx2DataRate
 */
extern AtResultCode_t AppAtRx2DrRead(void *p);

/*!
 * @fn
 * at+maxeirp set MaxEIRP
 */
extern AtResultCode_t AppAtMaxEirpSet(void *p);

/*!
 * @fn
 * at+maxeirp: read MaxEIRP
 */
extern AtResultCode_t AppAtMaxEirpRead(void *p);

/*!
 * @fn
 * at+downdwell set DownlinkDwellTime
 */
extern AtResultCode_t AppAtDownlinkDwellTimeSet(void *p);

/*!
 * @fn
 * at+downdwell: read DownlinkDwellTime
 */
extern AtResultCode_t AppAtDownlinkDwellTimeRead(void *p);

/*!
 * @fn
 * at+updwell set UplinkDwellTime
 */
extern AtResultCode_t AppAtUplinkDwellTimeSet(void *p);

/*!
 * @fn
 * at+updwell: read UplinkDwellTime
 */
extern AtResultCode_t AppAtUplinkDwellTimeRead(void *p);

/*!
 * @fn
 * at+pngsldr set PingSlotDataRate
 */
extern AtResultCode_t AppAtPngSlDrSet(void *p);

/*!
 * @fn
 * at+pngsldr: read PingSlotDataRate
 */
extern AtResultCode_t AppAtPngSlDrRead(void *p);
#endif

/*!
 * @fn
 * at+downfcnt set DownlinkFCnt
 */
extern AtResultCode_t AppAtDownlinkFCntSet(void *p);

/*!
 * @fn
 * at+downfcnt: read DownlinkFCnt
 */
extern AtResultCode_t AppAtDownlinkFCntRead(void *p);

/*!
 * @fn
 * at+upfcnt set UplinkFCnt
 */
extern AtResultCode_t AppAtUplinkFCntSet(void *p);

/*!
 * @fn
 * at+upfcnt: read UplinkFCnt
 */
extern AtResultCode_t AppAtUplinkFCntRead(void *p);


#if defined(APP_COMPLIANCE)
//-----------------------------------------------------
// prototypes: app_compliance_at_proc.c
//-----------------------------------------------------
extern AtResultCode_t AppComplianceAtModeAct(void *p);
extern AtResultCode_t AppComplianceAtModeSet(void *p);
extern AtResultCode_t AppComplianceAtModeRead(void *p);
#endif



#if (AT_EXTCMD_TAB_ARRAYSIZE == 0)
//-----------------------------------------------------
// AT command table (ROM table)
//-----------------------------------------------------
const AtExtendTab_t AppAtCommands[] =
{
    /* set,  get,  act,  len, *name */
    { AppAtResetAct,               NULL,                         NULL,               6, (int8_t *)"+RESET"     },
    { NULL,                        AppAtVerRead,                 NULL,               4, (int8_t *)"+VER"       },
    { NULL,                        NULL,                         AppAtSaveAct,       5, (int8_t *)"+SAVE"      },
    { AppAtLoadAct,                NULL,                         AppAtLoadAct,       5, (int8_t *)"+LOAD"      },
    { AppAtDevEUISet,              AppAtDevEUIRead,              NULL,               7, (int8_t *)"+DEVEUI"    },
    { AppAtClassSet,               AppAtClassRead,               NULL,               6, (int8_t *)"+CLASS"     },
    { AppAtDevAddrSet,             AppAtDevAddrRead,             NULL,               8, (int8_t *)"+DEVADDR"   },
    { AppAtNetIDSet,               AppAtNetIDRead,               NULL,               6, (int8_t *)"+NETID"     },
    { AppAtAppEUISet,              AppAtAppEUIRead,              NULL,               7, (int8_t *)"+APPEUI"    },
    { AppAtAppKeySet,              AppAtAppKeyRead,              NULL,               7, (int8_t *)"+APPKEY"    },
    { AppAtActModeSet,             AppAtActModeRead,             NULL,               8, (int8_t *)"+ACTMODE"   },
    { AppAtSendAct,                NULL,                         NULL,               5, (int8_t *)"+SEND"      },
    { AppAtMtypeSet,               AppAtMtypeRead,               NULL,               6, (int8_t *)"+MTYPE"     },
    { AppAtJoinAct,                NULL,                         AppAtJoinAct,       5, (int8_t *)"+JOIN"      },
    { AppAtRegionSet,              AppAtRegionRead,              NULL,               7, (int8_t *)"+REGION"    },
    { AppAtSendHexAct,             NULL,                         NULL,               8, (int8_t *)"+SENDHEX"   },
    { AppAtAdrSet,                 AppAtAdrRead,                 NULL,               4, (int8_t *)"+ADR"       },
    { AppAtRssiSet,                AppAtRssiRead,                NULL,               5, (int8_t *)"+RSSI"      },
    { AppAtRx1DelaySet,            AppAtRx1DelayRead,            NULL,               9, (int8_t *)"+RX1DELAY"  },
    { AppAtDRSet,                  AppAtDRRead,                  NULL,               3, (int8_t *)"+DR"        },
    { NULL,                        NULL,                         AppAtLinkchkAct,    8, (int8_t *)"+LINKCHK"   },
    { AppAtFPortSet,               AppAtFPortRead,               NULL,               6, (int8_t *)"+FPORT"     },
    { AppAtDCycleSet,              AppAtDCycleRead,              NULL,               7, (int8_t *)"+DCYCLE"    },
    { NULL,                        NULL,                         AppAtDevTimeAct,    8, (int8_t *)"+DEVTIME"   },
    { AppAtChannelsDefaultMaskSet, AppAtChannelsDefaultMaskRead, NULL,              10, (int8_t *)"+CHDEFMASK" },
    { AppAtDevNonceSet,            AppAtDevNonceRead,            NULL,               9, (int8_t *)"+DEVNONCE"  },
    { AppAtAppNonceSet,            AppAtAppNonceRead,            NULL,               9, (int8_t *)"+APPNONCE"  },
    { AppAtDownlinkFCntSet,        AppAtDownlinkFCntRead,        NULL,               9, (int8_t *)"+DOWNFCNT"  },
    { AppAtUplinkFCntSet,          AppAtUplinkFCntRead,          NULL,               7, (int8_t *)"+UPFCNT"    },
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    { AppAtNwkSKeySet,             AppAtNwkSKeyRead,             NULL,               8, (int8_t *)"+NWKSKEY"   },
    { AppAtAppSKeySet,             AppAtAppSKeyRead,             NULL,               8, (int8_t *)"+APPSKEY"   },
#endif
#ifdef LORAMAC_CLASSB_ENABLED
    { NULL,                NULL,                 AppAtBconAcqAct,    8, (int8_t *)"+BCONACQ"     },
    { NULL,                NULL,                 AppAtPngSlInfoAct, 10, (int8_t *)"+PNGSLINFO"   },
    { NULL,                NULL,                 AppAtBconTimAct,    8, (int8_t *)"+BCONTIM"     },
    { AppAtPngSlPeriodSet, AppAtPngSlPeriodRead, NULL,              12, (int8_t *)"+PNGSLPERIOD" },
#endif
#if (LORAMAC_MAX_MC_CTX > 0)
    { AppAtGenAppKeySet, AppAtGenAppKeyRead, NULL, 10, (int8_t *)"+GENAPPKEY" },
#endif
#if defined(DEBUG_LORAMAC)
    { AppAtDebugSet, AppAtDebugRead, NULL, 6, (int8_t *)"+DEBUG" },
#endif
#if defined(DEBUG_AT_COMMAND_EXPERIMENTAL)
    { NULL,                      AppAtAppChannelsRead,       NULL,  3, (int8_t *)"+CH"          },
    { NULL,                      AppAtAppChannelMaskRead,    NULL,  7, (int8_t *)"+CHMASK"      },
    { AppAtNbTransSet,           AppAtNbTransRead,           NULL,  8, (int8_t *)"+NBTRANS"     },
    { AppAtTxPowerSet,           AppAtTxPowerRead,           NULL,  8, (int8_t *)"+TXPOWER"     },
    { AppAtMaxDCycleSet,         AppAtMaxDCycleRead,         NULL, 10, (int8_t *)"+MAXDCYCLE"   },
    { AppAtRx1DrOffsetSet,       AppAtRx1DrOffsetRead,       NULL, 12, (int8_t *)"+RX1DROFFSET" },
    { AppAtRx2FreqSet,           AppAtRx2FreqRead,           NULL,  8, (int8_t *)"+RX2FREQ"     },
    { AppAtRx2DrSet,             AppAtRx2DrRead,             NULL,  6, (int8_t *)"+RX2DR"       },
    { AppAtMaxEirpSet,           AppAtMaxEirpRead,           NULL,  8, (int8_t *)"+MAXEIRP"     },
    { AppAtDownlinkDwellTimeSet, AppAtDownlinkDwellTimeRead, NULL, 10, (int8_t *)"+DOWNDWELL"   },
    { AppAtUplinkDwellTimeSet,   AppAtUplinkDwellTimeRead,   NULL,  8, (int8_t *)"+UPDWELL"     },
    { AppAtPngSlDrSet,           AppAtPngSlDrRead,           NULL,  8, (int8_t *)"+PNGSLDR"     },
#endif
#if defined(APP_COMPLIANCE)
    { AppComplianceAtModeSet, AppComplianceAtModeRead, AppComplianceAtModeAct, 11, (int8_t *)"+COMPLIANCE" },
#endif
};
#endif
