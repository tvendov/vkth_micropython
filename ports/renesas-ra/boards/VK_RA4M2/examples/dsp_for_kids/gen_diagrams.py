#!/usr/bin/env python3
"""Generate PNG diagrams from Mermaid code via mermaid.ink API."""
import urllib.request
import base64
import json
import os
import sys
import time

OUT_DIR = os.path.join(os.path.dirname(__file__), "img")
os.makedirs(OUT_DIR, exist_ok=True)

DIAGRAMS = {
    "01_sampling": """graph LR
    subgraph "Висок sample rate 16 точки"
    A["• • • • • • • • • • • • • • • • → точна форма ✓"]
    end
    subgraph "Нисък sample rate 4 точки"
    B["•       •       •       • → загубени детайли ✗"]
    end""",

    "02_quantisation": """graph LR
    A["1 бит<br/>█▁█▁<br/>2 нива"] --> B["8 бита<br/>▁▃▅██▅▃▁<br/>256 нива"]
    B --> C["12 бита<br/>🎯 VK_RA4M2 DAC<br/>4096 нива"]
    C --> D["16 бита<br/>CD качество<br/>65536 нива"]
    style C fill:#2ecc71,color:#fff""",

    "03_aliasing": """graph TB
    subgraph "✓ Достатъчен sample rate"
    A["Оригинал 440 Hz"] -->|"32 точки/период"| B["Вярна форма ✓"]
    end
    subgraph "✗ Твърде нисък sample rate"
    C["Оригинал 440 Hz"] -->|"3 точки/период"| D["ГРЕШНА честота ✗ ALIASING"]
    end
    style B fill:#2ecc71,color:#fff
    style D fill:#e74c3c,color:#fff""",

    "04_dac_pipeline_simple": """graph LR
    A["🔢 Число<br/>0..4095"] -->|"DAC"| B["⚡ Напрежение<br/>0..3.3V"]
    B --> C["🔊 Звук"]
    style A fill:#a29bfe,color:#fff
    style B fill:#ff9f43,color:#fff
    style C fill:#2ecc71,color:#fff""",

    "05_dc_ac_range": """graph LR
    A["0<br/>0V<br/>минимум"] --- B["2048<br/>1.65V<br/>🔇 тишина"]
    B --- C["4095<br/>3.3V<br/>максимум"]
    style B fill:#2ecc71,color:#fff""",

    "06_waveforms": """graph LR
    subgraph "Вълнови форми"
    A["🎵 Синусоида<br/>Мек, чист тон<br/>1 хармоник"]
    B["🎮 Квадратна<br/>Остър, 8-битов<br/>Нечетни хармоници"]
    C["🎶 Триъгълна<br/>Мек, флейта<br/>Нечетни, слаби"]
    D["🎸 Трионовидна<br/>Ярък, стъргащ<br/>Всички хармоници"]
    end
    style A fill:#3498db,color:#fff
    style B fill:#e74c3c,color:#fff
    style C fill:#2ecc71,color:#fff
    style D fill:#f39c12,color:#fff""",

    "07_wavetable": """graph TB
    subgraph "❌ Директен синтез БАВЕН"
    A1["За ВСЯКА точка:<br/>value = sin(...)"] --> A2["CPU работи<br/>непрекъснато"]
    end
    subgraph "✅ Wavetable синтез БЪРЗО"
    B1["ВЕДНЪЖ:<br/>Пресметни 256<br/>точки"] --> B2["Запиши в<br/>таблица"]
    B2 --> B3["DTC чете<br/>автоматично"]
    B4["CPU спи! 😴"]
    end
    style A2 fill:#e74c3c,color:#fff
    style B3 fill:#2ecc71,color:#fff
    style B4 fill:#2ecc71,color:#fff""",

    "08_clipping": """graph TB
    subgraph "Клипване"
    A["Оригинален<br/>връх = 5000"] -->|"clamp"| B["DAC получава<br/>само 4095"]
    B --> C["🔊 Изкривен звук!<br/>Сплескан на върха"]
    end
    style A fill:#e74c3c,color:#fff
    style C fill:#e74c3c,color:#fff""",

    "09_clicks": """graph LR
    A["Буфер A<br/>свършва на 4000"] -->|"СКОК!"| B["Буфер B<br/>започва на 500"]
    B --> C["🔊 ЩРАК!<br/>click/pop"]
    style C fill:#e74c3c,color:#fff""",

    "10_fourier": """graph LR
    subgraph "Адитивен синтез"
    A["🎵 f<br/>основен тон"] -->|"+"| D["Резултат:<br/>квадратна вълна 🎮"]
    B["🎵 f/3<br/>3-ти хармоник"] -->|"+"| D
    C["🎵 f/5<br/>5-ти хармоник"] -->|"+"| D
    end
    style D fill:#e74c3c,color:#fff""",

    "11_additive_mix": """graph LR
    A["🎵 Сигнал A<br/>ниска нота<br/>AMP=700"] -->|"+"| C["🎶 A+B = Акорд<br/>Макс: 1400<br/>по-малко от 2048 ✓"]
    B["🎵 Сигнал B<br/>висока нота<br/>AMP=700"] -->|"+"| C
    style C fill:#2ecc71,color:#fff""",
}

