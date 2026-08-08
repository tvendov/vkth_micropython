/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Vekatech Ltd.
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

// DSP module for Renesas RA Cortex-M4/M33 parts with FPU.
// Wraps ARM CMSIS-DSP float32 functions:
//   dsp.FIRFilter  — arm_fir_f32      (windowed-sinc FIR)
//   dsp.IIRFilter  — arm_biquad_cascade_df1_f32  (SOS biquad IIR)
//   dsp.rms(buf)   — arm_rms_f32      (RMS of a block)
//   dsp.power(buf) — arm_power_f32    (sum of squares)
//
// Compile-time guard: only included when MICROPY_HW_ENABLE_DSP == 1
// (set in boards/VK_RA4M2/mpconfigboard.h and Makefile).

#if MICROPY_HW_ENABLE_DSP

#include <math.h>
#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/binary.h"

// Board pin aliases collide with CMSIS-DSP PID structure field names.
#undef A0
#undef A1
#undef A2
#include "arm_math.h"

// ---------------------------------------------------------------------------
// Helper: extract float32 buffer from a MicroPython object (array, bytearray,
//         memoryview).  Returns pointer and element count.  Raises TypeError
//         if the buffer is not a valid float32 array.
// ---------------------------------------------------------------------------
static const float32_t *get_float32_buf(mp_obj_t obj, size_t *out_len) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.typecode != 'f') {
        mp_raise_TypeError(MP_ERROR_TEXT("expected array('f')"));
    }
    *out_len = bufinfo.len / sizeof(float32_t);
    return (const float32_t *)bufinfo.buf;
}

static float32_t *get_float32_buf_rw(mp_obj_t obj, size_t *out_len) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(obj, &bufinfo, MP_BUFFER_RW);
    if (bufinfo.typecode != 'f') {
        mp_raise_TypeError(MP_ERROR_TEXT("expected array('f')"));
    }
    *out_len = bufinfo.len / sizeof(float32_t);
    return (float32_t *)bufinfo.buf;
}

// ===========================================================================
// FIRFilter
// ===========================================================================
// Python:
//   f = dsp.FIRFilter(coeffs, block_size)
//   f.process(src, dst)
//
// coeffs   — array('f') of length numTaps, stored in time-reversed order
//            {h[N-1], ..., h[0]} as required by CMSIS-DSP.
// block_size — number of samples processed per call to process().
// State buffer is allocated from MicroPython heap.
//
// Note on sign convention: coefficients are passed directly to arm_fir_f32;
// no sign negation is applied (FIR has no feedback).

typedef struct _dsp_fir_obj_t {
    mp_obj_base_t base;
    arm_fir_instance_f32 S;
    float32_t *state;       // heap-allocated: numTaps + blockSize - 1
    float32_t *coeffs;      // heap-allocated copy of reversed coefficients
    uint16_t num_taps;
    uint32_t block_size;
} dsp_fir_obj_t;

static mp_obj_t dsp_fir_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 2, 2, false);

    size_t num_taps;
    const float32_t *src_coeffs = get_float32_buf(args[0], &num_taps);
    uint32_t block_size = mp_obj_get_int(args[1]);

    if (num_taps == 0 || block_size == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("num_taps and block_size must be > 0"));
    }

    dsp_fir_obj_t *self = mp_obj_malloc(dsp_fir_obj_t, type);
    self->num_taps   = (uint16_t)num_taps;
    self->block_size = block_size;

    // Copy coefficients to heap (CMSIS-DSP needs a persistent pointer)
    self->coeffs = m_new(float32_t, num_taps);
    memcpy(self->coeffs, src_coeffs, num_taps * sizeof(float32_t));

    // State buffer: numTaps + blockSize - 1
    size_t state_len = num_taps + block_size - 1;
    self->state = m_new0(float32_t, state_len);

    arm_fir_init_f32(&self->S, (uint16_t)num_taps, self->coeffs,
        self->state, block_size);

    return MP_OBJ_FROM_PTR(self);
}

