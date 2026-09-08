"""Offline artifact consistency checks; does not connect to or retest a board."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parent


def read(name):
    return (ROOT / name).read_text(encoding='utf-8-sig')


def main():
    for name in ('levels-initial', 'levels', 'cycles', 'production'):
        code = read(name + '-ram.py')
        compile(code, name + '-ram.py', 'exec')
        digest = hashlib.sha256(code.encode()).hexdigest()
        assert digest in read(name + '-host.log'), ('Missing uploaded-script SHA', name)
    for name in ('levels', 'cycles'):
        before = (ROOT / (name + '-dataflash-before.bin')).read_bytes()
        after = (ROOT / (name + '-dataflash-after.bin')).read_bytes()
        assert len(before) == len(after) == 512 and before[:4] == b'SDR1'
        length = int.from_bytes(before[4:6], 'little')
        end = (6 + length + 3) & ~3
        assert end == 368 and before[:end] == after[:end]
        params = json.loads(before[6:6 + length])
        assert params['f'] == 579900 and params['genlevel'] == 50
        # The saved schema abbreviates several names; it is not the runtime
        # dictionary. Compare fixture keys shared by both schemas explicitly.
        current = json.loads(read(name + '-before.json'))[0]
        for key in ('f', 'm', 'genfreq', 'genlevel', 'genwave',
                    'txlevel', 'txmic', 'txdepth', 'txdev'):
            assert current[key] == params[key], (name, key)
        assert 'JLINK_RESET_AFTER_RAM_TEST' in read(name + '-host.log')
    initial = read('levels-initial-host.log')
    assert 'GEN_LEVELS_PASS' in initial and 'AssertionError: Settings changed' in initial
    levels = read('levels-host.log')
    rows = [line.split() for line in levels.splitlines() if line.startswith('LEVEL ')]
    assert len(rows) == 20
    for mode in ('AM', 'USB', 'LSB', 'FM'):
        group = [row for row in rows if row[1] == mode]
        assert [int(row[2]) for row in group] == [0, 25, 50, 75, 100]
        for row in group:
            assert int(row[row.index('STEADY_CLIP') + 1]) == 0
        if mode != 'FM':
            spans = [int(row[row.index('I') + 2]) - int(row[row.index('I') + 1])
                     for row in group]
            assert spans == ([0, 102, 204, 306, 408] if mode == 'AM'
                             else [0, 408, 818, 1228, 1636])
    assert 'GEN_LEVELS_PASS' in levels and 'DATAFLASH_WRITTEN_RECORD_UNCHANGED 368' in levels
    cycles = read('cycles-host.log')
    rows = [line.split() for line in cycles.splitlines() if line.startswith('CYCLE ')]
    assert [int(row[1]) for row in rows] == list(range(16))
    assert [row[2] for row in rows] == ['AM', 'USB', 'LSB', 'FM'] * 4
    for row in rows:
        assert int(row[row.index('TX_CLIP') + 1]) == 0
        assert int(row[row.index('TX_DEADLINE') + 1]) == 0
    free = [int(row[row.index('RX_FREE') + 1]) for row in rows[4:]]
    assert (min(free), max(free)) == (66848, 66976)
    assert re.search(r'SOAK_PASS MS 204653 WARM_RX_FREE 66848 66976', cycles)
    production = read('production-host.log')
    assert 'NORMAL_SAVE_RESTORED True' in production
    assert 'INSTALLED_FIRMWARE_UNCHANGED 1594288' in production
    assert 'LEFT_RUNNING_TX_AM_579900_GEN_SINE_1000HZ_50_PERCENT_TX_LEVEL_40' in production
    print('PASS artifact audit: four RAM-script SHAs/syntax; two preserved 368-byte records;')
    print('20 level points; 16 cycles / 204653 ms; production state; initial failure retained.')
    print('OFFLINE CONSISTENCY ONLY: no new board, analog, touch or RF test.')


if __name__ == '__main__':
    main()
