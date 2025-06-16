#!/usr/bin/env python3
import subprocess
import sys

def run_test(name, code):
    print(f"Testing {name}...")
    try:
        result = subprocess.run(
            ["mpremote", "connect", "COM29", "exec", code],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            print(f"  ‚úÖ PASS: {name}")
            if result.stdout.strip():
                print(f"     {result.stdout.strip()}")
            return True
        else:
            print(f"  ‚ùå FAIL: {name}")
            return False
    except Exception as e:
        print(f"  Ì≤• ERROR: {name} - {e}")
        return False

# Run basic tests
tests = [
    ("GC Test", "import gc; l=[i for i in range(50)]; print(len(l)); del l; gc.collect(); print('GC OK')"),
    ("Memory Test", "import gc; buf=bytearray(60*1024); print('60KB OK'); del buf; gc.collect(); print('Cleanup OK')"),
    ("Machine Test", "import machine; print(f'Freq: {machine.freq()}'); print('Machine OK')"),
    ("Pin Test", "import machine; p=machine.Pin('P000', machine.Pin.OUT); p.value(1); p.value(0); print('Pin OK')"),
]

passed = 0
for name, code in tests:
    if run_test(name, code):
        passed += 1

print(f"\nResults: {passed}/{len(tests)} tests passed")
