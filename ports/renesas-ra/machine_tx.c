/* machine.IQTX: autonomous CW/AM/FM and C-DSP USB/LSB baseband engines. */
#include "py/runtime.h"
#include "py/binary.h"
#include "py/mperrno.h"
#include "ra/ra_tx_hw.h"
#include "ra/ra_tx_core.h"
#include "ra/ra_iq_adc.h"

#if MICROPY_HW_ENABLE_TX
typedef struct {
    mp_obj_base_t base;
    uint8_t *allocation;
    size_t allocation_bytes;
    size_t lut_bytes;
    bool active;
    ra_tx_config_t config;
} machine_tx_obj_t;

/* Native .bss is not a GC root. Keep the object AND its original allocation
 * alive through stop, partial initialization and failed checked teardown. */
MP_REGISTER_ROOT_POINTER(mp_obj_t machine_tx_owner);
MP_REGISTER_ROOT_POINTER(mp_obj_t machine_tx_file_buffers[2]);
extern const mp_obj_type_t machine_iqtx_type;

static void machine_tx_raise_error(void) {
    ra_tx_status_t s;
    ra_tx_hw_get_status(&s);
    mp_raise_OSError(s.error == RA_TX_ERROR_BUSY ? MP_EBUSY :
        s.error == RA_TX_ERROR_CONFIG ? MP_EINVAL : MP_EIO);
}

static machine_tx_obj_t *machine_tx_require_active(mp_obj_t self_in) {
    machine_tx_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active || MP_STATE_PORT(machine_tx_owner) != self_in) {
        mp_raise_OSError(MP_ENODEV);
    }
    return self;
}

