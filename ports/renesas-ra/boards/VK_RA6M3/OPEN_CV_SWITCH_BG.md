# OpenCV ON/OFF при непроменен QSPI дял

`MICROPY_PY_CV2_QSPI=0` е стандартният build без OpenCV. `=1` включва
SparkFun OpenCV и изисква неговия `USER_C_MODULES` път. Един и същ път може
да остане зададен и при OFF: тогава модулът не добавя код, ulab или C-пул.

Това е ключ при компилация, не Python настройка по време на работа.
`import cv2` не се предлага в OFF firmware. SDR остава отделно редактируемо
Python приложение; този ключ не го прави frozen и не променя `.py`/`.mpy`.

| Профил | Зададен heap | Файлов дял | Запазен кодов дял |
|---|---:|---:|---:|
| OFF | 284 KiB | 12 MiB | 4 MiB, не се изпълнява OpenCV |
| ON | 204 KiB | 12 MiB | 4 MiB, използва се за OpenCV |

Heap е цялата linker област, не `gc.mem_free()` след стартиране.
OFF връща 80 KiB от нея; действителният резултат се проверява в ELF.

## Компилация от SDR проектната папка

```powershell
.\build_sdr.ps1 -OpenCV Off -Jobs 16
.\build_sdr.ps1 -OpenCV On  -Jobs 16
```

Wrapper-ът е в `C:\Users\teodor\Desktop\stem\sdr\SDR_TRANCEIVER_RA6M3`.
Използва съществуващите `build-VK_RA6M3` и `build-CV2-QSPI-noalloc`,
съответно. Python за build е изрично native MinGW, с изчистени
`PYTHONHOME`/`PYTHONPATH`. Скриптът само компилира и проверява; не качва,
не ресетва, не отваря COM/J-Link и не променя файлове на платката.

Директният OpenCV build скрипт също приема `-OpenCV Off|On`:
`vendor/micropython-opencv/platforms/ra6m3/build_profile.ps1`.
И двата PowerShell входа по подразбиране са **OFF**. ON е само изричен избор.

## Защита на външния flash

Разпределението вече е свойство на платката, независимо от OpenCV:

- файлове: `0x60000000..0x60BFFFFF`;
- запазен код: `0x60C00000..0x60FFFFFF`.

`MICROPY_HW_QSPI_CODE_RESERVE` запазва границите за erase/write и
забраната за автоматично форматиране при неуспешен mount, включително OFF.
Умишлен factory reset остава отделна разрушителна потребителска операция.
Стар 16 MiB файлов дял изисква архив и изрична миграция, не автоматична промяна.

При бъдещо качване на OFF се използва неговият вътрешен `firmware.bin`;
старият QSPI образ може да остане неизползван. Не трябва да се качва стар
`firmware-qspi.bin` само защото е останал в някоя build папка. При ON двата
образа трябва да са съответстваща двойка. Качването е отделна стъпка.

## Проверки

`platforms/ra6m3/tests/test_profile_switch.py` проверява изходните защити,
отказ при стар 16 MiB linker layout и действителните ELF граници и heap.
OFF трябва да няма CV2/OpenCV символи, неговия пул или QSPI секции.
ON трябва да запазва модула, пула и правилното външно начало.
`profile-switch-audit.json` е build доказателство, не тест на платка.
