/*
 * IQADC - coherent I/Q ADC capture, Python wrapper.
 * Wraps ra_iq_adc (AGT + ELC + ADC0/ADC1 + DTC ping-pong, RA6M3 only).
 * API: IQADC(i_pin, q_pin, rate=, block=, pga=, gain=)
 *      .start() .stop() .read_block(ib, qb) .blocks() .overruns()
 *      .unit1_stalls() .last_error() .ready() .status() .deinit()
 *      .demod(mode) .audio_status() .read_audio(buf)
 * The demodulator produces a generic audio stream; play it via DAC.stream(iqadc).
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

/* timing() -> per-block DSP cycle budget.  block_cyc = cpu_hz*block/rate; the DSP
 * (dsp_process + demod_produce) must fit inside that.  Control-plane. */
static mp_obj_t machine_iqadc_timing(mp_obj_t self_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t last_cyc, max_cyc, avg_cyc, cpu_hz;
    ra_iq_adc_get_timing(&last_cyc, &max_cyc, &avg_cyc, &cpu_hz);

    uint32_t block_cyc = (self->rate != 0U)
        ? (uint32_t)(((uint64_t)cpu_hz * self->block) / self->rate) : 0U;
    mp_float_t max_pct = (block_cyc != 0U)
        ? ((mp_float_t)max_cyc * (mp_float_t)100.0 / (mp_float_t)block_cyc) : (mp_float_t)0.0;

    mp_obj_t d = mp_obj_new_dict(6);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_last_cyc), mp_obj_new_int(last_cyc));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_max_cyc), mp_obj_new_int(max_cyc));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_avg_cyc), mp_obj_new_int(avg_cyc));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_block_cyc), mp_obj_new_int(block_cyc));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_cpu_hz), mp_obj_new_int(cpu_hz));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_max_pct), mp_obj_new_float(max_pct));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_timing_obj, machine_iqadc_timing);

/* demod(mode) -> None.  Selects the demodulator that fills the audio ring: "am",
 * "usb", "lsb", "cw" or "off".  Control-plane only; the per-sample demod work runs in the ADC block
 * callback with no CPU per sample and no Python.  The DAC is decoupled: play the
 * stream with DAC.stream(iqadc). */
static mp_obj_t machine_iqadc_demod(mp_obj_t self_in, mp_obj_t mode_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }

    qstr mode = mp_obj_str_get_qstr(mode_in);
    uint8_t m;
    if (mode == MP_QSTR_am) {
        m = (uint8_t)RA_IQ_DEMOD_AM;
    } else if (mode == MP_QSTR_usb) {
        m = (uint8_t)RA_IQ_DEMOD_USB;
    } else if (mode == MP_QSTR_lsb) {
        m = (uint8_t)RA_IQ_DEMOD_LSB;
    } else if (mode == MP_QSTR_cw) {
        m = (uint8_t)RA_IQ_DEMOD_CW;
    } else if (mode == MP_QSTR_off) {
        m = (uint8_t)RA_IQ_DEMOD_OFF;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("unknown demod mode"));
    }
    ra_iq_adc_set_demod(m);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_iqadc_demod_obj, machine_iqadc_demod);

/* audio_status() -> dict.  ALLOCATES (builds a dict): control-plane only. */
static mp_obj_t machine_iqadc_audio_status(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_audio_status_t st;
    ra_iq_adc_get_audio_status(&st);

    mp_obj_t d = mp_obj_new_dict(3);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_demod), mp_obj_new_int(st.demod_mode));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_audio_underruns), mp_obj_new_int(st.audio_underruns));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_ring_overruns), mp_obj_new_int(st.ring_overruns));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_audio_status_obj, machine_iqadc_audio_status);

/* read_audio(buf) -> sample_count.  Debug/inspection pull of the demod audio ring;
 * NOT the realtime path (the realtime path is DAC.stream() via ra_iq_adc_audio_pull
 * from the DMAC ISR).  buf must be array('H') with len >= decimated sample_count.
 * On an empty ring the pull writes mid-scale silence, so this always fills buf. */
static mp_obj_t machine_iqadc_read_audio(mp_obj_t self_in, mp_obj_t buf_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }

    mp_buffer_info_t bi;
    mp_get_buffer_raise(buf_in, &bi, MP_BUFFER_WRITE);
    if (bi.typecode != 'H') {
        mp_raise_ValueError(MP_ERROR_TEXT("buf must be array('H')"));
    }

    size_t sample_count;
    ra_iq_adc_get_audio_params(NULL, &sample_count);
    if (bi.len < sample_count * sizeof(uint16_t)) {
        mp_raise_ValueError(MP_ERROR_TEXT("buf too small"));
    }

    ra_iq_adc_audio_pull((uint16_t *)bi.buf, sample_count);
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)sample_count);
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_iqadc_read_audio_obj, machine_iqadc_read_audio);

