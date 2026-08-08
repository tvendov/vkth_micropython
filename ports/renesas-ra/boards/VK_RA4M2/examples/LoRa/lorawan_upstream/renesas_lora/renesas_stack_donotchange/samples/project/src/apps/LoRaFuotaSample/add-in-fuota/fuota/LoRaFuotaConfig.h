/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __LORAFUOTACONFIG_H__
#define __LORAFUOTACONFIG_H__

/*!
 * FUOTA version definition.
 */
#if defined(FUOTA_VERSION_1_0_0) && defined(FUOTA_VERSION_2_0_0)
#error "Specified both FUOTA_VERSION_1_0_0 and FUOTA_VERSION_2_0_0"
#endif

#ifdef FUOTA_VERSION_1_0_0
#define FUOTA_VERSION       (0x01000000)
#endif

#ifdef FUOTA_VERSION_2_0_0
#define FUOTA_VERSION       (0x02000000)
#endif

#ifndef FUOTA_VERSION
#define FUOTA_VERSION       (0x01000000)
#endif

#undef FUOTA_VERSION_1_0_0
#undef FUOTA_VERSION_2_0_0
#define FUOTA_VERSION_1_0_0     (0x01000000)
#define FUOTA_VERSION_2_0_0     (0x02000000)

/*!
 * FUOTA configuration
 */

// Configuration for Clock Synchronization
#define FUOTA_CONFIG_CLKSNC_INIT_SYNC_PERIOD_SEC    60  // AppTimeReq request period (sec) until AppTime is synchronized [10-0x418390]

// Configuration for Fragmented Data Block Transport
#define FUOTA_CONFIG_FRGMNT_MAX_FRAG_INDEX          1       // Max number of FragIndex (fragment session) [1-4]
#define FUOTA_CONFIG_FRGMNT_MAX_DATABLK_SIZE        4096    // Max buffer size to receive fragment data block
#define FUOTA_CONFIG_FRGMNT_MAX_NBFRAG              200     // Max number of uncoded fragment
#define FUOTA_CONFIG_FRGMNT_MAX_NBFRAG_LOST         50      // Max number of dropped uncoded fragment 

// Configuration for Remote Multicast Setup
#define FUOTA_CONFIG_RMTMC_MAX_MC_CTX               1       // Max number of multicast context [1-LORAMAC_MAX_MC_CTX]

#endif  /* __LORAFUOTACONFIG_H__ */