// process(src, dst)
static mp_obj_t dsp_fir_process(mp_obj_t self_in, mp_obj_t src_in, mp_obj_t dst_in) {
    dsp_fir_obj_t *self = MP_OBJ_TO_PTR(self_in);
    size_t src_len, dst_len;
    const float32_t *src = get_float32_buf(src_in, &src_len);
    float32_t *dst = get_float32_buf_rw(dst_in, &dst_len);

    if (src_len < self->block_size || dst_len < self->block_size) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too small for block_size"));
    }
    arm_fir_f32(&self->S, src, dst, self->block_size);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(dsp_fir_process_obj, dsp_fir_process);

static const mp_rom_map_elem_t dsp_fir_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_process), MP_ROM_PTR(&dsp_fir_process_obj) },
};
static MP_DEFINE_CONST_DICT(dsp_fir_locals, dsp_fir_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_fir_type,
    MP_QSTR_FIRFilter,
    MP_TYPE_FLAG_NONE,
    make_new, dsp_fir_make_new,
    locals_dict, &dsp_fir_locals
    );

// ===========================================================================
// IIRFilter  (biquad cascade DF1)
// ===========================================================================
// Python:
//   f = dsp.IIRFilter(coeffs, num_stages, block_size)
//   f.process(src, dst)
//
// coeffs     — array('f') of length 5*num_stages.
//              Per stage: {b0, b1, b2, a1_cmsis, a2_cmsis}
//              SIGN CONVENTION: a1_cmsis = -a1_scipy, a2_cmsis = -a2_scipy
// num_stages — number of biquad (SOS) stages
// block_size — samples per process() call
// State buffer: 4 * num_stages floats, zeroed on init.

typedef struct _dsp_iir_obj_t {
    mp_obj_base_t base;
    arm_biquad_casd_df1_inst_f32 S;
    float32_t *state;       // heap: 4 * num_stages
    float32_t *coeffs;      // heap copy: 5 * num_stages
    uint8_t num_stages;
    uint32_t block_size;
} dsp_iir_obj_t;

static mp_obj_t dsp_iir_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 3, 3, false);

    size_t coeff_len;
    const float32_t *src_coeffs = get_float32_buf(args[0], &coeff_len);
    uint32_t num_stages  = mp_obj_get_int(args[1]);
    uint32_t block_size  = mp_obj_get_int(args[2]);

    if (num_stages == 0 || block_size == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("num_stages and block_size must be > 0"));
    }
    if (coeff_len < 5u * num_stages) {
        mp_raise_ValueError(MP_ERROR_TEXT("coeffs too short: need 5*num_stages elements"));
    }

    dsp_iir_obj_t *self = mp_obj_malloc(dsp_iir_obj_t, type);
    self->num_stages = (uint8_t)num_stages;
    self->block_size = block_size;

    self->coeffs = m_new(float32_t, 5 * num_stages);
    memcpy(self->coeffs, src_coeffs, 5 * num_stages * sizeof(float32_t));

    self->state = m_new0(float32_t, 4 * num_stages);

    arm_biquad_cascade_df1_init_f32(&self->S, (uint8_t)num_stages,
        self->coeffs, self->state);

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t dsp_iir_process(mp_obj_t self_in, mp_obj_t src_in, mp_obj_t dst_in) {
    dsp_iir_obj_t *self = MP_OBJ_TO_PTR(self_in);
    size_t src_len, dst_len;
    const float32_t *src = get_float32_buf(src_in, &src_len);
    float32_t *dst = get_float32_buf_rw(dst_in, &dst_len);

    if (src_len < self->block_size || dst_len < self->block_size) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too small for block_size"));
    }
    arm_biquad_cascade_df1_f32(&self->S, src, dst, self->block_size);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(dsp_iir_process_obj, dsp_iir_process);

static const mp_rom_map_elem_t dsp_iir_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_process), MP_ROM_PTR(&dsp_iir_process_obj) },
};
static MP_DEFINE_CONST_DICT(dsp_iir_locals, dsp_iir_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_iir_type,
    MP_QSTR_IIRFilter,
    MP_TYPE_FLAG_NONE,
    make_new, dsp_iir_make_new,
    locals_dict, &dsp_iir_locals
    );

