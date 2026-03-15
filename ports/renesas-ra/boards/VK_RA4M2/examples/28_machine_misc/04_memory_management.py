# Пример: управление на паметта в MicroPython на VK_RA4M2.
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
# Бележка: Примерът показва диагностика на heap-а и работа с буфери без да променя flash/dataflash.

import gc  # Импортираме gc за контрол и измерване на garbage-collected heap-а.
import micropython  # Импортираме micropython за mem_info и други ниско ниво runtime helper-и.

SHOW_DETAILED_MEM_INFO = False  # Оставяме подробния heap dump изключен, защото печата много текст.
TRY_BIG_ALLOC = False  # Оставяме голямата алокация изключена по подразбиране, за да не натоварваме heap-а излишно.

print("=== Управление на паметта ===")  # Печатаме заглавие на примера.

gc.collect()  # Пускаме GC в предвидим момент, за да започнем от по-чисто състояние.
print("След gc.collect(): mem_free =", gc.mem_free())  # Печатаме ориентировъчно свободния heap.
print("След gc.collect(): mem_alloc =", gc.mem_alloc())  # Печатаме ориентировъчно заетия heap.

if hasattr(gc, "threshold"):  # Проверяваме дали портът излага gc.threshold().
    print("gc.threshold() =", gc.threshold())  # Показваме текущия праг за автоматично задействане на GC.

temporary_blocks = [bytearray(128) for _ in range(8)]  # Заделяме няколко временни буфера, за да видим ефекта върху heap-а.
print("След временни буфери: mem_free =", gc.mem_free())  # Показваме, че свободната памет е намаляла.

del temporary_blocks  # Освобождаваме референцията към временните буфери.
gc.collect()  # Пускаме GC отново, за да върнем паметта в предвидим момент.
print("След освобождаване и collect: mem_free =", gc.mem_free())  # Печатаме възстановената свободна памет.

tx_buffer = bytearray(16)  # Създаваме един буфер, който ще преизползваме вместо да алокираме нов всеки път.
for index in range(len(tx_buffer)):  # Пълним буфера с демонстрационни стойности.
    tx_buffer[index] = index  # Записваме индекса като демонстрационна стойност.

window = memoryview(tx_buffer)[4:8]  # Вземаме прозорец към част от буфера без копиране.
window[0] = 200  # Променяме първия байт през memoryview.
window[3] = 201  # Променяме и последния байт от прозореца.

print("Буферът след промяна през memoryview:", list(tx_buffer))  # Показваме, че оригиналният буфер е променен на място.
print("Прозорецът е:", list(window))  # Печатаме само работния прозорец.

if hasattr(micropython, "alloc_emergency_exception_buf"):  # Проверяваме дали emergency exception buffer е наличен.
    micropython.alloc_emergency_exception_buf(100)  # Заделяме малък авариен буфер за exception-и в критичен контекст.
    print("Заделен е emergency exception buffer от 100 байта.")  # Потвърждаваме заделянето.

if hasattr(micropython, "mem_info"):  # Проверяваме дали подробната memory диагностика е налична.
    print("Следва micropython.mem_info()")  # Подготвяме потребителя за диагностичния dump.
    micropython.mem_info()  # Печатаме обобщена информация за heap-а.
    if SHOW_DETAILED_MEM_INFO:  # Само по желание печатаме и детайлната разбивка.
        print("Следва micropython.mem_info(1)")  # Маркираме по-подробния dump.
        micropython.mem_info(1)  # Печатаме по-детайлна разбивка на heap-а.

if hasattr(micropython, "heap_lock") and hasattr(micropython, "heap_unlock"):  # Проверяваме дали портът има heap lock API.
    print("Има micropython.heap_lock()/heap_unlock() за напреднали критични секции.")  # Даваме само безопасна информационна бележка.

if TRY_BIG_ALLOC:  # Само ако потребителят умишлено включи флага, пробваме голяма алокация.
    try:  # Опитваме голяма алокация в защитен блок.
        big_buffer = bytearray(80 * 1024)  # Пробваме голям буфер, който може и да не се събере според текущия heap.
        print("Голямата алокация успя, размер =", len(big_buffer))  # Печатаме реално заделения размер.
    except MemoryError:  # Ако няма достатъчно памет, хващаме грешката контролирано.
        print("MemoryError: няма достатъчно свободен heap за този buffer.")  # Печатаме честен резултат.

print("Примерът за управление на паметта приключи.")  # Завършваме примера.