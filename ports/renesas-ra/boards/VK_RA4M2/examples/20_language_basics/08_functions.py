# Пример: Функции, callbacks и таблици от функции в MicroPython на VK_RA4M2.
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


def add_values(left_value, right_value):  # Дефинираме функция, която получава два входни параметъра.
    return left_value + right_value  # Връщаме резултата като изходна стойност.


def min_max(values):  # Дефинираме функция, която връща две стойности наведнъж.
    return min(values), max(values)  # Връщаме tuple с минимум и максимум.


def update_state(state_dict, new_count):  # Дефинираме функция, която променя mutable параметър тип dict.
    state_dict["count"] = new_count  # Променяме данните вътре в подадения речник.


def print_message(text):  # Това е функция, която ще използваме като callback чрез референция.
    print("Callback казва:", text)  # Печатаме текста, подаден към callback функцията.


def run_callback(callback, value):  # Дефинираме функция, която приема друга функция като параметър.
    callback(value)  # Извикваме подадената функция с подадената стойност.


def square(value):  # Това е първата функция за таблицата от функции.
    return value * value  # Връщаме квадрата на аргумента.


def cube(value):  # Това е втората функция за таблицата от функции.
    return value * value * value  # Връщаме куба на аргумента.


state = {"count": 0}  # Подготвяме mutable структура, която ще се променя от функция.
result_sum = add_values(7, 5)  # Извикваме функцията за събиране.
minimum_value, maximum_value = min_max([12, 4, 19, 7])  # Разопаковаме два резултата върнати от функция.
update_state(state, 3)  # Променяме речника през функция.
selected_callback = print_message  # Съхраняваме функция в променлива, както в C бихме държали указател към функция.
operation_table = [square, cube]  # Това е списък от функции, тоест таблица от извикваеми обекти.

print("=== Функции и callbacks ===")  # Печатаме заглавие на примера.
print("Резултатът от add_values е", result_sum)  # Показваме върнатата стойност.
print("Минимум =", minimum_value, "максимум =", maximum_value)  # Показваме multiple return values.
print("Промененото състояние е", state)  # Показваме как mutable параметърът е бил променен.
run_callback(selected_callback, "Здравей от callback")  # Извикваме функция, предадена като параметър.
print("square(4) =", operation_table[0](4))  # Вземаме първата функция от таблицата и я извикваме.
print("cube(4) =", operation_table[1](4))  # Вземаме втората функция от таблицата и я извикваме.

print("Примерът за функции приключи.")  # Завършваме примера.
