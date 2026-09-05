"""Build the VK_RA6M3 TESTER AM/FM/USB/LSB/CW IQ-file bank.

The deterministic bank is project-owned and needs only the Python standard
library.  Supplying ``--zs1-dir`` also converts the known local, real off-air
ZS-1 AM/SSB/CW WAV recordings with NumPy.  A real FM pair is written only when
``--fm-source`` and its explicit ``--fm-shift-hz`` are supplied; the generator
never substitutes a synthetic waveform for a missing recording.  Third-party
recordings are intentionally not copied into Git; download suitable private
receiver-test material from:

    http://zs-1.ru/index.php/downloads/category/iq-records

ZS-1 files are 125 ksample/s, stereo, 24-bit WAV.  Their packed samples are
stored as sign/MSB/LSB bytes (not normal WAV little-endian 24-bit).  Conversion
selects a useful channel, shifts it to complex baseband, performs an FFT
band-limited resample, normalises it, and writes the strict SDRangel-compatible
32-byte-header S16LE I/Q format consumed by ``sdr_single._IqFileSource``.

FM rate contract: 48-kS/s IN files carry a -6-kHz low IF and require NCO
-6000; 24-kS/s MID/OUT files are centred and require NCO 0.  Both use the
same 6-kHz-channel narrow-FM discriminator path, not broadcast WFM.
"""

import argparse
import hashlib
import math
import pathlib
import re
import struct
import zlib


RATES = (48000, 24000)
SYNTH_SAMPLES = 2048       # exactly one production 8192-byte payload buffer
SYNTH_AMPLITUDE_48K = 12000
IN_FILE_SCALE = 16          # 48-kS/s IN S16 is divided by 16 into ADC-count units
HEADER_BYTES = 32
BITS = 16
FM_CARRIER_48_HZ = -6000.0  # physical RX convention: RF is below the Tayloe LO
FM_MOD_HZ = 984.375         # 42 cycles/2048 at 48 kS/s, 84 cycles at 24 kS/s
FM_DEVIATION_HZ = 4000.0
REAL_DURATION_S = 2.0
REAL_OVERLAP_S = 0.05

SYNTH_NAMES = ("am", "fm", "usb", "lsb", "cw")
REAL_NAMES = ("zam", "zusb", "zlsb", "zcw")

ZS1_FILES = {
    # Keep the selected carriers away from DC: IN passes through the receiver's
    # mandatory DC remover before reaching the movable 24-kS/s complex path.
    "am": ("AM_09_765_000_001.wav", 0.0, 3002.5),
    "ssb": ("SSB_14_180_000_001.wav", 0.0, 10000.0),
    "cw": ("CW_14MHz_001.wav", 14.0, 10063.4375),
}


def _header(sample_rate, center_hz):
    prefix = struct.pack("<IQQII", sample_rate, center_hz, 0, BITS, 0)
    return prefix + struct.pack("<I", zlib.crc32(prefix) & 0xFFFFFFFF)


def _write_payload(path, sample_rate, center_hz, payload):
    if not payload or len(payload) & 3:
        raise ValueError("payload must contain complete interleaved I/Q pairs")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(_header(sample_rate, center_hz) + payload)


def _synth_pair(kind, sample_rate, index):
    t = index / sample_rate
    # IN files emulate the signed part of a 12-bit ADC after native /16 scaling.
    # MID/OUT files are copied directly into that same internal int16 domain.
    amplitude = (SYNTH_AMPLITUDE_48K if sample_rate == 48000 else
                 SYNTH_AMPLITUDE_48K / IN_FILE_SCALE)
    if kind == "am":
        # 43 cycles in 2048 samples at 48 kS/s (86 at 24 kS/s): the
        # 1007.8125-Hz envelope and the 3-kHz carrier both close at LOOP.
        envelope = 1.0 + 0.5 * math.cos(2.0 * math.pi * 1007.8125 * t)
        phase = 2.0 * math.pi * 3000.0 * t
        value = amplitude * envelope
        return round(value * math.cos(phase)), round(value * math.sin(phase))
    if kind == "fm":
        # Constant-envelope NFM.  Both phase terms close exactly at the file
        # boundary, so LOOP repeats the next analytic sample rather than making
        # an arbitrary phase jump.  IN mirrors the real receiver's -6-kHz IF;
        # MID/OUT are already past the raw-input DC servo and stay at baseband.
        carrier_hz = FM_CARRIER_48_HZ if sample_rate == 48000 else 0.0
        phase = (2.0 * math.pi * carrier_hz * t +
                 (FM_DEVIATION_HZ / FM_MOD_HZ) *
                 math.sin(2.0 * math.pi * FM_MOD_HZ * t))
        return round(amplitude * math.cos(phase)), round(amplitude * math.sin(phase))
    if kind == "usb":
        phase = 2.0 * math.pi * 1500.0 * t
        return round(amplitude * math.cos(phase)), round(amplitude * math.sin(phase))
    if kind == "lsb":
        phase = -2.0 * math.pi * 1500.0 * t
        return round(amplitude * math.cos(phase)), round(amplitude * math.sin(phase))
    if kind == "cw":
        # A phase-closed +23.4375-Hz carrier survives the mandatory IN DC remover;
        # the receiver's CW BFO turns it into about 723 Hz.  The smooth 46.875-Hz
        # key envelope also closes exactly at both file sample rates.
        key = 0.5 + 0.5 * math.cos(2.0 * math.pi * 46.875 * t)
        phase = 2.0 * math.pi * 23.4375 * t
        value = amplitude * key
        return round(value * math.cos(phase)), round(value * math.sin(phase))
    raise ValueError("unknown synthetic profile " + kind)