/* iq_correction(enable=True, amp=<float>, phase=<float>) — manual I/Q imbalance.
 * amp/phase default to the current values, so enable/disable does not lose them. */
static mp_obj_t machine_iqadc_iq_correction(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_enable, ARG_amp, ARG_phase };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_enable, MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_amp,    MP_ARG_OBJ,  {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_phase,  MP_ARG_OBJ,  {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed), allowed, args);

    uint8_t en;
    int32_t amp_q15;
    int32_t phase_q15;
    ra_iq_adc_get_iq_correction(&en, &amp_q15, &phase_q15);

    if (args[ARG_amp].u_obj != MP_OBJ_NULL) {
        mp_float_t a = mp_obj_get_float(args[ARG_amp].u_obj);
        amp_q15 = (int32_t)(a * (mp_float_t)32768.0 + (a >= 0 ? (mp_float_t)0.5 : (mp_float_t)-0.5));
    }
    if (args[ARG_phase].u_obj != MP_OBJ_NULL) {
        mp_float_t p = mp_obj_get_float(args[ARG_phase].u_obj);
        phase_q15 = (int32_t)(p * (mp_float_t)32768.0 + (p >= 0 ? (mp_float_t)0.5 : (mp_float_t)-0.5));
    }
    ra_iq_adc_set_iq_correction(args[ARG_enable].u_bool ? 1U : 0U, amp_q15, phase_q15);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_iqadc_iq_correction_obj, 1, machine_iqadc_iq_correction);

static mp_obj_t machine_iqadc_iq_correction_status(mp_obj_t self_in) {
    (void)self_in;
    uint8_t en;
    int32_t amp_q15;
    int32_t phase_q15;
    ra_iq_adc_get_iq_correction(&en, &amp_q15, &phase_q15);

    mp_obj_t d = mp_obj_new_dict(3);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_correcting), mp_obj_new_int(en));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_amp), mp_obj_new_float((mp_float_t)amp_q15 / (mp_float_t)32768.0));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_phase), mp_obj_new_float((mp_float_t)phase_q15 / (mp_float_t)32768.0));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_iq_correction_status_obj, machine_iqadc_iq_correction_status);

/* agc(mode, *, gain=1.0, target=0.5) -> None.  mode is "off"/"fast"/"slow"/
 * "manual".  gain (float, Q15 when converted) is used only in manual mode; target
 * is a 0..1 fraction of the ~2048 audio range, converted to an amplitude set-point.
 * Control-plane; the per-sample AGC runs in the ADC block callback (integer only,
 * no Python). */
static mp_obj_t machine_iqadc_agc(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }

    /* Arg qstr names are SDR-unique (agc_mode/rms_target) to avoid the frozen
     * asyncio/LVGL qstr collisions that bite bare words like mode/target. */
    enum { ARG_mode, ARG_gain, ARG_target };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_agc_mode,   MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_gain,       MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_rms_target, MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed), allowed, args);

    /* Compare the mode string with strcmp so "off"/"fast"/"slow"/"manual" need no
     * qstrs (slow/manual are frozen-reserved on this board). */
    const char *ms = mp_obj_str_get_str(args[ARG_mode].u_obj);
    uint8_t m;
    if (strcmp(ms, "off") == 0) {
        m = 0U;
    } else if (strcmp(ms, "fast") == 0) {
        m = 1U;
    } else if (strcmp(ms, "slow") == 0) {
        m = 2U;
    } else if (strcmp(ms, "manual") == 0) {
        m = 3U;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("unknown agc mode"));
    }

    /* gain only meaningful in manual; default 1.0 (unity) when omitted. */
    mp_float_t g = (args[ARG_gain].u_obj != MP_OBJ_NULL)
        ? mp_obj_get_float(args[ARG_gain].u_obj) : (mp_float_t)1.0;
    int32_t gain_q15 = (int32_t)(g * (mp_float_t)32768.0 + (mp_float_t)0.5);

    /* target<=0 keeps current; the default 0.5 maps to half of the ~2048 range. */
    int32_t target;
    if (args[ARG_target].u_obj != MP_OBJ_NULL) {
        mp_float_t t = mp_obj_get_float(args[ARG_target].u_obj);
        target = (int32_t)(t * (mp_float_t)2048.0 + (mp_float_t)0.5);
    } else {
        target = 1024;
    }

    ra_iq_adc_set_agc(m, gain_q15, target);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_iqadc_agc_obj, 1, machine_iqadc_agc);

