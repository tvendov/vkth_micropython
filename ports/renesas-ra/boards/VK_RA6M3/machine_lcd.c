/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Vekatech Ltd.
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

#include "py/mpconfig.h"
#include <string.h>
#include "py/runtime.h"
#include "py/mphal.h"
#include "modmachine.h"
#include "hal_data.h"
#if defined(USE_FSP_DRW)
#include "dave2d_port.h"
#endif
#if defined(MICROPY_PY_LVGL) && (MICROPY_PY_LVGL == 1)
#include "lvgl/lvgl.h"
#endif

#if MODULE_LCD_ENABLED

#define FT5X06_DOWN          0
#define FT5X06_UP            1
#define FT5X06_CONTACT       2
#define FT5X06_NUM_POINTS    5
#define FT5X06_REG_TD_STATUS 0x02
#define FT5X06_I2C_TIMEOUT_MS 5U

#define extract_e(t) ((uint8_t)((t).event))
#define extract_x(t) ((int16_t)(((t).x_msb << 8) | ((t).x_lsb)))
#define extract_y(t) ((int16_t)(((t).y_msb << 8) | ((t).y_lsb)))

typedef enum
{
    TOUCH_EVENT_NONE,
    TOUCH_EVENT_DOWN,
    TOUCH_EVENT_HOLD,
    TOUCH_EVENT_MOVE,
    TOUCH_EVENT_UP
} touch_event_t;

typedef struct st_touch_coord
{
    uint16_t x;
    uint16_t y;
    touch_event_t event;
} touch_coord_t;

typedef struct st_ft5x06_touch
{
    uint8_t x_msb : 4;
    uint8_t       : 2;
    uint8_t event : 2;
    uint8_t x_lsb;

    uint8_t y_msb : 4;
    uint8_t id    : 4;
    uint8_t y_lsb : 8;

    uint8_t res1;
    uint8_t res2;
} ft5x06_touch_t;

/* Complete FT5X06 data payload (number of active points + all five touch points) */

typedef struct st_touch_data
{
    uint8_t num_stamp_points;
    touch_coord_t stamp_point[FT5X06_NUM_POINTS];
} touch_data_t;

typedef struct st_ft5x06_payload
{
    uint8_t num_points_active;
    ft5x06_touch_t data_raw[FT5X06_NUM_POINTS];
} ft5x06_payload_t;

typedef struct _machine_lcd_obj_t {
    mp_obj_base_t base;
    uint8_t idx;                // currently used buffer;
    uint8_t isinited;
    uint8_t isstarted;
    uint8_t ischanged;
    mp_hal_pin_obj_t bl;  // backlight ctr (ON OFF)
    mp_hal_pin_obj_t en;  // enable ctr (ON OFF)
    mp_hal_pin_obj_t tch; // touch

    uint8_t num_points;
    touch_coord_t point[FT5X06_NUM_POINTS];
} machine_lcd_obj_t;

static volatile uint8_t I2C_complete = 0;
static volatile i2c_master_event_t s_touch_i2c_event = I2C_MASTER_EVENT_ABORTED;
static volatile uint8_t s_touch_read_pending = 0;
static volatile uint8_t s_lcd_touch_active = 0;
static int16_t s_lcd_touch_x = 0;
static int16_t s_lcd_touch_y = 0;
static volatile uint32_t s_touch_irq_count = 0;
static volatile uint32_t s_touch_i2c_errors = 0;
static volatile uint32_t s_touch_i2c_timeouts = 0;

touch_data_t locked;

STATIC machine_lcd_obj_t machine_lcd_obj = {{&machine_lcd_type}, 0, 0, 0, 0, pin_P801, pin_P806, pin_P800, 0};

STATIC fsp_err_t lcd_glcdc_stop(void) {
    fsp_err_t err = FSP_SUCCESS;

    for (size_t retry = 0; retry < 50; retry++) {
        err = R_GLCDC_Stop(&g_display0_ctrl);
        if (err != FSP_ERR_INVALID_UPDATE_TIMING) {
            break;
        }
        mp_hal_delay_ms(1);
    }

    return err;
}

STATIC fsp_err_t lcd_glcdc_close(void) {
    fsp_err_t err = FSP_SUCCESS;

    for (size_t retry = 0; retry < 50; retry++) {
        err = R_GLCDC_Close(&g_display0_ctrl);
        if (err != FSP_ERR_INVALID_UPDATE_TIMING) {
            break;
        }
        mp_hal_delay_ms(1);
    }

    return err;
}

