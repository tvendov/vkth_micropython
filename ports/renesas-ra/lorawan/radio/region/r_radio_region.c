/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/

#if defined( RP_USE_RADIO_CFG_CHECK )

#include <stdbool.h>
#include <stdint.h>
#include "timer.h"
#include "radio.h"
#include "sx126x.h"
#include "r_radio_region_api.h"
#include "board.h"

//
// Configutaion
//
#if !defined(RP_REGION_DUTY_CYCLE_CHECK_DISABLED)
#define RP_REGION_DUTY_CYCLE_CHECK_DISABLED     (0) //!< 0: Enable dugty cycle check (default)
#endif                                              //!< 1: Disable dugty cycle check for debug purpose, 
#if !defined(RP_REGION_LIMIT_TX_POWER_ENABLED)
#define RP_REGION_LIMIT_TX_POWER_ENABLED        (1) //!< 1: Enable to limit Tx power if it exceeds the max value instead of returning an error (default)
#endif                                              //!< 0: Return an error if Tx power exceeds the max value without limiting it 

//
// Macro
//
#define RP_REGION_ANT_GAIN             (2.15f)   //!< Anntena gain (dB)
#define RP_REGION_MAX_TX_POWER_OFFSET  (0)       //!< Maximum transmission power offset in dB.
#define RP_REGION_MIN_CCA_TIME         (1000UL)  //!< Minimum carrier sense time in microsecond (us)
#define RP_REGION_CCA_RSSI_OFFSET      (3)       //!< Default RSSI offset for carrier sense (int8_t)
#define RP_REGION_LORA_BW_MARGIN       (10000UL) //!< Margin in Hz to calculate occupied bandwidth for LoRa transmission
#define RP_REGION_FSK_BW_MARGIN        (10000UL) //!< Margin in Hz to calculate occupied bandwidth for FSK transmission
#define RP_REGION_TIME_CALC_MIN_MARGIN (2ULL)    //!< Minimum expected system time calculation error in millisecond (ms)

#define RP_REGION_AS1_TX_MAX_TIME      (3600000) //!< AS1: Set Tx max time (No limit)
//#define RP_REGION_AS1_TX_MAX_TIME    (400)     //!< AS1: Set Tx max time (400 msec)
#define RP_REGION_AS2_TX_MAX_TIME      (3600000) //!< AS2: Set Tx max time (No limit)
//#define RP_REGION_AS2_TX_MAX_TIME    (400)     //!< AS2: Set Tx max time (400 msec)
#define RP_REGION_AS3_TX_MAX_TIME      (3600000) //!< AS3: Set Tx max time (No limit)
//#define RP_REGION_AS3_TX_MAX_TIME    (400)     //!< AS3: Set Tx max time (400 msec)
#define RP_REGION_AS4_TX_MAX_TIME      (3600000) //!< AS4: Set Tx max time (No limit)
//#define RP_REGION_AS4_TX_MAX_TIME    (400)     //!< AS4: Set Tx max time (400 msec)


//
// Modulation configurations
//

//! Valid modem configuration No.0
const RpRegionModemCfgDef_t RpRegionModemCfgDef0[] 
    = {
          // modem,  sfDr, bwFdev
          {MODEM_LORA,  7, 125000}, {MODEM_LORA,  8, 125000}, {MODEM_LORA,     9,  125000}, {MODEM_LORA, 10,  125000}, 
          {MODEM_LORA, 11, 125000}, {MODEM_LORA, 12, 125000}, {MODEM_FSK,  50000,  25000}
      };

//! Valid modem configuration No.1
const RpRegionModemCfgDef_t RpRegionModemCfgDef1[] 
    = { 
          {MODEM_LORA,  7, 250000}
      };

//! Valid modem configuration No.2
const RpRegionModemCfgDef_t RpRegionModemCfgDef2[]
    = {
          // modem,  sfDr, bwFdev
          {MODEM_LORA,  7, 125000}, {MODEM_LORA,  8, 125000}, {MODEM_LORA,     9,  125000}, {MODEM_LORA, 10,  125000},
          {MODEM_LORA, 11, 125000}, {MODEM_LORA, 12, 125000}
      };

//! Valid modem configuration No.3
const RpRegionModemCfgDef_t RpRegionModemCfgDef3[]
    = {
          // modem,  sfDr, bwFdev
          {MODEM_LORA,  7, 500000}, {MODEM_LORA,  8, 500000}, {MODEM_LORA,     9,  500000}, {MODEM_LORA, 10,  500000},
          {MODEM_LORA, 11, 500000}, {MODEM_LORA, 12, 500000}
      };

//! Valid modem configuration management 
const RpRegionModemCfg_t RpRegionModemCfg[]
    = {  
           //size,                                                     pModemCfg
          {sizeof(RpRegionModemCfgDef0)/sizeof(RpRegionModemCfgDef_t), &RpRegionModemCfgDef0[0]}, 
          {sizeof(RpRegionModemCfgDef1)/sizeof(RpRegionModemCfgDef_t), &RpRegionModemCfgDef1[0]},
          {sizeof(RpRegionModemCfgDef2)/sizeof(RpRegionModemCfgDef_t), &RpRegionModemCfgDef2[0]},
          {sizeof(RpRegionModemCfgDef3)/sizeof(RpRegionModemCfgDef_t), &RpRegionModemCfgDef3[0]},
      };

#define RP_REGION_MODEM_CFG_NO_AVAIABLE_WITH_FREQ_HOPPING  (2)      // RpRegionModemCfg[2] is available only when the upper layer utilize the frequency hopping


//! Radio band definition