/* agc_status() -> dict.  ALLOCATES (builds a dict): control-plane only. */
static mp_obj_t machine_iqadc_agc_status(mp_obj_t self_in) {
    (void)self_in;
    uint8_t mode;
    int32_t gain_q15;
    int32_t target;
    int32_t env;
    uint32_t clips;
    ra_iq_adc_get_agc(&mode, &gain_q15, &target, &env, &clips);

    mp_obj_t d = mp_obj_new_dict(5);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_agc_mode), mp_obj_new_int(mode));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_gain), mp_obj_new_float((mp_float_t)gain_q15 / (mp_float_t)32768.0));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_rms_target), mp_obj_new_int(target));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_rms), mp_obj_new_int(env));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_agc_clips), mp_obj_new_int(clips));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_agc_status_obj, machine_iqadc_agc_status);

/* volume(x) -> None sets the master output volume (x float, clamped 0..8); the
 * no-arg form volume() returns the current volume as a float. */
static mp_obj_t machine_iqadc_volume(size_t n_args, const mp_obj_t *args) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }

    if (n_args == 1) {
        return mp_obj_new_float((mp_float_t)ra_iq_adc_get_volume() / (mp_float_t)32768.0);
    }

    mp_float_t x = mp_obj_get_float(args[1]);
    if (x < (mp_float_t)0.0) {
        x = (mp_float_t)0.0;
    } else if (x > (mp_float_t)8.0) {
        x = (mp_float_t)8.0;
    }
    ra_iq_adc_set_volume((int32_t)(x * (mp_float_t)32768.0 + (mp_float_t)0.5));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_iqadc_volume_obj, 1, 2, machine_iqadc_volume);

/* bandwidth(hz) -> None sets the channel low-pass cutoff for the active mode (int
 * Hz; 0 = bypass).  Applied to the decimated I/Q after imbalance and before demod/
 * AGC so those stages do not work on out-of-channel noise.  Named "bandwidth", not
 * "filter" ("filter" is a frozen Python-builtin qstr).  Control-plane; the per-sample
 * cascade runs in the ADC block callback (integer only, no Python). */
static mp_obj_t machine_iqadc_bandwidth(mp_obj_t self_in, mp_obj_t hz_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }

    mp_int_t hz = mp_obj_get_int(hz_in);
    if (hz < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad bandwidth"));
    }
    ra_iq_adc_set_bandwidth((uint32_t)hz);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_iqadc_bandwidth_obj, machine_iqadc_bandwidth);

/* dec_kernel([on]) -> bool.  Hybrid CMSIS stage-1 A/B switch for the x2 decimator:
 * no arg reads the current kernel, a truthy arg selects CMSIS arm_fir_decimate_q15,
 * a falsy arg the hand integer FIR (default).  Toggling live resets iq.timing() so a
 * following iq.timing() reflects the selected kernel's per-block cost. */
static mp_obj_t machine_iqadc_dec_kernel(size_t n_args, const mp_obj_t *args) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    if (n_args >= 2) {
        ra_iq_adc_set_dec_kernel(mp_obj_is_true(args[1]) ? 1U : 0U);
    }
    return mp_obj_new_bool(ra_iq_adc_get_dec_kernel() != 0U);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_iqadc_dec_kernel_obj, 1, 2,
    machine_iqadc_dec_kernel);

/* hil_kernel([on]) -> bool.  Hybrid CMSIS stage-2 A/B switch for the SSB Hilbert:
 * no arg reads, a truthy arg selects CMSIS arm_fir_q15, falsy the hand loop (default).
 * Toggling live resets iq.timing() so a following read reflects the selected kernel. */
static mp_obj_t machine_iqadc_hil_kernel(size_t n_args, const mp_obj_t *args) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    if (n_args >= 2) {
        ra_iq_adc_set_hil_kernel(mp_obj_is_true(args[1]) ? 1U : 0U);
    }
    return mp_obj_new_bool(ra_iq_adc_get_hil_kernel() != 0U);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_iqadc_hil_kernel_obj, 1, 2,
    machine_iqadc_hil_kernel);

