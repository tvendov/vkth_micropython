/***********************************************************************************************************************
* DISCLAIMER
* This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No
* other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all
* applicable laws, including copyright laws.
* THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
* THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM
* EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES
* SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS
* SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
* Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of
* this software. By using this software, you agree to the additional terms and conditions found by accessing the
* following link:
* http://www.renesas.com/disclaimer
*
* Copyright (C) 2019-2022 Renesas Electronics Corporation. All rights reserved.
***********************************************************************************************************************/

/**
 *  GATT DATABASE QUICK REFERENCE TABLE:
 *  Abbreviations used for PROPERTIES:
 *      BC = Broadcast
 *      RD = Read
 *      WW = Write Without Response
 *      WR = Write
 *      NT = Notification
 *      IN = Indication
 *      RW = Reliable Write
 * 
 *  HANDLE | ATT_TYPE          | PROPERTIES  | ATT_VALUE                        | DEFINITION
 *  ============================================================================================
 *  GAP Service2
 *  ============================================================================================
 *  0x0001 | 0x28,0x00         | RD          | 0x00,0x18                        | GAP Service2 Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0002 | 0x28,0x02         | RD          | 0x0E,0x00,0x36,0x00,0x15,0x18    | Automation IO Service Included Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0003 | 0x28,0x03         | RD          | 0x0B,0x04,0x00,0x00,0x2A         | Device Name characteristic Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0004 | 0x00,0x2A         | BC,RD,WR    | 0x00,0x00,0x00,0x00,0x00,0x00... | Device Name characteristic value
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0005 | 0x03,0x29         | RD,WR       | 0x00,0x00                        | Server Characteristic Configuration descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0006 | 0x28,0x03         | RD          | 0x02,0x07,0x00,0x01,0x2A         | Appearance characteristic Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0007 | 0x01,0x2A         | RD          | 0x00,0x00                        | Appearance characteristic value
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0008 | 0x28,0x03         | RD          | 0x02,0x09,0x00,0x04,0x2A         | Peripheral Preferred Connection Parameters characteristic Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0009 | 0x04,0x2A         | RD          | 0x00,0x00,0x00,0x00,0x00,0x00... | Peripheral Preferred Connection Parameters characteristic value
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x000A | 0x28,0x03         | RD          | 0x02,0x0B,0x00,0xA6,0x2A         | Central Address Resolution characteristic Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x000B | 0xA6,0x2A         | RD          | 0x00                             | Central Address Resolution characteristic value
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x000C | 0x28,0x03         | RD          | 0x02,0x0D,0x00,0xC9,0x2A         | Resolvable Private Address Only characteristic Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x000D | 0xC9,0x2A         | RD          | 0x00                             | Resolvable Private Address Only characteristic value
 *  ============================================================================================
 *  Automation IO Service
 *  ============================================================================================
 *  0x000E | 0x28,0x00         | RD          | 0x15,0x18                        | Automation IO Service Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x000F | 0x28,0x02         | RD          | 0x01,0x00,0x0D,0x00,0x00,0x18    | GAP Service2 Included Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0010 | 0x28,0x03         | RD          | 0x1E,0x11,0x00,0x56,0x2A         | Digital 0 characteristic Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0011 | 0x56,0x2A         | RD,WW,WR... | 0x00,0x00                        | Digital 0 characteristic value
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0012 | 0x02,0x29         | RD,WR       | 0x00,0x00                        | Client Characteristic Configuration descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0013 | 0x04,0x29         | RD          | 0x1B,0x00,0x00,0x00,0x01,0x00... | Characteristic Presentation Format descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0014 | 0x01,0x29         | RD,WR       | 0x00,0x00,0x00,0x00,0x00,0x00... | Characteristic User Description descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0015 | 0x00,0x29         | RD          | 0x00,0x00                        | Characteristic Extended Properties descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0016 | 0x0A,0x29         | RD,WR       | 0x00,0x00,0x00                   | Value Trigger Setting descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0017 | 0x0E,0x29         | RD,WR       | 0x00,0x00,0x00,0x00              | Time Trigger Setting descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0018 | 0x09,0x29         | RD          | 0x08                             | Number of Digitals descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0019 | 0x28,0x03         | RD          | 0x1E,0x1A,0x00,0x56,0x2A         | Digital 1 characteristic Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x001A | 0x56,0x2A         | RD,WW,WR... | 0x00,0x00                        | Digital 1 characteristic value
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x001B | 0x02,0x29         | RD,WR       | 0x00,0x00                        | Client Characteristic Configuration descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x001C | 0x04,0x29         | RD          | 0x1B,0x00,0x00,0x00,0x01,0x00... | Characteristic Presentation Format descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x001D | 0x01,0x29         | RD,WR       | 0x00,0x00,0x00,0x00,0x00,0x00... | Characteristic User Description descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x001E | 0x00,0x29         | RD          | 0x00,0x00                        | Characteristic Extended Properties descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x001F | 0x0A,0x29         | RD,WR       | 0x00,0x00,0x00                   | Value Trigger Setting descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0020 | 0x0E,0x29         | RD,WR       | 0x00,0x00,0x00,0x00              | Time Trigger Setting descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0021 | 0x09,0x29         | RD          | 0x08                             | Number of Digitals descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0022 | 0x28,0x03         | RD          | 0x1E,0x23,0x00,0x58,0x2A         | Analog 0 characteristic Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0023 | 0x58,0x2A         | RD,WW,WR... | 0x00,0x00                        | Analog 0 characteristic value
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0024 | 0x02,0x29         | RD,WR       | 0x00,0x00                        | Client Characteristic Configuration descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0025 | 0x04,0x29         | RD          | 0x06,0x00,0x00,0x00,0x01,0x00... | Characteristic Presentation Format descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0026 | 0x01,0x29         | RD,WR       | 0x00,0x00,0x00,0x00,0x00,0x00... | Characteristic User Description descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0027 | 0x00,0x29         | RD          | 0x00,0x00                        | Characteristic Extended Properties descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0028 | 0x0A,0x29         | RD,WR       | 0x00,0x00,0x00,0x00,0x00         | Value Trigger Setting descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0029 | 0x0E,0x29         | RD,WR       | 0x00,0x00,0x00,0x00              | Time Trigger Setting descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x002A | 0x06,0x29         | RD          | 0x58,0x02,0x20,0x1C              | Valid Range descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x002B | 0x28,0x03         | RD          | 0x1E,0x2C,0x00,0x58,0x2A         | Analog 1 characteristic Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x002C | 0x58,0x2A         | RD,WW,WR... | 0x00,0x00                        | Analog 1 characteristic value
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x002D | 0x02,0x29         | RD,WR       | 0x00,0x00                        | Client Characteristic Configuration descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x002E | 0x04,0x29         | RD          | 0x06,0x00,0x00,0x00,0x01,0x00... | Characteristic Presentation Format descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x002F | 0x01,0x29         | RD,WR       | 0x00,0x00,0x00,0x00,0x00,0x00... | Characteristic User Description descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0030 | 0x00,0x29         | RD          | 0x00,0x00                        | Characteristic Extended Properties descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0031 | 0x0A,0x29         | RD,WR       | 0x00,0x00,0x00,0x00,0x00         | Value Trigger Setting descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0032 | 0x0E,0x29         | RD,WR       | 0x00,0x00,0x00,0x00              | Time Trigger Setting descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0033 | 0x06,0x29         | RD          | 0x58,0x02,0x20,0x1C              | Valid Range descriptor
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0034 | 0x28,0x03         | RD          | 0x12,0x35,0x00,0x5A,0x2A         | Aggregate characteristic Declaration
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0035 | 0x5A,0x2A         | RD,NT       | 0x00,0x00,0x00,0x00,0x00,0x00... | Aggregate characteristic value
 *  -------+-------------------+-------------+----------------------------------+---------------
 *  0x0036 | 0x02,0x29         | RD,WR       | 0x00,0x00                        | Client Characteristic Configuration descriptor
 *  ============================================================================================
 
 */

