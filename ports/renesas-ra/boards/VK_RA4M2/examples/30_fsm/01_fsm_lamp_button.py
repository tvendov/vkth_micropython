# Пример: Минимална FSM за лампа и бутон в MicroPython на VK_RA4M2.
# Ресурси на VK_RA4M2: ADC външни входове = 13 броя -> P000, P001, P002, P003, P004, P005, P006, P007, P008, P013, P014, P015, P500.
# Ресурси на VK_RA4M2: ADC вътрешни източници = 3 броя -> ADC.CORE_TEMP, ADC.CORE_VREF, ADC.VREF.
# Ресурси на VK_RA4M2: DAC изходи = 2 броя -> P014, P015.
# Ресурси на VK_RA4M2: PWM изходи = 14 броя -> P107, P106, P105, P104, P113, P114, P112, P115, P608, P409, P408, P600, P304, P303.
# Ресурси на VK_RA4M2: UART инстанции = 4 броя -> UART(0), UART(2), UART(7), UART(9).
# Ресурси на VK_RA4M2: I2C master = 2 броя -> I2C(0)=P400/P401 и I2C(1)=P100/P101.
# Ресурси на VK_RA4M2: I2CTarget = 2 броя -> I2CTarget(0)=P400/P401 и I2CTarget(1)=P100/P101.
# Ресурси на VK_RA4M2: SPI канали = 1 брой -> SPI=P103/P102/P100/P101.
# Ресурси на VK_RA4M2: TouchPad входове = 12 броя -> P205, P206, P407, P408, P409, P410, P411, P412, P413, P414, P415, P708.
# Ресурси на VK_RA4M2: LED = 1 брой -> LED1=P204, бутон = 1 брой -> SW1=P400.
# Ресурси на VK_RA4M2: Хардуерни Timer = 6 броя -> Timer(1), Timer(2), Timer(3), Timer(4), Timer(5), Timer(6), софтуерен Timer = Timer(-1), RTC = 1 брой, Data Flash = 8 KB, /flash = около 94 KB.

STATE_OFF = "OFF"  # Това е първото състояние на лампата.
STATE_ON = "ON"  # Това е второто състояние на лампата.
EVENT_BUTTON = "BUTTON"  # Това е събитието от бутона.

state = STATE_OFF  # Започваме с изключена лампа.


def handle_event(current_state, event_name):  # Дефинираме преходната функция на машината на състояния.
    if current_state == STATE_OFF and event_name == EVENT_BUTTON:  # Ако сме в OFF и бутонът е натиснат, минаваме в ON.
        return STATE_ON  # Връщаме новото състояние ON.
    if current_state == STATE_ON and event_name == EVENT_BUTTON:  # Ако сме в ON и бутонът е натиснат, минаваме в OFF.
        return STATE_OFF  # Връщаме новото състояние OFF.
    return current_state  # Ако няма преход, оставаме в същото състояние.


print("=== Минимална FSM: лампа + бутон ===")  # Печатаме заглавие на примера.

for event_name in (EVENT_BUTTON, EVENT_BUTTON, EVENT_BUTTON):  # Симулираме три натискания на бутон.
    print("Преди събитието state =", state)  # Печатаме състоянието преди прехода.
    state = handle_event(state, event_name)  # Изчисляваме новото състояние според събитието.
    print("След събитието state =", state)  # Печатаме новото състояние след прехода.

print("Примерът за минимална FSM приключи.")  # Завършваме примера.