static mp_obj_t machine_tx_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_mode, ARG_rate, ARG_amplitude, ARG_adc_mid, ARG_i_zero, ARG_q_zero,
        ARG_fm_gain, ARG_ramp_samples, ARG_deviation_hz, ARG_mic_gain, ARG_file_mode, ARG_file_tune };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_mode, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = RA_TX_MODE_CW} },
        { MP_QSTR_rate, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_amplitude, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 800} },
        { MP_QSTR_adc_mid, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2048} },
        { MP_QSTR_i_zero, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2048} },
        { MP_QSTR_q_zero, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2048} },
        { MP_QSTR_fm_gain, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_ramp_samples, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 220} },
        { MP_QSTR_deviation_hz, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_mic_gain, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 100} },
        { MP_QSTR_file_mode, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_file_tune, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed), allowed, args);
    if (args[ARG_rate].u_int == -1) {
        args[ARG_rate].u_int = ra_tx_mode_is_ssb(args[ARG_mode].u_int) ? RA_TX_SSB_RATE :
            (args[ARG_file_mode].u_int >= 0 ? 24000 : 44000);
    }
    /* Explicit old fm_gain keeps the raw DOC experiment compatible. New FM
     * defaults to conditioned voice with +/-2.5 kHz peak deviation. */
    if (args[ARG_deviation_hz].u_int == -1) {
        args[ARG_deviation_hz].u_int = args[ARG_mode].u_int == RA_TX_MODE_FM &&
            args[ARG_fm_gain].u_int == -1 ? 2500 : 0;
    } else if (args[ARG_deviation_hz].u_int > 0 && args[ARG_fm_gain].u_int != -1) {
        mp_raise_ValueError(MP_ERROR_TEXT("use deviation_hz OR raw fm_gain"));
    }
    if (args[ARG_fm_gain].u_int == -1) {
        args[ARG_fm_gain].u_int = 2;
    }
    if (args[ARG_file_mode].u_int < -1 || args[ARG_file_mode].u_int > RA_TX_MODE_LSB ||
        args[ARG_file_tune].u_int < -11000 || args[ARG_file_tune].u_int > 11000) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid FILE mode/tune"));
    }
    for (size_t i = 0; i < ARG_file_mode; ++i) {
        if (args[i].u_int < 0 || args[i].u_int > UINT16_MAX) {
            mp_raise_ValueError(MP_ERROR_TEXT("TX parameter out of range"));
        }
    }
    if (args[ARG_mode].u_int > RA_TX_MODE_LSB) {
        mp_raise_ValueError(MP_ERROR_TEXT("TX mode must be CW, AM, FM, USB or LSB"));
    }
    if (ra_tx_mode_is_ssb(args[ARG_mode].u_int) && args[ARG_rate].u_int != RA_TX_SSB_RATE) {
        mp_raise_ValueError(MP_ERROR_TEXT("USB/LSB requires rate=12000"));
    }
    if (args[ARG_fm_gain].u_int != 1 && args[ARG_fm_gain].u_int != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("fm_gain must be 1 or 2"));
    }
    ra_tx_config_t config = {
        .mode = args[ARG_mode].u_int,
        .sample_rate_hz = args[ARG_rate].u_int,
        .amplitude = args[ARG_amplitude].u_int,
        .adc_mid = args[ARG_adc_mid].u_int,
        .i_zero = args[ARG_i_zero].u_int,
        .q_zero = args[ARG_q_zero].u_int,
        .fm_gain = args[ARG_fm_gain].u_int,
        .ramp_samples = args[ARG_ramp_samples].u_int,
        .deviation_hz = args[ARG_deviation_hz].u_int,
        .mic_gain = args[ARG_mic_gain].u_int,
        .file_source = args[ARG_file_mode].u_int >= 0,
    };
    if (!ra_tx_core_validate(&config)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid TX rate, offset, amplitude or ramp"));
    }
    if (ra_tx_hw_owns_resources() || MP_STATE_PORT(machine_tx_owner) != MP_OBJ_NULL) {
        mp_raise_OSError(MP_EBUSY);
    }
    machine_tx_obj_t *self = mp_obj_malloc(machine_tx_obj_t, type);
    self->allocation = NULL;
    self->allocation_bytes = 0;
    self->lut_bytes = 0;
    self->active = false;
    self->config = config;
    MP_STATE_PORT(machine_tx_owner) = MP_OBJ_FROM_PTR(self);
    uint8_t *bank = NULL;
    size_t bank_bytes = ra_tx_core_lut_bytes(&config);
    size_t alignment = ra_tx_core_lut_alignment(&config);
    if (bank_bytes != 0) {
        /* The aligned interior must never be freed/rooted instead of the
         * original block. MemoryError leaves release() available for cleanup. */
        self->allocation_bytes = bank_bytes + alignment - 1U;
        self->lut_bytes = bank_bytes;
        self->allocation = m_new(uint8_t, self->allocation_bytes);
        bank = (uint8_t *)(((uintptr_t)self->allocation + alignment - 1U) &
            ~(uintptr_t)(alignment - 1U));
    }
    if (!ra_tx_hw_init(&config, bank, bank_bytes)) {
        ra_tx_status_t failure;
        ra_tx_hw_get_status(&failure);
        if (ra_tx_hw_deinit_checked()) {
            MP_STATE_PORT(machine_tx_owner) = MP_OBJ_NULL;
        }
        /* Failed teardown intentionally retains the owner and bank. */
        mp_raise_OSError(failure.error == RA_TX_ERROR_BUSY ? MP_EBUSY : MP_EIO);
    }
    self->active = true;
    if (config.file_source) {
        static const uint8_t decode_mode[] = {
            RA_IQ_DEMOD_CW, RA_IQ_DEMOD_AM, RA_IQ_DEMOD_FM, RA_IQ_DEMOD_USB, RA_IQ_DEMOD_LSB
        };
        if (!ra_iq_adc_file_decode_begin(decode_mode[args[ARG_file_mode].u_int], args[ARG_file_tune].u_int)) {
            mp_raise_OSError(MP_EBUSY); /* release() retains checked recovery */
        }
        if (ra_tx_mode_is_ssb(config.mode)) {
            ra_iq_adc_set_audio_filter(RA_IQ_AF_VOICE); /* anti-alias before 24->12 kS/s */
        }
    }
    return MP_OBJ_FROM_PTR(self);
}

static void machine_tx_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_tx_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ra_tx_status_t s;
    ra_tx_hw_get_status(&s);
    mp_printf(print, "IQTX(mode=%u, rate=%u, active=%u, running=%u)",
        (unsigned)self->config.mode, (unsigned)self->config.sample_rate_hz,
        self->active, self->active && s.running);
}

