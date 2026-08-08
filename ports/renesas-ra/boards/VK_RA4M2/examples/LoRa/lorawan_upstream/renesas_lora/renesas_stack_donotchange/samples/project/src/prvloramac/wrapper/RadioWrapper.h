/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    RadioWrapper.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifndef __RADIOWRAPPER_H__
#define __RADIOWRAPPER_H__

#include "radio.h"

/*----------------*/
/* typedef (enum) */
typedef enum _RadioWrapLoRaMode_t
{
    RADIOWRAP_LORAMODE_LORAWAN = 0,
    RADIOWRAP_LORAMODE_PRIVATELORA,
    /*---*/
    MAXNUM_RADIOWRAP_LORAMODE
} RadioWrapLoRaMode_t;

/*-----------*/
/* Functions */
extern RadioResult_t RadioWrapperInit( RadioWrapLoRaMode_t loraMode, RadioEvents_t *p_events );
extern RadioResult_t RadioWrapperSetLoRaMode( RadioWrapLoRaMode_t loraMode );


#endif  // __RADIOWRAPPER_H__
