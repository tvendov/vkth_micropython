"""Offline audit only: never accesses a serial port or target hardware."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parent


def read(name):
    return (ROOT / name).read_text(encoding='utf-8-sig')


def main():
    for name, log in (('levels', 'levels-host.log'), ('cycles', 'cycles-host.log'),
                      ('rx-native', 'rx-host.log'), ('rx-gui', 'rx-host.log'),
                      ('production', 'production-host.log')):
        code = read(name + '-ram.py')
        compile(code, name + '-ram.py', 'exec')
        assert hashlib.sha256(code.encode()).hexdigest() in read(log), name
    for name in ('levels', 'cycles'):
        before = (ROOT / (name + '-dataflash-before.bin')).read_bytes()
        after = (ROOT / (name + '-dataflash-after.bin')).read_bytes()
        assert len(before) == len(after) == 512 and before[:4] == b'SDR1'
        length = int.from_bytes(before[4:6], 'little')
        end = (6 + length + 3) & ~3
        assert end == 368 and before[:end] == after[:end]
        json.loads(before[6:6 + length])
        assert 'JLINK_RESET_AFTER_RAM_TEST' in read(name + '-host.log')
    levels = read('levels-host.log')
    rows = [line.split() for line in levels.splitlines() if line.startswith('LEVEL ')]
    assert len(rows) == 20
    for mode in ('AM', 'USB', 'LSB', 'FM'):
        group = [row for row in rows if row[1] == mode]
        assert [int(row[2]) for row in group] == [0, 25, 50, 75, 100]
        for row in group:
            for key in ('CLIP', 'TRANSITION_CLIP', 'STEADY_CLIP'):
                assert int(row[row.index(key) + 1]) == 0
        if mode != 'FM':
            spans = [int(row[row.index('I') + 2]) - int(row[row.index('I') + 1])
                     for row in group]
            assert spans == ([0, 102, 204, 306, 408] if mode == 'AM'
                             else [0, 408, 818, 1228, 1636])
    shapes = [line.split() for line in levels.splitlines()
              if line.startswith('SMOOTH_SHAPE_PASS ')]
    expected = {(mode, str(hz), wave) for mode in ('AM', 'USB', 'LSB', 'FM')
                for hz in (1000, 1750, 500) for wave in ('SQUARE', 'TRIANGLE', 'SINE')}
    assert len(shapes) == 36 and {tuple(row[1:4]) for row in shapes} == expected
    assert all(row[4:6] == ['CLIP', '0'] for row in shapes)
    assert 'GEN_SMOOTH_LEVELS_SHAPES_PASS' in levels
    cycles = read('cycles-host.log')
    rows = [line.split() for line in cycles.splitlines() if line.startswith('CYCLE ')]
    assert [int(row[1]) for row in rows] == list(range(8))
    assert [row[2] for row in rows] == ['AM', 'USB', 'LSB', 'FM'] * 2
    for row in rows:
        assert row[row.index('TX_CLIP') + 1] == '0'
        assert row[row.index('TX_DEADLINE') + 1] == '0'
    free = [int(row[row.index('RX_FREE') + 1]) for row in rows]
    assert (min(free), max(free)) == (66848, 66960)
    assert re.search(r'SOAK_PASS MS 102542 WARM_RX_FREE 66848 66960', cycles)
    rx = read('rx-host.log')
    assert 'RX_TONE_MONITOR_BOARD_PASS' in rx and 'RX_TONE_GUI_PASS' in rx
    assert rx.count('JLINK_RESET_AFTER_RAM_TEST') == 2
    core = read('core-host.log')
    raw = read('raw-reference-host.log')
    for mode, clips in ((1, 0), (2, 168), (3, 1312), (4, 1312)):
        assert f'GEN transitions mode={mode} smooth=0 clips={clips}' in raw
        assert f'GEN transitions mode={mode} smooth=1 clips=0' in core
    assert 'total == 0' in raw
    for mode, clips in ((2, 1851), (3, 1799), (4, 1799)):
        assert f'GEN full-scale square mode={mode} clips={clips}' in core
    assert 'ALL_13_PYTHON_DRIVEN_HOST_SUITES_PASS' in read('python-host.log')
    assert '3524 checks' in read('af-host.log')
    deploy = json.loads(read('deployment.json'))
    assert deploy['firmware_readback'] and deploy['qspi_16mib_identical']
    assert deploy['dataflash_written_record_identical'] and deploy['original_files'] == 76
    assert deploy['firmware_bytes'] == 1594536 and deploy['heap_bytes'] == 281600
    assert not any(deploy[key] for key in ('app_written', 'tests_written', 'opencv', 'lab'))
    production = read('production-host.log')
    assert 'NORMAL_SAVE_RESTORED True' in production
    assert 'INSTALLED_FIRMWARE_UNCHANGED 1594536' in production
    assert 'LEFT_RUNNING_TX_AM_579900_GEN_SINE_1000HZ_50_PERCENT_TX_LEVEL_40' in production
    print('PASS offline audit: five RAM source SHAs/syntax, two368-byte records;')
    print('20 levels +36 shapes, eight cycles/102542ms, RX native/GUI, production;')
    print('raw counterexample/new core PASS and real square-wave overload retained.')
    print('NO NEW HARDWARE OR PHYSICAL MEASUREMENT.')


if __name__ == '__main__':
    main()
