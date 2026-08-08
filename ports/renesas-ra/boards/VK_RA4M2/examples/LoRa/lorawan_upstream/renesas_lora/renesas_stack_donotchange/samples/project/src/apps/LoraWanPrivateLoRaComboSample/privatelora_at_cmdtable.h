/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#include "at-command.h"

#include "privatelora_at_proc.h"


#if (AT_EXTCMD_TAB_ARRAYSIZE == 0)
//-----------------------------------------------------
// AT command table (ROM table)
//-----------------------------------------------------
const AtExtendTab_t AppAtPrvLoRaCommands[] =
{
    /* set,  get,  act,  len, *name */
    { AppAtPrvLoRaResetAct,        NULL,                         NULL,                   6, (int8_t *)"+RESET"    },
    { NULL,                        AppAtPrvLoRaVerRead,          NULL,                   4, (int8_t *)"+VER"      },
    { NULL,                        NULL,                         AppAtPrvLoRaVerSaveAct, 5, (int8_t *)"+SAVE"     },
    { AppAtPrvLoRaVerLoadAct,      NULL,                         AppAtPrvLoRaVerLoadAct, 5, (int8_t *)"+LOAD"     },
    { AppAtPrvLoRaRegionSet,       AppAtPrvLoRaRegionRead,       NULL,                   7, (int8_t *)"+REGION"   },
    { AppAtPrvLoRaMacAddrSet,      AppAtPrvLoRaMacAddrRead,      NULL,                   7, (int8_t *)"+DEVEUI"   },
    { AppAtPrvLoRaChannelIDSet,    AppAtPrvLoRaChannelIDRead,    NULL,                   5, (int8_t *)"+CHID"     },
    { AppAtPrvLoRaDRSet,           AppAtPrvLoRaDRRead,           NULL,                   3, (int8_t *)"+DR"       },
    { AppAtPrvLoRaTxPowerSet,      AppAtPrvLoRaTxPowerRead,      NULL,                   8, (int8_t *)"+TXPOWER"  },
    { AppAtPrvLoRaRxOnWhenIdleSet, AppAtPrvLoRaRxOnWhenIdleRead, NULL,                   5, (int8_t *)"+RXON"     },
    { AppAtPrvLoRaRemoveDevSet,    NULL,                         NULL,                   7, (int8_t *)"+RMTDEV"   },
    { AppAtPrvLoRaKeyReqAct,       NULL,                         NULL,                   7, (int8_t *)"+KEYREQ"   },
    { AppAtPrvLoRaKeyResSet,       AppAtPrvLoRaKeyResRead,       NULL,                   7, (int8_t *)"+KEYRES"   },
    { AppAtPrvLoRaTxOptionsSet,    AppAtPrvLoRaTxOptionsRead,    NULL,                   6, (int8_t *)"+TXOPT"    },
    { AppAtPrvLoRaSendAct,         NULL,                         NULL,                   5, (int8_t *)"+SEND"     },
    { AppAtPrvLoRaSendHexAct,      NULL,                         NULL,                   8, (int8_t *)"+SENDHEX"  },
    { AppAtPrvLoRaDevInfoAct,      NULL,                         NULL,                   8, (int8_t *)"+DEVINFO"  },
    { AppAtPrvLoRaTxCycleAct,      NULL,                         NULL,                   8, (int8_t *)"+TXCYCLE"  },
    { AppAtPrvLoRaRssiSet,         AppAtPrvLoRaRssiRead,         NULL,                   5, (int8_t *)"+RSSI"     },
#if defined(DEBUG_PRVLORA)
    { AppAtPrvLoRaDebugSet,        AppAtPrvLoRaDebugRead,        NULL,                   6, (int8_t *)"+DEBUG"    },
#endif
#if defined(LORACOMBO_ENABLED)
    { AppAtLoRaModeSet,            AppAtLoRaModeRead,            NULL,                   9, (int8_t *)"+LORAMODE" },
#endif
};
#endif
