/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrivateLoRa.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifndef __PRIVATELORA_H__
#define __PRIVATELORA_H__

#include "PrivateLoRaConfig.h"
#include "PrvLoRaMacDebug.h"

/*--------*/
/* define */
#define PRVLORA_MACADDR_SIZE        8
#define PRVLORA_CRYPTKEY_SIZE       16
#define PRVLORA_TXDATA_MAXSIZE      238     // Frame(max255)-MHDR(nosec;17)

// updated remote device info
#define PRVLORA_REMOTEDEV_UPDATED_SESSION_KEY   0x0001
#define PRVLORA_REMOTEDEV_UPDATED_FCNT_TX       0x0002
#define PRVLORA_REMOTEDEV_UPDATED_FCNT_RX       0x0004

// Radio config
#ifdef LORACOMBO_ENABLED
    #ifndef RADIO_CFG_AS_ENABLED
        #ifdef REGION_AS923
        #define RADIO_CFG_AS_ENABLED
        #endif
    #endif
    #ifndef RADIO_CFG_EU_ENABLED
        #ifdef REGION_EU868
        #define RADIO_CFG_EU_ENABLED
        #endif
    #endif
    #ifndef RADIO_CFG_US_ENABLED
        #ifdef REGION_US915
        #define RADIO_CFG_US_ENABLED
        #endif
    #endif
    #ifndef RADIO_CFG_AU_ENABLED
        #ifdef REGION_AU915
        #define RADIO_CFG_AU_ENABLED
        #endif
    #endif
    #ifndef RADIO_CFG_IN_ENABLED
        #ifdef REGION_IN865
        #define RADIO_CFG_IN_ENABLED
        #endif
    #endif
    #ifndef RADIO_CFG_KR_ENABLED
        #ifdef REGION_KR920
        #define RADIO_CFG_KR_ENABLED
        #endif
    #endif
#endif

/*----------------*/
/* typedef (enum) */
// Status for PrivateLoRa
typedef enum _PrvLoRaStatus_t
{
    PRVLORA_STATUS_OK = 0,
    PRVLORA_STATUS_ERROR,
    PRVLORA_STATUS_BUSY,
    PRVLORA_STATUS_INACTIVE,
    PRVLORA_STATUS_PARAMETER_INVALID,
    PRVLORA_STATUS_REQUSET_INVALID,
    PRVLORA_STATUS_NO_REMOTE_DEVICE_ENTRY,
    PRVLORA_STATUS_NOT_SUPPORTED,
    PRVLORA_STATUS_SERVICE_UNKNOWN,
    PRVLORA_STATUS_IB_ATTRIBUTE_INVALID,
    PRVLORA_STATUS_LENGTH_ERROR,
    PRVLORA_STATUS_COMMAND_ERROR,
    PRVLORA_STATUS_INSUFFICIENT_MEMORY,
    PRVLORA_STATUS_DATARATE_INVALID,
    PRVLORA_STATUS_CHANNEL_INVALID,
    PRVLORA_STATUS_RADIO_ERROR,
    PRVLORA_STATUS_RADIO_CHANNEL_BUSY,
    PRVLORA_STATUS_RADIO_DUTYCYCLE_RESTRICTED,
    PRVLORA_STATUS_RADIO_PARAMETER_INVALID,
    // internal status
    PRVLORA_STATUS_UNKNOWN_DEVICE,
    PRVLORA_STATUS_CRYPTO_ERROR,
    /*---*/
    MAXNUM_PRVLORA_STATUS,
} PrvLoRaStatus_t;

// event status
typedef enum _PrvLoRaEventInfoStatus_t
{
    // common status
    PRVLORA_EVENTINFO_STATUS_OK = 0,
    PRVLORA_EVENTINFO_STATUS_ERROR,
    // mcps-confirm status
    PRVLORA_EVENTINFO_STATUS_TX_TIMEOUT,
    PRVLORA_EVENTINFO_STATUS_TX_NOACK,
    PRVLORA_EVENTINFO_STATUS_TX_CANCELED,
    PRVLORA_EVENTINFO_STATUS_TX_CHANNELBUSY,
    PRVLORA_EVENTINFO_STATUS_TX_DUTYCYCLE_RESTRICTED,
    PRVLORA_EVENTINFO_STATUS_TX_RADIO_ERROR,
    // mlme status (MLME_KEY)
    PRVLORA_EVENTINFO_STATUS_KEYREQ_FAILED,
    /*---*/
    MAXNUM_PRVLORA_EVENTINFO_STATUS,
} PrvLoRaEventInfoStatus_t;