/*******************************************************************************
* Includes   <System Includes> , "Project Includes"
*******************************************************************************/
#include <stdio.h>
#include "gatt_db.h"

/* MicroPython port note:
 * QE-generated GATT DB sources are shared across multiple Renesas families.
 * Some variants expect BLE_CFG_RF_CONN_MAX, while this repo uses
 * BLE_CFG_RF_CONNECTION_MAXIMUM (see boards/<BOARD>/ra_cfg/fsp_cfg/r_ble_cfg.h).
 * Keep this file buildable by providing safe fallbacks.
 */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

/*****************************************************************************
* Global definition
******************************************************************************/
static const uint8_t gs_gatt_const_uuid_arr[] =
{
    /* Primary Service Declaration : 0 */
    0x00, 0x28,

    /* Secondary Service Declaration : 2 */
    0x01, 0x28,

    /* Included Service Declaration : 4 */
    0x02, 0x28,

    /* Characteristic Declaration : 6 */
    0x03, 0x28,

    /* GAP Service2 : 8 */
    0x00, 0x18,

    /* Device Name : 10 */
    0x00, 0x2A,

    /* Server Characteristic Configuration : 12 */
    0x03, 0x29,

    /* Appearance : 14 */
    0x01, 0x2A,

    /* Peripheral Preferred Connection Parameters : 16 */
    0x04, 0x2A,

    /* Central Address Resolution : 18 */
    0xA6, 0x2A,

    /* Resolvable Private Address Only : 20 */
    0xC9, 0x2A,

    /* Automation IO Service : 22 */
    0x15, 0x18,

    /* Digital 0 : 24 */
    0x56, 0x2A,

    /* Client Characteristic Configuration : 26 */
    0x02, 0x29,

    /* Characteristic Presentation Format : 28 */
    0x04, 0x29,

    /* Characteristic User Description : 30 */
    0x01, 0x29,

    /* Characteristic Extended Properties : 32 */
    0x00, 0x29,

    /* Value Trigger Setting : 34 */
    0x0A, 0x29,

    /* Time Trigger Setting : 36 */
    0x0E, 0x29,

    /* Number of Digitals : 38 */
    0x09, 0x29,

    /* Analog 0 : 40 */
    0x58, 0x2A,

    /* Valid Range : 42 */
    0x06, 0x29,

    /* Aggregate : 44 */
    0x5A, 0x2A,

};

