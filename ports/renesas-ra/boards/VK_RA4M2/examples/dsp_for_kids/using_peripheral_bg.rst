.. _renesas-ra_using_peripheral_bg:

Използване на периферии
========================

За бърза помощна информация въведете::

    help()

Можете да достъпвате перифериите на RA MCU чрез MicroPython модули.
За да видите списък с поддържаните модули, въведете::

    help('modules')

Особено важни са модулът `machine` и класът :ref:`machine.Pin <machine.Pin>`
за работа с периферии.

Използвайки "from machine import Pin", можете да ползвате имена на пинове,
съответстващи на имената на пиновете на RA MCU — Pin.cpu.P000 и 'P000'.
Освен това можете да използвате имената 'LED1', 'LED2', 'SW1' и 'SW2',
ако платката има тези LED-ове и бутони.

Мигане на LED
-------------

Като прост пример можете да въведете следната програма за мигане на LED1.
Моля натиснете Enter 4 пъти след въвеждането на последния time.sleep(1). ::

    import time
    from machine import Pin
    led1 = Pin('LED1')
    print(led1)
    while True:
        led1.on()
        time.sleep(1)
        led1.off()
        time.sleep(1)

Ще видите LED1 да мига на всяка 1 секунда.

Ако искате да спрете програмата, натиснете CTRL-C. ::

    Traceback (most recent call last):
      File "<stdin>", line 5, in <module>
    KeyboardInterrupt:

Това съобщение се показва и програмата спира.
Съобщението означава, че програмата е била прекъсната на ред 5, оператор "while".

Чрез print(led1) можете да потвърдите, че LED1 е свързан към Pin.cpu.P106
на платката::

     Pin(Pin.cpu.P106, mode=Pin.OUT, pull=Pin.PULL_NONE, drive=Pin.LOW_POWER)

Така ще получите същия резултат, ако посочите Pin(Pin.cpu.P106)
вместо Pin('LED1'). ::

    import time
    from machine import Pin
    led1 = Pin(Pin.cpu.P106)
    print(led1)
    while True:
        led1.on()
        time.sleep(1)
        led1.off()
        time.sleep(1)
