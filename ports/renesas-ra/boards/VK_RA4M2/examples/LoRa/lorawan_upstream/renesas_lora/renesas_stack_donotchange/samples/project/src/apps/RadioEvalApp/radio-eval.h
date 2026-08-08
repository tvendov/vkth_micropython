/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef RADIO_EVAL_H
#define RADIO_EVAL_H

#include "board.h"
#include "at-command.h"

/*!
 * Version
 */
#define APP_REVAL_VERMAJOR  (4)
#define APP_REVAL_VERMINOR  (90)
#define APP_REVAL_VERSION   (APP_REVAL_VERMAJOR*100 + APP_REVAL_VERMINOR)
#define APP_REVAL_VERINFO   ("20251128")

/*!
 * Global definition
 */
#define APP_RADIO_GFSK                  (0)
#define APP_RADIO_LORA                  (1)
#define APP_RADIO_FREQ_LO               (426000000UL)     // Hz
#define APP_RADIO_FREQ_HI               (928000000UL)     // Hz
#define APP_RADIO_RXGAIN_POWERSAVING    (0)
#define APP_RADIO_RXGAIN_BOOSTED        (1)
#define APP_SLEEP_MODE_COLD             (0)
#define APP_SLEEP_MODE_WARM             (1)
#define APP_EVT_UART0_RECV              (0x00000001)
#define APP_EVT_ATCMD_RESP              (0x00000002)
#define APP_EVT_STATE_TRAN              (0x00000004)
#define APP_FRAMETYPE_PER               (1)
#define APP_FRAMETYPE_PN9               (2)
#define APP_FRAMETYPE_ANY               (3)
#define APP_FRAMETYPE_EUI               (4)
#define APP_FRAMETYPE_EXT               (5)
/*!
 * default settings
 */
#define APP_RADIO_FREQ                  (923000000UL) // Hz
#define APP_RADIO_PAYLOADLEN            (16)        // bytes
#define APP_RADIO_TXPOWER               (0)         // dBm
#define APP_RADIO_TXTIMEOUT             (1000UL)    // ms
#define APP_RADIO_RXTIMEOUT             (1000UL)    // ms
#define APP_RADIO_GFSK_FDEV             (25000UL)   // Hz
#define APP_RADIO_GFSK_BW               (50000UL)   // Hz
#define APP_RADIO_GFSK_DR               (50000UL)   // Hz
#define APP_RADIO_GFSK_PREAMBLELEN      (5)         // bytes
#define APP_RADIO_GFSK_FIXLEN           (false)
#define APP_RADIO_GFSK_CRCON            (true)
#define APP_RADIO_GFSK_RXCONTINUOUS     (true)
#define APP_RADIO_GFSK_SYMBTIMEOUT      (5)         // bytes
#define APP_RADIO_LORA_BW               (0UL)
#define APP_RADIO_LORA_SF               (7UL)
#define APP_RADIO_LORA_CODERATE         (1)
#define APP_RADIO_LORA_PREAMBLELEN      (8)         // symbol
#define APP_RADIO_LORA_FIXLEN           (false)
#define APP_RADIO_LORA_CRCON            (true)
#define APP_RADIO_LORA_IQINVERTED       (false)
#define APP_RADIO_LORA_SYMBTIMEOUT      (5)         // symbol
#define APP_RADIO_LORA_RXCONTINUOUS     (true)
#define APP_RADIO_LORA_PUBLICNETWORK    (false)
#define APP_RADIO_PIB_RSSIOFFSET        (0)         // dBm
#define APP_RADIO_PIB_XTALXTATRIM       (0x13)
#define APP_RADIO_PIB_XTALXTBTRIM       (0x13)
#define APP_RADIO_PIB_GAINBOOSTEED      (false)
#define APP_TXINTVL_DUTYCYCLE           (3000UL)    // ms
#define APP_TXCCA_ENABLE                (false)
#define APP_TXCCA_RSSITHRESH            (-80)       // dBm
#define APP_TXCCA_SCANDURATION          (5)         // ms
#define APP_PN9_DATA_LEN                (255)
#define APP_DEFAULT_DEVEUI              {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF}

/*!
 * default reception buffer array size
 */
#if defined(APP_PACKETDATA_ARRAYSIZE_CONFIG)
    #define APP_PACKETDATA_ARRAYSIZE    (APP_PACKETDATA_ARRAYSIZE_CONFIG)
#else
    #define APP_PACKETDATA_ARRAYSIZE    (255)
#endif/*APP_PACKETDATA_ARRAYSIZE_CONFIG*/

/*!
 * default at-command reception buffer array size
 */
