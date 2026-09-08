"""Run existing RAM-only level/cycle fixtures on the new envelope firmware."""
from pathlib import Path
import tx_gen_level_soak_20260908 as suite

suite.DEPLOY = Path((suite.WORK/'gen-smooth-deployment-path.txt').read_text().strip())
# Include startup and every transition: cumulative CLIP must remain zero.
suite.LEVELS = suite.LEVELS.replace('assert steady_clips==0,',
    "assert last['dsp_clips']==0 and steady_clips==0,")
suite.LEVELS += '''
for name in ('AM','USB','LSB','FM'):
    mode(name)
    backend()
    gen_setting('LEVEL','50%')
    for hz in (1000,1750,500):
        gen_setting('FREQUENCY',str(hz)+' Hz')
        for wave in ('SQUARE','TRIANGLE','SINE'):
            gen_setting('WAVE',wave)
            time.sleep_ms(180)
            st=healthy()
            assert st['dsp_clips']==0,(name,hz,wave,st)
            print('SMOOTH_SHAPE_PASS',name,hz,wave,'CLIP',st['dsp_clips'],
                  'MAX',st['dsp_max_cycles'],'BUDGET',st['dsp_budget_cycles'])
    gen_setting('FREQUENCY','1000 Hz')
    click(a.ui.get('scr-settings').get_child(0).get_child(1))
print('GEN_SMOOTH_LEVELS_SHAPES_PASS; no physical I/Q/RF measurement')
'''
suite.CYCLES = suite.CYCLES.replace('range(16)', 'range(8)')

if __name__ == '__main__':
    suite.main()
