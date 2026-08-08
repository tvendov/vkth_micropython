/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrivateLoRaConfig.h
  * @author  Renesas Electronics Corporation
  * @brief
**/

#ifndef __PRIVATELORACONFIG_H__
#define __PRIVATELORACONFIG_H__

/*------------------------*/
/* define (configuration) */

/*
 * Config - Max number of remote device
 */
#define PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM         1

/*
 * Config - Max number of indirect tx queue
 */
#define PRVLORA_CONFIG_INDIRECT_TX_QUEUE_MAXNUM     1

/*
 * Config - System maximum overall timing error (msec)
 * Config - adjustment tx/rx timing (msec)
 */
#if defined(__RA0E1__)
    #define PRVLORA_CONFIG_SYSTEM_MAX_RX_ERROR          (35)    // - System maximum overall timing error
    #define PRVLORA_CONFIG_TRXADJUST_TX2RX              (7)     // - adjust timing; from TxDone to RxOn
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_FIXED     (8)     // - adjust timing; from RxDone to just before Tx
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_SLOPE     (395)   //   + time to transmit Tx data to radio
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_INTERCEPT (10524) //     (time varies depending on data length)
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_RATIO     (10000) //     (ratio)*(adjTime)=(slope)*(dataLen)+(intercept)
#elif defined(__RA0E2__)
    #define PRVLORA_CONFIG_SYSTEM_MAX_RX_ERROR          (35)    // - System maximum overall timing error
    #define PRVLORA_CONFIG_TRXADJUST_TX2RX              (8)     // - adjust timing; from TxDone to RxOn
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_FIXED     (9)     // - adjust timing; from RxDone to just before Tx
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_SLOPE     (448)   //   + time to transmit Tx data to radio
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_INTERCEPT (12256) //     (time varies depending on data length)
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_RATIO     (10000) //     (ratio)*(adjTime)=(slope)*(dataLen)+(intercept)
#else  // (RA2E1,RA2L1)
    #define PRVLORA_CONFIG_SYSTEM_MAX_RX_ERROR          (35)    // - System maximum overall timing error
    #define PRVLORA_CONFIG_TRXADJUST_TX2RX              (10)    // - adjust timing; from TxDone to RxOn
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_FIXED     (11)    // - adjust timing; from RxDone to just before Tx
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_SLOPE     (576)   //   + time to transmit Tx data to radio
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_INTERCEPT (14292) //     (time varies depending on data length)
    #define PRVLORA_CONFIG_TRXADJUST_RX2TXRES_RATIO     (10000) //     (ratio)*(adjTime)=(slope)*(dataLen)+(intercept)
#endif

//-----------------------------------------------------------------------------
// (don't edit) Check MCU definition
    #if defined(__RA0E1__) && defined(__RA0E2__)
        #error "Multiple RA0s are defined."
    #endif
    #if (RP_CPU_CLK != 8)
        #error "RP_CPU_CLK should be set to 8."
    #endif

/*
 * Check config
 */
    #define PRVLORA_CFGCHK_REMOTE_DEVICE_ABSMAXNUM      3
    #define PRVLORA_CFGCHK_INDIRECT_TX_QUEUE_ABSMAXNUM  3

#if ( PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM == 0 )
#error "PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM is zero."
#elif ( PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM > PRVLORA_CFGCHK_REMOTE_DEVICE_ABSMAXNUM )
#error "PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM is over."
#endif

#if ( PRVLORA_CONFIG_INDIRECT_TX_QUEUE_MAXNUM == 0 )
#error "PRVLORA_CONFIG_INDIRECT_TX_QUEUE_MAXNUM is zero."
#elif ( PRVLORA_CONFIG_INDIRECT_TX_QUEUE_MAXNUM > PRVLORA_CFGCHK_INDIRECT_TX_QUEUE_ABSMAXNUM )
#error "PRVLORA_CONFIG_INDIRECT_TX_QUEUE_MAXNUM is over."
#endif


#endif  // __PRIVATELORACONFIG_H__