#if defined(CMDBUFFER_ARRAYSIZE_CONFIG)
    #define AT_CMDBUFFER_ARRAYSIZE      CMDBUFFER_ARRAYSIZE_CONFIG
#else
    #define AT_CMDBUFFER_ARRAYSIZE      (510+32)
#endif

/*!
 * application states
 */
typedef enum AppStatesTag {
    APP_IDLE  = 0,                          /* 0x00 */
    APP_INIT  = 1,                          /* 0x01 */
    APP_RESET = 2,                          /* 0x02 */
    APP_RX_CONFIG,                          /* 0x03 */
    APP_RX,                                 /* 0x04 */
    APP_RX_TIMEOUT,                         /* 0x05 */
    APP_RX_ERROR,                           /* 0x06 */
    APP_RX_RUNNING,                         /* 0x07 */
    APP_RX_STOP,                            /* 0x08 */
    APP_TX_CONFIG,                          /* 0x09 */
    APP_TX,                                 /* 0x0a */
    APP_TX_TIMEOUT,                         /* 0x0b */
    APP_TX_RUNNING,                         /* 0x0c */
    APP_TX_WAITING,                         /* 0x0d */
    APP_TXCW,                               /* 0x0e */
    APP_TXCP,                               /* 0x0f */
    APP_TX_DONE,                            /* 0x10 */
    APP_TX_STOP,                            /* 0x11 */
    APP_CAD,                                /* 0x12 */
    APP_CAD_DONE,                           /* 0x13 */
    APP_LOWPOWER,                           /* 0x14 */
    APP_SLEEP,                              /* 0x15 */
    APP_WAKEUP,                             /* 0x16 */
    APP_TXCX_RUNNING,                       /* 0x17 */
} AppStates_t;

/*!
 * byte order conversion
 */
typedef struct ByteOrderTag {
    union {
        uint32_t    n;
        uint8_t     b[4];
    };
} ByteOrder_t;

/*!
 * RX
 */
typedef struct TxCbTag {
    uint32_t        txPktNum;
    uint32_t        txPktCount;
    uint32_t        txSeqNum;
    uint32_t        txDutyCycle;
    uint32_t        ccaScanDuration;
    int16_t         ccaRssiThresh;
    bool            ccaEnable;
    bool            isVerbose;
} TxCb_t;

/*!
 * TX
 */
typedef struct RxCbTag {
    uint32_t        rxPktOkCount;           /*  OkPkt       */
    uint32_t        rxPktNgCount;           /*  NgPkt       */
    uint32_t        rxPktToCount;           /*  Timeout     */
    uint32_t        rxPktCount;             /*  TotalPkt    */
    uint32_t        rxBitsOkCount;          /*  OkBits      */
    uint32_t        rxBitsNgCount;          /*  NgBits      */
    uint32_t        rxBitsToCount;          /*  Timeout     */
    uint32_t        rxBitsCount;            /*  TotalBits   */
    uint32_t        pktBitsOkCount;
    uint32_t        pktBitsNgCount;
    uint32_t        pktBitsCount;
    uint32_t        rssiCount;
    int16_t         rssiMin;
    int16_t         rssiMax;
    int32_t         rssiMean;
    uint32_t        snrCount;
    int16_t         snrMin;
    int16_t         snrMax;
    int32_t         snrMean;
    bool            isVerbose;
} RxCb_t;

/*!
 * Power Saveing
 */
typedef struct PowerSaveCbTag {
    uint32_t        period;                 // sec
    uint8_t         mode;
} PowerSaveCb_t;

/*!
 * Spectrum Analyzer
 */
typedef struct SAnalyzerCbTag {
    uint32_t        freqStart;
    uint32_t        freqStop;
    uint32_t        freqOffset;
    uint8_t         freqNb;
    uint16_t        scanTimes;
    uint8_t         delayMs;
} SAnalyzerCb_t;

/*!
 * FSK
 */
typedef struct FskControlBlockTag {
    uint32_t        dataRate;
    uint32_t        bandWidth;
    uint32_t        fDev;
    uint16_t        preambleLen;
    uint16_t        symbTimeout;
    bool            fixLen;
    bool            crcOn;
    bool            rxContinuous;
} FskCb_t;

/*!
 * LoRa
 */
typedef struct LoRaControlBlockTag {
    uint32_t        spreadFactor;
    uint32_t        bandWidth;
    uint16_t        preambleLen;
    uint16_t        symbTimeout;
    uint8_t         codeRate;
    bool            fixLen;
    bool            crcOn;
    bool            iqInverted;
    bool            rxContinuous;
    bool            publicNetwork;
} LoRaCb_t;

/*!
 * PIB
 */
