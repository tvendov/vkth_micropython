/*!
 * \file      LoRaMacClassBConfig.h
 *
 * \brief     LoRa MAC Class B configuration
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
 *              (C)2013 Semtech
 *
 *               ___ _____ _   ___ _  _____ ___  ___  ___ ___
 *              / __|_   _/_\ / __| |/ / __/ _ \| _ \/ __| __|
 *              \__ \ | |/ _ \ (__| ' <| _| (_) |   / (__| _|
 *              |___/ |_/_/ \_\___|_|\_\_| \___/|_|_\\___|___|
 *              embedded.connectivity.solutions===============
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 *
 * \author    Gregory Cristian ( Semtech )
 *
 * \author    Daniel Jaeckle ( STACKFORCE )
 *
 * \defgroup  LORAMACCLASSB LoRa MAC Class B configuration
 *            This header file contains parameters to configure the class b operation.
 *            By default, all parameters are set according to the specification.
 * \{
 */
/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __LORAMACCLASSBCONFIG_H__
#define __LORAMACCLASSBCONFIG_H__

#ifdef __cplusplus
extern "C"
{
#endif

/*!
 * Defines the beacon interval in ms
 */
#define CLASSB_BEACON_INTERVAL                      128000

/*!
 * Beacon reserved time in ms
 */
#define CLASSB_BEACON_RESERVED                      2120

/*!
 * Beacon guard time in ms
 */
#define CLASSB_BEACON_GUARD                         3000

/*!
 * Beacon window time in ms
 */
#define CLASSB_BEACON_WINDOW                        122880

/*!
 * Beacon window time in numer of slots
 */
#define CLASSB_BEACON_WINDOW_SLOTS                  4096

/*!
 * Ping slot length time in ms
 */
#define CLASSB_PING_SLOT_WINDOW                     30

/*!
 * Maximum allowed beacon less time in ms
 */
#define CLASSB_MAX_BEACON_LESS_PERIOD               7200000

/*!
 * Delay time for the BeaconTimingAns in ms
 */
#define CLASSB_BEACON_DELAY_BEACON_TIMING_ANS       30


/*!
 * Maximum symbol timeout for beacons
 */
#define CLASSB_BEACON_SYMBOL_TO_EXPANSION_MAX       255

/*!
 * Maximum symbol timeout for ping slots
 */
#define CLASSB_PING_SLOT_SYMBOL_TO_EXPANSION_MAX    255


/*!
 * Network time (DeviceTimeAns) has worst case accuracy of +/-100msec
 */
#define CLASSB_NETWORKTIME_ACCURACY                 100

/*!
 * Default ping slot periodicity
 */
#define CLASSB_PING_SLOT_PERIODICITY_DEFAULT        7

/*!
 * Threshold for RF sleep
 * RF can sleep deeply when ping slot periodicity is above it.
 */
#define CLASSB_PING_SLOT_PERIODICITY_RFSLEEP_THRESHOLD  4


#ifdef __cplusplus
}
#endif

#endif // __LORAMACCLASSBCONFIG_H__
