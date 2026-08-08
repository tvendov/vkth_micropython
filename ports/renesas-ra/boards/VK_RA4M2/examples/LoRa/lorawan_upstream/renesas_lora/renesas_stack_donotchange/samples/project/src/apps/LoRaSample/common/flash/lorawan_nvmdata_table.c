/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

/******************************************************************************
   include
******************************************************************************/
#include "board.h"
#include "lorawan_proc.h"
#include "lora_sample.h"


// NVM Read/Write table
//   - Read/Write request flag
//   - Parameter (void pointer) to read/write
//   - Size of parameter
//   - ID to read/write NVM

const AppLoraWanNvmDataTable_t appLoraWanNvmDataTable[APP_LORAWAN_NUM_NVMDATA] = 
{
// Requried for OTAA mode

    /* Group1 (DevNonce, AppNonce) */
    { APP_LORAWAN_NVMDATA_RWFLG_GROUP1,                (void *)&appLoraWanNvmData.devNonce,
                                                             APP_LORAWAN_NVMDATA_SIZE_GROUP1,
                                                             APP_LORAWAN_NVMDATA_ID_GROUP1        },
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
// Required for ABP mode

    /* FrameCounter(downlink) */
    { APP_LORAWAN_NVMDATA_RWFLG_DOWNLINK_FCNT,        (void *)&appLoraWanNvmData.downlinkFCnt,
                                                             APP_LORAWAN_NVMDATA_SIZE_DOWNLINK_FCNT,
                                                             APP_LORAWAN_NVMDATA_ID_DOWNLINK_FCNT      },
     /* FrameCounter(uplink) */
    { APP_LORAWAN_NVMDATA_RWFLG_UPLINK_FCNT,          (void *)&appLoraWanNvmData.uplinkFCnt,
                                                             APP_LORAWAN_NVMDATA_SIZE_UPLINK_FCNT,
                                                             APP_LORAWAN_NVMDATA_ID_UPLINK_FCNT        },

    /* Channels */
    { APP_LORAWAN_NVMDATA_RWFLG_CHANNELS,              (void *)&appLoraWanNvmData.channelsList,
                                                             APP_LORAWAN_NVMDATA_SIZE_CHANNELS,
                                                             APP_LORAWAN_NVMDATA_ID_CHANNELS        },

    /* Group2 (ChannelsMask, ChannlesDataRate, NbTrans, TxPower, ...) */          
    { APP_LORAWAN_NVMDATA_RWFLG_GROUP2,                (void *)&appLoraWanNvmData.channelsMask,
                                                             APP_LORAWAN_NVMDATA_SIZE_GROUP2,
                                                             APP_LORAWAN_NVMDATA_ID_GROUP2        },
#endif
};