void machine_lcd_soft_reset(void) {
    machine_lcd_obj_t *self = &machine_lcd_obj;

    if (!self->isinited) {
        return;
    }

    R_ICU_ExternalIrqDisable(&g_external_irq11_ctrl);
    R_ICU_ExternalIrqClose(&g_external_irq11_ctrl);
    R_SCI_I2C_Close(&g_i2c_touch_ctrl);

    if (self->isstarted && FSP_SUCCESS == lcd_glcdc_stop()) {
        self->isstarted = 0;
    }

    if (FSP_SUCCESS == lcd_glcdc_close()) {
        self->isinited = 0;
        self->isstarted = 0;
    }

    ra_gpio_write(self->bl->pin, 0);
    ra_gpio_write(self->en->pin, 0);
    ra_gpio_write(self->tch->pin, 0);
    self->num_points = 0;
    I2C_complete = 0;
    s_touch_i2c_event = I2C_MASTER_EVENT_ABORTED;
    s_touch_read_pending = 0;
    s_lcd_touch_active = 0;
    memset(&locked, 0, sizeof(locked));
}

static uint8_t s_lcd_lvgl_bridged = 0;   // C LVGL display/indev bridge installed?

void machine_lcd_lvgl_soft_reset(void) {
    #if defined(MICROPY_PY_LVGL) && (MICROPY_PY_LVGL == 1)
    if (lv_is_initialized()) {
        s_lcd_lvgl_bridged = 0;          // lv_deinit destroys the C display/indev
        #if defined(USE_FSP_DRW)
        vk_ra6m3_dave2d_prepare_deinit();
        #endif
        lv_deinit();
        #if defined(USE_FSP_DRW)
        vk_ra6m3_dave2d_finish_deinit();
        #endif
    }
    #endif
}

void touch_i2c_callback(i2c_master_callback_args_t *p_args) {
    s_touch_i2c_event = p_args->event;
    I2C_complete = 1;
}

static bool touch_i2c_wait(i2c_master_event_t expected_event) {
    uint32_t start_ms = (uint32_t)mp_hal_ticks_ms();
    while (!I2C_complete) {
        if ((uint32_t)((uint32_t)mp_hal_ticks_ms() - start_ms) >= FT5X06_I2C_TIMEOUT_MS) {
            s_touch_i2c_timeouts++;
            (void)R_SCI_I2C_Abort(&g_i2c_touch_ctrl);
            return false;
        }
    }

    if (s_touch_i2c_event != expected_event) {
        s_touch_i2c_errors++;
        return false;
    }
    return true;
}

