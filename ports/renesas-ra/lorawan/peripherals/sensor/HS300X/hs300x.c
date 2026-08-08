/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/

#include "board.h"

#if defined(RM_HS300X_H)

static volatile bool g_hs300x_processing = false;


void hs300x_callback(rm_hs300x_callback_args_t * p_args)
{
    g_hs300x_processing = (RM_HS300X_EVENT_SUCCESS != p_args->event);
}


void app_hs300x_init(void)
{
    i2c_master_instance_t * p_driver_instance = (i2c_master_instance_t *) g_comms_i2c_bus0_extended_cfg.p_driver_instance;
    p_driver_instance->p_api->open(p_driver_instance->p_ctrl, p_driver_instance->p_cfg);
    RM_HS300X_Open(g_hs300x_sensor0.p_ctrl, g_hs300x_sensor0.p_cfg);
}


bool app_hs300x_start_measure(void)
{
    g_hs300x_processing = true;
    if (FSP_SUCCESS != RM_HS300X_MeasurementStart(g_hs300x_sensor0.p_ctrl)) {
        return ( false );
    }
    while ( g_hs300x_processing ) { };

    return ( true );
}


app_hs300x_result_t app_hs300x_get_result(void)
{
    app_hs300x_result_t result = {0}; // result.isValid=false(0)

    rm_hs300x_raw_data_t raw_data;
    rm_hs300x_data_t data;

    g_hs300x_processing = true;
    if (FSP_SUCCESS != RM_HS300X_Read(g_hs300x_sensor0.p_ctrl, &raw_data)) {
        return ( result );
    }
    while ( g_hs300x_processing ) { };

    if (FSP_SUCCESS != RM_HS300X_DataCalculate(g_hs300x_sensor0.p_ctrl, &raw_data, &data)) {
        return ( result );
    }

    result.hum = (uint16_t)(data.humidity.integer_part * 10 + data.humidity.decimal_part / 10);//[0.1%]
    result.temp = (uint16_t)(data.temperature.integer_part * 10 + data.temperature.decimal_part / 10);//[0.1deg]
    result.stat = 0U; // unused
    result.isValid = true;

    return ( result );
}

#endif /* RM_HS300X_H */
