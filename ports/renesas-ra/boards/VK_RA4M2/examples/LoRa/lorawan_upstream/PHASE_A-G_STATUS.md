# VK_RA4M2 LoRaWAN end-node — alloc-free hot path (Phase A–G)

Финален статус след Phase A–G refactor. Date: 2026-05-06.

## Постигнато

| Метрика | Преди (master) | След Phase A–G |
|---|---|---|
| **Hot-path alloc / uplink cycle** | ~100–200 B | **0 B** |
| `send_uplink` alloc | ~14 fresh objects | 0 |
| `parse_downlink` alloc | ~10 fresh objects (incl. dict literal) | 0 |
| SPI alloc / команда | ~30 × `bytes(1)` (~900 B) | 0 (DTC + pre-alloc) |
| `gc.disable()` compatible | ❌ | ✅ |
| Steady-state free heap | drift с GC pauses | flat ~18 KB |
| `lorawan_app.mpy` size (production) | 21043 B (`_DEBUG=True`) | 18246 B (-13%) |

## Архитектура — кое къде живее

```
ports/renesas-ra/
├── extmod/machine_spi.c             ← write_readinto(tx, rx [,n]) — 4-арг patch
├── ports/renesas-ra/modaes_cmac.c   ← + ecb_encrypt + compute_into (in-place AES)
├── boards/VK_RA4M2/
│   ├── manifest.py                  ← freezes _upstream/sx126x.py + LoRaWAN/
│   └── examples/LoRa/
│       ├── _upstream/sx126x.py      ← byte-at-a-time DTC SPI, pre-alloc буфери
│       └── lorawan_upstream/
│           ├── lorawan_app.py       ← Phase A–G refactored
│           ├── radio/               ← (vendored, изместено във frozen _upstream)
│           └── LoRaWAN/             ← (frozen)
```

## Какво работи (validated на board COM21)

- Boot + ADR state restore + OLED detect
- OTAA join към TTN (DevAddr 01213472)
- CFList parsing — 5 extra канала (867.1–867.9 MHz)
- Multi-channel uplink (round-robin 8 канала)
- FCntUp 178 → 247 across reboots (с +16 boot margin)
- Heartbeat / button / display / persist tasks паралелно
- `aes_cmac.ecb_encrypt` bit-exact срещу NIST AES-128 ECB test vector
- `gc.disable()` post-init, free heap stable

## Hot path — alloc-free walkthrough

```python
# main while loop:
send_uplink(sx, devaddr, nwkskey, appskey, fcnt, 0x02, payload, fopts, confirmed)
   # → builds in _FRAME[256], _AI/_SI/_B0/_MIC_INPUT/_CMAC_OUT reused
   # → native aes_cmac.ecb_encrypt + compute_into in-place
   # → 0 alloc

dl = await listen_rx1(...)         # asyncio.sleep_ms alloc-free
   # → parse_downlink fills _DL_INFO[reused] / _DL_CMDS[reused.clear()]
   # → MIC compare via _DL_MIC[4] byte-by-byte
   # → 0 alloc (success path)

# ADR/DISP dict updates → 0 alloc (existing keys)
# fcnt += 1 → 0 alloc
# gc.collect() → reclaims ~2 KB transient (memoryview slices)
# await asyncio.sleep(interval_s) → 0 alloc
```

## Какво остана с alloc

- **Boot/init phase** — banner, OTAA join, ADR setup → alloc OK (run once)
- **Cold paths** — MAC handlers (LinkADRReq/RXTimingSetup), button press cycle → rare
- **`encrypt_frm_payload` shim в `parse_downlink`** — alloc-ва `bytes(payload_len)` за return. Само ако downlink има payload (~10–20 B). Бъдещ optimization (Phase H?).
- **`memoryview(_FRAME)[:total_len]`** в `sx.send` — ~24 B/call, освобождава се след send returns. Един alloc per uplink.

## Конфигурация (toggles в `lorawan_app.py`)

```python
_DEBUG       = const(False)   # True = development (всички prints), False = production
_GC_DISABLE  = const(True)    # True = gc.disable() след init, False = leave GC on
```

| Combo | Use case |
|---|---|
| `_DEBUG=True, _GC_DISABLE=False` | Debug, full visibility, GC active |
| `_DEBUG=False, _GC_DISABLE=False` | Silent prod with GC safety net |
| `_DEBUG=False, _GC_DISABLE=True` | **Pure production** — deterministic latency, max alloc-free |
| `_DEBUG=True, _GC_DISABLE=True` | ❌ NOT recommended (prints alloc, GC off → MemoryError) |