static uint8_t gs_gatt_value_arr[] =
{
    /* Device Name */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* Device Name : Server Characteristic Configuration */
    0, 0,

    /* Appearance */
    0x00, 0x00,

    /* Peripheral Preferred Connection Parameters */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* Central Address Resolution */
    0x00,

    /* Resolvable Private Address Only */
    0x00,

    /* Digital 0 */
    0x00, 0x00,

    /* Digital 0 : Characteristic Presentation Format */
    27, 0, 0, 0, 1, 0, 1,

    /* Digital 0 : Characteristic User Description */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* Digital 0 : Characteristic Extended Properties */
    0, 0,

    /* Digital 0 : Value Trigger Setting */
    0x00, 0x00, 0x00,

    /* Digital 0 : Time Trigger Setting */
    0x00, 0x00, 0x00, 0x00,

    /* Digital 0 : Number of Digitals */
    8,

    /* Digital 1 */
    0x00, 0x00,

    /* Digital 1 : Characteristic Presentation Format */
    27, 0, 0, 0, 1, 0, 2,

    /* Digital 1 : Characteristic User Description */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* Digital 1 : Characteristic Extended Properties */
    0, 0,

    /* Digital 1 : Value Trigger Setting */
    0x00, 0x00, 0x00,

    /* Digital 1 : Time Trigger Setting */
    0x00, 0x00, 0x00, 0x00,

    /* Digital 1 : Number of Digitals */
    8,

    /* Analog 0 */
    0x00, 0x00,

    /* Analog 0 : Characteristic Presentation Format */
    6, 0, 0, 0, 1, 0, 1,

    /* Analog 0 : Characteristic User Description */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* Analog 0 : Characteristic Extended Properties */
    0, 0,

    /* Analog 0 : Value Trigger Setting */
    0x00, 0x00, 0x00, 0x00, 0x00,

    /* Analog 0 : Time Trigger Setting */
    0x00, 0x00, 0x00, 0x00,

    /* Analog 0 : Valid Range */
    88, 2, 32, 28,

    /* Analog 1 */
    0x00, 0x00,

    /* Analog 1 : Characteristic Presentation Format */
    6, 0, 0, 0, 1, 0, 2,

    /* Analog 1 : Characteristic User Description */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* Analog 1 : Characteristic Extended Properties */
    0, 0,

    /* Analog 1 : Value Trigger Setting */
    0x00, 0x00, 0x00, 0x00, 0x00,

    /* Analog 1 : Time Trigger Setting */
    0x00, 0x00, 0x00, 0x00,

    /* Analog 1 : Valid Range */
    88, 2, 32, 28,

    /* Aggregate */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

};

