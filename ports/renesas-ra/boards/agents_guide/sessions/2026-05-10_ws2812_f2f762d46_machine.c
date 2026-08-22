/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026
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

#include <stdio.h>
#include <string.h>

#include "py/runtime.h"
#include "py/misc.h"
#include "py/mphal.h"
#include "pin.h"
#include "modmachine.h"
#include "ra/ra_sci_ws2812.h"

#if defined(MICROPY_HW_WS2812_DATA) && defined(MICROPY_HW_WS2812_SCI_CH)

#define MACHINE_WS2812_DEFAULT_BAUDRATE (2400000)
#define MACHINE_WS2812_DEFAULT_LATCH_US (80)
#define MACHINE_WS2812_MAX_BPP (4)

typedef struct _machine_ws2812_obj_t {
    mp_obj_base_t base;
    bool active;
    uint8_t ch;
    uint8_t bpp;
    size_t n;
    size_t buf_len;
    size_t tx_len;
    uint32_t baudrate;
    uint32_t latch_us;
    mp_hal_pin_obj_t pin;
    uint8_t *buf;
    uint8_t *txbuf;
} machine_ws2812_obj_t;

static const uint8_t ws2812_order[MACHINE_WS2812_MAX_BPP] = {1, 0, 2, 3};

static machine_ws2812_obj_t machine_ws2812_obj = {
    {&machine_ws2812_type},
    false,
    MICROPY_HW_WS2812_SCI_CH,
    3,
    0,
    0,
    0,
    MACHINE_WS2812_DEFAULT_BAUDRATE,
    MACHINE_WS2812_DEFAULT_LATCH_US,
    MICROPY_HW_WS2812_DATA,
    NULL,
    NULL,
};

static size_t machine_ws2812_normalize_index(machine_ws2812_obj_t *self, mp_obj_t index_in) {
    mp_int_t index = mp_obj_get_int(index_in);
    if (index < 0) {
        index += self->n;
    }
    if (index < 0 || (size_t)index >= self->n) {
        mp_raise_msg(&mp_type_IndexError, MP_ERROR_TEXT("index out of range"));
    }
    return (size_t)index;
}

static void machine_ws2812_set_pixel(machine_ws2812_obj_t *self, size_t index, mp_obj_t value_in) {
    mp_obj_t *items;
    mp_obj_get_array_fixed_n(value_in, self->bpp, &items);

    size_t offset = index * self->bpp;
    for (size_t i = 0; i < self->bpp; ++i) {
        mp_int_t value = mp_obj_get_int(items[i]);
        if (value < 0 || value > 255) {
            mp_raise_ValueError(MP_ERROR_TEXT("color out of range"));
        }
        self->buf[offset + ws2812_order[i]] = (uint8_t)value;
    }
}

static mp_obj_t machine_ws2812_get_pixel(machine_ws2812_obj_t *self, size_t index) {
    mp_obj_t items[MACHINE_WS2812_MAX_BPP];
    size_t offset = index * self->bpp;
    for (size_t i = 0; i < self->bpp; ++i) {
        items[i] = MP_OBJ_NEW_SMALL_INT(self->buf[offset + ws2812_order[i]]);
    }
    return mp_obj_new_tuple(self->bpp, items);
}

static void machine_ws2812_encode_byte(uint8_t value, uint8_t *dst) {
    uint32_t encoded = 0;
    for (size_t i = 0; i < 8; ++i) {
        encoded <<= 3;
        encoded |= (value & 0x80) ? 0x6 : 0x4;
        value <<= 1;
    }
    dst[0] = (uint8_t)(encoded >> 16);
    dst[1] = (uint8_t)(encoded >> 8);
    dst[2] = (uint8_t)encoded;
}

static void machine_ws2812_prepare_tx(machine_ws2812_obj_t *self) {
    for (size_t i = 0; i < self->buf_len; ++i) {
        machine_ws2812_encode_byte(self->buf[i], &self->txbuf[i * 3]);
    }
}

static void machine_ws2812_resize_buffers(machine_ws2812_obj_t *self, size_t buf_len) {
    size_t tx_len = buf_len * 3;

    if (self->buf == NULL) {
        self->buf = m_new(uint8_t, buf_len);
    } else {
        self->buf = m_renew(uint8_t, self->buf, self->buf_len, buf_len);
    }

    if (self->txbuf == NULL) {
        self->txbuf = m_new(uint8_t, tx_len);
    } else {
        self->txbuf = m_renew(uint8_t, self->txbuf, self->tx_len, tx_len);
    }

    self->buf_len = buf_len;
    self->tx_len = tx_len;
    memset(self->buf, 0, self->buf_len);
    memset(self->txbuf, 0, self->tx_len);
}

static void machine_ws2812_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_ws2812_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "WS2812(n=%u, bpp=%u, baudrate=%u, pin=%q, active=%u)",
        (unsigned int)self->n, self->bpp, (unsigned int)self->baudrate, self->pin->name, self->active);
}

