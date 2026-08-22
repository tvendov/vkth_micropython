# renesas_micropython — контекст за агенти

Този fork се използва за два различни вида работа. Прочети съответния раздел според задачата.

## Активен проект: SDR приемник на VK_RA6M3

Целта е реален RA6M3 SDR приемен път вътре в Renesas RA MicroPython порта:
I/Q запис, DSP в C, автономен DAC аудио изход. MicroPython остава control plane;
realtime работата не зависи от Python цикли.

**Пълните правила и анализи живеят извън това репо**, в
`C:\Users\teodor\Desktop\stem\sdr\SDR_TRANCEIVER_RA6M3`:

| Файл | Съдържание |
|---|---|
| `SDR-RA6M3-RULES.md` | Действащите правила, Document ID **SDR-RA6M3-RULES-v0.3**. Чети го пръв. |
| `ADC12-INVENTORY.md` | Инвентар на ADC12 блоковете от гл. 47 + текущото състояние на порта |
| `BLOCKER-ADC1-VECTOR.md` | Анализ на липсващия ADC1 activation source, четири пътя и препоръка |

Тази папка е извън репото, така че стартирай с достъп до нея:

```
claude --add-dir "C:\Users\teodor\Desktop\stem\sdr\SDR_TRANCEIVER_RA6M3"
```

### Фиксирани решения (не се предоговарят)

- **ARCH-ADC-001** — ADC0 = I, ADC1 = Q, два синхронни канала.
- **ARCH-TRIG-002** — двете единици от **един и същ ELC източник**: `ELC.ELSR8` (ELC_AD00)
  и `ELC.ELSR10` (ELC_AD10) към същото събитие. Смесване софтуерен/ELC тригер е забранено.
- **ARCH-ADC-002** — I и Q само на S&H каналите AN000–AN002 / AN100–AN102, с
  `ADSHCR.SHANS = 1`, `ADSHMSR.SHMD = 1` и идентични `ADSSTRn`/`SSTSH` на двете единици.
  Пиновете: **P000 = AN000**, **P004 = AN100**, **P014 = DA0**.
- **ARCH-PGA-001/002** — на тези шест канала ADC12 иска `ADPGACR` нибъл `9h` или `Eh`; началната
  стойност не е валидна конфигурация. PGA и S&H работят заедно (Table 47.14). Диференциалният
  режим се включва на цялата единица (`PnDEN` на трите усилвателя) и не се смесва със single-ended.
  PGAVSS: `P003` (unit 0), `P007` (unit 1) — в single-ended трябва да е вързан към AVSS0, което на
  VK_RA6M3 не е дадено, затова диференциалният вход е предпочитан.
- **ARCH-AFE-001** — при включен S&H: импеданс на източника ≤ 1 kΩ, период на семплиране ≥ 400 ns.
- **ARCH-DSP-001 / ARCH-DAC-001 / ARCH-PY-001** — DSP в C на block boundary; аудио през timed
  DAC с DTC/DMAC double buffering; Python само control/status.
- **REQ-RT-001..006** — без blocking wait, без allocation/GC след `start()`, без Python/print/IO
  от realtime callback, прекъсвания само на block boundary, движение на данни през DTC/DMAC,
  overrun/underrun се броят и се показват.

### Авторитетни източници

- **DOC-MCU-001** — RA6M3 Hardware User's Manual, **R01UH0886EJ0120 Rev.1.20**, локално:
  `ports/renesas-ra/boards/VK_RA6M3/r01uh0886ej0120-ra6m3.pdf` и `.md` (търсимата конверсия).
  Авторитет за електрически, тайминг и регистрови факти (REQ-WORK-005).
- `boards/VK_RA6M3/ra6m3_af.csv` и `pins.csv` — pin / alternate function таблици.
- Генерираният порт код е авторитет за текущото поведение на firmware-а.

### Състояние към 21.08.2026

Проверено:

- `ra/ra_storm_adc.c` вече реализира AGT → ELC → ADC12 → DTC ping-pong за **unit 0**.
  Той е шаблонът за I/Q пътя. Заделя `VECTOR_NUMBER_ADC0_SCAN_END`, но го гаси с
  `BSP_IRQ_DISABLED` — слотът съществува само като DTC activation source (`IELSRn.DTCE`).
- `ELC_EVENT_ADC1_SCAN_END = 81` съществува в FSP; `ADC121_ADI` е събитие `051h` с DTC и DMAC.
- `boards/VK_RA6M3/ra_gen/vector_data.*` вече има `ADC0_SCAN_END` на слот 48 и
  `ADC1_SCAN_END` на слот **57**; `VECTOR_DATA_IRQ_COUNT = 58`, свободни са 58–95.
  ADC1 слотът е за диагностика и `stats()`, **не** като втори независим транспортен тригер.
