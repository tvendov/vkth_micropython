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
#define FSP_CRITICAL_SECTION_DEFINE
#define FSP_CRITICAL_SECTION_ENTER (++enters)
#define FSP_CRITICAL_SECTION_EXIT (++leaves)
bool ra_tx_hw_stop(void) { ++stops; return true; }
bool ra_tx_hw_start(void) { ++starts; return true; }
'''

TEST = r'''
int main(void) {
    tx.lut = NULL;
    tx.ready = tx.status.owned = tx.status.running = true;
    tx.config = (ra_tx_config_t){.mode=RA_TX_MODE_AM, .sample_rate_hz=44000,
        .amplitude=400, .adc_mid=2048, .i_zero=2048, .q_zero=2048,
        .ramp_samples=220, .fm_gain=2, .audio_controls=true, .audio_gain=100, .am_depth=50};
    assert(ra_tx_core_build_lut(&tx.config, NULL, 0));
    ra_tx_config_t next = tx.config;
    next.amplitude = 800;
    next.am_depth = 80;
    assert(ra_tx_hw_audio_configure(&next));
    assert(stops == 0 && starts == 0 && enters == 1 && enters == leaves && tx.status.running);
    assert(tx.config.amplitude == 800 && tx.config.am_depth == 80);
    ra_tx_config_t saved = tx.config;
    next.audio_controls = false;
    assert(!ra_tx_hw_audio_configure(&next));
    assert(enters == 1 && memcmp(&saved, &tx.config, sizeof(saved)) == 0);
    /* Already-stopped owner stays stopped. */
    tx.status.running = false;
    assert(ra_tx_hw_audio_configure(&saved));
    assert(stops == 0 && starts == 0 && !tx.status.running);
    for (unsigned file = 0; file <= 1; ++file) {
    for (unsigned mode = RA_TX_MODE_AM; mode <= RA_TX_MODE_LSB; ++mode) {
        if (mode == RA_TX_MODE_FM) continue;
        tx.config.mode = mode;
        tx.config.file_source = file;
        tx.config.sample_rate_hz = mode == RA_TX_MODE_AM ? 24000 : 12000;
        tx.status.running = true;
        next = tx.config;
        next.amplitude = 0;
        next.audio_gain = 500;
        unsigned old_enters = enters;
        assert(ra_tx_hw_audio_configure(&next));
        assert(stops == 0 && starts == 0 && tx.status.running);
        assert(enters == old_enters + 1 && enters == leaves);
        assert(tx.config.amplitude == 0 && tx.config.audio_gain == 500);
        next.audio_gain = 1601;
        assert(!ra_tx_hw_audio_configure(&next) && enters == old_enters + 1);
        next = tx.config;
        next.mode = RA_TX_MODE_FM;
        assert(!ra_tx_hw_audio_configure(&next));
        next = tx.config;
        next.file_source = !file;
        assert(!ra_tx_hw_audio_configure(&next));
    }
    }
    tx.status.error = RA_TX_ERROR_STOP_TIMEOUT;
    next = tx.config;
    assert(!ra_tx_hw_audio_configure(&next));
    tx.status.error = RA_TX_ERROR_NONE;
    tx.ready = false;
    assert(!ra_tx_hw_audio_configure(&next));
    puts("PASS actual native audio controller: MIC/FILE AM/SSB atomic scalars without LUT or restart; invalid/unready/faulted owner rejected");
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
