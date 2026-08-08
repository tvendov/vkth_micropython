/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/     

#ifndef __R_RADIO_REGION_H__
#define __R_RADIO_REGION_H__

#include "timer.h"


//
// Flag Macros
//
#define RP_REGION_FLAG_SUCCESS            (0x0000)
#define RP_REGION_FLAG_FAIL_BAND_DISABLED (0x0001)
#define RP_REGION_FLAG_FAIL_FREQUENCY     (0x0002)
#define RP_REGION_FLAG_FAIL_MODEM_CONFIG  (0x0004)
#define RP_REGION_FLAG_FAIL_TX_POWER      (0x0008)
#define RP_REGION_FLAG_FAIL_TIME_ON_AIR   (0x0010)
#define RP_REGION_FLAG_FAIL_DUTY_CYCLE    (0x0020)
#define RP_REGION_FLAG_FAIL_TX_INTERVAL   (0x0040)
#define RP_REGION_FLAG_FAIL_CARRIER_SENSE (0x0080)


//
// type
//
typedef uint32_t  RpRegionSetCh_t;    //!< For Radio.SetChannel() argument retention.
typedef int16_t   RpRegionModems_t;   //!< Virtually equivalent to RadioModems_t
typedef uint16_t  RpRegionRsltFlag_t; //!< For layer specific result flags


//
// enum
//

//! Radio configuration check mode
typedef enum
{
    RADIO_CFG_CHK_MODE_0  = 0x00, //!< default
}RpRegionChkMode_t;

//! Configuration type enumueration that is internally used
typedef enum{
    RP_REGION_CFGTYPE_ALL = 1,
    RP_REGION_CFGTYPE_CHECK_MODE,
    RP_REGION_CFGTYPE_RXCFG,
    RP_REGION_CFGTYPE_TXCFG,
    RP_REGION_CFGTYPE_SETCH,
}RpRegionRadioCfgType_t;


//! Radio configuration check result. Equivalent to RadioResult_t. @see RadioResult_t
typedef enum{
   RP_REGION_SUCCESS = 0,
   RP_REGION_ARG_IS_NULL,
   RP_REGION_ARG_IS_INVALID,
   RP_REGION_FAIL, // = 3
   // 4 is RFU
   RP_REGION_CHECK_FAIL_RX_CFG          = 100,
   RP_REGION_CHECK_FAIL_TX_CFG          = 101,
   RP_REGION_CHECK_FAIL_TX_DUTY_CYCLE   = 102,
   RP_REGION_CHECK_FAIL_TX_CHANNEL_BUSY = 103,
}RpRegionResult_t;


//
// Structure
//

//! Radio band check result structure
typedef struct RpRegionCheckResult_s{
    RpRegionResult_t checkRslt;         //!< Result of radio configurations check
    uint8_t          passedBand;        //!< ID of radio band that passsed radio configurations check (if any)
} RpRegionChkResult_t;

//! Reception configurations structure. @see SetRxConfig
typedef struct RpRegionRxCfg_s{
    RpRegionModems_t    modem;
    uint32_t            bandwidth;
    uint32_t            datarate;
    uint8_t             coderate;
    uint16_t            preambleLen;
    bool                fixLen;
    bool                crcOn;
}RpRegionRxCfg_t;

//! Transmission configurations structure @see SetTxConfig
typedef struct RpRegionTxCfg_s{
    RpRegionModems_t    modem;
    int8_t              power;
    uint32_t            fdev;
    uint32_t            bandwidth;
    uint32_t            datarate;
    uint8_t             coderate;
    uint16_t            preambleLen;
    bool                fixLen;
    bool                crcOn;
}RpRegionTxCfg_t;

//! Radio configuration structure 
typedef struct RpRegionRadioCfg_s{
    RpRegionChkMode_t   chkMode;                        //!< Radio configurations check mode
    RpRegionSetCh_t     setCh;                          //!< Frequency configurations
    RpRegionRxCfg_t     rxCfg;                          //!< Reception configurations
    RpRegionTxCfg_t     txCfg;                          //!< Transmission configurations
    TimerTime_t         tsLastTx;                       //!< Timestamp of last transmission time (transmission start time).
    uint32_t            lastToa;                        //!< Time On Air (TOA) of the packet previously sent, in millisecond (ms).
    uint16_t            lastTxBand;                     //!< ID of the band in which last successful packet transmission was performed.
    RadioConfigRegion_t region;                         //!< Region/country
    int8_t              lastTxPower;                    //!< Tx power set to SX126x lastly
    uint8_t             numBands;                       //!< Numer of bands
}RpRegionRadioCfg_t;


//! Modem configuration structure
typedef struct RpRegionModemCfgDef_s {
    RpRegionModems_t modem;                             //!< MODEM_LORA or MODEM_FSK
    uint32_t         sfDr;                              //!< SF (5 to 12) when modem = MODEM_LORA, datarate in bps when modem = MODEM_FSK
    uint32_t         bwFdev;                            //!< Bandwidth in Hz when modem = MODEM_LORA, fdev when modem = MODEM_FSK.
} RpRegionModemCfgDef_t;

//! Modem configurations management structure
typedef struct RpRegionModemCfg_s {
    uint8_t                       size;                 //!< Array size of modem configurations structure
    const RpRegionModemCfgDef_t * pModemCfg;            //!< Pointer to the head of modem configurations structure array
} RpRegionModemCfg_t;

