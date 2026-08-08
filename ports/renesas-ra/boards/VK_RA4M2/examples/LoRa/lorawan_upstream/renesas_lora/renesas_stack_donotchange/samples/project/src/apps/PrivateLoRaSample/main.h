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

/*-------*/
/* macro */
// LoRa mode
#define APP_LORA_MODE_NONE              0xFF
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

// save/load parameters
extern void AppResetParams( void );
extern void AppFactoryResetParams( void );
extern void AppLoadParams( uint8_t mode );
extern void AppSaveParams( void );


#endif /* __MAIN_H__ */