//! Radio configuration for each country/region
const RpRegionBand_t RpRegionBandDef[] = { 
    /*     region,   freqStart,   freqEnd, chSpacing,                txMaxPower, txMaxTime, txSlpTime,   ccaTime, ccaTh,  ccaBw, dutyCycle, modemCfgNo */
#if defined(RADIO_CFG_EU_ENABLED)
    {RADIO_CFG_EU,   863062500, 864937500,         1, 13.9 - RP_REGION_ANT_GAIN,      3600,         0,         0,     0,      0,        10,          0}, // EU-1-BW125
    {RADIO_CFG_EU,   863125000, 864875000,         1, 13.9 - RP_REGION_ANT_GAIN,      3600,         0,         0,     0,      0,        10,          1}, // EU-1-BW250
    {RADIO_CFG_EU,   865062500, 867937500,         1, 13.9 - RP_REGION_ANT_GAIN,     36000,         0,         0,     0,      0,       100,          0}, // EU-2-BW125
    {RADIO_CFG_EU,   865125000, 867875000,         1, 13.9 - RP_REGION_ANT_GAIN,     36000,         0,         0,     0,      0,       100,          1}, // EU-2-BW250
    {RADIO_CFG_EU,   868062500, 868537500,         1, 13.9 - RP_REGION_ANT_GAIN,     36000,         0,         0,     0,      0,       100,          0}, // EU-3-BW125
    {RADIO_CFG_EU,   868125000, 868475000,         1, 13.9 - RP_REGION_ANT_GAIN,     36000,         0,         0,     0,      0,       100,          1}, // EU-3-BW250
    {RADIO_CFG_EU,   868762500, 869137500,         1, 13.9 - RP_REGION_ANT_GAIN,      3600,         0,         0,     0,      0,        10,          0}, // EU-4-BW125
    {RADIO_CFG_EU,   868825000, 869075000,         1, 13.9 - RP_REGION_ANT_GAIN,      3600,         0,         0,     0,      0,        10,          1}, // EU-4-BW250
    {RADIO_CFG_EU,   869462500, 869587500,         1, 26.9 - RP_REGION_ANT_GAIN,    360000,         0,         0,     0,      0,      1000,          0}, // EU-5-BW125
    {RADIO_CFG_EU,   869525000, 869525000,         1, 26.9 - RP_REGION_ANT_GAIN,    360000,         0,         0,     0,      0,      1000,          1}, // EU-5-BW250
    {RADIO_CFG_EU,   869762500, 869937500,         1, 13.9 - RP_REGION_ANT_GAIN,     36000,         0,         0,     0,      0,       100,          0}, // EU-6-BW125
    {RADIO_CFG_EU,   869825000, 869875000,         1, 13.9 - RP_REGION_ANT_GAIN,     36000,         0,         0,     0,      0,       100,          1}, // EU-6-BW250
#endif
#if defined(RADIO_CFG_IN_ENABLED)
    {RADIO_CFG_IN,   865062500, 867937500,         1, 26.9 - RP_REGION_ANT_GAIN,   3600000,         0,         0,     0,      0,       250,          0}, // IN-BW125
#endif
#if defined(RADIO_CFG_AS1_ENABLED)
    {RADIO_CFG_AS1,  915062500, 927937500,         1,   16 - RP_REGION_ANT_GAIN, RP_REGION_AS1_TX_MAX_TIME, 0, 0,     0,      0,       100,          0}, // AS1-BW125
    {RADIO_CFG_AS1,  915125000, 927875000,         1,   16 - RP_REGION_ANT_GAIN, RP_REGION_AS1_TX_MAX_TIME, 0, 0,     0,      0,       100,          1}, // AS1-BW250
#endif
#if defined(RADIO_CFG_AS2_ENABLED)
    {RADIO_CFG_AS2,  920062500, 922937500,         1,   16 - RP_REGION_ANT_GAIN, RP_REGION_AS2_TX_MAX_TIME, 0, 0,     0,      0,       100,          0}, // AS2-BW125
    {RADIO_CFG_AS2,  920125000, 922875000,         1,   16 - RP_REGION_ANT_GAIN, RP_REGION_AS2_TX_MAX_TIME, 0, 0,     0,      0,       100,          1}, // AS2-BW250
#endif
#if defined(RADIO_CFG_AS3_ENABLED)
    {RADIO_CFG_AS3,  915062500, 920937500,         1,   16 - RP_REGION_ANT_GAIN, RP_REGION_AS3_TX_MAX_TIME, 0, 0,     0,      0,       100,          0}, // AS3-BW125
    {RADIO_CFG_AS3,  915125000, 920875000,         1,   16 - RP_REGION_ANT_GAIN, RP_REGION_AS3_TX_MAX_TIME, 0, 0,     0,      0,       100,          1}, // AS3-BW250
#endif
#if defined(RADIO_CFG_AS4_ENABLED)
    {RADIO_CFG_AS4,  917062500, 919937500,         1,   16 - RP_REGION_ANT_GAIN, RP_REGION_AS4_TX_MAX_TIME, 0, 0,     0,      0,       100,          0}, // AS4-BW125
    {RADIO_CFG_AS4,  917200000, 919800000,         1,   16 - RP_REGION_ANT_GAIN, RP_REGION_AS4_TX_MAX_TIME, 0, 0,     0,      0,       100,          1}, // AS4-BW250
#endif
#if defined(RADIO_CFG_US_ENABLED)
    {RADIO_CFG_US,   902062500, 927937500,         1,                        30,   3600000,         0,         0,     0,      0,     10000,          2}, // US-BW125(*)
    {RADIO_CFG_US,   902250000, 927750000,         1,                        30,   3600000,         0,         0,     0,      0,     10000,          3}, // US-BW500
#endif
#if defined(RADIO_CFG_AU_ENABLED)
    {RADIO_CFG_AU,   915062500, 927937500,         1,   30 - RP_REGION_ANT_GAIN,   3600000,         0,         0,     0,      0,     10000,          2}, // AU-BW125(*)
    {RADIO_CFG_AU,   915250000, 927750000,         1,   30 - RP_REGION_ANT_GAIN,   3600000,         0,         0,     0,      0,     10000,          3}, // AU-BW500
#endif
#if defined(RADIO_CFG_KR_ENABLED)
    {RADIO_CFG_KR,   920900000, 921900000,    200000,   10 - RP_REGION_ANT_GAIN,   3600000,         0,      6000,   -65, 234300,     10000,          0}, // KR-1-BW125
    {RADIO_CFG_KR,   922100000, 923300000,    200000,   14 - RP_REGION_ANT_GAIN,   3600000,         0,      6000,   -65, 234300,     10000,          0}, // KR-2-BW125
#endif
#if defined(RADIO_CFG_JP_ENABLED)
    {RADIO_CFG_JP,   920600000, 923400000,    200000,                        13,      4000,        50,      5000,   -80, 234300,     10000,          0}, // JP-1-BW125
    {RADIO_CFG_JP,   923600000, 928000000,    200000,                        13,       200,         2,      1000,   -80, 234300,      1000,          0}, // JP-2-BW125
    {RADIO_CFG_JP,   920700000, 921900000,    400000,                        13,      4000,        50,      5000,   -80, 467000,     10000,          1}, // JP-1-BW250-1
    {RADIO_CFG_JP,   922700000, 927900000,    400000,                        13,       200,         2,      1000,   -80, 467000,      1000,          1}, // JP-2-BW250-1
    {RADIO_CFG_JP,   920900000, 923300000,    400000,                        13,      4000,        50,      5000,   -80, 467000,     10000,          1}, // JP-1-BW250-2
    {RADIO_CFG_JP,   923700000, 927700000,    400000,                        13,       200,         2,      1000,   -80, 467000,      1000,          1}, // JP-2-BW250-2
#endif
#if defined(RADIO_CFG_JP_LDC_ENABLED)
    {RADIO_CFG_JP_LDC, 920600000, 923400000,  200000,                        13,      4000,        50,         0,     0,      0,       100,          0}, // JP-1-BW125-LDC
#endif
};

#define RP_REGION_NO_OF_BANDS_IN_TOTAL  (sizeof(RpRegionBandDef)/sizeof(RpRegionBand_t))

#if defined(RADIO_CFG_EU_ENABLED)
#define RP_REGION_MAX_NO_OF_BANDS_IN_REGION (12)    //!< Maximum number of radio bands defined in region
#elif defined(RADIO_CFG_JP_ENABLED)
#define RP_REGION_MAX_NO_OF_BANDS_IN_REGION (6)     //!< Maximum number of radio bands defined in region
#elif defined(RADIO_CFG_AS1_ENABLED) || defined(RADIO_CFG_AS2_ENABLED) || defined(RADIO_CFG_AS3_ENABLED) || defined(RADIO_CFG_AS4_ENABLED) \
        || defined(RADIO_CFG_US_ENABLED) || defined(RADIO_CFG_AU_ENABLED) || defined(RADIO_CFG_KR_ENABLED)
