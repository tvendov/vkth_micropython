"""Offline integrity and result audit; does not access the board."""
import ast
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def read(name):
    return (ROOT / name).read_text(encoding='utf-8')


def rows(name, prefix):
    return [line.split() for line in read(name).splitlines() if line.startswith(prefix)]


def pairs(tokens, start):
    return dict(zip(tokens[start::2], tokens[start + 1::2]))


def main():
    controls = rows('controls-hil.log', 'HF_CONTROL ')
    expected = {(mode, src) for mode in ('AM', 'USB', 'LSB') for src in ('MIC', 'R:' + mode)}
    assert len(controls) == 6 and {(r[1], r[2]) for r in controls} == expected
    for row in controls:
        d = pairs(row, 3)
        assert int(d['AF']) > 0 and int(d['SAMPLES']) > 0
        assert d['UND_STEADY'] == d['CLIP_TOTAL'] == d['CLIP_STEADY'] == '0'
        assert int(d['MAX']) < int(d['BUDGET'])
    assert 'HF_MIC_FILE_CONTROLS_PASS' in read('controls-hil.log')

    repeat = rows('soak-repeat1-hil.log', 'HF_SOAK_TX ')
    assert len(repeat) == 12 and [int(r[1]) for r in repeat] == list(range(12))
    assert {(r[2], r[3]) for r in repeat} == expected
    file_und = []
    for row in repeat:
        d = pairs(row, 4)
        assert int(d['AF']) > 0
        assert d['UND_STEADY'] == d['CLIP'] == d['CLIP_STEADY'] == d['DEADLINE'] == '0'
        if row[3] != 'MIC':
            assert 6 <= int(d['LOOPS']) <= 7
            file_und.append(int(d['UND_TRANSITION']))
    assert len(file_und) == 6 and (min(file_und), max(file_und)) == (11247, 14820)
    rx = rows('soak-repeat1-hil.log', 'HF_SOAK_RX ')
    assert len(rx) == 12
    free = [int(r[3]) for r in rx[3:]]
    assert (min(free), max(free)) == (66480, 66640)
    assert 'HF_VOICE_SOAK_PASS MS 193585' in read('soak-repeat1-hil.log')

    first = rows('soak-partial.log', 'HF_SOAK_TX ')
    assert len(first) == 1 and first[0][1:4] == ['0', 'AM', 'MIC']
    error = read('soak-error.txt').strip()
    assert error.startswith('RuntimeError(') and error.endswith(')')
    traceback = ast.literal_eval(error[len('RuntimeError('):-1])
    mismatch = ast.literal_eval(next(l.split('AssertionError: ', 1)[1]
                                    for l in traceback.splitlines() if l.startswith('AssertionError: ')))
    assert mismatch == ('clock', 3500000,
                        bytes.fromhex('ffff00704dfdffcd'), bytes.fromhex('00ff00f000000000'))
    assert 'HF_VOICE_SOAK_PASS' not in read('soak-host.log')
    assert (ROOT / 'soak-ram.py').read_bytes() == (ROOT / 'soak-repeat1-ram.py').read_bytes()
    clock = rows('clock-hil.log', 'CLOCK_READ ')
    assert len(clock) == 72 and all(r[-2:] == ['OK', 'True'] for r in clock)
    assert len(rows('clock-hil.log', 'CLOCK_WRITE ')) == 27
    assert 'CLOCK_PROBE_BAD_READS 0' in read('clock-hil.log')

    for name in ('controls', 'soak', 'clock', 'soak-repeat1', 'production'):
        raw = (ROOT / (name + '-ram.py')).read_bytes()
        compile(raw, name, 'exec')
        # Host write_text stores CRLF on Windows; checked_execute sends the
        # original Python string, with LF. Match those actual uploaded bytes.
        digest = hashlib.sha256(read(name + '-ram.py').encode('utf-8')).hexdigest()
        assert digest in read(name + '-host.log'), (name, 'Missing uploaded payload SHA')
        if name != 'production':
            assert 'JLINK_RESET_AFTER_RAM_TEST' in read(name + '-host.log')
        if name not in ('soak', 'production'):
            before = (ROOT / (name + '-dataflash-before.bin')).read_bytes()
            after = (ROOT / (name + '-dataflash-after.bin')).read_bytes()
            assert len(before) == len(after) == 512 and before[:4] == b'SDR1'
            end = (6 + int.from_bytes(before[4:6], 'little') + 3) & ~3
            assert end == 368 and before[:end] == after[:end]
            assert 'WRITTEN_SETTINGS_UNCHANGED 368' in read(name + '-host.log')

    state = json.loads(read('before.json'))
    assert state[1:] == ['TX', 'AM', 'GEN', True]
    assert state[0]['f'] == 579900 and state[0]['txlevel'] == 40
    assert state[0]['genfreq'] == 10000 and state[0]['genlevel'] == 50
    prod = read('production-hil.log')
    st = ast.literal_eval(next(l.split('579900 ', 1)[1] for l in prod.splitlines()
                               if l.startswith('PRODUCTION_GEN AM 579900 ')))
    assert st['running'] and st['outputs_enabled'] and st['gen_source'] and st['af_frames'] > 0
    assert st['error'] == st['dsp_clips'] == st['dsp_deadline_misses'] == st['file_underruns'] == 0
    assert 'PRODUCTION_CLOCK_READBACK 1 579900' in prod and 'NORMAL_SAVE_RESTORED True' in prod
    assert 'INSTALLED_FIRMWARE_UNCHANGED 1594536' in read('production-host.log')
    assert 'JLINK_RESET_AFTER_RAM_TEST' not in read('production-host.log')
    print('HF_EVIDENCE_PASS: 6 controls; initial clock failure retained; 72 diagnostic reads;')
    print('12-cycle repeat / 193585 ms; transition UND retained; 368-byte settings preserved;')
    print('5 payload SHA checks; production state restored. OFFLINE AUDIT, NOT NEW HIL/RF.')


if __name__ == '__main__':
    main()