static mp_obj_t machine_tx_start(mp_obj_t self_in) {
    (void)machine_tx_require_active(self_in);
    if (!ra_tx_hw_start()) {
        machine_tx_raise_error();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_start_obj, machine_tx_start);

static mp_obj_t machine_tx_stop(mp_obj_t self_in) {
    (void)machine_tx_require_active(self_in);
    if (!ra_tx_hw_stop()) {
        machine_tx_raise_error();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_stop_obj, machine_tx_stop);

static mp_obj_t machine_tx_fm_configure(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_tx_obj_t *self = machine_tx_require_active(pos_args[0]);
    enum { ARG_deviation_hz, ARG_mic_gain, ARG_amplitude };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_deviation_hz, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_mic_gain, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_amplitude, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed), allowed, args);
    ra_tx_config_t config = self->config;
    for (unsigned i = 0; i < MP_ARRAY_SIZE(allowed); ++i) {
        if (args[i].u_int < -1 || args[i].u_int > UINT16_MAX) {
            mp_raise_ValueError(MP_ERROR_TEXT("FM control out of range"));
        }
    }
    if (args[ARG_deviation_hz].u_int != -1) { config.deviation_hz = args[ARG_deviation_hz].u_int; }
    if (args[ARG_mic_gain].u_int != -1) { config.mic_gain = args[ARG_mic_gain].u_int; }
    if (args[ARG_amplitude].u_int != -1) { config.amplitude = args[ARG_amplitude].u_int; }
    if (!ra_tx_hw_fm_configure(&config)) {
        mp_raise_ValueError(MP_ERROR_TEXT("FM controls require active voice FM and valid parameters"));
    }
    self->config = config;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_tx_fm_configure_obj, 1, machine_tx_fm_configure);

static mp_obj_t machine_tx_key(mp_obj_t self_in, mp_obj_t down_in) {
    machine_tx_obj_t *self = machine_tx_require_active(self_in);
    if (self->config.mode != RA_TX_MODE_CW) {
        mp_raise_ValueError(MP_ERROR_TEXT("key is only valid in CW"));
    }
    if (!ra_tx_hw_key(mp_obj_is_true(down_in))) {
        machine_tx_raise_error();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_tx_key_obj, machine_tx_key);

bool machine_tx_deinit_all(void) {
    if (!ra_tx_hw_deinit_checked()) {
        return false;
    }
    if (MP_STATE_PORT(machine_tx_owner) != MP_OBJ_NULL) {
        machine_tx_obj_t *self = MP_OBJ_TO_PTR(MP_STATE_PORT(machine_tx_owner));
        self->active = false;
        self->allocation = NULL;
        self->allocation_bytes = 0;
        self->lut_bytes = 0;
    }
    MP_STATE_PORT(machine_tx_owner) = MP_OBJ_NULL;
    MP_STATE_PORT(machine_tx_file_buffers)[0] = MP_OBJ_NULL;
    MP_STATE_PORT(machine_tx_file_buffers)[1] = MP_OBJ_NULL;
    return true;
}

static mp_obj_t machine_tx_deinit(mp_obj_t self_in) {
    machine_tx_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (MP_STATE_PORT(machine_tx_owner) == self_in && !machine_tx_deinit_all()) {
        machine_tx_raise_error();
    }
    self->active = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_deinit_obj, machine_tx_deinit);

/* Usable even if construction raised before the user received an object. */
static mp_obj_t machine_tx_release(void) {
    if (!machine_tx_deinit_all()) {
        machine_tx_raise_error();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(machine_tx_release_obj, machine_tx_release);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(machine_tx_release_static_obj, MP_ROM_PTR(&machine_tx_release_obj));

static mp_obj_t machine_tx_status(mp_obj_t self_in) {
    machine_tx_obj_t *self = machine_tx_require_active(self_in);
    ra_tx_status_t s;
    ra_tx_hw_get_status(&s);
    mp_obj_t result = mp_obj_new_dict(0);
    #define STORE_INT(key, val) mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_##key), mp_obj_new_int(val))
    #define STORE_BOOL(key, val) mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_##key), mp_obj_new_bool(val))
    STORE_INT(mode, s.mode);
    STORE_BOOL(owned, s.owned);
    STORE_BOOL(running, s.running);
    STORE_BOOL(keyed, s.keyed);
    STORE_BOOL(quiesced, s.quiesced);
    STORE_BOOL(outputs_enabled, s.outputs_enabled);
    STORE_BOOL(i_enabled, s.i_enabled);
    STORE_BOOL(q_enabled, s.q_enabled);
    STORE_INT(error, s.error);
    STORE_INT(fsp_error, s.fsp_error);
    STORE_INT(timer, s.timer_channel);
    STORE_INT(requested_rate, s.requested_rate_hz);
    STORE_INT(timer_clock, s.timer_clock_hz);
    STORE_INT(period_counts, s.timer_period);
    STORE_INT(last_adc, s.last_adc);
    STORE_INT(phase, s.phase);
    STORE_INT(i_code, s.i_code);
    STORE_INT(q_code, s.q_code);
    STORE_INT(transfer_count, s.transfer_count);
    STORE_INT(unexpected_callbacks, s.unexpected_irqs);
    STORE_BOOL(cpu_dsp, s.cpu_dsp);
    STORE_INT(dsp_samples, s.dsp_samples);
    STORE_INT(dsp_last_cycles, s.dsp_last_cycles);
    STORE_INT(dsp_max_cycles, s.dsp_max_cycles);
    STORE_INT(dsp_budget_cycles, s.dsp_budget_cycles);
    STORE_INT(dsp_deadline_misses, s.dsp_deadline_misses);
    STORE_INT(dsp_clips, s.dsp_clips);
    STORE_INT(adc_rails, s.adc_rails);
    STORE_INT(dc_estimate, s.dc_estimate);
    STORE_INT(audio_peak, s.audio_peak);
    STORE_INT(af_frames, s.af_frames);
    STORE_INT(af_error, s.af_error);
    STORE_BOOL(af_enabled, s.af_enabled);
    STORE_BOOL(file_source, s.file_source);
    STORE_INT(file_underruns, s.file_underruns);
    STORE_INT(deviation_hz, self->config.deviation_hz);
    STORE_INT(mic_gain, self->config.mic_gain);
    STORE_INT(amplitude, self->config.amplitude);
    STORE_INT(lut_bytes, self->lut_bytes);
    STORE_INT(lut_allocation_bytes, self->allocation == NULL ? 0 : self->allocation_bytes);
    STORE_INT(adc_mid, self->config.adc_mid);
    STORE_INT(fm_gain, self->config.fm_gain);
    mp_float_t rate = s.timer_period ? (mp_float_t)s.timer_clock_hz / s.timer_period : 0;
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_actual_rate), mp_obj_new_float(rate));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_cw_pin), mp_const_none);
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_mic_pin), MP_OBJ_NEW_QSTR(MP_QSTR_P001));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_i_pin), MP_OBJ_NEW_QSTR(MP_QSTR_P014));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_q_pin), MP_OBJ_NEW_QSTR(MP_QSTR_P015));
    #undef STORE_INT
    #undef STORE_BOOL
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_status_obj, machine_tx_status);

