/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacRegion.c
  * @author  Renesas Electronics Corporation
  * @brief
**/


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "board.h"
#include "radio.h"

#include "PrivateLoRa.h"
#include "PrvLoRaMacRadio.h"
#include "PrvLoRaMacRegion.h"
#include "PrvLoRaMacFrame.h"


/*--------*/
/* define */
#if !defined(RADIO_CFG_AS_ENABLED) && !defined(RADIO_CFG_EU_ENABLED) && !defined(RADIO_CFG_US_ENABLED) && \
    !defined(RADIO_CFG_AU_ENABLED) && !defined(RADIO_CFG_IN_ENABLED) && !defined(RADIO_CFG_KR_ENABLED)
#error "None of the RadioCfg definitions are specified."
#endif

#define PRVLORA_REGION_NOTDEFINED                       MAXNUM_PRVLORA_REGION

//
#define PRVLORA_REGION_DEFAULT_CHID                     0
#define PRVLORA_REGION_DEFAULT_TXPOWER                  0

//
#define PRVLORA_REGION_MAX_FRAMESIZE                    PRVLORA_FRAME_MAXSIZE
#define PRVLORA_REGION_TX_TIMEOUT_MS                    10000


// for AS1,2,3,4,JP
#if defined(RADIO_CFG_AS_ENABLED)
    // for AS1,2,3,4
    #define PRVLORA_REGION_AS_MAXNUM_DR                 8
    #define PRVLORA_REGION_AS_DEFAULT_DRINDEX           2
    #define PRVLORA_REGION_AS_DEFAULT_CHID              PRVLORA_REGION_DEFAULT_CHID
    #define PRVLORA_REGION_AS_DEFAULT_TXPOWER           PRVLORA_REGION_DEFAULT_TXPOWER

    // for JP
    #define PRVLORA_REGION_JP_MAXNUM_DR                 10
    #define PRVLORA_REGION_JP_DEFAULT_DRINDEX           2
    #define PRVLORA_REGION_JP_DEFAULT_CHID              PRVLORA_REGION_DEFAULT_CHID
    #define PRVLORA_REGION_JP_DEFAULT_TXPOWER           PRVLORA_REGION_DEFAULT_TXPOWER

    // for JP-LDC
    #define PRVLORA_REGION_JP_LDC_MAXNUM_DR             7
    #define PRVLORA_REGION_JP_LDC_DEFAULT_DRINDEX       2
    #define PRVLORA_REGION_JP_LDC_DEFAULT_CHID          PRVLORA_REGION_DEFAULT_CHID
    #define PRVLORA_REGION_JP_LDC_DEFAULT_TXPOWER       PRVLORA_REGION_DEFAULT_TXPOWER
#endif

// for EU
#if defined(RADIO_CFG_EU_ENABLED)
    #define PRVLORA_REGION_EU_MAXNUM_DR                 8
    #define PRVLORA_REGION_EU_DEFAULT_DRINDEX           2
    #define PRVLORA_REGION_EU_DEFAULT_CHID              PRVLORA_REGION_DEFAULT_CHID
    #define PRVLORA_REGION_EU_DEFAULT_TXPOWER           PRVLORA_REGION_DEFAULT_TXPOWER
#endif

// for US
#if defined(RADIO_CFG_US_ENABLED)
    #define PRVLORA_REGION_US_MAXNUM_DR                 1
    #define PRVLORA_REGION_US_DEFAULT_DRINDEX           0
    #define PRVLORA_REGION_US_DEFAULT_CHID              PRVLORA_REGION_DEFAULT_CHID
    #define PRVLORA_REGION_US_DEFAULT_TXPOWER           PRVLORA_REGION_DEFAULT_TXPOWER
#endif

// for AU
#if defined(RADIO_CFG_AU_ENABLED)
    #define PRVLORA_REGION_AU_MAXNUM_DR                 1
    #define PRVLORA_REGION_AU_DEFAULT_DRINDEX           0
    #define PRVLORA_REGION_AU_DEFAULT_CHID              PRVLORA_REGION_DEFAULT_CHID
    #define PRVLORA_REGION_AU_DEFAULT_TXPOWER           PRVLORA_REGION_DEFAULT_TXPOWER
#endif

