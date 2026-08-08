/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

/******************************************************************************
   include
******************************************************************************/
#include "board.h"
#include "privatelora_proc.h"
#include "privatelora_nvmdata_table.h"

// NVM Read/Write table
//   - Read/Write request flag
//   - Read/Write parameter type
//   - Parameter (void pointer) to read/write
//   - Size of parameter
//   - ID to read/write NVM
const AppPrvLoRaNvmDataTable_t appPrvLoRaNvmDataTable[ APP_PRVLORA_NUM_NVMDATA ] =
{
    /* PrvLoRaSettings */
    { APP_PRVLORA_NVMDATA_RWFLG_PRVLORASETTINGS,    APP_PRVLORA_NVMDATA_TYPE_FIXED,
                                                    (uint8_t *)APP_PRVLORA_NVMDATA_PARAM_PRVLORASETTINGS, 
                                                    APP_PRVLORA_NVMDATA_SIZE_PRVLORASETTINGS,
                                                    APP_PRVLORA_NVMDATA_ID_PRVLORASETTINGS },
    /* TxCycle */
    { APP_PRVLORA_NVMDATA_RWFLG_TXCYCLE,            APP_PRVLORA_NVMDATA_TYPE_FIXED,
                                                    (uint8_t *)APP_PRVLORA_NVMDATA_PARAM_TXCYCLE, 
                                                    APP_PRVLORA_NVMDATA_SIZE_TXCYCLE,
                                                    APP_PRVLORA_NVMDATA_ID_TXCYCLE },
#if (PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM >= 1)
    /* RemoteDevice #0 */
    { APP_PRVLORA_NVMDATA_RWFLG_REMOTEDEVICE,       APP_PRVLORA_NVMDATA_TYPE_VAR( 0 ),
                                                    (uint8_t *)APP_PRVLORA_NVMDATA_PARAM_REMOTEDEVICE( 0 ),
                                                    APP_PRVLORA_NVMDATA_SIZE_REMOTEDEVICE,
                                                    APP_PRVLORA_NVMDATA_ID_REMOTEDEVICE( 0 )    },
#endif
#if (PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM >= 2)
    /* RemoteDevice #1 */
    { APP_PRVLORA_NVMDATA_RWFLG_REMOTEDEVICE,       APP_PRVLORA_NVMDATA_TYPE_VAR( 1 ),
                                                    (uint8_t *)APP_PRVLORA_NVMDATA_PARAM_REMOTEDEVICE( 1 ),
                                                    APP_PRVLORA_NVMDATA_SIZE_REMOTEDEVICE,
                                                    APP_PRVLORA_NVMDATA_ID_REMOTEDEVICE( 1 )    },
#endif
#if (PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM >= 3)
    /* RemoteDevice #2 */
    { APP_PRVLORA_NVMDATA_RWFLG_REMOTEDEVICE,       APP_PRVLORA_NVMDATA_TYPE_VAR( 2 ),
                                                    (uint8_t *)APP_PRVLORA_NVMDATA_PARAM_REMOTEDEVICE( 2 ),
                                                    APP_PRVLORA_NVMDATA_SIZE_REMOTEDEVICE,
                                                    APP_PRVLORA_NVMDATA_ID_REMOTEDEVICE( 2 )    },
#endif
#if (PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM >= 4)
#error "Range is over"
#endif
};