/* Same borrowed-buffer protocol as IQADC, serviced by the App's independent
 * 20-ms FILE timer. No file I/O or Python callback runs in the sample IRQ. */
static void machine_tx_require_file(mp_obj_t self_in) {
    if (!machine_tx_require_active(self_in)->config.file_source) {
        mp_raise_ValueError(MP_ERROR_TEXT("TX source is MIC"));
    }
}

static mp_obj_t machine_tx_file_attach(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    machine_tx_require_file(args[0]);
    ra_tx_status_t st;
    ra_tx_hw_get_status(&st);
    mp_int_t point = mp_obj_get_int(args[3]);
    if (st.running || point < 0 || point > RA_IQ_INJECT_POINT_OUT) {
        mp_raise_ValueError(MP_ERROR_TEXT("TX FILE needs stopped TX and valid point"));
    }
    mp_buffer_info_t a, b;
    mp_get_buffer_raise(args[1], &a, MP_BUFFER_WRITE);
    mp_get_buffer_raise(args[2], &b, MP_BUFFER_WRITE);
    bool a_byte = a.typecode == BYTEARRAY_TYPECODE || a.typecode == 'B' || a.typecode == 'b';
    bool b_byte = b.typecode == BYTEARRAY_TYPECODE || b.typecode == 'B' || b.typecode == 'b';
    if (a.len != b.len || !a_byte || !b_byte ||
        !ra_iq_adc_file_attach(a.buf, b.buf, a.len, point, NULL, NULL)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid TX FILE buffers"));
    }
    MP_STATE_PORT(machine_tx_file_buffers)[0] = args[1];
    MP_STATE_PORT(machine_tx_file_buffers)[1] = args[2];
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_tx_file_attach_obj, 5, 5, machine_tx_file_attach);