// for IN
#if defined(RADIO_CFG_IN_ENABLED)
    #define PRVLORA_REGION_IN_MAXNUM_DR                 7
    #define PRVLORA_REGION_IN_DEFAULT_DRINDEX           2
    #define PRVLORA_REGION_IN_DEFAULT_CHID              PRVLORA_REGION_DEFAULT_CHID
    #define PRVLORA_REGION_IN_DEFAULT_TXPOWER           PRVLORA_REGION_DEFAULT_TXPOWER
#endif

// for KR
#if defined(RADIO_CFG_KR_ENABLED)
    #define PRVLORA_REGION_KR_MAXNUM_DR                 6
    #define PRVLORA_REGION_KR_DEFAULT_DRINDEX           2
    #define PRVLORA_REGION_KR_DEFAULT_CHID              PRVLORA_REGION_DEFAULT_CHID
    #define PRVLORA_REGION_KR_DEFAULT_TXPOWER           PRVLORA_REGION_DEFAULT_TXPOWER
#endif

/*!
 * \brief Returns `N / D` rounded to the smallest integer value greater than or equal to `N / D`
 * \warning when `D == 0`, the result is undefined
 * \remark `N` and `D` can be signed or unsigned
 * \param [IN] N the numerator, which can have any sign
 * \param [IN] D the denominator, which can have any sign
 * \retval N / D with any fractional part rounded to the smallest integer value greater than or equal to `N / D`
 */
#define DIV_CEIL( N, D )                                                       \
    (                                                                          \
        ( N > 0 ) ?                                                            \
        ( ( ( N ) + ( D ) - 1 ) / ( D ) ) :                                    \
        ( ( N ) / ( D ) )                                                      \
    )

/*!
 * \brief Returns `N / D` rounded to the largest integer value smaller than or equal to `N / D`
 * \warning when `D == 0`, the result is undefined
 * \remark `N` and `D` can be signed or unsigned
 * \param [IN] N the numerator, which can have any sign
 * \param [IN] D the denominator, which can have any sign
 * \retval N / D with any fractional part rounded to the largest integer value smaller than or equal to `N / D`
 */
#define DIV_FLOOR( N, D )                                                       \
    (                                                                          \
        ( N < 0 ) ?                                                            \
        ( ( ( N ) - ( D ) + 1 ) / ( D ) ) :                                    \
        ( ( N ) / ( D ) )                                                      \
    )



/*----------------*/
/* typedef (enum) */

/*------------------------*/
/* typedef (struct/union) */

// Region parameters
typedef struct _PrvLoRaRegionParams_t
{
    uint8_t     modem;
    uint8_t     dataRate;
    uint32_t    bandWidth;
    uint32_t    centerFreq0;
    uint32_t    chSpacing;
    uint8_t     numChs;
} PrvLoRaRegionParams_t;

typedef struct _PrvLoRaRegionManage_t
{
    PrvLoRaRegion_t                 region;
    const PrvLoRaRegionParams_t     *p_regionParamsTbl;
    uint8_t                         maxNumDr;
    PrvLoRaRadioCfg_t               radioCfgRgn;
} PrvLoRaRegionManage_t;

/*-------------------------*/
/* global variable (const) */

// for AS1,2,3,4,JP
#if defined(RADIO_CFG_AS_ENABLED)
const PrvLoRaRegionParams_t PrvLoRaRgnParam_as1[ PRVLORA_REGION_AS_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF12, 125000,    915200000,   200000,    64 },
    /* DR_1 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF11, 125000,    915200000,   200000,    64 },
    /* DR_2 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF10, 125000,    915200000,   200000,    64 },
    /* DR_3 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF9,  125000,    915200000,   200000,    64 },
    /* DR_4 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  125000,    915200000,   200000,    64 },
    /* DR_5 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  125000,    915200000,   200000,    64 },
    /* DR_6 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  250000,    915300000,   400000,    32 },
    /* DR_7 */  { PRVLORA_MODEM_FSK,  PRVLORA_DATARATE_50K,   50000,    915200000,   200000,    64 },
};

