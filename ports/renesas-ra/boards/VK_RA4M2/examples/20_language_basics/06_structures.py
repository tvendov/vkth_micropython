# Пример: Структури в MicroPython на VK_RA4M2.
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

from collections import namedtuple  # Импортираме namedtuple като лек и четим заместител на проста структура.

SensorRecord = namedtuple("SensorRecord", ("name", "pin", "unit"))  # Описваме поле по поле една проста структура.

light_sensor = {"name": "Light", "adc_pin": "P000", "limits": {"dark": 800, "bright": 3000}}  # dict е най-честият MicroPython заместител на структура.
touch_sensor = SensorRecord("TouchPad", "P205", "counts")  # Създаваме namedtuple инстанция с именувани полета.

devices = [  # Тук правим списък от структури, както в C бихме имали масив от struct.
    {"name": "LED1", "pin": "P204", "kind": "output"},  # Първият елемент описва LED като структура в dict вид.
    {"name": "SW1", "pin": "P400", "kind": "input"},  # Вторият елемент описва бутона като структура в dict вид.
]  # Затваряме списъка от структури.

print("=== Структури в MicroPython ===")  # Печатаме заглавие на примера.
print("Сензорът light_sensor е", light_sensor)  # Показваме цялата структура тип dict.
print("Праг за тъмно е", light_sensor["limits"]["dark"])  # Достъпваме вложена структура в структура.
print("Touch sensor pin =", touch_sensor.pin)  # Достъпваме поле от namedtuple с име вместо индекс.
print("Първото устройство е", devices[0]["name"], "върху", devices[0]["pin"])  # Вземаме елемент от списък и поле от dict.

devices.append({"name": "PWM1A", "pin": "P105", "kind": "pwm"})  # Добавяме нова структура в списъка.
print("Броят на описаните устройства е", len(devices))  # Печатаме колко структури има в списъка.

print("Примерът за структури приключи.")  # Завършваме примера.
