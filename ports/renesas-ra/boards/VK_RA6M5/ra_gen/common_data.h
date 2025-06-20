/* generated common header file - do not edit */
#ifndef COMMON_DATA_H_
#define COMMON_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "r_sce.h"
#include "r_icu.h"
#include "r_external_irq_api.h"
#include "r_ioport.h"
#include "bsp_pin_cfg.h"
FSP_HEADER
extern sce_instance_ctrl_t sce_ctrl;
extern const sce_cfg_t sce_cfg;
/** External IRQ on ICU Instance. */
extern const external_irq_instance_t g_external_irq14;

/** Access the ICU instance using these structures when calling API functions directly (::p_api is not used). */
extern icu_instance_ctrl_t g_external_irq14_ctrl;
extern const external_irq_cfg_t g_external_irq14_cfg;

#ifndef callback_icu
void callback_icu(external_irq_callback_args_t *p_args);
#endif
/** External IRQ on ICU Instance. */
extern const external_irq_instance_t g_external_irq13;

/** Access the ICU instance using these structures when calling API functions directly (::p_api is not used). */
extern icu_instance_ctrl_t g_external_irq13_ctrl;
extern const external_irq_cfg_t g_external_irq13_cfg;

#ifndef callback_icu
void callback_icu(external_irq_callback_args_t *p_args);
#endif
/** External IRQ on ICU Instance. */
extern const external_irq_instance_t g_external_irq7;

/** Access the ICU instance using these structures when calling API functions directly (::p_api is not used). */
extern icu_instance_ctrl_t g_external_irq7_ctrl;
extern const external_irq_cfg_t g_external_irq7_cfg;

#ifndef callback_icu
void callback_icu(external_irq_callback_args_t *p_args);
#endif
//IRQ 5
/** External IRQ on ICU Instance. */
extern const external_irq_instance_t g_external_irq5;

/** Access the ICU instance using these structures when calling API functions directly (::p_api is not used). */
extern icu_instance_ctrl_t g_external_irq5_ctrl;
extern const external_irq_cfg_t g_external_irq5_cfg;

#ifndef callback_icu
void callback_icu(external_irq_callback_args_t *p_args);
#endif
//IRQ 9
/** External IRQ on ICU Instance. */
extern const external_irq_instance_t g_external_irq9;

/** Access the ICU instance using these structures when calling API functions directly (::p_api is not used). */
extern icu_instance_ctrl_t g_external_irq9_ctrl;
extern const external_irq_cfg_t g_external_irq9_cfg;

#ifndef callback_icu
void callback_icu(external_irq_callback_args_t *p_args);
#endif

//IRQ 10
/** External IRQ on ICU Instance. */
extern const external_irq_instance_t g_external_irq10;

/** Access the ICU instance using these structures when calling API functions directly (::p_api is not used). */
extern icu_instance_ctrl_t g_external_irq10_ctrl;
extern const external_irq_cfg_t g_external_irq10_cfg;

#ifndef callback_icu
void callback_icu(external_irq_callback_args_t *p_args);
#endif

//IRQ 12
/** External IRQ on ICU Instance. */
extern const external_irq_instance_t g_external_irq12;

/** Access the ICU instance using these structures when calling API functions directly (::p_api is not used). */
extern icu_instance_ctrl_t g_external_irq12_ctrl;
extern const external_irq_cfg_t g_external_irq12_cfg;

#ifndef callback_icu
void callback_icu(external_irq_callback_args_t *p_args);
#endif

/* IOPORT Instance */
extern const ioport_instance_t g_ioport;

/* IOPORT control structure. */
extern ioport_instance_ctrl_t g_ioport_ctrl;
void g_common_init(void);
FSP_FOOTER
#endif /* COMMON_DATA_H_ */
