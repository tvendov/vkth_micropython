/* generated configuration header file - matching Renesas BLE example */
#ifndef R_BLE_CFG_H_
#define R_BLE_CFG_H_
#ifdef __cplusplus
extern "C" {
#endif

/* BLE Stack Variant: 0=Extended/All, 1=Balance, 2=Compact */
#define BLE_CFG_LIBRARY_TYPE (0)

/* Debug addresses */
#define BLE_CFG_RF_DEBUG_PUBLIC_ADDRESS {0xFF,0xFF,0xFF,0x50,0x90,0x74}
#define BLE_CFG_RF_DEBUG_RANDOM_ADDRESS {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}

/* Connection parameters - reduced for RAM constraints */
#define BLE_CFG_RF_CONNECTION_MAXIMUM (1)
#define BLE_CFG_RF_CONNECTION_DATA_MAXIMUM (247)
#define BLE_CFG_RF_ADVERTISING_DATA_MAXIMUM (31)
#define BLE_CFG_RF_ADVERTISING_SET_MAXIMUM (1)
#define BLE_CFG_RF_SYNC_SET_MAXIMUM (1)

/* Security */
#define BLE_CFG_ENABLE_SECURE_DATA (0)
#define BLE_CFG_SECURE_DATA_DATAFLASH_BLOCK (0)
#define BLE_CFG_NUMBER_BONDING (7)

/* Event notifications */
#define BLE_CFG_EVENT_NOTIFY_CONNECTION_START (0)
#define BLE_CFG_EVENT_NOTIFY_CONNECTION_CLOSE (0)
#define BLE_CFG_EVENT_NOTIFY_ADVERTISING_START (0)
#define BLE_CFG_EVENT_NOTIFY_ADVERTISING_CLOSE (0)
#define BLE_CFG_EVENT_NOTIFY_SCANNING_START (0)
#define BLE_CFG_EVENT_NOTIFY_SCANNING_CLOSE (0)
#define BLE_CFG_EVENT_NOTIFY_INITIATING_START (0)
#define BLE_CFG_EVENT_NOTIFY_INITIATING_CLOSE (0)
#define BLE_CFG_EVENT_NOTIFY_DEEP_SLEEP_START (0)
#define BLE_CFG_EVENT_NOTIFY_DEEP_SLEEP_WAKEUP (0)
#define BLE_CFG_EVENT_NOTIFY_ENABLE_VAL (0)

/* RF Configuration - CRITICAL */
#define BLE_CFG_RF_CLVAL (6)
#define BLE_CFG_RF_DCDC_CONVERTER_ENABLE (0)
#define BLE_CFG_RF_EXT32K_EN (0)
#define BLE_CFG_RF_MCU_CLKOUT_PORT (0)
#define BLE_CFG_RF_MCU_CLKOUT_FREQ (0)
#define BLE_CFG_RF_SCA (250)
#define BLE_CFG_RF_MAX_TX_POW (1)
#define BLE_CFG_RF_DEF_TX_POW (0)
#define BLE_CFG_RF_CLKOUT_EN (0)
#define BLE_CFG_RF_DEEP_SLEEP_EN (0)  /* Disabled during RF bring-up debugging */

/* Clock */
#define BLE_CFG_MCU_MAIN_CLK_KHZ (8000)

/* Device data storage */
#define BLE_CFG_DEV_DATA_CF_BLOCK (255)
#define BLE_CFG_DEV_DATA_DF_BLOCK (-1)

/* GATT */
#define BLE_CFG_GATT_MTU_SIZE (247)

/* Timer */
#define BLE_CFG_TIMER_NUMBER_OF_SLOT (10)

#ifdef __cplusplus
}
#endif
#endif /* R_BLE_CFG_H_ */

