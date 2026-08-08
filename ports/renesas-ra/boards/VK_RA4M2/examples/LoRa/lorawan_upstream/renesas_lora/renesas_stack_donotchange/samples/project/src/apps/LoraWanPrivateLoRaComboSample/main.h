/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    main.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifndef __MAIN_H__
#define __MAIN_H__

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/*-------*/
/* macro */

// (check LoRa mode)
#if !defined(LORACOMBO_ENABLED)
#error "Not specified LORACOMBO_ENABLED"
#endif

// LoRa mode
#define APP_LORA_MODE_NONE              0xFF
#define APP_LORA_MODE_LORAWAN           0
#define APP_LORA_MODE_PRIVATELORA       1


/*----------------*/
/* typedef (enum) */


/*------------------------*/
/* typedef (struct/union) */


/*--------------------------*/
/* global variable (extern) */


/*-----------*/
/* Functions */ 

extern uint8_t AppSetLoRaMode( uint8_t loraMode );
extern uint8_t AppGetLoRaMode( void );


#endif /* __MAIN_H__ */