typedef struct PibDataTag {
    int8_t          rssiOffset;
    uint32_t        ccaBandWidth;
    uint8_t         xtalXtaTrim;
    uint8_t         xtalXtbTrim;
    bool            callRxDoneInPayloadCrcError;
    bool            gainBoosted;
    bool            radioCfgCheckEnable;
    uint8_t         region;
} PibData_t;

/*!
 * Radio
 */
typedef struct RadioControlBlockTag {
    RadioPacketTypes_t modem;
    uint32_t        freq;
    int8_t          txPower;
    uint8_t         payloadLen;
    uint32_t        txTimeout;
    uint32_t        rxTimeout;
    LoRaCb_t        lora;
    FskCb_t         fsk;
    PibData_t       pib;
    SAnalyzerCb_t   sa;
} RadioCb_t;

/*!
 * Packet control
 */
typedef struct PacketDataControlBlockTag {
    BufferCb_t      bCb;
    int8_t          pktData [ APP_PACKETDATA_ARRAYSIZE ];
} PacketCb_t;

/*!
 * Packet data
 */
typedef struct PacketDataTag {
    PacketCb_t      txPktCb;        // for transmission and verification
    PacketCb_t      rxPktCb;        // last received packet data
} PacketData_t;

/*!
 * Device info
 */
typedef struct NodeInfoTag {
    uint8_t         devEui[8];
} NodeInfo_t;

/*!
 * Application
 */
typedef struct ApplicationControlBlockTag {
    uint32_t        version;
    uint32_t        event;
    uint8_t         appPacketDataType;
    AppStates_t     state;
    RadioCb_t       radioCb;
    TxCb_t          txCb;
    RxCb_t          rxCb;
    PacketCb_t      anyPktCb;
    PowerSaveCb_t   powerSaveCb;
    NodeInfo_t      nodeInfo;
} AppCb_t;


/*
 * Function Prototype
 */
void AppRadioInit (void);
void AppRadioSetRxConfig (RadioModems_t modem);
void AppRadioSetTxConfig (RadioModems_t modem);
AppStates_t AppGetState (AppCb_t *);
void AppSetState (AppCb_t *, AppStates_t);
uint32_t AppGetTranState (AppCb_t *, AppStates_t *);
uint32_t AppSetTranState (AppCb_t *, AppStates_t);
uint32_t AppSetEvent (AppCb_t *, uint32_t);
bool AppMatchPacketType (RadioPacketTypes_t);
void AppClearBuffer (BufferCb_t *, int8_t *, uint16_t);
void AppSetupPacketData (uint8_t);
void AppReloadDefaultCb (void);
void AppSaveCb (void);
void AppUpdateSensorData(bool);


/*!
 * Constant table that stores the application control block as default
 */
