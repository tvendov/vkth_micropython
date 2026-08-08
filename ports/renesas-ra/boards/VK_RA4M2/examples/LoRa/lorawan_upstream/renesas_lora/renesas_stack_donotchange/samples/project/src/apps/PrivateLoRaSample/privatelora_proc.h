/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    privatelora_proc.h
  * @author  Renesas Electronics Corporation
  * @brief
**/

#ifndef __PRIVATELORA_PROC_H__
#define __PRIVATELORA_PROC_H__

#include "PrivateLoRa.h"

#include "board.h"
#include "privatelora_nvmdata_table.h"

/*-----------*/
/* define #1 */
#define APP_PRVLORA_LEN_MACADDR         PRVLORA_MACADDR_SIZE
#define APP_PRVLORA_LEN_SECKEY          PRVLORA_CRYPTKEY_SIZE

#define APP_PRVLORA_LEN_TXCYCLEDATA     16

/* ----- version of data format when to store parameters to data flash */
#define APP_PRVLORA_DATA_FORMAT_VERSION_LEN     8
#define APP_PRVLORA_DATA_FORMAT_VERSION         'P','L','F','V','4','.','4','0'


/*------------------------*/
/* typedef (struct/union) */
// Private LoRa device setting
typedef struct _AppPrvLoRaSettings_t
{
    PrvLoRaRegion_t             region;
    uint8_t                     maxNumRemoteDev;
    /*---*/
    uint8_t                     macAddr[ APP_PRVLORA_LEN_MACADDR ];
    uint8_t                     channelId;
    uint8_t                     drIndex;
    int8_t                      txPower;
    bool                        rxOnWhenIdle;
    bool                        permitKeyReq;
    /*---*/
    PrvLoRaTxOptions_t          txOptions;
    bool                        dispRssi;
} AppPrvLoRaSettings_t;

// remote device information
typedef struct _AppPrvLoRaRemoteDevInfo_t
{
    uint8_t     devMacAddr[ APP_PRVLORA_LEN_MACADDR ];  // (note;NVM) pay attention to alignment/padding
    uint8_t     psk[ APP_PRVLORA_LEN_SECKEY ];          // (note;NVM) pay attention to alignment/padding
    uint8_t     sessionKey[ APP_PRVLORA_LEN_SECKEY ];   // (note;NVM) pay attention to alignment/padding
    uint32_t    frameCounterTx;                         // (note;NVM) pay attention to alignment/padding
    uint32_t    frameCounterRx;                         // (note;NVM) pay attention to alignment/padding
    /*---*/
    bool        isValid;
} AppPrvLoRaRemoteDevInfo_t;

// tx cycle
typedef struct _AppPrvLoRaTxCycleMng_t
{
    uint32_t        period;                              // (note;NVM) pay attention to alignment/padding
    uint8_t         dstAddr[ APP_PRVLORA_LEN_MACADDR ];  // (note;NVM) pay attention to alignment/padding
    /*---*/
    bool            isReady;
    bool            isStart;
    TimerEvent_t    txCycleTimer;
    uint8_t         txData[ APP_PRVLORA_LEN_TXCYCLEDATA ];
    uint8_t         txDataLen;
} AppPrvLoRaTxCycleMng_t;

// NVM table
typedef struct _AppPrvLoRaNvmFixedNumDataTable_t
{
    uint32_t        rwReqFlag;
    uint32_t        rwType;
    uint8_t         *p_data;
    uint8_t         dataSize;
    uint8_t         dataId;
} AppPrvLoRaNvmDataTable_t;

// Parameters to store NVM
typedef struct _AppPrvLoRaNvmParameters_t
{
    uint8_t                     nvmFormatVer[ APP_PRVLORA_DATA_FORMAT_VERSION_LEN ];
    AppPrvLoRaSettings_t        prvLoraSettings;
    AppPrvLoRaTxCycleMng_t      txCycleMng;  // only period and dstAddr[] are stored
    AppPrvLoRaRemoteDevInfo_t   remoteDevInfo[ PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM ];
} AppPrvLoRaNvmParameters_t;


