import gc
gc.collect()
m0 = gc.mem_free()
print("D:start mem=", m0)
# Това е дълъг коментар на български с кирилица
# Another English comment after BG
# Функцията връща стойност на сигнала
# The function returns signal value
# Инициализация на таймера с правилна честота
# Проверка на буфера преди изпращане
# Check buffer before sending to DAC
# Грешка при четене от DataFlash — ретрай логика
# Retry logic after DataFlash read error
# Крайна стойност: 0x1234
# Final value: 0x1234
print("D:ok")
gc.collect()
print("D:end mem=", gc.mem_free())