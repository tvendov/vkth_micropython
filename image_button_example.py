# image_button_example.py – LVGL Image Button example on VK-RA6M5
# Version 1.4 – оптимизирано за VK-RA6M5 с вградени LVGL възможности
# Status: ready for compilation

# ── LVGL конфигурация (lv_conf.h) ────────────────────────────────────────────
# Уверете се, че в lv_conf.h са зададени следните макроси:
#   #define LV_USE_IMGBTN        1   /* Активира widget Image Button */
#   #define LV_USE_IMG           1   /* Активира widget Image */
#   #define LV_USE_PNG           1   /* Активира PNG декодер */
#   #define LV_USE_FS_STDIO      1   /* Enable standard C FILE* file system */
#   #define LV_COLOR_DEPTH       32  /* Задава цветова дълбочина (ARGB8888) */
#   #define LV_ENABLE_GC         1   /* Активира garbage collector integration */

# ── Проверка за наличност на MicroPython модули ──────────────────────────────
def has_module(mod_name):
    try:
        __import__(mod_name)
        return True
    except ImportError:
        return False

required_modules = [
    "gc", "time", "lvgl", "os", "machine", "st77xx", "lv_utils"
]
print("=== Проверка на MicroPython модули ===")
mods_missing = []
for m in required_modules:
    status = "наличен" if has_module(m) else "ЛИПСВА"
    print(f"  {m}: {status}")
    if status == "ЛИПСВА":
        mods_missing.append(m)
if mods_missing:
    print("Внимание: липсват модули. Инсталирайте ги или активирайте чрез firmware.")

# ── Създаване на тестови изображения в паметта ──────────────────────────────
# Създаваме прости цветни бутони директно в паметта
def create_simple_button_data(width=80, height=80, pressed=False):
    """Създава прости RGB565 данни за бутон"""
    # RGB565 формат: 5 бита червено, 6 бита зелено, 5 бита синьо
    if pressed:
        # По-тъмен цвят за натиснат бутон (тъмно синьо)
        color = 0x001F  # Синьо в RGB565
    else:
        # Светъл цвят за освободен бутон (светло синьо)
        color = 0x3D7F  # Светло синьо в RGB565

    # Създаваме данните за изображението
    data = bytearray()
    for y in range(height):
        for x in range(width):
            # Добавяме border ефект
            if x < 3 or x >= width-3 or y < 3 or y >= height-3:
                border_color = 0xFFFF if not pressed else 0x8410  # Бял/сив border
                data.extend(border_color.to_bytes(2, 'little'))
            else:
                data.extend(color.to_bytes(2, 'little'))

    return bytes(data)

print("=== Създаване на тестови изображения ===")
btn_rel_data = create_simple_button_data(80, 80, pressed=False)
btn_pr_data = create_simple_button_data(80, 80, pressed=True)
print(f"  btn_rel_data: {len(btn_rel_data)} bytes")
print(f"  btn_pr_data: {len(btn_pr_data)} bytes")

# Ако няма ключови модули, може да спрем
if not has_module('lvgl'):
    raise RuntimeError("LVGL модулът е задължителен за demo-то.")

# ── Зареждане на Micropython модули ─────────────────────────────────────────
import gc, time
import lvgl as lv
from os import uname
from machine import SPI
from st77xx import St7789 as STdrv
import lv_utils

# ── Инициализация на LVGL и дисплея ───────────────────────────────────────────
lv.init()
gc.collect()
SLICE_H = 60
if "VK-RA6M5" in uname().machine:
    lcd = STdrv(
        rot=0, res=(240, 240), spi=SPI(0, baudrate=50_000_000, polarity=1),
        cs="D6", dc="D8", bl="D7", rst="D9",
        doublebuffer=False, factor=SLICE_H
    )
    lcd.set_backlight(100)
else:
    raise RuntimeError("Unsupported board: не разпознавате VK-RA6M5")
gc.collect()

# ── Създаване на основен екран ───────────────────────────────────────────────
scr = lv.obj()
lv.scr_load(scr)

# ── Инициализация на PNG декодер ──────────────────────────────────────────
# LVGL има вграден PNG декодер, който се инициализира автоматично
print("PNG декодер: използва се вградения LVGL PNG декодер")

# ── Функция за създаване на image descriptor ──────────────────────────────
def create_image_descriptor(data, width=80, height=80):
    """Създава LVGL image descriptor от RGB565 данни"""
    return lv.img_dsc_t({
        'header': {
            'w': width,
            'h': height,
            'cf': lv.COLOR_FORMAT.RGB565
        },
        'data_size': len(data),
        'data': data
    })

# ── Създаване на Image Button widget ──────────────────────────────────────
btn_rel = create_image_descriptor(btn_rel_data, 80, 80)
btn_pr = create_image_descriptor(btn_pr_data, 80, 80)
if btn_rel and btn_pr:
    imgbtn = lv.imgbtn(scr)
    imgbtn.set_src(lv.imgbtn.STATE.RELEASED, btn_rel)
    imgbtn.set_src(lv.imgbtn.STATE.PRESSED, btn_pr)
    imgbtn.center()

    # ── Callback за събития на image button ─────────────────────────────────
    def btn_event_cb(event):
        if event.get_code() == lv.EVENT.CLICKED:
            print("Image button clicked!")
    imgbtn.add_event_cb(btn_event_cb, lv.EVENT.ALL)
    print("Image Button създаден успешно!")
else:
    # Фолбеж: създаване на стандартен бутон при липсващи изображения
    print("Demo: Image Button няма да бъде създаден поради липсващи изображения. Използва се fallback бутон.")
    btn = lv.btn(scr)
    btn.center()
    label = lv.label(btn)
    label.set_text("Button")
    # Callback за събития на fallback бутона
    def btn_fallback_event(event):
        if event.get_code() == lv.EVENT.CLICKED:
            print("Fallback button clicked!")
    btn.add_event_cb(btn_fallback_event, lv.EVENT.ALL)
    print("Fallback Button създаден успешно!")

# ── Idle Loop ───────────────────────────────────────────────────────────────
print("Стартиране на LVGL event loop...")
last = time.ticks_ms()
while True:
    lv.timer_handler()
    time.sleep_ms(5)
    if time.ticks_diff(time.ticks_ms(), last) > 10000:
        gc.collect()
        last = time.ticks_ms()