#define RP_REGION_MAX_NO_OF_BANDS_IN_REGION (2)     //!< Maximum number of radio bands defined in region
#elif defined(RADIO_CFG_IN_ENABLED) || defined(RADIO_CFG_JP_LDC_ENABLED)
#define RP_REGION_MAX_NO_OF_BANDS_IN_REGION (1)     //!< Maximum number of radio bands defined in region
#else
#error "error: RADIO_CFG_xx_ENABLED is undefined"
#endif

//
// Global variable
//
static RpRegionBandMng_t     RpRegionBand[RP_REGION_MAX_NO_OF_BANDS_IN_REGION];   //!< Holds band status and band configurations
static RpRegionRadioCfg_t    RpRegionRadioCfg;                      //!< Holds radio parameters
static bool                  RpRegionIsInitialized = false;         //!< radio/region layer initialization flag


//
// Global variable (extern)
//
extern const uint32_t              LoraRegBandwidth[11];            //!< Defined in radio.c and contains LoRa bandwidths corresponding to register values
extern const RadioLoRaBandwidths_t Bandwidths[];                    //!< Resolves LoRa bandwidth, defined in radio.c
extern PIB_VAL_t PibValues;

void RpRegionInitPib(void)
{
    if(!RpRegionIsInitialized)
    {
         PibValues.region =
#if defined(RADIO_CFG_EU_ENABLED)
                RADIO_CFG_EU;
#elif defined(RADIO_CFG_IN_ENABLED)
                RADIO_CFG_IN;
#elif defined(RADIO_CFG_AS1_ENABLED)
                RADIO_CFG_AS1;
#elif defined(RADIO_CFG_AS2_ENABLED)
                RADIO_CFG_AS2;
#elif defined(RADIO_CFG_AS3_ENABLED)
                RADIO_CFG_AS3;
#elif defined(RADIO_CFG_AS4_ENABLED)
                RADIO_CFG_AS4;
#elif defined(RADIO_CFG_US_ENABLED)
                RADIO_CFG_US;
#elif defined(RADIO_CFG_AU_ENABLED)
                RADIO_CFG_AU;
#elif defined(RADIO_CFG_KR_ENABLED)
                RADIO_CFG_KR;
#elif defined(RADIO_CFG_JP_ENABLED)
                RADIO_CFG_JP;
#elif defined(RADIO_CFG_JP_LDC_ENABLED)
                RADIO_CFG_JP_LDC;
#endif
        PibValues.freqHoppingUsed = false;
    }
}

bool RpRegionCheckRadioConfigRegion(RadioConfigRegion_t region)
{
    bool result;

    switch( region )
    {
#if defined(RADIO_CFG_EU_ENABLED)
        case RADIO_CFG_EU:
#endif
#if defined(RADIO_CFG_IN_ENABLED)
        case RADIO_CFG_IN:
#endif
#if defined(RADIO_CFG_AS1_ENABLED)
        case RADIO_CFG_AS1:
#endif
#if defined(RADIO_CFG_AS2_ENABLED)
        case RADIO_CFG_AS2:
#endif
#if defined(RADIO_CFG_AS3_ENABLED)
        case RADIO_CFG_AS3:
#endif
#if defined(RADIO_CFG_AS4_ENABLED)
        case RADIO_CFG_AS4:
#endif
#if defined(RADIO_CFG_US_ENABLED)
        case RADIO_CFG_US:
#endif
#if defined(RADIO_CFG_AU_ENABLED)
        case RADIO_CFG_AU:
#endif
#if defined(RADIO_CFG_KR_ENABLED)
        case RADIO_CFG_KR:
#endif
#if defined(RADIO_CFG_JP_ENABLED)
        case RADIO_CFG_JP:
#endif
#if defined(RADIO_CFG_JP_LDC_ENABLED)
        case RADIO_CFG_JP_LDC:
#endif
            result = true;
            break;
        default:
            result = false;
            break;
    }

    return(result);
}

/*!
 * \brief                  Initializes radio/region layer
 *
 * \param[in]  bandEnMask  Radio band enable mask. Each bit number (0 to 15) corresponds to the radio band of the same number.
 *
 * \return                 None.
 */
bool RpRegionInit( uint16_t bandEnMask )
{
    uint8_t i;
    bool result;
    RadioConfigRegion_t region;

    region = PibValues.region;
    result = RpRegionCheckRadioConfigRegion(region);
    if (result == true)
    {
        memset(&RpRegionRadioCfg.chkMode, 0x00, sizeof(RpRegionChkMode_t));
        memset(&RpRegionRadioCfg.setCh,   0x00, sizeof(RpRegionSetCh_t));
        memset(&RpRegionRadioCfg.rxCfg,   0x00, sizeof(RpRegionRxCfg_t));
        memset(&RpRegionRadioCfg.txCfg,   0x00, sizeof(RpRegionTxCfg_t));

        if((!RpRegionIsInitialized) || (region != RpRegionRadioCfg.region))
        {
            RpRegionRadioCfg.region          = region;
            RpRegionRadioCfg.tsLastTx        = 0ULL;
            RpRegionRadioCfg.lastToa         = 0UL;
            RpRegionRadioCfg.lastTxBand      = 0U;
            RpRegionRadioCfg.lastTxPower     = -128;
            RpRegionRadioCfg.numBands        = 0U;
            
            for(i=0;i<RP_REGION_NO_OF_BANDS_IN_TOTAL;i++)
            {
                if(RpRegionBandDef[i].region == region)
                {
                    if (bandEnMask & 0x0001) 
                    {
                        if (RpRegionRadioCfg.numBands < RP_REGION_MAX_NO_OF_BANDS_IN_REGION)    // fail safe
                        {
                            RpRegionBand[RpRegionRadioCfg.numBands].pBandCfg   = &RpRegionBandDef[i];
                            RpRegionBand[RpRegionRadioCfg.numBands].rssiOffset = RP_REGION_CCA_RSSI_OFFSET;
                            RpRegionBand[RpRegionRadioCfg.numBands].bandEn     = true;
                            RpRegionRadioCfg.numBands++;
                        }
                        bandEnMask >>= 1; 
                    }
                }
            }
            RpRegionIsInitialized = true;
        }
    }

    return(result);
}

/*!
 * \brief  Initializes all global variables managed in the radio/region layer including 
 *         transmission timestamps.
 *
 * \return None.
 */
void RpRegionInitAll( void )
{
    RpRegionIsInitialized = false;
    RpRegionInit(0xffff);
}

/*!
 * \brief Calculates estimated occupied bandwidth.
 *
 * \param [in] modem    Modem Type (MODEM_FSK or MODEM_LORA)
 * \param [in] bwLora   LoRa bandwidth.
 *                      MODEM_FSK : Set 0
 *                      MODEM_LORA: Set 0 to 9 according to the  "bandwidth" argument defitiniton of Radio.SetTxConfig()
 * \param [in] fdevFsk  FSK fdev in Hz. 
 * \param [in] brFsk    FSK bitrate (datarate) in bits per second.
 *
 * \return              Estimated occupied bandwidth for the configurations specified.
 */
uint32_t RpRegionCalcObw( RpRegionModems_t modem, uint32_t bwLora, uint32_t fdevFsk, uint32_t brFsk )
{
    uint32_t obw = 0xFFFFFFFFUL;
    
    switch(modem)
    {
        case MODEM_LORA:
            obw = RpRegionResolveLoraBw(bwLora, true);
            break;
        case MODEM_FSK:
            obw = 2 * fdevFsk + brFsk + RP_REGION_FSK_BW_MARGIN;
            break;
        default:
            /* return the initial value */
            break;
    }
    
    return obw;
}