def build_synthetic(out_dir):
    written = []
    for kind in SYNTH_NAMES:
        for rate in RATES:
            payload = bytearray(SYNTH_SAMPLES * 4)
            for index in range(SYNTH_SAMPLES):
                i_value, q_value = _synth_pair(kind, rate, index)
                struct.pack_into("<hh", payload, index * 4, i_value, q_value)
            path = out_dir / (kind + ("48" if rate == 48000 else "24") + ".sdriq")
            _write_payload(path, rate, 0, payload)
            written.append(path)
    return written


def _riff_chunks(path):
    chunks = {}
    with path.open("rb") as stream:
        if stream.read(12)[0:4] != b"RIFF":
            raise ValueError(path.name + ": not RIFF/WAVE")
        while True:
            chunk_header = stream.read(8)
            if len(chunk_header) != 8:
                break
            chunk_id, declared = struct.unpack("<4sI", chunk_header)
            offset = stream.tell()
            available = max(0, path.stat().st_size - offset)
            chunks[chunk_id] = (offset, min(declared, available))
            stream.seek(declared + (declared & 1), 1)
    return chunks


def _read_zs1(path, start_s, duration_s=2.0, overlap_s=0.05):
    try:
        import numpy as np
    except ImportError as exc:
        raise RuntimeError("ZS-1 conversion requires NumPy") from exc

    chunks = _riff_chunks(path)
    if b"fmt " not in chunks or b"data" not in chunks:
        raise ValueError(path.name + ": missing fmt/data chunk")
    with path.open("rb") as stream:
        stream.seek(chunks[b"fmt "][0])
        fmt = stream.read(chunks[b"fmt "][1])
        tag, channels, rate, _byte_rate, align, bits = struct.unpack_from(
            "<HHIIHH", fmt)
        if (tag, channels, rate, align, bits) != (1, 2, 125000, 6, 24):
            raise ValueError(path.name + ": unexpected ZS-1 WAV format")
        center_hz = 0
        if b"auxi" in chunks:
            stream.seek(chunks[b"auxi"][0])
            aux = stream.read(chunks[b"auxi"][1]).decode("utf-16le", "ignore")
            match = re.search(r"<Frequency>([0-9]+)</Frequency>", aux)
            if match:
                center_hz = int(match.group(1))

        wanted = round((duration_s + overlap_s) * rate)
        first = round(start_s * rate)
        data_offset, data_bytes = chunks[b"data"]
        available_frames = data_bytes // align
        if first + wanted > available_frames:
            raise ValueError(path.name + ": selected clip exceeds real EOF")
        stream.seek(data_offset + first * align)
        raw = stream.read(wanted * align)

    # ZS-1's 24-bit packing is big-endian within each WAV sample: byte 0 is
    # sign/MSB and byte 2 is LSB.  Decoding it as normal WAV LE produces noise.
    octets = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
    unsigned = (octets[:, 2].astype(np.int32) |
                (octets[:, 1].astype(np.int32) << 8) |
                (octets[:, 0].astype(np.int32) << 16))
    signed = (unsigned ^ 0x800000) - 0x800000
    iq = signed.reshape(-1, 2).astype(np.float64)
    return iq[:, 0] + 1j * iq[:, 1], rate, center_hz


