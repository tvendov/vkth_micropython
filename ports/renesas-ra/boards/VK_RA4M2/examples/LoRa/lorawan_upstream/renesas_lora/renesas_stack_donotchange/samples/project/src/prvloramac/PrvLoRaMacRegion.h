/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacRegion.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifndef __PRVLORAMACREGION_H__
#define __PRVLORAMACREGION_H__

#include "PrivateLoRa.h"

/*----------------*/
/* typedef (enum) */

/*------------------------*/
/* typedef (struct/union) */

// default region parameters
typedef struct _PrvLoRaRegionDefaultParams_t
{
    uint8_t     drIndex;
    uint8_t     channelId;
    int8_t      txPower;
} PrvLoRaRegionDefaultParams_t;

/*-----------*/
/* Functions */

extern PrvLoRaStatus_t PrivateLoRaRegionInit( PrvLoRaRegion_t              region, 
                                              PrvLoRaRegionDefaultParams_t *p_defaultParams );
extern PrvLoRaStatus_t PrivateLoRaRegionSetRadioCfg( bool isRadioCfgChkEnabled );

extern PrvLoRaStatus_t PrivateLoRaRegionGetDataRate( uint8_t  drIndex, 
                                                     uint8_t  *p_modem, 
                                                     uint8_t  *p_dataRate, 
                                                     uint32_t *p_bandWidth );
extern PrvLoRaStatus_t PrivateLoRaRegionGetFrequency( uint8_t ch, uint8_t drIndex, uint32_t *p_frequency );
extern PrvLoRaStatus_t PrivateLoRaRegionGetMaxFrameSize( uint8_t   drIndex, 
                                                         uint8_t  *p_maxFrameSize, 
                                                         uint32_t *p_txTimeout );

extern PrvLoRaStatus_t PrivateLoRaRegionGetMaxRxWindow( uint32_t *p_maxRxWindow );
extern PrvLoRaStatus_t PrivateLoRaRegionGetRxWindowParams( uint8_t  drIndex,
                                                           uint8_t  minRxSymbols, 
                                                           uint32_t rxErrorMs, 
                                                           uint32_t *p_windowTimeout, 
                                                           int32_t  *p_windowOffset );

#endif  // __PRVLORAMACREGION_H__