// Regions for PrivateLoRa
typedef enum _PrvLoRaRegion_t
{
    PRVLORA_REGION_EU = 0,
    PRVLORA_REGION_IN,
    PRVLORA_REGION_AS1,
    PRVLORA_REGION_AS2,
    PRVLORA_REGION_AS3,
    PRVLORA_REGION_AS4,
    PRVLORA_REGION_US,
    PRVLORA_REGION_AU,
    PRVLORA_REGION_KR,
    PRVLORA_REGION_JP,
    PRVLORA_REGION_JP_LDC,
    /*---*/
    MAXNUM_PRVLORA_REGION,
} PrvLoRaRegion_t;

// IB for PrivateLoRa
typedef enum _PrvLoRaIb_t
{
    PRVLORA_IB_MACADDR = 0,             /* R/W         */
    PRVLORA_IB_CHANNEL_ID,              /* R/W         */
    PRVLORA_IB_DR,                      /* R/W         */
    PRVLORA_IB_TXPOWER,                 /* R/W         */
    PRVLORA_IB_RXONWHENIDLE,            /* R/W         */
    PRVLORA_IB_KEYREQ_PERMISSION,       /* R/W         */
    PRVLORA_IB_TXCYCLE_TIME,            /* R/W         */
    PRVLORA_IB_RADIO_CFG_CHECK_ENABLE,  /* R/W         */
} PrvLoRaIb_t;

// MLME request for PrivateLoRa
typedef enum _PrvLoRaMlme_t
{
    PRVLORA_MLME_KEY = 0,
    PRVLORA_MLME_DEVINFO,
    PRVLORA_MLME_TXCYCLE,
    /*---*/
    MAXNUM_PRVLORA_MLME,
} PrvLoRaMlme_t;


// Notification from PrivateLoRa MAC
typedef enum _PrvLoRaNotifyId_t
{
    PRVLORA_NOTIFY_UPDATE_REMOTEDEV = 0,
    /*---*/
    MAXNUM_PRVLORA_NOTIFY,
} PrvLoRaNotifyId_t;


/*------------------------*/
/* typedef (struct/union) */

// IB for PrivateLoRa
typedef struct _PrvLoRaIbReqTxCycle_t
{
    uint8_t     dstMacAddr[ PRVLORA_MACADDR_SIZE ];
    uint32_t    txCycleTime;
} PrvLoRaIbReqTxCycle_t;

typedef union _PrvLoRaIbRequest_t
{
    uint8_t                     macAddr[ PRVLORA_MACADDR_SIZE ];
    uint8_t                     channelId;
    uint8_t                     drIndex;
    int8_t                      txPower;
    bool                        rxOnWhenIdle;
    float                       maxEirp;
    uint32_t                    systemMaxRxError;
    bool                        keyReqPermit;
    PrvLoRaIbReqTxCycle_t       txCycle;
    bool                        radioCfgCheckEnable;
} PrvLoRaIbRequest_t;

// TxOptions
typedef union _PrvLoRaTxOptions_t
{
    uint8_t     txOptValue;
    struct
    {
        uint8_t AckRequest  : 1;
        uint8_t SecEnable   : 1;
        uint8_t IndirectTx  : 1;
        uint8_t _reserved   : 4;
        uint8_t ack         : 1;  // Application cannot use it. Only use in PrivateLoRa stack.
    } options;
} PrvLoRaTxOptions_t;

// MCPS-Request for PrivateLoRa
typedef struct _PrvLoRaMcpsReq_t
{
    uint8_t                 dstMacAddr[ PRVLORA_MACADDR_SIZE ];
    uint8_t                 *p_txData;
    uint8_t                 txDataSize;
    uint16_t                txHandle;
    PrvLoRaTxOptions_t      txOptions;
} PrvLoRaMcpsReq_t;


