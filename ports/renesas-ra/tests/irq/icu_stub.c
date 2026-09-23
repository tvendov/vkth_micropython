#include <stdbool.h>
#include <stdint.h>

// Match the production boundary: the mapper is a separate translation unit.
bool ra_icu_find_irq_no(uint32_t pin, uint8_t *line) {
    *line = pin % 16;
    return true;
}