/*!
 * \brief Resolves LoRa bandwidth from bandwidth index of Radio API.
 *
 * \param [in] bwIndex   LoRa bandwidth in index (0 to 9) of bandwith argument of radio APIs.
 * \param [in] addMargin Decides if bandwidth margin needs to be added
 *
 * \return              LoRa bandwidth in Hz.
 */

uint32_t RpRegionResolveLoraBw( uint32_t bwIndex, bool addMargin )
{
    uint32_t bwHz;
    uint32_t margin = addMargin ? RP_REGION_LORA_BW_MARGIN : 0UL;

    if(bwIndex < (sizeof(LoraRegBandwidth)/sizeof(LoraRegBandwidth[0])))
    {
        bwHz = LoraRegBandwidth[Bandwidths[bwIndex]]; 
        
        if(bwHz == 0UL)
        {
            bwHz  = 0xFFFFFFFFUL - margin; // invalid value
        }
        else
        {
            bwHz += margin; 
        }
    }
    else
    {
        bwHz = 0xFFFFFFFFUL - margin; // Invalid value
    }
    
    return bwHz;
}


/*!
 * \brief Validates 
 *
 * \param [in] freqUsed      Radio frequency (centre frequency, in Hz) to be used. e.g. 923200000 Hz.
 * \param [in] freqRangeLow  Lowest frequency (centre frequency, in Hz) permitted. e.g. 9230000000 Hz.
 * \param [in] freqRangeHigh Highest frequency (centre frequency, in Hz) permitted. e.g. 9234000000 Hz.
 * \param [in] bwUsed        Occupied bandwidth of transmission/reception. e.g. 125000 Hz.
 * \param [in] bwChannel     Radio channel spacing in Hz. e.g. 200000 Hz. 
 * 
 * \return                   Check result. Only one fail cause is returned even if there are multiple fails.
 */
RpRegionRsltFlag_t RpRegionCheckFreqInChannel( uint32_t freqUsed, uint32_t freqRangeLow, uint32_t freqRangeHigh, uint32_t bwUsed, uint32_t bwChannel )
{
    RpRegionRsltFlag_t rslt = RP_REGION_FLAG_SUCCESS;

    // Check 1: Frequency in band?
    if((freqUsed < freqRangeLow) || (freqUsed > freqRangeHigh))
    {
        rslt = RP_REGION_FLAG_FAIL_FREQUENCY;  // Frequency out of range allowed.
    }

    // Check 2: Occupied bandwidth smaller than channel spacing?
    if(rslt == RP_REGION_FLAG_SUCCESS)
    {
        if(bwUsed >= bwChannel)
        {
            rslt = RP_REGION_FLAG_FAIL_FREQUENCY;  // (occupied_bandwidth) >= (channel_spacing)
        }
    }

    // Check 3: Is the centre frequency of a channel used?
    if(rslt == RP_REGION_FLAG_SUCCESS)
    {
        if(((freqUsed - freqRangeLow) % bwChannel) != 0)
        {
             rslt = RP_REGION_FLAG_FAIL_FREQUENCY;  // Channel centre is not used.
        }
    }
    
    return rslt;
}

/*!
 * \brief Validates radio transmission configurations. 
 */
RpRegionResult_t RpRegionSendCheck( uint8_t *pBuf, uint8_t size )
{
    RpRegionChkMode_t   chkMode;
    RpRegionChkResult_t rsltCheck  = {RP_REGION_FAIL, 0};
    RpRegionResult_t    rsltReturn = RP_REGION_SUCCESS;
    
    RpRegionRetrieveCfg(RP_REGION_CFGTYPE_CHECK_MODE, &chkMode);
    
    switch(chkMode)
    {
        case RADIO_CFG_CHK_MODE_0:
            rsltCheck = RpRegionSendCheckMode0( size ); 
            break;
        default:
            break;
    }
    
    if(rsltCheck.checkRslt != RP_REGION_SUCCESS)
    {
        rsltReturn = rsltCheck.checkRslt;
    }
    
    return rsltReturn;
}


/*!
 * \brief Validates transmission configurations (modem parameters and transmission power).
 *
 * \param[in] pTxCfg  Pointer to transmission configurations to validate.
 * \param[in] bandId  ID of radio band.
 *
 * \return Check result.
 */
RpRegionRsltFlag_t RpRegionCheckTxModemCfg( RpRegionTxCfg_t *pTxCfg, uint8_t bandId )
{
    RpRegionRsltFlag_t rslt = RP_REGION_FLAG_FAIL_MODEM_CONFIG;
    uint8_t i;
    uint32_t sfDr, bwFdev;
    uint8_t  modemCfgNo;

    if(RpRegionBand[bandId].bandEn == true)
    {
#if RP_REGION_LIMIT_TX_POWER_ENABLED == 0
        // Check 1: Output power
        if(pTxCfg->power <= (RpRegionBand[bandId].pBandCfg->txMaxPower + RP_REGION_MAX_TX_POWER_OFFSET))      
#endif
        {
            // Check 2: Modem configurations
            if(pTxCfg->modem == MODEM_LORA)
            {
                sfDr   = pTxCfg->datarate;
                bwFdev = RpRegionResolveLoraBw(pTxCfg->bandwidth, false);
            }
            else
            {
                sfDr   = pTxCfg->datarate;
                bwFdev = pTxCfg->fdev;
            }

            modemCfgNo = RpRegionBand[bandId].pBandCfg->modemCfgNo;
            for(i=0; i<RpRegionModemCfg[modemCfgNo].size; i++)
            {
                if(  (pTxCfg->modem == RpRegionModemCfg[modemCfgNo].pModemCfg[i].modem)
                   &&(sfDr          == RpRegionModemCfg[modemCfgNo].pModemCfg[i].sfDr)
                   &&(bwFdev        == RpRegionModemCfg[modemCfgNo].pModemCfg[i].bwFdev))
                {
                    // Found a match in the specified modem configuration set. Break to return Success.
                    rslt = RP_REGION_FLAG_SUCCESS;
                    break;
                }
            }
        }
#if RP_REGION_LIMIT_TX_POWER_ENABLED == 0
        else
        {
            // Defined output power is over the defined maximum.
            rslt = RP_REGION_FLAG_FAIL_TX_POWER;
        }
#endif
    }
    else
    {
        rslt = RP_REGION_FLAG_FAIL_BAND_DISABLED;
    }
    
    return rslt;
}


/*!
 * \brief Validates radio frequency and bandwidth.
 *
 * \param[in] freq    Center frequency in Hz.
 * \param[in] bw      Bandwidth in Hz.
 * \param[in] bandId  ID of radio band.
 *
 * \return Check result.
 */