static const uint8_t gs_gatt_const_value_arr[] =
{
    /* Included Service : Automation IO Service */
    0x0E, 0x00, // Start Handle
    0x36, 0x00, // End Handle
    0x15, 0x18, // UUID

    /* Device Name */
    0x0B,       // Properties
    0x04, 0x00, // Attr Handle
    0x00, 0x2A, // UUID

    /* Appearance */
    0x02,       // Properties
    0x07, 0x00, // Attr Handle
    0x01, 0x2A, // UUID

    /* Peripheral Preferred Connection Parameters */
    0x02,       // Properties
    0x09, 0x00, // Attr Handle
    0x04, 0x2A, // UUID

    /* Central Address Resolution */
    0x02,       // Properties
    0x0B, 0x00, // Attr Handle
    0xA6, 0x2A, // UUID

    /* Resolvable Private Address Only */
    0x02,       // Properties
    0x0D, 0x00, // Attr Handle
    0xC9, 0x2A, // UUID

    /* Included Service : GAP Service2 */
    0x01, 0x00, // Start Handle
    0x0D, 0x00, // End Handle
    0x00, 0x18, // UUID

    /* Digital 0 */
    0x1E,       // Properties
    0x11, 0x00, // Attr Handle
    0x56, 0x2A, // UUID

    /* Digital 1 */
    0x1E,       // Properties
    0x1A, 0x00, // Attr Handle
    0x56, 0x2A, // UUID

    /* Analog 0 */
    0x1E,       // Properties
    0x23, 0x00, // Attr Handle
    0x58, 0x2A, // UUID

    /* Analog 1 */
    0x1E,       // Properties
    0x2C, 0x00, // Attr Handle
    0x58, 0x2A, // UUID

    /* Aggregate */
    0x12,       // Properties
    0x35, 0x00, // Attr Handle
    0x5A, 0x2A, // UUID

};

static const uint8_t gs_gatt_db_const_peer_specific_val_arr[] =
{
    /* Digital 0 : Client Characteristic Configuration */
    0x00, 0x00,

    /* Digital 1 : Client Characteristic Configuration */
    0x00, 0x00,

    /* Analog 0 : Client Characteristic Configuration */
    0x00, 0x00,

    /* Analog 1 : Client Characteristic Configuration */
    0x00, 0x00,

    /* Aggregate : Client Characteristic Configuration */
    0x00, 0x00,

};

/* Peer-specific values (e.g. CCCD) storage per-connection.
 * Prefer the stack's configured max connection count.
 */
#if defined(BLE_CFG_RF_CONNECTION_MAXIMUM)
#define GATT_DB_CONN_MAX (BLE_CFG_RF_CONNECTION_MAXIMUM)
#elif defined(BLE_ABS_CFG_RF_CONNECTION_MAXIMUM)
#define GATT_DB_CONN_MAX (BLE_ABS_CFG_RF_CONNECTION_MAXIMUM)
#elif defined(BLE_CFG_RF_CONN_MAX)
#define GATT_DB_CONN_MAX (BLE_CFG_RF_CONN_MAX)
#else
#define GATT_DB_CONN_MAX (1)
#endif

