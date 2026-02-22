## 2026-02-22 VK_RA4M2: EXTINT (ICU external IRQ) и лимитът BSP_ICU_VECTOR_MAX_ENTRIES

### Симптом
- Runtime probe в MicroPython показа, че работят само няколко EXTINT пина (първоначално само IRQ0: P105, P206, P400).
- Опит за добавяне на IRQ10..IRQ15 чрез `ra_gen/vector_data.c` доведе до build грешка:
  - `array index in initializer exceeds array bounds` за индекси `[32]..[37]`.

### Причина
- В FSP BSP:
  - `BSP_ICU_VECTOR_MAX_ENTRIES = BSP_VECTOR_TABLE_MAX_ENTRIES - BSP_CORTEX_VECTOR_TABLE_ENTRIES`
  - За VK_RA4M2 generated cfg:
    - `BSP_CORTEX_VECTOR_TABLE_ENTRIES = 16`
    - `BSP_VECTOR_TABLE_MAX_ENTRIES = 48`
  - Следователно `BSP_ICU_VECTOR_MAX_ENTRIES = 32` и валидните NVIC IRQ индекси са `0..31`.
- `g_vector_table[]` и `g_interrupt_event_link_select[]` са оразмерени с `BSP_ICU_VECTOR_MAX_ENTRIES`, затова всеки инициализатор на `[32]..` е извън масива.

### Допълнителна бележка (защо не е просто “увеличи дефайна”)
- FSP `bsp_irq.c` инициализира хардуера с цикъл:
  - `for i < (BSP_ICU_VECTOR_MAX_ENTRIES - BSP_FEATURE_ICU_FIXED_IELSR_COUNT) { R_ICU->IELSR[i] = ... }`
- Ако се увеличи `BSP_ICU_VECTOR_MAX_ENTRIES` само “на сила”, рискът е:
  - несъответствие с реалния брой NVIC IRQ линии, които проектът/MCU поддържа
  - запис/инициализация на невалидни IRQ линии
  - runtime нестабилност

### Какво работи като workaround в момента
- Добавени са `VECTOR_NUMBER_ICU_IRQ1..IRQ9` на вектори `23..31` (в рамките на 0..31).
- Това разширява наличните EXTINT пинове според таблицата в `ra/ra_icu.c`.

### Как да стигнем до IRQ10..IRQ15
- Не чрез увеличаване на лимита.
- Нужно е да се освободят още 6 vector слота в диапазона `0..31` като се изключат/премахнат ненужни peripheral IRQ събития от конфигурацията (идеално през FSP `configuration.xml` + regenerate).
- След освобождаване: да се дадат вектори на `EVENT_ICU_IRQ10..EVENT_ICU_IRQ15` в рамките на `0..31`.