RpRegionRsltFlag_t RpRegionCheckFreqBand( uint32_t freq, uint32_t bw, uint8_t bandId )
{
    RpRegionRsltFlag_t rslt = RP_REGION_SUCCESS;
    
    do
    {
        // Check 1: Is frequency in range?
        if(    (freq < RpRegionBand[bandId].pBandCfg->freqStart) 
            || (RpRegionBand[bandId].pBandCfg->freqEnd < freq  ) )
        {
            rslt = RP_REGION_FLAG_FAIL_FREQUENCY; 
            break;
        }
        
        // Check 2: Is frequency alligned with channel center?
        if(((freq - RpRegionBand[bandId].pBandCfg->freqStart) % RpRegionBand[bandId].pBandCfg->chSpacing) != 0)
        {
            rslt = RP_REGION_FLAG_FAIL_FREQUENCY; 
            break;
        }
    
        // Check 3: Is occupied bandwidth within channel spacing?
        if(RpRegionBand[bandId].pBandCfg->chSpacing != 1)       // if chSpaceing is 1, skip to compare with bandwidth
        {
            if(RpRegionBand[bandId].pBandCfg->chSpacing < bw) 
            {
                rslt = RP_REGION_FLAG_FAIL_FREQUENCY; 
                break;
            }
        }
    } while(0);
    
    return rslt;
}


/*!
 * \brief Validates packet transmission time (time on air; ToA).
 *
 * \param[in] toa     ToA to be validated in millisecond (ms).
 * \param[in] bandId  ID of radio band.
 *
 * \return Check result.
 */
RpRegionRsltFlag_t RpRegionCheckTimeOnAir( uint32_t toa, uint8_t bandId )
{
    RpRegionRsltFlag_t rslt = RP_REGION_FLAG_SUCCESS;
    
    if(RpRegionBand[bandId].pBandCfg->txMaxTime < toa)
    {
        rslt = RP_REGION_FLAG_FAIL_TIME_ON_AIR;
    }
    
    return rslt;
}


/*!
 * \brief Check if the device can transmit a packet in a radio band under current configurations.
 *
 * \param[in] size Transmission payload size in byte.
 *
 * \return Check result.
 */
RpRegionChkResult_t RpRegionSendCheckMode0( uint8_t size ) 
{
    RpRegionChkResult_t rsltReturn = {.checkRslt = RP_REGION_CHECK_FAIL_TX_CFG, .passedBand = 255U};
    RpRegionRsltFlag_t  rsltAll[RP_REGION_MAX_NO_OF_BANDS_IN_REGION] = {RP_REGION_FLAG_SUCCESS,};
    RpRegionTxCfg_t     txCfg;
    RpRegionSetCh_t     freqCfg;
    uint32_t            toa, obw;
    uint8_t             i;
    bool                rsltCca;
    uint16_t            ccaTime16;
    uint32_t            ccaBwBackup;
    int8_t              rssiOffsetBackup;
    
    // Retrieve transmission configurations
    RpRegionRetrieveCfg(RP_REGION_CFGTYPE_TXCFG, &txCfg);
    RpRegionRetrieveCfg(RP_REGION_CFGTYPE_SETCH, &freqCfg);

    // Calculate occupied bandwidth that is used in Check 1-1
    obw         = RpRegionCalcObw(txCfg.modem, txCfg.bandwidth, txCfg.fdev, txCfg.datarate);
    
    // Calculate transmission packet "time on air" that is used in Check 1-2
    toa         = Radio.TimeOnAir((RadioModems_t)txCfg.modem, size);
    
    for(i = 0; i < RpRegionRadioCfg.numBands; i++)
    {   
#if defined(RADIO_CFG_US_ENABLED) || defined(RADIO_CFG_AU_ENABLED)
        if ((RpRegionRadioCfg.region == RADIO_CFG_US) || (RpRegionRadioCfg.region == RADIO_CFG_AU))
        {
            if (PibValues.freqHoppingUsed == false)
            {
                if (RpRegionBand[i].pBandCfg->modemCfgNo == RP_REGION_MODEM_CFG_NO_AVAIABLE_WITH_FREQ_HOPPING)
                {
                    // Skip this band because it cannot be used when the upper layer does not utilize the frequency hopping
                    continue;
                }
            }
        }
#endif
        if(RpRegionBand[i].bandEn)
        {
            // Check 1: Radio configurations
            {
                // Check 1-1: Center frequency and bandwidth
                rsltAll[i] |= RpRegionCheckFreqBand(freqCfg, obw, i);

                // Check 1-2: Modulation and output power 
                rsltAll[i] |= RpRegionCheckTxModemCfg(&txCfg, i);

                // Check 1-3: Transmission time (time on air)
                rsltAll[i] |= RpRegionCheckTimeOnAir(toa, i);
            }

            if(rsltAll[i] != RP_REGION_FLAG_SUCCESS)
            {
                rsltReturn.checkRslt = RP_REGION_CHECK_FAIL_TX_CFG;
            }
            else
            {
                // Check 2: Transmission timing
                {
#if RP_REGION_DUTY_CYCLE_CHECK_DISABLED == 0
                    // Check 2-1: Transmission duty cycle
                    rsltAll[i] |= RpRegionCheckDutyCycle();
#endif
                    // Check 2-2: Minimum transmission interval
                    rsltAll[i] |= RpRegionCheckTxInterval();
                }
                    
                // Check 3: Carrier sense 
                if(rsltAll[i] == RP_REGION_SUCCESS)
                {
                    if(RP_REGION_MIN_CCA_TIME <= RpRegionBand[i].pBandCfg->ccaTime)
                    {
                        Radio.GetPib(PIB_CCA_BANDWIDTH, (uint8_t *)&ccaBwBackup);                       // Backup current carrier sense bandwidth configuration
                        Radio.GetPib(PIB_RSSI_OFFSET  , (uint8_t *)&rssiOffsetBackup);                  // Backup current RSSI offset configuration
                        Radio.SetPib(PIB_CCA_BANDWIDTH, (uint8_t *)&(RpRegionBand[i].pBandCfg->ccaBw)); // Set expected carrier sense bandwidth
                        Radio.SetPib(PIB_RSSI_OFFSET  , (uint8_t *)&(RpRegionBand[i].rssiOffset));      // Set expected carrier sense offset
                        ccaTime16 = (uint16_t)(RpRegionBand[i].pBandCfg->ccaTime / 1000UL);                       // Perform carrier sense
                        rsltCca   = Radio.IsChannelFree((RadioModems_t)txCfg.modem,freqCfg, RpRegionBand[i].pBandCfg->ccaTh, ccaTime16);
                        Radio.SetPib(PIB_CCA_BANDWIDTH, (uint8_t *)&ccaBwBackup);                       // Set carrier sense bandwidth config back to the original value
                        Radio.SetPib(PIB_RSSI_OFFSET  , (uint8_t *)&rssiOffsetBackup);                  // Set RSSI offset config back to the original value
                        if(!rsltCca)
                        {
                            rsltAll[i] |= RP_REGION_FLAG_FAIL_CARRIER_SENSE;
                        }
                    }
                }
                
                if(rsltAll[i] == RP_REGION_SUCCESS)
                {
                    rsltReturn.checkRslt  = RP_REGION_SUCCESS;
                    rsltReturn.passedBand = i;
                    
                    // update the transmission timestamp and time on air
                    RpRegionRadioCfg.tsLastTx   = TimerGetCurrentTime();
                    RpRegionRadioCfg.lastToa    = toa;
                    RpRegionRadioCfg.lastTxBand = i;

#if RP_REGION_LIMIT_TX_POWER_ENABLED == 1
                    // Limit the specified tx power to the max value defined in the band table
                    {
                        int8_t power;
                        int8_t max_power = RpRegionBand[i].pBandCfg->txMaxPower + RP_REGION_MAX_TX_POWER_OFFSET;
                        if(txCfg.power > max_power)
                        {
                            power = max_power;
                        }
                        else
                        {
                            power = txCfg.power;
                        }
                        if (RpRegionRadioCfg.lastTxPower != power)
                        {
                            RpRegionRadioCfg.lastTxPower = power;
                            SX126xSetRfTxPower( power );
                        }
                    }
#endif
                }
                else
                {
                    if(rsltAll[i] & (RP_REGION_FLAG_FAIL_DUTY_CYCLE | RP_REGION_FLAG_FAIL_TX_INTERVAL))
                    {
                        rsltReturn.checkRslt = RP_REGION_CHECK_FAIL_TX_DUTY_CYCLE;
                    }
                    else if(rsltAll[i] & RP_REGION_FLAG_FAIL_CARRIER_SENSE)
                    {
                        rsltReturn.checkRslt = RP_REGION_CHECK_FAIL_TX_CHANNEL_BUSY ;
                    }
                }
                
                // End check whether or not checks 2 and 3 passed.
                break;
            }
        }
    }
    
    return rsltReturn;
}


