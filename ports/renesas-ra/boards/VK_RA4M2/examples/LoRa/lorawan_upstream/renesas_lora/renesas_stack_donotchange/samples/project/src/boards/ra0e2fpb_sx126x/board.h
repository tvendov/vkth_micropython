/*!
 * \file      board.h
 *
 * \brief     Target board general functions implementation
 *
 * \copyright Revised BSD License, see section \ref LICENSE.
 *
 * \code
 *                ______                              _
 *               / _____)             _              | |
 *              ( (____  _____ ____ _| |_ _____  ____| |__
 *               \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 *               _____) ) ____| | | || |_| ____( (___| | | |
 *              (______/|_____)_|_|_| \__)_____)\____)_| |_|
 *              (C)2013-2017 Semtech
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 *
 * \author    Gregory Cristian ( Semtech )
 */

/*
    Copyright (c) 2024 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __BOARD_H__
#define __BOARD_H__

#ifdef __cplusplus
extern "C"
{
#endif

#define RA0E2FPB_SX126x      // FPB-RA0E2(RTK7FPA0E2S00001BJ) + Semtech SX126x board

#if defined(RA0E2FPB_SX126x)
#define R7FA0E209
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "utilities.h"

#include "timer.h"
#include "delay.h"
#include "gpio.h"
#include "uart.h"
#include "uart-board.h"
#include "spi.h"
#include "radio.h"
#include "sx126x.h"
#include "sx126x-board.h"
#include "pinName-board.h"
#include "hal_data.h"
#ifdef RM_TINYCRYPT
#define RM_TINCYRYPT_PORT_Init()
#define HW_SCE_PowerOn()
#define HW_SCE_PowerOff()
#endif
#if defined(RM_HS300X_H)
#include "hs300x.h"
#endif

#if defined(DEBUG_LORAMAC)
#include "LoRaMacDebug.h"
#endif
#if defined(DEBUG_PRVLORA)
#include "privatelora_debug.h"
#endif

#if defined(__ICCARM__)
#include <intrinsics.h>
#endif

/*!
 * Possible power sources
 */
enum BoardPowerSources
{
    USB_POWER = 0,
    BATTERY_POWER,
};

/*!
 * \brief Initializes the mcu.
 */
void BoardInitMcu( void );

/*!
 * \brief Resets the mcu.
 */
void BoardResetMcu( void );

/*!
 * \brief Initializes the boards peripherals.
 */
void BoardInitPeriph( void );

/*!
 * \brief De-initializes the target board peripherals to decrease power
 *        consumption.
 */
void BoardDeInitMcu( void );

/*!
 * \brief Gets the current potentiometer level value
 *
 * \retval value  Potentiometer level ( value in percent )
 */
uint8_t BoardGetPotiLevel( void );

/*!
 * \brief Measure the Battery voltage
 *
 * \retval value  battery voltage in volts
 */
uint32_t BoardGetBatteryVoltage( void );

/*!
 * \brief Get the current battery level
 *
 * \retval value  battery level [  0: USB,
 *                                 1: Min level,
 *                                 x: level
 *                               254: fully charged,
 *                               255: Error]
 */
uint8_t BoardGetBatteryLevel( void );

/*!
 * \brief Get the current MCU temperature in degree celcius * 256
 *
 * \retval temperature * 256
 */
int16_t BoardGetTemperature( void );

/*!
 * Returns a pseudo random seed generated using the MCU Unique ID
 *
 * \retval seed Generated pseudo random seed
 */
uint32_t BoardGetRandomSeed( void );

/*!
 * \brief Gets the board 64 bits unique ID
 *
 * \param [IN] id Pointer to an array that will contain the Unique ID
 */
void BoardGetUniqueId( uint8_t *id );

/*!
 * \brief Manages the entry into ARM cortex deep-sleep mode
 */
void BoardLowPowerHandler( void );

/*!
 * \brief Get the board power source
 *
 * \retval value  power source [0: USB_POWER, 1: BATTERY_POWER]
 */
uint8_t GetBoardPowerSource( void );

/*!
 * \brief Get the board version
 *
 * \retval value  Version
 */
Version_t BoardGetVersion( void );


//------------------------------------------------------------------------
// Additional definitions for Renesas
//------------------------------------------------------------------------

#if !defined(__RA0E2__)
#define __RA0E2__
#endif

#if !defined (SUCCESS)
#define SUCCESS                 (1)
#endif
#if !defined(FAIL)
#define FAIL                    (0)
#endif