static bool ft5x06_payload_get(void) {
    machine_lcd_obj_t *self = &machine_lcd_obj;
    touch_coord_t new_touch;
    ft5x06_payload_t touch_payload;

    /* Clear payload struct */
    memset(&touch_payload, 0, sizeof(ft5x06_payload_t));

    /* Read the data about the touch point(s) */
    uint8_t reg = FT5X06_REG_TD_STATUS;

    /* Write TD_STATUS address */
    I2C_complete = 0;
    s_touch_i2c_event = I2C_MASTER_EVENT_ABORTED;
    if (FSP_SUCCESS != R_SCI_I2C_Write(&g_i2c_touch_ctrl, &reg, 1, true)) {
        s_touch_i2c_errors++;
        (void)R_SCI_I2C_Abort(&g_i2c_touch_ctrl);
        return false;
    }
    if (!touch_i2c_wait(I2C_MASTER_EVENT_TX_COMPLETE)) {
        return false;
    }

    /* Read TD_STATUS through all five TOUCHn_** register sets */
    I2C_complete = 0;
    s_touch_i2c_event = I2C_MASTER_EVENT_ABORTED;
    if (FSP_SUCCESS != R_SCI_I2C_Read(&g_i2c_touch_ctrl, (uint8_t *)&touch_payload,
        sizeof(ft5x06_payload_t), false)) {
        s_touch_i2c_errors++;
        (void)R_SCI_I2C_Abort(&g_i2c_touch_ctrl);
        return false;
    }
    if (!touch_i2c_wait(I2C_MASTER_EVENT_RX_COMPLETE)) {
        return false;
    }

    uint8_t num_points = touch_payload.num_points_active & 0x0fU;
    if (num_points > FT5X06_NUM_POINTS) {
        num_points = FT5X06_NUM_POINTS;
    }
    bool any_active = false;
    int16_t release_x = s_lcd_touch_x;
    int16_t release_y = s_lcd_touch_y;

    if (num_points) {
        /* Process the raw data for the touch point(s) into useful data */
        for (uint8_t i = 0; i < num_points; i++)
        {
            new_touch.x = (uint16_t)extract_x(touch_payload.data_raw[i]);
            new_touch.y = (uint16_t)extract_y(touch_payload.data_raw[i]);
            new_touch.event = extract_e(touch_payload.data_raw[i]);
            bool point_active = false;

            /* Set event type based on received data */
            switch (new_touch.event)
            {
                case FT5X06_DOWN:
                    self->point[i].event = TOUCH_EVENT_DOWN;
                    point_active = true;
                    break;
                case FT5X06_UP:
                    self->point[i].event = TOUCH_EVENT_UP;
                    break;
                case FT5X06_CONTACT:
                    /* Check if the point is moving or not */
                    if ((self->point[i].x != new_touch.x) || (self->point[i].y != new_touch.y)) {
                        self->point[i].event = TOUCH_EVENT_MOVE;
                    } else {
                        self->point[i].event = TOUCH_EVENT_HOLD;
                    }
                    point_active = true;
                    break;
                default:
                    self->point[i].event = TOUCH_EVENT_NONE;
                    break;
            }

            /* Set new coordinates */
            self->point[i].x = new_touch.x;
            self->point[i].y = new_touch.y;
            release_x = (int16_t)new_touch.x;
            release_y = (int16_t)new_touch.y;
            if (point_active) {
                any_active = true;
                s_lcd_touch_x = (int16_t)new_touch.x;
                s_lcd_touch_y = (int16_t)new_touch.y;
            }
        }
    }

    /* Preserve the last release position, and publish every packet (including UP/zero). */
    if (!any_active && num_points) {
        s_lcd_touch_x = release_x;
        s_lcd_touch_y = release_y;
    }
    self->num_points = num_points;
    s_lcd_touch_active = any_active ? 1U : 0U;
    return true;
}

static bool touch_service_pending(void) {
    bool pending;
    FSP_CRITICAL_SECTION_DEFINE;
    FSP_CRITICAL_SECTION_ENTER;
    pending = s_touch_read_pending != 0U;
    s_touch_read_pending = 0;
    FSP_CRITICAL_SECTION_EXIT;

    if (!pending) {
        return true;
    }
    if (!ft5x06_payload_get()) {
        machine_lcd_obj.num_points = 0;
        s_lcd_touch_active = 0;
        return false;
    }
    return true;
}

void callback_icu(external_irq_callback_args_t *p_args) { // callback_icu
    if (g_external_irq11_cfg.channel == p_args->channel) {
        /* Never transact on SCI-I2C in the external IRQ. The next LVGL/Python poll
         * consumes this flag and performs one bounded foreground transaction. */
        s_touch_irq_count++;
        s_touch_read_pending = 1;
    }
}

static volatile uint32_t lcd_vsync_counter = 0;

void lcd_Vsync_ISR(display_callback_args_t *p_args) {
    (void)p_args;
    lcd_vsync_counter++;
}

STATIC mp_int_t machine_lcd_set_buffer(mp_obj_t self_in, mp_buffer_info_t *bufinfo, mp_uint_t flags) {
    // mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    bufinfo->buf = g_display0_cfg.input[0].p_base;
    bufinfo->len = g_display0_cfg.input[0].hstride * g_display0_cfg.input[0].vsize * ((g_display0_cfg.input[0].format < DISPLAY_IN_FORMAT_16BITS_RGB565)? 4 : 2);
    bufinfo->typecode = 'B'; // view framebuf as bytes
    return 0;
}

STATIC mp_obj_t machine_lcd_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    // return self object
    return MP_OBJ_FROM_PTR(&machine_lcd_obj);
}

