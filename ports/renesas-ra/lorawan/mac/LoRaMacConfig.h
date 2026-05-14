/*
    (C) 2017 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __LORAMACCONFIG_H__
#define __LORAMACCONFIG_H__

/*!
 * LoRaWAN version definition.
 */
#ifdef LORAWAN_VERSION_1_0_4
#define LORAMAC_VERSION         (0x01000400)
#endif

#ifdef LORAWAN_VERSION_1_0_3
#define LORAMAC_VERSION         (0x01000300)
#endif

#ifndef LORAMAC_VERSION
#define LORAMAC_VERSION         (0x01000300)
#endif

#if defined(LORAWAN_VERSION_1_0_4) && defined(LORAWAN_VERSION_1_0_3)
#error "Specified both LORAWAN_VERSION_1_0_4 and LORAWAN_VERSION_1_0_3"
#endif

#undef LORAWAN_VERSION_1_0_4
#undef LORAWAN_VERSION_1_0_3
#define LORAWAN_VERSION_1_0_4    (0x01000400)
#define LORAWAN_VERSION_1_0_3    (0x01000300)

/*!
 * Indicates if a random DevNonce must be used or not
 *    1: Use random DevNonce
 *    0: Use counter based DevNonce
 */
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
#define LORAMAC_USE_RANDOM_DEV_NONCE                        0
#elif (LORAMAC_VERSION == LORAWAN_VERSION_1_0_3)
#if !defined(LORAMAC_USE_RANDOM_DEV_NONCE)
#define LORAMAC_USE_RANDOM_DEV_NONCE                        1
#endif
#endif

/*!
 * Indicates if JoinNonce(AppNonce) is counter based and requires to be checked
 *    2: Check JoinNonce whether it is counter based one
 *    1: Check JoinNonce whetehr it is not equal to the previous one
 *    0: Donot check JoinNonce
 */
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
#define LORAMAC_USE_JOIN_NONCE_CHECK                1
#elif (LORAMAC_VERSION == LORAWAN_VERSION_1_0_3)
#if !defined(LORAMAC_USE_JOIN_NONCE_CHECK)
#define LORAMAC_USE_JOIN_NONCE_CHECK                0
#endif
#endif

/*!
 * Default configuration for AS923 CCA operation.
 */
#define LORAMAC_ENABLE_CCA_DEFAULT          true

/*!
 * Check multicast downlink frame counter which is in max/min range.
 */
#define LORAMAC_CHECK_MCFCNT_RANGE          false

/*!
 * (don't edit) Check MCU definition
 */
    #if defined(__RA0E1__) && defined(__RA0E2__)
        #error "Multiple RA0s are defined."
    #endif
    #if (RP_CPU_CLK != 8)
        #error "RP_CPU_CLK should be set to 8."
    #endif

/*!
 * r13 — Join-only RX1 early-open margin (milliseconds).
 *
 * Applied via MAX() against MacParams.SystemMaxRxError at JoinAccept RX1
 * compute (see LoRaMac.c around the RegionComputeRxWindowParameters call
 * gated on NetworkActivation == ACTIVATION_TYPE_NONE). The override is
 * RX1-only; RX2 keeps the configured SystemMaxRxError so the SF12 fallback
 * window does not widen unnecessarily.
 *
 * Empirical basis: SF7 JoinAcceptDelay1=5000 ms scheduling on the bench
 * showed RX1 opening ~92 ms after the gateway JoinAccept preamble ended.
 * 100 ms gives ~90 ms earlier RX1 open at MinRxSymbols=24 and matches the
 * P-C-100 probe that succeeded 5/5 cold OTAA joins.
 *
 * MAX() against MacParams.SystemMaxRxError so a Python-side user override
 * via mac.set_max_rx_error(N>100) still wins.
 */
#define JOIN_RX1_MAX_RX_ERROR_MS            (100u)

/*!
 * Processing time for downlink (from TxDone to start RxON)
 */
#if defined(__RA0E1__)
#define LORAMAC_STACK_PROCTIMEMS_RX1ON      (6)
#define LORAMAC_STACK_PROCTIMEMS_RX2ON      (6)
#elif defined(__RA0E2__)
#define LORAMAC_STACK_PROCTIMEMS_RX1ON      (8)
#define LORAMAC_STACK_PROCTIMEMS_RX2ON      (8)
#else  // (RA2E1, RA2L1)
#define LORAMAC_STACK_PROCTIMEMS_RX1ON      (9)
#define LORAMAC_STACK_PROCTIMEMS_RX2ON      (9)
#endif





/*!
 * Processing time for beacon and ping slot
 */
