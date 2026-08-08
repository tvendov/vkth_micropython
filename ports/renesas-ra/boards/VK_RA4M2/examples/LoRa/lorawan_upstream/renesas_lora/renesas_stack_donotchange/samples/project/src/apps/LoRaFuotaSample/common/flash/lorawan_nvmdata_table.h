/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __R_DFLASH_LWPERSIST_TABLE_H__
#define __R_DFLASH_LWPERSIST_TABLE_H__

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
#define APP_LORAWAN_NUM_NVMDATA      5
#else
#define APP_LORAWAN_NUM_NVMDATA      1
#endif

// Base block id (block #0 - #3 are reserved for basic parametes)
// Base block id (block #14 - #31 are reserved for future use)
// Note: 1 block = 256 bytes 

// block #4 - #13 :
    // Requried for OTAA mode
#define APP_LORAWAN_NVMDATA_ID_GROUP1         4   // block #4 - #5 : 2 blocks, CircularBuffer, 34byte
        // APP_LORAWAN_NVMDATA_RWFLG_DEV_NONCE
        // APP_LORAWAN_NVMDATA_RWFLG_APP_NONCE

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    // Required for ABP mode
#define APP_LORAWAN_NVMDATA_ID_UPLINK_FCNT    6   // block #6 - #7 : 2 blocks, CircularBuffer, 2byte
#define APP_LORAWAN_NVMDATA_ID_DOWNLINK_FCNT  8   // block #8 - #9 : 2 blocks, CircularBuffer, 2byte
#define APP_LORAWAN_NVMDATA_ID_CHANNELS       10  // block #10 - #11 : 2 blocks, CircularBuffer, 160byte
#define APP_LORAWAN_NVMDATA_ID_GROUP2         12  // block #12 - #13 : 2 blocks, CircularBuffer, 34byte          
        // APP_LORAWAN_NVMDATA_ID_CHANNELS_MASK          
        // APP_LORAWAN_NVMDATA_ID_CHANNELS_DATARATE      
        // APP_LORAWAN_NVMDATA_ID_CHANNELS_NB_TRANS      
        // APP_LORAWAN_NVMDATA_ID_CHANNELS_TXPOWER       
        // APP_LORAWAN_NVMDATA_ID_MAX_DCYCLE             
        // APP_LORAWAN_NVMDATA_ID_RX1_DROFFSET           
        // APP_LORAWAN_NVMDATA_ID_RX2_FREQUENCY          
        // APP_LORAWAN_NVMDATA_ID_RX2_DATARATE           
        // APP_LORAWAN_NVMDATA_ID_RECEIVE_DELAY_1        
        // APP_LORAWAN_NVMDATA_ID_MAX_EIRP               
        // APP_LORAWAN_NVMDATA_ID_DOWNLINK_DWELLTIME     
        // APP_LORAWAN_NVMDATA_ID_UPLINK_DWELLTIME       
        // APP_LORAWAN_NVMDATA_ID_PING_SLOT_DATARATE     
#endif

#define APP_LORAWAN_NVMDATA_RWFLG_GROUP1          \
            (                                           \
                APP_LORAWAN_NVMDATA_RWFLG_DEV_NONCE          /* DevNonce */               \
              | APP_LORAWAN_NVMDATA_RWFLG_APP_NONCE          /* App/JoinNonce */          \
            )

#define APP_LORAWAN_NVMDATA_SIZE_GROUP1          \
            (                                           \
                APP_LORAWAN_NVMDATA_SIZE_DEV_NONCE          /* DevNonce */               \
              + APP_LORAWAN_NVMDATA_SIZE_APP_NONCE          /* App/JoinNonce */          \
            )

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
#define APP_LORAWAN_NVMDATA_RWFLG_GROUP2          \
            (                                           \
                APP_LORAWAN_NVMDATA_RWFLG_CHANNELS_MASK      /* ChannelMask */            \
              | APP_LORAWAN_NVMDATA_RWFLG_CHANNELS_DATARATE  /* DataRate (ADR on) */      \
              | APP_LORAWAN_NVMDATA_RWFLG_CHANNELS_NB_TRANS  /* NbTrans */                \
              | APP_LORAWAN_NVMDATA_RWFLG_CHANNELS_TXPOWER   /* TxPower */                \
              | APP_LORAWAN_NVMDATA_RWFLG_MAX_DCYCLE         /* MaxDutycycle */           \
              | APP_LORAWAN_NVMDATA_RWFLG_RX1_DROFFSET       /* Rx1DROffset */            \
              | APP_LORAWAN_NVMDATA_RWFLG_RX2_FREQUENCY      /* Rx2Channels(Freq) */      \
              | APP_LORAWAN_NVMDATA_RWFLG_RX2_DATARATE       /* Rx2Channels(DR) */        \
              | APP_LORAWAN_NVMDATA_RWFLG_RECEIVE_DELAY_1    /* RxTimingDelay */          \
              | APP_LORAWAN_NVMDATA_RWFLG_MAX_EIRP           /* MaxEIRP */                \
              | APP_LORAWAN_NVMDATA_RWFLG_DOWNLINK_DWELLTIME /* DownlinkDwellTime */      \
              | APP_LORAWAN_NVMDATA_RWFLG_UPLINK_DWELLTIME   /* UplinkDwellTime */        \
              | APP_LORAWAN_NVMDATA_RWFLG_PING_SLOT_DATARATE /* PingSlotDataRate */       \
            )

#define APP_LORAWAN_NVMDATA_SIZE_GROUP2          \
            (                                           \
                APP_LORAWAN_NVMDATA_SIZE_CHANNELS_MASK      /* ChannelMask */            \
              + APP_LORAWAN_NVMDATA_SIZE_CHANNELS_DATARATE  /* DataRate (ADR on) */      \
              + APP_LORAWAN_NVMDATA_SIZE_CHANNELS_NB_TRANS  /* NbTrans */                \
              + APP_LORAWAN_NVMDATA_SIZE_CHANNELS_TXPOWER   /* TxPower */                \
              + APP_LORAWAN_NVMDATA_SIZE_MAX_DCYCLE         /* MaxDutycycle */           \
              + APP_LORAWAN_NVMDATA_SIZE_RX1_DROFFSET       /* Rx1DROffset */            \
              + APP_LORAWAN_NVMDATA_SIZE_RX2_FREQUENCY      /* Rx2Channels(Freq) */      \
              + APP_LORAWAN_NVMDATA_SIZE_RX2_DATARATE       /* Rx2Channels(DR) */        \
              + APP_LORAWAN_NVMDATA_SIZE_RECEIVE_DELAY_1    /* RxTimingDelay */          \
              + APP_LORAWAN_NVMDATA_SIZE_MAX_EIRP           /* MaxEIRP */                \
              + APP_LORAWAN_NVMDATA_SIZE_DOWNLINK_DWELLTIME /* DownlinkDwellTime */      \
              + APP_LORAWAN_NVMDATA_SIZE_UPLINK_DWELLTIME   /* UplinkDwellTime */        \
              + APP_LORAWAN_NVMDATA_SIZE_PING_SLOT_DATARATE /* PingSlotDataRate */       \
            )
#endif

#endif  // __R_DFLASH_LWPERSIST_TABLE_H__

