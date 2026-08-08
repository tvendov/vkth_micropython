#!/usr/bin/env python3
"""Generate PNG diagrams for BOOK_BG.md Chapter 0 and system diagrams via mermaid.ink API."""
import urllib.request
import base64
import json
import os
import time

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "img")
os.makedirs(OUT_DIR, exist_ok=True)

DIAGRAMS = {
    "00_nano_pinout_top": """graph LR
    subgraph "VK-RA4M2 Nano — горен ред от USB към антена"
    A["P110"] --- B["P109"]
    B --- C["P112"]
    C --- D["P303"]
    D --- E["P304"]
    E --- F["P107"]
    F --- G["P106"]
    G --- H["P102"]
    H --- I["P104"]
    I --- J["P105"]
    J --- K["Gnd"]
    K --- L["P301"]
    L --- M["P302"]
    M --- N["P200"]
    N --- O["P207"]
    O --- P["P204"]
    P --- Q["P113"]
    Q --- R["P409"]
    R --- S["SCL"]
    S --- T["SDA"]
    end
    style A fill:#4a9eff,color:#fff
    style B fill:#4a9eff,color:#fff
    style C fill:#ff9f43,color:#fff
    style K fill:#636e72,color:#fff
    style P fill:#2ecc71,color:#fff
    style S fill:#9b59b6,color:#fff
    style T fill:#9b59b6,color:#fff""",

    "00_nano_pinout_bottom": """graph LR
    subgraph "VK-RA4M2 Nano — долен ред от USB към антена"
    A["P111"] --- B["3.3V"]
    B --- C["Aref"]
    C --- D["P014"]
    D --- E["P000"]
    E --- F["P001"]
    F --- G["P002"]
    G --- H["P101"]
    H --- I["P004"]
    I --- J["3.3V"]
    J --- K["P201"]
    K --- L["Gnd"]
    L --- M["Vin"]
    M --- N["AGnd"]
    N --- O["P013"]
    O --- P["P015"]
    P --- Q["P500"]
    Q --- R["P400"]
    R --- S["P215"]
    S --- T["P214"]
    end
    style B fill:#e74c3c,color:#fff
    style D fill:#ff9f43,color:#fff
    style J fill:#e74c3c,color:#fff
    style L fill:#636e72,color:#fff
    style P fill:#ff9f43,color:#fff
    style Q fill:#2ecc71,color:#fff
    style R fill:#9b59b6,color:#fff""",

    "00_nano_functions": """graph TB
    subgraph "VK-RA4M2 Nano — ключови функции"
    direction LR
    A["🟢 LED1<br/>P204<br/>active-low"] --- B["🔵 LED2<br/>P408"]
    B --- C["🔘 SW1<br/>P400<br/>active-low"]
    C --- D["🎵 DAC0<br/>P014"]
    D --- E["🎵 DAC1<br/>P015"]
    E --- F["🌈 WS2812<br/>P112 data<br/>P500 power"]
    F --- G["📡 I2C 1<br/>SCL SDA"]
    end
    style A fill:#2ecc71,color:#fff
    style B fill:#3498db,color:#fff
    style C fill:#636e72,color:#fff
    style D fill:#ff9f43,color:#fff
    style E fill:#ff9f43,color:#fff
    style F fill:#9b59b6,color:#fff
    style G fill:#e74c3c,color:#fff""",

    "00_usb_flow": """flowchart LR
    A["🔌 USB кабел<br/>Micro-USB"] -->|"свържи"| B["💻 Компютър<br/>Device Manager"]
    B -->|"COM3"| C["📟 REPL<br/>Thonny / mpremote"]
    C -->|">>>"| D["✅ Готово!<br/>print hello"]
    style A fill:#636e72,color:#fff
    style B fill:#4a9eff,color:#fff
    style C fill:#ff9f43,color:#fff
    style D fill:#2ecc71,color:#fff""",

    "00_upload_flow": """flowchart LR
    A["📝 .py файл<br/>на компютъра"] -->|"mpremote fs cp<br/>или Thonny Save"| B["💾 /flash<br/>на борда"]
    B -->|"import module"| C["▶️ Изпълнява се"]
    C -->|"Ctrl-C"| D["⏹ Спиране"]
    D -->|"Ctrl-D"| E["🔄 Soft Reset"]
    style A fill:#3498db,color:#fff
    style B fill:#ff9f43,color:#fff
    style C fill:#2ecc71,color:#fff
    style D fill:#e74c3c,color:#fff
    style E fill:#636e72,color:#fff""",

    "00_troubleshoot": """flowchart TB
    A["❓ Проблем"] --> B{"USB COM<br/>порт?"}
    B -->|"Не се вижда"| C["Смени кабела<br/>Пробвай друг USB порт"]
    B -->|"Вижда се"| D{"REPL<br/>отговаря?"}
    D -->|"Няма >>>"| E["Провери firmware<br/>Презареди .hex"]
    D -->|"Да"| F{"Код<br/>работи?"}
    F -->|"MemoryError"| G["gc.collect()<br/>Изтрий стари файлове"]
    F -->|"HardFault"| H["Спри DAC преди<br/>WS2812 strip.write"]
    F -->|"main.py loop"| I["RST + бърз Ctrl-C"]
    F -->|"✅ Работи"| J["🎉 Всичко е наред!"]
    style A fill:#e74c3c,color:#fff
    style J fill:#2ecc71,color:#fff
    style C fill:#ff9f43,color:#fff
    style E fill:#ff9f43,color:#fff
    style G fill:#ff9f43,color:#fff
    style H fill:#ff9f43,color:#fff
    style I fill:#ff9f43,color:#fff""",

    "00_conflicts": """graph TB
    subgraph "Конфликти между ресурси"
    A["I2C 0 SCL"] ---|"споделен P400"| B["SW1 бутон"]
    C["SPI MISO"] ---|"споделен P100"| D["I2C 1 SCL"]
    E["SPI MOSI"] ---|"споделен P101"| F["I2C 1 SDA"]
    G["WS2812 SCI2 DTC"] ---|"DTC race!"| H["DAC AGT DTC"]
    I["DAC0 изход"] ---|"споделен P014"| J["ADC вход"]
    end
    style A fill:#e74c3c,color:#fff
    style B fill:#e74c3c,color:#fff
    style G fill:#ff9f43,color:#fff
    style H fill:#ff9f43,color:#fff""",

    "00_learning_paths": """flowchart TB
    A["Глава 0<br/>Първо свързване"] --> B["Глава 1-2<br/>LED + asyncio"]
    B --> C{"Изберете маршрут"}
    C -->|"Последователно"| D["Част II-IX<br/>Пълен курс"]
    C -->|"Arduino фон"| E["Част II<br/>Езикови основи"]
    C -->|"Python фон"| F["Част III<br/>Периферии"]
    C -->|"Аудио / DSP"| G["Гл. 14 DAC<br/>→ Част X → XI"]
    C -->|"WS2812 игра"| H["Гл. 40-41<br/>→ Гл. 53"]
    D --> I["🎓 Завършен курс"]
    style A fill:#4a9eff,color:#fff
    style I fill:#2ecc71,color:#fff
    style G fill:#ff9f43,color:#fff
    style H fill:#9b59b6,color:#fff""",
}


def render_one(name, mermaid_code):
    """Download PNG from mermaid.ink API."""
    graph_json = json.dumps({"code": mermaid_code, "mermaid": {"theme": "default"}})
    b64 = base64.urlsafe_b64encode(graph_json.encode("utf-8")).decode("ascii")
    url = f"https://mermaid.ink/img/{b64}?type=png&bgColor=white"
    out_path = os.path.join(OUT_DIR, f"{name}.png")

    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = resp.read()
        with open(out_path, "wb") as f:
            f.write(data)
        print(f"  OK {name}.png  ({len(data)} bytes)")
        return True
    except Exception as e:
        print(f"  FAIL {name}: {e}")
        return False


def main():
    print(f"Generating {len(DIAGRAMS)} book diagrams into {OUT_DIR}")
    ok = 0
    for name, code in DIAGRAMS.items():
        if render_one(name, code):
            ok += 1
        time.sleep(0.5)
    print(f"\nDone: {ok}/{len(DIAGRAMS)} diagrams generated.")


if __name__ == "__main__":
    main()