STATIC mp_obj_t lcd_init(mp_obj_t self_in) {
    machine_lcd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->isinited) {
        size_t framebuffer_size = g_display0_cfg.input[0].hstride *
            g_display0_cfg.input[0].vsize * sizeof(uint16_t);
        memset(g_display0_cfg.input[0].p_base, 0, framebuffer_size);

        if (FSP_SUCCESS == R_GLCDC_Open(&g_display0_ctrl, &g_display0_cfg)) {
            self->isinited = 1;
            ra_gpio_config(self->bl->pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_LOW_POWER, 0);
            ra_gpio_config(self->en->pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_LOW_POWER, 0);
            ra_gpio_write(self->bl->pin, 1);
            ra_gpio_write(self->en->pin, 1);
        } else {
            mp_raise_ValueError(MP_ERROR_TEXT("Can't init R_GLCDC"));
        }

        ra_gpio_config(self->tch->pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_LOW_POWER, 0);
        ra_gpio_write(self->tch->pin, 1);

        if (FSP_SUCCESS != R_SCI_I2C_Open(&g_i2c_touch_ctrl, &g_i2c_touch_cfg)) {
            machine_lcd_soft_reset();
            mp_raise_ValueError(MP_ERROR_TEXT("Can't init R_SCI"));
        }

        self->num_points = 0;
        s_touch_read_pending = 0;
        s_lcd_touch_active = 0;
        s_lcd_touch_x = 0;
        s_lcd_touch_y = 0;
        memset(&locked, 0, sizeof(locked));

        /* Enable touch IRQ only after the touch I2C instance is ready. */
        if (FSP_SUCCESS != R_ICU_ExternalIrqOpen(&g_external_irq11_ctrl, &g_external_irq11_cfg) ||
            FSP_SUCCESS != R_ICU_ExternalIrqEnable(&g_external_irq11_ctrl)) {
            machine_lcd_soft_reset();
            mp_raise_ValueError(MP_ERROR_TEXT("Can't init touch IRQ"));
        }
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Olready inited!"));
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_lcd_init_obj, lcd_init);

STATIC mp_obj_t lcd_touched(mp_obj_t self_in) {
    machine_lcd_obj_t *self = MP_OBJ_TO_PTR(self_in);

    /* Legacy Python input path: service the same IRQ-pending flag as the C bridge. */
    (void)touch_service_pending();

    FSP_CRITICAL_SECTION_DEFINE;
    FSP_CRITICAL_SECTION_ENTER;

    if (!locked.num_stamp_points) {
        if (self->num_points) {
            locked.num_stamp_points = (self->num_points > FT5X06_NUM_POINTS)? FT5X06_NUM_POINTS : self->num_points;

            for (uint8_t i = 0; i < locked.num_stamp_points; i++) {
                locked.stamp_point[i] = self->point[i];
            }

            self->num_points = 0;
        } else {
            locked.num_stamp_points = 0;
        }
    }

    FSP_CRITICAL_SECTION_EXIT;

    return MP_OBJ_NEW_SMALL_INT(locked.num_stamp_points);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_lcd_touched_obj, lcd_touched);

STATIC mp_obj_t lcd_touches(mp_obj_t self_in) {
    // machine_lcd_obj_t *self = MP_OBJ_TO_PTR(self_in);

    const char *ev[] = {"NONE", "DOWN", "HOLD", "MOVE", "UP"};

    if (!locked.num_stamp_points) {
        lcd_touched(self_in);
    }

    mp_obj_list_t *list = mp_obj_new_list(locked.num_stamp_points, NULL);

    if (locked.num_stamp_points) {
        // Add elements to the list
        for (uint8_t i = 0; i < locked.num_stamp_points; i++) {
            list->items[i] = mp_obj_new_tuple(3, (mp_obj_t[]) {mp_obj_new_int(locked.stamp_point[i].x), mp_obj_new_int(locked.stamp_point[i].y), mp_obj_new_str(ev[locked.stamp_point[i].event], strlen(ev[locked.stamp_point[i].event]))});
        }

        locked.num_stamp_points = 0;
    }
    return MP_OBJ_FROM_PTR(list);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_lcd_touches_obj, lcd_touches);

#if defined(USE_FSP_DRW)
STATIC mp_obj_t lcd_drw_stats(mp_obj_t self_in) {
    (void)self_in;
    uint32_t irq_count;
    uint32_t allocation_count;
    uint32_t active_bytes;
    uint32_t peak_bytes;
    vk_ra6m3_dave2d_get_stats(&irq_count, &allocation_count, &active_bytes, &peak_bytes);
    mp_obj_t stats[] = {
        mp_obj_new_int_from_uint(irq_count),
        mp_obj_new_int_from_uint(allocation_count),
        mp_obj_new_int_from_uint(active_bytes),
        mp_obj_new_int_from_uint(peak_bytes),
    };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(stats), stats);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_lcd_drw_stats_obj, lcd_drw_stats);
#endif