static const AppCb_t appDefaultCb = {
    .version = APP_REVAL_VERSION,
    .event = 0UL,
    .appPacketDataType = APP_FRAMETYPE_PER,
    .state = APP_IDLE,
    .radioCb.modem = PACKET_TYPE_LORA,
    .radioCb.freq = APP_RADIO_FREQ,
    .radioCb.txPower = APP_RADIO_TXPOWER,
    .radioCb.txTimeout = APP_RADIO_TXTIMEOUT,
    .radioCb.rxTimeout = APP_RADIO_RXTIMEOUT,
    .radioCb.payloadLen = APP_RADIO_PAYLOADLEN,
    .radioCb.lora.spreadFactor = APP_RADIO_LORA_SF,
    .radioCb.lora.bandWidth = APP_RADIO_LORA_BW,
    .radioCb.lora.preambleLen = APP_RADIO_LORA_PREAMBLELEN,
    .radioCb.lora.symbTimeout = APP_RADIO_LORA_SYMBTIMEOUT,
    .radioCb.lora.codeRate = APP_RADIO_LORA_CODERATE,
    .radioCb.lora.fixLen = APP_RADIO_LORA_FIXLEN,
    .radioCb.lora.crcOn = APP_RADIO_LORA_CRCON,
    .radioCb.lora.iqInverted = APP_RADIO_LORA_IQINVERTED,
    .radioCb.lora.rxContinuous = APP_RADIO_LORA_RXCONTINUOUS,
    .radioCb.lora.publicNetwork = APP_RADIO_LORA_PUBLICNETWORK,
    .radioCb.fsk.dataRate = APP_RADIO_GFSK_DR,
    .radioCb.fsk.bandWidth = APP_RADIO_GFSK_BW,
    .radioCb.fsk.fDev = APP_RADIO_GFSK_FDEV,
    .radioCb.fsk.preambleLen = APP_RADIO_GFSK_PREAMBLELEN,
    .radioCb.fsk.symbTimeout = APP_RADIO_GFSK_SYMBTIMEOUT,
    .radioCb.fsk.fixLen = APP_RADIO_GFSK_FIXLEN,
    .radioCb.fsk.crcOn = APP_RADIO_GFSK_CRCON,
    .radioCb.fsk.rxContinuous = APP_RADIO_GFSK_RXCONTINUOUS,
    .radioCb.pib.rssiOffset = APP_RADIO_PIB_RSSIOFFSET,
    .radioCb.pib.ccaBandWidth = 0,
    .radioCb.pib.xtalXtaTrim = APP_RADIO_PIB_XTALXTATRIM,
    .radioCb.pib.xtalXtbTrim = APP_RADIO_PIB_XTALXTBTRIM,
    .radioCb.pib.callRxDoneInPayloadCrcError = true,
    .radioCb.pib.gainBoosted = APP_RADIO_PIB_GAINBOOSTEED,
    .radioCb.pib.radioCfgCheckEnable = false,
    .radioCb.pib.region =
#if defined(RADIO_CFG_EU_ENABLED)
                RADIO_CFG_EU,
#elif defined(RADIO_CFG_IN_ENABLED)
                RADIO_CFG_IN,
#elif defined(RADIO_CFG_AS1_ENABLED)
                RADIO_CFG_AS1,
#elif defined(RADIO_CFG_AS2_ENABLED)
                RADIO_CFG_AS2,
#elif defined(RADIO_CFG_AS3_ENABLED)
                RADIO_CFG_AS3,
#elif defined(RADIO_CFG_AS4_ENABLED)
                RADIO_CFG_AS4,
#elif defined(RADIO_CFG_US_ENABLED)
                RADIO_CFG_US,
#elif defined(RADIO_CFG_AU_ENABLED)
                RADIO_CFG_AU,
#elif defined(RADIO_CFG_KR_ENABLED)
                RADIO_CFG_KR,
#elif defined(RADIO_CFG_JP_ENABLED)
                RADIO_CFG_JP,
#elif defined(RADIO_CFG_JP_LDC_ENABLED)
                RADIO_CFG_JP_LDC,
#endif
    .radioCb.sa.freqStart = APP_RADIO_FREQ,
    .radioCb.sa.freqStop = APP_RADIO_FREQ + (1000UL * 100),
    .radioCb.sa.freqOffset = 1000UL,
    .radioCb.sa.freqNb = 100,
    .radioCb.sa.scanTimes = 1,
    .radioCb.sa.delayMs = 0,
    .txCb.txPktNum = 1UL,
    .txCb.txPktCount = 0UL,
    .txCb.txSeqNum = 1UL,
    .txCb.txDutyCycle = APP_TXINTVL_DUTYCYCLE,
    .txCb.ccaScanDuration = APP_TXCCA_SCANDURATION,
    .txCb.ccaRssiThresh = APP_TXCCA_RSSITHRESH,
    .txCb.ccaEnable = APP_TXCCA_ENABLE,
    .txCb.isVerbose = true,
    .rxCb.rxPktOkCount = 0UL,
    .rxCb.rxPktNgCount = 0UL,
    .rxCb.rxPktToCount = 0UL,
    .rxCb.rxPktCount = 0UL,
    .rxCb.rxBitsOkCount	= 0UL,
    .rxCb.rxBitsNgCount = 0UL,
    .rxCb.rxBitsToCount = 0UL,
    .rxCb.rxBitsCount = 0UL,
    .rxCb.pktBitsOkCount = 0UL,
    .rxCb.pktBitsNgCount = 0UL,
    .rxCb.pktBitsCount = 0UL,
    .rxCb.rssiCount = 0UL,
    .rxCb.rssiMin = 0,
    .rxCb.rssiMax = INT8_MIN,
    .rxCb.rssiMean = 0L,
    .rxCb.snrCount = 0UL,
    .rxCb.snrMin = INT8_MAX,
    .rxCb.snrMax = INT8_MIN,
    .rxCb.snrMean = 0L,
    .rxCb.isVerbose = true,
    .anyPktCb.bCb.pos = 0,
    .anyPktCb.bCb.arraySize = APP_PACKETDATA_ARRAYSIZE,
    .anyPktCb.bCb.locked = 0,
    .anyPktCb.bCb.options = 0,
    .anyPktCb.pktData = {0,},
    .powerSaveCb.period = 10UL,
    .powerSaveCb.mode = APP_SLEEP_MODE_WARM,
    .nodeInfo.devEui = APP_DEFAULT_DEVEUI,
};

#endif/*RADIO_EVAL_H*/