// ===========================================================================
// Module-level functions: rms(buf) and power(buf)
// ===========================================================================
// rms(buf)   — returns float: sqrt(sum(x²)/N)
// power(buf) — returns float: sum(x²)
//
// Both accept array('f') or any readable float32 buffer.

static mp_obj_t dsp_rms(mp_obj_t buf_in) {
    size_t n;
    const float32_t *buf = get_float32_buf(buf_in, &n);
    if (n == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("empty buffer"));
    }
    float32_t result;
    arm_rms_f32(buf, (uint32_t)n, &result);
    return mp_obj_new_float(result);
}
static MP_DEFINE_CONST_FUN_OBJ_1(dsp_rms_obj, dsp_rms);

static mp_obj_t dsp_power(mp_obj_t buf_in) {
    size_t n;
    const float32_t *buf = get_float32_buf(buf_in, &n);
    if (n == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("empty buffer"));
    }
    float32_t result;
    arm_power_f32(buf, (uint32_t)n, &result);
    return mp_obj_new_float(result);
}
static MP_DEFINE_CONST_FUN_OBJ_1(dsp_power_obj, dsp_power);

// ===========================================================================
// FFT  (real forward FFT, arm_rfft_fast_f32)
// ===========================================================================
// Python:
//   f = dsp.FFT(fft_len)          # fft_len must be power-of-2, 32..4096
//   f.run(src_f, dst_f)           # 128 float in -> 128 float out (packed complex)
//   f.magnitude(cx_f, mag_f)      # packed complex -> N/2+1 magnitudes
//   f.window_apply(src_f, dst_f)  # multiply src by Hamming window -> dst
//
// Output format of arm_rfft_fast_f32 (N=128):
//   dst[0]   = DC    (real only)
//   dst[1]   = Nyquist (real only, packed into Im slot of bin 0)
//   dst[2..N-1] = complex pairs [Re1,Im1, Re2,Im2, ..., Re(N/2-1),Im(N/2-1)]
//
// magnitude() handles this correctly:
//   mag[0]    = |DC|
//   mag[N/2]  = |Nyquist|
//   mag[1..N/2-1] = arm_cmplx_mag_f32 of dst[2..N-1]

#define DSP_FFT_MAX_BANDS (32u)

typedef struct _dsp_fft_obj_t {
    mp_obj_base_t base;
    arm_rfft_fast_instance_f32 S;
    float32_t *window;              // Hamming window, length fft_len, heap-alloc
    uint16_t   fft_len;
    // Band boundaries — precomputed by set_bands(), zero powf() in hot path
    uint16_t   band_lo[DSP_FFT_MAX_BANDS];
    uint16_t   band_hi[DSP_FFT_MAX_BANDS];
    uint8_t    n_bands;             // 0 = not configured
    // dBFS scaling — precomputed by set_bands(n, db_floor=-60)
    float32_t  db_floor;            // noise floor in dBFS  (e.g. -60.0)
    float32_t  db_scale;            // 255 / (-db_floor)    (e.g.   4.25)
} dsp_fft_obj_t;

static mp_obj_t dsp_fft_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    uint32_t fft_len = (uint32_t)mp_obj_get_int(args[0]);

    // arm_rfft_fast_init_f32 supports: 32,64,128,256,512,1024,2048,4096
    if (fft_len < 32 || fft_len > 4096 || (fft_len & (fft_len - 1)) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("fft_len must be power-of-2, 32..4096"));
    }

    dsp_fft_obj_t *self = mp_obj_malloc(dsp_fft_obj_t, type);
    self->fft_len = (uint16_t)fft_len;
    self->n_bands = 0;

    if (arm_rfft_fast_init_f32(&self->S, fft_len) != ARM_MATH_SUCCESS) {
        mp_raise_ValueError(MP_ERROR_TEXT("FFT init failed"));
    }

    // Precompute Hamming window: w[n] = 0.54 - 0.46*cos(2π*n/(N-1))
    self->window = m_new(float32_t, fft_len);
    float32_t scale = (float32_t)(2.0 * M_PI / (fft_len - 1));
    for (uint32_t i = 0; i < fft_len; ++i) {
        self->window[i] = 0.54f - 0.46f * cosf(scale * (float32_t)i);
    }
    return MP_OBJ_FROM_PTR(self);
}