STATIC mp_obj_t lcd_deinit(mp_obj_t self_in) {
    machine_lcd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->isinited) {
        machine_lcd_soft_reset();
        if (self->isinited) {
            mp_raise_ValueError(MP_ERROR_TEXT("Can't deinit LCD"));
        }
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Not inited! call LCD.init() first"));
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_lcd_deinit_obj, lcd_deinit);

STATIC mp_obj_t lcd_start(mp_obj_t self_in) {
    machine_lcd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->isstarted) {
        if (FSP_SUCCESS == R_GLCDC_Start(&g_display0_ctrl)) {
            self->isstarted = 1;
        } else {
            mp_raise_ValueError(MP_ERROR_TEXT("Can't start R_GLCDC"));
        }
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Olready started!"));
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_lcd_start_obj, lcd_start);

STATIC mp_obj_t lcd_stop(mp_obj_t self_in) {
    machine_lcd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->isstarted) {
        if (FSP_SUCCESS == R_GLCDC_Stop(&g_display0_ctrl)) {
            self->isstarted = 0;
        } else {
            mp_raise_ValueError(MP_ERROR_TEXT("Can't stop R_GLCDC"));
        }
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Not started! call LCD.start() first"));
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_lcd_stop_obj, lcd_stop);

/*
STATIC mp_obj_t lcd_changebuf(mp_obj_t self_in, mp_obj_t idx){
	machine_lcd_obj_t *self = MP_OBJ_TO_PTR(self_in);
	mp_int_t i = mp_obj_get_int(idx);

	self->ischanged = 0;
	if(self->isinited && self->isstarted)
		if(FSP_SUCCESS == R_GLCDC_BufferChange(&g_display0_ctrl, fb_background[i? 1 : 0], DISPLAY_FRAME_LAYER_1))
		{
			self->ischanged = 1;
			self->idx = i;
		}
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_lcd_changebuf_obj, lcd_changebuf);
*/
// vsync([timeout_ms=20]) -- block until the NEXT GLCDC line-detect (VSYNC)
// event or timeout. Returns True if the pulse arrived, False on timeout.
// Used to gate LVGL render start so DIRECT-mode drawing chases the scanout
// beam instead of racing it mid-frame (visible flicker on frequent updates).
STATIC mp_obj_t lcd_vsync(size_t n_args, const mp_obj_t *args) {
    (void)args;
    uint32_t timeout_ms = (n_args > 1) ? (uint32_t)mp_obj_get_int(args[1]) : 20;
    uint32_t start_cnt = lcd_vsync_counter;
    uint32_t t0 = (uint32_t)mp_hal_ticks_ms();
    while (lcd_vsync_counter == start_cnt) {
        if ((uint32_t)mp_hal_ticks_ms() - t0 >= timeout_ms) {
            return mp_const_false;
        }
    }
    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_lcd_vsync_obj, 1, 2, lcd_vsync);

/* ---- C LVGL display + input bridge -----------------------------------------------
 * Replaces the per-frame Python flush/touch/VSYNC callbacks in pRGB.py.  Those allocate
 * MicroPython wrapper objects on every render (flush) and every ~30 Hz input poll (read)
 * -- a continuous idle garbage stream that forces GC.  Registering the same three
 * callbacks in C removes that garbage entirely.  DIRECT single-framebuffer, identical
 * behaviour to the Python path (GLCDC framebuffer, cached FT5x06 touch, VSYNC gate). */
static void lcd_lv_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);                    // DIRECT mode: drawing is done
}

static volatile uint32_t s_indev_calls = 0;          // diag: LVGL polls of the read cb
static volatile uint32_t s_indev_press = 0;          // diag: reads that saw a contact

static void lcd_lv_indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    s_indev_calls++;
    /* The external IRQ only raises a flag. Service at most one bounded SCI-I2C read here,
     * in foreground context, then report the persistent DOWN/MOVE/HOLD versus UP level.
     * No fresh FT5x06 packet means "state unchanged", not a synthetic RELEASED. */
    (void)touch_service_pending();

    uint8_t active;
    int16_t x;
    int16_t y;
    FSP_CRITICAL_SECTION_DEFINE;
    FSP_CRITICAL_SECTION_ENTER;
    active = s_lcd_touch_active;
    x = s_lcd_touch_x;
    y = s_lcd_touch_y;
    FSP_CRITICAL_SECTION_EXIT;
    if (active) {
        s_indev_press++;
    }
    data->point.x = (int32_t)x;
    data->point.y = (int32_t)y;
    data->state = active ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// touch_debug() -> (indev polls, pressed polls, x, y, active, IRQs, I2C errors, timeouts).