// MCPS-Confirm for PrivateLoRa
typedef struct _PrvLoRaMcpsCfm_t
{
    PrvLoRaEventInfoStatus_t    eventStatus;
    uint16_t                    txHandle;
} PrvLoRaMcpsCfm_t;


// MCPS-Indication for PrivateLoRa
typedef struct _PrvLoRaMcpsInd_t
{
    PrvLoRaEventInfoStatus_t    eventStatus;
    uint8_t                     *p_srcMacAddr;
    uint8_t                     *p_rxData;
    uint8_t                     rxDataSize;
    int16_t                     rssi;
    int8_t                      snr;
    bool                        isAck;
    bool                        isSecurity;
} PrvLoRaMcpsInd_t;


// MLME-Request (PRVLORA_MLME_KEY) for PrivateLoRa
typedef struct _PrvLoRaMlmeKeyReq_t
{
    uint8_t                 dstMacAddr[ PRVLORA_MACADDR_SIZE ];
    PrvLoRaTxOptions_t      txOptions;
} PrvLoRaMlmeKeyReq_t;

// MLME-Request (PRVLORA_MLME_DEVINFO) for PrivateLoRa
typedef struct _PrvLoRaMlmeDevInfoReq_t
{
    uint8_t                 dstMacAddr[ PRVLORA_MACADDR_SIZE ];
    PrvLoRaTxOptions_t      txOptions;
} PrvLoRaMlmeDevInfoReq_t;

// MLME-Request (PRVLORA_MLME_TXCYCLE) for PrivateLoRa
typedef struct _PrvLoRaMlmeTxCycleReq_t
{
    uint8_t                 dstMacAddr[ PRVLORA_MACADDR_SIZE ];
    PrvLoRaTxOptions_t      txOptions;
    uint32_t                txCycleTime;
} PrvLoRaMlmeTxCycleReq_t;

// MLME-Request for PrivateLoRa
typedef struct _PrvLoRaMlmeReq_t
{
    PrvLoRaMlme_t               mlmeType;
    union
    {
        PrvLoRaMlmeKeyReq_t         keyReq;
        PrvLoRaMlmeDevInfoReq_t     devInfoReq;
        PrvLoRaMlmeTxCycleReq_t     txCycleReq;
    } req;
} PrvLoRaMlmeReq_t;


// MLME-Confirm (PRVLORA_MLME_KEY)
typedef struct _PrvLoRaMlmeKeyCfm_t
{
    PrvLoRaEventInfoStatus_t    status;         // (must be top entry)
    uint8_t                     dstMacAddr[ PRVLORA_MACADDR_SIZE ];
} PrvLoRaMlmeKeyCfm_t;

// MLME-Confirm (PRVLORA_MLME_DEVINFO)
typedef struct _PrvLoRaMlmeDevInfoCfm_t
{
    PrvLoRaEventInfoStatus_t    status;         // (must be top entry)
    int8_t                      snr;            // 6-bit  (upper 2bit is RFU)
    int8_t                      txPower;        // 8-bit  (signed)
    uint32_t                    txCycleTime;    // 24-bit (upper 8bit is RFU)
} PrvLoRaMlmeDevInfoCfm_t;

// MLME-Confirm (PRVLORA_MLME_TXCYCLE)
typedef struct _PrvLoRaMlmeTxCycleCfm_t
{
    PrvLoRaEventInfoStatus_t    status;         // (must be top entry)
} PrvLoRaMlmeTxCycleCfm_t;

// MLME-Confirm
typedef struct _PrvLoRaMlmeCfm_t
{
    PrvLoRaMlme_t               mlmeType;
    union
    {
        PrvLoRaEventInfoStatus_t    status;
        /*----*/
        PrvLoRaMlmeKeyCfm_t         keyCfm;
        PrvLoRaMlmeDevInfoCfm_t     devInfoCfm;
        PrvLoRaMlmeTxCycleCfm_t     txCycleCfm;
    } cfm;
} PrvLoRaMlmeCfm_t;


// MLME-Indication (PRVLORA_MLME_KEY)
typedef struct _PrvLoRaMlmeKeyInd_t
{
    uint8_t     srcMacAddr[ PRVLORA_MACADDR_SIZE ];
} PrvLoRaMlmeKeyInd_t;

