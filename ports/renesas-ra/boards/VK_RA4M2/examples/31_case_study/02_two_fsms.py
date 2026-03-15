# Пример: Декомпозиция на две FSM за лампа и вентилатор на VK_RA4M2.
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

from machine import Pin  # Импортираме Pin за реален хардуерен достъп.
import time  # Импортираме time за кратко закъснение.

led = Pin("LED1", Pin.OUT, value=1)  # LED1 = P204, active-low: value=1 означава изгасен.
button = Pin("SW1", Pin.IN, Pin.PULL_UP)  # SW1 = P400, active-low: value=0 означава натиснат.

LED_ON = 0  # Active-low: 0 = светва.
LED_OFF = 1  # Active-low: 1 = гаси.

lamp_state = "OFF"  # Това е текущото състояние на FSM за лампата.
fan_state = "OFF"  # Това е текущото състояние на FSM за вентилатора.

# Генерираме събитията: ако SW1 е натиснат, добавяме BUTTON към списъка.
event_sequence = []  # Започваме с празен списък.

if button.value() == 0:  # Проверяваме дали SW1 е натиснат в момента.
    event_sequence.append("BUTTON")  # Добавяме реално събитие от бутона.

event_sequence.extend(["HUMIDITY_HIGH", "TIMEOUT_LAMP", "TIMEOUT_FAN"])  # Добавяме симулирани събития.

print("=== Две FSM: лампа и вентилатор ===")  # Печатаме заглавие на примера.
print("SW1 натиснат:", button.value() == 0)  # Показваме състоянието на реалния бутон.

for event_name in event_sequence:  # Обработваме събитията последователно и за двете машини.
    if event_name == "BUTTON":  # Бутонът управлява лампата.
        lamp_state = "ON" if lamp_state == "OFF" else "OFF"  # Превключваме FSM на лампата.

    if event_name == "HUMIDITY_HIGH":  # Високата влажност влияе на FSM на вентилатора.
        fan_state = "ON"  # Включваме вентилатора.

    if event_name == "TIMEOUT_LAMP":  # Timeout за лампата връща OFF.
        lamp_state = "OFF"  # Изключваме лампата.

    if event_name == "TIMEOUT_FAN":  # Timeout за вентилатора връща OFF.
        fan_state = "OFF"  # Изключваме вентилатора.

    # Отразяваме състоянието на лампата върху реалния LED1.
    led.value(LED_ON if lamp_state == "ON" else LED_OFF)  # LED1 следва FSM на лампата.

    print("След", event_name, "-> lamp =", lamp_state, ", fan =", fan_state)  # Печатаме състоянията.
    time.sleep_ms(500)  # Кратко закъснение, за да се види промяната на LED1.

led.value(LED_OFF)  # Гасим LED1 при завършване.

print("Примерът за две FSM приключи.")  # Завършваме примера.
