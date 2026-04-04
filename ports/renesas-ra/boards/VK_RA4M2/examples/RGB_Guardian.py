# ================== CONFIG ==================
import time                         # системни функции за време: ticks_ms, sleep_ms
import math                         # тригонометрия: math.sin, math.pi — само при инициализация
import random                       # random.randint — за случаен цвят на враговете
from machine import Pin, WS2812, Timer  # Pin: GPIO; WS2812: SCI LED driver; Timer: async notify
from retro_synth import RetroSynth  # DAC/DTC аудио модул — синтезатор на ретро звуци

# ---- USER SETTINGS ----
LED_COUNT = 61          # брой LED-ове на лентата — промени при различна дължина

PIN_WS_DATA  = "P112"  # SCI2 TX — данни за WS2812 лентата (единичен проводник)
PIN_WS_POWER = "P500"  # захранване на лентата — HIGH=вкл, LOW=изкл

PIN_BTN_RED   = "P109" # бутон Червен  — PULL_UP: натиснат=LOW, пуснат=HIGH
PIN_BTN_GREEN = "P110" # бутон Зелен   — PULL_UP
PIN_BTN_BLUE  = "P111" # бутон Син     — PULL_UP

PIN_DAC = "P014"        # DAC0 изход — аналогов аудио сигнал (алтернатива P015=DAC1)

BRIGHTNESS = 25         # 0-255: максимална яркост на LED-овете (25 ≈ 10% — пестене на ток)

# ================== HARDWARE INIT ==================
power = Pin(PIN_WS_POWER, Pin.OUT, value=1)           # P500 HIGH — захранва лентата
time.sleep_ms(100)                                    # изчакване WS2812 да се стабилизира

strip = WS2812(pixel_count=LED_COUNT, pin=Pin(PIN_WS_DATA), channels=3)  # 61 RGB пиксела
synth = RetroSynth(dac_pin=PIN_DAC)                   # инициализира DAC + DTC буфери

btn_red   = Pin(PIN_BTN_RED,   Pin.IN, Pin.PULL_UP)  # P109 вход с вътрешен pull-up резистор
btn_green = Pin(PIN_BTN_GREEN, Pin.IN, Pin.PULL_UP)  # P110 вход с вътрешен pull-up резистор
btn_blue  = Pin(PIN_BTN_BLUE,  Pin.IN, Pin.PULL_UP)  # P111 вход с вътрешен pull-up резистор