const PrvLoRaRegionParams_t PrvLoRaRgnParam_as2[ PRVLORA_REGION_AS_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF12, 125000,    920200000,   200000,    14 },
    /* DR_1 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF11, 125000,    920200000,   200000,    14 },
    /* DR_2 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF10, 125000,    920200000,   200000,    14 },
    /* DR_3 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF9,  125000,    920200000,   200000,    14 },
    /* DR_4 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  125000,    920200000,   200000,    14 },
    /* DR_5 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  125000,    920200000,   200000,    14 },
    /* DR_6 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  250000,    920500000,   400000,     6 },
    /* DR_7 */  { PRVLORA_MODEM_FSK,  PRVLORA_DATARATE_50K,   50000,    920200000,   200000,    14 },
};

const PrvLoRaRegionParams_t PrvLoRaRgnParam_as3[ PRVLORA_REGION_AS_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF12, 125000,    915200000,   200000,    29 },
    /* DR_1 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF11, 125000,    915200000,   200000,    29 },
    /* DR_2 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF10, 125000,    915200000,   200000,    29 },
    /* DR_3 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF9,  125000,    915200000,   200000,    29 },
    /* DR_4 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  125000,    915200000,   200000,    29 },
    /* DR_5 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  125000,    915200000,   200000,    29 },
    /* DR_6 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  250000,    915300000,   400000,    14 },
    /* DR_7 */  { PRVLORA_MODEM_FSK,  PRVLORA_DATARATE_50K,   50000,    915200000,   200000,    29 },
};

const PrvLoRaRegionParams_t PrvLoRaRgnParam_as4[ PRVLORA_REGION_AS_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF12, 125000,    917100000,   200000,    15 },
    /* DR_1 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF11, 125000,    917100000,   200000,    15 },
    /* DR_2 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF10, 125000,    917100000,   200000,    15 },
    /* DR_3 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF9,  125000,    917100000,   200000,    15 },
    /* DR_4 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  125000,    917100000,   200000,    15 },
    /* DR_5 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  125000,    917100000,   200000,    15 },
    /* DR_6 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  250000,    917300000,   400000,     7 },
    /* DR_7 */  { PRVLORA_MODEM_FSK,  PRVLORA_DATARATE_50K,   50000,    917100000,   200000,    15 },
};

const PrvLoRaRegionParams_t PrvLoRaRgnParam_jp[ PRVLORA_REGION_JP_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF12, 125000,    920600000,   200000,    15 },
    /* DR_1 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF11, 125000,    920600000,   200000,    15 },
    /* DR_2 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF10, 125000,    920600000,   200000,    15 },
    /* DR_3 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF9,  125000,    920600000,   200000,    15 },
    /* DR_4 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  125000,    920600000,   200000,    38 },
    /* DR_5 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  125000,    920600000,   200000,    38 },
    /* DR_6 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  250000,    920700000,   400000,     4 },
    /* DR_7 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  250000,    922700000,   400000,    14 },
    /* DR_8 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  250000,    920900000,   400000,    18 },
    /* DR_9 */  { PRVLORA_MODEM_FSK,  PRVLORA_DATARATE_50K,   50000,    920600000,   200000,    38 },
};

const PrvLoRaRegionParams_t PrvLoRaRgnParam_jp_ldc[ PRVLORA_REGION_JP_LDC_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF12, 125000,    920600000,   200000,    15 },
    /* DR_1 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF11, 125000,    920600000,   200000,    15 },
    /* DR_2 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF10, 125000,    920600000,   200000,    15 },
    /* DR_3 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF9,  125000,    920600000,   200000,    15 },
    /* DR_4 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  125000,    920600000,   200000,    15 },
    /* DR_5 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  125000,    920600000,   200000,    15 },
    /* DR_6 */  { PRVLORA_MODEM_FSK,  PRVLORA_DATARATE_50K,   50000,    920600000,   200000,    15 },
};
#endif

// for EU
#if defined(RADIO_CFG_EU_ENABLED)
const PrvLoRaRegionParams_t PrvLoRaRgnParam_eu[ PRVLORA_REGION_EU_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF12, 125000,    863100000,   200000,    28 },
    /* DR_1 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF11, 125000,    863100000,   200000,    28 },
    /* DR_2 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF10, 125000,    863100000,   200000,    28 },
    /* DR_3 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF9,  125000,    863100000,   200000,    28 },
    /* DR_4 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  125000,    863100000,   200000,    28 },
    /* DR_5 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  125000,    863100000,   200000,    28 },
    /* DR_6 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  250000,    863200000,   400000,    12 },
    /* DR_7 */  { PRVLORA_MODEM_FSK,  PRVLORA_DATARATE_50K,   50000,    863100000,   200000,    28 },
};
#endif

