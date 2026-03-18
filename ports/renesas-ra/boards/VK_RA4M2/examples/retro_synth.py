# Reusable retro synth helper for VK_RA4M2 DAC audio.

from array import array
from machine import DAC, Pin
import time


MID = 2048
DEFAULT_AMP = 1400
DEFAULT_TABLE_LEN = 128
DEFAULT_NOISE_BURST_LEN = 2048

_NOTE_INDEX = {
    "C": 0,
    "C#": 1,
    "DB": 1,
    "D": 2,
    "D#": 3,
    "EB": 3,
    "E": 4,
    "F": 5,
    "F#": 6,
    "GB": 6,
    "G": 7,
    "G#": 8,
    "AB": 8,
    "A": 9,
    "A#": 10,
    "BB": 10,
    "B": 11,
}


def clamp12(value):
    if value < 0:
        return 0
    if value > 4095:
        return 4095
    return value


def midi_to_freq(midi_note):
    return 440.0 * (2.0 ** ((midi_note - 69) / 12.0))


def note_to_freq(note_or_freq):
    if isinstance(note_or_freq, (int, float)):
        return float(note_or_freq)

    text = note_or_freq.strip().replace("♭", "b").replace("♯", "#")
    if text.endswith("Hz") or text.endswith("HZ"):
        return float(text[:-2])

    if len(text) < 2:
        raise ValueError("invalid note")

    name = text[0].upper()
    pos = 1
    if pos < len(text) and text[pos] in ("#", "b", "B"):
        name += text[pos].upper()
        pos += 1

    octave = int(text[pos:])
    if name not in _NOTE_INDEX:
        raise ValueError("invalid note")

    midi_note = (octave + 1) * 12 + _NOTE_INDEX[name]
    return midi_to_freq(midi_note)


def semitone_freq(base_freq, semitones):
    return float(base_freq) * (2.0 ** (semitones / 12.0))


def make_square(length, amp, duty_num=1, duty_den=2):
    buf = array("H", [MID] * length)
    threshold = (length * duty_num) // duty_den
    hi = clamp12(MID + amp)
    lo = clamp12(MID - amp)
    for i in range(length):
        buf[i] = hi if i < threshold else lo
    return buf


def make_triangle(length, amp):
    buf = array("H", [MID] * length)
    top = clamp12(MID + amp)
    bottom = clamp12(MID - amp)
    half = length // 2
    for i in range(length):
        if i < half:
            buf[i] = bottom + ((top - bottom) * i) // max(1, half - 1)
        else:
            j = i - half
            buf[i] = top - ((top - bottom) * j) // max(1, half - 1)
    return buf


def make_saw(length, amp):
    buf = array("H", [MID] * length)
    start = clamp12(MID - amp)
    span = clamp12(MID + amp) - start
    for i in range(length):
        buf[i] = start + (span * i) // max(1, length - 1)
    return buf


def make_noise_burst(length, amp):
    buf = array("H", [MID] * length)
    lfsr = 0xACE1
    for i in range(length):
        bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1
        lfsr = (lfsr >> 1) | (bit << 15)
        level = amp * (length - i) // length
        sample = MID + level if (lfsr & 1) else MID - level
        buf[i] = clamp12(sample)
    return buf


class RetroSynth:
    def __init__(
        self,
        dac_pin="P014",
        amp=DEFAULT_AMP,
        table_len=DEFAULT_TABLE_LEN,
        noise_burst_len=DEFAULT_NOISE_BURST_LEN,
    ):
        self.dac_pin = dac_pin
        self.amp = amp
        self.table_len = table_len
        self.noise_burst_len = noise_burst_len
        self.tables = {
            "square": make_square(table_len, amp, 1, 2),
            "pulse50": make_square(table_len, amp, 1, 2),
            "pulse25": make_square(table_len, amp, 1, 4),
            "pulse12": make_square(table_len, amp, 1, 8),
            "triangle": make_triangle(table_len, amp),
            "saw": make_saw(table_len, amp),
        }
        self.noise_burst = make_noise_burst(noise_burst_len, amp)
        self.dac = DAC(Pin(dac_pin))
        self.dac.write(MID)

    def _table(self, wave):
        key = wave.lower()
        if key == "pulse":
            key = "pulse25"
        return self.tables[key]

    def _freq(self, note_or_freq):
        return note_to_freq(note_or_freq)

    def note_rate(self, note_or_freq, wave="square"):
        table = self._table(wave)
        return max(1, int(self._freq(note_or_freq) * len(table)))

    def start(self, note_or_freq, wave="square"):
        table = self._table(wave)
        self.dac.write_timed(
            table,
            self.note_rate(note_or_freq, wave),
            mode=DAC.CIRCULAR,
            transfer=DAC.TRANSFER_DTC,
        )

    def stop(self):
        self.dac.stop()
        self.dac.write(MID)

    def rest(self, duration_ms):
        self.stop()
        time.sleep_ms(duration_ms)

    def deinit(self):
        self.stop()
        self.dac.deinit()

    def playing(self):
        return self.dac.playing()

    def play_note(self, note_or_freq, duration_ms, wave="square", gap_ms=0):
        self.start(note_or_freq, wave)
        time.sleep_ms(duration_ms)
        self.stop()
        if gap_ms:
            time.sleep_ms(gap_ms)

    def play_sweep(self, start_note, end_note, duration_ms, wave="square", steps=24, gap_ms=0):
        start_hz = self._freq(start_note)
        end_hz = self._freq(end_note)
        step_delay = max(1, duration_ms // steps)
        for i in range(steps):
            freq_hz = start_hz + ((end_hz - start_hz) * i) / max(1, steps - 1)
            self.start(freq_hz, wave)
            time.sleep_ms(step_delay)
        self.stop()
        if gap_ms:
            time.sleep_ms(gap_ms)

    def play_arpeggio(self, root_note, semitones=(0, 4, 7), step_ms=60, cycles=4, wave="pulse25", gap_ms=0):
        root_hz = self._freq(root_note)
        for _ in range(cycles):
            for semitone in semitones:
                self.play_note(semitone_freq(root_hz, semitone), step_ms, wave, gap_ms)

    def play_melody(self, sequence, wave="pulse25", gap_ms=10):
        for item in sequence:
            if len(item) == 2:
                note_or_freq, duration_ms = item
                item_wave = wave
            else:
                note_or_freq, duration_ms, item_wave = item

            if note_or_freq is None:
                self.rest(duration_ms)
            else:
                self.play_note(note_or_freq, duration_ms, item_wave, gap_ms)

    def _play_noise_burst_once(self, sample_rate, transfer):
        self.stop()
        self.dac.write_timed(
            self.noise_burst,
            sample_rate,
            mode=DAC.NORMAL,
            transfer=transfer,
        )
        while self.dac.playing():
            time.sleep_ms(10)
        self.dac.write(MID)

    def play_noise_burst(self, sample_rate=12000, gap_ms=0):
        try:
            self._play_noise_burst_once(sample_rate, DAC.TRANSFER_DMAC)
        except OSError:
            self._play_noise_burst_once(sample_rate, DAC.TRANSFER_DTC)
        if gap_ms:
            time.sleep_ms(gap_ms)

    def coin(self):
        self.play_note("B5", 70, "pulse25", gap_ms=20)
        self.play_note("E6", 90, "pulse25")

    def jump(self):
        self.play_sweep("A3", "F5", 140, "square", steps=18)

    def laser(self):
        self.play_sweep("A6", "A3", 220, "saw", steps=28)

    def explosion(self):
        self.play_noise_burst(12000)