- `hal_data.c` има `g_adc1_cfg` с `.unit = 1`, но `.trigger = ADC_TRIGGER_SOFTWARE`.
  Не се редактира — моделът в този порт е copy-and-override по време на изпълнение.
- `r_dtc.c` е в build-а през SDHI блока (`Makefile` ред 509), `r_dmac.c` безусловно (ред 523).
- `r_gpt.c` се добавя само в BLE блока, а VK_RA6M3 няма BLE → **GPT драйверът не се компилира**.
  Наличният работещ тригер е AGT (`ra/ra_timer.c`).

Открито и незакрито:

- **I/Q кохерентност**: два независими транспорта (ADC0→I и ADC1→Q) могат тихо да се разминат
  с една проба при изгубено активиране, което дава непоправима фазова ротация. Данните трябва да
  се вземат с **едно** activation събитие — DTC chain или DMAC offset схема. `ADC1_SCAN_END`
  слотът се добавя за диагностика и `stats()`, не като втори транспортен тригер.
- **OPEN-001** — пиновете са потвърдени по `ra6m3_af.csv`, но не и по схемата на платката.
- **OPEN-002** — `fs_iq`: препоръка 48 kHz, получено чрез семплиране на 96 kHz и децимация ×2.
- **OPEN-006** — **затворен**: Table 47.14 ги изброява като съвместими.
- **OPEN-008** — липсва спецификация за честотна лента и време за установяване на PGA (гл. 60.13
  дава само усилване ±1–2 % и offset ±8 mV). За ±20 kHz трябва измерване на хардуер.
- **OPEN-009** — дали `P003` (PGAVSS000, извежда се на Arduino `A3`) е вързан към AVSS0 на платката.

Променено в репото по този проект:

- `ra/ra_adc.h`, `ra/ra_adc.c` — PGA слой за RA6M3: `ra_adc_pga_config/_ch`, `_set_gain`,
  `_get`, `_supported`, `_gain_milli`; режими OFF / BYPASS / SINGLE / DIFFERENTIAL; PGAVSS пинът
  се подготвя и в двата усилвателни режима; диференциалният вдига `PnDEN` на цялата единица и
  отхвърля смесване със single-ended.
- `machine_adc.c`, `qstrdefsport.h` — PGA константи и методи към `machine.ADC`; PGA-способните
  пинове влизат по подразбиране в `BYPASS`.
- `boards/VK_RA6M3/ra_gen/vector_data.{c,h}` — `ADC1_SCAN_END` на слот 57. Генериран файл,
  промяната е ръчна и трябва да влезе в build отчета (REQ-GIT-007).
- `boards/VK_RA6M3/machine_lcd.c` — GLCDC VSYNC брояч и `lcd.vsync()`. Извън SDR обхвата.
- `boards/VK_RA6M3/ra6m3_done.md` — журнал на промените по платката.

Последният успешен билд е **след** поправките по `PnDEN` и PGAVSS ASEL (21.08.2026 20:31,
`firmware.bin` = 1545320 B). `ra_adc.c` обаче е още uncommitted — артефактите са от dirty дърво.

### Следващи стъпки

1. Билд и отчет с таг `SDR-RA6M3-BUILD-20260821-01`.
2. Решение AGT срещу GPT за източника на тригера.
3. Двуканален вариант на `ra_storm_adc.c`: ELSR8 + ELSR10 за тригера и **едно** активиране за
   трансфера (DTC верига или DMAC offset), а `ADC1_SCAN_END` само за броене.
4. Проверка на OPEN-009 по схемата на платката, преди PGA да влезе в реалния AFE.

## Работни правила

- **REQ-DIR-002** — преди редакция изброй абсолютните пътища, които ще бъдат пипнати.
- **REQ-GIT-001/002** — преди редакция `git status --short`; никакъв revert/reset/checkout
  върху чужди промени без изрична заявка. Дървото често е dirty; `machine_lcd.c` не е част от
  тази работа.
- **REQ-GIT-007** — `ra_gen/*` са генерирани файлове. Промяна в тях се прави през FSP Smart
  Configurator или се записва изрично в build отчета.
- **REQ-WORK-004** — хардуерни твърдения искат хардуерни доказателства. Компилация, симулация
  и код-ревю са отделни класове доказателства и се назовават като такива.
- **BUILD-ENV-001..006** — билд само от MSYS2/UCRT64:

```bash
export PATH=/ucrt64/bin:/usr/bin:$PATH
cd /home/teodor/renesas_micropython/ports/renesas-ra
make BOARD=VK_RA6M3 -j8
```

  Ненулев exit код = провален билд, дори при частични обекти. Артефактите са под
  `ports/renesas-ra/build-VK_RA6M3`.

- Документите се пишат без разводняване, с академичен тон, без необяснени референции и без
  повтаряне на едно и също съдържание на две места.