/*!
 * \brief Check if the device is under restriction of transmission interval.
 *
 * \return Check result.
 */
RpRegionRsltFlag_t RpRegionCheckTxInterval( void )
{
    RpRegionRsltFlag_t rslt = RP_REGION_FLAG_SUCCESS;
    
    if((RpRegionRadioCfg.lastToa != 0UL) && (RpRegionBand[RpRegionRadioCfg.lastTxBand].pBandCfg->txSlpTime != 0U))
    {
        TimerTime_t tsNow, timeElapsed, timeSlp;
        tsNow       = (TimerTime_t)TimerGetCurrentTime();
        
        if(tsNow < (RpRegionRadioCfg.tsLastTx + (TimerTime_t)RpRegionRadioCfg.lastToa + RP_REGION_TIME_CALC_MIN_MARGIN))
        {
            timeElapsed = 0;
        }
        else
        {
            timeElapsed = tsNow - (RpRegionRadioCfg.tsLastTx + RpRegionRadioCfg.lastToa + RP_REGION_TIME_CALC_MIN_MARGIN);
        }
        
        timeSlp =  (TimerTime_t) RpRegionBand[RpRegionRadioCfg.lastTxBand].pBandCfg->txSlpTime;
        
        if(timeElapsed < timeSlp)
        {
            rslt = RP_REGION_FLAG_FAIL_TX_INTERVAL;
        }
    }
    
    return rslt;
}

/*!
 * \brief Check if the device is under restriction of transmission duty cycle backoff.
 *
 * \param[in] toa       ID of radio band to check.
 * \param[in] dutyCycle Minimum transmission duty cycle in basis point (bp). 1 % = 100 bp, 100 % = 10,000 bp. 
 *
 * \return Minimum transmission backoff (in millisecond) by duty cycle restriction. 0 means no backoff required.
 */
uint32_t RpRegionCalcDutyCycleBackoff( uint32_t toa, uint16_t dutyCycle )
{
    volatile uint32_t calc = 0UL;
    uint32_t backoff = 0UL;
    
    if(dutyCycle == 0U)
    {
        dutyCycle = 1U;
    }
    else if (dutyCycle > 10000U)
    {
        dutyCycle = 10000U;
    }
    
    if(dutyCycle != 10000U)
    {
        if     ((uint16_t)(dutyCycle / 1000U) != 0U)
        {
            calc = 1000UL;
        }
        else if((uint16_t)(dutyCycle /  100U) != 0U)
        {
            calc = 100UL;
        }
        else if((uint16_t)(dutyCycle /   10U) != 0U)
        {
            calc = 10UL;
        }
        else
        {
            calc = 1UL;
        }
        
        backoff = ((uint32_t)((uint32_t)(10000U - dutyCycle) / calc)) * toa;
    }
    
    return backoff;
}

/*!
 * \brief Check if the device is under duty cycle restriction of a radio band.
 *
 * \return Check result.
 */
RpRegionRsltFlag_t RpRegionCheckDutyCycle( void )
{
    RpRegionRsltFlag_t rslt = RP_REGION_FLAG_SUCCESS;
    TimerTime_t  tsNow, elapsedTime, slpTime; 

    if((RpRegionRadioCfg.lastToa != 0) && (RpRegionBand[RpRegionRadioCfg.lastTxBand].pBandCfg->dutyCycle >= 1))
    {
        tsNow = TimerGetCurrentTime();
        if(tsNow < (RpRegionRadioCfg.tsLastTx + RpRegionRadioCfg.lastToa + RP_REGION_TIME_CALC_MIN_MARGIN))
        {
            elapsedTime = 0;
        }
        else
        {
            elapsedTime = tsNow - (RpRegionRadioCfg.tsLastTx + RpRegionRadioCfg.lastToa + RP_REGION_TIME_CALC_MIN_MARGIN);
        }

        // Calculate required sleep time after last transmission
        slpTime = (TimerTime_t)RpRegionCalcDutyCycleBackoff((uint64_t)RpRegionRadioCfg.lastToa, RpRegionBand[RpRegionRadioCfg.lastTxBand].pBandCfg->dutyCycle);

        // Check if required sleep time has elapsed since last tranmission
        if((slpTime != 0UL) && (slpTime > elapsedTime))
        {
            rslt = RP_REGION_FLAG_FAIL_DUTY_CYCLE;
        }
    }

    return rslt;
}


/*!
 * \brief Check if specified frequency is within in any of the radio bands defined. This function is dedicated to Radio.CheckRfFrequency.
 *
 * \param  [in] frequency    Radio frequency to be checked in Hz. e.g. 923000000.
 *
 * \return                   Check result.
 *         \retval  true     The specified frequency is allowed to use
 *         \retval  false    The specified frequency is not allowed to use (not in any of the predefined radio bands).
 */
bool RpRegionCheckRfFrequency( uint32_t frequency )
{
    bool    rslt = false;
    uint8_t i;
    
    for(i=0;i<RpRegionRadioCfg.numBands;i++)
    {
#if defined(RADIO_CFG_US_ENABLED) || defined(RADIO_CFG_AU_ENABLED)
        if ((RpRegionRadioCfg.region == RADIO_CFG_US) || (RpRegionRadioCfg.region == RADIO_CFG_AU))
        {
            if (PibValues.freqHoppingUsed == false)
            {
                if (RpRegionBand[i].pBandCfg->modemCfgNo == RP_REGION_MODEM_CFG_NO_AVAIABLE_WITH_FREQ_HOPPING)
                {
                    // Skip this band because it cannot be used when the upper layer currently does not utilize the frequency hopping
                    continue;
                }
            }
        }
#endif
        if(  (RpRegionBand[i].bandEn == true)
           &&((RpRegionBand[i].pBandCfg->freqStart <= frequency) && (frequency <= RpRegionBand[i].pBandCfg->freqEnd)))
        {
            rslt = true;
            break;
        }
    }

    return rslt;
}


/*!
 * \brief  Parameters storage function forwarded from Radio.SetChannel()
 *
 * \param  [in] frequency    Radio frequency to be checked in Hz. e.g. 923000000.
 *
 * \return                   Check result only RADIO_REGION_SUCCESS returns.
 *      \retval  RADIO_REGION_SUCCESS  SetChannel request correctly processed (equivalent to RADIO_SUCCESS).  @see RADIO_SUCCESS
 */
