/* SCI Simple IIC master driver for Renesas RA MCUs. */

#ifndef PORTS_RENESAS_RA_RA_SCI_I2C_H_
#define PORTS_RENESAS_RA_RA_SCI_I2C_H_

#include <stdbool.h>
#include <stdint.h>

#define RA_SCI_I2C_OK             (0)
#define RA_SCI_I2C_NACK           (-1)
#define RA_SCI_I2C_TIMEOUT        (-2)
#define RA_SCI_I2C_ERR            (-3)
#define RA_SCI_I2C_DEF_TIMEOUT    (1000U)
#define RA_SCI_I2C_MAX_FREQ       (400000U)

bool ra_sci_i2c_find_pins(uint32_t sda_pin, uint32_t scl_pin, uint32_t *ch);
bool ra_sci_i2c_init(uint32_t ch, uint32_t sda_pin, uint32_t scl_pin, uint32_t freq);
void ra_sci_i2c_deinit(uint32_t ch);
int ra_sci_i2c_write(uint32_t ch, uint16_t addr, const uint8_t *src,
    uint32_t len, bool stop, uint32_t timeout_ms);
int ra_sci_i2c_read(uint32_t ch, uint16_t addr, uint8_t *dest,
    uint32_t len, bool stop, uint32_t timeout_ms);

#endif /* PORTS_RENESAS_RA_RA_SCI_I2C_H_ */
