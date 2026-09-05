#!/usr/bin/env python3
"""Host source/output contract for the VK_RA6M3 receiver IQ-file bank.

This executes the generator but not the firmware C or target hardware.  It
independently checks deterministic FM content, both rate-specific placements,
the actual LOOP transition, and the opt-in-only real-FM input contract.
"""

import cmath
import importlib.util
import math
import pathlib
import struct
import tempfile
import unittest
import zlib


GENERATOR = pathlib.Path(__file__).with_name("make_sdriq_receiver_bank.py")


def load_generator():
    spec = importlib.util.spec_from_file_location("receiver_bank_generator", GENERATOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_sdriq(path):
    data = path.read_bytes()
    rate, center, timestamp, bits, reserved, crc = struct.unpack(
        "<IQQIII", data[:32])
    if zlib.crc32(data[:28]) & 0xFFFFFFFF != crc:
        raise AssertionError(path.name + ": bad header CRC")
    return rate, center, timestamp, bits, reserved, list(
        struct.iter_unpack("<hh", data[32:])), data


def phase_step(first, second):
    i0, q0 = first
    i1, q1 = second
    return math.atan2(i0 * q1 - q0 * i1, i0 * i1 + q0 * q1)


class ReceiverBankSourceTests(unittest.TestCase):
    def test_00_synthetic_fm_is_deterministic_phase_closed_and_rate_specific(self):
        bank = load_generator()
        with tempfile.TemporaryDirectory() as first_dir, tempfile.TemporaryDirectory() as second_dir:
            first = bank.build_synthetic(pathlib.Path(first_dir))
            second = bank.build_synthetic(pathlib.Path(second_dir))
            bank.verify(first)
            bank.verify(second)

            first_by_name = {path.name: path for path in first}
            second_by_name = {path.name: path for path in second}
            expected = {name + suffix + ".sdriq"
                        for name in ("am", "fm", "usb", "lsb", "cw")
                        for suffix in ("48", "24")}
            self.assertEqual(set(first_by_name), expected)
            self.assertEqual(set(second_by_name), expected)
            for name in expected:
                self.assertEqual(first_by_name[name].read_bytes(),
                                 second_by_name[name].read_bytes(), name)

            metrics = {}
            for rate, suffix, expected_carrier in (
                    (48000, "48", -6000.0), (24000, "24", 0.0)):
                path = first_by_name["fm" + suffix + ".sdriq"]
                actual_rate, center, timestamp, bits, reserved, pairs, data = read_sdriq(path)
                self.assertEqual((actual_rate, center, timestamp, bits, reserved),
                                 (rate, 0, 0, 16, 0))
                self.assertEqual(len(data), 32 + 2048 * 4)
                self.assertEqual(len(pairs), 2048)

                radii = [math.hypot(i, q) for i, q in pairs]
                self.assertLessEqual(max(radii) - min(radii), 2.0)
                steps = [phase_step(pairs[index], pairs[(index + 1) % len(pairs)])
                         for index in range(len(pairs))]
                hz = [step * rate / (2.0 * math.pi) for step in steps]
                carrier = sum(hz) / len(hz)
                self.assertAlmostEqual(carrier, expected_carrier, delta=2.0)
                self.assertLess(min(hz), expected_carrier - 3900.0)
                self.assertGreater(max(hz), expected_carrier + 3900.0)

                # The LOOP step is the same analytic interval as the first step.
                self.assertAlmostEqual(steps[-1], steps[0], delta=0.001)
                cycles = bank.FM_MOD_HZ * len(steps) / rate
                self.assertAlmostEqual(cycles, round(cycles), delta=1e-12)
                tone = sum((value - carrier) *
                           cmath.exp(-2j * math.pi * cycles * index / len(hz))
                           for index, value in enumerate(hz))
                measured_deviation = 2.0 * abs(tone) / len(hz)
                self.assertAlmostEqual(measured_deviation,
                                       bank.FM_DEVIATION_HZ, delta=25.0)
                metrics[suffix] = (carrier, min(hz), max(hz),
                                   measured_deviation)
            print("FM SOURCE METRICS", metrics)

    def test_01_real_fm_is_explicit_optional_input_and_never_fabricated(self):
        bank = load_generator()
        self.assertFalse(bank._validate_real_fm_options(None, None))
        with self.assertRaisesRegex(ValueError, "requires --fm-shift-hz"):
            bank._validate_real_fm_options(pathlib.Path("capture.wav"), None)
        with self.assertRaisesRegex(ValueError, "requires --fm-source"):
            bank._validate_real_fm_options(None, 0.0)
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            missing = root / "missing-real-fm.wav"
            with self.assertRaisesRegex(FileNotFoundError,
                                        "real FM source WAV not found"):
                bank._validate_real_fm_options(missing, 0.0)
            paths = bank.build_synthetic(root / "bank")
            self.assertFalse(any(path.name.startswith("zfm") for path in paths))
            self.assertFalse((root / "bank" / "zfm48.sdriq").exists())
            self.assertFalse((root / "bank" / "zfm24.sdriq").exists())

    def test_02_phase_safe_real_fm_crossfade_cannot_cartesian_null(self):
        try:
            import numpy as np
        except ImportError:
            self.skipTest("NumPy is required only for optional real-FM conversion")
        bank = load_generator()
        sample_rate = 1000
        count = sample_rate
        overlap = 100
        # Deliberately opposed start/continuation phases.  A Cartesian blend has
        # a zero at mid-fade; the FM path must rotate a non-zero phasor instead.
        source = np.ones(count + overlap, dtype=np.complex128)
        source[count - 2:] = -1.0 + 0.0j
        result = bank._shift_and_loop(source, sample_rate, 0.0,
                                      duration_s=1.0, overlap_s=0.1,
                                      phase_safe=True)
        self.assertEqual(len(result), count)
        self.assertGreater(float(np.min(np.abs(result[:overlap]))), 0.999)
        self.assertGreater(float(abs(result[-1])), 0.999)
        self.assertGreater(float(abs(result[0])), 0.999)


if __name__ == "__main__":
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(ReceiverBankSourceTests))
    if not result.wasSuccessful():
        raise SystemExit(1)
    print("IQ RECEIVER BANK SOURCE TEST PASS", result.testsRun, "tests")
