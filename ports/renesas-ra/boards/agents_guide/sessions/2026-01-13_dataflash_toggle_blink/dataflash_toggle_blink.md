## Цел
Тест за **постоянна памет в Data Flash** на Renesas RA (VK_RA4M2) чрез MicroPython модула `dataflash`.

След **ресет**:
- ако байтът е `0x00` → записва `0xFF` (чрез erase) и LED мига **2 пъти/сек**
- ако байтът е `0xFF` (или друго) → записва `0x00` и LED мига **10 пъти/сек**

Така при всеки ресет се получава „toggle“ между двете скорости.

---

## Какъв е API-то на `dataflash`
Модулът е вграден в порта (`ports/renesas-ra/moddataflash.c`) и дава:
- `dataflash.size()` → размер на Data Flash (bytes)
- `dataflash.block_size()` → erase block size (bytes)
- `dataflash.write_size()` → минимална write единица (bytes)
- `dataflash.read(offset, length)` → връща `bytes`
- `dataflash.write(offset, buf)` → програмира (само 1->0 позволено без erase)
- `dataflash.erase()` → трие целия Data Flash
- `dataflash.erase_block(index)` → трие 1 block

Важно: Flash **не може** да прави 0→1 битове без erase. Затова `0x00 -> 0xFF` изисква `erase_block()`.

---

## Примерен код
Файл: `main.py` в тази директория.

Логика:
1) `state = dataflash.read(0, 1)[0]`
2) ако `state == 0x00` → `dataflash.erase_block(0)` и blink=2
3) иначе → `dataflash.write(0, b"\x00")` и blink=10

---

## Качване на борда (mpremote)
(Използвай правилния COM порт за твоята машина.)

1) Копирай скрипта като `main.py`, за да се стартира автоматично след reset:
- `mpremote connect COMx fs cp main.py :main.py`

2) Ресет:
- `mpremote connect COMx reset`

Очаквано поведение:
- първи старт (обикновено Data Flash е 0xFF) → ще стане 0x00 и ще мига ~10Hz
- следващ ресет → ще стане 0xFF (чрез erase) и ще мига ~2Hz
- следващ ресет → пак 10Hz …

---

## Забележки / реален статус
- Този тест трие **block 0** на Data Flash (за целите на теста). Ако по-късно пазим повече данни, трябва да се направи layout (пример: отделен block за state).
- Използва `machine.Pin("P409")` (изход) за мигане.
- В този порт `Pin` няма `toggle()` метод, затова toggle се прави чрез `value(0/1)`.