/*-----------*/
/* define #2 */
/* Nvm Data type */
#define APP_PRVLORA_NVMDATA_TYPE_FIXED      (0x00000000)
#define APP_PRVLORA_NVMDATA_TYPE_VAR(n)     (0x80000000 + (n))

/* Nvm Data Read/Write flag */
#define APP_PRVLORA_NVMDATA_RWFLG_PRVLORASETTINGS       ( 0x00000001 )  // TYPE_FIXED

#define APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE_PERIOD        ( 0x00000002 )  // TYPE_FIXED
#define APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE_MACADDR       ( 0x00000004 )  // TYPE_FIXED
#define APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE           ( APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE_PERIOD  | \
                                                      APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE_MACADDR )

#define APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_MACADDR        ( 0x80000000 )  // TYPE_VAR(n)
#define APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_PSK            ( 0x40000000 )  // TYPE_VAR(n)
#define APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_SESSION_KEY    ( 0x20000000 )  // TYPE_VAR(n)
#define APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_FCNTTX         ( 0x10000000 )  // TYPE_VAR(n)
#define APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_FCNTRX         ( 0x08000000 )  // TYPE_VAR(n)
#define APP_PRVLORA_NVMDATA_RWFLG_REMOTEDEVICE      ( APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_MACADDR | \
                                                      APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_PSK  | \
                                                      APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_SESSION_KEY | \
                                                      APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_FCNTTX  | \
                                                      APP_PRVLORA_NVMDATA_RWFLG_RMTDEV_FCNTRX  )

#define APP_PRVLORA_NVMDATA_RWFLG_ALL               ( APP_PRVLORA_NVMDATA_RWFLG_PRVLORASETTINGS | \
                                                      APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE         | \
                                                      APP_PRVLORA_NVMDATA_RWFLG_REMOTEDEVICE )

/* Nvm Data Read/Write parameter (pointer) */
#define APP_PRVLORA_NVMDATA_PARAM_PRVLORASETTINGS       &( appPrvLoRaNvmParameters )

#define APP_PRVLORA_NVMDATA_PARAM_TXCYCLE_PERIOD        &( appPrvLoRaNvmParameters.txCycleMng.period )
#define APP_PRVLORA_NVMDATA_PARAM_TXCYCLE_MACADDR       &( appPrvLoRaNvmParameters.txCycleMng.dstAddr[ 0 ] )
#define APP_PRVLORA_NVMDATA_PARAM_TXCYCLE           &( appPrvLoRaNvmParameters.txCycleMng.period )

#define APP_PRVLORA_NVMDATA_PARAM_RMTDEV_MACADDR(n)     &( appPrvLoRaNvmParameters.remoteDevInfo[(n)].devMacAddr[ 0 ] )
#define APP_PRVLORA_NVMDATA_PARAM_RMTDEV_PSK(n)         &( appPrvLoRaNvmParameters.remoteDevInfo[(n)].psk[ 0 ] )
#define APP_PRVLORA_NVMDATA_PARAM_RMTDEV_SESSION_KEY(n) &( appPrvLoRaNvmParameters.remoteDevInfo[(n)].sessionKey[ 0 ] )
#define APP_PRVLORA_NVMDATA_PARAM_RMTDEV_FCNTTX(n)      &( appPrvLoRaNvmParameters.remoteDevInfo[(n)].frameCounterTx )
#define APP_PRVLORA_NVMDATA_PARAM_RMTDEV_FCNTRX(n)      &( appPrvLoRaNvmParameters.remoteDevInfo[(n)].frameCounterRx )
#define APP_PRVLORA_NVMDATA_PARAM_REMOTEDEVICE(n)   &( appPrvLoRaNvmParameters.remoteDevInfo[(n)] )

/* Nvm Data Read/Write size */
#define APP_PRVLORA_NVMDATA_SIZE_PRVLORASETTINGS        ( APP_PRVLORA_DATA_FORMAT_VERSION_LEN + sizeof(AppPrvLoRaSettings_t) )

