"""Compile actual RX queue/refill functions with host IRQ stubs. No board I/O.

The producer/DAC timing below is a deterministic model, not an IRQ timing or
analogue measurement. Source extraction keeps the tested queue code identical
to firmware without importing the MCU, FSP or the Python VM into the host test.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"^[\w *]+\b" + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


PRELUDE = r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint32_t mask;
static uint32_t __get_PRIMASK(void) { return mask; }
static void __disable_irq(void) { mask = 1U; }
static void __set_PRIMASK(uint32_t value) { mask = value; }
#define __DMB() ((void)0)
static uint32_t ra_disable_irq(void) { uint32_t old = mask; mask = 1; return old; }
static void ra_enable_irq(uint32_t old) { mask = old; }
static void ra_iq_timing_request_window_reset(void) {}
static struct { uint32_t audio_underruns, ring_overruns; } s_audio;
static struct { uint32_t sample_rate_hz, block_samples; } s_status = {48000, 128};
typedef struct { uint8_t ch; } machine_dac_obj_t;
'''

TESTS = r'''
static uint32_t sent, consumed[2];
static unsigned frames;
static int32_t signal(uint32_t at) { return (int32_t)(at % 997U) - 498; }
static void reset(bool paired) {
    s_ring_head = s_ring_tail = s_scope_q_head = s_scope_q_tail = 0;
    s_audio_chunk_seen = s_audio_chunk_ready = 0;
    s_scope_q_consumer_active = paired;
    s_scope_stage = paired ? 1U : 0U;
    memset(&s_audio, 0, sizeof(s_audio));
    sent = consumed[0] = consumed[1] = 0;
    ra_iq_adc_scope_enable(0);
}
static void produce(unsigned count, bool paired) {
    uint32_t ih = s_ring_head, qh = s_scope_q_head;
    for (unsigned k = 0; k < count; ++k) {
        int32_t v = signal(sent++);
        ra_iq_scope_push_iq(&ih, &qh, v, -v, paired);
    }
    s_ring_head = ih;
    s_scope_q_head = qh;
}
static bool refill(unsigned ch) {
    uint16_t buf[RA_IQ_AUDIO_DMA_SAMPLES + 2];
    buf[0] = 0xace1;
    buf[RA_IQ_AUDIO_DMA_SAMPLES + 1] = 0xbabe;
    uint32_t before = ch ? s_scope_q_tail : s_ring_tail;
    machine_dac_obj_t obj = {(uint8_t)ch};
    CHECK(machine_dac_iq_fill(&obj, buf + 1, RA_IQ_AUDIO_DMA_SAMPLES));
    bool moved = before != (ch ? s_scope_q_tail : s_ring_tail);
    for (unsigned k = 0; k < RA_IQ_AUDIO_DMA_SAMPLES; ++k) {
        int32_t v = moved ? signal(consumed[ch]++) : 0;
        CHECK(buf[k + 1] == (uint16_t)(2048 + (ch ? -v : v)));
    }
    CHECK(buf[0] == 0xace1 && buf[RA_IQ_AUDIO_DMA_SAMPLES + 1] == 0xbabe);
    CHECK(mask == 0);
    return moved;
}
static void pair_refill(unsigned first) {
    bool a = refill(first);
    bool b = refill(first ^ 1U);
    CHECK(a == b);
    CHECK(consumed[0] == consumed[1]);
}
static void cadence(unsigned block, unsigned phase, bool paired, bool dac_first) {
    reset(paired);
    for (unsigned tick = 1; tick <= 512U * 200U; ++tick) {
        bool dac = tick % 512U == 0;
        bool adc = tick % block == phase;
        if (dac && dac_first) {
            if (paired) { pair_refill(tick / 512U % 2U); } else { refill(0); }
        }
        if (adc) { produce(block, paired); }
        if (dac && !dac_first) {
            if (paired) { pair_refill(tick / 512U % 2U); } else { refill(0); }
        }
        CHECK(s_audio.ring_overruns == 0);
    }
    CHECK(consumed[0] >= 512U * 198U);
    CHECK(s_audio.audio_underruns <= 512U);
    frames += 200;
}
int main(void) {
    uint32_t fs;
    size_t block;
    ra_iq_adc_get_audio_params(&fs, &block);
    CHECK(fs == 24000 && block == 64); /* debug API / producer unchanged */
    CHECK(RA_IQ_AUDIO_DMA_SAMPLES == 512);
    CHECK(RA_IQ_ADC_MAX_BLOCK_SAMPLES == 256);
    CHECK(RA_IQ_AUDIO_RING > 2U * RA_IQ_AUDIO_DMA_SAMPLES + RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2U);

    reset(false);
    CHECK(!refill(0));
    produce(448, false);
    CHECK(!refill(0)); /* partial queue retained, not consumed + padded */
    CHECK(s_ring_tail == 0);
    produce(64, false);
    CHECK(refill(0));
    CHECK(consumed[0] == 512);
    CHECK(!refill(1)); /* absent Q never backpressures mono */

    /* ADC publishes between paired callbacks: both must defer, in either order. */
    for (unsigned first = 0; first < 2; ++first) {
        reset(true);
        produce(448, true);
        CHECK(!refill(first));
        produce(64, true);
        CHECK(!refill(first ^ 1U));
        pair_refill(first ^ 1U);
        CHECK(consumed[0] == 512 && consumed[1] == 512);
        CHECK(s_audio.ring_overruns == 0);
    }
    /* All legal decimated producer sizes, including sizes not dividing 512. */
    for (unsigned b = 5; b <= 128; ++b) {
        for (unsigned mode = 0; mode < 4; ++mode) {
            cadence(b, mode % 2 ? b - 1 : 0, mode / 2, mode % 2);
        }
    }
    /* Route/source flush resets the paired gate; stopped Q does not block mono. */
    reset(true);
    produce(448, true);
    CHECK(!refill(0));
    ra_iq_adc_set_scope(0);
    consumed[0] = sent;
    produce(512, false);
    CHECK(refill(0));
    ra_iq_adc_set_scope(1);
    ra_iq_adc_scope_q_consumer(1);
    consumed[0] = consumed[1] = sent;
    produce(512, true);
    pair_refill(1);
    ra_iq_adc_scope_q_consumer_stopped();
    produce(512, false);
    CHECK(refill(0));
    ra_iq_adc_scope_q_consumer(0);

    /* Exactly one 512-point scope frame from each new 512-sample DAC fill. */
    reset(false);
    ra_iq_adc_scope_enable(1);
    produce(512, false);
    CHECK(refill(0));
    const int16_t *scope;
    size_t scope_n;
    CHECK(ra_iq_adc_scope_frame(&scope, &scope_n) && scope_n == 512);
    for (unsigned k = 0; k < 512; ++k) { CHECK(scope[k] == signal(k)); }
    CHECK(!ra_iq_adc_scope_frame(&scope, &scope_n));
    CHECK(!ra_iq_adc_audio_chunk_ready(2, 512));
    CHECK(!ra_iq_adc_audio_chunk_ready(0, 513));
    CHECK(!ra_iq_adc_audio_chunk_ready(0, 0));
    puts("PASS actual C mono/paired queue, refill, wrap, scope, route lifecycle");
    printf("PASS %u assertions; %u modeled DMA periods; producer sizes 5..128\n", checks, frames);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    port = Path(__file__).resolve().parents[2]
    source = (port / "ra/ra_iq_adc.c").read_text(encoding="utf-8")
    header = (port / "ra/ra_iq_adc.h").read_text(encoding="utf-8")
    dac = (port / "machine_dac.c").read_text(encoding="utf-8")
    macros = "\n".join(re.search(r"^#define " + name + r"\b[^\n]*", source + "\n" + header, re.M)[0]
                       for name in ("RA_IQ_AUDIO_DMA_SAMPLES", "RA_IQ_ADC_MAX_BLOCK_SAMPLES",
                                    "RA_IQ_AUDIO_RING", "RA_IQ_AUDIO_RING_MASK", "RA_IQ_BLK_CHFILT",
                                    "RA_IQ_LAST_PREDEMOD_BLK", "RA_IQ_BLK_LIMITER",
                                    "RA_IQ_SPECTRUM_N", "RA_IQ_SPEC_N"))
    names = ("s_audio_ring", "s_ring_head", "s_ring_tail", "s_scope_stage", "s_scope_ring_q",
             "s_scope_q_head", "s_scope_q_tail", "s_scope_q_consumer_active", "s_audio_chunk_seen",
             "s_audio_chunk_ready", "s_scope_audio", "s_scope_wr", "s_scope_half", "s_scope_ready",
             "s_scope_enable")
    globals_ = "\n".join(re.search(r"^static [^\n;]*\b" + name + r"\b[^;]*;", source, re.M)[0]
                         for name in names)
    bodies = "\n".join(function(source, name) for name in (
        "ra_iq_scope_code", "ra_iq_scope_push_iq", "ra_iq_adc_get_audio_params",
        "ra_iq_adc_audio_chunk_ready", "ra_iq_adc_audio_pull", "ra_iq_adc_scope_pull_q",
        "ra_iq_adc_set_scope", "ra_iq_adc_scope_q_consumer", "ra_iq_adc_scope_q_consumer_stopped",
        "ra_iq_adc_scope_enable", "ra_iq_adc_scope_push", "ra_iq_adc_scope_frame"))
    stream = function(dac, "machine_dac_stream")
    assert "sample_count = RA_IQ_AUDIO_DMA_SAMPLES" in stream
    assert "ra_iq_adc_get_audio_params(&freq, NULL)" in stream
    assert stream.index("machine_dac_stop_stream(self)") < stream.index("buf_a[i] = 2048U")
    assert dac.count("[MACHINE_DAC_STREAM_CH][RA_IQ_AUDIO_DMA_SAMPLES]") == 2
    for name in ("ra_iq_adc_set_scope", "ra_iq_adc_start", "ra_iq_adc_scope_q_consumer"):
        assert "s_audio_chunk_seen = 0U" in function(source, name), name
    env = os.environ.copy()
    env["PATH"] = str(Path(args.cc).parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="rx-dac512-") as tmp:
        test = Path(tmp) / "test.c"
        exe = Path(tmp) / ("test.exe" if os.name == "nt" else "test")
        test.write_text(PRELUDE + macros + "\n" + globals_ + "\n" + bodies + "\n" +
                        function(dac, "machine_dac_iq_fill") + "\n" + TESTS, encoding="utf-8")
        subprocess.run([args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-pedantic", str(test), "-o", str(exe)], check=True, env=env)
        subprocess.run([str(exe)], check=True, env=env)
    print("PASS source integration; target IRQ timing and analog output NOT VERIFIED")


if __name__ == "__main__":
    main()