/* chf_kernel([on]) -> bool.  Hybrid CMSIS stage-3 A/B switch for the channel filter:
 * no arg reads, a truthy arg selects the f32 Butterworth biquad, falsy the integer
 * one-pole cascade (default).  Toggling live resets iq.timing(). */
static mp_obj_t machine_iqadc_chf_kernel(size_t n_args, const mp_obj_t *args) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    if (n_args >= 2) {
        ra_iq_adc_set_chf_kernel(mp_obj_is_true(args[1]) ? 1U : 0U);
    }
    return mp_obj_new_bool(ra_iq_adc_get_chf_kernel() != 0U);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_iqadc_chf_kernel_obj, 1, 2,
    machine_iqadc_chf_kernel);

/* mag_kernel([on]) -> bool.  Hybrid CMSIS stage-4 A/B switch for the AM envelope:
 * no arg reads, a truthy arg selects f32 arm_cmplx_mag_f32 (exact), falsy the integer
 * alpha-max-beta-min approximation (default).  Toggling live resets iq.timing(). */
static mp_obj_t machine_iqadc_mag_kernel(size_t n_args, const mp_obj_t *args) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    if (n_args >= 2) {
        ra_iq_adc_set_mag_kernel(mp_obj_is_true(args[1]) ? 1U : 0U);
    }
    return mp_obj_new_bool(ra_iq_adc_get_mag_kernel() != 0U);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_iqadc_mag_kernel_obj, 1, 2,
    machine_iqadc_mag_kernel);

/* audio_filter([name]) -> str.  Post-demod audio band-limiter preset: no arg reads the
 * current preset, an arg selects one of "off" / "am" / "voice" (alias "ssb") / "cw".
 * set_demod applies a per-mode default (AM low-pass, SSB voice band-pass, CW peak). */
static mp_obj_t machine_iqadc_audio_filter(size_t n_args, const mp_obj_t *args) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    if (n_args >= 2) {
        qstr name = mp_obj_str_get_qstr(args[1]);
        uint8_t m;
        if (name == MP_QSTR_off) {
            m = RA_IQ_AF_OFF;
        } else if (name == MP_QSTR_am) {
            m = RA_IQ_AF_AM;
        } else if ((name == MP_QSTR_voice) || (name == MP_QSTR_ssb)) {
            m = RA_IQ_AF_VOICE;
        } else if (name == MP_QSTR_cw) {
            m = RA_IQ_AF_CW;
        } else {
            mp_raise_ValueError(MP_ERROR_TEXT("unknown audio filter"));
        }
        ra_iq_adc_set_audio_filter(m);
    }
    qstr cur;
    switch (ra_iq_adc_get_audio_filter()) {
        case RA_IQ_AF_AM:    cur = MP_QSTR_am;    break;
        case RA_IQ_AF_VOICE: cur = MP_QSTR_voice; break;
        case RA_IQ_AF_CW:    cur = MP_QSTR_cw;    break;
        default:             cur = MP_QSTR_off;   break;
    }
    return MP_OBJ_NEW_QSTR(cur);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_iqadc_audio_filter_obj, 1, 2,
    machine_iqadc_audio_filter);

/* squelch([thresh]) -> dict {thresh, open, env}.  With an arg, sets the squelch
 * threshold (0 disables); always returns the current threshold, gate state and the
 * live pre-AGC envelope so a UI can tune it. */
static mp_obj_t machine_iqadc_squelch(size_t n_args, const mp_obj_t *args) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    if (n_args >= 2) {
        ra_iq_adc_set_squelch(mp_obj_get_int(args[1]));
    }
    int32_t thresh, env;
    uint8_t open;
    ra_iq_adc_get_squelch(&thresh, &open, &env);
    mp_obj_t d = mp_obj_new_dict(3);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_thresh), mp_obj_new_int(thresh));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_open), mp_obj_new_bool(open != 0U));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_env), mp_obj_new_int(env));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_iqadc_squelch_obj, 1, 2,
    machine_iqadc_squelch);

/* smeter() -> dict {rms, dbfs}.  Signal-strength readout from the channel-filtered
 * complex baseband, independent of the demod mode and the AGC (for a UI / waterfall). */
static mp_obj_t machine_iqadc_smeter(mp_obj_t self_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    int32_t rms;
    float dbfs;
    ra_iq_adc_get_smeter(&rms, &dbfs);
    mp_obj_t d = mp_obj_new_dict(2);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_rms), mp_obj_new_int(rms));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_dbfs), mp_obj_new_float(dbfs));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_smeter_obj, machine_iqadc_smeter);

