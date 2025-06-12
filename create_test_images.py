#!/usr/bin/env python3
"""
Създава тестови PNG изображения за LVGL Image Button demo
Използва PIL (Pillow) за генериране на прости бутони
"""

try:
    from PIL import Image, ImageDraw, ImageFont
    import os
except ImportError:
    print("Моля инсталирайте Pillow: pip install Pillow")
    exit(1)

def create_button_image(filename, size=(80, 80), color=(100, 150, 200), pressed=False, text="BTN"):
    """Създава PNG изображение на бутон"""

    # Създаваме изображение с RGBA формат
    img = Image.new('RGBA', size, (0, 0, 0, 0))  # Прозрачен фон
    draw = ImageDraw.Draw(img)

    # Цветове за бутона
    if pressed:
        # По-тъмен цвят за натиснат бутон
        button_color = tuple(max(0, c - 50) for c in color)
        border_color = (50, 50, 50, 255)
    else:
        # Нормален цвят за освободен бутон
        button_color = color + (255,)  # Добавяме alpha канал
        border_color = (200, 200, 200, 255)

    # Рисуваме заоблен правоъгълник за бутона
    margin = 5
    draw.rounded_rectangle(
        [margin, margin, size[0] - margin, size[1] - margin],
        radius=10,
        fill=button_color,
        outline=border_color,
        width=2
    )

    # Добавяме градиент ефект
    for i in range(5):
        alpha = 50 - i * 10
        highlight_color = tuple(min(255, c + 30) for c in color) + (alpha,)
        draw.rounded_rectangle(
            [margin + i, margin + i, size[0] - margin - i, margin + 15 + i],
            radius=8 - i,
            fill=highlight_color
        )

    # Добавяме текст
    try:
        # Опитваме се да използваме системен шрифт
        font = ImageFont.truetype("arial.ttf", 14)
    except:
        # Ако няма системен шрифт, използваме default
        font = ImageFont.load_default()

    # Центрираме текста
    bbox = draw.textbbox((0, 0), text, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    text_x = (size[0] - text_width) // 2
    text_y = (size[1] - text_height) // 2

    # Рисуваме текста с shadow ефект
    shadow_color = (0, 0, 0, 128)
    draw.text((text_x + 1, text_y + 1), text, fill=shadow_color, font=font)

    text_color = (255, 255, 255, 255) if not pressed else (200, 200, 200, 255)
    draw.text((text_x, text_y), text, fill=text_color, font=font)

    # Запазваме изображението
    img.save(filename, 'PNG')
    print(f"Създадено: {filename} ({size[0]}x{size[1]})")

def main():
    """Създава тестовите изображения"""
    print("Създаване на тестови PNG изображения за LVGL Image Button...")

    # Създаваме различни анимирани бутони
    button_configs = [
        ('btn_blue.png', (70, 130, 180), 'BLUE'),
        ('btn_green.png', (70, 180, 70), 'GREEN'),
        ('btn_red.png', (180, 70, 70), 'RED'),
        ('btn_orange.png', (180, 130, 70), 'ORANGE'),
        ('btn_purple.png', (130, 70, 180), 'PURPLE'),
        ('btn_cyan.png', (70, 180, 180), 'CYAN')
    ]

    for filename, color, text in button_configs:
        create_button_image(filename, size=(80, 80), color=color, pressed=False, text=text)

    # Създаваме и pressed версии
    for filename, color, text in button_configs[:3]:  # Само първите 3
        pressed_name = filename.replace('.png', '_pressed.png')
        create_button_image(pressed_name, size=(80, 80), color=color, pressed=True, text=text)

    # Проверяваме размерите на файловете
    all_files = [config[0] for config in button_configs]
    all_files.extend(['btn_blue_pressed.png', 'btn_green_pressed.png', 'btn_red_pressed.png'])

    total_size = 0
    for filename in all_files:
        if os.path.exists(filename):
            size = os.path.getsize(filename)
            print(f"{filename}: {size} bytes")
            total_size += size

    print(f"\nОбщо създадени: {len(all_files)} файла, {total_size} bytes")
    print("\nГотово! Файловете са готови за копиране на VK-RA6M5 board.")
    print("Копирайте PNG файловете в root директорията на board-а за SD Card тест.")

if __name__ == "__main__":
    main()