static mp_obj_t machine_ws2812_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    (void)type;

    enum { ARG_pixel_count, ARG_pin, ARG_channels, ARG_baudrate, ARG_latch_us };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_pixel_count, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_pin,         MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_channels,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 3} },
        { MP_QSTR_baudrate,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = MACHINE_WS2812_DEFAULT_BAUDRATE} },
        { MP_QSTR_latch_us,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = MACHINE_WS2812_DEFAULT_LATCH_US} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_pixel_count].u_int <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad n"));
    }
    if (args[ARG_channels].u_int != 3 && args[ARG_channels].u_int != 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad bpp"));
    }
    if (args[ARG_baudrate].u_int <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad baudrate"));
    }
    if (args[ARG_latch_us].u_int < 50) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad latch_us"));
    }

    machine_ws2812_obj_t *self = &machine_ws2812_obj;
    const machine_pin_obj_t *pin = MICROPY_HW_WS2812_DATA;
    if (args[ARG_pin].u_obj != MP_OBJ_NULL) {
        pin = machine_pin_find(args[ARG_pin].u_obj);
    }

    uint8_t ch = 0xff;
    if (!ra_sci_ws2812_find_ch(pin->pin, &ch)) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad WS2812 pin"));
    }

    size_t n = (size_t)args[ARG_pixel_count].u_int;
    size_t bpp = (size_t)args[ARG_channels].u_int;
    if (n > SIZE_MAX / bpp) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too large"));
    }

    if (self->active) {
        ra_sci_ws2812_deinit(self->ch, self->pin->pin);
        self->active = false;
    }

    self->pin = pin;
    self->ch = ch;
    self->n = n;
    self->bpp = (uint8_t)bpp;
    self->baudrate = (uint32_t)args[ARG_baudrate].u_int;
    self->latch_us = (uint32_t)args[ARG_latch_us].u_int;
    machine_ws2812_resize_buffers(self, n * bpp);

    if (!ra_sci_ws2812_init(self->ch, self->pin->pin, self->baudrate)) {
        mp_raise_ValueError(MP_ERROR_TEXT("WS2812 bus busy"));
    }

    self->active = true;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t machine_ws2812_unary_op(mp_unary_op_t op, mp_obj_t self_in) {
    machine_ws2812_obj_t *self = MP_OBJ_TO_PTR(self_in);
    switch (op) {
        case MP_UNARY_OP_LEN:
            return MP_OBJ_NEW_SMALL_INT(self->n);
        default:
            return MP_OBJ_NULL;
    }
}

static mp_obj_t machine_ws2812_subscr(mp_obj_t self_in, mp_obj_t index_in, mp_obj_t value_in) {
    machine_ws2812_obj_t *self = MP_OBJ_TO_PTR(self_in);
    size_t index = machine_ws2812_normalize_index(self, index_in);
    if (value_in == MP_OBJ_NULL) {
        return MP_OBJ_NULL;
    } else if (value_in == MP_OBJ_SENTINEL) {
        return machine_ws2812_get_pixel(self, index);
    } else {
        machine_ws2812_set_pixel(self, index, value_in);
        return mp_const_none;
    }
}

static mp_obj_t machine_ws2812_write(mp_obj_t self_in) {
    machine_ws2812_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) {
        mp_raise_ValueError(MP_ERROR_TEXT("WS2812 inactive"));
    }
    machine_ws2812_prepare_tx(self);
    ra_sci_ws2812_write(self->ch, self->pin->pin, self->txbuf, self->tx_len, self->latch_us);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_ws2812_write_obj, machine_ws2812_write);

static mp_obj_t machine_ws2812_fill(mp_obj_t self_in, mp_obj_t value_in) {
    machine_ws2812_obj_t *self = MP_OBJ_TO_PTR(self_in);
    for (size_t i = 0; i < self->n; ++i) {
        machine_ws2812_set_pixel(self, i, value_in);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_ws2812_fill_obj, machine_ws2812_fill);

static mp_obj_t machine_ws2812_deinit(mp_obj_t self_in) {
    machine_ws2812_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->active) {
        ra_sci_ws2812_deinit(self->ch, self->pin->pin);
        self->active = false;
    }
    if (self->buf != NULL) {
        memset(self->buf, 0, self->buf_len);
    }
    if (self->txbuf != NULL) {
        memset(self->txbuf, 0, self->tx_len);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_ws2812_deinit_obj, machine_ws2812_deinit);

static const mp_rom_map_elem_t machine_ws2812_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&machine_ws2812_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&machine_ws2812_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_ws2812_deinit_obj) },
};

static MP_DEFINE_CONST_DICT(machine_ws2812_locals_dict, machine_ws2812_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_ws2812_type,
    MP_QSTR_WS2812,
    MP_TYPE_FLAG_NONE,
    make_new, machine_ws2812_make_new,
    print, machine_ws2812_print,
    unary_op, machine_ws2812_unary_op,
    subscr, machine_ws2812_subscr,
    locals_dict, &machine_ws2812_locals_dict
    );

#endif
