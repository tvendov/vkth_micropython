/*
 * IQADC - coherent I/Q ADC capture, Python wrapper.
 * Wraps ra_iq_adc (AGT + ELC + ADC0/ADC1 + DTC ping-pong, RA6M3 only).
 * API: IQADC(i_pin, q_pin, rate=, block=, pga=, gain=)
 *      .start() .stop() .read_block(ib, qb) .blocks() .overruns()
 *      .unit1_stalls() .last_error() .ready() .status() .deinit()
 *
 * REQ-RT-002: nothing the running capture loop calls may allocate on the
 * Python heap.  The consumer pre-allocates the array('H') I/Q buffers ONCE
 * before start() and passes them into read_block() every call; read_block()
 * only memcpy's the acquired half into them.  Small-int returns and the
 * mp_const_* singletons do not allocate, so the hot path (read_block and the
 * int/bool getters) uses only those as return values.  status() DOES allocate
 * (it builds a dict) and is control-plane only - see the note on that method.
 *
 * Known limitation: blocks/overruns/unit1_stalls and the sequence number are
 * uint32.  Above MP_SMALL_INT_MAX the zero-allocation getters raise
 * OverflowError instead of returning a heap-allocated long int.  At 375 blk/s
 * that is reached after ~16 days on this port.  Accepted for v1; a long-run
 * consumer must stop()/deinit() and reset the counters before then.
 */

#include "py/mpconfig.h"

#if MICROPY_HW_ENABLE_IQ_ADC

#include <string.h>

#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/binary.h"
#include "py/smallint.h"
#include "pin.h"
#include "ra/ra_adc.h"
#include "ra/ra_dac.h"
#include "ra/ra_iq_adc.h"
#include "ra/ra_sdr_caps.h"

/* ------------------------------------------------------------------ */
/* Object                                                               */
/* ------------------------------------------------------------------ */

typedef struct _machine_iqadc_obj_t {
    mp_obj_base_t base;
    bool          active;
    uint32_t      i_pin;
    uint32_t      q_pin;
    uint32_t      rate;
    uint16_t      block;
} machine_iqadc_obj_t;

static machine_iqadc_obj_t machine_iqadc_obj;

extern const mp_obj_type_t machine_iqadc_type;

static mp_obj_t machine_iqadc_new_small_uint(uint32_t value) {
    if (value > (uint32_t)MP_SMALL_INT_MAX) {
        mp_raise_msg(&mp_type_OverflowError, MP_ERROR_TEXT("counter overflow"));
    }
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)value);
}

/* ------------------------------------------------------------------ */
/* print                                                                */
/* ------------------------------------------------------------------ */

static void machine_iqadc_print(const mp_print_t *print,
                                mp_obj_t self_in,
                                mp_print_kind_t kind) {
    (void)kind;
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ra_iq_adc_status_t st;
    ra_iq_adc_get_status(&st);
    mp_printf(print, "IQADC(rate=%u, block=%u, running=%u)",
              (unsigned)self->rate, (unsigned)self->block, st.running);
}

/* ------------------------------------------------------------------ */
/* make_new: IQADC(i_pin, q_pin, *, rate=48000, block=128,             */
/*                 pga=PGA_BYPASS, gain=0)                              */
/* ------------------------------------------------------------------ */