// MLME-Indication (PRVLORA_MLME_TXCYCLE)
typedef struct _PrvLoRaMlmeTxCycleInd_t
{
    uint8_t     srcMacAddr[ PRVLORA_MACADDR_SIZE ];
    uint32_t    txCycleTime;
    bool        isSecurity;
} PrvLoRaMlmeTxCycleInd_t;

// MLME-Indication
typedef struct _PrvLoRaMlmeInd_t
{
    PrvLoRaMlme_t               mlmeType;
    union
    {
        PrvLoRaMlmeKeyInd_t         keyInd;
        PrvLoRaMlmeTxCycleInd_t     txCycleInd;
    } ind;
} PrvLoRaMlmeInd_t;


// Notification (PRVLORA_NOTIFY_UPDATE_REMOTEDEV)
// & Remote device information
typedef struct _PrvLoRaRemoteDevUpdated_t
{
    uint16_t    updatedParams;
    uint8_t     devAddress[ PRVLORA_MACADDR_SIZE ];
    uint8_t     secPsk[ PRVLORA_CRYPTKEY_SIZE ];
    uint8_t     secSessionKey[ PRVLORA_CRYPTKEY_SIZE ];
    uint32_t    frameCounterTx;
    uint32_t    frameCounterRx;
} PrvLoRaRemoteDevUpdated_t;
typedef PrvLoRaRemoteDevUpdated_t   PrvLoRaNotifyUpdatedRemoteDev_t;

// Notification
typedef struct _PrvLoRaNotification_t
{
    PrvLoRaNotifyId_t       notifyType;
    union 
    {
        PrvLoRaNotifyUpdatedRemoteDev_t     updtRemoteDevNty;
    } nty;
} PrvLoRaNotification_t;


// PrivateLoRa events
typedef struct _PrvLoRaPrimitives_t
{
    void ( *PrvLoRaMacMcpsConfirm )( PrvLoRaMcpsCfm_t *p_mcpsCfm );
    void ( *PrvLoRaMacMcpsIndication )( PrvLoRaMcpsInd_t *p_mcpsInd );
    void ( *PrvLoRaMacMlmeConfirm )( PrvLoRaMlmeCfm_t *p_MlmeCfm );
    void ( *PrvLoRaMacMlmeIndication )( PrvLoRaMlmeInd_t *p_MlmeInd );
    void ( *PrvLoRaMacNotification )( PrvLoRaNotification_t *p_notify );
} PrvLoRaPrimitives_t;


/*-----------------*/
/* Functions (API) */
extern PrvLoRaStatus_t PrivateLoRaInitialization( PrvLoRaPrimitives_t *p_primitives,
                                                  PrvLoRaRegion_t     region );
extern PrvLoRaStatus_t PrivateLoRaStart( void );
extern PrvLoRaStatus_t PrivateLoRaStop( void );
extern void PrivateLoRaProcess( void );

extern PrvLoRaStatus_t PrivateLoRaGetRequest( PrvLoRaIb_t ibId, PrvLoRaIbRequest_t *p_ibGet );
extern PrvLoRaStatus_t PrivateLoRaSetRequest( PrvLoRaIb_t ibId, PrvLoRaIbRequest_t *p_ibSet );

extern PrvLoRaStatus_t PrivateLoRaRegisterRemoteDevice( uint8_t  *p_remoteMacAddr,
                                                        uint8_t  *p_psk,
                                                        uint8_t  *p_sessionKey,
                                                        uint32_t initFrameCounterTx,
                                                        uint32_t initFrameCounterRx );
extern PrvLoRaStatus_t PrivateLoRaUnregisterRemoteDevice( uint8_t *p_remoteMacAddr );

extern PrvLoRaStatus_t PrivateLoRaMcpsRequest( PrvLoRaMcpsReq_t *p_mcpsReq );
extern PrvLoRaStatus_t PrivateLoRaMlmeRequest( PrvLoRaMlmeReq_t *p_mlmeReq );

extern PrvLoRaStatus_t PrivateLoRaSetLowPower( void );

#endif  // __PRIVATELORA_H__
