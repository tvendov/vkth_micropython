# MOISTURE.PY - автоматична поливачка за саксия за VK_RA4M2.
# Всеки ред е коментиран на български за по-лесна проверка и поддръжка.
# P206 е капацитивният вход за влажност, калибриран от test_cap_pf.py.
# P014 е ADC входът за проверка дали помпата работи на сухо.
# P302 е левият бутон с pull-up и активна нула.
# P301 е десният бутон с pull-up и активна нула.
# P001 е червен светодиод, активна логическа 1, състояние сухо.
# P002 е зелен светодиод, активна логическа 1, състояние влажно.
# P100 е син светодиод, активна логическа 1, състояние мокро.
# P111 е PWM изходът за помпата.
# P109/P110 са I2C SCL/SDA за SSD1306 OLED дисплея.

from machine import ADC, Pin, PWM, SoftI2C, Timer, TouchPad  # Импортираме хардуерните класове от MicroPython.
import framebuf  # Импортираме framebuf за рисуване върху OLED дисплея.
import time  # Импортираме time за кратко изчакване в главния цикъл.
import dataflash  # Импортираме dataflash за запис на границите във Data Flash.


PIN_BTN_LEFT = "P302"  # Задаваме пина на левия бутон.
PIN_BTN_RIGHT = "P301"  # Задаваме пина на десния бутон.
PIN_CAP = "P206"  # Задаваме капацитивния вход за влажност.
PIN_DRY_ADC = "P014"  # Задаваме ADC входа за сух ход на помпата.
PIN_LED_RED = "P001"  # Задаваме червения светодиод за състояние сухо.
PIN_LED_GREEN = "P002"  # Задаваме зеления светодиод за състояние влажно.
PIN_LED_BLUE = "P100"  # Задаваме синия светодиод за състояние мокро.
PIN_PUMP_PWM = "P111"  # Задаваме PWM пина за управление на помпата.
PIN_OLED_SCL = "P109"  # Задаваме SCL пина за OLED дисплея.
PIN_OLED_SDA = "P110"  # Задаваме SDA пина за OLED дисплея.

OLED_W = 128  # Задаваме ширината на OLED дисплея в пиксели.
OLED_H = 16  # Задаваме височината на OLED дисплея в пиксели.
OLED_ADDR = 0x3C  # Задаваме стандартния I2C адрес на SSD1306.
OLED_FREQ = 400000  # Задаваме I2C честотата за OLED дисплея.

STATE_DRY = 0  # Дефинираме състояние сухо.
STATE_MOIST = 1  # Дефинираме състояние влажно.
STATE_WET = 2  # Дефинираме състояние мокро.
STATE_NAMES = ("DRY", "MOIST", "WET")  # Дефинираме текстови имена за печат в конзолата.
STATE_DISPLAY = ("SUH", "VLA", "MOK")  # Дефинираме кратки текстове за OLED дисплея.

PF_DEFAULT_LOWER = 50  # Задаваме default долна граница на влажно при празен Data Flash.
PF_DEFAULT_UPPER = 100  # Задаваме default горна граница на влажно при празен Data Flash.
PF_MAX_EXPECTED = 150.0  # Задаваме очакван горен диапазон за документация.

PUMP_FREQ_HZ = 1000  # Задаваме PWM честотата на помпата.
PUMP_DUTY_ON = 75  # Задаваме duty cycle при включена помпа.
PUMP_DUTY_OFF = 0  # Задаваме duty cycle при изключена помпа.

DRY_RUN_ADC_LIMIT = None  # Оставяме прага за сух ход неизползван до реална калибрация.
DF_BLOCK = 0  # Използваме Data Flash блок 0 за параметрите на поливачката.
DF_OFFSET = 0  # Започваме записа от offset 0 в Data Flash.
DF_MAGIC = b"MOIS"  # Дефинираме магическа дума за разпознаване на валиден запис.
DF_VERSION = 1  # Дефинираме версия на формата за запис в Data Flash.
DF_RECORD_LEN = 16  # Записът е фиксиран 16 байта за по-надеждно подравняване.


P206_CAL_POINTS = (  # Започваме калибрационната таблица за P206.
    (6100, 0.0),  # Точка 1: сурова стойност SEN към 0.0 pF.
    (17777, 33.0),  # Точка 2: сурова стойност SEN към 33.0 pF.
    (27400, 68.0),  # Точка 3: сурова стойност SEN към 68.0 pF.
    (45177, 99.0),  # Точка 4: сурова стойност SEN към 99.0 pF.
    (62954, 132.0),  # Точка 5: сурова стойност SEN към 132.0 pF.
)  # Завършваме калибрационната таблица за P206.