//! Radio band definition structure
typedef struct RpRegionBand_s {
    RadioConfigRegion_t region;                         //!< Region/country
    uint32_t           freqStart;                       //!< lowest center frequency of the band in Hz
    uint32_t           freqEnd;                         //!< highest center frequency of the band in Hz
    uint32_t           chSpacing;                       //!< channel spacing in Hz
    int8_t             txMaxPower;                      //!< maximum transmission power in dBm
    uint32_t           txMaxTime;                       //!< maximum transmission time per packet in millisecond (ms)
    uint16_t           txSlpTime;                       //!< minimum sleep time after transmission in millisecond (ms)
    uint32_t           ccaTime;                         //!< minimum carrier sense time in microsecond (us)
    int16_t            ccaTh;                           //!< carrier sense threshold in dBm
    uint32_t           ccaBw;                           //!< carrier sense bandwidth in Hz to set to PIB_CCA_BANDWIDTH. See PIB description for allowed values.
    uint16_t           dutyCycle;                       //!< minimum transmission duty cycle in basis point (bp). 1 % = 100 bp, 100 % = 10,000 bp. max: 10,000 bp, min: 1 bp.
    uint8_t            modemCfgNo;                      //!< modem configuration number, which is offset to modem configurations set array
} RpRegionBand_t;

//! Radio band operation management strucutre
typedef struct RpRegionBandMng_s {
    const RpRegionBand_t  *pBandCfg;                    //!< Pointer to radio band definition.
    bool     bandEn;                                    //!< Band enable.
    int8_t   rssiOffset;                                //!< RSSI offset in dB for carrier sense.
} RpRegionBandMng_t;

//! Radio band definition retrieval data.
typedef struct RpRegionGetBandCfg_s {
    uint16_t                                numBand;    //!< Number of radio bands defined.
    RpRegionBandMng_t                      *pBandCfg;   //!< Pointer to radio band definition.
    const RpRegionModemCfg_t               *pModemCfg;  //!< Pointer to modem definition.
} RpRegionGetBandCfg_t;



// Functions for radio layer (radio.c).
bool                RpRegionInit                   ( uint16_t bandEnMask );
void                RpRegionInitPib                ( void );
bool                RpRegionCheckRfFrequency       ( uint32_t frequency );
RpRegionResult_t    RpRegionSendCheck              ( uint8_t *pBuf, uint8_t size );
RpRegionResult_t    RpRegionRxCheck                ( uint32_t timeout );
RpRegionResult_t    RpRegionSetChannel             ( uint32_t freq );
RpRegionResult_t    RpRegionSetRxConfig            ( RpRegionModems_t modem, uint32_t bandwidth, uint32_t datarate, uint8_t coderate,
                                                     uint32_t bandwidthAfc, uint16_t preambleLen, uint16_t symbTimeout, bool fixLen,
                                                     uint8_t payloadLen, bool crcOn, bool freqHopOn, uint8_t hopPeriod,
                                                     bool iqInverted, bool rxContinuous );
RpRegionResult_t    RpRegionSetTxConfig            ( RpRegionModems_t modem, int8_t power, uint32_t fdev, uint32_t bandwidth, uint32_t datarate,
                                                     uint8_t coderate, uint16_t preambleLen, bool fixLen, bool crcOn, bool freqHopOn,
                                                     uint8_t hopPeriod, bool iqInverted, uint32_t timeout );
RpRegionResult_t    RpRegionTxContCheck            (uint8_t freq, int8_t power, uint16_t time);
RpRegionResult_t    RpRegionTxInfinitePreambleCheck(uint8_t freq, int8_t power, uint16_t time);


// Internal functions for r_radio_region.c.
void                RpRegionStoreCfg               ( RpRegionRadioCfgType_t cfgType, void *pCfg );
void                RpRegionRetrieveCfg            ( RpRegionRadioCfgType_t cfgType, void *pCfg );
uint32_t            RpRegionCalcObw                ( RpRegionModems_t modem, uint32_t bwLora, uint32_t fdevFsk, uint32_t brFsk );
uint32_t            RpRegionResolveLoraBw          ( uint32_t bwIndex, bool addMargin );
uint32_t            RpRegionCalcDutyCycleBackoff   ( uint32_t toa, uint16_t dutyCycle );
RpRegionRsltFlag_t  RpRegionCheckDutyCycle         ( void );
RpRegionRsltFlag_t  RpRegionCheckTxInterval        ( void );
RpRegionRsltFlag_t  RpRegionCheckTimeOnAir         ( uint32_t toa, uint8_t bandId );
RpRegionRsltFlag_t  RpRegionCheckFreqInChannel     ( uint32_t freqUsed, uint32_t freqRangeLow, uint32_t freqRangeHigh, uint32_t bwUsed, uint32_t bwChannel );
RpRegionRsltFlag_t  RpRegionCheckTxModemCfg        ( RpRegionTxCfg_t *pCfg, uint8_t bandId );
RpRegionChkResult_t RpRegionSendCheckMode0         ( uint8_t size );
RpRegionChkResult_t RpRegionRxCheckMode0           ( void );


// Function for evaluation purposes only.
void                 RpRegionSetRssiOffset( uint16_t bandMask, int16_t rssiOffset );
int16_t              RpRegionGetRssiOffset( uint8_t  bandId );
RpRegionGetBandCfg_t RpRegionGetBandConfig( void );

#endif // __R_RADIO_REGION_H__