static mp_obj_t machine_tx_file_commit(mp_obj_t self, mp_obj_t index, mp_obj_t size) {
    machine_tx_require_file(self);
    mp_int_t i = mp_obj_get_int(index), n = mp_obj_get_int(size);
    if (i < 0 || i > 1 || n < 0 || !ra_iq_adc_file_commit(i, n)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid TX FILE commit"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_tx_file_commit_obj, machine_tx_file_commit);

static mp_obj_t machine_tx_file_start(mp_obj_t self) {
    machine_tx_require_file(self);
    ra_tx_status_t st;
    ra_tx_hw_get_status(&st);
    if (st.running) {
        mp_raise_OSError(MP_EBUSY);
    }
    if (!ra_iq_adc_file_start()) {
        mp_raise_ValueError(MP_ERROR_TEXT("TX FILE not attached"));
    }
    (void)ra_iq_adc_file_decode_service(); /* prefill before enabling AGT */
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_file_start_obj, machine_tx_file_start);

static mp_obj_t machine_tx_file_service(mp_obj_t self) {
    machine_tx_require_file(self);
    return MP_OBJ_NEW_SMALL_INT(ra_iq_adc_file_decode_service());
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_file_service_obj, machine_tx_file_service);

static mp_obj_t machine_tx_file_free(mp_obj_t self) {
    machine_tx_require_file(self);
    ra_tx_status_t st;
    ra_tx_hw_get_status(&st);
    if (st.running) {
        mp_raise_OSError(MP_EBUSY);
    }
    ra_iq_adc_file_detach();
    MP_STATE_PORT(machine_tx_file_buffers)[0] = MP_OBJ_NULL;
    MP_STATE_PORT(machine_tx_file_buffers)[1] = MP_OBJ_NULL;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_file_free_obj, machine_tx_file_free);

static mp_obj_t machine_tx_file_status_into(mp_obj_t self, mp_obj_t target) {
    machine_tx_require_file(self);
    mp_buffer_info_t buf;
    mp_get_buffer_raise(target, &buf, MP_BUFFER_WRITE);
    if (buf.len < 15 * sizeof(int32_t) || buf.typecode != 'i' || ((uintptr_t)buf.buf & 3U)) {
        mp_raise_ValueError(MP_ERROR_TEXT("FILE status needs array i[15]"));
    }
    ra_iq_file_status_t s;
    ra_iq_adc_file_get_status(&s);
    int32_t *v = buf.buf;
    v[0] = s.attached; v[1] = s.requested_on; v[2] = s.active_on; v[3] = s.point;
    v[4] = s.state[0]; v[5] = s.state[1]; v[6] = s.valid_bytes[0]; v[7] = s.valid_bytes[1];
    v[8] = s.active_index; v[9] = s.active_offset_bytes; v[10] = s.underruns;
    v[11] = s.source_blocks; v[12] = s.samples_consumed; v[13] = 0; v[14] = 0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_tx_file_status_into_obj, machine_tx_file_status_into);

static const mp_rom_map_elem_t machine_tx_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_CW), MP_ROM_INT(RA_TX_MODE_CW) },
    { MP_ROM_QSTR(MP_QSTR_AM), MP_ROM_INT(RA_TX_MODE_AM) },
    { MP_ROM_QSTR(MP_QSTR_FM), MP_ROM_INT(RA_TX_MODE_FM) },
    { MP_ROM_QSTR(MP_QSTR_USB), MP_ROM_INT(RA_TX_MODE_USB) },
    { MP_ROM_QSTR(MP_QSTR_LSB), MP_ROM_INT(RA_TX_MODE_LSB) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&machine_tx_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&machine_tx_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_key), MP_ROM_PTR(&machine_tx_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&machine_tx_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_fm_configure), MP_ROM_PTR(&machine_tx_fm_configure_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_tx_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_release), MP_ROM_PTR(&machine_tx_release_static_obj) },
    { MP_ROM_QSTR(MP_QSTR_FILE_API_VERSION), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_FILE_FREE), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_FILE_READY), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_FILE_ACTIVE), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_file_attach), MP_ROM_PTR(&machine_tx_file_attach_obj) },
    { MP_ROM_QSTR(MP_QSTR_file_commit), MP_ROM_PTR(&machine_tx_file_commit_obj) },
    { MP_ROM_QSTR(MP_QSTR_file_start), MP_ROM_PTR(&machine_tx_file_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_file_service), MP_ROM_PTR(&machine_tx_file_service_obj) },
    { MP_ROM_QSTR(MP_QSTR_file_stop), MP_ROM_PTR(&machine_tx_file_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_file_free), MP_ROM_PTR(&machine_tx_file_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_file_status_into), MP_ROM_PTR(&machine_tx_file_status_into_obj) },
};
static MP_DEFINE_CONST_DICT(machine_tx_locals_dict, machine_tx_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_iqtx_type,
    MP_QSTR_IQTX,
    MP_TYPE_FLAG_NONE,
    make_new, machine_tx_make_new,
    print, machine_tx_print,
    locals_dict, &machine_tx_locals_dict
    );
#endif
