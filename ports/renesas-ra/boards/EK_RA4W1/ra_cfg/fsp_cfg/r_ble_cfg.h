/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef R_BLE_CFG_H
#define R_BLE_CFG_H

/***********************************************************************************************************************
 * BLE Stack Configuration
 **********************************************************************************************************************/

/* BLE Stack Variant Selection (per FSP r_ble_api.h)
 * 0: Extended
 * 1: Balance
 * 2: Compact
 */
#define BLE_CFG_LIBRARY_TYPE                (2)  // Compact variant (matches linked prebuilt compact library)

/* BLE GAP Configuration */
#define BLE_CFG_RF_DEEP_SLEEP_EN            (0)  // Disable deep sleep for now
#define BLE_CFG_RF_SLOW_CLK_PPM             (50) // Slow clock accuracy in ppm

/* BLE GATT Configuration */
#define BLE_CFG_EN_GATT_CACHING             (0)  // Disable GATT caching
#define BLE_CFG_EN_SECURE_DATA              (0)  // Disable secure data for now

/* BLE L2CAP Configuration */
#define BLE_CFG_MAX_L2CAP_CBFC_PSM          (1)  // Max L2CAP PSMs

/* BLE Memory Configuration */
#define BLE_CFG_TOTAL_HEAP_SIZE             (8192)  // 8KB heap for BLE stack

/* BLE Event Queue Configuration */
#define BLE_CFG_EVENT_QUEUE_DEPTH           (32)    // Match MICROPY_BLE_EVENT_QUEUE_SIZE

/* BLE HCI Configuration */
#define BLE_CFG_HCI_MODE_EN                 (0)     // Disable HCI mode (use direct API)

/* BLE Privacy Configuration */
#define BLE_CFG_EN_RPA                      (0)     // Disable Resolvable Private Address for now

/* BLE Security Configuration */
#define BLE_CFG_EN_SEC_DATA                 (0)     // Disable security data storage for now

/* BLE Advertising Configuration */
#define BLE_CFG_MAX_ADV_SETS                (1)     // Max advertising sets

/* BLE Connection Configuration */
#define BLE_CFG_MAX_CONN                    (1)     // Max simultaneous connections

/* BLE GATT Server Configuration */
#define BLE_CFG_GATTS_MAX_SERVICES          (4)     // Max GATT services
#define BLE_CFG_GATTS_MAX_CHAR              (16)    // Max GATT characteristics
#define BLE_CFG_GATTS_MAX_CHAR_DESC         (16)    // Max GATT descriptors

/* BLE GATT Client Configuration */
#define BLE_CFG_GATTC_MAX_SERVICES          (4)     // Max discovered services
#define BLE_CFG_GATTC_MAX_CHAR              (16)    // Max discovered characteristics

/* BLE Vendor Specific Configuration */
#define BLE_CFG_EN_VS                       (0)     // Disable vendor specific commands

/* BLE Debug Configuration */
#define BLE_CFG_EN_DEBUG_LOG                (0)     // Disable debug logging

#endif // R_BLE_CFG_H