RpRegionResult_t RpRegionSetChannel( uint32_t freq )
{
    RpRegionSetCh_t  cfg;
    RpRegionResult_t rslt = RP_REGION_SUCCESS;
    
    memset(&cfg, 0, sizeof(cfg));
    cfg = freq;
    RpRegionStoreCfg(RP_REGION_CFGTYPE_SETCH, &cfg);
    
    return rslt;
}


/*!
 * \brief Parameters storage function forwarded from Radio.SetRxConfig(). 
 *        Arguments are same as Radio.SetRxConfig().  @see RxConfig
 *
 * \return Process result. only RP_REGION_SUCCESS returns.
 *   \retval RP_REGION_SUCCESS success.
 */
RpRegionResult_t RpRegionSetRxConfig( RpRegionModems_t modem, uint32_t bandwidth,
                                      uint32_t datarate, uint8_t coderate,
                                      uint32_t bandwidthAfc, uint16_t preambleLen,
                                      uint16_t symbTimeout, bool fixLen,
                                      uint8_t payloadLen,
                                      bool crcOn, bool freqHopOn, uint8_t hopPeriod,
                                      bool iqInverted, bool rxContinuous )
{
    RpRegionResult_t rslt = RP_REGION_SUCCESS;
    RpRegionRxCfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));

    cfg.modem       = modem;
    cfg.bandwidth   = bandwidth;
    cfg.datarate    = datarate;
    cfg.coderate    = coderate;
    cfg.preambleLen = preambleLen;
    cfg.fixLen      = fixLen;
    cfg.crcOn       = crcOn;
    
    RpRegionStoreCfg(RP_REGION_CFGTYPE_RXCFG, &cfg);
    
    return rslt;
}


/*!
 * \brief Parameters storage function forwarded from Radio.SetTxConfig().
 */
RpRegionResult_t RpRegionSetTxConfig( RpRegionModems_t modem, int8_t power, uint32_t fdev,
                                      uint32_t bandwidth, uint32_t datarate,
                                      uint8_t coderate, uint16_t preambleLen,
                                      bool fixLen, bool crcOn, bool freqHopOn,
                                      uint8_t hopPeriod, bool iqInverted, uint32_t timeout )
{
    RpRegionResult_t rslt = RP_REGION_SUCCESS;
    RpRegionTxCfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));

    cfg.modem       = modem;
    cfg.power       = power;
    cfg.fdev        = fdev;
    cfg.bandwidth   = bandwidth;
    cfg.datarate    = datarate;
    cfg.coderate    = coderate;
    cfg.preambleLen = preambleLen;
    cfg.fixLen      = fixLen;
    cfg.crcOn       = crcOn;
    
    RpRegionStoreCfg(RP_REGION_CFGTYPE_TXCFG, &cfg);
    RpRegionRadioCfg.lastTxPower = power;

    return rslt;
}
    

/*!
 * \brief Store radio configurations of a specified type to global variable.
 * 
 * \param[in] cfgType Type of configurations to store. @see RpRegionRadioCfgType_t
 * \param[in] pCfg    Pointer to the configurations variable.
 *
 * return None.
 */
void RpRegionStoreCfg( RpRegionRadioCfgType_t cfgType, void *pCfg )
{
    CRITICAL_SECTION_BEGIN();

    switch(cfgType)
    {
        case RP_REGION_CFGTYPE_ALL:
            RpRegionRadioCfg         = *(RpRegionRadioCfg_t *)pCfg;
            break;
        case RP_REGION_CFGTYPE_CHECK_MODE:
            RpRegionRadioCfg.chkMode = *(RpRegionChkMode_t *) pCfg;
            break;
        case RP_REGION_CFGTYPE_RXCFG:
            RpRegionRadioCfg.rxCfg   = *(RpRegionRxCfg_t *)pCfg;
            break;
        case RP_REGION_CFGTYPE_TXCFG:
            RpRegionRadioCfg.txCfg   = *(RpRegionTxCfg_t *)pCfg;
            break;
        case RP_REGION_CFGTYPE_SETCH:
            RpRegionRadioCfg.setCh   = *(RpRegionSetCh_t *)pCfg;
            break;
        default:
            break;
    }

    CRITICAL_SECTION_END();
}


/*!
 * \brief Retrieves radio configurations from global variable.
 *
 * \param[in] cfgType Type of configuration to retrieve. @see RpRegionRadioCfgType_t
 * \param[in] pCfg    Pointer to the memory area to store the retrieved configuration.
 *
 * return None.
 */
void RpRegionRetrieveCfg( RpRegionRadioCfgType_t cfgType, void *pCfg )
{
    CRITICAL_SECTION_BEGIN();
    
    switch(cfgType)
    {
        case RP_REGION_CFGTYPE_ALL:
            *(RpRegionRadioCfg_t *)pCfg = RpRegionRadioCfg;
            break;
        case RP_REGION_CFGTYPE_CHECK_MODE:
            *(RpRegionChkMode_t *) pCfg = RpRegionRadioCfg.chkMode;
            break;
        case RP_REGION_CFGTYPE_RXCFG:
            *(RpRegionRxCfg_t *)   pCfg = RpRegionRadioCfg.rxCfg;
            break;
        case RP_REGION_CFGTYPE_TXCFG:
            *(RpRegionTxCfg_t *)   pCfg = RpRegionRadioCfg.txCfg;
            break;
        case RP_REGION_CFGTYPE_SETCH:
            *(RpRegionSetCh_t *)   pCfg = RpRegionRadioCfg.setCh;
            break;
        default:
            break;
    }
    
    CRITICAL_SECTION_END();
}

/*!
 * \brief    Validates radio reception configurations.
 *           Argument is same as Radio.Rx().  @see Rx
 *
 * \return   Validation result.
    \retval RP_REGION_SUCCESS             Reception for the current configurations is allowd.
    \retval RP_REGION_CHECK_FAIL_RX_CFG   Reception for the current configurations is not allowed.
 */
RpRegionResult_t RpRegionRxCheck( uint32_t timeout )
{
    RpRegionChkMode_t   chkMode;
    RpRegionChkResult_t rsltCheck = {RP_REGION_FAIL, 0};
    RpRegionResult_t    rslt = RP_REGION_SUCCESS;
    
    RpRegionRetrieveCfg(RP_REGION_CFGTYPE_CHECK_MODE, &chkMode);
    
    switch(chkMode)
    {
        case RADIO_CFG_CHK_MODE_0:
            rsltCheck = RpRegionRxCheckMode0();
            break;
        default:
            break;
    }
    
    if(rsltCheck.checkRslt != RP_REGION_SUCCESS)
    {
        rslt = rsltCheck.checkRslt;
    }
    
    return rslt;
}

/*!
 * \brief                        Checks if current reception configurations are valid.
 *
 * \return                       Check result. "passedBand" member of the return shows the band number 
 *       \retval RP_REGION_CHECK_FAIL_RX_CFG(.checkRslt)   Current reception configurations are not allowd.
 *       \retval RP_REGION_SUCESS(.checkRslt)              Current reception configurations are allow
 */