/* tune([hz]) -> int.  Digital fine-tune NCO: with an arg, shifts the offset hz inside
 * the captured baseband (+/- fs/2) down to 0 Hz before the channel filter (0 = centre);
 * always returns the current, clamped offset.  Coarse tuning is the analog LO. */
static mp_obj_t machine_iqadc_tune(size_t n_args, const mp_obj_t *args) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    if (n_args >= 2) {
        ra_iq_adc_set_tune(mp_obj_get_int(args[1]));
    }
    return mp_obj_new_int(ra_iq_adc_get_tune());
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_iqadc_tune_obj, 1, 2,
    machine_iqadc_tune);

/* spectrum_bars(buf) -> int|None.  Allocation-free UI spectrum: buf is a caller
 * array('h', N); the pre-NCO capture FFT is shifted by the current tune offset, then
 * reduced 256->N, dB-scaled, and attack/release smoothed in C, filling buf with
 * int16 heights 0..50.  Returns N
 * when a fresh snapshot was written, else
 * None.  No MicroPython object is allocated -- safe to call in the zero-alloc loop. */
static mp_obj_t machine_iqadc_spectrum_bars(mp_obj_t self_in, mp_obj_t buf_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    mp_buffer_info_t bi;
    mp_get_buffer_raise(buf_in, &bi, MP_BUFFER_WRITE);
    if (bi.typecode != 'h') {
        mp_raise_ValueError(MP_ERROR_TEXT("buf must be array('h')"));
    }
    size_t n = bi.len / sizeof(int16_t);
    ra_iq_adc_spectrum_enable(1U);
    if (!ra_iq_adc_spectrum_bars((int16_t *)bi.buf, n, 50)) {
        return mp_const_none;
    }
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_iqadc_spectrum_bars_obj, machine_iqadc_spectrum_bars);

/* counters(buf) -> int.  Allocation-free counter snapshot: buf is a caller
 * array('i', >=6); filled with [blocks, overruns, unit1_stalls, audio_underruns,
 * ring_overruns, agc_clips].  Returns the count written.  Replaces the dict-returning
 * status()/audio_status()/agc_status() in the poll loop -- no allocation. */
static mp_obj_t machine_iqadc_counters(mp_obj_t self_in, mp_obj_t buf_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    mp_buffer_info_t bi;
    mp_get_buffer_raise(buf_in, &bi, MP_BUFFER_WRITE);
    if (bi.typecode != 'i' && bi.typecode != 'l') {
        mp_raise_ValueError(MP_ERROR_TEXT("buf must be array('i')"));
    }
    size_t n = bi.len / sizeof(int32_t);
    size_t c = ra_iq_adc_get_counters((int32_t *)bi.buf, n);
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)c);
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_iqadc_counters_obj, machine_iqadc_counters);

/* filter_status() -> dict {bandwidth, bypassed, fs}.  fs is the audio (decimated)
 * rate the cutoff is measured against.  ALLOCATES a dict: control-plane only. */
static mp_obj_t machine_iqadc_filter_status(mp_obj_t self_in) {
    (void)self_in;
    uint32_t fs;
    ra_iq_adc_get_audio_params(&fs, NULL);

    mp_obj_t d = mp_obj_new_dict(3);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_bandwidth), mp_obj_new_int((mp_int_t)ra_iq_adc_get_bandwidth()));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_bypassed), mp_obj_new_int(ra_iq_adc_filter_bypassed()));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_fs), mp_obj_new_int((mp_int_t)fs));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_filter_status_obj, machine_iqadc_filter_status);

/* spectrum(buf) -> bin_count | None.  buf must be array('f') (float32) with
 * len >= RA_IQ_SPECTRUM_N.  On the first call it enables spectrum accumulation in
 * the ADC block callback (a gated int16 copy; the FFT itself runs here, not in the
 * ISR).  Returns None when no completed snapshot is ready yet; otherwise fills buf
 * with the fftshift-ed magnitude spectrum (DC at buf[N/2]) and returns N.  The
 * float window + CMSIS FFT run in this call (control plane, FPU is fine). */
static mp_obj_t machine_iqadc_spectrum(mp_obj_t self_in, mp_obj_t buf_in) {
    machine_iqadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }

    mp_buffer_info_t bi;
    mp_get_buffer_raise(buf_in, &bi, MP_BUFFER_WRITE);
    if (bi.typecode != 'f') {
        mp_raise_ValueError(MP_ERROR_TEXT("buf must be array('f')"));
    }

    size_t n = ra_iq_adc_spectrum_size();
    if (bi.len < n * sizeof(float)) {
        mp_raise_ValueError(MP_ERROR_TEXT("buf too small"));
    }

    ra_iq_adc_spectrum_enable(1U);

    if (!ra_iq_adc_spectrum((float *)bi.buf, n)) {
        return mp_const_none;
    }
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_iqadc_spectrum_obj, machine_iqadc_spectrum);

