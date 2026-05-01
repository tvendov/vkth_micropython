# LoRa тестова инфраструктура (VK_RA4M2 + Wio-SX1262)

Тестовите скриптове реализират плана от `../TEST_PLAN.md` за двете
паралелни Python библиотеки:

- **A** — `lora-sx126x` от `micropython-lib`
- **B** — `ehong-tl/micropySX126X`

## Файлове

| Файл | Какво прави | Колко платки |
|------|--------------|---------------|
| `_config.py` | Споделени pin / RF параметри (EU868) | — |
| `_radio.py`  | Унифициран адаптер за двете либи | — |
| `_log.py`    | JSON-line логер с PASS/FAIL обобщение | — |
| `t1_smoke.py` | Smoke / sanity тест | 1 |
| `t3_tx.py` | Изпраща N номерирани пакета | 2 (двойка с `t3_rx.py`) |
| `t3_rx.py` | Приема и проверява пакетите | 2 |
| `t3_pingpong.py` | Двупосочен RTT тест (master + slave) | 2 |
| `t5_stress.py` | 1000-пакетен burst, проверка за heap leaks | 2 |
| `t4_parity.py` | Сравнение A vs B на същия линк | 2 |

## Pre-requisites

И на двата DUT-а трябва да са инсталирани избраните либи:

**Вариант A (lora-sx126x):**
```
mpremote mip install lora-sx126x
```

**Вариант B (micropySX126X):**
Копирайте `_sx126x.py`, `sx126x.py`, `sx1262.py` от
https://github.com/ehong-tl/micropySX126X в `/flash/lib/`.

Копирайте цялата `tests/` папка на двата DUT-а:
```
mpremote cp -r tests/ :tests/
```

На REPL:
```
import sys
sys.path.insert(0, "/flash/tests")
```

## Как да изпълним тестовете

### T1 — Smoke (само 1 платка)
На която и да е от двете платки::
```python
import t1_smoke
t1_smoke.run("A")     # тества lora-sx126x
t1_smoke.run("B")     # тества micropySX126X
t1_smoke.run_both()   # последователно A после B
```

### T3.tx + T3.rx (двойка платки)

На DUT-2 (приемник):
```python
import t3_rx
t3_rx.run(lib="A", count=100, total_timeout_s=180)
```

В рамките на 5 секунди стартирайте на DUT-1 (предавател):
```python
import t3_tx
t3_tx.run(lib="A", count=100)
```

Очакван резултат: на приемника `T3.rx.received_at_least_99pct` = `PASS`.

### T3.pingpong (двойка платки)

На DUT-2:
```python
import t3_pingpong
t3_pingpong.slave(lib="A", duration_s=300)
```

На DUT-1:
```python
import t3_pingpong
t3_pingpong.master(lib="A", count=50)
```

Очакван резултат: RTT median ≤ 200 ms за SF7 / 32 B.

### T5 — стрес (двойка платки)

На DUT-2:
```python
import t5_stress
t5_stress.rx(lib="A")
```

След това на DUT-1:
```python
import t5_stress
t5_stress.tx(lib="A")
```

Очакван резултат:
- `T5.stress.tx.completed` = PASS
- `T5.stress.rx.per_le_1pct` = PASS
- `T5.stress.rx.no_leak` = PASS (heap не нараства > 4 KB)

### T4 — паритет A vs B (двойка платки)

На DUT-2 (стабилен предавател за 5 минути):
```python
import t4_parity
t4_parity.tx_loop(lib="A", duration_s=300)
```

На DUT-1 (двупосочно измерване с A после B):
```python
import t4_parity
t4_parity.dual_rx(samples_per_lib=20)
```

Очакван резултат: |ΔRSSI| ≤ 2 dB, |ΔSNR| ≤ 1 dB.

## Анализ на логовете

Всеки тест извежда `LOG {...}` JSON-линии и накрая `RESULT: PASS|FAIL`
+ `EXIT_CODE: 0|1`. За колекция и анализ от хост:

```bash
mpremote run tests/t3_rx.py 2>&1 | tee dut2.log
# на втория терминал:
mpremote run tests/t3_tx.py 2>&1 | tee dut1.log
# след това:
grep '^LOG ' dut1.log dut2.log | python -m json.tool
```

## Бележки

- Скриптовете са идемпотентни: всеки извиква `r.init()` и `r.close()`,
  така че може да ги изпълнявате последователно без reboot.
- Между TX скриптове задължително изчаквайте поне `MIN_TX_GAP_MS` (1 s) —
  за да спазите ETSI EU868 duty cycle ≤ 1%.
- При смяна на `SF` или `BW` променете и `MIN_TX_GAP_MS` в `_config.py`,
  за да остане под лимита на duty cycle.
