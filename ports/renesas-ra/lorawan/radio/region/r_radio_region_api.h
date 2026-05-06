/*
    Copyright (c) 2022-2023 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/     

#ifndef __R_RADIO_REGION_API_H__
#define __R_RADIO_REGION_API_H__

/*!
 * Macro definitions when used with LoRaWAN protocol stack
 */
#if defined(REGION_EU868)
#if !defined(RADIO_CFG_EU_ENABLED)
#define RADIO_CFG_EU_ENABLED
#endif
#endif

#if defined(REGION_IN865)
#if !defined(RADIO_CFG_IN_ENABLED)
#define RADIO_CFG_IN_ENABLED
#endif
#endif

#if defined(REGION_US915)
#if !defined(RADIO_CFG_US_ENABLED)
#define RADIO_CFG_US_ENABLED
#endif
#endif

#if defined(REGION_AU915)
#if !defined(RADIO_CFG_AU_ENABLED)
#define RADIO_CFG_AU_ENABLED
#endif
#endif

#if defined(REGION_KR920)
#if !defined(RADIO_CFG_KR_ENABLED)
#define RADIO_CFG_KR_ENABLED
#endif
#endif

#if defined(RADIO_CFG_AS_ENABLED) || defined(REGION_AS923)
#if !defined(RADIO_CFG_AS1_ENABLED)
#define RADIO_CFG_AS1_ENABLED
#endif
#if !defined(RADIO_CFG_AS2_ENABLED)
#define RADIO_CFG_AS2_ENABLED
#endif
#if !defined(RADIO_CFG_AS3_ENABLED)
#define RADIO_CFG_AS3_ENABLED
#endif
#if !defined(RADIO_CFG_AS4_ENABLED)
#define RADIO_CFG_AS4_ENABLED
#endif
#if !defined(RADIO_CFG_JP_ENABLED)
#define RADIO_CFG_JP_ENABLED
#endif
#if !defined(RADIO_CFG_JP_LDC_ENABLED)
#define RADIO_CFG_JP_LDC_ENABLED
#endif
#endif

/*!
 * Radio configuration for region/country
 */
typedef enum{
    RADIO_CFG_EU     = 0x00,
    RADIO_CFG_IN     = 0x01,
    RADIO_CFG_AS1    = 0x02,
    RADIO_CFG_AS2    = 0x03,
    RADIO_CFG_AS3    = 0x04,
    RADIO_CFG_AS4    = 0x05,
    RADIO_CFG_US     = 0x06,
    RADIO_CFG_AU     = 0x07,
    RADIO_CFG_KR     = 0x08,
    RADIO_CFG_JP     = 0x09,
    RADIO_CFG_JP_LDC = 0x0A,
}RadioConfigRegion_t;

/*!
 * \defgroup   group_tag   RADIO_REGION_API
 * \brief      Radio driver API defined in the radio/region layer.
 */
/*\{*/

/*!
 * \brief    Retrieves estimated transmission backoff time for next transmission.
 * 
 * \return   Estimated time user should wait until next packet transmission in millisecond (ms).
 */
int32_t             RpRegionGetTimeToNextTx( void );

/*\}*/

void RpRegionInitAll( void );

#include "r_radio_region.h"

#endif // __R_RADIO_REGION_API_H__