static mp_obj_t machine_iqadc_spectrum_stop(mp_obj_t self_in) {
    (void)self_in;
    ra_iq_adc_spectrum_enable(0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_spectrum_stop_obj, machine_iqadc_spectrum_stop);

/* spectrum_info() -> dict {bins, bin_hz, center_hz}.  center_hz is 0 (baseband,
 * DC-centered after fftshift).  Control-plane; allocates a dict. */
static mp_obj_t machine_iqadc_spectrum_info(mp_obj_t self_in) {
    (void)self_in;
    mp_obj_t d = mp_obj_new_dict(3);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_bins), mp_obj_new_int((mp_int_t)ra_iq_adc_spectrum_size()));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_bin_hz), mp_obj_new_int((mp_int_t)ra_iq_adc_spectrum_bin_hz()));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_center_hz), mp_obj_new_int(0));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_iqadc_spectrum_info_obj, machine_iqadc_spectrum_info);

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
    { MP_ROM_QSTR(MP_QSTR_timing),       MP_ROM_PTR(&machine_iqadc_timing_obj) },
    { MP_ROM_QSTR(MP_QSTR_demod),        MP_ROM_PTR(&machine_iqadc_demod_obj) },
    { MP_ROM_QSTR(MP_QSTR_audio_status), MP_ROM_PTR(&machine_iqadc_audio_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_audio),   MP_ROM_PTR(&machine_iqadc_read_audio_obj) },
    { MP_ROM_QSTR(MP_QSTR_iq_correction), MP_ROM_PTR(&machine_iqadc_iq_correction_obj) },
    { MP_ROM_QSTR(MP_QSTR_iq_correction_status), MP_ROM_PTR(&machine_iqadc_iq_correction_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_agc),          MP_ROM_PTR(&machine_iqadc_agc_obj) },
    { MP_ROM_QSTR(MP_QSTR_agc_status),   MP_ROM_PTR(&machine_iqadc_agc_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_volume),       MP_ROM_PTR(&machine_iqadc_volume_obj) },
    { MP_ROM_QSTR(MP_QSTR_bandwidth),    MP_ROM_PTR(&machine_iqadc_bandwidth_obj) },
    { MP_ROM_QSTR(MP_QSTR_dec_kernel),   MP_ROM_PTR(&machine_iqadc_dec_kernel_obj) },
    { MP_ROM_QSTR(MP_QSTR_hil_kernel),   MP_ROM_PTR(&machine_iqadc_hil_kernel_obj) },
    { MP_ROM_QSTR(MP_QSTR_chf_kernel),   MP_ROM_PTR(&machine_iqadc_chf_kernel_obj) },
    { MP_ROM_QSTR(MP_QSTR_mag_kernel),   MP_ROM_PTR(&machine_iqadc_mag_kernel_obj) },
    { MP_ROM_QSTR(MP_QSTR_audio_filter), MP_ROM_PTR(&machine_iqadc_audio_filter_obj) },
    { MP_ROM_QSTR(MP_QSTR_squelch),      MP_ROM_PTR(&machine_iqadc_squelch_obj) },
    { MP_ROM_QSTR(MP_QSTR_smeter),       MP_ROM_PTR(&machine_iqadc_smeter_obj) },
    { MP_ROM_QSTR(MP_QSTR_tune),         MP_ROM_PTR(&machine_iqadc_tune_obj) },
    { MP_ROM_QSTR(MP_QSTR_filter_status), MP_ROM_PTR(&machine_iqadc_filter_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_spectrum),     MP_ROM_PTR(&machine_iqadc_spectrum_obj) },
    { MP_ROM_QSTR(MP_QSTR_spectrum_bars), MP_ROM_PTR(&machine_iqadc_spectrum_bars_obj) },
    { MP_ROM_QSTR(MP_QSTR_counters),     MP_ROM_PTR(&machine_iqadc_counters_obj) },
    { MP_ROM_QSTR(MP_QSTR_spectrum_stop), MP_ROM_PTR(&machine_iqadc_spectrum_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_spectrum_info), MP_ROM_PTR(&machine_iqadc_spectrum_info_obj) },
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