static mp_obj_t machine_iqadc_make_new(const mp_obj_type_t *type,
                                       size_t n_args, size_t n_kw,
                                       const mp_obj_t *all_args) {
    (void)type;
    enum { ARG_i_pin, ARG_q_pin, ARG_rate, ARG_block, ARG_pga, ARG_gain };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_i_pin, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_q_pin, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_rate,  MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 48000} },
        { MP_QSTR_block, MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 128} },
        { MP_QSTR_pga,   MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = (int)RA_ADC_PGA_BYPASS} },
        { MP_QSTR_gain,  MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 0} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
                              MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_rate].u_int <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad rate"));
    }
    if (args[ARG_block].u_int < 1 ||
        args[ARG_block].u_int > RA_IQ_ADC_MAX_BLOCK_SAMPLES) {
        mp_raise_ValueError(MP_ERROR_TEXT("block out of range"));
    }
    if (args[ARG_pga].u_int == (int)RA_ADC_PGA_OFF) {
        mp_raise_ValueError(MP_ERROR_TEXT("pga OFF rejected"));
    }
    if ((args[ARG_pga].u_int != (int)RA_ADC_PGA_BYPASS)
        && (args[ARG_pga].u_int != (int)RA_ADC_PGA_SINGLE)
        && (args[ARG_pga].u_int != (int)RA_ADC_PGA_DIFFERENTIAL)) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad pga"));
    }
    if (args[ARG_gain].u_int < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad gain"));
    }
    if ((args[ARG_pga].u_int == (int)RA_ADC_PGA_SINGLE)
        && (args[ARG_gain].u_int > (int)RA_ADC_PGA_GAIN_13_333)) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad gain"));
    }
    if ((args[ARG_pga].u_int == (int)RA_ADC_PGA_DIFFERENTIAL)
        && (args[ARG_gain].u_int > (int)RA_ADC_PGA_DIFF_GAIN_5_667)) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad gain"));
    }
    if ((args[ARG_pga].u_int == (int)RA_ADC_PGA_BYPASS)
        && (args[ARG_gain].u_int != 0)) {
        mp_raise_ValueError(MP_ERROR_TEXT("bypass gain must be 0"));
    }

    const machine_pin_obj_t *ip = machine_pin_find(args[ARG_i_pin].u_obj);
    const machine_pin_obj_t *qp = machine_pin_find(args[ARG_q_pin].u_obj);

    /* The I/Q pins must land in the PGA / dedicated-S&H channel range of each
     * unit; those ranges come from the capability layer.  On VK_RA6M3 they are
     * AN000..AN002 (unit 0) and AN100..AN102 (unit 1). */
    const ra_sdr_mcu_facts_t *mcu = ra_sdr_caps_get()->mcu;
    uint8_t i_lo = mcu->pga_adc0_first_ch;
    uint8_t i_hi = (uint8_t)(mcu->pga_adc0_first_ch + mcu->pga_channels_per_unit - 1U);
    uint8_t q_lo = mcu->pga_adc1_first_ch;
    uint8_t q_hi = (uint8_t)(mcu->pga_adc1_first_ch + mcu->pga_channels_per_unit - 1U);

    uint8_t i_ch, q_ch;
    if (!ra_adc_pin_to_ch(ip->pin, &i_ch) || (i_ch < i_lo) || (i_ch > i_hi)) {
        mp_raise_ValueError(MP_ERROR_TEXT("i_pin must be AN000..AN002"));
    }
    if (!ra_adc_pin_to_ch(qp->pin, &q_ch) || (q_ch < q_lo) || (q_ch > q_hi)) {
        mp_raise_ValueError(MP_ERROR_TEXT("q_pin must be AN100..AN102"));
    }

    machine_iqadc_obj_t *self = &machine_iqadc_obj;

    if (self->active) {
        ra_iq_adc_deinit();
        self->active = false;
    }

    self->base.type = &machine_iqadc_type;
    self->i_pin = ip->pin;
    self->q_pin = qp->pin;
    self->rate  = (uint32_t)args[ARG_rate].u_int;
    self->block = (uint16_t)args[ARG_block].u_int;

    if (!ra_iq_adc_init(self->i_pin, self->q_pin, self->rate, self->block,
                        (ra_adc_pga_mode_t)args[ARG_pga].u_int,
                        (uint8_t)args[ARG_gain].u_int)) {
        mp_raise_OSError(MP_EIO);
    }

    self->active = true;
    return MP_OBJ_FROM_PTR(self);
}

/* ------------------------------------------------------------------ */
/* Methods                                                              */
/* ------------------------------------------------------------------ */

static mp_obj_t machine_iqadc_start(mp_obj_t self_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    if (!ra_iq_adc_start()) { mp_raise_OSError(MP_EIO); }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_start_obj, machine_iqadc_start);

static mp_obj_t machine_iqadc_stop(mp_obj_t self_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->active) { ra_iq_adc_stop(); }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_stop_obj, machine_iqadc_stop);

