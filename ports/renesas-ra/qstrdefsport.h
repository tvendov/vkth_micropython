/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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

// qstrs specific to this port
// *FORMAT-OFF*

// Entries for sys.path
Q(/flash)
Q(/flash/lib)
Q(/sd)
Q(/sd/lib)

// For os.sep
Q(/)

// For machine.ADC vref selection.
Q(vref)
Q(avcc)
Q(external)
Q(internal)
Q(REF_AVCC)
Q(REF_EXTERNAL)
Q(REF_INTERNAL)

// For machine.ADC PGA support on RA6M3.
Q(pga_supported)
Q(pga)
Q(set_gain)
Q(gain)
Q(bypass)
Q(differential)
Q(PGA_OFF)
Q(PGA_BYPASS)
Q(PGA_SINGLE)
Q(PGA_DIFFERENTIAL)
Q(PGA_GAIN_2_000)
Q(PGA_GAIN_2_500)
Q(PGA_GAIN_2_667)
Q(PGA_GAIN_2_857)
Q(PGA_GAIN_3_077)
Q(PGA_GAIN_3_333)
Q(PGA_GAIN_3_636)
Q(PGA_GAIN_4_000)
Q(PGA_GAIN_4_444)
Q(PGA_GAIN_5_000)
Q(PGA_GAIN_5_714)
Q(PGA_GAIN_6_667)
Q(PGA_GAIN_8_000)
Q(PGA_GAIN_10_000)
Q(PGA_GAIN_13_333)
Q(PGA_DIFF_GAIN_1_500)
Q(PGA_DIFF_GAIN_2_333)
Q(PGA_DIFF_GAIN_4_000)
Q(PGA_DIFF_GAIN_5_667)

// For machine.IQADC coherent I/Q capture on RA6M3.
Q(IQADC)
Q(i_pin)
Q(q_pin)
Q(rate)
Q(block)
Q(read_block)
Q(blocks)
Q(overruns)
Q(unit1_stalls)
Q(last_error)
Q(initialised)
Q(dsp_status)
Q(dsp_blocks)
Q(dsp_samples)
Q(i_mean)
Q(q_mean)
Q(demod)
Q(audio_status)
Q(read_audio)
Q(audio_underruns)
Q(ring_overruns)
Q(iq_correction)
Q(iq_correction_status)
Q(amp)
Q(phase)
Q(correcting)
Q(stream_from)
// iq_adc SSB demod modes; "am"/"off" are auto-collected elsewhere, "usb"/"lsb"/"cw" are not
Q(usb)
Q(lsb)
Q(cw)
// iq_adc audio-output stages (RMS AGC + master volume).  Method/kw/key names are
// SDR-unique to dodge the frozen asyncio/LVGL qstr collisions that bite bare words
// (mode/target/env/clips/slow/manual).  Mode strings are compared with strcmp, so
// off/fast/slow/manual need no qstrs.  "gain" is declared above.
Q(agc)
Q(agc_status)
Q(volume)
Q(agc_mode)
Q(rms_target)
Q(rms)
Q(agc_clips)
// iq_adc channel low-pass filter.  "bandwidth"/"filter_status"/"bypassed"/"fs" are
// SDR-unique; the method is named "bandwidth" (not "filter", a frozen builtin qstr).
Q(bandwidth)
Q(filter_status)
Q(bypassed)
Q(fs)
// iq_adc spectrum (FFT) path.  All SDR-unique; none collide with frozen qstrs.
Q(spectrum)
Q(spectrum_stop)
Q(spectrum_info)
Q(bins)
Q(bin_hz)
Q(center_hz)
// iq_adc DSP timing (DWT cycle budget)
Q(timing)
Q(last_cyc)
Q(max_cyc)
Q(avg_cyc)
Q(block_cyc)
Q(cpu_hz)
Q(max_pct)

#if MICROPY_HW_ENABLE_USB
// for usb modes
Q(MSC+HID)
Q(VCP+MSC)
Q(VCP+HID)
#endif