_SMALL_BUF = bytearray(64)  # Създаваме малък буфер за оригиналния 8x8 текст.
_SMALL_FB = framebuf.FrameBuffer(_SMALL_BUF, 64, 8, framebuf.MONO_VLSB)  # Създаваме малък frame buffer за text2x.
_tick_1s = False  # Създаваме флаг, който софтуерният таймер вдига всяка секунда.


def estimate_pf_from_p206_sen(sen):  # Дефинираме функция за преобразуване от SEN към pF.
    if sen <= P206_CAL_POINTS[0][0]:  # Проверяваме дали стойността е под първата калибрационна точка.
        x0, y0 = P206_CAL_POINTS[0]  # Вземаме първата точка за екстраполация.
        x1, y1 = P206_CAL_POINTS[1]  # Вземаме втората точка за екстраполация.
        return y0 + (sen - x0) * (y1 - y0) / (x1 - x0)  # Връщаме линейно изчислените pF.

    for i in range(len(P206_CAL_POINTS) - 1):  # Обхождаме всички съседни калибрационни точки.
        x0, y0 = P206_CAL_POINTS[i]  # Вземаме текущата калибрационна точка.
        x1, y1 = P206_CAL_POINTS[i + 1]  # Вземаме следващата калибрационна точка.
        if x0 <= sen <= x1:  # Проверяваме дали SEN попада между тези две точки.
            return y0 + (sen - x0) * (y1 - y0) / (x1 - x0)  # Връщаме линейна интерполация в pF.

    x0, y0 = P206_CAL_POINTS[-2]  # Вземаме предпоследната точка за горна екстраполация.
    x1, y1 = P206_CAL_POINTS[-1]  # Вземаме последната точка за горна екстраполация.
    return y0 + (sen - x0) * (y1 - y0) / (x1 - x0)  # Връщаме pF над последната точка.


def clamp_param_pf(pf):  # Дефинираме ограничаване на параметър за запис във flash.
    v = int(pf + 0.5)  # Закръгляме текущия капацитет до цяло pF.
    if v < 0:  # Проверяваме дали стойността е под нула.
        return 0  # Ограничаваме долната стойност до 0 pF.
    if v > 999:  # Проверяваме дали стойността е над 999.
        return 999  # Ограничаваме горната стойност до 999 pF.
    return v  # Връщаме нормализираната стойност.


def limits_are_valid(lower, upper):  # Дефинираме проверка дали границите са валидни без автоматична поправка.
    return 0 <= lower <= 999 and 0 <= upper <= 999 and lower < upper  # Валидно е само ако lower е под upper.


def state_from_pf(pf, lower_limit, upper_limit):  # Дефинираме state machine преход според калибрираните граници.
    if pf < lower_limit:  # Ако капацитетът е под долната граница, почвата е суха.
        return STATE_DRY  # Връщаме състояние сухо.
    if pf < upper_limit:  # Ако капацитетът е между долната и горната граница, почвата е влажна.
        return STATE_MOIST  # Връщаме състояние влажно.
    return STATE_WET  # При горната граница и нагоре връщаме състояние мокро.


def clamp_display_pf(pf):  # Дефинираме функция за ограничаване на pF за дисплея.
    v = int(pf + 0.5)  # Закръгляме pF до цяло число.
    if v < 0:  # Проверяваме дали числото е отрицателно.
        return 0  # При отрицателна стойност показваме нула.
    if v > 999:  # Проверяваме дали числото е над 3 цифри.
        return 999  # Ограничаваме дисплея до 999.
    return v  # Връщаме нормалната закръглена стойност.


def format_oled_text(state, pf):  # Дефинираме текста за OLED дисплея.
    return "%s%03dpF" % (STATE_DISPLAY[state], clamp_display_pf(pf))  # Връщаме текст като SUH049pF.


def format_limits_text(lower, upper):  # Дефинираме текст за показване на границите.
    return "L%03dU%03d" % (clamp_display_pf(lower), clamp_display_pf(upper))  # Връщаме формат L050U100.


def df_checksum_bytes(data):  # Дефинираме checksum върху байтов масив.
    s = 0  # Започваме сумата от нула.
    for b in data:  # Обхождаме всички байтове от payload-а.
        s = (s + b) & 0xFF  # Натрупваме 8-bit сума.
    return s  # Връщаме checksum байта.