// for US
#if defined(RADIO_CFG_US_ENABLED)
const PrvLoRaRegionParams_t PrvLoRaRgnParam_us[ PRVLORA_REGION_US_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  500000,    902900000,   600000,    42 },
};
#endif

// for AU
#if defined(RADIO_CFG_AU_ENABLED)
const PrvLoRaRegionParams_t PrvLoRaRgnParam_au[ PRVLORA_REGION_AU_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  500000,    915500000,   600000,    21 },
};
#endif

// for IN
#if defined(RADIO_CFG_IN_ENABLED)
const PrvLoRaRegionParams_t PrvLoRaRgnParam_in[ PRVLORA_REGION_IN_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF12, 125000,    865100000,   200000,    15 },
    /* DR_1 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF11, 125000,    865100000,   200000,    15 },
    /* DR_2 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF10, 125000,    865100000,   200000,    15 },
    /* DR_3 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF9,  125000,    865100000,   200000,    15 },
    /* DR_4 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  125000,    865100000,   200000,    15 },
    /* DR_5 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  125000,    865100000,   200000,    15 },
    /* DR_6 */  { PRVLORA_MODEM_FSK,  PRVLORA_DATARATE_50K,   50000,    865100000,   200000,    15 },
};
#endif

// for KR
#if defined(RADIO_CFG_KR_ENABLED)
const PrvLoRaRegionParams_t PrvLoRaRgnParam_kr[ PRVLORA_REGION_KR_MAXNUM_DR ] =
{
               /* modem,              dataRate,              bandWidth, centerFreq0, chSpacing, numChs */
    /* DR_0 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF12, 125000,    920900000,   200000,    13 },
    /* DR_1 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF11, 125000,    920900000,   200000,    13 },
    /* DR_2 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF10, 125000,    920900000,   200000,    13 },
    /* DR_3 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF9,  125000,    920900000,   200000,    13 },
    /* DR_4 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF8,  125000,    920900000,   200000,    13 },
    /* DR_5 */  { PRVLORA_MODEM_LORA, PRVLORA_DATARATE_SF7,  125000,    920900000,   200000,    13 },
};
#endif

/*-----------------*/
/* global variable */
PrvLoRaRegionManage_t PrvLoRaRegionMng = { .region = PRVLORA_REGION_NOTDEFINED };

/*--------------------*/
/* function prototype */
static PrvLoRaStatus_t PrivateLoRaRegionGetParamsTbl( uint8_t                     drIndex,
                                                      const PrvLoRaRegionParams_t **pp_regionParam );


//--------------------------------------------------------------------------------------------------
//

