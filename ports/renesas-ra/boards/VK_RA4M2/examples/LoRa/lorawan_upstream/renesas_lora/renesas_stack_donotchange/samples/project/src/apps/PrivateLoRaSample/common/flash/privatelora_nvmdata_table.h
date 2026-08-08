/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __PRVLORA_NVMDATA_TABLE_H__
#define __PRVLORA_NVMDATA_TABLE_H__

#include "PrivateLoRaConfig.h"

#define APP_PRVLORA_NUM_NVMDATA         ( 2 + PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM )

// Base block id (block #0 - #5 are reserved for lorasample parametes)
// Note: 1 block = 256 bytes
#ifdef LORACOMBO_ENABLED
    // block #0 - #5 are reserved for lorasample parametes
    #define _APP_PRVLORA_NVMID_START    ( 6 )
#else
    #define _APP_PRVLORA_NVMID_START    ( 0 )
#endif

// block #0(+offset) - #1(+offset)  : 2 blocks
#define APP_PRVLORA_NVMDATA_ID_PRVLORASETTINGS  ( _APP_PRVLORA_NVMID_START + 0 )

// block #2(+offset) - #3(+offset)  : 2 blocks
#define APP_PRVLORA_NVMDATA_ID_TXCYCLE          ( _APP_PRVLORA_NVMID_START + 2 )

// block #4+{(n)*2}(+offset) - #5+{(n)*2}(+offset) : 2 blocks * (n+1)  (n is 0 origin)
#define APP_PRVLORA_NVMDATA_ID_REMOTEDEVICE(n)  ( _APP_PRVLORA_NVMID_START + 4 + ((n) * 2) )


#define APP_PRVLORA_NVMDATA_BLKID_START         _APP_PRVLORA_NVMID_START
#define APP_PRVLORA_NVMDATA_BLKID_COUNT         ( APP_PRVLORA_NUM_NVMDATA * 2 )

#endif // __PRVLORA_NVMDATA_TABLE_H__