#ifdef LORAMAC_CLASSB_ENABLED
    #if defined(__RA0E2__)
    #define CLASSB_STACK_PROCTIMEMS_BEACON_ACQISITION       (13)
    #define CLASSB_STACK_PROCTIMEMS_BEACON                  (13)
    #define CLASSB_STACK_PROCTIMEMS_PING_SLOT_SHORT_PERIOD  (9)
    #define CLASSB_STACK_PROCTIMEMS_PING_SLOT_LONG_PERIOD   (13)
    #else  // (RA2E1,RA2L1)
    #define CLASSB_STACK_PROCTIMEMS_BEACON_ACQISITION       (15)
    #define CLASSB_STACK_PROCTIMEMS_BEACON                  (15)
    #define CLASSB_STACK_PROCTIMEMS_PING_SLOT_SHORT_PERIOD  (10)
    #define CLASSB_STACK_PROCTIMEMS_PING_SLOT_LONG_PERIOD   (15)
    #endif




#endif  //LORAMAC_CLASSB_ENABLED

/*!
 * maximum/minimum value of SysTimeErrorTimeMs
 */
#ifdef LORAMAC_CLASSB_ENABLED
#define CLASSB_BEACON_SYSTIMEDELTA_MAXERROR     (int32_t)(CLASSB_BEACON_INTERVAL * BOARD_CLOCK_ERROR_PPM / 1000000)
#define CLASSB_BEACON_SYSTIMEDELTA_MINERROR     (int32_t)(CLASSB_BEACON_SYSTIMEDELTA_MAXERROR * (-1))
#define CLASSB_BEACON_SYSTIMEDELTA_INITERROR    (0)
#endif  //LORAMAC_CLASSB_ENABLED

/*-----------------------------------------------------------*/
// default coinfigurations
#define LORAMAC_ACTMODE_ABP_ENABLED     // Enable ABP
#define LORAMAC_RXC_CONTINUOUS_ENABLED  // Use radio driver continuous rx function in case of RxC
#define LORAMAC_CTX_SAVERESTORE_ENALED
#define LORAMAC_SET_CH_RX1FREQ_ENABLED  // Enable Rx1Frequency in the struct 'ChannelParam_t'

/*!
 * Custom for LoRaWAN/PrivateLoRa combo
 */
#if defined( LORACOMBO_ENABLED )
// Disable ABP
#undef  LORAMAC_ACTMODE_ABP_ENABLED
#endif

/*!
 * Custom for RA0E1
 */
#if  defined(__RA0E1__)

// Check region definition
#if defined(REGION_AS923)
#define REGION_DEF_AS923_ENABLED    (1)
#else
#define REGION_DEF_AS923_ENABLED    (0)
#endif

#if defined(REGION_EU868)
#define REGION_DEF_EU868_ENABLED    (1)
#else
#define REGION_DEF_EU868_ENABLED    (0)
#endif

#if defined(REGION_US915)
#define REGION_DEF_US915_ENABLED    (1)
#else
#define REGION_DEF_US915_ENABLED    (0)
#endif

#if defined(REGION_AU915)
#define REGION_DEF_AU915_ENABLED    (1)
#else
#define REGION_DEF_AU915_ENABLED    (0)
#endif

#if defined(REGION_IN865)
#define REGION_DEF_IN865_ENABLED    (1)
#else
#define REGION_DEF_IN865_ENABLED    (0)
#endif

#if defined(REGION_KR920)
#define REGION_DEF_KR920_ENABLED    (1)
#else
#define REGION_DEF_KR920_ENABLED    (0)
#endif

#define REGION_NUM_ENABLED  ( REGION_DEF_AS923_ENABLED + REGION_DEF_EU868_ENABLED + \
                              REGION_DEF_US915_ENABLED + REGION_DEF_AU915_ENABLED + \
                              REGION_DEF_IN865_ENABLED + REGION_DEF_KR920_ENABLED )

#if (REGION_NUM_ENABLED == 0)
#error "Region not defined"
#endif
#if (REGION_NUM_ENABLED > 1)
    #error "RA0E1 does not support multi-regions."
#endif

// Check ClassB definition before configuration
#ifdef LORAMAC_CLASSB_ENABLED
    #error "RA0E1 does not support class B features."
#endif

// Disable ABP
#undef  LORAMAC_ACTMODE_ABP_ENABLED
// Do not use radio driver continuous rx function in case of RxC
#undef  LORAMAC_RXC_CONTINUOUS_ENABLED
#undef  LORAMAC_CTX_SAVERESTORE_ENALED
// Remove Rx1Frequency from ChannelParam_t if region is US915 or AU915
#if defined(REGION_US915) || defined(REGION_AU915)
#undef  LORAMAC_SET_CH_RX1FREQ_ENABLED
#endif

#endif  // Custom

#endif  //__LORAMACCONFIG_H__
