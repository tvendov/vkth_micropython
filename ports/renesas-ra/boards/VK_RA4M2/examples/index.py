# Скрипт: Листване на всички примери на борда VK_RA4M2.
# Стартирайте на борда с: import index

import os  # Импортираме os за работа с файловата система.


def list_examples(base_path="/flash/examples"):  # Функция за листване на примери.
    """Листва всички .py файлове в examples/ директорията на борда."""

    print("=== Примери на VK_RA4M2 ===")  # Заглавие.
    print()  # Празен ред.

    total_files = 0  # Брояч на общия брой файлове.

    try:  # Опитваме да отворим директорията.
        entries = sorted(os.listdir(base_path))  # Сортираме съдържанието.
    except OSError:  # Ако директорията не съществува:
        print("Директорията", base_path, "не е намерена.")  # Съобщение за грешка.
        print("Уверете се, че примерите са качени на борда.")  # Указание.
        return 0  # Връщаме 0 файла.

    for entry in entries:  # Обхождаме всички записи.
        full_path = base_path + "/" + entry  # Пълен път до записа.

        try:  # Проверяваме дали е директория.
            sub_entries = sorted(os.listdir(full_path))  # Опитваме да листнем.

            # Това е поддиректория — листваме .py файловете в нея.
            py_files = [f for f in sub_entries if f.endswith(".py")]  # Филтрираме .py файлове.

            if py_files:  # Ако има .py файлове в директорията:
                print("[", entry, "]")  # Печатаме името на директорията.

                for py_file in py_files:  # За всеки .py файл:
                    print("  ", py_file)  # Печатаме с отстъп.
                    total_files += 1  # Увеличаваме брояча.

                print()  # Празен ред между секциите.

        except OSError:  # Ако не е директория:
            if entry.endswith(".py") and entry != "index.py":  # Ако е .py файл (без самия index):
                print(entry)  # Печатаме го.
                total_files += 1  # Увеличаваме брояча.

    print()  # Празен ред преди обобщение.
    print("Общо примери:", total_files)  # Печатаме общия брой.

    return total_files  # Връщаме броя за програмно използване.


# --- Стартиране ---
list_examples()  # Извикваме функцията при импортиране.

