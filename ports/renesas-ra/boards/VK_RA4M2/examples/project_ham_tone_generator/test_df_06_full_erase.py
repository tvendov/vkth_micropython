# Тест 06: Full erase + blank verification на всички 8 KB.
#
# Правилен подход (след RA4M2 §44.16.2 + FSP r_flash_hp module guide):
#   - FACI blankCheck е авторитативен за "erased?" въпроса.
#   - AHB readback (четене от 0x08000000+) след erase-only е UNDEFINED per spec.
#     Хардуерът може да върне 0xFF, може и да не върне — зависи от FCACHE,
#     silicon revision и дали FACI е завършил pipeline-а. НЕ разчитай на това.
#   - След explicit write(0, b"\xFF" * N) AHB readback Е стабилен 0xFF, защото
#     FACI е програмирал именно тази стойност и FCACHE е синхронизиран.

import dataflash

print("=== TEST 06: Full erase + FACI blank check ===")

size = dataflash.size()
print("Total size:", size, "bytes")

# --- Стъпка 1: Full erase ---
print()
print("Стъпка 1: dataflash.erase() (full erase)...")
try:
    dataflash.erase()
    print("  OK (no exception)")
except Exception as e:
    print("  FAILED:", e)
    raise

# --- Стъпка 2: FACI blank check (авторитативен) ---
print()
print("Стъпка 2: FACI blank check via dataflash.is_blank(0, {})...".format(size))
try:
    blank = dataflash.is_blank(0, size)
    if blank:
        print("  FACI: BLANK  -> PASS")
    else:
        print("  FACI: NOT BLANK -> FAIL (erase не сработи?)")
except Exception as e:
    print("  FAILED (FSP error):", e)
    blank = False

# --- Стъпка 3: AHB readback (само илюстративен — undefined per spec) ---
print()
print("Стъпка 3: AHB readback след erase-only (UNDEFINED per RA4M2 §44.16.2).")
print("  Стойностите по-долу са информативни — НЕ са test criterion.")
sample = dataflash.read(0, 16)
print("  Първи 16 байта (AHB, може да са всякакви):", [hex(b) for b in sample])
print("  Бележка: spec не гарантира 0xFF след erase; FACI е авторитетен.")

# --- Стъпка 4: Програмираме 0xFF и верифицираме AHB readback ---
print()
print("Стъпка 4: write(0, b'\\xFF' * 64) — програмираме 0xFF в FACI...")
chunk = b"\xff" * 64
try:
    n = dataflash.write(0, chunk)
    print("  Записани", n, "байта")
except Exception as e:
    print("  FAILED:", e)
    n = 0

if n == 64:
    rb = dataflash.read(0, 64)
    non_ff = sum(1 for b in rb if b != 0xFF)
    if non_ff == 0:
        print("  AHB readback след explicit write(0xFF): всички 64 байта = 0xFF  -> STABLE")
    else:
        print("  AHB readback: {} байта != 0xFF (неочаквано след write 0xFF)".format(non_ff))

# --- Финален вердикт ---
print()
if blank:
    print(">>> PASS: FACI потвърди blank state след erase (авторитативен).")
else:
    print(">>> FAIL: FACI отчете NOT BLANK след erase — hardware/firmware проблем.")
