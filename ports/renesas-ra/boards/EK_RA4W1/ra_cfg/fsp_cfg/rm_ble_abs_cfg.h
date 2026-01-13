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

#ifndef RM_BLE_ABS_CFG_H
#define RM_BLE_ABS_CFG_H

/***********************************************************************************************************************
 * BLE Abstraction Layer Configuration
 **********************************************************************************************************************/

/* BLE ABS Parameter Check
 * 0: Disable parameter checking
 * 1: Enable parameter checking
 */
#define RM_BLE_ABS_CFG_PARAM_CHECKING_ENABLE    (BSP_CFG_PARAM_CHECKING_ENABLE)

/* BLE ABS Event Callback
 * 0: Disable event callback
 * 1: Enable event callback
 */
#define RM_BLE_ABS_CFG_EVENT_CALLBACK_ENABLE    (1)

/* BLE ABS Bonding Configuration */
#define BLE_ABS_CFG_NUMBER_BONDING              (1)     // Max bonded devices

/* BLE ABS Timer Configuration */
#define BLE_ABS_CFG_TIMER_NUMBER_OF_SLOT        (4)     // Number of timer slots

#endif // RM_BLE_ABS_CFG_H

