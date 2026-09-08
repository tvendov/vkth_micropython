"""Actual native audio control function + actual C math; peripheral stubs only."""
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
static struct { bool ready; ra_tx_config_t config; ra_tx_status_t status; uint8_t *lut; } tx;
static unsigned stops, starts, enters, leaves;
static bool stop_ok, start_ok;
#define FSP_CRITICAL_SECTION_DEFINE
#define FSP_CRITICAL_SECTION_ENTER (++enters)
#define FSP_CRITICAL_SECTION_EXIT (++leaves)
#define FSP_ERR_INVALID_ARGUMENT 1
static bool tx_error(ra_tx_error_t err, int fsp) { (void)fsp; tx.status.error = err; return false; }
bool ra_tx_hw_stop(void) { ++stops; if (!stop_ok) return false; tx.status.running = false; return true; }
bool ra_tx_hw_start(void) {
    ++starts;
    /* Re-arming is forbidden until ALL table entries match the new config. */
    for (unsigned raw = 0; raw < 4096; ++raw) {
        unsigned actual = tx.lut[2*raw] | ((unsigned)tx.lut[2*raw+1] << 8);
        assert(actual == ra_tx_core_am_sample(&tx.config, raw, NULL));
    }
    tx.status.running = start_ok;
    return start_ok;
}
'''

TEST = r'''
int main(void) {
    uint8_t bank[RA_TX_AM_LUT_BYTES];
    tx.lut = bank;
    tx.ready = tx.status.owned = tx.status.running = true;
    tx.config = (ra_tx_config_t){.mode=RA_TX_MODE_AM, .sample_rate_hz=44000,
        .amplitude=400, .adc_mid=2048, .i_zero=2048, .q_zero=2048,
        .ramp_samples=220, .fm_gain=2, .audio_controls=true, .audio_gain=100, .am_depth=50};
    assert(ra_tx_core_build_lut(&tx.config, bank, sizeof(bank)));
    ra_tx_config_t next = tx.config;
    next.amplitude = 800;
    next.am_depth = 80;
    stop_ok = start_ok = true;
    assert(ra_tx_hw_audio_configure(&next));
    assert(stops == 1 && starts == 1 && !enters && tx.status.running);
    ra_tx_config_t saved = tx.config;
    next.amplitude = 200;
    stop_ok = false;
    assert(!ra_tx_hw_audio_configure(&next));
    assert(stops == 2 && starts == 1 && memcmp(&saved, &tx.config, sizeof(saved)) == 0);
    stop_ok = true;
    start_ok = false;
    assert(!ra_tx_hw_audio_configure(&next));
    assert(stops == 3 && starts == 2 && !tx.status.running);
    /* Already-stopped owner stays stopped. */
    assert(ra_tx_hw_audio_configure(&saved));
    assert(stops == 4 && starts == 2 && !tx.status.running);
    for (unsigned mode = RA_TX_MODE_AM; mode <= RA_TX_MODE_LSB; ++mode) {
        if (mode == RA_TX_MODE_FM) continue;
        tx.config.mode = mode;
        tx.config.file_source = true;
        tx.config.sample_rate_hz = mode == RA_TX_MODE_AM ? 24000 : 12000;
        tx.status.running = true;
        next = tx.config;
        next.amplitude = 0;
        next.audio_gain = 500;
        unsigned old_enters = enters;
        assert(ra_tx_hw_audio_configure(&next));
        assert(stops == 4 && starts == 2 && tx.status.running);
        assert(enters == old_enters + 1 && enters == leaves);
        assert(tx.config.amplitude == 0 && tx.config.audio_gain == 500);
        next.audio_gain = 1601;
        assert(!ra_tx_hw_audio_configure(&next) && enters == old_enters + 1);
        next = tx.config;
        next.mode = RA_TX_MODE_FM;
        assert(!ra_tx_hw_audio_configure(&next));
        next = tx.config;
        next.file_source = false;
        assert(!ra_tx_hw_audio_configure(&next));
    }
    puts("PASS actual native audio controller: checked MIC stop/rebuild/start and failures; FILE AM/SSB atomic scalars, no restart; invalid config rejected");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    port = Path(__file__).resolve().parents[2]
    source = (port / 'ra/ra_tx_hw.c').read_text()
    begin = source.index('bool ra_tx_hw_audio_configure(')
    end = source.index('\n}\n', begin) + 3
    function = source[begin:end]
    env = dict(os.environ)
    env['PATH'] = str(Path(args.cc).parent) + os.pathsep + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='tx-audio-control-') as temporary:
        fixture = Path(temporary) / 'test.c'
        binary = Path(temporary) / 'test.exe'
        fixture.write_text(FIXTURE + function + TEST)
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                        '-I', str(port/'ra'), str(fixture), str(port/'ra/ra_tx_core.c'),
                        '-lm', '-o', str(binary)], check=True, env=env)
        subprocess.run([str(binary)], check=True, env=env)
    print('Host only: timer/DTC/DAC, IRQ timing and analog output NOT VERIFIED.')


if __name__ == '__main__':
    main()
