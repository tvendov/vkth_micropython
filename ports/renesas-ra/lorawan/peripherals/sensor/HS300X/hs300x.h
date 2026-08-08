/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef APP_HS300X_H
#define APP_HS300X_H

/*
HS300X
*/
#define HS300X_RESPONSE_TIME    (4000)

typedef struct {
    int16_t hum;
    int16_t temp;
    uint8_t stat;
    bool isValid;
} app_hs300x_result_t;

void app_hs300x_init(void);
bool app_hs300x_start_measure(void);
app_hs300x_result_t app_hs300x_get_result(void);

#endif // APP_HS300X_H
