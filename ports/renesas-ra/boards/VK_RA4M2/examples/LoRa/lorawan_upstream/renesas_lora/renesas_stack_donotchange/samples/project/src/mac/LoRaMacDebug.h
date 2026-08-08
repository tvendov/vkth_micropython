/*
    (C) 2019 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __LORAMACDEBUG_H__
#define __LORAMACDEBUG_H__

#if defined(DEBUG_LORAMAC)

#define LORAMAC_DEBUG_MODE_OUTPUT_SNIFFER_FORMAT    (0x00000001UL)
#define LORAMAC_DEBUG_MODE_RX                       (0x00000002UL)
#define LORAMAC_DEBUG_MODE_TX                       (0x00000004UL)
#define LORAMAC_DEBUG_MODE_CCA                      (0x00000008UL)
#define LORAMAC_DEBUG_MODE_PING_UNICAST             (0x00000010UL)
#define LORAMAC_DEBUG_MODE_PING_MULTICAST           (0x00000020UL)
#define LORAMAC_DEBUG_MODE_LOWPWR_RF                (0x00000040UL)
#define LORAMAC_DEBUG_MODE_PSEUDO_MCULOWPWR         (0x00000100UL)
#define LORAMAC_DEBUG_MODE_BEACON                   (0x00000200UL)

typedef struct sLoRaMacDebug
{
    /*!
     * Debug mode
     */
    uint32_t Mode;  
    /*!
     * Rx configuration
     */
    struct {
        uint8_t Bandwidth;
        uint32_t Datarate;
        uint16_t SymbTimeout;
        uint32_t Frequency;
        uint8_t PacketType;
    } RxConfig;
    /*!
     * Tx configuration
     */
    struct {
        uint8_t Bandwidth;
        uint32_t Datarate;
        uint32_t Timeout;
        int8_t   Power;
        uint32_t Frequency;
        uint8_t PacketType;
    } TxConfig;
    /*!
     * Tx data
     */
    struct {
        /*!
         * Payload of last Tx 
         */
        uint8_t *pPayload;
        /*!
         * Payload size of last Tx
         */
        uint8_t PayloadLen;
    } Tx;
    /*!
     * Check RFIC low power
     */
    struct {
        uint8_t RfPwrMode;  // 0=Standby, 1=Wakeup, 2=SleepWarm, 3=SleepCold
    } LowPwrRf;
} LoRaMacDebug_t;


extern LoRaMacDebug_t LoRaMacDebug;

void LoRaMacDebugInit(void);
void LoRaMacDebugSetMode(uint32_t mode);
uint32_t LoRaMacDebugGetMode(void);

void LoRaMacDebugOnRadioTxDone( TimerTime_t txDoneTime );
void LoRaMacDebugOnRadioRxDone( uint8_t *payload, uint16_t size, 
                        int16_t rssi, int8_t snr, uint8_t rxSlot, TimerTime_t rxDoneTime );

#ifdef LORAMAC_CLASSB_ENABLED
void LoRaMacDebugLoRaMacClassBProcessBeacon( void *vp_beaconCtx,
                                             TimerTime_t beaconEventTime, 
                                             int32_t sysTimeErr,
                                             TimerTime_t currentTime );
void LoRaMacDebugLoRaMacClassBProcessPingSlot(TimerTime_t pingSlotTime);
void LoRaMacDebugLoRaMacClassBProcessMulticastSlot(TimerTime_t multicastSlotTime);
#endif

int16_t LoRaMacDebugSetPseudoLowPower( void );

#endif  // defined(DEBUG_LORAMAC)

#endif  // __LORAMACDEBUG_H_

