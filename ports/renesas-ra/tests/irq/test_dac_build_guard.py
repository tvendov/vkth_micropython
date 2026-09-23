"""Regression for the independent DAC build blocker found during IRQ validation."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct { unsigned ch, iq_stream; } machine_dac_obj_t;
static machine_dac_obj_t obj;
static bool stop_ok;
static unsigned roots, flushes;
#define MP_OBJ_NULL NULL
static bool ra_dac_stream_stop(unsigned ch) {
    (void)ch;
    if (stop_ok) obj.iq_stream=0; /* Model stop callback changing the live flag. */
    return stop_ok;
}
#if MICROPY_HW_ENABLE_IQ_ADC
static void ra_iq_adc_scope_q_consumer(unsigned active) { assert(!active);++flushes; }
#endif
static void machine_dac_buffer_root_set(machine_dac_obj_t *self,void *root) {
    assert(self==&obj && root==NULL);++roots;
}
'''
TEST = r'''
int main(void) {
    obj=(machine_dac_obj_t){1,1};stop_ok=false;
    assert(!machine_dac_stop_one(&obj));assert(obj.iq_stream && !roots && !flushes);
    stop_ok=true;assert(machine_dac_stop_one(&obj));assert(!obj.iq_stream && roots==1);
    assert(flushes==MICROPY_HW_ENABLE_IQ_ADC);
    assert(machine_dac_stop_one(&obj));assert(roots==2 && flushes==MICROPY_HW_ENABLE_IQ_ADC);
    obj=(machine_dac_obj_t){0,1};assert(machine_dac_stop_one(&obj));
    assert(flushes==MICROPY_HW_ENABLE_IQ_ADC);
    return 0;
}
'''

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    port = Path(__file__).resolve().parents[2]
    text = (port / 'machine_dac.c').read_text()
    start = text.index('static bool machine_dac_stop_one(')
    body = text[start:text.index('\n}', start) + 2]
    env = dict(os.environ)
    env['PATH'] = str(Path(args.cc).parent) + os.pathsep + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='ra-dac-guard-') as directory:
        source, binary = Path(directory)/'test.c', Path(directory)/'test.exe'
        source.write_text(PRELUDE + body + TEST)
        for enabled in (0, 1):
            subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Os',
                            '-DMICROPY_HW_ENABLE_IQ_ADC='+str(enabled), str(source),
                            '-o', str(binary)], check=True, env=env)
            subprocess.run([str(binary)], check=True, env=env)
            print('PASS real DAC stop body: IQ_ADC='+str(enabled)+', failed/successful stop, snapshot before callback')

if __name__ == '__main__':
    main()
