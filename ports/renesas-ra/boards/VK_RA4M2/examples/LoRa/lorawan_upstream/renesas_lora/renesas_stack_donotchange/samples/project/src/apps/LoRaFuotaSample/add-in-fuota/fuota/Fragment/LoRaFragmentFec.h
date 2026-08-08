/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2017 Semtech

Description: 	Firmware update over the air with LoRa proof of concept
				Functions for the decoding
*/
/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.

    Changes:
       - Changed from C++ based source code to C source code.
       - Changed names such as functions, variable, macros and file names.
       - Changed data type of variables to reduce memory.
*/

#ifndef __LORAFRAGMENTFEC_H__
#define __LORAFRAGMENTFEC_H__

#ifdef FUOTA_ENABLED
    #ifdef DEBUG_FUOTA
    #define DEBUG_FRGMNT
    #endif
#else
    #define DEBUG_FRGMNT
#endif

#include "LoRaFragmentConfig.h"


/*----------*/

// status
#define FRGMNT_FEC_STATUS_ONGOING    (uint16_t)0xFFFF

/*----------*/

// functions
extern void LoRaFragmentFecInit( void );
extern FrgmntStatus_t LoRaFragmentFecSetup( uint8_t fragIndex, uint16_t nbFrag, uint8_t fragSize );
extern FrgmntStatus_t LoRaFragmentFecMissedUncoded( uint8_t fragIndex, uint16_t missedNth );
extern uint16_t LoRaFragmentFecProcessRedundancy( uint8_t  fragIndex, 
                                                  uint16_t fragNth, 
                                                  uint8_t  *p_redundancyFrame,
                                                  uint8_t  *p_dataBlockBuffer );

#endif  /* __LORAFRAGMENTFEC_H__ */
