# Пример: Два бутона с fast ASM IRQ и shared ring buffer на VK_RA4M2.
# Ресурси на VK_RA4M2: external IRQ pin за "+" = P301 -> IRQ6.
# Ресурси на VK_RA4M2: external IRQ pin за "-" = P302 -> IRQ5.
# Бележка: Свържете всеки бутон между съответния пин и GND, защото тук ползваме вътрешен pull-up и trigger на falling edge.
# Бележка: Примерът ползва fast=True, така че ISR-ите са @micropython.asm_thumb и не минават през Python callback path.
# Бележка: За да може ASM ISR да пише в shared буфери, адресите им се вграждат в кода след runtime създаването им чрез exec().

from array import array  # Импортираме array, за да държим малък 32-bit shared state блок в RAM.
from machine import Pin  # Импортираме Pin, за да вържем двата бутона към external IRQ.
from uctypes import addressof  # Импортираме addressof, за да вземем адресите на ring buffer-а и state блока.
import machine  # Импортираме machine, за да ползваме disable_irq/enable_irq при четене от опашката.
import micropython  # Импортираме micropython, защото fast IRQ handler-ите са @micropython.asm_thumb.
import time  # Импортираме time, за да правим лек polling извън прекъсването.

PLUS_PIN_NAME = "P301"  # Този бутон ще увеличава стойността и е вързан към IRQ6.
MINUS_PIN_NAME = "P302"  # Този бутон ще намалява стойността и е вързан към IRQ5.
QUEUE_LEN = 16  # Дължината на ring buffer-а е 16 събития, което е достатъчно за демонстрация.
EVENT_PLUS = 1  # Кодираме натиснат "+" като 1.
EVENT_MINUS = 2  # Кодираме натиснат "-" като 2.
DEBOUNCE_MS = 30  # След отпускане изчакваме 30 ms стабилно високо ниво, преди да разрешим ново събитие.

events = bytearray(QUEUE_LEN)  # Това е shared ring buffer-ът, в който ISR-ите пишат кодовете на събитията.
event_values = array("i", [0] * QUEUE_LEN)  # Тук пазим snapshot на стойността за всяко enqueue-нато събитие.
state = array("i", [0, 0, 0, 0, 0, 0])  # state[0]=wr_idx, state[1]=rd_idx, state[2]=value, state[3]=lost_events, state[4]=plus_lock, state[5]=minus_lock.

queue_addr = addressof(events)  # Взимаме началния адрес на ring buffer-а за директен byte достъп от ASM.
values_addr = addressof(event_values)  # Взимаме адреса на буфера със snapshot стойности за директен word достъп от ASM.
state_addr = addressof(state)  # Взимаме началния адрес на state блока за директен word достъп от ASM.


def build_button_isr_source(function_name, delta, event_code):  # Генерираме текста на отделен ASM ISR за всеки бутон.
    if delta > 0:  # За бутона "+" ползваме add().
        value_update = "    add(r2, r2, 1)"
        lock_offset = 16
    else:  # За бутона "-" ползваме sub().
        value_update = "    sub(r2, r2, 1)"
        lock_offset = 20

    return f"""
@micropython.asm_thumb
def {function_name}(r0):
    movwt(r1, {state_addr})
    ldr(r2, [r1, {lock_offset}])
    cmp(r2, 0)
    bne(DONE_{function_name})
    movw(r2, 1)
    str(r2, [r1, {lock_offset}])

    ldr(r2, [r1, 8])
{value_update}
    str(r2, [r1, 8])

    ldr(r3, [r1, 0])
    ldr(r4, [r1, 4])
    add(r5, r3, 1)
    movw(r6, {QUEUE_LEN})
    cmp(r5, r6)
    blt(NO_WRAP_{function_name})
    movw(r5, 0)
    label(NO_WRAP_{function_name})

    cmp(r5, r4)
    beq(FULL_{function_name})

    movwt(r6, {queue_addr})
    add(r6, r6, r3)
    movw(r7, {event_code})
    strb(r7, [r6, 0])

    movwt(r6, {values_addr})
    mov(r7, r3)
    lsl(r7, r7, 2)
    add(r6, r6, r7)
    str(r2, [r6, 0])

    str(r5, [r1, 0])
    b(DONE_{function_name})

    label(FULL_{function_name})
    ldr(r3, [r1, 12])
    add(r3, r3, 1)
    str(r3, [r1, 12])

    label(DONE_{function_name})
"""