/* read_block(ib, qb) -> seq (small int) | None
 * ib, qb must each be array('H') with len(bytes) >= block*2.  Copies the
 * acquired I and Q halves into them and returns the block sequence number.
 * Returns None when no completed block is ready.  ZERO ALLOC: the buffers are
 * caller-owned and pre-allocated before start(); this method only memcpy's and
 * returns a small int / the None singleton.                                   */
static mp_obj_t machine_iqadc_read_block(mp_obj_t self_in,
                                         mp_obj_t ib_in, mp_obj_t qb_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }

    mp_buffer_info_t bi, bq;
    mp_get_buffer_raise(ib_in, &bi, MP_BUFFER_WRITE);
    mp_get_buffer_raise(qb_in, &bq, MP_BUFFER_WRITE);

    if (bi.typecode != 'H' || bq.typecode != 'H') {
        mp_raise_ValueError(MP_ERROR_TEXT("buf must be array('H')"));
    }
    if (bi.len < (size_t)self->block * sizeof(uint16_t) ||
        bq.len < (size_t)self->block * sizeof(uint16_t)) {
        mp_raise_ValueError(MP_ERROR_TEXT("buf too small"));
    }

    const uint16_t *ip;
    const uint16_t *qp;
    size_t n;
    uint32_t seq;
    if (!ra_iq_adc_acquire(&ip, &qp, &n, &seq)) {
        return mp_const_none;
    }
    if (n > self->block) {
        mp_raise_OSError(MP_EIO);
    }

    memcpy(bi.buf, ip, n * sizeof(uint16_t));
    memcpy(bq.buf, qp, n * sizeof(uint16_t));
    return machine_iqadc_new_small_uint(seq);
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_iqadc_read_block_obj, machine_iqadc_read_block);

/* Zero-alloc int/bool getters.  Each reads the status snapshot into a local
 * struct and returns exactly one small int or a bool singleton.              */

static mp_obj_t machine_iqadc_blocks(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_adc_status_t st;
    ra_iq_adc_get_status(&st);
    return machine_iqadc_new_small_uint(st.blocks);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_blocks_obj, machine_iqadc_blocks);

static mp_obj_t machine_iqadc_overruns(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_adc_status_t st;
    ra_iq_adc_get_status(&st);
    return machine_iqadc_new_small_uint(st.overruns);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_overruns_obj, machine_iqadc_overruns);

static mp_obj_t machine_iqadc_unit1_stalls(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_adc_status_t st;
    ra_iq_adc_get_status(&st);
    return machine_iqadc_new_small_uint(st.unit1_stalls);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_unit1_stalls_obj, machine_iqadc_unit1_stalls);

static mp_obj_t machine_iqadc_last_error(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_adc_status_t st;
    ra_iq_adc_get_status(&st);
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)st.last_error);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_last_error_obj, machine_iqadc_last_error);

static mp_obj_t machine_iqadc_ready(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_adc_status_t st;
    ra_iq_adc_get_status(&st);
    return (st.ready != 0) ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_ready_obj, machine_iqadc_ready);

/* status() -> dict.  ALLOCATES (builds a dict): control-plane only.  Do NOT
 * call this inside the running capture loop - use it before start(), after
 * stop(), or for one-off diagnostics.  For the hot path use the zero-alloc
 * getters above.                                                             */
static mp_obj_t machine_iqadc_status(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_adc_status_t st;
    ra_iq_adc_get_status(&st);

    mp_obj_t d = mp_obj_new_dict(9);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_initialised), mp_obj_new_int(st.initialised));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_running), mp_obj_new_int(st.running));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_ready), mp_obj_new_int(st.ready));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_rate), mp_obj_new_int(st.sample_rate_hz));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_block), mp_obj_new_int(st.block_samples));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_blocks), mp_obj_new_int(st.blocks));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_overruns), mp_obj_new_int(st.overruns));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_unit1_stalls), mp_obj_new_int(st.unit1_stalls));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_last_error), mp_obj_new_int(st.last_error));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_status_obj, machine_iqadc_status);

