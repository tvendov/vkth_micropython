# MicroPython asyncio (uasyncio v3) – кратко ръководство

Това е практично ръководство за **asyncio в MicroPython** (в това репо: `extmod/asyncio`, версия `3.0.0`).
Може да ползваш и `import uasyncio as asyncio` (shim към `asyncio`).

## 1) Ментален модел (какво всъщност става)

MicroPython asyncio е **кооперативен** scheduler:
- Кодът ти *трябва* да прави `await` често, за да даде време на други задачи.
- Ако изпълниш блокираща функция/дълъг цикъл без `await`, „замръзваш“ всички задачи.

Копируем модел:

    +-------------------------------+
    | event loop                    |
    |  - TaskQueue (готови tasks)   |
    |  - IOQueue   (poll за I/O)    |
    +---------------+---------------+
                    |
                изпълнява по малко
              от всяка coroutine до
              следващото await/yield

## 2) Минимален старт

- coroutine = функция с `async def`
- `await` = „чакай“ друга coroutine/операция
- `asyncio.run(main())` стартира loop-а и чака main да завърши

## 3) Основни примитиви (които реално ще ползваш)

### Sleep / yield
- `await asyncio.sleep(секунди)`
- `await asyncio.sleep_ms(ms)` (MicroPython-удобно)
- `await asyncio.sleep_ms(0)` = yield (дай шанс на други tasks)

### Паралелизъм (кооперативен)
- `asyncio.create_task(coro())` пуска background task
- `await asyncio.gather(t1, t2, ...)` чака всички

### Timeout
- `await asyncio.wait_for(aw, timeout_s)`
- `await asyncio.wait_for_ms(aw, timeout_ms)`
При timeout: вдига `asyncio.TimeoutError`.

### Cancel
- `task.cancel()`
- вътре в task-а хващаш `asyncio.CancelledError`

### Синхронизация
- `asyncio.Lock()` – mutex
- `asyncio.Event()` – set/clear + `await event.wait()`
- `asyncio.ThreadSafeFlag()` – *може да се set-ва от IRQ/друг thread*, `await flag.wait()`

### TCP Streams (ако имаш network)
- `await asyncio.open_connection(host, port)` -> `(reader, writer)`
- `await asyncio.start_server(cb, host, port)` -> `Server`

Забележка: в `extmod/asyncio/stream.py` има TODO: `socket.getaddrinfo()` е блокиращо.

## 4) Най-честите грешки (и как да ги избегнеш)

1) **Blocking I/O / time.sleep()**
   - Не ползвай `time.sleep()` в asyncio код.
   - Ползвай `await asyncio.sleep()`/`sleep_ms()`.

2) **Дълги while цикли без await**
   - Във всеки „вечен“ loop добави `await asyncio.sleep_ms(0)` или реално чакане.

3) **Памет/GC**
   - Избягвай излишни allocations в tight loops (на MCU това се усеща).
   - Преизползвай буфери (`bytearray`, `memoryview`) при работа с I/O.

4) **IRQ callback**
   - В IRQ: не прави тежка работа.
   - За „събуди task“: `ThreadSafeFlag.set()` и после `await flag.wait()` в asyncio.

## 5) Как да стартираш примерите

Примерна идея с mpremote (адаптирай към твоя порт/COM):

    mpremote connect COMx fs cp agents_guide/asyncio/projects/01_blink/main.py :main.py
    mpremote connect COMx repl

Алтернатива: копирай файла на борда и го стартирай от `>>> import main`.

## 6) Примери

Виж `agents_guide/asyncio/projects/`.
Всеки проект е отделна папка (за „добри практики“ и лесно копиране).

