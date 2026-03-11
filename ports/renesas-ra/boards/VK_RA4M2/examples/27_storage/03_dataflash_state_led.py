# Пример: запомняне на състояние в Data Flash и LED индикация на VK_RA4M2.
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
# Бележка: Този пример умишлено променя първия байт на Data Flash, за да покаже устойчиво състояние след рестарт.

import time  # Импортираме time, за да правим видимо мигане на LED1.
from machine import Pin  # Импортираме Pin за управление на вградения светодиод.
import dataflash  # Импортираме dataflash за достъп до отделната Data Flash област.

LED_ON = 0  # LED1 на VK_RA4M2 е active-low: 0 означава светнат LED.
LED_OFF = 1  # Логическа 1 изгасва LED1.
STATE_OFFSET = 0  # Използваме първия байт на Data Flash като прост persistent флаг.

led = Pin("LED1", Pin.OUT, value=LED_OFF)  # Използваме board alias-а LED1 вместо суров GPIO номер.


def _toggle_delay_ms(blinks_per_sec):  # Преобразуваме честота на мигане към half-period в милисекунди.
    return int(1000 // (2 * blinks_per_sec))  # Връщаме half-period в ms.


def _read_state():  # Четем един байт от Data Flash и връщаме числовата му стойност.
    return dataflash.read(STATE_OFFSET, 1)[0]  # Връщаме стойността на първия байт.


def _set_state_to_ff_via_erase():  # Erase на блок 0 връща байтовете в него към 0xFF.
    dataflash.erase_block(0)  # Изтриваме блок 0, всички байтове стават 0xFF.


def _set_state_to_00():  # Записваме нула в първия байт на вече изтрития блок.
    dataflash.write(STATE_OFFSET, bytes([0x00]))  # Записваме 0x00 в първия байт.


def _toggle_led():  # Превключваме LED1 между светнато и изгасено състояние.
    led.value(LED_ON if led.value() == LED_OFF else LED_OFF)  # Обръщаме текущото състояние.


def main():  # Главната функция показва как Data Flash пази състояние между стартиранията.
    state = _read_state()  # Четем запомнения байт при boot.
    print("BOOT: dataflash[0] =", hex(state))  # Печатаме какво сме прочели при boot.

    if state == 0x00:  # Ако предишният старт е записал 0x00, сега правим erase към 0xFF.
        print("Action: ERASE block 0 (-> 0xFF)")  # Информираме потребителя за действието.
        _set_state_to_ff_via_erase()  # Изтриваме блок 0.
        blinks_per_sec = 2  # Бавно мигане означава erase.
    else:  # Иначе приемаме, че байтът е 0xFF и записваме 0x00.
        print("Action: WRITE 0x00")  # Информираме потребителя за действието.
        _set_state_to_00()  # Записваме 0x00.
        blinks_per_sec = 10  # Бързо мигане означава write.

    new_state = _read_state()  # Четем отново след операцията, за да видим новото устойчиво състояние.
    print("AFTER: dataflash[0] =", hex(new_state))  # Печатаме новата стойност.
    print("LED1 ще мига с", blinks_per_sec, "мигания в секунда.")  # Казваме каква е честотата.
    print("---------------------------")  # Разделител.

    delay_ms = _toggle_delay_ms(blinks_per_sec)  # Изчисляваме half-period за мигането.

    while True:  # Мигаме безкрайно като видима индикация на текущото записано състояние.
        _toggle_led()  # Превключваме LED1.
        time.sleep_ms(delay_ms)  # Чакаме half-period.


main()  # Стартираме примера веднага при изпълнение на файла.