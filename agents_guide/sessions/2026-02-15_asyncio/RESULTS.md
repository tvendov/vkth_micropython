# Session results — 2026-02-15 — MicroPython asyncio

## Какво направихме

Създадохме нова папка с документация и runnable примери:

- `agents_guide/asyncio/README.md`
  - кратко ръководство за MicroPython `asyncio` (uasyncio v3.0.0)
  - копируем ASCII модел на event loop
  - практични бележки (timeout, cancel, Lock/Event/ThreadSafeFlag)

- `agents_guide/asyncio/projects/README.md`
  - индекс на проектите

- `agents_guide/asyncio/projects/01_blink/main.py`
- `agents_guide/asyncio/projects/02_two_tasks/main.py`
- `agents_guide/asyncio/projects/03_wait_for_timeout/main.py`
- `agents_guide/asyncio/projects/04_lock_event/main.py`

## Забележки/съвместимост

- В това репо `extmod/asyncio/__init__.py` показва `__version__ = (3, 0, 0)`.
- `import uasyncio` работи като shim към `asyncio` (`extmod/asyncio/uasyncio.py`).
- Пиновете за LED са примерни: `LED`, `P011`, `P409`. Ако не пасва, редактирай `_make_led()`.

## Следващи стъпки (ако искаш)

1) Да добавим пример с `ThreadSafeFlag` + `Pin.irq()` (button -> async task).
2) Да добавим пример за TCP stream (ако конкретната ти платка има network).
3) Да добавим RA-специфичен пример (например: non-blocking I2C loop + asyncio).