static uint8_t gs_gatt_db_peer_specific_val_arr[
    sizeof(gs_gatt_db_const_peer_specific_val_arr) * (GATT_DB_CONN_MAX + 1)
];
static const st_ble_gatts_db_uuid_cfg_t gs_gatt_type_table[] =
{
    /* 0 : Primary Service Declaration */
    {
        /* UUID Offset */
        0,
        /* First Occurrence for Type */
        0x0001,
        /* Last Occurrence for Type */
        0x000E,
    },

    /* 1 : GAP Service2 */
    {
        /* UUID Offset */
        8,
        /* First Occurrence for Type */
        0x0001,
        /* Last Occurrence for Type */
        0x0000,
    },

    /* 2 : Included Service Declaration */
    {
        /* UUID Offset */
        4,
        /* First Occurrence for Type */
        0x0002,
        /* Last Occurrence for Type */
        0x000F,
    },

    /* 3 : Characteristic Declaration */
    {
        /* UUID Offset */
        6,
        /* First Occurrence for Type */
        0x0003,
        /* Last Occurrence for Type */
        0x0034,
    },

    /* 4 : Device Name */
    {
        /* UUID Offset */
        10,
        /* First Occurrence for Type */
        0x0004,
        /* Last Occurrence for Type */
        0x0000,
    },

    /* 5 : Server Characteristic Configuration */
    {
        /* UUID Offset */
        12,
        /* First Occurrence for Type */
        0x0005,
        /* Last Occurrence for Type */
        0x0000,
    },

    /* 6 : Appearance */
    {
        /* UUID Offset */
        14,
        /* First Occurrence for Type */
        0x0007,
        /* Last Occurrence for Type */
        0x0000,
    },

    /* 7 : Peripheral Preferred Connection Parameters */
    {
        /* UUID Offset */
        16,
        /* First Occurrence for Type */
        0x0009,
        /* Last Occurrence for Type */
        0x0000,
    },

    /* 8 : Central Address Resolution */
    {
        /* UUID Offset */
        18,
        /* First Occurrence for Type */
        0x000B,
        /* Last Occurrence for Type */
        0x0000,
    },

    /* 9 : Resolvable Private Address Only */
    {
        /* UUID Offset */
        20,
        /* First Occurrence for Type */
        0x000D,
        /* Last Occurrence for Type */
        0x0000,
    },

    /* 10 : Automation IO Service */
    {
        /* UUID Offset */
        22,
        /* First Occurrence for Type */
        0x000E,
        /* Last Occurrence for Type */
        0x0000,
    },

    /* 11 : Digital 0 */
    {
        /* UUID Offset */
        24,
        /* First Occurrence for Type */
        0x0011,
        /* Last Occurrence for Type */
        0x001A,
    },

    /* 12 : Client Characteristic Configuration */
    {
        /* UUID Offset */
        26,
        /* First Occurrence for Type */
        0x0012,
        /* Last Occurrence for Type */
        0x0036,
    },

    /* 13 : Characteristic Presentation Format */
    {
        /* UUID Offset */
        28,
        /* First Occurrence for Type */
        0x0013,
        /* Last Occurrence for Type */
        0x002E,
    },

    /* 14 : Characteristic User Description */
    {
        /* UUID Offset */
        30,
        /* First Occurrence for Type */
        0x0014,
        /* Last Occurrence for Type */
        0x002F,
    },

    /* 15 : Characteristic Extended Properties */
    {
        /* UUID Offset */
        32,
        /* First Occurrence for Type */
        0x0015,
        /* Last Occurrence for Type */
        0x0030,
    },

    /* 16 : Value Trigger Setting */
    {
        /* UUID Offset */
        34,
        /* First Occurrence for Type */
        0x0016,
        /* Last Occurrence for Type */
        0x0031,
    },

    /* 17 : Time Trigger Setting */
    {
        /* UUID Offset */
        36,
        /* First Occurrence for Type */
        0x0017,
        /* Last Occurrence for Type */
        0x0032,
    },

    /* 18 : Number of Digitals */
    {
        /* UUID Offset */
        38,
        /* First Occurrence for Type */
        0x0018,
        /* Last Occurrence for Type */
        0x0021,
    },

    /* 19 : Analog 0 */
    {
        /* UUID Offset */
        40,
        /* First Occurrence for Type */
        0x0023,
        /* Last Occurrence for Type */
        0x002C,
    },

    /* 20 : Valid Range */
    {
        /* UUID Offset */
        42,
        /* First Occurrence for Type */
        0x002A,
        /* Last Occurrence for Type */
        0x0033,
    },

    /* 21 : Aggregate */
    {
        /* UUID Offset */
        44,
        /* First Occurrence for Type */
        0x0035,
        /* Last Occurrence for Type */
        0x0000,
    },

};