# ================== GAME CONSTANTS ==================
PLAYER_POS  = 0                          # играчът е на LED 0 (лявата страна на лентата)
MAX_BULLETS = 10                         # максимален брой куршуми едновременно в полет
ENEMY_COUNT = min(30, LED_COUNT // 2)    # врагове: max 30 или половината от LED-овете

# ================== GAME STATE ==================
STATE_STANDBY   = 0   # изчакване — LED 0 пулсира, играта не е стартирана
STATE_PLAYING   = 1   # активна игра — бутони, врагове, куршуми
STATE_GAME_OVER = 2   # край — всички LED-ове светват червено за 1 сек

state = STATE_STANDBY  # начално състояние при зареждане

# ================== GAME DATA ==================
# Pre-dimmed color constants — created once at import, never re-created
_B       = BRIGHTNESS                               # псевдоним за по-кратък запис
C_OFF    = (0, 0, 0)                               # изгасен пиксел
C_RED    = (_B, 0, 0)                              # червен с ограничена яркост
C_GREEN  = (0, _B, 0)                              # зелен с ограничена яркост
C_BLUE   = (0, 0, _B)                              # син с ограничена яркост
C_PLAYER = (_B * 80 // 255, _B * 80 // 255, _B * 80 // 255)  # бял пиксел ~30% яркост
COLORS   = [C_RED, C_GREEN, C_BLUE]               # индекс 0/1/2 → кортеж с цвят

# Pre-allocated bytearray pools — fixed size, zero runtime allocation
b_pos    = bytearray(MAX_BULLETS)  # позиция на куршум (LED индекс 0..60)
b_color  = bytearray(MAX_BULLETS)  # цвят на куршум: 0=червен 1=зелен 2=син
b_active = bytearray(MAX_BULLETS)  # 1=куршумът лети, 0=слотът е свободен

e_pos    = bytearray(ENEMY_COUNT)  # позиция на враг (255=убит/неактивен)
e_color  = bytearray(ENEMY_COUNT)  # цвят на враг: 0/1/2 — трябва да съвпадне с куршума

# Pre-computed standby sine table — no float math in game loop
_SIN_LEN   = 64                    # брой стъпки в един период на синуса
_sin_table = bytearray(            # предварително изчислена таблица — стойности 0..80
    int((1 + math.sin(2 * math.pi * i / _SIN_LEN)) * 40)
    for i in range(_SIN_LEN)
)                                  # изчислява се веднъж при import, после само четене
_sin_phase  = 0                    # текущ индекс в синусовата таблица
_standby_px = [0, 0, 0]           # RGB на standby пиксела — reuse без нова алокация

BTN_COOLDOWN = 150          # ms: bounce < 20ms << 150ms — филтрира фалшиви натискания
_btn_time    = [0, 0, 0]   # ticks_ms на последен изстрел за всеки бутон (pre-alloc)

last_enemy_move = 0         # ticks_ms на последното преместване на враговете
enemy_interval  = 400       # ms между стъпките на враговете (по-малко = по-бързо)

score = 0                   # брой убити врагове в текущата игра

# ================== ASYNC WS2812 ==================
_ws_done = bytearray(1)     # флаг: DTC трансферът е завършил (1=готов)
_ws_done[0] = 1             # при старт няма чакащ трансфер
_latch_t    = Timer(-1)     # software timer — нула AGT канали, нула IRQ вектори

def _ws_cb(t):
    _ws_done[0] = 1         # извиква се от Python scheduler след края на DTC

# ================== UTILS ==================
def push():
    # 1. Изчакай края на предишния async трансфер (обикновено мигновено след sleep_ms)
    deadline = time.ticks_add(time.ticks_ms(), 6)  # timeout 6ms
    while not _ws_done[0]:
        if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
            break                           # timeout защита
    strip.sync()             # финализира SCI TX: изключва го и пин→LOW (<10µs)

    # 2. Спри DAC/AGT преди нов SCI2 трансфер — предотвратява DTC race → HardFault
    synth.stop()

    # 3. Стартирай async DTC трансфер — връща се в <20µs, CPU е свободен!
    _ws_done[0] = 0
    strip.write_async()      # DTC предава 61px × 24bit = ~1.88ms без CPU
    _latch_t.init(mode=Timer.ONE_SHOT, period=2, callback=_ws_cb)  # notify след ~2ms

# ================== INIT ==================
def init_game():
    global score, state    # модифицира глобални променливи

    score = 0              # нулира резултата

    for i in range(MAX_BULLETS):  # деактивира всички куршумни слотове
        b_active[i] = 0

    for j in range(ENEMY_COUNT):           # разполага враговете от дясно на ляво
        e_pos[j]   = LED_COUNT - 1 - j    # позиция: 60, 59, 58... (десният край)
        e_color[j] = random.randint(0, 2) # случаен цвят 0=червен 1=зелен 2=син

    state = STATE_PLAYING  # превключва към активна игра

# ================== INPUT ==================
def handle_input():
    now = time.ticks_ms()                              # текущо време в ms
    if not btn_red.value():                            # P109 LOW = бутонът е натиснат
        if time.ticks_diff(now, _btn_time[0]) > BTN_COOLDOWN:  # изтекъл ли е cooldown?
            _btn_time[0] = now                         # записва времето на изстрела
            fire(0)                                    # стреля с червен куршум
    if not btn_green.value():                          # P110 LOW = натиснат
        if time.ticks_diff(now, _btn_time[1]) > BTN_COOLDOWN:
            _btn_time[1] = now
            fire(1)                                    # стреля с зелен куршум
    if not btn_blue.value():                           # P111 LOW = натиснат
        if time.ticks_diff(now, _btn_time[2]) > BTN_COOLDOWN:
            _btn_time[2] = now
            fire(2)                                    # стреля със син куршум

# ================== SHOOT ==================
def fire(color_id):
    for i in range(MAX_BULLETS):     # търси свободен слот в пула
        if not b_active[i]:          # намерен свободен слот
            b_pos[i]    = PLAYER_POS + 1  # стартира веднага пред играча
            b_color[i]  = color_id        # задава цвета на куршума
            b_active[i] = 1               # маркира слота като активен
            synth.play_sweep("A5", "A6", 40, "square", steps=6)  # лазерен chirp нагоре
            return                         # само един куршум на натискане

# ================== UPDATE ==================
def update_bullets():
    for i in range(MAX_BULLETS):      # обхожда всички слотове
        if b_active[i]:               # само активни куршуми
            b_pos[i] += 1             # куршумът се движи 1 LED надясно (към враговете)
            if b_pos[i] >= LED_COUNT: # излязъл от лентата
                b_active[i] = 0       # деактивира слота — готов за повторна употреба

def update_enemies(now):
    global last_enemy_move            # следи кога е бил последният ход

    if time.ticks_diff(now, last_enemy_move) > enemy_interval:  # изтекъл интервал?
        last_enemy_move = now         # записва времето на тази стъпка
        for j in range(ENEMY_COUNT):
            if e_pos[j] < 255 and e_pos[j] > 0:  # жив враг, не е стигнал играча
                e_pos[j] -= 1        # движи се 1 LED наляво (към играча)

def check_collisions():
    global score                      # модифицира глобалния резултат

    for i in range(MAX_BULLETS):      # проверява всеки активен куршум
        if not b_active[i]:
            continue                  # прескача неактивни слотове

        for j in range(ENEMY_COUNT):  # сравнява с всеки жив враг
            if e_pos[j] < 255 and e_pos[j] == b_pos[i]:  # съвпадение на позиция
                if e_color[j] == b_color[i]:    # правилен цвят — попадение!
                    e_pos[j]    = 255            # 255 = sentinel за убит враг
                    b_active[i] = 0              # куршумът е изразходван
                    score += 1                   # добавя точка
                    synth.coin()                 # звук: B5→E6 (ретро монета)
                else:                            # грешен цвят — куршумът спира
                    b_active[i] = 0              # деактивира куршума
                    synth.play_sweep("G4", "C3", 80, "saw", steps=8)  # тъжен звук надолу
                break                            # куршумът е изразходван — прекрати търсенето

# ================== GAME OVER ==================
def check_game_over():
    global state                      # променя глобалното FSM състояние

    for j in range(ENEMY_COUNT):      # проверява дали някой враг е достигнал играча
        if e_pos[j] < 255 and e_pos[j] <= PLAYER_POS:  # жив враг на pos 0
            state = STATE_GAME_OVER   # превключва FSM към game over
            synth.explosion()         # звук: шумов burst (noise LFSR)
            break                     # само един game over sound

# ================== RENDER ==================
def render():
    strip.fill(C_OFF)                  # изчиства целия буфер с native fill() — по-бързо от loop

    strip[PLAYER_POS] = C_PLAYER       # рисува играча на позиция 0 (бял)

    for j in range(ENEMY_COUNT):       # рисува живите врагове
        p = e_pos[j]
        if p < 255:                    # 255 = мъртъв, пропуска го
            strip[p] = COLORS[e_color[j]]     # цветен враг — pre-allocated tuple

    for i in range(MAX_BULLETS):       # рисува активните куршуми
        if b_active[i]:
            strip[b_pos[i]] = COLORS[b_color[i]]  # цветен куршум — pre-allocated tuple

    push()                             # изпраща буфера към LED лентата

# ================== STATES ==================
def standby():
    global _sin_phase                  # фазата е глобална, напредва всеки кадър

    b = _sin_table[_sin_phase]                    # чете яркост от таблицата (0..80)
    _sin_phase = (_sin_phase + 1) % _SIN_LEN      # напредва фазата, wrap при 64

    _standby_px[0] = b                 # R = b (бяло = R+G+B еднакви)
    _standby_px[1] = b                 # G = b
    _standby_px[2] = b                 # B = b — мутира списъка in-place, без алокация

    strip.fill(C_OFF)                  # изчиства лентата с native fill()
    strip[0] = _standby_px             # LED 0 пулсира бяло — сигнал за ready
    push()                             # изпраща кадъра

    if not btn_red.value() or not btn_green.value() or not btn_blue.value():
        init_game()                    # всеки бутон стартира играта

def playing(now):
    handle_input()          # чете бутоните и стреля при нужда
    update_bullets()        # движи куршумите с 1 LED надясно
    update_enemies(now)     # движи враговете с 1 LED наляво (на всеки enemy_interval ms)
    check_collisions()      # проверява съвпадения куршум↔враг
    check_game_over()       # проверява дали враг е достигнал позиция 0
    render()                # рисува кадъра и го изпраща към лентата

def game_over():
    strip.fill(C_RED)                  # всички LED-ове светват червено с native fill()
    push()                             # изпраща червения кадър
    time.sleep_ms(1000)                # пауза 1 сек за визуален ефект

    reset_to_standby()                 # връща се в режим на изчакване

def reset_to_standby():
    global state
    state = STATE_STANDBY              # FSM → STATE_STANDBY

# ================== MAIN LOOP =================

def GAME():
    global state                       # FSM се управлява от глобалната state

    while True:
        now = time.ticks_ms()          # един ticks_ms на кадър — без повторно извикване

        if state == STATE_STANDBY:
            standby()                  # пулсиращ LED 0, чака бутон

        elif state == STATE_PLAYING:
            playing(now)               # пълен игрови кадър

        elif state == STATE_GAME_OVER:
            game_over()                # червен екран + reset

        time.sleep_ms(10)              # ~10ms на кадър = ~100 fps (реално по-малко заради звука)

#    print(
#        "R:", btn_red.value(),        # debug: показва raw стойности на бутоните
#        "G:", btn_green.value(),
#        "B:", btn_blue.value()
#    )
#    time.sleep_ms(200)

try:
    GAME()                             # стартира главния цикъл (блокира завинаги)
except KeyboardInterrupt:
    _latch_t.deinit()                  # спира Timer(-1) callback-а
    strip.sync()                       # финализира евентуален текущ async трансфер
    strip.fill(C_OFF)                  # гаси лентата при Ctrl+C
    strip.write()                      # последен blocking write за сигурност
    strip.deinit()                     # освобождава SCI2 backend-а