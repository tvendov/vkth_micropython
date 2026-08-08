/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacRadio.h
  * @author  Renesas Electronics Corporation
  * @brief
**/

#ifndef __PRVLORAMACRADIO_H__
#define __PRVLORAMACRADIO_H__

#include "PrivateLoRa.h"
#include "radio.h"

/*--------*/
/* define */

// Modem
#define PRVLORA_MODEM_NONE                  0
#define PRVLORA_MODEM_LORA                  1
#define PRVLORA_MODEM_FSK                   2

// Data rate
#define PRVLORA_DATARATE_SF12               12
#define PRVLORA_DATARATE_SF11               11
#define PRVLORA_DATARATE_SF10               10
#define PRVLORA_DATARATE_SF9                9
#define PRVLORA_DATARATE_SF8                8
#define PRVLORA_DATARATE_SF7                7
#define PRVLORA_DATARATE_50K                50  // FSK:50kbps

// Bandwidth
#define PRVLORA_BANDWIDTH_LORA125KHZ        0
#define PRVLORA_BANDWIDTH_LORA250KHZ        1
#define PRVLORA_BANDWIDTH_LORA500KHZ        2
#define PRVLORA_BANDWIDTH_FSK               50000

// RxWindow
#define PRVLORA_RADIO_RXWINDOW              3000

// Rx mode (indirect or continuous)
#define PRVLORA_RXMODE_INDIRECT             false
#define PRVLORA_RXMODE_CONTINUOUS           true

/*----------------*/
/* typedef (enum) */

// IB for radio
typedef enum _PrvLoRaRadioIb_t
{
    PRVLORA_RADIO_IB_CFG_REGION = 0,        /* PIB_RADIO_CFG_REGION */
    PRVLORA_RADIO_IB_CFG_CHECK_ENABLE,      /* PIB_RADIO_CFG_CHECK_ENABLE */
    PRVLORA_RADIO_IB_CFG_FREQ_HOPPING_USED, /* PIB_RADIO_CFG_FREQ_HOPPING_USED */
} PrvLoRaRadioIb_t;

// Radio config (-> r_radio_region_api.h)
typedef RadioConfigRegion_t     PrvLoRaRadioCfg_t;
    #define PRVLORA_RADIO_CFG_EU        RADIO_CFG_EU
    #define PRVLORA_RADIO_CFG_IN        RADIO_CFG_IN
    #define PRVLORA_RADIO_CFG_AS1       RADIO_CFG_AS1
    #define PRVLORA_RADIO_CFG_AS2       RADIO_CFG_AS2
    #define PRVLORA_RADIO_CFG_AS3       RADIO_CFG_AS3
    #define PRVLORA_RADIO_CFG_AS4       RADIO_CFG_AS4
    #define PRVLORA_RADIO_CFG_US        RADIO_CFG_US
    #define PRVLORA_RADIO_CFG_AU        RADIO_CFG_AU
    #define PRVLORA_RADIO_CFG_KR        RADIO_CFG_KR
    #define PRVLORA_RADIO_CFG_JP        RADIO_CFG_JP
    #define PRVLORA_RADIO_CFG_JP_LDC    RADIO_CFG_JP_LDC

/*------------------------*/
/* typedef (struct/union) */

// IB for radio
typedef union _PrvLoRaRadioIbReq_t
{
    PrvLoRaRadioCfg_t   radioCfg;
    bool                radioCfgCheckEnable;
    bool                radioCfgFreqHoppingUsed;
} PrvLoRaRadioIbReq_t;

// Tx parameters
typedef struct _PrvLoRaRadioTxParams_t
{
    uint8_t     modem;
    uint32_t    frequency;
    uint8_t     dataRate;
    uint32_t    bandWidth;
    int8_t      txPower;
    uint8_t     maxFrameSize;
    uint32_t    txTimeout;
} PrvLoRaRadioTxParams_t;

// Rx parameters
typedef struct _PrvLoRaRadioRxParams_t
{
    uint8_t     modem;
    uint32_t    frequency;
    uint8_t     dataRate;
    uint32_t    bandWidth;
    uint32_t    maxRxWindow;
    uint8_t     minRxSymbols;
    uint32_t    rxError;
    uint32_t    windowTimeout;
    int32_t     windowOffset;
    uint8_t     maxFrameSize;
} PrvLoRaRadioRxParams_t;

// Radio events status
// (TxDone)
typedef struct _PrvLoRaRadioEventsTxDone_t
{
    TimerTime_t     txDoneTime;
} PrvLoRaRadioEventsTxDone_t;

// (RxDone)
typedef struct _PrvLoRaRadioEventsRxDone_t
{
    TimerTime_t     rxDoneTime;
    uint8_t         *p_payload;
    uint16_t        size;
    int16_t         rssi;
    int8_t          snr;
} PrvLoRaRadioEventsRxDone_t;

typedef struct _PrvLoRaRadioEvents_t
{
    union
    {
        uint8_t     evtValue;
        struct
        {
            uint8_t RxTimeout        : 1;
            uint8_t RxError          : 1;
            uint8_t TxTimeout        : 1;
            uint8_t RxDone           : 1;
            uint8_t TxDone           : 1;
        } events;
    } radioEvent;

    union
    {
        PrvLoRaRadioEventsTxDone_t  txDoneInfo;
        PrvLoRaRadioEventsRxDone_t  rxDoneInfo;
    } eventInfo;
} PrvLoRaRadioEvents_t;


/*-----------*/
/* Functions */

// init
extern PrvLoRaStatus_t PrivateLoRaRadioInit( void );
extern PrvLoRaStatus_t PrivateLoRaRadioSetLoRaMode( void );

// PIB
extern PrvLoRaStatus_t PrivateLoRaRadioGetRequest( PrvLoRaRadioIb_t ibId, PrvLoRaRadioIbReq_t *p_ibGet );
extern PrvLoRaStatus_t PrivateLoRaRadioSetRequest( PrvLoRaRadioIb_t ibId, PrvLoRaRadioIbReq_t *p_ibSet );

// Tx
extern PrvLoRaStatus_t PrivateLoRaRadioSendTx( PrvLoRaRadioTxParams_t *p_txParam,
                                               uint8_t                *p_txPacket,
                                               uint8_t                txPktSize );

// Rx
extern PrvLoRaStatus_t PrivateLoRaRadioStartRx( PrvLoRaRadioRxParams_t *p_rxParam, bool isContinuous );

// Radio event
extern void PrivateLoRaRadioIrqProcess( void );
extern void PrivateLoRaRadioGetEvents( PrvLoRaRadioEvents_t *p_radioEvt );
extern bool PrivateLoRaRadioIsReadyProcess( void );

// Sleep / Wakeup
extern void PrivateLoRaRadioSleepCold( void );
extern void PrivateLoRaRadioSleepWarm( void );
extern void PrivateLoRaRadioWakeup( void );


#endif  // __PRVLORAMACRADIO_H__
