# Пример: Управление на осветление и вентилация за баня в MicroPython на VK_RA4M2.
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
# Ресурси на VK_RA4M2: Хардуерни Timer = 2 броя -> Timer(1), Timer(2), софтуерен Timer = Timer(-1), RTC = 1 брой, Data Flash = 8 KB, /flash = около 94 KB.
# Бележка: Симулация с примерни сензори, но с реален LED1 (P204) и SW1 (P400) за изход и вход.

from machine import Pin  # Импортираме Pin за реален хардуерен достъп.
import time  # Импортираме time за кратко закъснение.

led = Pin("LED1", Pin.OUT, value=1)  # LED1 = P204, active-low: value=1 означава изгасен.
button = Pin("SW1", Pin.IN, Pin.PULL_UP)  # SW1 = P400, active-low: value=0 означава натиснат.

LED_ON = 0  # Active-low: 0 = светва.
LED_OFF = 1  # Active-low: 1 = гаси.

sensors = {"pir": True, "door_open": False, "light_level": 300, "humidity": 82}  # Примерни входни данни от четири сензора.
outputs = {"lamp": False, "fan": False}  # Два управлявани изхода за лампа и вентилатор.

# Проверяваме дали бутонът е натиснат като допълнителен вход.
button_pressed = (button.value() == 0)  # SW1 е active-low: 0 означава натиснат.

if sensors["pir"] and sensors["light_level"] < 800:  # Ако има движение и е тъмно, включваме лампата.
    outputs["lamp"] = True  # Включваме лампата.

if button_pressed:  # Ако бутонът е натиснат, също включваме лампата.
    outputs["lamp"] = True  # Ръчно включване от потребителя.

if sensors["humidity"] > 75 or sensors["door_open"]:  # Ако влажността е висока или вратата е отворена, включваме вентилатора.
    outputs["fan"] = True  # Включваме вентилатора.

# Отразяваме решението върху реалния LED1.
if outputs["lamp"]:  # Ако лампата трябва да е включена:
    led.value(LED_ON)  # Светваме LED1.
else:  # Ако лампата трябва да е изключена:
    led.value(LED_OFF)  # Гасим LED1.

print("=== Реален пример: баня ===")  # Печатаме заглавие на примера.
print("SW1 натиснат:", button_pressed)  # Показваме състоянието на реалния бутон.
print("Сензори:", sensors)  # Печатаме входните данни.
print("Изходи:", outputs)  # Печатаме изчисленото управление.
print("LED1:", "ON" if outputs["lamp"] else "OFF")  # Показваме реалното състояние на LED1.

time.sleep(2)  # Изчакваме 2 секунди, за да се види LED1.
led.value(LED_OFF)  # Гасим LED1 при завършване.

print("Примерът за баня приключи.")  # Завършваме примера.
