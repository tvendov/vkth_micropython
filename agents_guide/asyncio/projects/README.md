# asyncio проекти (MicroPython)

Структура:
- `projects/01_blink/` – минимален blink с `asyncio.run()`
- `projects/02_two_tasks/` – две задачи + `create_task()` + `gather()`
- `projects/03_wait_for_timeout/` – timeout с `wait_for_ms()` и правилен cancel
- `projects/04_lock_event/` – `Lock` + `Event` (producer/consumer)

Бележки:
- Пиновете са примерни. Ако нямаш alias `LED`, смени с реален пин (напр. `P011`).
- Ако ползваш IRQ (бутон), предпочитай `ThreadSafeFlag` (описано в основния README).