def _shift_and_loop(source, sample_rate, shift_hz, duration_s=REAL_DURATION_S,
                    overlap_s=REAL_OVERLAP_S, phase_safe=False):
    import numpy as np

    count = round(duration_s * sample_rate)
    overlap = round(overlap_s * sample_rate)
    if len(source) < count + overlap:
        raise ValueError("short source clip")
    phase = 2.0 * np.pi * shift_hz * np.arange(count + overlap) / sample_rate
    shifted = source[:count + overlap] * np.exp(1j * phase)

    # Make the LOOP seam continuous: sample count-1 is followed by its real
    # continuation at count, then the first overlap returns to the beginning.
    result = shifted[:count].copy()
    weight = np.linspace(0.0, 1.0, overlap, endpoint=False)
    continuation = shifted[count:count + overlap]
    beginning = shifted[:overlap]
    if phase_safe:
        # A Cartesian crossfade can cancel two equal FM vectors whose phases are
        # opposed.  Interpolate radius and the unwrapped relative phase instead;
        # this preserves a usable discriminator vector throughout the seam.
        relative = np.unwrap(np.angle(beginning * np.conj(continuation)))
        relative -= (2.0 * np.pi *
                     round(float(np.median(relative)) / (2.0 * np.pi)))
        radius = ((1.0 - weight) * np.abs(continuation) +
                  weight * np.abs(beginning))
        result[:overlap] = radius * np.exp(
            1j * (np.angle(continuation) + weight * relative))
    else:
        result[:overlap] = ((1.0 - weight) * continuation +
                            weight * beginning)
    return result


def _fft_resample(source, target_count):
    import numpy as np

    source_count = len(source)
    if target_count > source_count or (source_count - target_count) & 1:
        raise ValueError("this converter supports even integer FFT truncation")
    spectrum = np.fft.fftshift(np.fft.fft(source))
    first = (source_count - target_count) // 2
    reduced = spectrum[first:first + target_count]
    return (np.fft.ifft(np.fft.ifftshift(reduced)) *
            (target_count / source_count))


def _numpy_payload(source, target_peak, hard_limit):
    import numpy as np

    level = float(np.percentile(np.abs(source), 99.9))
    if not math.isfinite(level) or level <= 0.0:
        raise ValueError("empty/invalid real-IQ selection")
    scaled = source * (target_peak / level)
    interleaved = np.empty(len(scaled) * 2, dtype="<i2")
    interleaved[0::2] = np.rint(
        np.clip(scaled.real, -hard_limit, hard_limit)).astype(np.int16)
    interleaved[1::2] = np.rint(
        np.clip(scaled.imag, -hard_limit, hard_limit)).astype(np.int16)
    return interleaved.tobytes()


def build_zs1(out_dir, source_dir):
    import numpy as np

    selected = {}
    for key, (filename, start_s, shift_hz) in ZS1_FILES.items():
        source, rate, _center = _read_zs1(source_dir / filename, start_s)
        selected[key] = _shift_and_loop(source, rate, shift_hz)

    # After +10 kHz the selected real 20-m USB speech occupies positive
    # frequencies, matching the receiver's RF convention.  Conjugation creates
    # the corresponding negative-frequency LSB test.
    profiles = {
        "zam": selected["am"],
        "zusb": selected["ssb"],
        "zlsb": np.conj(selected["ssb"]),
        "zcw": selected["cw"],
    }
    # These are recentered baseband fixtures, not RF waveforms.  Keep center=0
    # so the file cannot be misread as a request to feed 9/14 MHz into RA6M3.
    # The original RF centers remain discoverable in the source WAV metadata.
    profile_centers = {name: 0 for name in REAL_NAMES}
    written = []
    for name in REAL_NAMES:
        for rate in RATES:
            target_count = 2 * rate
            converted = _fft_resample(profiles[name], target_count)
            path = out_dir / (name + ("48" if rate == 48000 else "24") + ".sdriq")
            point_scale = 1.0 if rate == 48000 else 1.0 / IN_FILE_SCALE
            _write_payload(path, rate, profile_centers[name],
                           _numpy_payload(converted,
                                          20000.0 * point_scale,
                                          30000.0 * point_scale))
            written.append(path)
    return written


def _validate_real_fm_options(source_path, shift_hz):
    """Return whether real FM was requested; reject incomplete input cleanly."""
    if source_path is None and shift_hz is None:
        return False
    if source_path is None:
        raise ValueError("--fm-shift-hz requires --fm-source")
    if shift_hz is None:
        raise ValueError("--fm-source requires --fm-shift-hz")
    if not math.isfinite(float(shift_hz)):
        raise ValueError("--fm-shift-hz must be finite")
    source_path = pathlib.Path(source_path)
    if not source_path.is_file():
        raise FileNotFoundError("real FM source WAV not found: " + str(source_path))
    return True