DIAGRAMS2 = {
    "12_fir_filter": """graph LR
    A["📥 Входен сигнал<br/>ниска честота<br/>+ шум"] --> B["🔧 FIR Филтър<br/>5-tap moving average"]
    B --> C["📤 Изход<br/>само ниска честота<br/>шумът премахнат ✓"]
    style A fill:#e74c3c,color:#fff
    style B fill:#f39c12,color:#fff
    style C fill:#2ecc71,color:#fff""",

    "13_fir_vs_iir": """graph LR
    subgraph "FIR без памет"
    A1["вход n"] --> B1["× коеф"]
    A2["вход n-1"] --> B2["× коеф"]
    B1 --> C1["Σ → изход"]
    B2 --> C1
    end
    subgraph "IIR с памет 🧠"
    D1["вход n"] --> E1["× коеф"]
    D2["изход n-1"] --> E2["× коеф"]
    E1 --> F1["Σ → изход"]
    E2 --> F1
    end""",

    "14_fft_pipeline": """graph LR
    A["🎵 Вход"] --> B["🪟 Window<br/>Hamming"]
    B --> C["🔄 FFT"]
    C --> D["📊 Magnitude"]
    D --> E["📈 Bands"]
    style A fill:#3498db,color:#fff
    style B fill:#9b59b6,color:#fff
    style C fill:#e74c3c,color:#fff
    style D fill:#f39c12,color:#fff
    style E fill:#2ecc71,color:#fff""",

    "15_hamming_window": """graph LR
    subgraph "Без прозорец"
    A["Рязък край ✂️"] --> B["Фалшиви честоти ✗"]
    end
    subgraph "С Hamming прозорец"
    C["Плавен край 🔔"] --> D["Чист спектър ✓"]
    end
    style B fill:#e74c3c,color:#fff
    style D fill:#2ecc71,color:#fff""",
}


DIAGRAMS3 = {
    "16_hw_pipeline": """flowchart LR
    A["🎵 AGT Timer<br/>22050 Hz"] -->|"тик!"| B["📋 DTC / DMAC"]
    B -->|"число<br/>0..4095"| C["⚡ DAC"]
    C -->|"0..3.3V"| D["🔊 P014"]
    E["💾 RAM буфер"] --> B
    F["🧠 CPU<br/>СВОБОДНО!"] -.->|"запълва"| E
    style A fill:#4a9eff,color:#fff
    style B fill:#ff9f43,color:#fff
    style C fill:#ee5a24,color:#fff
    style D fill:#2ecc71,color:#fff
    style E fill:#a29bfe,color:#fff
    style F fill:#636e72,color:#fff""",

    "17_double_buffer": """sequenceDiagram
    participant DMAC as 📋 DMAC
    participant DAC as ⚡ DAC
    participant CPU as 🧠 CPU
    rect rgb(200, 230, 255)
    Note over DMAC,DAC: DMAC чете буфер A
    DMAC->>DAC: A[0], A[1], A[2]...
    CPU-->>CPU: Запълва буфер B
    end
    rect rgb(255, 230, 200)
    Note over DMAC,DAC: DMAC чете буфер B
    DMAC->>DAC: B[0], B[1], B[2]...
    CPU-->>CPU: Запълва буфер A
    end
    Note over DMAC,CPU: Повтаря се без прекъсване!""",

    "18_learning_path": """flowchart TB
    A["Урок 0-1<br/>DAC основи"] --> B["Урок 2-3<br/>Вълни и генериране"]
    B --> C["Урок 4<br/>Clipping / Aliasing"]
    C --> D["Урок 5<br/>RMS измерване"]
    D --> E["Урок 6<br/>Смесване"]
    E --> F["Урок 7-8<br/>FIR / IIR филтри"]
    F --> G["Урок 9<br/>FFT спектър"]
    G --> H["Урок 10<br/>Real-time audiomixer"]
    style A fill:#3498db,color:#fff
    style D fill:#f39c12,color:#fff
    style F fill:#9b59b6,color:#fff
    style G fill:#e74c3c,color:#fff
    style H fill:#2ecc71,color:#fff""",
}

ALL = {}
ALL.update(DIAGRAMS)
ALL.update(DIAGRAMS2)
ALL.update(DIAGRAMS3)


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
    print(f"Generating {len(ALL)} diagrams into {OUT_DIR}")
    ok = 0
    for name, code in ALL.items():
        if render_one(name, code):
            ok += 1
        time.sleep(0.5)
    print(f"\nDone: {ok}/{len(ALL)} diagrams generated.")


if __name__ == "__main__":
    main()