static const st_ble_gatts_db_attr_cfg_t gs_gatt_db_attr_table[] =
{
    /* Handle : 0x0000 */
    /* Blank */
    {
        /* Properties */
        0,
        /* Auxiliary Properties */
        BLE_GATT_DB_NO_AUXILIARY_PROPERTY,
        /* Value Size */
        1,
        /* Next Attribute Type Index */
        0x0001,
        /* UUID Offset */
        0,
        /* Value */
        NULL,
    },

    /* Handle : 0x0001 */
    /* GAP Service2 : Primary Service Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY | BLE_GATT_DB_AUTHORIZATION_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x000E,
        /* UUID Offset */
        0,
        /* Value */
        (uint8_t *)(gs_gatt_const_uuid_arr + 8),
    },

    /* Handle : 0x0002 */
    /* Automation IO Service : Included Service Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        6,
        /* Next Attribute Type Index */
        0x000F,
        /* UUID Offset */
        4,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 0),
    },

    /* Handle : 0x0003 */
    /* Device Name : Characteristic Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x0006,
        /* UUID Offset */
        6,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 6),
    },

    /* Handle : 0x0004 */
    /* Device Name */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_NO_AUXILIARY_PROPERTY,
        /* Value Size */
        128,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        10,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 0),
    },

    /* Handle : 0x0005 */
    /* Device Name : Server Characteristic Configuration */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        12,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 130),
    },

    /* Handle : 0x0006 */
    /* Appearance : Characteristic Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x0008,
        /* UUID Offset */
        6,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 11),
    },

    /* Handle : 0x0007 */
    /* Appearance */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        14,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 132),
    },

    /* Handle : 0x0008 */
    /* Peripheral Preferred Connection Parameters : Characteristic Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x000A,
        /* UUID Offset */
        6,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 16),
    },

    /* Handle : 0x0009 */
    /* Peripheral Preferred Connection Parameters */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        8,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        16,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 134),
    },

    /* Handle : 0x000A */
    /* Central Address Resolution : Characteristic Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x000C,
        /* UUID Offset */
        6,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 21),
    },

    /* Handle : 0x000B */
    /* Central Address Resolution */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        1,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        18,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 142),
    },

    /* Handle : 0x000C */
    /* Resolvable Private Address Only : Characteristic Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x0010,
        /* UUID Offset */
        6,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 26),
    },

    /* Handle : 0x000D */
    /* Resolvable Private Address Only */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        1,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        20,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 143),
    },

    /* Handle : 0x000E */
    /* Automation IO Service : Primary Service Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        0,
        /* Value */
        (uint8_t *)(gs_gatt_const_uuid_arr + 22),
    },

    /* Handle : 0x000F */
    /* GAP Service2 : Included Service Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        6,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        4,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 31),
    },

    /* Handle : 0x0010 */
    /* Digital 0 : Characteristic Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x0019,
        /* UUID Offset */
        6,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 37),
    },

    /* Handle : 0x0011 */
    /* Digital 0 */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE | BLE_GATT_DB_WRITE_WITHOUT_RSP,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x001A,
        /* UUID Offset */
        24,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 144),
    },

    /* Handle : 0x0012 */
    /* Digital 0 : Client Characteristic Configuration */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY | BLE_GATT_DB_PEER_SPECIFIC_VAL_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x001B,
        /* UUID Offset */
        26,
        /* Value */
        (uint8_t *)(gs_gatt_db_peer_specific_val_arr + 0),
    },

    /* Handle : 0x0013 */
    /* Digital 0 : Characteristic Presentation Format */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        7,
        /* Next Attribute Type Index */
        0x001C,
        /* UUID Offset */
        28,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 146),
    },

    /* Handle : 0x0014 */
    /* Digital 0 : Characteristic User Description */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        100,
        /* Next Attribute Type Index */
        0x001D,
        /* UUID Offset */
        30,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 153),
    },

    /* Handle : 0x0015 */
    /* Digital 0 : Characteristic Extended Properties */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x001E,
        /* UUID Offset */
        32,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 253),
    },

    /* Handle : 0x0016 */
    /* Digital 0 : Value Trigger Setting */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        3,
        /* Next Attribute Type Index */
        0x001F,
        /* UUID Offset */
        34,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 255),
    },

    /* Handle : 0x0017 */
    /* Digital 0 : Time Trigger Setting */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        4,
        /* Next Attribute Type Index */
        0x0020,
        /* UUID Offset */
        36,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 258),
    },

    /* Handle : 0x0018 */
    /* Digital 0 : Number of Digitals */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        1,
        /* Next Attribute Type Index */
        0x0021,
        /* UUID Offset */
        38,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 262),
    },

    /* Handle : 0x0019 */
    /* Digital 1 : Characteristic Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x0022,
        /* UUID Offset */
        6,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 42),
    },

    /* Handle : 0x001A */
    /* Digital 1 */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE | BLE_GATT_DB_WRITE_WITHOUT_RSP,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        24,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 263),
    },

    /* Handle : 0x001B */
    /* Digital 1 : Client Characteristic Configuration */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY | BLE_GATT_DB_PEER_SPECIFIC_VAL_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0024,
        /* UUID Offset */
        26,
        /* Value */
        (uint8_t *)(gs_gatt_db_peer_specific_val_arr + 2),
    },

    /* Handle : 0x001C */
    /* Digital 1 : Characteristic Presentation Format */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        7,
        /* Next Attribute Type Index */
        0x0025,
        /* UUID Offset */
        28,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 265),
    },

    /* Handle : 0x001D */
    /* Digital 1 : Characteristic User Description */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        100,
        /* Next Attribute Type Index */
        0x0026,
        /* UUID Offset */
        30,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 272),
    },

    /* Handle : 0x001E */
    /* Digital 1 : Characteristic Extended Properties */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0027,
        /* UUID Offset */
        32,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 372),
    },

    /* Handle : 0x001F */
    /* Digital 1 : Value Trigger Setting */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        3,
        /* Next Attribute Type Index */
        0x0028,
        /* UUID Offset */
        34,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 374),
    },

    /* Handle : 0x0020 */
    /* Digital 1 : Time Trigger Setting */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        4,
        /* Next Attribute Type Index */
        0x0029,
        /* UUID Offset */
        36,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 377),
    },

    /* Handle : 0x0021 */
    /* Digital 1 : Number of Digitals */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        1,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        38,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 381),
    },

    /* Handle : 0x0022 */
    /* Analog 0 : Characteristic Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x002B,
        /* UUID Offset */
        6,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 47),
    },

    /* Handle : 0x0023 */
    /* Analog 0 */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE | BLE_GATT_DB_WRITE_WITHOUT_RSP,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x002C,
        /* UUID Offset */
        40,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 382),
    },

    /* Handle : 0x0024 */
    /* Analog 0 : Client Characteristic Configuration */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY | BLE_GATT_DB_PEER_SPECIFIC_VAL_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x002D,
        /* UUID Offset */
        26,
        /* Value */
        (uint8_t *)(gs_gatt_db_peer_specific_val_arr + 4),
    },

    /* Handle : 0x0025 */
    /* Analog 0 : Characteristic Presentation Format */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        7,
        /* Next Attribute Type Index */
        0x002E,
        /* UUID Offset */
        28,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 384),
    },

    /* Handle : 0x0026 */
    /* Analog 0 : Characteristic User Description */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        20,
        /* Next Attribute Type Index */
        0x002F,
        /* UUID Offset */
        30,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 391),
    },

    /* Handle : 0x0027 */
    /* Analog 0 : Characteristic Extended Properties */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0030,
        /* UUID Offset */
        32,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 411),
    },

    /* Handle : 0x0028 */
    /* Analog 0 : Value Trigger Setting */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x0031,
        /* UUID Offset */
        34,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 413),
    },

    /* Handle : 0x0029 */
    /* Analog 0 : Time Trigger Setting */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        4,
        /* Next Attribute Type Index */
        0x0032,
        /* UUID Offset */
        36,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 418),
    },

    /* Handle : 0x002A */
    /* Analog 0 : Valid Range */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        4,
        /* Next Attribute Type Index */
        0x0033,
        /* UUID Offset */
        42,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 422),
    },

    /* Handle : 0x002B */
    /* Analog 1 : Characteristic Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x0034,
        /* UUID Offset */
        6,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 52),
    },

    /* Handle : 0x002C */
    /* Analog 1 */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE | BLE_GATT_DB_WRITE_WITHOUT_RSP,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        40,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 426),
    },

    /* Handle : 0x002D */
    /* Analog 1 : Client Characteristic Configuration */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY | BLE_GATT_DB_PEER_SPECIFIC_VAL_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0036,
        /* UUID Offset */
        26,
        /* Value */
        (uint8_t *)(gs_gatt_db_peer_specific_val_arr + 6),
    },

    /* Handle : 0x002E */
    /* Analog 1 : Characteristic Presentation Format */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        7,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        28,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 428),
    },

    /* Handle : 0x002F */
    /* Analog 1 : Characteristic User Description */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        20,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        30,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 435),
    },

    /* Handle : 0x0030 */
    /* Analog 1 : Characteristic Extended Properties */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        32,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 455),
    },

    /* Handle : 0x0031 */
    /* Analog 1 : Value Trigger Setting */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        34,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 457),
    },

    /* Handle : 0x0032 */
    /* Analog 1 : Time Trigger Setting */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        4,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        36,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 462),
    },

    /* Handle : 0x0033 */
    /* Analog 1 : Valid Range */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        4,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        42,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 466),
    },

    /* Handle : 0x0034 */
    /* Aggregate : Characteristic Declaration */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        5,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        6,
        /* Value */
        (uint8_t *)(gs_gatt_const_value_arr + 57),
    },

    /* Handle : 0x0035 */
    /* Aggregate */
    {
        /* Properties */
        BLE_GATT_DB_READ,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY,
        /* Value Size */
        8,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        44,
        /* Value */
        (uint8_t *)(gs_gatt_value_arr + 470),
    },

    /* Handle : 0x0036 */
    /* Aggregate : Client Characteristic Configuration */
    {
        /* Properties */
        BLE_GATT_DB_READ | BLE_GATT_DB_WRITE,
        /* Auxiliary Properties */
        BLE_GATT_DB_FIXED_LENGTH_PROPERTY | BLE_GATT_DB_PEER_SPECIFIC_VAL_PROPERTY,
        /* Value Size */
        2,
        /* Next Attribute Type Index */
        0x0000,
        /* UUID Offset */
        26,
        /* Value */
        (uint8_t *)(gs_gatt_db_peer_specific_val_arr + 8),
    },

};

