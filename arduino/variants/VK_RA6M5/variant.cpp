#include "Arduino.h"
#include "bsp_pin_cfg.h"
#include "board_cfg.h"

extern "C" void SystemInit(void);

void initVariant() {
    SystemInit();
    bsp_init(NULL);
}