/*!
 * Auto Board Configuration Detection
 * If RP_DETECT_BOARD_CONFIG is defined,
 * board configuration regarding XTAL/TCXO and SX1261/SX1262
 * are configured automatically by scanning ports of the board.
 * NOTE:
 * This function will work with SEMTECH LoRa Shield only.
 * For other boards, don't use RP_DETECT_BOARD_CONFIG, and
 * Specify DEFAULT_POWER_SELECT and DEFAULT_CLOCK_SELECT
 */
#define RP_DETECT_BOARD_CONFIG

/*!
 * Board Radio Power Select. Set either RADIO_LOPOWER_SEL or RADIO_HIPOWER_SEL to DEFAULT_POWER_SELECT
 * according to the radio transceiver product employed.
 *      Radio Transceiver  :  DEFAULT_POWER_SELECT value
 *   1) SX1261             :  RADIO_LOPOWER_SEL
 *   2) SX1262             :  RADIO_HIPOWER_SEL
 */
#define RADIO_LOPOWER_SEL           (1)
#define RADIO_HIPOWER_SEL           (2)
#if !defined(DEFAULT_POWER_SELECT)
#define DEFAULT_POWER_SELECT        RADIO_LOPOWER_SEL
#endif

/*!
 * Board Radio Regulator Config. Define RP_USE_DCDC_FOR_RADIO when using DC-DC converter on radio transceiver
 */
#define RP_USE_DCDC_FOR_RADIO

/*!
 * RF (Antenna) switch Config.
 *      Define RP_USE_RF_SWITCH when using a board with RF switch.
 *      Define RP_CONTROL_ANTSW_BY_MCU when RF switch power is controlled by MCU.
 */
#define RP_USE_RF_SWITCH
#define RP_CONTROL_ANTSW_BY_MCU

/*!
 * Radio Clock Source Config. Define RP_USE_TCXO_FOR_RADIO when using TCXO for clock source of radio transceiver.
 */
//#define RP_USE_TCXO_FOR_RADIO

#define RADIO_CLOCK_XTAL_SEL        (0)
#define RADIO_CLOCK_TCXO_SEL        (1)
#if defined(RP_USE_TCXO_FOR_RADIO)
#define DEFAULT_CLOCK_SELECT RADIO_CLOCK_TCXO_SEL
#else
#define DEFAULT_CLOCK_SELECT RADIO_CLOCK_XTAL_SEL
#endif

 /*!
 * DIO3 TCXO Voltage
 */
#define RP_TCXO_CTRL_VOLTAGE        (TCXO_CTRL_1_8V)   // DIO3 output (TCXO power) voltage

 /*!
 * DIO3 TCXO timeout
 */
#define RP_TCXO_STAB_TIME           (640U)   // TCXO stabilization time. Unit: 15.625 us

 /*!
 * Default XTAL XTA/XTB Trim
 */
#define RP_XTAL_XTA_TRIM            (0x13)   // default XTAL XTA Trim (0x00 - 0x2F)
#define RP_XTAL_XTB_TRIM            (0x13)   // default XTAL XTB Trim (0x00 - 0x2F)

/*!
 * Maximum CCA duration
 */
#define RP_CCA_TIMER_MAX_TIME_MS    (50U)   // Maximum CCA duration in ms (millisecond)
#define RP_CCA_TIMER_MAX_COUNT      (1563U) // Maximum CCA durataion in timer count.
                                            // (0.050[sec]) / (1 / 31250 [Hz]) = 1562.5 [clock]

 /*!
 * Additional TimeOnAir offset
 */
#define RP_TOA_OFFSET_LORA          (0)   // LoRa TimeOnAir offset[ms] LoRa [ms]
#define RP_TOA_OFFSET_FSK_7KBPS     (1)   // FSK TimeOnAir offset[ms] @ 7Kbps - 300kbps
#define RP_TOA_OFFSET_FSK_3KBPS     (2)   // FSK TimeOnAir offset[ms] @ 3Kbps - 7kbps
#define RP_TOA_OFFSET_FSK_2KBPS     (3)   // FSK TimeOnAir offset[ms] @ 2Kbps - 3kbps
#define RP_TOA_OFFSET_FSK_1KBPS     (5)   // FSK TimeOnAir offset[ms] @ 1Kbps - 2Kbps
#define RP_TOA_OFFSET_FSK_600BPS    (7)   // FSK TimeOnAir offset[ms] @ 600pbs - 1Kbps

/*!
 * GPIO pin settings should be configured as below by RASC.
 */