static const st_ble_gatts_db_char_cfg_t gs_gatt_characteristic[] =
{
    /* 0 : Device Name */
    {
        /* Number of Attributes */
        {
            3,
        },
        /* Start Handle */
        0x0003,
        /* Service Index */
        0,
    },

    /* 1 : Appearance */
    {
        /* Number of Attributes */
        {
            2,
        },
        /* Start Handle */
        0x0006,
        /* Service Index */
        0,
    },

    /* 2 : Peripheral Preferred Connection Parameters */
    {
        /* Number of Attributes */
        {
            2,
        },
        /* Start Handle */
        0x0008,
        /* Service Index */
        0,
    },

    /* 3 : Central Address Resolution */
    {
        /* Number of Attributes */
        {
            2,
        },
        /* Start Handle */
        0x000A,
        /* Service Index */
        0,
    },

    /* 4 : Resolvable Private Address Only */
    {
        /* Number of Attributes */
        {
            2,
        },
        /* Start Handle */
        0x000C,
        /* Service Index */
        0,
    },

    /* 5 : Digital 0 */
    {
        /* Number of Attributes */
        {
            9,
        },
        /* Start Handle */
        0x0010,
        /* Service Index */
        1,
    },

    /* 6 : Digital 1 */
    {
        /* Number of Attributes */
        {
            9,
        },
        /* Start Handle */
        0x0019,
        /* Service Index */
        1,
    },

    /* 7 : Analog 0 */
    {
        /* Number of Attributes */
        {
            9,
        },
        /* Start Handle */
        0x0022,
        /* Service Index */
        1,
    },

    /* 8 : Analog 1 */
    {
        /* Number of Attributes */
        {
            9,
        },
        /* Start Handle */
        0x002B,
        /* Service Index */
        1,
    },

    /* 9 : Aggregate */
    {
        /* Number of Attributes */
        {
            3,
        },
        /* Start Handle */
        0x0034,
        /* Service Index */
        1,
    },

};