def make_df_record(lower, upper):  # Дефинираме създаване на binary запис за Data Flash.
    record = bytearray(DF_RECORD_LEN)  # Създаваме фиксиран 16-байтов запис.
    record[0:4] = DF_MAGIC  # Записваме magic думата в началото.
    record[4] = DF_VERSION  # Записваме версията на формата.
    record[5] = lower & 0xFF  # Записваме младшия байт на долната граница.
    record[6] = (lower >> 8) & 0xFF  # Записваме старшия байт на долната граница.
    record[7] = upper & 0xFF  # Записваме младшия байт на горната граница.
    record[8] = (upper >> 8) & 0xFF  # Записваме старшия байт на горната граница.
    record[15] = df_checksum_bytes(record[0:15])  # Записваме checksum върху първите 15 байта.
    return bytes(record)  # Връщаме immutable bytes за dataflash.write().


def load_limits_from_flash():  # Дефинираме зареждане на границите от Data Flash.
    try:  # Опитваме да прочетем persistent записа.
        raw = bytes(dataflash.read(DF_OFFSET, DF_RECORD_LEN))  # Четем 16 байта от Data Flash.
    except Exception as e:  # Хващаме грешка при четене.
        print("Data Flash read failed:", e)  # Печатаме причината за неуспешното четене.
        return PF_DEFAULT_LOWER, PF_DEFAULT_UPPER, False, "DF ERR"  # Връщаме default стойности и грешка.
    if raw[0:4] != DF_MAGIC:  # Проверяваме magic думата.
        print("Data Flash limits missing, using defaults")  # Печатаме, че няма валиден запис.
        return PF_DEFAULT_LOWER, PF_DEFAULT_UPPER, False, "NO CFG"  # Връщаме default стойности и статус.
    if raw[4] != DF_VERSION:  # Проверяваме версията на формата.
        print("Data Flash limits version failed, using defaults")  # Печатаме грешка във версията.
        return PF_DEFAULT_LOWER, PF_DEFAULT_UPPER, False, "CFG ERR"  # Връщаме default стойности и грешка.
    if raw[15] != df_checksum_bytes(raw[0:15]):  # Проверяваме checksum байта.
        print("Data Flash limits checksum failed, using defaults")  # Печатаме checksum грешка.
        return PF_DEFAULT_LOWER, PF_DEFAULT_UPPER, False, "CFG ERR"  # Връщаме default стойности и грешка.
    lower = raw[5] | (raw[6] << 8)  # Декодираме долната граница от два байта.
    upper = raw[7] | (raw[8] << 8)  # Декодираме горната граница от два байта.
    if not limits_are_valid(lower, upper):  # Проверяваме дали прочетените граници са логически валидни.
        print("Data Flash limits invalid, using defaults")  # Печатаме грешна конфигурация.
        return PF_DEFAULT_LOWER, PF_DEFAULT_UPPER, False, "CFG ERR"  # Връщаме default стойности и грешка.
    print("Data Flash limits loaded:", lower, upper)  # Печатаме заредените граници.
    return lower, upper, True, "OK"  # Връщаме границите и флаг за валиден flash.


def save_limits_to_flash(lower, upper):  # Дефинираме запис на границите в Data Flash.
    if not limits_are_valid(lower, upper):  # Проверяваме дали конфигурацията е валидна преди запис.
        print("REFUSE save invalid limits:", lower, upper)  # Печатаме отказ от запис на грешни граници.
        return False  # Връщаме неуспех, без да пишем във Data Flash.
    record = make_df_record(lower, upper)  # Създаваме binary записа.
    try:  # Опитваме erase и write както в примерите.
        dataflash.erase_block(DF_BLOCK)  # Изтриваме Data Flash блок 0 преди нов запис.
        written = dataflash.write(DF_OFFSET, record)  # Записваме persistent параметрите.
        readback = bytes(dataflash.read(DF_OFFSET, DF_RECORD_LEN))  # Четем обратно записаните байтове.
        if readback != record:  # Проверяваме дали read-back съвпада с очаквания запис.
            print("Data Flash verify failed")  # Печатаме грешка при проверка след запис.
            print("expected:", record)  # Печатаме очакваните байтове.
            print("readback:", readback)  # Печатаме прочетените байтове.
            return False  # Връщаме неуспех при различен read-back.
        print("Data Flash limits saved:", lower, upper, "bytes:", written)  # Печатаме резултата от записа.
        return written == len(record)  # Връщаме дали са записани всички байтове.
    except Exception as e:  # Хващаме грешка при erase/write.
        print("Data Flash save failed:", e)  # Печатаме причината за неуспешния запис.
        return False  # Връщаме неуспех.