RpRegionChkResult_t RpRegionRxCheckMode0( void )
{
    RpRegionChkResult_t rsltReturn = {.checkRslt = RP_REGION_CHECK_FAIL_RX_CFG, .passedBand = 255U};
    RpRegionRsltFlag_t  rsltAll[RP_REGION_MAX_NO_OF_BANDS_IN_REGION] = {RP_REGION_FLAG_SUCCESS,};
    RpRegionRxCfg_t     rxCfg;
    RpRegionSetCh_t     freqCfg;
    uint32_t            obw, bw;
    uint8_t             i, j;
    uint8_t             modemCfgNo;
    
    RpRegionRetrieveCfg(RP_REGION_CFGTYPE_RXCFG, &rxCfg);
    RpRegionRetrieveCfg(RP_REGION_CFGTYPE_SETCH, &freqCfg);

    if(rxCfg.modem == MODEM_FSK)
    {
        obw = rxCfg.bandwidth;
    }
    else
    {
        obw = RpRegionCalcObw(rxCfg.modem, rxCfg.bandwidth, 0, 0);
    }
    
    for(i = 0; i < RpRegionRadioCfg.numBands; i++)
    {
#if defined(RADIO_CFG_US_ENABLED) || defined(RADIO_CFG_AU_ENABLED)
        if ((RpRegionRadioCfg.region == RADIO_CFG_US) || (RpRegionRadioCfg.region == RADIO_CFG_AU))
        {
            if (PibValues.freqHoppingUsed == false)
            {
                if (RpRegionBand[i].pBandCfg->modemCfgNo == RP_REGION_MODEM_CFG_NO_AVAIABLE_WITH_FREQ_HOPPING)
                {
                    // Skip this band because it cannot be used when the upper layer currently does not utilize the frequency hopping
                    continue;
                }
            }
        }
#endif
        if(RpRegionBand[i].bandEn)
        {
            // Check 1: Radio configurations
            {
                // Check 1-1: Center frequency and bandwidth
                rsltAll[i] |= RpRegionCheckFreqBand(freqCfg, obw, i);

                // Check 1-2: Modulation 
                if(rsltAll[i] == RP_REGION_FLAG_SUCCESS)
                {
                    modemCfgNo = RpRegionBand[i].pBandCfg->modemCfgNo;
                    if(rxCfg.modem == MODEM_LORA)
                    {
                        // LoRa: check if the specified SF and bandwidth are defined in the modem configurations table.
                        bw = RpRegionResolveLoraBw(rxCfg.bandwidth, false);
                        for(j=0; j<RpRegionModemCfg[modemCfgNo].size; j++)
                        {
                            if(  (rxCfg.modem    == RpRegionModemCfg[modemCfgNo].pModemCfg[j].modem)
                               &&(rxCfg.datarate == RpRegionModemCfg[modemCfgNo].pModemCfg[j].sfDr)
                               &&(bw             == RpRegionModemCfg[modemCfgNo].pModemCfg[j].bwFdev)   )
                            {
                                rsltReturn.checkRslt  = RP_REGION_SUCCESS;
                                rsltReturn.passedBand = i;
                                break;
                            }
                        }
                    }
                    else
                    {
                        // FSK: just check if FSK is defined in the modem configurations table.
                        for(j=0; j<RpRegionModemCfg[modemCfgNo].size; j++)
                        {
                            if(rxCfg.modem == RpRegionModemCfg[modemCfgNo].pModemCfg[j].modem)
                            {
                                rsltReturn.checkRslt  = RP_REGION_SUCCESS;
                                rsltReturn.passedBand = i;
                                break;
                            }
                        }
                    }
                }
                
                if(rsltReturn.checkRslt == RP_REGION_SUCCESS)
                {
                    break;
                }
            }
        }
    }
    
    return rsltReturn;
}


/*!
 * \brief                        Checks if specified continuous modulated (preamble) transmission is permitted.
 *
 * \param[in]    freq            Frequency.
 * \param[in]    powe            Transmission power in dBm.
 * \param[in]    time            Transmission time in second (s).
 *
 * \return                       Check result.
 *       \retval RP_REGION_FAIL  Continuous modulated transmission is not permitted. This value is equivalent to RADIO_FAIL.   @see RADIO_FAIL
 */
RpRegionResult_t RpRegionTxInfinitePreambleCheck(uint8_t freq, int8_t power, uint16_t time)
{
    // Continuos preamble transmission is not allowed.
    return RP_REGION_FAIL;
}

/*!
 * \brief                        Checks if specied continuous unmodulated (single tone) transmission is permitted.
 *
 * \param[in]    freq            Frequency.
 * \param[in]    powe            Transmission power in dBm.
 * \param[in]    time            Transmission time in second (s).
 *
 * \return                       Check result. 
 *       \retval RP_REGION_FAIL  Continuous unmodulated transmission is not permitted. This value is equivalent to RADIO_FAIL.   @see RADIO_FAIL
 */
RpRegionResult_t RpRegionTxContCheck(uint8_t freq, int8_t power, uint16_t time)
{
    // Continuos transmission of single tone signal is not allowed.
    return RP_REGION_FAIL;
}

int32_t RpRegionGetTimeToNextTx( void )
{
    uint8_t     i;
    int32_t     rslt = 0L;
    uint32_t    slpTimeDcycle;
    TimerTime_t elapsedTime, curTime, slpTime;
    bool        noBandEnabled = true;
    
    for(i=0; i<RpRegionRadioCfg.numBands; i++)
    {
#if defined(RADIO_CFG_US_ENABLED) || defined(RADIO_CFG_AU_ENABLED)
        if ((RpRegionRadioCfg.region == RADIO_CFG_US) || (RpRegionRadioCfg.region == RADIO_CFG_AU))
        {
            if (PibValues.freqHoppingUsed == false)
            {
                if (RpRegionBand[i].pBandCfg->modemCfgNo == RP_REGION_MODEM_CFG_NO_AVAIABLE_WITH_FREQ_HOPPING)
                {
                    // Skip this band because it cannot be used when the upper layer currently does not utilize the frequency hopping
                    continue;
                }
            }
        }
#endif
        if(RpRegionBand[i].bandEn)
        {
            noBandEnabled    = false;
        }
    }
    
    //Return -1 as error if there is no radio band defined or enabled
    if(noBandEnabled)
    {
        rslt = -1L;
    }
    else
    {
        if(RpRegionRadioCfg.lastToa != 0UL)
        {
            //Calculate elapsed time
            curTime          = TimerGetCurrentTime();
            if(curTime > (RpRegionRadioCfg.tsLastTx + RP_REGION_TIME_CALC_MIN_MARGIN))
            {
                elapsedTime =  curTime - (RpRegionRadioCfg.tsLastTx + RP_REGION_TIME_CALC_MIN_MARGIN);
            }
            else
            {
                elapsedTime = 0;
            }
            
            //Calculate expected transmission wait time
            slpTimeDcycle    = RpRegionCalcDutyCycleBackoff(RpRegionRadioCfg.lastToa, RpRegionBand[RpRegionRadioCfg.lastTxBand].pBandCfg->dutyCycle);
            if(slpTimeDcycle >= RpRegionBand[RpRegionRadioCfg.lastTxBand].pBandCfg->txSlpTime)
            {
                slpTime = slpTimeDcycle;
            }
            else
            {
                slpTime = RpRegionBand[RpRegionRadioCfg.lastTxBand].pBandCfg->txSlpTime;
            }
            slpTime += RpRegionRadioCfg.lastToa;

            if(slpTime > elapsedTime)
            {
                rslt = slpTime - elapsedTime;
            }
        }
    }
    
    return rslt;
}

#endif // RP_USE_RADIO_CFG_CHECK
