"""Actual MIC ADC callback and C sample body; peripheral registers are stubs."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

FIXTURE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ra_tx_core.h"
static struct {
    bool ready;
    uint16_t raw, phase;
    ra_tx_config_t config;
    ra_tx_status_t status;
    uint8_t *lut;
    union { ra_tx_fm_state_t fm; ra_tx_ssb_state_t ssb; } dsp;
} tx;
static struct { uint32_t CYCCNT; } cycles;
static struct { uint16_t DADR[2]; } dac;
static struct { uint16_t ADDR[32]; } adc;
#define DWT (&cycles)
#define R_DAC (&dac)
#define R_ADC0 (&adc)
#define FSP_ERR_TIMEOUT 1
#define ADC_EVENT_SCAN_COMPLETE 1
typedef struct { int event; } adc_callback_args_t;
static unsigned unexpected;
static void tx_unexpected_irq(void *p) { (void)p; ++unexpected; }
static bool tx_error(ra_tx_error_t error, int fsp) {
    tx.status.error = error; tx.status.fsp_error = fsp; return false;
}
'''

TEST = r'''
int main(void) {
    tx.config = (ra_tx_config_t){.mode=RA_TX_MODE_AM, .sample_rate_hz=44000,
        .amplitude=409, .adc_mid=2048, .i_zero=2048, .q_zero=2048,
        .ramp_samples=220, .fm_gain=2, .audio_controls=true, .audio_gain=100, .am_depth=50};
    tx.ready = tx.status.running = true;
    tx.status.dsp_budget_cycles = 2728;
    assert(ra_tx_uses_cpu(&tx.config));
    assert(ra_tx_core_lut_bytes(&tx.config) == 0);
    assert(ra_tx_core_lut_alignment(&tx.config) == 1);
    assert(ra_tx_core_build_lut(&tx.config, NULL, 0));
    adc_callback_args_t event = {.event=ADC_EVENT_SCAN_COMPLETE};
    for (unsigned raw=0; raw<4096; ++raw) {
        adc.ADDR[1] = raw;
        tx_adc_callback(&event);
        assert(tx.raw == raw);
        assert(dac.DADR[0] == ra_tx_core_am_sample(&tx.config, raw, NULL));
        assert(dac.DADR[1] == 2048);
        assert(tx.status.dsp_samples == raw+1);
    }
    assert(!unexpected && !tx.status.dsp_clips && !tx.status.dsp_deadline_misses);
    tx.config.amplitude = 0;
    tx_adc_callback(&event);
    assert(dac.DADR[0] == 2048 && dac.DADR[1] == 2048);
    unsigned samples = tx.status.dsp_samples;
    tx.config.file_source = true;
    tx_adc_callback(&event); /* FILE must never consume microphone ADC IRQs. */
    tx.config.file_source = false;
    tx.config.audio_controls = false;
    tx_adc_callback(&event); /* legacy DTC path is not reinterpreted as C AM */
    tx.config.audio_controls = true;
    tx_adc_callback(NULL);
    tx.status.running = false;
    tx_adc_callback(&event);
    assert(unexpected == 4 && tx.status.dsp_samples == samples);
    puts("PASS actual MIC ADC callback/C sample body: 4096 AM codes, no LUT, mute, FILE/legacy/stopped guards");
    return 0;
}
'''

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    port = Path(__file__).resolve().parents[2]
    source = (port/'ra/ra_tx_hw.c').read_text()
    functions = []
    for name in ('tx_cpu_sample', 'tx_adc_callback'):
        begin = source.index('static void ' + name + '(')
        end = source.index('\n}\n', begin) + 3
        functions.append(source[begin:end])
    env = dict(os.environ)
    env['PATH'] = str(Path(args.cc).parent) + os.pathsep + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='tx-am-mic-') as directory:
        fixture = Path(directory)/'test.c'
        binary = Path(directory)/'test.exe'
        fixture.write_text(FIXTURE + '\n'.join(functions) + TEST)
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                        '-I', str(port/'ra'), str(fixture), str(port/'ra/ra_tx_core.c'),
                        '-lm', '-o', str(binary)], check=True, env=env)
        subprocess.run([str(binary)], check=True, env=env)
    print('Host only: actual ISR delivery, elapsed time and physical ADC/DAC NOT VERIFIED.')

if __name__ == '__main__':
    main()
