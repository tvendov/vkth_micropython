from machine import ADC, Pin, PWM, SoftI2C  # Взимаме ADC, GPIO, PWM и софтуерен I2C.
import framebuf                              # Взимаме framebuf за OLED дисплея.
import time                                  # Взимаме time за закъснения и таймери.


ADC_VREF = ADC.REF_EXTERNAL                  # ADC работи с външен reference.
ADC_VREF_VOLTS = 2.5                         # Външният reference е 2.5 V.
ADC_MAX = 4095                               # ADC е 12-bit, максимумът е 4095.
DIV_GAIN = 3.2                               # Делителят 2.2k/1k връща входното напрежение с коефициент 3.2.
TOL = 0.05                                   # Толерансът е +/-5%.

STATE_IDLE = 0                               # IDLE чака бутон за старт.
STATE_MEASURE_FIRST = 1                      # MEASURE_FIRST чака първата група 3 или 4.
STATE_GROUP_OK = 2                           # GROUP_OK дава acclaim chirp за първата група.
STATE_MEASURE_SECOND = 3                     # MEASURE_SECOND чака останалите до 7.
STATE_FINAL_OK = 4                           # FINAL_OK дава финален 100% chirp.

EV_NONE = 0                                  # Няма ново събитие.
EV_START = 1                                 # BUT1 стартира нов цикъл.
EV_PROMPT = 2                                # Време е за подканящ chirp.
EV_3_OK = 3                                  # Групата от 3 напрежения е OK.
EV_4_OK = 4                                  # Групата от 4 напрежения е OK.
EV_ALL_OK = 5                                # Всички 7 напрежения са OK.

OLED_W = 128                                 # OLED ширина.
OLED_H = 16                                  # OLED височина.
OLED_FREQ = 400000                           # OLED I2C скорост.
OLED_ADDR_0 = 0x3C                           # Първи възможен OLED адрес.
OLED_ADDR_1 = 0x3D                           # Втори възможен OLED адрес.