def _frequency_shift(source, sample_rate, shift_hz):
    import numpy as np

    phase = 2.0 * np.pi * shift_hz * np.arange(len(source)) / sample_rate
    return source * np.exp(1j * phase)


def build_real_fm(out_dir, source_path, start_s, shift_hz):
    """Convert one explicitly supplied real ZS-1 FM capture into zfm48/zfm24.

    ``shift_hz`` is the complex shift which puts the selected FM carrier at DC
    before resampling.  The 48-kS/s IN asset is then moved to -6 kHz; the
    24-kS/s MID/OUT asset remains centred.  No source path means no real FM and
    is handled by ``main`` before this function is called.
    """
    import numpy as np

    source_path = pathlib.Path(source_path)
    if not source_path.is_file():
        raise FileNotFoundError("real FM source WAV not found: " + str(source_path))
    if not math.isfinite(float(start_s)) or start_s < 0.0:
        raise ValueError("--fm-start-s must be a finite non-negative value")
    if not math.isfinite(float(shift_hz)):
        raise ValueError("--fm-shift-hz must be finite")

    source, source_rate, _center = _read_zs1(source_path, start_s)
    if abs(float(shift_hz)) >= source_rate / 2:
        raise ValueError("--fm-shift-hz must stay inside the source Nyquist band")
    centred = _shift_and_loop(source, source_rate, float(shift_hz),
                              phase_safe=True)

    written = []
    for rate in RATES:
        converted = _fft_resample(centred, round(REAL_DURATION_S * rate))
        if rate == 48000:
            converted = _frequency_shift(converted, rate, FM_CARRIER_48_HZ)
        point_scale = 1.0 if rate == 48000 else 1.0 / IN_FILE_SCALE
        path = out_dir / ("zfm" + ("48" if rate == 48000 else "24") + ".sdriq")
        _write_payload(path, rate, 0,
                       _numpy_payload(converted,
                                      20000.0 * point_scale,
                                      30000.0 * point_scale))
        written.append(path)
    return written


def _read_file(path):
    data = path.read_bytes()
    if len(data) < HEADER_BYTES or (len(data) - HEADER_BYTES) & 3:
        raise AssertionError(path.name + ": invalid total size")
    rate, center, timestamp, bits, reserved, crc = struct.unpack(
        "<IQQIII", data[:HEADER_BYTES])
    if zlib.crc32(data[:28]) & 0xFFFFFFFF != crc:
        raise AssertionError(path.name + ": header CRC")
    if rate not in RATES or bits != BITS or reserved != 0 or timestamp != 0:
        raise AssertionError(path.name + ": header contract")
    pairs = list(struct.iter_unpack("<hh", data[HEADER_BYTES:]))
    if len(pairs) < 2048:
        raise AssertionError(path.name + ": not LOOP-safe")
    return rate, center, pairs, data


def _phase_step(first, second):
    i0, q0 = first
    i1, q1 = second
    return math.atan2(i0 * q1 - q0 * i1, i0 * i1 + q0 * q1)


def _wrapped_delta(first, second):
    return math.atan2(math.sin(first - second), math.cos(first - second))


def _percentile(values, fraction):
    ordered = sorted(values)
    index = round(fraction * (len(ordered) - 1))
    return ordered[index]