#define APP_PRVLORA_NVMDATA_SIZE_TXCYCLE_PERIOD         ( sizeof(uint32_t) )
#define APP_PRVLORA_NVMDATA_SIZE_TXCYCLE_MACADDR        ( APP_PRVLORA_LEN_MACADDR )
#define APP_PRVLORA_NVMDATA_SIZE_TXCYCLE            ( APP_PRVLORA_NVMDATA_SIZE_TXCYCLE_PERIOD  + \
                                                      APP_PRVLORA_NVMDATA_SIZE_TXCYCLE_MACADDR )

#define APP_PRVLORA_NVMDATA_SIZE_RMTDEV_MACADDR         ( APP_PRVLORA_LEN_MACADDR )
#define APP_PRVLORA_NVMDATA_SIZE_RMTDEV_PSK             ( APP_PRVLORA_LEN_SECKEY )
#define APP_PRVLORA_NVMDATA_SIZE_RMTDEV_SESSION_KEY     ( APP_PRVLORA_LEN_SECKEY )
#define APP_PRVLORA_NVMDATA_SIZE_RMTDEV_FCNTTX          ( sizeof(uint32_t) )
#define APP_PRVLORA_NVMDATA_SIZE_RMTDEV_FCNTRX          ( sizeof(uint32_t) )
#define APP_PRVLORA_NVMDATA_SIZE_REMOTEDEVICE       ( APP_PRVLORA_NVMDATA_SIZE_RMTDEV_MACADDR + \
                                                      APP_PRVLORA_NVMDATA_SIZE_RMTDEV_PSK  + \
                                                      APP_PRVLORA_NVMDATA_SIZE_RMTDEV_SESSION_KEY + \
                                                      APP_PRVLORA_NVMDATA_SIZE_RMTDEV_FCNTTX  + \
                                                      APP_PRVLORA_NVMDATA_SIZE_RMTDEV_FCNTRX  )

/*--------------------------*/
/* global variable (extern) */
// NVM
// - format version
extern const uint8_t appPrvLoRaNvmFormatVer[ APP_PRVLORA_DATA_FORMAT_VERSION_LEN ];
extern const AppPrvLoRaNvmDataTable_t   appPrvLoRaNvmDataTable[];

// NVM parameters
extern AppPrvLoRaNvmParameters_t    appPrvLoRaNvmParameters;


/*-----------*/
/* Functions */
//--- PrivateLoRaInitialization ---//
extern PrvLoRaStatus_t AppPrvLoRaInit( void );
extern PrvLoRaStatus_t AppPrvLoRaSetRegion( PrvLoRaRegion_t region );
extern PrvLoRaStatus_t AppPrvLoRaGetRegion( PrvLoRaRegion_t *p_region );
extern PrvLoRaStatus_t AppPrvLoRaStart( void );
extern PrvLoRaStatus_t AppPrvLoRaStop( void );

//--- PrivateLoRaGet/SetRequest ---//
// PRVLORA_IB_MACADDR
extern PrvLoRaStatus_t AppPrvLoRaSetMacAddr( uint8_t *p_macAddr );
extern PrvLoRaStatus_t AppPrvLoRaGetMacAddr( uint8_t *p_macAddr );
// PRVLORA_IB_CHANNEL_ID
extern PrvLoRaStatus_t AppPrvLoRaSetChannelId( uint8_t channelId );
extern PrvLoRaStatus_t AppPrvLoRaGetChannelId( uint8_t *p_channelId );
// PRVLORA_IB_DR
extern PrvLoRaStatus_t AppPrvLoRaSetDR( uint8_t drIndex );
extern PrvLoRaStatus_t AppPrvLoRaGetDR( uint8_t *p_drIndex );
// PRVLORA_IB_TXPOWER
extern PrvLoRaStatus_t AppPrvLoRaSetTxPower( int8_t txPower );
extern PrvLoRaStatus_t AppPrvLoRaGetTxPower( int8_t *p_txPower );
// PRVLORA_IB_RXONWHENIDLE
extern PrvLoRaStatus_t AppPrvLoRaSetRxOnWhenIdle( bool rxOnWhenIdle );
extern PrvLoRaStatus_t AppPrvLoRaGetRxOnWhenIdle( bool *p_rxOnWhenIdle );
// PRVLORA_IB_KEYREQ_PERMISSION
extern PrvLoRaStatus_t AppPrvLoRaSetKeyReqPermit( bool permitKeyReq );
extern PrvLoRaStatus_t AppPrvLoRaGetKeyReqPermit( bool *p_permitKeyReq );
// PRVLORA_IB_TXCYCLE_TIME
extern PrvLoRaStatus_t AppPrvLoRaSetTxCycleTime( uint8_t *p_dstMacAddr, uint32_t txCycleTime );
extern PrvLoRaStatus_t AppPrvLoRaGetTxCycleTime( uint8_t *p_dstMacAddr, uint32_t *p_txCycleTime );