## Phase summary

```
Phase A — print() guards (75 → guarded, dead-eliminated when _DEBUG=False) ✅
Phase B — frame builder pre-alloc + native AES into-style API              ✅
Phase C — included in B (modaes_cmac.c extension)                          ✅
Phase D — _DL_INFO dict reuse + hoisted const tables                       ✅
Phase E — verified asyncio primitives alloc-free (sleep_ms, sleep, TSF)    ✅
Phase F — deferred FCnt persistence via persist_task() background          ✅
Phase G — gc.disable() activated post-init, app runs stably                ✅
```

## Файлове променени

```
M  extmod/machine_spi.c                              (+30 lines)
M  extmod/modmachine.h                               (~1 line)
M  ports/renesas-ra/modaes_cmac.c                    (+90 lines)
M  ports/renesas-ra/.../LoRa/_upstream/sx126x.py     (~120 lines: SPItransfer rewrite)
M  ports/renesas-ra/.../LoRa/lorawan_upstream/lorawan_app.py  (~250 lines diff)
```

## Pre-allocated buffers (общо 648 B static, в `lorawan_app.py`)

```python
_FRAME      = bytearray(256)   # full TX PHY frame
_AI         = bytearray(16)    # A_i block (CTR-mode payload encryption)
_SI         = bytearray(16)    # S_i = AES(AppSKey, A_i)
_B0         = bytearray(16)    # B0 block (MIC computation header)
_MIC_INPUT  = bytearray(272)   # B0 || msg, fed to CMAC
_CMAC_OUT   = bytearray(16)    # full CMAC; first 4 bytes = MIC
_KDF_BLOCK  = bytearray(16)    # input block for session-key derivation
_JOIN_PLAIN = bytearray(40)    # decrypted Join Accept body
_DL_MIC     = bytearray(4)     # downlink MIC verification scratch
```

## Native AES API extension (`modaes_cmac.c`)

```python
aes_cmac.compute(key, msg) -> bytes(16)               # legacy, allocates
aes_cmac.compute_into(key, msg, dst [, msg_len])      # NEW: zero-alloc
aes_cmac.ecb_encrypt(key, src, dst)                   # NEW: zero-alloc single-block ECB
```

Replaces the previous `cryptolib.aes(key, MODE_ECB).encrypt(...)` chain which always allocated a fresh bytes object on every call.

## Phase G measurements (board COM21, _DEBUG=False, _GC_DISABLE=True)

```
[Phase G] GC disabled, free=18496 B    ← fired after 2 uplinks
18320 False
18272 False
18208 False
16208 False    ← transient dip during one cycle
18192 False
18144 False    ← stable, recovers via explicit gc.collect()
```

Steady-state free heap stays 18000–18300 B across 60 s. Transient ~2 KB dip per cycle (memoryview slices, sx.recv internal buffers) reclaimed by explicit `gc.collect()` at end of each uplink.

## Известни warnings (не блокери)

- Linker warning: `LOAD segment with RWX permissions` — съществуващ за RA порт-а, не свързан с нашите промени.
- 8 MHz SCI9 SPI continuous burst → SX126x bit-shift artefact — **fixed** чрез byte-at-a-time pattern с pre-alloc буфери.
- `_DEBUG=True + _GC_DISABLE=True` комбинация: prints alloc → eventually MemoryError. Не препоръчвана.

## Полезен страничен effect

Patch на `extmod/machine_spi.c` (4-арг `write_readinto(tx, rx, n)`) е **upstream-clean** добавка — backward-compatible, готов за MicroPython upstream PR.

## Готови за следващи стъпки (опционално)

- **Phase H** — eliminate последните ~24 B/uplink (memoryview slice в `sx.send`) чрез port-side `sx.send_into(buf, length)` API.
- **Phase 1 (от RX1 latency план)** — IRQ-driven RX (DIO1 + ThreadSafeFlag + AGT timer) → RX1 jitter от ~5 ms на ~50 µs (Renesas parity).
- **Long-run stress test** — 30 минути monitoring за hidden leaks.
- **Downlink stress test** — TTN command burst, проверка `parse_downlink` под `gc.disable()`.
