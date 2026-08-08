# hw_noise_generator_plan.py — Хардуерен шумов генератор (0% CPU)
#
# Архитектура: Temperature Sensor → ADC12 → ADBUF ring → DMAC → RAM ring buffer
# Всичко хардуерно след еднократна CPU конфигурация. Нула загубени пинове.
#
# ╔══════════════════════════════════════════════════════════════════╗
# ║  AGT0 ──ELC──► ADC12 ──ADBUF──► DMAC ──► RAM ring buffer      ║
# ║  (тик)         (TSN)   (16×16b)  (repeat)  (2048-8192×16b)     ║
# ║                  │                              │               ║
# ║          вътрешен TSN              ┌────────────┤               ║
# ║         (0 пина вход)              ▼            ▼               ║
# ║                              DTC→DAC       DSP/Python           ║
# ║                             (аналог)       (цифрово)            ║
# ╚══════════════════════════════════════════════════════════════════╝
#
# ─────────────────────────────────────────────────────────────────
# ХАРДУЕРНИ БЛОКОВЕ (всички вътрешни, 0 пина за вход)
# ─────────────────────────────────────────────────────────────────
#
# 1. Temperature Sensor (TSN) — 0x400F_3000
#    - Вътрешен полупроводников сензор, ~1.41V изход
#    - Термичен шум: ±1-3 LSB (2-3 бита ентропия на проба)
#    - TSCR.TSEN = 1 → старт, TSCR.TSOE = 1 → свързва към ADC
#    - Стабилизация: 30 µs (tTSTBL)
#
# 2. ADC12 — 0x4017_0000
#    - 12-bit SAR ADC, канал = TSN (ADEXICR.TSSAD = 1)
#    - Continuous scan mode (ADCSR.ADCS = 01b)
#    - Ring buffer: ADBUFEN.BUFEN = 1 → ADBUF0..ADBUF15 (16 слота)
#    - Без averaging (ADADC.ADC = 000b) → максимален шум
#    - Right-align: ADCER.ADRFMT = 0 → данни в [11:0]
#    - ADBUFPTR показва текущ write pointer (0..15)
#
# 3. ELC (Event Link Controller)
#    - Event source: AGT0_INT (event 0x001)
#    - Event dest:   ADC12 scan start (ADCSR.ADST)
#    - Хардуерен тригер: AGT тик → ADC conversion, без CPU
#
# 4. AGT0 (Asynchronous General-Purpose Timer)
#    - Период = PCLKB / sample_rate
#    - 24 MHz / 22050 Hz = 1088 тика (45.35 µs)
#    - Генерира event на всеки тик → ELC → ADC start
#
# 5. DMAC ch0 (DMA Controller)
#    - Mode: REPEAT transfer (DMTMD.MD = 01b)
#    - TKP: 1 (free-running, безкраен цикъл)
#    - Source: ADBUF0 (0x4017_00B0), FIXED address
#    - Destination: RAM ring buffer, INCREMENT
#    - Data size: 16-bit (DMTMD.SZ = 01b)
#    - Repeat size: N (напр. 2048 или 4096)
#    - DTS: 0 (destination = repeat area → auto-wrap)
#    - Trigger: ADC12 SCANEND event
#    - Резултат: ADC complete → DMAC копира ADBUF → RAM[wr++]
#               Когато wr == N → wr = 0 (хардуерен reload)
#
# 6. RAM Ring Buffer
#    - uint16 масив, 2048-8192 елемента, align 2
#    - DMAC пише непрекъснато, консуматорът чете независимо
#    - Буферът НИКОГА не се повтаря идентично (TSN шумът е различен)
#
# ─────────────────────────────────────────────────────────────────
# КОНСУМАТОРИ (три варианта, избираеми)
# ─────────────────────────────────────────────────────────────────
#
# Вариант A: Аналогов шум → DAC (0% CPU)
#    DTC CIRCULAR: src = RAM ring, dst = DADR0 (DAC data register)
#    AGT1 тригер: независим sample rate за DAC playback
#    Шумът излиза на P014 като аналогов сигнал
#
# Вариант B: DSP вход (CPU само при обработка)
#    dsp.fft(memoryview(ring))  → спектрален анализ на шума
#    dsp.fir(...)               → филтриран шум (band-limited)
#    seed = ring[0] ^ ring[1]   → хардуерен PRNG seed
#
# Вариант C: Python достъп (CPU само при четене)
#    buf = memoryview(noise_ring)  → zero-copy достъп
#    Четене без спиране на генератора
#
# ─────────────────────────────────────────────────────────────────
# КАЧЕСТВО НА ЕНТРОПИЯТА
# ─────────────────────────────────────────────────────────────────
#
# ADC 12-bit, TSN шум ±1-3 LSB → само 2-3 бита ентропия от 12.
# Горните 9-10 бита ≈ константа (~1.41V DC).
# Спектърът НЕ е плосък без обработка.
#
# Whitening техники (подобряват спектъра):
#
#   1. XOR folding: noise = (raw >> 8) ^ (raw & 0xFF)
#      Свива 12 бита до 8, увеличава ентропийната плътност.
#
#   2. First-difference: noise[i] = raw[i] - raw[i-1]
#      Премахва DC, подсилва високочестотен шум.
#      Може да се направи с DTC chain transfer (хардуерно).
#
#   3. Von Neumann decorrelator:
#      Двойки (a, b): a > b → 1, a < b → 0, a == b → skip.
#      Намалява throughput наполовина, но гарантира unbiased.
#
# ─────────────────────────────────────────────────────────────────
# ОГРАНИЧЕНИЯ И РИСКОВЕ
# ─────────────────────────────────────────────────────────────────
#
# - TSN шум е само 2-3 бита → нужен whitening за бял шум
# - Storm ADC ползва ADC12 → координация (shared peripheral)
# - DMAC канали: 8 налични, 1 зает от шумовия генератор
# - TSN стабилизация: 30 µs → еднократно при init
# - ADC scan time: ~1 µs → макс. ~1 MHz sample rate (повече от нужно)
#
# ─────────────────────────────────────────────────────────────────
# ФАЗИ НА ИМПЛЕМЕНТАЦИЯ
# ─────────────────────────────────────────────────────────────────
#
# Фаза 1: Proof of concept (Python + ISR)
#   - AGT ISR чете ADC TSN канал (CPU в ISR, ~5% CPU)
#   - Записва в кръгов буфер
#   - FFT анализ на шума → проверка на ентропията
#   - Определяне на реалния шумов профил на TSN
#
# Фаза 2: Хардуерна верига (0% CPU)
#   - ELC: AGT → ADC тригер (замества ISR)
#   - DMAC REPEAT: ADBUF → RAM ring (замества CPU копиране)
#   - Осцилоскоп верификация на непрекъснатостта
#
# Фаза 3: MicroPython API
#   - class NoiseGenerator:
#       - __init__(sample_rate=22050, buf_size=4096)
#       - .start() / .stop()
#       - .buffer → memoryview (zero-copy четене)
#       - .dac_out(pin) → опционален аналогов изход
#       - .entropy_bits → оценка на ентропията (2-3)
#       - .whitening = 'xor' | 'diff' | 'vonneumann' | None
#
# ─────────────────────────────────────────────────────────────────
# СРАВНЕНИЕ С АЛТЕРНАТИВИ
# ─────────────────────────────────────────────────────────────────
#
#  Метод              CPU     Ентропия   Пинове  Real-time
#  ─────────────────────────────────────────────────────────
#  TSN→ADC→DMAC→RAM   0%      2-3 b/s    0       ДА
#  SCE9 TRNG→буфер    5 ms    128 b      0       НЕ (batch)
#  LFSR (софтуерен)   ~5%     0 (детерм) 0       ДА
#  ADC floating pin   0%      5-10 b/s   1 пин   ДА
#  TRNG seed + LFSR   0%*     0 + seed   0       ДА
#
#  * 0% след еднократен os.urandom() за seed
#
# ─────────────────────────────────────────────────────────────────
# РЕГИСТРИ (справочник за имплементация)
# ─────────────────────────────────────────────────────────────────
#
# ELC:
#   ELCR    = 0x40082000            [7:ELCON — 1=enable]
#   ELSEGR  = event link registers  [source → destination mapping]
#   AGT0_INT event = 0x001
#   ADC12 scan start = destination peripheral
#
# DMAC ch0:
#   DMSAR0  = 0x40005000 + 0x00    [source address = ADBUF0]
#   DMDAR0  = 0x40005000 + 0x04    [dest address = RAM ring buf]
#   DMCRA0  = 0x40005000 + 0x08    [transfer count / repeat size]
#   DMCRB0  = 0x40005000 + 0x0C    [block / repeat control]
#   DMTMD0  = 0x40005000 + 0x10    [transfer mode: REPEAT, 16-bit]
#   DMINT0  = 0x40005000 + 0x14    [interrupt enable: optional]
#   DMREQ0  = 0x40005000 + 0x1C    [DMA activation: peripheral trigger]
#   DELSR0  = 0x40006280           [event source = ADC12_SCANEND]
#   DMAST   = 0x40005200           [0:DMST — 1=enable DMA module]
#
# DAC12 (за вариант A):
#   DADR0   = 0x4005E000 + 0x00    [11:0 — 12-bit DAC data]
#   DACR    = 0x4005E004           [6:DAOE0 — 1=DA0 output enable]
#
# ─────────────────────────────────────────────────────────────────
# ТАЙМИНГ
# ─────────────────────────────────────────────────────────────────
#
# AGT0 период @ 22050 Hz:
#   PCLKB = 24 MHz → AGT count = 24_000_000 / 22050 = 1088
#   Точност: 24_000_000 / 1088 = 22058.82 Hz (0.04% грешка)
#
# ADC12 conversion time:
#   ~1 µs за 1 канал (TSN) → макс. ~1 MHz
#   При 22050 Hz → 45 µs период, ADC е 44× по-бърз → няма bottleneck
#
# DMAC transfer time:
#   1 bus cycle = ~10 ns → 1 × uint16 transfer ≈ 20-40 ns
#   При 22050 Hz → 45 µs период, DMAC е ~1000× по-бърз
#
# RAM ring buffer пълно обновяване:
#   2048 семпъла / 22050 Hz = 93 ms за 1 обхождане
#   4096 семпъла / 22050 Hz = 186 ms
#   8192 семпъла / 22050 Hz = 372 ms
#   Всяко обхождане = нови данни от TSN → НИКОГА не се повтаря
#
# ─────────────────────────────────────────────────────────────────
# БЕЛЕЖКИ ЗА КООРДИНАЦИЯ СЪС STORM ADC
# ─────────────────────────────────────────────────────────────────
#
# Storm ADC (ra_storm_adc.c) също ползва ADC12 + AGT + ELC + DTC.
# Не могат да работят ЕДНОВРЕМЕННО на един ADC блок.
#
# Решения:
#   a) Мултиплексиране: noise gen спира когато Storm ADC работи
#   b) Time-sharing: noise gen на ADC unit 0, Storm на ADC unit 1
#      (RA4M2 има САМО 1 ADC unit → не може)
#   c) Shared scan: добави TSN към scan group на Storm ADC
#      Storm вече сканира AN000..AN00x, добавяме TSN
#      DMAC копира ADDR_TSN → noise ring, DTC копира ADDR_AN → storm buf
#      Най-елегантно но най-сложно за имплементация
#
# ─────────────────────────────────────────────────────────────────
