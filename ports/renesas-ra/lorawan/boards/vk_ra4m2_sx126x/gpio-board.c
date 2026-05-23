/*!
 * \file      gpio-board.c
 * \brief     VK_RA4M2 GPIO board driver — 1:1 MicroPython wrapper.
 *
 * Vendor GpioMcu* surface preserved; internals dispatch to public `machine.Pin`
 * objects bound to `Gpio_t::mp_pin_obj` by `mod_lorawan.c` after parsing the
 * `LoRaWAN(spi_bus=..., cs=..., irq=..., rst=..., gpio_busy=...)` kwargs.
 *
 * No direct FSP R_IOPORT_* access; no enum-keyed lookup map. The Python Pin
 * object lives directly inside the vendor Gpio_t slot (`mp_pin_obj` field
 * added in lorawan/system/gpio.h for the renesas MP wrapper variant).
 *
 * (C) Semtech 2013-2017, Renesas 2022 (LICENSE_RENESAS.TXT).
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "extmod/modmachine.h"

#include "board.h"
#include "gpio-board.h"

void GpioMcuInit( Gpio_t *obj, PinNames pin, PinModes mode, PinConfigs config, PinTypes type, uint32_t value )
{
    (void)mode; (void)config; (void)type; (void)value;
    if ( NULL == obj ) {
        return;
    }
    /* Python owns hardware config; we only keep obj->pin for ICU IRQ lookup in
     * sx126x-board.c. obj->mp_pin_obj is bound by mod_lorawan.c ctor. */
    obj->pin = pin;
}

void GpioMcuWrite( Gpio_t *obj, uint32_t value )
{
    if ( NULL == obj || NULL == obj->mp_pin_obj ) {
        return;
    }
    mp_obj_t dest[3];
    mp_load_method( (mp_obj_t)obj->mp_pin_obj, MP_QSTR_value, dest );
    dest[2] = mp_obj_new_int( ( 0U == value ) ? 0 : 1 );
    mp_call_method_n_kw( 1, 0, dest );
}

uint32_t GpioMcuRead( Gpio_t *obj )
{
    if ( NULL == obj || NULL == obj->mp_pin_obj ) {
        return 0;
    }
    mp_obj_t dest[2];
    mp_load_method( (mp_obj_t)obj->mp_pin_obj, MP_QSTR_value, dest );
    mp_obj_t result = mp_call_method_n_kw( 0, 0, dest );
    return ( mp_obj_get_int( result ) != 0 ) ? 1u : 0u;
}

void GpioMcuToggle( Gpio_t *obj )
{
    if ( NULL == obj || NULL == obj->mp_pin_obj ) {
        return;
    }
    GpioMcuWrite( obj, GpioMcuRead( obj ) ? 0u : 1u );
}
