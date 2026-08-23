"""Build the VK_RA6M3 TESTER AM/USB/LSB/CW IQ-file bank.

The deterministic bank is project-owned and needs only the Python standard
library.  Supplying ``--zs1-dir`` also converts local, real off-air ZS-1 WAV
recordings with NumPy.  The third-party recordings are intentionally not copied
into Git; download them for private receiver testing from:

    http://zs-1.ru/index.php/downloads/category/iq-records

ZS-1 files are 125 ksample/s, stereo, 24-bit WAV.  Their packed samples are
stored as sign/MSB/LSB bytes (not normal WAV little-endian 24-bit).  Conversion
selects a useful channel, shifts it to complex baseband, performs an FFT
band-limited resample, normalises it, and writes the strict SDRangel-compatible
32-byte-header S16LE I/Q format consumed by ``sdr_single._IqFileSource``.
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

SYNTH_NAMES = ("am", "usb", "lsb", "cw")
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
    if kind == "usb":
        phase = -2.0 * math.pi * 1500.0 * t  # firmware USB convention
        return round(amplitude * math.cos(phase)), round(amplitude * math.sin(phase))
    if kind == "lsb":
        phase = 2.0 * math.pi * 1500.0 * t
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


def _shift_and_loop(source, sample_rate, shift_hz, duration_s=2.0,
                    overlap_s=0.05):
    import numpy as np

    count = round(duration_s * sample_rate)
    overlap = round(overlap_s * sample_rate)
    if len(source) < count + overlap:
        raise ValueError("short source clip")
    phase = 2.0 * np.pi * shift_hz * np.arange(count + overlap) / sample_rate
    shifted = source[:count + overlap] * np.exp(1j * phase)

    # Make the LOOP seam continuous without muting it: the first overlap fades
    # from the real continuation at t=duration to the original beginning.
    result = shifted[:count].copy()
    weight = np.linspace(0.0, 1.0, overlap, endpoint=False)
    result[:overlap] = ((1.0 - weight) * shifted[count:count + overlap] +
                        weight * shifted[:overlap])
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
    # frequencies.  Conjugation maps it to the receiver's established negative-Q
    # USB convention; the unconjugated twin is the derived real-content LSB test.
    profiles = {
        "zam": selected["am"],
        "zusb": np.conj(selected["ssb"]),
        "zlsb": selected["ssb"],
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
            if stem in ("usb", "zusb") and rotation >= 0:
                raise AssertionError(path.name + ": USB rotation sign")
            if stem in ("lsb", "zlsb") and rotation <= 0:
                raise AssertionError(path.name + ": LSB rotation sign")
        if stem in ("cw", "zcw"):
            radii = [abs(i) + abs(q) for i, q in pairs]
            if min(radii) > max(radii) // 20:
                raise AssertionError(path.name + ": CW is not keyed")
        digest = hashlib.sha256(data).hexdigest().upper()
        print(path.name, len(data), "bytes", rate, "S/s", len(pairs),
              "pairs", "center", center, "rms", round(rms, 1),
              "internal_peak", internal_peak,
              "sha256", digest)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent /
                        "generated" / "iqbank")
    parser.add_argument("--zs1-dir", type=pathlib.Path,
                        help="directory containing the three extracted ZS-1 WAV files")
    args = parser.parse_args()
    paths = build_synthetic(args.out_dir)
    if args.zs1_dir is not None:
        paths += build_zs1(args.out_dir, args.zs1_dir)
    verify(paths)
    print("IQ RECEIVER BANK PASS", len(paths), "files")


if __name__ == "__main__":
    main()