static const st_ble_gatts_db_serv_cfg_t gs_gatt_service[] =
{
    /* GAP Service2 */
    {
        /* Num of Services */
        {
            1,
        },
        /* Description */
        0,
        /* Service Start Handle */
        0x0001,
        /* Service End Handle */
        0x000D,
        /* Characteristic Start Index */
        0,
        /* Characteristic End Index */
        4,
    },

    /* Automation IO Service */
    {
        /* Num of Services */
        {
            1,
        },
        /* Description */
        0,
        /* Service Start Handle */
        0x000E,
        /* Service End Handle */
        0x0036,
        /* Characteristic Start Index */
        5,
        /* Characteristic End Index */
        9,
    },

};

st_ble_gatts_db_cfg_t g_gatt_db_table =
{
    gs_gatt_const_uuid_arr,
    gs_gatt_value_arr,
    gs_gatt_const_value_arr,
    gs_gatt_db_peer_specific_val_arr,
    gs_gatt_db_const_peer_specific_val_arr,
    gs_gatt_type_table,
    gs_gatt_db_attr_table,
    gs_gatt_characteristic,
    gs_gatt_service,
    ARRAY_SIZE(gs_gatt_service),
    ARRAY_SIZE(gs_gatt_characteristic),
    ARRAY_SIZE(gs_gatt_type_table),
    ARRAY_SIZE(gs_gatt_db_const_peer_specific_val_arr),
};