exec(build_button_isr_source("plus_isr", +1, EVENT_PLUS), globals(), globals())  # Създаваме ASM ISR за бутона "+" след като адресите вече са известни.
exec(build_button_isr_source("minus_isr", -1, EVENT_MINUS), globals(), globals())  # Създаваме ASM ISR за бутона "-" по същия модел.

plus_button = Pin(PLUS_PIN_NAME, Pin.IN, Pin.PULL_UP)  # Настройваме P301 като вход с pull-up резистор.
minus_button = Pin(MINUS_PIN_NAME, Pin.IN, Pin.PULL_UP)  # Настройваме P302 като вход с pull-up резистор.

plus_button.irq(trigger=Pin.IRQ_FALLING, handler=plus_isr, fast=True)  # Вързваме fast ASM IRQ за бутона "+".
minus_button.irq(trigger=Pin.IRQ_FALLING, handler=minus_isr, fast=True)  # Вързваме fast ASM IRQ за бутона "-".

print("=== Fast ASM IRQ: два бутона и shared queue ===")  # Печатаме заглавието на демонстрацията.
print("Свържете бутон '+' към P301 и GND.")  # Даваме ясна инструкция за първия бутон.
print("Свържете бутон '-' към P302 и GND.")  # Даваме ясна инструкция за втория бутон.
print("ISR-ите променят value и записват event код в ring buffer.")  # Обясняваме какво прави прекъсването.
print("Извън IRQ се печата кой бутон е натиснат.")  # Обясняваме какво прави основният цикъл.

last_lost_report = 0  # Пазим последно отчетения брой загубени събития, за да не печатаме едно и също съобщение многократно.
plus_release_ms = None  # Тук пазим момента, от който бутонът "+" е освободен стабилно високо.
minus_release_ms = None  # Тук пазим момента, от който бутонът "-" е освободен стабилно високо.

while True:  # Основният цикъл чете ring buffer-а и печата събитията извън прекъсването.
    now = time.ticks_ms()  # Взимаме текущия tick за release debounce на двата бутона.

    if state[4]:  # Ако бутонът "+" е заключен след натискане, чакаме стабилно отпускане преди да го re-arm-нем.
        if plus_button.value():
            if plus_release_ms is None:
                plus_release_ms = now
            elif time.ticks_diff(now, plus_release_ms) >= DEBOUNCE_MS:
                irq_state = machine.disable_irq()
                state[4] = 0
                machine.enable_irq(irq_state)
                plus_release_ms = None
        else:
            plus_release_ms = None

    if state[5]:  # Ако бутонът "-" е заключен след натискане, чакаме стабилно отпускане преди да го re-arm-нем.
        if minus_button.value():
            if minus_release_ms is None:
                minus_release_ms = now
            elif time.ticks_diff(now, minus_release_ms) >= DEBOUNCE_MS:
                irq_state = machine.disable_irq()
                state[5] = 0
                machine.enable_irq(irq_state)
                minus_release_ms = None
        else:
            minus_release_ms = None

    irq_state = machine.disable_irq()  # За кратко спираме IRQ, за да вземем консистентна снимка на wr/rd индексите.
    write_idx = state[0]  # Копираме текущия write индекс.
    read_idx = state[1]  # Копираме текущия read индекс.
    if read_idx != write_idx:  # Ако има поне едно чакащо събитие, го вземаме атомарно.
        event_code = events[read_idx]  # Четем event кода от ring buffer-а.
        event_value = event_values[read_idx]  # Четем точната snapshot стойност, записана от ISR за това събитие.
        state[1] = (read_idx + 1) % QUEUE_LEN  # Преместваме read индекса към следващия елемент.
    else:  # Ако няма събитие, само маркираме, че няма какво да печатаме.
        event_code = 0
        event_value = 0
    current_value = state[2]  # Пазим и текущата обща стойност, за да може при нужда да се сравни с event snapshot-а.
    lost_events = state[3]  # Вземаме броя загубени събития при пълна опашка.
    machine.enable_irq(irq_state)  # Връщаме IRQ веднага след кратката критична секция.

    if event_code == EVENT_PLUS:  # Ако сме получили код за "+", печатаме това събитие извън прекъсването.
        print("PLUS  value =", event_value)
    elif event_code == EVENT_MINUS:  # Ако сме получили код за "-", печатаме това събитие извън прекъсването.
        print("MINUS value =", event_value)

    if lost_events != last_lost_report:  # Ако сме изпуснали събития поради пълна опашка, показваме го отделно.
        print("Queue overflow, lost events =", lost_events)
        last_lost_report = lost_events

    time.sleep_ms(2)  # Късият sleep пази конзолата четима и позволява навременно re-arm-ване след отпускане.