STATIC mp_obj_t lcd_touch_debug(mp_obj_t self_in) {
    (void)self_in;
    mp_obj_t t[8] = {
        mp_obj_new_int_from_uint(s_indev_calls),
        mp_obj_new_int_from_uint(s_indev_press),
        MP_OBJ_NEW_SMALL_INT(s_lcd_touch_x),
        MP_OBJ_NEW_SMALL_INT(s_lcd_touch_y),
        MP_OBJ_NEW_SMALL_INT(s_lcd_touch_active),
        mp_obj_new_int_from_uint(s_touch_irq_count),
        mp_obj_new_int_from_uint(s_touch_i2c_errors),
        mp_obj_new_int_from_uint(s_touch_i2c_timeouts),
    };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(t), t);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_lcd_touch_debug_obj, lcd_touch_debug);

static void lcd_lv_render_start_cb(lv_event_t *e) {
    (void)e;
    uint32_t start_cnt = lcd_vsync_counter;          // wait for the next GLCDC frame pulse
    uint32_t t0 = (uint32_t)mp_hal_ticks_ms();
    while (lcd_vsync_counter == start_cnt) {
        if ((uint32_t)mp_hal_ticks_ms() - t0 >= 20U) {
            break;
        }
    }
}

// lvgl_setup() -- create the LVGL display + pointer indev in C with C callbacks, so no
// Python wrapper is allocated per frame or per input poll. Call once after lv.init().
// Returns True if the C bridge was installed (idempotent). pRGB uses it when present and
// keeps its Python callbacks only as a fallback for firmware without this method.
STATIC mp_obj_t lcd_lvgl_setup(mp_obj_t self_in) {
    (void)self_in;
    if (s_lcd_lvgl_bridged) {
        return mp_const_true;
    }
    if (!lv_is_initialized()) {
        lv_init();
    }
    uint32_t w = g_display0_cfg.input[0].hsize;
    uint32_t h = g_display0_cfg.input[0].vsize;
    void *fb = g_display0_cfg.input[0].p_base;
    uint32_t sz = g_display0_cfg.input[0].hstride * g_display0_cfg.input[0].vsize *
        ((g_display0_cfg.input[0].format < DISPLAY_IN_FORMAT_16BITS_RGB565) ? 4U : 2U);

    lv_display_t *disp = lv_display_create(w, h);
    if (disp == NULL) {
        return mp_const_false;
    }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, lcd_lv_flush_cb);
    lv_display_set_buffers(disp, fb, NULL, sz, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_add_event_cb(disp, lcd_lv_render_start_cb, LV_EVENT_RENDER_START, NULL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, disp);
    lv_indev_set_read_cb(indev, lcd_lv_indev_read_cb);

    s_lcd_lvgl_bridged = 1;
    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_lcd_lvgl_setup_obj, lcd_lvgl_setup);

STATIC const mp_rom_map_elem_t lcd_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),                MP_ROM_PTR(&machine_lcd_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_vsync),               MP_ROM_PTR(&machine_lcd_vsync_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),              MP_ROM_PTR(&machine_lcd_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_start),               MP_ROM_PTR(&machine_lcd_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),                MP_ROM_PTR(&machine_lcd_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_touched),             MP_ROM_PTR(&machine_lcd_touched_obj) },
    { MP_ROM_QSTR(MP_QSTR_touches),             MP_ROM_PTR(&machine_lcd_touches_obj) },
    { MP_ROM_QSTR(MP_QSTR_lvgl_setup),          MP_ROM_PTR(&machine_lcd_lvgl_setup_obj) },
    { MP_ROM_QSTR(MP_QSTR_touch_debug),         MP_ROM_PTR(&machine_lcd_touch_debug_obj) },
    #if defined(USE_FSP_DRW)
    { MP_ROM_QSTR(MP_QSTR_drw_stats),            MP_ROM_PTR(&machine_lcd_drw_stats_obj) },
    #endif
    // control
    // { MP_ROM_QSTR(MP_QSTR_changebuf),           MP_ROM_PTR(&machine_lcd_changebuf_obj) },
};
STATIC MP_DEFINE_CONST_DICT(lcd_locals_dict, lcd_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_lcd_type,
    MP_QSTR_LCD,
    MP_TYPE_FLAG_NONE,
    make_new, machine_lcd_make_new,
    buffer, machine_lcd_set_buffer,
    locals_dict, &lcd_locals_dict
    );

#endif // MODULE_LCD_ENABLED