class SSD1306:  # Дефинираме минимален клас за SSD1306 OLED 128x16.
    _INIT_CMDS = (  # Започваме списъка с команди за инициализация на дисплея.
        0xAE, 0xD5, 0x80, 0xA8, 0x0F, 0xD3, 0x00, 0x40,  # Първа част от SSD1306 init командите.
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,  # Втора част от SSD1306 init командите.
        0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,  # Трета част и включване на дисплея.
    )  # Завършваме списъка с init команди.

    def __init__(self, i2c, addr=OLED_ADDR):  # Създаваме OLED обект с I2C и адрес.
        self.i2c = i2c  # Запазваме I2C обекта.
        self.addr = addr  # Запазваме I2C адреса на дисплея.
        self._tx_buf = bytearray(1 + OLED_W * OLED_H // 8)  # Създаваме буфер за контролен байт плюс пиксели.
        self._tx_buf[0] = 0x40  # Задаваме контролния байт за данни към SSD1306.
        self._fb_view = memoryview(self._tx_buf)[1:]  # Създаваме изглед към частта с пиксели.
        self.fb = framebuf.FrameBuffer(self._fb_view, OLED_W, OLED_H, framebuf.MONO_VLSB)  # Създаваме frame buffer.
        self._cmd1 = bytearray(2)  # Създаваме малък буфер за единична команда.
        self._cmd1[0] = 0x00  # Задаваме контролен байт за команда.
        self._show_cmd = bytearray(b"\x00\x21\x00\x7F\x22\x00\x01")  # Задаваме прозореца за обновяване на дисплея.
        self._init_panel()  # Изпращаме init командите към дисплея.
        self.fill(0)  # Изчистваме frame buffer-а.
        self.show()  # Показваме празния екран.

    def _cmd(self, c):  # Дефинираме помощна функция за изпращане на команда.
        self._cmd1[1] = c  # Поставяме командата във втория байт.
        self.i2c.writeto(self.addr, self._cmd1)  # Изпращаме командата към дисплея.

    def _init_panel(self):  # Дефинираме инициализация на OLED панела.
        for c in self._INIT_CMDS:  # Обхождаме всички init команди.
            self._cmd(c)  # Изпращаме текущата команда.

    def fill(self, color):  # Дефинираме изчистване или запълване на дисплея.
        self.fb.fill(color)  # Запълваме frame buffer-а с избрания цвят.

    def show(self):  # Дефинираме изпращане на frame buffer-а към дисплея.
        self.i2c.writeto(self.addr, self._show_cmd)  # Изпращаме адресния прозорец.
        self.i2c.writeto(self.addr, self._tx_buf)  # Изпращаме пикселните данни.


def text2x(big_fb, text, x, y, color):  # Дефинираме 2x шрифта от примера i2c_display_ok.py.
    n = len(text)  # Измерваме дължината на текста.
    if n > 8:  # Проверяваме дали текстът е по-дълъг от 8 символа.
        n = 8  # Ограничаваме текста до 8 символа за 128x16 OLED.
    _SMALL_FB.fill(0)  # Изчистваме малкия frame buffer.
    _SMALL_FB.text(text, 0, 0, 1)  # Рисуваме стандартен 8x8 текст в малкия буфер.
    src_w = n * 8  # Изчисляваме ширината на текста в малкия буфер.
    for py in range(8):  # Обхождаме редовете на малкия текст.
        for px in range(src_w):  # Обхождаме колоните на малкия текст.
            if _SMALL_FB.pixel(px, py):  # Проверяваме дали текущият малък пиксел свети.
                xx = x + (px << 1)  # Изчисляваме X координата в големия буфер.
                yy = y + (py << 1)  # Изчисляваме Y координата в големия буфер.
                big_fb.pixel(xx, yy, color)  # Рисуваме първия пиксел от 2x2 блока.
                big_fb.pixel(xx + 1, yy, color)  # Рисуваме втория пиксел от 2x2 блока.
                big_fb.pixel(xx, yy + 1, color)  # Рисуваме третия пиксел от 2x2 блока.
                big_fb.pixel(xx + 1, yy + 1, color)  # Рисуваме четвъртия пиксел от 2x2 блока.


def oled_probe(i2c):  # Дефинираме търсене на OLED дисплей по I2C.
    devs = i2c.scan()  # Сканираме I2C шината.
    print("I2C scan:", devs)  # Печатаме намерените I2C адреси.
    if 0x3C in devs:  # Проверяваме стандартния адрес 0x3C.
        return 0x3C  # Връщаме адрес 0x3C.
    if 0x3D in devs:  # Проверяваме алтернативния адрес 0x3D.
        return 0x3D  # Връщаме адрес 0x3D.
    return None  # Връщаме None, ако дисплей не е намерен.


def init_oled():  # Дефинираме инициализация на OLED дисплея.
    i2c = SoftI2C(scl=Pin(PIN_OLED_SCL, Pin.OPEN_DRAIN), sda=Pin(PIN_OLED_SDA, Pin.OPEN_DRAIN), freq=OLED_FREQ)  # Създаваме SoftI2C.
    addr = oled_probe(i2c)  # Търсим дисплей на шината.
    if addr is None:  # Проверяваме дали дисплей не е намерен.
        print("OLED NOT found - headless")  # Печатаме, че системата ще работи без дисплей.
        return None  # Връщаме None при липса на дисплей.
    try:  # Започваме защитен опит за създаване на OLED обект.
        oled = SSD1306(i2c, addr)  # Създаваме SSD1306 обект.
        print("OLED ready at addr", hex(addr))  # Печатаме адреса на готовия дисплей.
        return oled  # Връщаме готовия OLED обект.
    except Exception as e:  # Хващаме грешка при инициализация на дисплея.
        print("OLED init failed:", e)  # Печатаме грешката от OLED инициализацията.
        return None  # Връщаме None, за да продължи програмата без дисплей.


def render_oled(oled, state, pf):  # Дефинираме обновяване на OLED дисплея.
    if oled is None:  # Проверяваме дали OLED дисплей липсва.
        return  # Излизаме без действие при липса на дисплей.
    oled.fill(0)  # Изчистваме дисплея.
    text2x(oled.fb, format_oled_text(state, pf), 0, 0, 1)  # Рисуваме състояние и pF с 2x шрифт.
    oled.show()  # Изпращаме изображението към OLED дисплея.


def render_text_oled(oled, text):  # Дефинираме показване на кратък текст на OLED.
    if oled is None:  # Проверяваме дали OLED дисплей липсва.
        return  # Излизаме без действие при липса на дисплей.
    oled.fill(0)  # Изчистваме дисплея преди новия текст.
    text2x(oled.fb, text, 0, 0, 1)  # Рисуваме текста със същия 2x шрифт.
    oled.show()  # Изпращаме текста към OLED дисплея.


class Leds:  # Дефинираме клас за трите светодиода, които светят при логическа 1.
    def __init__(self):  # Създаваме LED обектите.
        self.red = Pin(PIN_LED_RED, Pin.OUT, value=0)  # Настройваме червения LED като изгасен.
        self.green = Pin(PIN_LED_GREEN, Pin.OUT, value=0)  # Настройваме зеления LED като изгасен.
        self.blue = Pin(PIN_LED_BLUE, Pin.OUT, value=0)  # Настройваме синия LED като изгасен.

    def off_all(self):  # Дефинираме изключване на всички светодиоди.
        self.red.value(0)  # Гасим червения LED с логическа 0.
        self.green.value(0)  # Гасим зеления LED с логическа 0.
        self.blue.value(0)  # Гасим синия LED с логическа 0.

    def show_state(self, state):  # Дефинираме показване на текущото състояние със светодиодите.
        self.off_all()  # Първо гасим всички, за да няма два светодиода едновременно.
        if state == STATE_DRY:  # Проверяваме дали текущото състояние е сухо.
            self.red.value(1)  # Светваме само червения LED с логическа 1.
        elif state == STATE_MOIST:  # Проверяваме дали текущото състояние е влажно.
            self.green.value(1)  # Светваме само зеления LED с логическа 1.
        elif state == STATE_WET:  # Проверяваме дали текущото състояние е мокро.
            self.blue.value(1)  # Светваме само синия LED с логическа 1.


class Pump:  # Дефинираме клас за управление на помпата.
    def __init__(self):  # Инициализираме помпата.
        self._pwm = None  # Подготвяме поле за PWM обекта.
        self._pin = None  # Подготвяме поле за fallback цифров пин.
        self._using_pwm = False  # Записваме, че PWM още не е потвърден.
        try:  # Опитваме да създадем PWM на P111.
            self._pwm = PWM(Pin(PIN_PUMP_PWM), freq=PUMP_FREQ_HZ, duty=PUMP_DUTY_OFF)  # Създаваме PWM с изключена помпа.
            self._using_pwm = True  # Отбелязваме, че PWM е активен.
            print("Pump PWM ready on", PIN_PUMP_PWM)  # Печатаме, че PWM е готов.
        except Exception as e:  # Хващаме грешка, ако P111 не поддържа PWM в текущия firmware.
            print("Pump PWM init failed on", PIN_PUMP_PWM, "- fallback Pin.OUT:", e)  # Печатаме fallback причината.
            self._pin = Pin(PIN_PUMP_PWM, Pin.OUT, value=0)  # Създаваме цифров изход като резервен режим.
        self.off()  # Гарантираме, че помпата започва изключена.

    def duty(self, duty_percent):  # Дефинираме задаване на duty cycle.
        if duty_percent < 0:  # Проверяваме за отрицателен duty cycle.
            duty_percent = 0  # Ограничаваме duty cycle до минимум 0.
        if duty_percent > 100:  # Проверяваме за duty cycle над 100.
            duty_percent = 100  # Ограничаваме duty cycle до максимум 100.
        if self._using_pwm:  # Проверяваме дали работим в PWM режим.
            self._pwm.duty(duty_percent)  # Задаваме PWM duty cycle в проценти.
        else:  # Влизаме в резервен цифров режим.
            self._pin.value(1 if duty_percent > 0 else 0)  # Пускаме или спираме цифровия изход.

    def on(self):  # Дефинираме включване на помпата.
        self.duty(PUMP_DUTY_ON)  # Задаваме работния duty cycle на помпата.

    def off(self):  # Дефинираме изключване на помпата.
        self.duty(PUMP_DUTY_OFF)  # Задаваме duty cycle 0.

    def deinit(self):  # Дефинираме освобождаване на помпата.
        self.off()  # Първо спираме помпата.
        if self._using_pwm:  # Проверяваме дали има PWM обект.
            self._pwm.deinit()  # Деинициализираме PWM обекта.


class MoistureSensor:  # Дефинираме клас за капацитивния сензор за влажност.
    def __init__(self):  # Инициализираме TouchPad сензора.
        self.tp = TouchPad(Pin(PIN_CAP))  # Създаваме TouchPad на P206.
        TouchPad.cap_global_config(  # Задаваме глобалните CTSU настройки от test_cap_pf.py.
            ctsuclk_div=4,  # Задаваме делител на CTSU clock.
            prmode=2,  # Задаваме режим с 62 pulses.
            prratio=0,  # Задаваме pulse ratio.
            atune1=1,  # Включваме high-current настройка.
            noise=True,  # Включваме noise reduction.
            auto_offset=False,  # Изключваме touch-oriented auto offset.
        )  # Завършваме глобалната CTSU конфигурация.
        self.tp.cap_config(  # Задаваме конфигурацията на P206 канала.
            sdpa=10,  # Задаваме drive frequency параметър.
            snum=11,  # Задаваме броя групи за измерване.
            icog=3,  # Задаваме токов коефициент 40%.
            so=0,  # Задаваме offset current на 0.
            auto_ssdiv=True,  # Включваме автоматично изчисляване на ssdiv.
        )  # Завършваме конфигурацията на P206 канала.
        tim = self.tp.timing()  # Прочитаме timing информацията за диагностика.
        print("P206 fdrive_hz:", tim["base_hz"])  # Печатаме drive честотата.
        print("P206 gate_ns  :", tim["gate_ns"])  # Печатаме gate времето.

    def read(self):  # Дефинираме четене на капацитивния сензор.
        sen, ref = self.tp.read_counts()  # Четем SEN и REF броячите от TouchPad.
        pf = estimate_pf_from_p206_sen(sen)  # Преобразуваме SEN към pF.
        return sen, ref, pf  # Връщаме сурови стойности и изчислен pF.


def _timer_cb(timer):  # Дефинираме callback за софтуерния таймер.
    global _tick_1s  # Посочваме, че използваме глобалния tick флаг.
    _tick_1s = True  # Вдигаме флага за изпълнение на state machine.


def main():  # Дефинираме главната програма.
    global _tick_1s  # Посочваме, че main променя глобалния tick флаг.

    print("=== VK_RA4M2 automatic moisture pump ===")  # Печатаме заглавие на програмата.
    print("P206 capacitance state machine, 1 second software timer")  # Печатаме кратко описание.

    btn_left = Pin(PIN_BTN_LEFT, Pin.IN, Pin.PULL_UP)  # Инициализираме левия бутон с pull-up.
    btn_right = Pin(PIN_BTN_RIGHT, Pin.IN, Pin.PULL_UP)  # Инициализираме десния бутон с pull-up.
    dry_adc = ADC(Pin(PIN_DRY_ADC), bits=12)  # Инициализираме ADC входа P014 в 12-bit режим.
    leds = Leds()  # Създаваме обекта за светодиодите.
    pump = Pump()  # Създаваме обекта за помпата.
    sensor = MoistureSensor()  # Създаваме капацитивния сензор P206.
    oled = init_oled()  # Инициализираме OLED дисплея.
    lower_limit, upper_limit, limits_from_flash, limits_status = load_limits_from_flash()  # Зареждаме долната и горната граница от Data Flash.
    if not limits_from_flash and limits_status == "CFG ERR":  # Проверяваме дали Data Flash съдържа грешна конфигурация.
        render_text_oled(oled, "CFG ERR")  # Индикираме грешна конфигурация на OLED.
        print("BOOT config error, defaults will be used")  # Печатаме, че ще ползваме default стойности.
        time.sleep_ms(2000)  # Оставяме съобщението за грешка видимо за 2 секунди.
    elif not limits_from_flash and limits_status == "DF ERR":  # Проверяваме дали има грешка при четене от Data Flash.
        render_text_oled(oled, "DF ERR")  # Индикираме Data Flash грешка на OLED.
        print("BOOT dataflash error, defaults will be used")  # Печатаме, че ще ползваме default стойности.
        time.sleep_ms(2000)  # Оставяме съобщението за грешка видимо за 2 секунди.
    render_text_oled(oled, format_limits_text(lower_limit, upper_limit))  # Показваме границите след reset във формат L050U100.
    print("BOOT limits lower=%d upper=%d flash=%d status=%s" % (lower_limit, upper_limit, limits_from_flash, limits_status))  # Печатаме boot границите за диагностика.
    time.sleep_ms(2000)  # Оставяме boot стойностите видими на дисплея за 2 секунди.

    soft_timer = Timer(-1)  # Създаваме софтуерен таймер.
    soft_timer.init(period=1000, mode=Timer.PERIODIC, callback=_timer_cb)  # Стартираме таймера на 1 секунда.
    _tick_1s = True  # Заявяваме първо измерване веднага след старта.

    last_state = None  # Запазваме последното състояние за печат само при промяна.
    prev_left_pressed = False  # Запазваме предишното състояние на левия бутон за edge-detect.
    prev_right_pressed = False  # Запазваме предишното състояние на десния бутон за edge-detect.

    try:  # Започваме основния защитен цикъл.
        while True:  # Стартираме безкрайния работен цикъл.
            if not _tick_1s:  # Проверяваме дали още няма нов 1-секунден tick.
                time.sleep_ms(20)  # Изчакваме кратко, за да не товарим процесора.
                continue  # Връщаме се в началото на цикъла.

            _tick_1s = False  # Изчистваме tick флага, защото ще обработим текущата секунда.

            sen, ref, pf = sensor.read()  # Измерваме P206 и изчисляваме pF.
            adc_raw = dry_adc.read()  # Четем ADC стойността от P014.
            left_pressed = btn_left.value() == 0  # Проверяваме левия бутон, активен на нула.
            right_pressed = btn_right.value() == 0  # Проверяваме десния бутон, активен на нула.
            limits_saved_now = False  # Подготвяме флаг дали този tick е записал нови граници.
            if left_pressed and not prev_left_pressed:  # Проверяваме ново натискане на левия бутон.
                candidate_lower = clamp_param_pf(pf)  # Маркираме текущия pF като кандидат за долна граница.
                if candidate_lower >= upper_limit:  # Проверяваме дали новата долна граница е грешна спрямо горната.
                    render_text_oled(oled, "CFG ERR")  # Индикираме грешна конфигурация на OLED.
                    print("LEFT refused lower=%d upper=%d" % (candidate_lower, upper_limit))  # Печатаме отказания запис.
                elif save_limits_to_flash(candidate_lower, upper_limit):  # Опитваме запис само при логически валидна двойка.
                    lower_limit = candidate_lower  # При успешен запис приемаме новата долна граница.
                    render_text_oled(oled, format_limits_text(lower_limit, upper_limit))  # Показваме записаните граници на OLED.
                else:  # Влизаме тук при неуспешен flash запис или read-back verify.
                    render_text_oled(oled, "DF ERR")  # Индикираме Data Flash грешка на OLED.
                    print("LEFT flash save failed lower=%d upper=%d" % (candidate_lower, upper_limit))  # Печатаме неуспешния flash запис.
                limits_saved_now = True  # Отбелязваме, че OLED вече показва новите граници.
            if right_pressed and not prev_right_pressed:  # Проверяваме ново натискане на десния бутон.
                candidate_upper = clamp_param_pf(pf)  # Маркираме текущия pF като кандидат за горна граница.
                if candidate_upper <= lower_limit:  # Проверяваме дали новата горна граница е грешна спрямо долната.
                    render_text_oled(oled, "CFG ERR")  # Индикираме грешна конфигурация на OLED.
                    print("RIGHT refused lower=%d upper=%d" % (lower_limit, candidate_upper))  # Печатаме отказания запис.
                elif save_limits_to_flash(lower_limit, candidate_upper):  # Опитваме запис само при логически валидна двойка.
                    upper_limit = candidate_upper  # При успешен запис приемаме новата горна граница.
                    render_text_oled(oled, format_limits_text(lower_limit, upper_limit))  # Показваме записаните граници на OLED.
                else:  # Влизаме тук при неуспешен flash запис или read-back verify.
                    render_text_oled(oled, "DF ERR")  # Индикираме Data Flash грешка на OLED.
                    print("RIGHT flash save failed lower=%d upper=%d" % (lower_limit, candidate_upper))  # Печатаме неуспешния flash запис.
                limits_saved_now = True  # Отбелязваме, че OLED вече показва новите граници.
            prev_left_pressed = left_pressed  # Запомняме текущото състояние на левия бутон.
            prev_right_pressed = right_pressed  # Запомняме текущото състояние на десния бутон.
            state = state_from_pf(pf, lower_limit, upper_limit)  # Определяме състоянието според текущите граници.

            if state == STATE_DRY:  # Проверяваме дали почвата е суха.
                pump.on()  # Пускаме помпата при сухо.
            else:  # Влизаме при влажно или мокро.
                pump.off()  # Спираме помпата при влажно и мокро.

            dry_run = False  # Подготвяме флаг за сух ход на помпата.
            if DRY_RUN_ADC_LIMIT is not None and state == STATE_DRY:  # Проверяваме дали dry-run прагът е настроен и помпата работи.
                dry_run = adc_raw < DRY_RUN_ADC_LIMIT  # Изчисляваме dry-run условие според ADC прага.
                if dry_run:  # Проверяваме дали е засечен сух ход.
                    pump.off()  # Спираме помпата за защита.

            leds.show_state(state)  # Обновяваме светодиодите според state machine.
            if not limits_saved_now:  # Проверяваме дали не трябва да задържим екрана с границите.
                render_oled(oled, state, pf)  # Обновяваме OLED дисплея със състояние и pF.

            if state != last_state:  # Проверяваме дали състоянието се е променило.
                print("STATE ->", STATE_NAMES[state])  # Печатаме новото състояние.
                last_state = state  # Запомняме текущото състояние.

            print("sen=%5d ref=%5d pf=%6.1f low=%3d up=%3d state=%s pump=%s adc=%4d L=%d R=%d dry_run=%d" %  # Подготвяме диагностичен ред.
                  (sen, ref, pf, lower_limit, upper_limit, STATE_NAMES[state],  # Добавяме SEN, REF, pF, граници и име на състояние.
                   "ON" if state == STATE_DRY and not dry_run else "OFF",  # Показваме дали помпата трябва да е включена.
                   adc_raw, left_pressed, right_pressed, dry_run))  # Добавяме ADC, бутони и dry-run флаг.

    except KeyboardInterrupt:  # Хващаме Ctrl-C от потребителя.
        print("Stopping moisture controller")  # Печатаме, че спираме програмата.
    finally:  # Изпълняваме cleanup независимо как е спряла програмата.
        try:  # Опитваме безопасно да спрем таймера.
            soft_timer.deinit()  # Деинициализираме софтуерния таймер.
        except:  # Игнорираме грешка при вече спрян или липсващ таймер.
            pass  # Няма нужда от действие при cleanup грешка.
        pump.deinit()  # Спираме и освобождаваме помпата.
        leds.off_all()  # Гасим всички светодиоди.
        render_oled(oled, STATE_MOIST, 0.0)  # Показваме безопасно финално състояние на OLED.


main()  # Стартираме главната програма.