// run(src_f, dst_f) — forward real FFT, src and dst must be array('f', fft_len)
static mp_obj_t dsp_fft_run(mp_obj_t self_in, mp_obj_t src_in, mp_obj_t dst_in) {
    dsp_fft_obj_t *self = MP_OBJ_TO_PTR(self_in);
    size_t src_len, dst_len;
    const float32_t *src = get_float32_buf(src_in, &src_len);
    float32_t       *dst = get_float32_buf_rw(dst_in, &dst_len);
    if (src_len < self->fft_len || dst_len < self->fft_len) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too small"));
    }
    // arm_rfft_fast_f32 requires non-const input (may modify src); copy to dst first
    memcpy(dst, src, self->fft_len * sizeof(float32_t));
    arm_rfft_fast_f32(&self->S, dst, dst, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(dsp_fft_run_obj, dsp_fft_run);

// magnitude(cx_f, mag_f) — packed rfft output -> N/2+1 magnitudes
// cx_f:  array('f', fft_len),   mag_f: array('f', fft_len/2 + 1)
static mp_obj_t dsp_fft_magnitude(mp_obj_t self_in, mp_obj_t cx_in, mp_obj_t mag_in) {
    dsp_fft_obj_t *self = MP_OBJ_TO_PTR(self_in);
    size_t cx_len, mag_len;
    const float32_t *cx  = get_float32_buf(cx_in, &cx_len);
    float32_t       *mag = get_float32_buf_rw(mag_in, &mag_len);
    uint32_t half = self->fft_len / 2;
    if (cx_len < self->fft_len || mag_len < half + 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too small"));
    }
    mag[0]    = fabsf(cx[0]);                          // DC
    mag[half] = fabsf(cx[1]);                          // Nyquist
    arm_cmplx_mag_f32(&cx[2], &mag[1], half - 1);     // bins 1..N/2-1
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(dsp_fft_magnitude_obj, dsp_fft_magnitude);

// window_apply(src_f, dst_f) — multiply src by precomputed Hamming window
static mp_obj_t dsp_fft_window_apply(mp_obj_t self_in, mp_obj_t src_in, mp_obj_t dst_in) {
    dsp_fft_obj_t *self = MP_OBJ_TO_PTR(self_in);
    size_t src_len, dst_len;
    const float32_t *src = get_float32_buf(src_in, &src_len);
    float32_t       *dst = get_float32_buf_rw(dst_in, &dst_len);
    if (src_len < self->fft_len || dst_len < self->fft_len) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too small"));
    }
    arm_mult_f32(src, self->window, dst, self->fft_len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(dsp_fft_window_apply_obj, dsp_fft_window_apply);

// set_bands(n, db_floor=-60) — precompute log-spaced bin boundaries + dBFS scale.
// n        : number of bands 1..32
// db_floor : noise floor in dBFS, e.g. -60 (default), -40 (less sensitive), -80 (more)
// Calls powf() n+1 times — call ONCE before the main loop, not inside it.
static mp_obj_t dsp_fft_set_bands(size_t n_args, const mp_obj_t *args) {
    dsp_fft_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint32_t n = (uint32_t)mp_obj_get_int(args[1]);
    if (n == 0 || n > DSP_FFT_MAX_BANDS) {
        mp_raise_ValueError(MP_ERROR_TEXT("n_bands must be 1..32"));
    }

    // Optional db_floor parameter — default -60 dBFS.
    float32_t db_floor = (n_args > 2) ? (float32_t)mp_obj_get_float(args[2]) : -60.0f;
    if (db_floor >= 0.0f) {
        mp_raise_ValueError(MP_ERROR_TEXT("db_floor must be negative (e.g. -60)"));
    }
    self->db_floor = db_floor;
    self->db_scale = 255.0f / (-db_floor);   // precompute: counts per dB

    uint32_t half = self->fft_len / 2;        // usable bins 1..half
    float32_t lo   = 1.0f;
    float32_t hi   = (float32_t)half;
    float32_t step = powf(hi / lo, 1.0f / (float32_t)n);

    for (uint32_t b = 0; b < n; ++b) {
        uint32_t blo = (uint32_t)(lo * powf(step, (float32_t)b));
        uint32_t bhi = (uint32_t)(lo * powf(step, (float32_t)(b + 1)));
        if (blo < 1)    { blo = 1; }
        if (bhi > half) { bhi = half; }
        if (bhi <= blo) { bhi = blo + 1; }
        self->band_lo[b] = (uint16_t)blo;
        self->band_hi[b] = (uint16_t)bhi;
    }
    self->n_bands = (uint8_t)n;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(dsp_fft_set_bands_obj, 2, 3, dsp_fft_set_bands);

// bands(mag_f, out_b) — map magnitudes to n_bands output bytes using dBFS scale.
// ZERO powf() — uses precomputed boundaries and db_floor from set_bands().
// dBFS scale: 0 dBFS (full-scale sine) → 255, db_floor dBFS → 0.
// Reference: ref = (fft_len/2) * 0.54  (Hamming coherent gain for full-scale input).

static mp_obj_t dsp_fft_bands(mp_obj_t self_in, mp_obj_t mag_in, mp_obj_t out_in) {
    dsp_fft_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->n_bands == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("call set_bands(n) first"));
    }
    size_t mag_len;
    const float32_t *mag = get_float32_buf(mag_in, &mag_len);
    if (mag_len < (size_t)(self->fft_len / 2 + 1)) {
        mp_raise_ValueError(MP_ERROR_TEXT("mag buf too small"));
    }
    mp_buffer_info_t outinfo;
    mp_get_buffer_raise(out_in, &outinfo, MP_BUFFER_WRITE);
    if (outinfo.len < self->n_bands) {
        mp_raise_ValueError(MP_ERROR_TEXT("out buf too small"));
    }
    uint8_t *out = (uint8_t *)outinfo.buf;

    // ref: peak magnitude of full-scale sine after Hamming window (fft_len-dependent).
    // ref = (fft_len/2) * hamming_coherent_gain(0.54).
    const float32_t ref = (float32_t)(self->fft_len >> 1) * 0.54f;

    for (uint8_t b = 0; b < self->n_bands; ++b) {
        uint16_t lo = self->band_lo[b];
        uint16_t hi = self->band_hi[b];
        float32_t sum = 0.0f;
        for (uint16_t k = lo; k < hi; ++k) { sum += mag[k]; }
        float32_t avg = sum / (float32_t)(hi - lo);

        // dBFS: add epsilon to avoid log10(0) = -inf.
        float32_t db = 20.0f * log10f(avg / ref + 1e-7f);

        // Map [db_floor .. 0] dBFS → [0 .. 255], clamp outside range.
        float32_t fv = (db - self->db_floor) * self->db_scale;
        out[b] = (fv <= 0.0f) ? 0u : (fv >= 255.0f) ? 255u : (uint8_t)fv;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(dsp_fft_bands_obj, dsp_fft_bands);

static const mp_rom_map_elem_t dsp_fft_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_run),          MP_ROM_PTR(&dsp_fft_run_obj) },
    { MP_ROM_QSTR(MP_QSTR_magnitude),    MP_ROM_PTR(&dsp_fft_magnitude_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_apply), MP_ROM_PTR(&dsp_fft_window_apply_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_bands),    MP_ROM_PTR(&dsp_fft_set_bands_obj) },
    { MP_ROM_QSTR(MP_QSTR_bands),        MP_ROM_PTR(&dsp_fft_bands_obj) },
};
static MP_DEFINE_CONST_DICT(dsp_fft_locals, dsp_fft_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_fft_type,
    MP_QSTR_FFT,
    MP_TYPE_FLAG_NONE,
    make_new, dsp_fft_make_new,
    locals_dict, &dsp_fft_locals
);

// ===========================================================================
// dsp.bands(mag_f, out_b, n_bands) — log-spaced bin averaging -> bytearray
// ===========================================================================
// mag_f:   array('f', N/2+1) from fft.magnitude() — bins 0..N/2
// out_b:   bytearray(n_bands) — output 0-255 per band
// n_bands: number of output bands (default 7)
// Mapping: log-spaced from bin 1 to bin N/2 (skips DC bin 0)
// Peak tracking: normalises to running max for auto-gain

static mp_obj_t dsp_bands(size_t n_args, const mp_obj_t *args) {
    if (n_args < 2 || n_args > 3) {
        mp_raise_TypeError(MP_ERROR_TEXT("bands(mag_f, out_b [, n_bands])"));
    }
    size_t mag_len;
    const float32_t *mag = get_float32_buf(args[0], &mag_len);

    mp_buffer_info_t outinfo;
    mp_get_buffer_raise(args[1], &outinfo, MP_BUFFER_WRITE);
    uint8_t *out = (uint8_t *)outinfo.buf;
    size_t n_bands = outinfo.len;
    if (n_args == 3) {
        n_bands = (size_t)mp_obj_get_int(args[2]);
    }
    if (n_bands == 0 || n_bands > outinfo.len) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad n_bands"));
    }

    uint32_t n_bins = (uint32_t)(mag_len - 1); // usable bins 1..mag_len-1
    if (n_bins < 2) { return mp_const_none; }

    float32_t lo = 1.0f;
    float32_t hi = (float32_t)n_bins;
    float32_t step = powf(hi / lo, 1.0f / (float32_t)n_bands);

    for (size_t b = 0; b < n_bands; ++b) {
        uint32_t bin_lo = (uint32_t)(lo * powf(step, (float32_t)b));
        uint32_t bin_hi = (uint32_t)(lo * powf(step, (float32_t)(b + 1)));
        if (bin_lo < 1)      { bin_lo = 1; }
        if (bin_hi > n_bins) { bin_hi = n_bins; }
        if (bin_hi <= bin_lo) { bin_hi = bin_lo + 1; }
        float32_t sum = 0.0f;
        for (uint32_t k = bin_lo; k < bin_hi; ++k) { sum += mag[k]; }
        float32_t avg = sum / (float32_t)(bin_hi - bin_lo);
        // Scale to 0-255: clip at a reasonable max (tune per application)
        int32_t v = (int32_t)(avg * 128.0f);
        out[b] = (v < 0) ? 0 : (v > 255) ? 255 : (uint8_t)v;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(dsp_bands_obj, 2, 3, dsp_bands);

// ===========================================================================
// Module definition
// ===========================================================================
static const mp_rom_map_elem_t dsp_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_dsp) },
    { MP_ROM_QSTR(MP_QSTR_FIRFilter), MP_ROM_PTR(&dsp_fir_type) },
    { MP_ROM_QSTR(MP_QSTR_IIRFilter), MP_ROM_PTR(&dsp_iir_type) },
    { MP_ROM_QSTR(MP_QSTR_FFT),       MP_ROM_PTR(&dsp_fft_type) },
    { MP_ROM_QSTR(MP_QSTR_rms),       MP_ROM_PTR(&dsp_rms_obj)  },
    { MP_ROM_QSTR(MP_QSTR_power),     MP_ROM_PTR(&dsp_power_obj) },
    { MP_ROM_QSTR(MP_QSTR_bands),     MP_ROM_PTR(&dsp_bands_obj) },
};
static MP_DEFINE_CONST_DICT(dsp_module_globals, dsp_module_globals_table);

const mp_obj_module_t mp_module_dsp = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dsp_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_dsp, mp_module_dsp);

#endif // MICROPY_HW_ENABLE_DSP
