/* generated HAL header file - do not edit */
#ifndef HAL_DATA_H_
#define HAL_DATA_H_
#include <stdint.h>
#include "common_data.h"
#include "r_lpm.h"
#include "r_lpm_api.h"
#include "r_flash_lp.h"
#include "r_flash_api.h"
#if MICROPY_HW_ENABLE_BLE
#include "r_agt.h"
#include "r_timer_api.h"
#endif
FSP_HEADER

/** lpm Instance */
extern const lpm_instance_t g_lpm0;

/** Access the LPM instance using these structures when calling API functions directly (::p_api is not used). */
extern lpm_instance_ctrl_t g_lpm0_ctrl;
extern const lpm_cfg_t g_lpm0_cfg;

/* Flash on Flash LP Instance. */
extern const flash_instance_t g_flash0;

/** Access the Flash LP instance using these structures when calling API functions directly (::p_api is not used). */
extern flash_lp_instance_ctrl_t g_flash0_ctrl;
extern const flash_cfg_t g_flash0_cfg;

#ifndef NULL
void NULL(flash_callback_args_t *p_args);
#endif

#if MICROPY_HW_ENABLE_BLE
/* AGT Timer for BLE Abstraction Layer */
extern const timer_instance_t g_timer_ble;
extern agt_instance_ctrl_t g_timer_ble_ctrl;
extern const timer_cfg_t g_timer_ble_cfg;
extern const agt_extended_cfg_t g_timer_ble_extend;
#endif

FSP_FOOTER
#endif /* HAL_DATA_H_ */