/* Phase-3 DSP counters (control-plane; allocates a dict, not the realtime path). */
static mp_obj_t machine_iqadc_dsp_status(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_dsp_status_t st;
    ra_iq_adc_get_dsp_status(&st);

    mp_obj_t d = mp_obj_new_dict(4);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_dsp_blocks), mp_obj_new_int(st.dsp_blocks));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_dsp_samples), mp_obj_new_int(st.dsp_samples));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_i_mean), mp_obj_new_int(st.i_mean));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_q_mean), mp_obj_new_int(st.q_mean));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_dsp_status_obj, machine_iqadc_dsp_status);

/* am_dac(dac_pin, ch=0) -> None.  Starts the AM-demod-to-DAC audio path on the
 * decimated I/Q.  dac_pin must be a DAC-capable pin (P014 / DA0 on VK_RA6M3).
 * Control-plane: this only arms the path; the per-sample work runs in the ADC
 * and DAC ISRs with no CPU per sample and no Python. */
static mp_obj_t machine_iqadc_am_dac(size_t n_args, const mp_obj_t *args) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }

    const machine_pin_obj_t *dp = machine_pin_find(args[1]);
    if (!ra_dac_is_dac_pin(dp->pin)) {
        mp_raise_ValueError(MP_ERROR_TEXT("not a DAC pin"));
    }

    mp_int_t ch = (n_args > 2) ? mp_obj_get_int(args[2]) : 0;
    if (ch < 0 || ch > 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad ch"));
    }

    if (!ra_iq_adc_am_dac_start(dp->pin, (uint8_t)ch)) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_iqadc_am_dac_obj,
                                           2, 3, machine_iqadc_am_dac);

static mp_obj_t machine_iqadc_am_dac_stop(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_adc_am_dac_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_am_dac_stop_obj, machine_iqadc_am_dac_stop);

/* am_status() -> dict.  ALLOCATES (builds a dict): control-plane only. */
static mp_obj_t machine_iqadc_am_status(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_am_status_t st;
    ra_iq_adc_get_am_status(&st);

    mp_obj_t d = mp_obj_new_dict(3);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_am_active), mp_obj_new_int(st.am_active));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_audio_underruns), mp_obj_new_int(st.audio_underruns));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_ring_overruns), mp_obj_new_int(st.ring_overruns));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_am_status_obj, machine_iqadc_am_status);

static mp_obj_t machine_iqadc_deinit(mp_obj_t self_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->active) {
        ra_iq_adc_deinit();
        self->active = false;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_deinit_obj, machine_iqadc_deinit);

/* ------------------------------------------------------------------ */
/* Type                                                                 */
/* ------------------------------------------------------------------ */

static const mp_rom_map_elem_t machine_iqadc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_start),        MP_ROM_PTR(&machine_iqadc_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),         MP_ROM_PTR(&machine_iqadc_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_block),   MP_ROM_PTR(&machine_iqadc_read_block_obj) },
    { MP_ROM_QSTR(MP_QSTR_blocks),       MP_ROM_PTR(&machine_iqadc_blocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_overruns),     MP_ROM_PTR(&machine_iqadc_overruns_obj) },
    { MP_ROM_QSTR(MP_QSTR_unit1_stalls), MP_ROM_PTR(&machine_iqadc_unit1_stalls_obj) },
    { MP_ROM_QSTR(MP_QSTR_last_error),   MP_ROM_PTR(&machine_iqadc_last_error_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready),        MP_ROM_PTR(&machine_iqadc_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_status),       MP_ROM_PTR(&machine_iqadc_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_dsp_status),   MP_ROM_PTR(&machine_iqadc_dsp_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_am_dac),       MP_ROM_PTR(&machine_iqadc_am_dac_obj) },
    { MP_ROM_QSTR(MP_QSTR_am_dac_stop),  MP_ROM_PTR(&machine_iqadc_am_dac_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_am_status),    MP_ROM_PTR(&machine_iqadc_am_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),       MP_ROM_PTR(&machine_iqadc_deinit_obj) },
};
static MP_DEFINE_CONST_DICT(machine_iqadc_locals_dict,
                             machine_iqadc_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_iqadc_type,
    MP_QSTR_IQADC,
    MP_TYPE_FLAG_NONE,
    make_new, machine_iqadc_make_new,
    print,    machine_iqadc_print,
    locals_dict, &machine_iqadc_locals_dict
);

#endif /* MICROPY_HW_ENABLE_IQ_ADC */