//--- PrivateLoRaRegisterRemoteDevice ---//
extern PrvLoRaStatus_t AppPrvLoRaSetRemoteDeviceInfo( uint8_t *p_remoteMacAddr, uint8_t *p_psk );
extern PrvLoRaStatus_t AppPrvLoRaClearRemoteDeviceInfo( uint8_t *p_remoteMacAddr );

//--- PrivateLoRaMcpsRequest ---//
extern PrvLoRaStatus_t AppPrvLoRaSetTxOptions( PrvLoRaTxOptions_t txOptions );
extern PrvLoRaStatus_t AppPrvLoRaGetTxOptions( PrvLoRaTxOptions_t *p_txOptions );
extern PrvLoRaStatus_t AppPrvLoRaSendData( uint8_t *p_dstMacAddr, uint8_t *p_data, uint8_t dataSize, uint16_t txHandle );

//--- PrivateLoRaMlmeRequest ---//
extern PrvLoRaStatus_t AppPrvLoRaNetworkStart( void );
extern PrvLoRaStatus_t AppPrvLoRaKeyRequest( uint8_t *p_dstMacAddr );
extern PrvLoRaStatus_t AppPrvLoRaDevInfoReq( uint8_t *p_dstMacAddr );
PrvLoRaStatus_t AppPrvLoRaTxCycleReq( uint8_t *p_dstMacAddr, uint32_t txCycleTime );

//--- PrivateLoRa application ---//
extern void AppPrvLoRaMcuReset( void );
extern PrvLoRaStatus_t AppPrvLoRaSetRssi( bool dispRssi );
extern PrvLoRaStatus_t AppPrvLoRaGetRssi( bool *p_dispRssi );

// (RemoteDeviceInfo)
extern void AppPrvLoRaRmtDevInfoInit( void );
extern PrvLoRaStatus_t AppPrvLoRaRmtDevInfoRegister( uint8_t  *p_devMacAddr,
                                                     uint8_t  *p_psk,
                                                     uint8_t  *p_sessionKey,
                                                     uint32_t frameCounterTx,
                                                     uint32_t frameCounterRx );
extern PrvLoRaStatus_t AppPrvLoRaRmtDevInfoUnregister( uint8_t *p_devMacAddr );
extern PrvLoRaStatus_t AppPrvLoRaRmtDevInfoUpdateSessionKey( uint8_t *p_devMacAddr, uint8_t *p_sessionKey );
extern PrvLoRaStatus_t AppPrvLoRaRmtDevInfoUpdateFrameCounterTx( uint8_t *p_devMacAddr, uint32_t frameCounterTx );
extern PrvLoRaStatus_t AppPrvLoRaRmtDevInfoUpdateFrameCounterRx( uint8_t *p_devMacAddr, uint32_t frameCounterRx );

// (TxCycle)
extern void AppPrvLoRaTxCycleInit( void );
extern void AppPrvLoRaTxCycleSetParameter( uint8_t *p_srcAddr, uint32_t txCycleTime );
extern void AppPrvLoRaTxCycleClearParameter( uint8_t *p_dstAddr );
extern void AppPrvLoRaTxCycleUpdateTimer( void );
extern void AppPrvLoRaTxCycleStopTimer( void );
extern PrvLoRaStatus_t AppPrvLoRaTxCycleSendFrame( void );

// (NVM)
extern PrvLoRaStatus_t AppPrvLoRaNvmLoadParameters( void );
extern PrvLoRaStatus_t AppPrvLoRaNvmSaveParameters( uint32_t writeValFlags );

#endif  /* __PRIVATELORA_PROC_H__ */