#define RADIO_RESET                 (P0_15)
#define RADIO_MOSI                  (P5_1)
#define RADIO_MISO                  (P5_2)
#define RADIO_SCLK                  (P5_0)
#define RADIO_NSS                   (P1_15)
#define RADIO_BUSY                  (P4_9)
#define RADIO_DIO_1                 (P2_1)

#if defined(RP_CONTROL_ANTSW_BY_MCU)
#define ANT_SWITCH_POWER            (P1_6)
#endif

#define RP_RADIO_DEVICE_SEL         (P0_13)
#define RP_RADIO_XTAL_SEL           (P0_12)

/*!
 * Flag definitions to indicate what interrupt source woke MCU up, and corresponding variable definition.
 * Users may add their own definitions to unused bits if needed for post-wake-up process.
 */
extern volatile uint32_t BoardMcuWokeUpBy;

#define WAKEUP_TRIGGER_NONE             (0x00000000UL)
#define WAKEUP_TRIGGER_READY            (0x00000001UL)
#define WAKEUP_TRIGGER_RADIO_DIO1       (0x00000002UL)
#define WAKEUP_TRIGGER_INTRTC           (0x00000004UL)
#define WAKEUP_TRIGGER_INTAGT           (0x00000008UL)
#define WAKEUP_TRIGGER_TIMER_TIMEOUT    (0x00000010UL)

/*!
 * \brief Clock error
 */
#define BOARD_CLOCK_ERROR_PPM       (40)    // ppm
#define BOARD_CLOCK_ERROR_PPM_MAX   (100)   // ppm

/*!
 *  Low power entry condition.
 *  Condition : (1) Applications allow for MCU to go to low power state
 */
#define RP_LOWPWR_COND_ENTRY  ( BoardIsLowPowerAllowed() )

/*!
 *  Low power handler loop condition.
 *  Condition: (1), (2) and (3)
 *         (1) MCU did not wake up by timer timeout
 *         (2) MCU did not wake up by the SX126x-DIO1 interrupt
 *         (3) Applications allow for MCU to go to low power state
 */
#define RP_LOWPWR_COND_LOOP ( \
                                 !(BoardMcuWokeUpBy & WAKEUP_TRIGGER_TIMER_TIMEOUT) \
                              && !(BoardMcuWokeUpBy & WAKEUP_TRIGGER_RADIO_DIO1)    \
                              && BoardIsLowPowerAllowed() \
                            )


/*!
 * Type definition and function prototype for utility functions
 */
#define BoardEnableAllIrq()     __enable_irq(); __DSB(); __ISB()
#define BoardDisableAllIrq()    __disable_irq(); __DSB(); __ISB()

#define print               RpMcuPrint
#define print_hex           RpMcuPrintHex
#define print_dec           RpMcuPrintDec
#define print_newline()     print( "\r\n" )

/*!
 * Function prototype for Low Power Management
 */
fsp_err_t BoardLpmInit(void);
fsp_err_t BoardLpmEnter(void);
fsp_err_t BoardLpmExit(void);

/*!
 * Type definition of callback function whether to enable Low power operation
 */
typedef bool ( BoardIsLowPowerAllowedCallback_t )( void );

/*!
 * \brief Set mcu to low power state.
 *
 * \retval value  Process result ( SUCCESS or FAIL )
 */
int16_t SetLowPower( void );

/*!
 * \brief Preprocess to configure MCU functions after DIO interrupt.
 *
 * \retval value  none
 */
void BoardRadioIrqPreprocess( void );

/*!
 * \brief Gets MCU low power operation status flag.
 */
uint32_t GetLowPowerFlag( void );

/*!
 * \brief Sets MCU low power operation status flag.
 */
void SetLowPowerFlag( uint32_t flag );

/*!
 * \brief Clears all MCU low power operation status flags.
 */
void ClearLowPowerFlag( void );

/*!
 * \brief Ask if applications allow for MCU to go to low power state.
 */
bool BoardIsLowPowerAllowed( void );

/*!
 * \brief Set callback function for BoardIsLowPowerAllowed
 */
void BoardSetIsLowPowerAllowedCallback( BoardIsLowPowerAllowedCallback_t *p );

/*!
 * \brief Disable Irq regarding DIO1
 */
void BoardRadioDio1DisableIrq( void );

/*!
 * \brief Enable Irq regarding DIO1
 */
void BoardRadioDio1EnableIrq( void );

/*!
 * \brief Disable Irq regarding Timer
 */
void BoardTimerDisableIrq( void );

/*!
 * \brief Enable Irq regarding Timer without clearing pending Irq
 */
void BoardTimerEnableIrqNoClear( void );

#ifdef __cplusplus
}
#endif

#endif // __BOARD_H__