class SSD1306:                               # Минимален драйвер за SSD1306 128x16.
    def __init__(self, i2c, addr):           # Инициализация на OLED.
        self.i2c = i2c                       # Запомняме I2C шината.
        self.addr = addr                     # Запомняме I2C адреса.
        self.buf = bytearray(1 + OLED_W * OLED_H // 8)  # Буфер за команден байт плюс пиксели.
        self.buf[0] = 0x40                   # 0x40 значи data stream към SSD1306.
        self.fb = framebuf.FrameBuffer(memoryview(self.buf)[1:], OLED_W, OLED_H, framebuf.MONO_VLSB)  # Framebuffer.
        self.cmd_buf = bytearray(2)          # Малък буфер за една команда.
        self.cmd_buf[0] = 0x00               # 0x00 значи command stream.
        self.show_cmd = bytearray(b"\x00\x21\x00\x7F\x22\x00\x01")  # Команда за целия екран.
        self.init_panel()                    # Пускаме SSD1306 панела.
        self.clear()                         # Чистим екрана.

    def cmd(self, c):                        # Изпраща една команда.
        self.cmd_buf[1] = c                  # Слагаме командата във втория байт.
        self.i2c.writeto(self.addr, self.cmd_buf)  # Пишем към дисплея.

    def init_panel(self):                    # Последователен init, като в работещия пример.
        self.cmd(0xAE)                       # Display OFF.
        self.cmd(0xD5)                       # Clock divide команда.
        self.cmd(0x80)                       # Clock divide стойност.
        self.cmd(0xA8)                       # Multiplex команда.
        self.cmd(0x0F)                       # 16 реда.
        self.cmd(0xD3)                       # Display offset команда.
        self.cmd(0x00)                       # Offset 0.
        self.cmd(0x40)                       # Start line 0.
        self.cmd(0x8D)                       # Charge pump команда.
        self.cmd(0x14)                       # Charge pump ON.
        self.cmd(0x20)                       # Addressing mode команда.
        self.cmd(0x00)                       # Horizontal addressing.
        self.cmd(0xA1)                       # Segment remap.
        self.cmd(0xC8)                       # COM scan reverse.
        self.cmd(0xDA)                       # COM pins команда.
        self.cmd(0x02)                       # COM pins за 128x16.
        self.cmd(0x81)                       # Contrast команда.
        self.cmd(0x7F)                       # Contrast стойност.
        self.cmd(0xD9)                       # Precharge команда.
        self.cmd(0xF1)                       # Precharge стойност.
        self.cmd(0xDB)                       # VCOMH команда.
        self.cmd(0x40)                       # VCOMH стойност.
        self.cmd(0xA4)                       # Display follows RAM.
        self.cmd(0xA6)                       # Normal display.
        self.cmd(0xAF)                       # Display ON.

    def clear(self):                         # Чисти OLED.
        self.fb.fill(0)                      # Чистим framebuffer.
        self.show()                          # Пращаме празния екран.

    def show(self):                          # Показва framebuffer-а.
        self.i2c.writeto(self.addr, self.show_cmd)  # Задаваме прозореца.
        self.i2c.writeto(self.addr, self.buf)       # Пращаме пикселите.


def oled_find(i2c):                          # Търси OLED на 0x3C или 0x3D.
    try:                                     # Пробваме първия адрес.
        i2c.writeto(OLED_ADDR_0, b"")        # Празно писане само за ACK.
        return OLED_ADDR_0                   # Ако има ACK, това е адресът.
    except OSError:                          # Ако няма ACK, продължаваме.
        pass                                 # Няма действие.
    try:                                     # Пробваме втория адрес.
        i2c.writeto(OLED_ADDR_1, b"")        # Празно писане само за ACK.
        return OLED_ADDR_1                   # Ако има ACK, това е адресът.
    except OSError:                          # Ако няма ACK, няма дисплей.
        pass                                 # Няма действие.
    return None                              # OLED не е намерен.


def oled_text(oled, line1, line2):           # Пише два реда с малък 8x8 шрифт.
    if oled is None:                         # Ако няма OLED, излизаме.
        return                               # Няма действие.
    oled.fb.fill(0)                          # Чистим екрана.
    oled.fb.text(line1, 0, 0, 1)             # Пишем първи ред.
    oled.fb.text(line2, 0, 8, 1)             # Пишем втори ред.
    oled.show()                              # Показваме екрана.


def adc_volts(adc):                          # Чете ADC и връща реалното входно напрежение.
    raw = adc.read()                         # Четем 12-bit ADC.
    adc_v = raw * ADC_VREF_VOLTS / ADC_MAX   # Смятаме напрежението на ADC пина.
    input_v = adc_v * DIV_GAIN               # Смятаме напрежението преди делителя.
    return input_v                           # Връщаме входното напрежение.


def is_ok(v, target):                        # Проверява дали напрежението е в +/-5%.
    low = target * (1.0 - TOL)               # Долна граница.
    high = target * (1.0 + TOL)              # Горна граница.
    return v >= low and v <= high            # True ако е в прозореца.


def led_set(pin, value):                     # Слага LED в 0 или 1.
    pin.value(value)                         # Пишем изхода.


def buzzer_on():                             # Пуска бузера.
    return PWM(Pin("P113"), freq=2000, duty=50)  # Всички звуци са 2 kHz, PWM 50%.


def buzzer_beep(ms):                         # Един beep.
    bz = buzzer_on()                         # Пускаме PWM.
    time.sleep_ms(ms)                        # Държим звука.
    bz.deinit()                              # Спираме PWM.
    Pin("P113", Pin.OUT, value=0)            # Държим пина нисък.


def prompt_chirp():                          # Подканящ chirp.
    buzzer_beep(200)                         # 20% от 1 секунда, PWM остава 50%.


def acclaim_chirp():                         # Chirp за първа успешна група.
    buzzer_beep(120)                         # Първи кратък звук.
    time.sleep_ms(100)                       # Пауза.
    buzzer_beep(120)                         # Втори кратък звук.


def final_chirp():                           # Финален 100% chirp.
    buzzer_beep(180)                         # Първи звук.
    time.sleep_ms(100)                       # Пауза.
    buzzer_beep(180)                         # Втори звук.
    time.sleep_ms(100)                       # Пауза.
    buzzer_beep(350)                         # Дълъг финален звук.


def button_pressed(pin):                     # Проверява бутон с pull-up.
    return pin.value() == 0                  # Натиснат бутон дава 0.


def wait_button_release(pin):                # Чака бутонът да бъде пуснат.
    while button_pressed(pin):               # Докато още е натиснат.
        time.sleep_ms(20)                    # Малко изчакване.


print("=== VK_RA4M2 7 voltage tester ===")   # Заглавие в терминала.

adc_12 = ADC("P500", bits=12, vref=ADC_VREF) # ADC за 1.2 V.
adc_18 = ADC("P015", bits=12, vref=ADC_VREF) # ADC за 1.8 V.
adc_30 = ADC("P013", bits=12, vref=ADC_VREF) # ADC за 3.0 V.
adc_33 = ADC("P000", bits=12, vref=ADC_VREF) # ADC за 3.3 V.
adc_33b = ADC("P014", bits=12, vref=ADC_VREF) # ADC за второто 3.3 V.
adc_50 = ADC("P001", bits=12, vref=ADC_VREF) # ADC за 5.0 V.
adc_50b = ADC("P002", bits=12, vref=ADC_VREF) # ADC за второто 5.0 V.

led_12 = Pin("P111", Pin.OUT, value=0)       # Зелен LED за 1.2 V.
led_18 = Pin("P303", Pin.OUT, value=0)       # Зелен LED за 1.8 V.
led_30 = Pin("P106", Pin.OUT, value=0)       # Зелен LED за 3.0 V.
led_33 = Pin("P102", Pin.OUT, value=0)       # Зелен LED за 3.3 V.
led_33b = Pin("P103", Pin.OUT, value=0)      # Зелен LED за второто 3.3 V.
led_50 = Pin("P107", Pin.OUT, value=0)       # Зелен LED за 5.0 V.
led_50b = Pin("P101", Pin.OUT, value=0)      # Зелен LED за второто 5.0 V.
led_ok = Pin("P112", Pin.OUT, value=0)       # Общ зелен LED OK.
led_bad = Pin("P214", Pin.OUT, value=0)      # Червен LED за активен незавършен тест.

but1 = Pin("P104", Pin.IN, Pin.PULL_UP)      # BUT1 стартира нов цикъл.

oled = None                                  # По подразбиране няма OLED.
try:                                         # Пробваме OLED.
    i2c = SoftI2C(scl=Pin("P410", Pin.OPEN_DRAIN), sda=Pin("P411", Pin.OPEN_DRAIN), freq=OLED_FREQ)  # Софтуерен I2C.
    oled_addr = oled_find(i2c)               # Търсим адрес.
    if oled_addr is not None:                # Ако има дисплей.
        oled = SSD1306(i2c, oled_addr)       # Инициализираме OLED.
        oled_text(oled, "7V TESTER", "READY") # Показваме готовност.
        print("OLED ready", hex(oled_addr))  # Печатаме адреса.
    else:                                    # Ако няма дисплей.
        print("OLED not found")              # Пишем в терминала.
except Exception as exc:                     # Ако OLED даде грешка.
    print("OLED error", type(exc).__name__, exc) # Пишем грешката.
    oled = None                              # Продължаваме без OLED.

state = STATE_IDLE                           # Започваме в IDLE.
last_prompt_ms = time.ticks_ms()             # Таймер за подканящ chirp.

ok_12 = 0                                    # Запомнен успех за 1.2 V.
ok_18 = 0                                    # Запомнен успех за 1.8 V.
ok_30 = 0                                    # Запомнен успех за 3.0 V, група 3.
ok_33 = 0                                    # Запомнен успех за 3.3 V.
ok_33b = 0                                   # Запомнен успех за 3.3 V UL, група 3.
ok_50 = 0                                    # Запомнен успех за 5.0 V, група 3.
ok_50b = 0                                   # Запомнен успех за 5.0 V UR.
group3_done = 0                              # Група 3 още не е завършена.
group4_done = 0                              # Група 4 още не е завършена.


def reset_cycle():                           # Нулира един тестов цикъл.
    global ok_12, ok_18, ok_30, ok_33, ok_33b, ok_50, ok_50b # Ползва глобалните флагове.
    global group3_done, group4_done, last_prompt_ms # Ползва групите и chirp таймера.
    ok_12 = 0                                # Нулираме 1.2 V.
    ok_18 = 0                                # Нулираме 1.8 V.
    ok_30 = 0                                # Нулираме 3.0 V.
    ok_33 = 0                                # Нулираме 3.3 V.
    ok_33b = 0                               # Нулираме 3.3 V UL.
    ok_50 = 0                                # Нулираме 5.0 V.
    ok_50b = 0                               # Нулираме 5.0 V UR.
    group3_done = 0                          # Нулираме група 3.
    group4_done = 0                          # Нулираме група 4.
    led_set(led_12, 0)                       # Гасим LED 1.2 V.
    led_set(led_18, 0)                       # Гасим LED 1.8 V.
    led_set(led_30, 0)                       # Гасим LED 3.0 V.
    led_set(led_33, 0)                       # Гасим LED 3.3 V.
    led_set(led_33b, 0)                      # Гасим LED 3.3 V UL.
    led_set(led_50, 0)                       # Гасим LED 5.0 V.
    led_set(led_50b, 0)                      # Гасим LED 5.0 V UR.
    led_set(led_ok, 0)                       # Гасим OK LED.
    led_set(led_bad, 1)                      # Пускаме червен LED.
    last_prompt_ms = time.ticks_ms()         # Рестартираме chirp таймера.


def state_machine(event):                    # Цялата state machine приема само event.
    global state, last_prompt_ms             # Променя state и chirp таймера.
    if state == STATE_IDLE:                  # Ако чакаме старт.
        if event == EV_START:                # Ако има старт.
            reset_cycle()                    # Нулираме теста.
            oled_text(oled, "MEASURE", "WAIT 3/4") # Показваме мерене.
            state = STATE_MEASURE_FIRST      # Влизаме в първо мерене.
    elif state == STATE_MEASURE_FIRST:       # Ако чакаме първата група.
        if event == EV_PROMPT:               # Ако има prompt event.
            prompt_chirp()                   # Подканяме оператора.
            last_prompt_ms = time.ticks_ms() # Обновяваме таймера.
        elif event == EV_3_OK or event == EV_4_OK: # Ако група 3 или група 4 е готова.
            state = STATE_GROUP_OK           # Влизаме в transient GROUP_OK.
            oled_text(oled, "GROUP OK", "CHANGE POWER") # Казваме да смени захранванията.
            acclaim_chirp()                  # Даваме acclaim chirp.
            last_prompt_ms = time.ticks_ms() # Обновяваме таймера.
            state = STATE_MEASURE_SECOND     # Продължаваме към втората група.
        elif event == EV_ALL_OK:             # Ако по някаква причина всичко е OK.
            state = STATE_FINAL_OK           # Отиваме към финал.
    elif state == STATE_MEASURE_SECOND:      # Ако чакаме втората група.
        if event == EV_PROMPT:               # Ако има prompt event.
            prompt_chirp()                   # Подканяме оператора.
            last_prompt_ms = time.ticks_ms() # Обновяваме таймера.
        elif event == EV_ALL_OK:             # Ако всички 7 са OK.
            state = STATE_FINAL_OK           # Отиваме към финал.
    elif state == STATE_FINAL_OK:            # Ако сме на финал.
        led_set(led_bad, 0)                  # Гасим червен LED.
        led_set(led_ok, 1)                   # Светваме OK LED.
        oled_text(oled, "100% OK", "BRAVO")  # Показваме 100% успех.
        final_chirp()                        # Даваме финален chirp.
        state = STATE_IDLE                   # Връщаме се в IDLE.


while True:                                  # Главен цикъл.
    event = EV_NONE                          # Всеки цикъл започва без event.

    if state == STATE_IDLE:                  # В IDLE не мерим.
        v12 = adc_volts(adc_12)              # Мерим 1.2 V и в IDLE.
        v18 = adc_volts(adc_18)              # Мерим 1.8 V и в IDLE.
        v30 = adc_volts(adc_30)              # Мерим 3.0 V и в IDLE.
        v33 = adc_volts(adc_33)              # Мерим 3.3 V и в IDLE.
        v33b = adc_volts(adc_33b)            # Мерим 3.3 V UL и в IDLE.
        v50 = adc_volts(adc_50)              # Мерим 5.0 V и в IDLE.
        v50b = adc_volts(adc_50b)            # Мерим 5.0 V UR и в IDLE.
        oled_text(oled, "%3.1f%4.1f%4.1f%4.1f" % (v12, v18, v30, v33), "%3.1f%4.1f%4.1f" % (v33b, v50, v50b)) # Показваме волтметър в IDLE.
        if button_pressed(but1):             # Ако BUT1 е натиснат.
            wait_button_release(but1)        # Чакаме отпускане.
            event = EV_START                 # Генерираме старт event.

    elif state == STATE_MEASURE_FIRST or state == STATE_MEASURE_SECOND: # В двата measure state-а мерим.
        v12 = adc_volts(adc_12)              # Мерим 1.2 V.
        v18 = adc_volts(adc_18)              # Мерим 1.8 V.
        v30 = adc_volts(adc_30)              # Мерим 3.0 V, група 3.
        v33 = adc_volts(adc_33)              # Мерим 3.3 V.
        v33b = adc_volts(adc_33b)            # Мерим 3.3 V UL, група 3.
        v50 = adc_volts(adc_50)              # Мерим 5.0 V, група 3.
        v50b = adc_volts(adc_50b)            # Мерим 5.0 V UR.

        oled_text(oled, "%3.1f%4.1f%4.1f%4.1f" % (v12, v18, v30, v33), "%3.1f%4.1f%4.1f" % (v33b, v50, v50b)) # Показваме 7 напрежения.

        if ok_12 == 0 and is_ok(v12, 1.2):   # Ако 1.2 V е в норма.
            ok_12 = 1                        # Запомняме успех.
            led_set(led_12, 1)               # Светваме LED.
        if ok_18 == 0 and is_ok(v18, 1.8):   # Ако 1.8 V е в норма.
            ok_18 = 1                        # Запомняме успех.
            led_set(led_18, 1)               # Светваме LED.
        if ok_30 == 0 and is_ok(v30, 3.0):   # Ако 3.0 V е в норма.
            ok_30 = 1                        # Запомняме успех.
            led_set(led_30, 1)               # Светваме LED.
        if ok_33 == 0 and is_ok(v33, 3.3):   # Ако 3.3 V е в норма.
            ok_33 = 1                        # Запомняме успех.
            led_set(led_33, 1)               # Светваме LED.
        if ok_33b == 0 and is_ok(v33b, 3.3): # Ако 3.3 V UL е в норма.
            ok_33b = 1                       # Запомняме успех.
            led_set(led_33b, 1)              # Светваме LED.
        if ok_50 == 0 and is_ok(v50, 5.0):   # Ако 5.0 V е в норма.
            ok_50 = 1                        # Запомняме успех.
            led_set(led_50, 1)               # Светваме LED.
        if ok_50b == 0 and is_ok(v50b, 5.0): # Ако 5.0 V UR е в норма.
            ok_50b = 1                       # Запомняме успех.
            led_set(led_50b, 1)              # Светваме LED.

        if group3_done == 0 and ok_33b == 1 and ok_50 == 1 and ok_30 == 1: # Проверяваме фиксираната група 3.
            group3_done = 1                  # Запомняме група 3.
            event = EV_3_OK                  # Генерираме event 3 OK.
        if group4_done == 0 and ok_12 == 1 and ok_18 == 1 and ok_33 == 1 and ok_50b == 1: # Проверяваме група 4.
            group4_done = 1                  # Запомняме група 4.
            event = EV_4_OK                  # Генерираме event 4 OK.
        if group3_done == 1 and group4_done == 1: # Ако и двете групи са готови.
            event = EV_ALL_OK                # Генерираме финален event.
        if event == EV_NONE and time.ticks_diff(time.ticks_ms(), last_prompt_ms) >= 1000: # Ако няма друг event и е време.
            event = EV_PROMPT                # Генерираме prompt event.

        print("state=%d event=%d g3=%d g4=%d %.2f %.2f %.2f %.2f %.2f %.2f %.2f" % (state, event, group3_done, group4_done, v12, v18, v30, v33, v33b, v50, v50b)) # Печат.

    state_machine(event)                     # Викаме state machine с event.
    time.sleep_ms(50)                        # Малка пауза за стабилен цикъл.
