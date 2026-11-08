from machine import RTC  # Вграденият часовник на RA4M2.
import time  # Изчакване между прочитанията.


def check_clock(rtc):  # Проверява дали часовникът действително брои.
    before = rtc.datetime()  # Прочитаме началния час.
    time.sleep_ms(1100)  # Изчакваме повече от една секунда.
    after = rtc.datetime()  # Прочитаме часа отново.
    if before == after:  # Часът трябва да се е променил.
        raise RuntimeError("RTC is not counting; check 32.768 kHz SOSC")  # Спираме при неработещ RTC.


def init_clock(year, month, day, weekday, hour, minute, second):  # Сверяване само по изрична команда.
    rtc = RTC(source="sosc")  # Избираме външния кварц 32.768 kHz.
    check_clock(rtc)  # Проверяваме тактовия сигнал преди запис.
    rtc.datetime((year, month, day, weekday, hour, minute, second, 0))  # Записваме дата и час; понеделник е 1.
    return rtc.datetime()  # Връщаме записания час за проверка.


def show_clock(seconds=0):  # Нула означава непрекъснат печат до Ctrl+C.
    rtc = RTC()  # Използваме запазения RTC, без повторно сверяване.
    check_clock(rtc)  # Не показваме спрял часовник като работещ.
    count = 0  # Брой отпечатани редове.
    while seconds == 0 or count < seconds:  # Работим непрекъснато или зададения брой секунди.
        dt = rtc.datetime()  # Четем хардуерния часовник.
        print("%04d-%02d-%02d %02d:%02d:%02d" % (dt[0], dt[1], dt[2], dt[4], dt[5], dt[6]))  # Печатаме дата и час.
        count = count + 1  # Отчитаме отпечатания ред.
        time.sleep(1)  # Изчакваме една секунда.


if __name__ == "__main__":  # При пряко стартиране само четем RTC.
    show_clock()  # Никога не сверяваме автоматично след рестарт.