PrvLoRaStatus_t PrivateLoRaRegionInit( PrvLoRaRegion_t              region,
                                       PrvLoRaRegionDefaultParams_t *p_defaultParams )
{
    PrvLoRaStatus_t         ret;

    // initial check
    if( p_defaultParams == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // init
    ret = PRVLORA_STATUS_OK;

    switch( region )
    {
#if defined(RADIO_CFG_AS_ENABLED)
        case PRVLORA_REGION_AS1:
        case PRVLORA_REGION_AS2:
        case PRVLORA_REGION_AS3:
        case PRVLORA_REGION_AS4:
            PrvLoRaRegionMng.region   = region;
            PrvLoRaRegionMng.maxNumDr = PRVLORA_REGION_AS_MAXNUM_DR;
            if( region == PRVLORA_REGION_AS1 )
            {
                PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_as1;
                PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_AS1;
            }
            else if( region == PRVLORA_REGION_AS2 )
            {
                PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_as2;
                PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_AS2;
            }
            else if( region == PRVLORA_REGION_AS3 )
            {
                PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_as3;
                PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_AS3;
            }
            else // if( region == PRVLORA_REGION_AS4 )
            {
                PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_as4;
                PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_AS4;
            }

            p_defaultParams->drIndex           = PRVLORA_REGION_AS_DEFAULT_DRINDEX;
            p_defaultParams->channelId         = PRVLORA_REGION_AS_DEFAULT_CHID;
            p_defaultParams->txPower           = PRVLORA_REGION_AS_DEFAULT_TXPOWER;
            break;

        case PRVLORA_REGION_JP:
            PrvLoRaRegionMng.region            = region;
            PrvLoRaRegionMng.maxNumDr          = PRVLORA_REGION_JP_MAXNUM_DR;
            PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_jp;
            PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_JP;

            p_defaultParams->drIndex           = PRVLORA_REGION_JP_DEFAULT_DRINDEX;
            p_defaultParams->channelId         = PRVLORA_REGION_JP_DEFAULT_CHID;
            p_defaultParams->txPower           = PRVLORA_REGION_JP_DEFAULT_TXPOWER;
            break;

        case PRVLORA_REGION_JP_LDC:
            PrvLoRaRegionMng.region            = region;
            PrvLoRaRegionMng.maxNumDr          = PRVLORA_REGION_JP_LDC_MAXNUM_DR;
            PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_jp_ldc;
            PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_JP_LDC;

            p_defaultParams->drIndex           = PRVLORA_REGION_JP_LDC_DEFAULT_DRINDEX;
            p_defaultParams->channelId         = PRVLORA_REGION_JP_LDC_DEFAULT_CHID;
            p_defaultParams->txPower           = PRVLORA_REGION_JP_LDC_DEFAULT_TXPOWER;
            break;
#endif
#if defined(RADIO_CFG_EU_ENABLED)
        case PRVLORA_REGION_EU:
            PrvLoRaRegionMng.region            = region;
            PrvLoRaRegionMng.maxNumDr          = PRVLORA_REGION_EU_MAXNUM_DR;
            PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_eu;
            PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_EU;

            p_defaultParams->drIndex           = PRVLORA_REGION_EU_DEFAULT_DRINDEX;
            p_defaultParams->channelId         = PRVLORA_REGION_EU_DEFAULT_CHID;
            p_defaultParams->txPower           = PRVLORA_REGION_EU_DEFAULT_TXPOWER;
            break;
#endif
#if defined(RADIO_CFG_US_ENABLED)
        case PRVLORA_REGION_US:
            PrvLoRaRegionMng.region            = region;
            PrvLoRaRegionMng.maxNumDr          = PRVLORA_REGION_US_MAXNUM_DR;
            PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_us;
            PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_US;

            p_defaultParams->drIndex           = PRVLORA_REGION_US_DEFAULT_DRINDEX;
            p_defaultParams->channelId         = PRVLORA_REGION_US_DEFAULT_CHID;
            p_defaultParams->txPower           = PRVLORA_REGION_US_DEFAULT_TXPOWER;
            break;
#endif
#if defined(RADIO_CFG_AU_ENABLED)
        case PRVLORA_REGION_AU:
            PrvLoRaRegionMng.region            = region;
            PrvLoRaRegionMng.maxNumDr          = PRVLORA_REGION_AU_MAXNUM_DR;
            PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_au;
            PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_AU;

            p_defaultParams->drIndex           = PRVLORA_REGION_AU_DEFAULT_DRINDEX;
            p_defaultParams->channelId         = PRVLORA_REGION_AU_DEFAULT_CHID;
            p_defaultParams->txPower           = PRVLORA_REGION_AU_DEFAULT_TXPOWER;
            break;
#endif
#if defined(RADIO_CFG_IN_ENABLED)
        case PRVLORA_REGION_IN:
            PrvLoRaRegionMng.region            = region;
            PrvLoRaRegionMng.maxNumDr          = PRVLORA_REGION_IN_MAXNUM_DR;
            PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_in;
            PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_IN;

            p_defaultParams->drIndex           = PRVLORA_REGION_IN_DEFAULT_DRINDEX;
            p_defaultParams->channelId         = PRVLORA_REGION_IN_DEFAULT_CHID;
            p_defaultParams->txPower           = PRVLORA_REGION_IN_DEFAULT_TXPOWER;
            break;
#endif
#if defined(RADIO_CFG_KR_ENABLED)
        case PRVLORA_REGION_KR:
            PrvLoRaRegionMng.region            = region;
            PrvLoRaRegionMng.maxNumDr          = PRVLORA_REGION_KR_MAXNUM_DR;
            PrvLoRaRegionMng.p_regionParamsTbl = PrvLoRaRgnParam_kr;
            PrvLoRaRegionMng.radioCfgRgn       = PRVLORA_RADIO_CFG_KR;

            p_defaultParams->drIndex           = PRVLORA_REGION_KR_DEFAULT_DRINDEX;
            p_defaultParams->channelId         = PRVLORA_REGION_KR_DEFAULT_CHID;
            p_defaultParams->txPower           = PRVLORA_REGION_KR_DEFAULT_TXPOWER;
            break;
#endif
        default:
            ret = PRVLORA_STATUS_NOT_SUPPORTED;
            break;
    }

    return ret;
}

PrvLoRaStatus_t PrivateLoRaRegionSetRadioCfg( bool isRadioCfgChkEnabled )
{
    PrvLoRaRadioIbReq_t     radioIbSet;

    // initial check
    if( PrvLoRaRegionMng.region == PRVLORA_REGION_NOTDEFINED )
    {
        return PRVLORA_STATUS_ERROR;
    }

    radioIbSet.radioCfg = PrvLoRaRegionMng.radioCfgRgn;
    PrivateLoRaRadioSetRequest( PRVLORA_RADIO_IB_CFG_REGION, &radioIbSet );

    radioIbSet.radioCfgCheckEnable = isRadioCfgChkEnabled;
    PrivateLoRaRadioSetRequest( PRVLORA_RADIO_IB_CFG_CHECK_ENABLE, &radioIbSet );

    radioIbSet.radioCfgFreqHoppingUsed = false;
    PrivateLoRaRadioSetRequest( PRVLORA_RADIO_IB_CFG_FREQ_HOPPING_USED, &radioIbSet );

    return PRVLORA_STATUS_OK;
}

//--------------------------------------------------------------------------------------------------
//

static PrvLoRaStatus_t PrivateLoRaRegionGetParamsTbl( uint8_t                     drIndex,
                                                      const PrvLoRaRegionParams_t **pp_regionParam )
{
    PrvLoRaStatus_t             ret;
    const PrvLoRaRegionParams_t *p_regionParam;

    // initial check
    if( PrvLoRaRegionMng.region == PRVLORA_REGION_NOTDEFINED )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // init
    ret = PRVLORA_STATUS_PARAMETER_INVALID;

    if( drIndex < PrvLoRaRegionMng.maxNumDr )
    {
        p_regionParam = &( PrvLoRaRegionMng.p_regionParamsTbl[ drIndex ] );

        if( p_regionParam->modem != PRVLORA_MODEM_NONE )
        {
            (*pp_regionParam) = p_regionParam;
            ret = PRVLORA_STATUS_OK;
        }
    }

    return ret;
}

PrvLoRaStatus_t PrivateLoRaRegionGetDataRate( uint8_t  drIndex,
                                              uint8_t  *p_modem,
                                              uint8_t  *p_dataRate,
                                              uint32_t *p_bandWidth )
{
    PrvLoRaStatus_t             ret;
    const PrvLoRaRegionParams_t *p_regionParam;

    // initial check
    if( ( p_dataRate == NULL ) || ( p_bandWidth == NULL ) || ( p_modem == NULL ) )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // get region parameter
    ret = PrivateLoRaRegionGetParamsTbl( drIndex, &p_regionParam );
    if( ret == PRVLORA_STATUS_OK )
    {
        (*p_modem)     = p_regionParam->modem;
        (*p_dataRate)  = p_regionParam->dataRate;
        (*p_bandWidth) = p_regionParam->bandWidth;
    }

    return ret;
}

PrvLoRaStatus_t PrivateLoRaRegionGetFrequency( uint8_t ch, uint8_t drIndex, uint32_t *p_frequency )
{
    PrvLoRaStatus_t             ret;
    const PrvLoRaRegionParams_t *p_regionParam;
    uint32_t                    calcFrequency;

    // initial check
    if( PrvLoRaRegionMng.region == PRVLORA_REGION_NOTDEFINED )
    {
        return PRVLORA_STATUS_ERROR;
    }
    if( p_frequency == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // get region parameter
    ret = PrivateLoRaRegionGetParamsTbl( drIndex, &p_regionParam );
    if( ret == PRVLORA_STATUS_OK )
    {
        if( ch < p_regionParam->numChs )
        {
            calcFrequency = p_regionParam->centerFreq0;
            calcFrequency = calcFrequency + ( p_regionParam->chSpacing * ch );

            (*p_frequency) = calcFrequency;
        }
        else
        {
            ret = PRVLORA_STATUS_PARAMETER_INVALID;
        }
    }

    return ret;
}

PrvLoRaStatus_t PrivateLoRaRegionGetMaxFrameSize( uint8_t   drIndex, 
                                                  uint8_t  *p_maxFrameSize, 
                                                  uint32_t *p_txTimeout )
{
    // drIndex is currently RFD.
    FSP_PARAMETER_NOT_USED( drIndex );

    // initial check
    if( PrvLoRaRegionMng.region == PRVLORA_REGION_NOTDEFINED )
    {
        return PRVLORA_STATUS_ERROR;
    }
    if( p_maxFrameSize == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // get region parameter
    (*p_maxFrameSize) = PRVLORA_REGION_MAX_FRAMESIZE;
    if( p_txTimeout != NULL )
    {
        (*p_txTimeout) = PRVLORA_REGION_TX_TIMEOUT_MS;
    }

    return PRVLORA_STATUS_OK;
}

PrvLoRaStatus_t PrivateLoRaRegionGetMaxRxWindow( uint32_t *p_maxRxWindow )
{
    // initial check
    if( PrvLoRaRegionMng.region == PRVLORA_REGION_NOTDEFINED )
    {
        return PRVLORA_STATUS_ERROR;
    }
    if( p_maxRxWindow == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }

    (*p_maxRxWindow) = PRVLORA_RADIO_RXWINDOW;
    return PRVLORA_STATUS_OK;
}

PrvLoRaStatus_t PrivateLoRaRegionGetRxWindowParams( uint8_t  drIndex,
                                                    uint8_t  minRxSymbols,
                                                    uint32_t rxErrorMs,
                                                    uint32_t *p_windowTimeout,
                                                    int32_t  *p_windowOffset )
{
    PrvLoRaStatus_t             ret;
    const PrvLoRaRegionParams_t *p_regionParam;
    uint32_t                    tSymbolInUs;
    uint32_t                    windowTimeout;
    int32_t                     windowOffset;
    uint32_t                    wakeupTimeMs;

    // initial check
    if( ( p_windowTimeout == NULL ) || ( p_windowTimeout == NULL ) )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // get region parameter
    ret = PrivateLoRaRegionGetParamsTbl( drIndex, &p_regionParam );

    // calculate symbol time
    if( ret == PRVLORA_STATUS_OK )
    {
        switch( p_regionParam->modem )
        {
            case PRVLORA_MODEM_LORA:
                tSymbolInUs = (uint32_t)( ( 1UL << (p_regionParam->dataRate) ) * 1000000UL );  // (numerator)
                tSymbolInUs = (uint32_t)( tSymbolInUs / p_regionParam->bandWidth );
                break;

            case PRVLORA_MODEM_FSK :
                tSymbolInUs = (uint32_t)( 8000UL / ( uint32_t )p_regionParam->dataRate );
                break;

            //case PRVLORA_MODEM_NONE:
            default:
                ret = PRVLORA_STATUS_PARAMETER_INVALID;
                break;
        }
    }

    // calculate window timeout and offset
    if( ret == PRVLORA_STATUS_OK )
    {
        // window timeout (symbol)
        windowTimeout = ( 2L * minRxSymbols - 8L ) * tSymbolInUs + 2L * ( rxErrorMs * 1000L );  // (numerator)
        windowTimeout = DIV_CEIL( windowTimeout, tSymbolInUs );
        (*p_windowTimeout) = MAX( windowTimeout, minRxSymbols );

        // window offset (ms)
        wakeupTimeMs = 0;  // fixed
        windowOffset = (int32_t)( 4L * tSymbolInUs ) -
                       (int32_t)DIV_CEIL( ( (*p_windowTimeout) * tSymbolInUs ), 2L ) -
                       (int32_t)( wakeupTimeMs * 1000L );  // (numerator)
        (*p_windowOffset) = (int32_t)DIV_FLOOR( windowOffset, 1000L );
    }

    return ret;
}