def _verify_fm(path, rate, pairs, real):
    radii = [math.hypot(i, q) for i, q in pairs]
    median_radius = _percentile(radii, 0.5)
    if median_radius <= 0.0:
        raise AssertionError(path.name + ": empty FM vector")

    # Include last->first: this is the transition the LOOP player actually makes.
    steps = [_phase_step(pairs[index], pairs[(index + 1) % len(pairs)])
             for index in range(len(pairs))]
    hz = [step * rate / (2.0 * math.pi) for step in steps]
    expected_carrier = FM_CARRIER_48_HZ if rate == 48000 else 0.0
    mean_hz = sum(hz) / len(hz)
    lo_hz = _percentile(hz, 0.01 if real else 0.0)
    hi_hz = _percentile(hz, 0.99 if real else 1.0)

    if abs(mean_hz - expected_carrier) > (1000.0 if real else 2.0):
        raise AssertionError(path.name + ": FM carrier placement")
    if real:
        # A weak/wide/WFM selection is not a valid fixture for the 24-kS/s,
        # 6-kHz-channel NFM receiver.  The explicit source/shift must yield a
        # bounded discriminator trajectory around the requested centre.
        if (lo_hz < expected_carrier - 5750.0 or
                hi_hz > expected_carrier + 5750.0):
            raise AssertionError(path.name + ": real FM is not bounded NFM")
    else:
        if (lo_hz > expected_carrier - 3900.0 or
                hi_hz < expected_carrier + 3900.0):
            raise AssertionError(path.name + ": synthetic FM deviation")
        if max(radii) - min(radii) > max(3.0, median_radius * 0.001):
            raise AssertionError(path.name + ": synthetic FM envelope")

    # The seam must neither mute nor introduce a phase-derivative outlier.  The
    # strict synthetic vector is mathematically phase closed; real FM gets a
    # looser bound for off-air noise after band-limited resampling.
    if min(radii[-1], radii[0]) < median_radius * (0.20 if real else 0.99):
        raise AssertionError(path.name + ": FM seam amplitude collapse")
    seam = steps[-1]
    seam_join = abs(_wrapped_delta(steps[0], seam))
    seam_curve = max(abs(_wrapped_delta(seam, steps[-2])),
                     seam_join)
    if ((real and seam_curve > 0.75) or
            (not real and (seam_join > 0.01 or seam_curve > 0.10))):
        raise AssertionError(path.name + ": FM seam phase discontinuity")
    return mean_hz, lo_hz, hi_hz, seam_curve


def verify(paths):
    for path in sorted(paths):
        rate, center, pairs, data = _read_file(path)
        if center != 0:
            raise AssertionError(path.name + ": baseband center must be zero")
        peak = max(max(abs(i), abs(q)) for i, q in pairs)
        rms = math.sqrt(sum(i * i + q * q for i, q in pairs) / len(pairs))
        internal_peak = peak // IN_FILE_SCALE if rate == 48000 else peak
        if (peak == 0 or rms < 100.0 or peak > 30000 or
                internal_peak > 2047):
            raise AssertionError(path.name + ": invalid signal level")
        # Files carry a rate suffix (usb48, zlsb24, ...).  Normalise it before
        # profile-specific checks; otherwise the sign/keying gates never run.
        stem = re.sub(r"(?:24|48)$", "", path.stem)
        if stem in ("usb", "lsb", "zusb", "zlsb"):
            rotation = sum(i0 * q1 - q0 * i1
                           for (i0, q0), (i1, q1) in zip(pairs, pairs[1:]))
            if stem in ("usb", "zusb") and rotation <= 0:
                raise AssertionError(path.name + ": USB rotation sign")
            if stem in ("lsb", "zlsb") and rotation >= 0:
                raise AssertionError(path.name + ": LSB rotation sign")
        if stem in ("cw", "zcw"):
            radii = [abs(i) + abs(q) for i, q in pairs]
            if min(radii) > max(radii) // 20:
                raise AssertionError(path.name + ": CW is not keyed")
        fm_metrics = None
        if stem in ("fm", "zfm"):
            fm_metrics = _verify_fm(path, rate, pairs, stem == "zfm")
        digest = hashlib.sha256(data).hexdigest().upper()
        print(path.name, len(data), "bytes", rate, "S/s", len(pairs),
              "pairs", "center", center, "rms", round(rms, 1),
              "internal_peak", internal_peak,
              "fm_hz", (tuple(round(value, 2) for value in fm_metrics)
                         if fm_metrics is not None else "-"),
              "sha256", digest)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent /
                        "generated" / "iqbank")
    parser.add_argument("--zs1-dir", type=pathlib.Path,
                        help="directory containing the three extracted ZS-1 WAV files")
    parser.add_argument("--fm-source", type=pathlib.Path,
                        help="optional real 125-kS/s ZS-1 stereo 24-bit FM IQ WAV")
    parser.add_argument("--fm-start-s", type=float, default=0.0,
                        help="start second of the 2-s real FM selection (default: 0)")
    parser.add_argument("--fm-shift-hz", type=float,
                        help="required complex shift which centres --fm-source at 0 Hz")
    args = parser.parse_args()
    try:
        real_fm_requested = _validate_real_fm_options(args.fm_source,
                                                       args.fm_shift_hz)
    except (ValueError, FileNotFoundError) as exc:
        parser.error(str(exc))
    paths = build_synthetic(args.out_dir)
    if args.zs1_dir is not None:
        paths += build_zs1(args.out_dir, args.zs1_dir)
    if real_fm_requested:
        paths += build_real_fm(args.out_dir, args.fm_source, args.fm_start_s,
                               args.fm_shift_hz)
    verify(paths)
    print("IQ RECEIVER BANK PASS", len(paths), "files")


if __name__ == "__main__":
    main()
