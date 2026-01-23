/*
 * Production-safe CTSU driver (FSP 4.4)
 * Производствено-безопасен CTSU драйвер (FSP версия 4.4)
 * Target: RA4W1 / RA4M2
 * Целеви микроконтролери: RA4W1 / RA4M2
 * Use case: MicroPython binding
 * Предназначение: Свързване с MicroPython
 *
 * - Single init - Еднократна инициализация
 * - Fixed channels - Фиксирани канали (не се променят динамично)
 * - Self-capacitive multi-scan - Само-капацитивен мулти-скан режим
 * - TS00 hardware workaround - Заобикаляне на хардуерен проблем с TS00
 * - No dynamic state machine - Без динамична машина на състоянията
 */

// Включваме FSP CTSU драйвер - имплементация на CTSU хардуера
#include "r_ctsu.h"
// Включваме FSP CTSU API - интерфейс за CTSU функции
#include "r_ctsu_api.h"
// Включваме FSP IOPORT драйвер - за конфигуриране на пинове
#include "r_ioport.h"
// Включваме BSP API - базови системни функции (забавяне, грешки и др.)
#include "bsp_api.h"
// Включваме vector_data - номера на прекъсванията за CTSU
#include "vector_data.h"
// Public wrapper API
#include "ra_ctsu.h"
// Включваме стандартни C библиотеки
#include <string.h>      // За memset() - запълване на памет с нули
#include <stdbool.h>     // За bool, true, false типове

// Дефиниция за невалиден пин (не съществува в FSP, използваме за маркиране на неизползвани канали)
// Стойност 0xFFFF означава "няма пин" в таблицата за съответствие TS канал -> GPIO пин
#define BSP_IO_PORT_PIN_INVALID  (0xFFFF)

/* ============================================================
 * USER CONFIGURATION - ПОТРЕБИТЕЛСКА КОНФИГУРАЦИЯ
 * ============================================================ */

/* CTSU1 drive frequency target:
 * For RA4M2 we want ~0.5 MHz drive for all TS.
 * Using the CTSU1 drive clock path (per CTSU1 block diagram):
 *   f_drive = PCLKB / (2^CTSU_CFG_PCLK_DIVISION) / (2 * (sdpa + 1))
 * With PCLKB=25 MHz and CTSU_CFG_PCLK_DIVISION=0:
 *   sdpa=24 -> divisor=2*(24+1)=50 -> f_drive=25 MHz/50=0.5 MHz.
 */
#if defined(BSP_MCU_GROUP_RA4M2)
#define RA_CTSU_SDPA_DEFAULT  (24U)
#else
#define RA_CTSU_SDPA_DEFAULT  (0x1FU)
#endif

/* Active TS channels (MUST be strictly ascending)
 *
 * IMPORTANT:
 * - Only list TS channels that actually exist on the selected MCU AND are mapped
 *   to a valid GPIO pin in ts_to_pin[].
 * - If you enable a TS channel in CHAC but the pin is invalid/not routed, CTSU
 *   can generate ICOMP/OVERFLOW events and offset tuning may never complete.
 */
#if defined(BSP_MCU_GROUP_RA4M2)
/* VK_RA4M2: keep minimal and only enable the channel we currently map.
 * TS01 -> P205 (see ts_to_pin[] below).
 */
static const uint8_t g_ts_channels[] = { 1 ,2 };
#elif defined(BSP_MCU_GROUP_RA4W1)
/* EK_RA4W1: choose up to CTSU_CFG_NUM_SELF_ELEMENTS valid channels.
 * TS00 -> P204, TS01 -> P206, TS03 -> P407, TS10 -> P109.
 */
static const uint8_t g_ts_channels[] = { 0, 1, 3, 10 };
#else
/* This file currently provides pin mappings only for RA4W1 and RA4M2.
 * Keep behavior consistent with the ts_to_pin[] mapping section below.
 */
#error "Unsupported MCU - Неподдържан микроконтролер"
#endif

// Изчисляваме броя на активните канали (размер на масива / размер на един елемент)
#define CTSU_NUM_CHANNELS (sizeof(g_ts_channels) / sizeof(g_ts_channels[0]))

/* ============================================================
 * PIN MAPPING - СЪОТВЕТСТВИЕ МЕЖДУ TS КАНАЛИ И GPIO ПИНОВЕ
 * ============================================================ */

// Проверяваме дали компилираме за RA4W1 микроконтролер
#if defined(BSP_MCU_GROUP_RA4W1)

// TSCAP пин - референтен капацитивен пин (задължителен за CTSU)
// P205 се използва като TSCAP за RA4W1
#define TSCAP_PIN BSP_IO_PORT_02_PIN_05  /* P205 */

// Таблица за съответствие: TS канал номер -> GPIO пин
// Индексът е TS номер (0-35), стойността е GPIO пин (или BSP_IO_PORT_PIN_INVALID ако няма)
// RA4W1 поддържа 11 CTSU канала: TS00, TS01, TS03, TS10, TS12, TS13, TS18, TS28, TS30, TS31, TS34
// Източник: RA4W1 Group Datasheet  Pin list (QFN56, CTSU column).
// Кръстосано проверено с: ports/renesas-ra/boards/EK_RA4W1/ra_gen/pin_data.c (за наличните на борда TS/TSCAP пинове).
static const bsp_io_port_pin_t ts_to_pin[36] = {
    [0]  = BSP_IO_PORT_02_PIN_04,  /* TS00 -> P204 */
    [1]  = BSP_IO_PORT_02_PIN_06,  /* TS01 -> P206 */
    [2]  = BSP_IO_PORT_PIN_INVALID, /* TS02 не съществува на RA4W1 */
    [3]  = BSP_IO_PORT_04_PIN_07,  /* TS03 -> P407 */
    [4]  = BSP_IO_PORT_PIN_INVALID, /* TS04 не съществува на RA4W1 */
    [5]  = BSP_IO_PORT_PIN_INVALID, /* TS05 не съществува на RA4W1 */
    [6]  = BSP_IO_PORT_PIN_INVALID, /* TS06 не съществува на RA4W1 */
    [7]  = BSP_IO_PORT_PIN_INVALID, /* TS07 не съществува на RA4W1 */
    [8]  = BSP_IO_PORT_PIN_INVALID, /* TS08 не съществува на RA4W1 */
    [9]  = BSP_IO_PORT_PIN_INVALID, /* TS09 не съществува на RA4W1 */

    [10] = BSP_IO_PORT_01_PIN_09,  /* TS10 -> P109 */
    [11] = BSP_IO_PORT_PIN_INVALID, /* TS11 не съществува на RA4W1 */
    [12] = BSP_IO_PORT_01_PIN_11,  /* TS12 -> P111 */
    [13] = BSP_IO_PORT_01_PIN_04,  /* TS13 -> P104 */
    [14] = BSP_IO_PORT_PIN_INVALID, /* TS14 не съществува на RA4W1 */
    [15] = BSP_IO_PORT_PIN_INVALID, /* TS15 не съществува на RA4W1 */
    [16] = BSP_IO_PORT_PIN_INVALID, /* TS16 не съществува на RA4W1 */
    [17] = BSP_IO_PORT_PIN_INVALID, /* TS17 не съществува на RA4W1 */
    [18] = BSP_IO_PORT_04_PIN_02,  /* TS18 -> P402 */
    [19] = BSP_IO_PORT_PIN_INVALID, /* TS19 не съществува на RA4W1 */
    [20] = BSP_IO_PORT_PIN_INVALID, /* TS20 не съществува на RA4W1 */
    [21] = BSP_IO_PORT_PIN_INVALID, /* TS21 не съществува на RA4W1 */
    [22] = BSP_IO_PORT_PIN_INVALID, /* TS22 не съществува на RA4W1 */
    [23] = BSP_IO_PORT_PIN_INVALID, /* TS23 не съществува на RA4W1 */
    [24] = BSP_IO_PORT_PIN_INVALID, /* TS24 не съществува на RA4W1 */
    [25] = BSP_IO_PORT_PIN_INVALID, /* TS25 не съществува на RA4W1 */
    [26] = BSP_IO_PORT_PIN_INVALID, /* TS26 не съществува на RA4W1 */
    [27] = BSP_IO_PORT_PIN_INVALID, /* TS27 не съществува на RA4W1 */
    [28] = BSP_IO_PORT_00_PIN_15,  /* TS28 -> P015 */
    [29] = BSP_IO_PORT_PIN_INVALID, /* TS29 не съществува на RA4W1 */
    [30] = BSP_IO_PORT_00_PIN_10,  /* TS30 -> P010 */
    [31] = BSP_IO_PORT_00_PIN_11,  /* TS31 -> P011 */
    [32] = BSP_IO_PORT_PIN_INVALID, /* TS32 не съществува на RA4W1 */
    [33] = BSP_IO_PORT_PIN_INVALID, /* TS33 не съществува на RA4W1 */
    [34] = BSP_IO_PORT_01_PIN_05,  /* TS34 -> P105 */
    [35] = BSP_IO_PORT_PIN_INVALID, /* TS35 не съществува на RA4W1 */
};

// Ако компилираме за RA4M2 микроконтролер
#elif defined(BSP_MCU_GROUP_RA4M2)

// TSCAP пин за RA4M2 - P207
#define TSCAP_PIN BSP_IO_PORT_02_PIN_07  /* P207 */

// Таблица за съответствие: TS канал номер -> GPIO пин за RA4M2
// RA4M2 поддържа 12 CTSU канала: TS01-TS12
// Източник: RA4M2 Group Datasheet Rev.1.40, Table 1.17 Pin list
static const bsp_io_port_pin_t ts_to_pin[36] = {
    [0]  = BSP_IO_PORT_PIN_INVALID, /* TS00 не съществува на RA4M2 */
    [1]  = BSP_IO_PORT_02_PIN_05,  /* TS01 -> P205 */
    [2]  = BSP_IO_PORT_02_PIN_06,  /* TS02 -> P206 */
    [3]  = BSP_IO_PORT_PIN_INVALID,  /* TS03 -> P407 */
    [4]  = BSP_IO_PORT_PIN_INVALID,  /* TS04 -> P408 */
    [5]  = BSP_IO_PORT_PIN_INVALID,  /* TS05 -> P409 */
    [6]  = BSP_IO_PORT_PIN_INVALID,  /* TS06 -> P410 */
    [7]  = BSP_IO_PORT_PIN_INVALID,  /* TS07 -> P411 */
    [8]  = BSP_IO_PORT_PIN_INVALID,  /* TS08 -> P412 */
    [9]  = BSP_IO_PORT_PIN_INVALID,  /* TS09 -> P413 */
    [10] = BSP_IO_PORT_PIN_INVALID,  /* TS10 -> P414 */
    [11] = BSP_IO_PORT_PIN_INVALID,  /* TS11 -> P415 */
    [12] = BSP_IO_PORT_PIN_INVALID,  /* TS12 -> P708 */
    [13] = BSP_IO_PORT_PIN_INVALID, /* TS13 не съществува на RA4M2 */
    [14] = BSP_IO_PORT_PIN_INVALID, /* TS14 не съществува на RA4M2 */
    [15] = BSP_IO_PORT_PIN_INVALID, /* TS15 не съществува на RA4M2 */
    [16] = BSP_IO_PORT_PIN_INVALID, /* TS16 не съществува на RA4M2 */
    [17] = BSP_IO_PORT_PIN_INVALID, /* TS17 не съществува на RA4M2 */
    [18] = BSP_IO_PORT_PIN_INVALID, /* TS18 не съществува на RA4M2 */
    [19] = BSP_IO_PORT_PIN_INVALID, /* TS19 не съществува на RA4M2 */
    [20] = BSP_IO_PORT_PIN_INVALID, /* TS20 не съществува на RA4M2 */
    [21] = BSP_IO_PORT_PIN_INVALID, /* TS21 не съществува на RA4M2 */
    [22] = BSP_IO_PORT_PIN_INVALID, /* TS22 не съществува на RA4M2 */
    [23] = BSP_IO_PORT_PIN_INVALID, /* TS23 не съществува на RA4M2 */
    [24] = BSP_IO_PORT_PIN_INVALID, /* TS24 не съществува на RA4M2 */
    [25] = BSP_IO_PORT_PIN_INVALID, /* TS25 не съществува на RA4M2 */
    [26] = BSP_IO_PORT_PIN_INVALID, /* TS26 не съществува на RA4M2 */
    [27] = BSP_IO_PORT_PIN_INVALID, /* TS27 не съществува на RA4M2 */
    [28] = BSP_IO_PORT_PIN_INVALID, /* TS28 не съществува на RA4M2 */
    [29] = BSP_IO_PORT_PIN_INVALID, /* TS29 не съществува на RA4M2 */
    [30] = BSP_IO_PORT_PIN_INVALID, /* TS30 не съществува на RA4M2 */
    [31] = BSP_IO_PORT_PIN_INVALID, /* TS31 не съществува на RA4M2 */
    [32] = BSP_IO_PORT_PIN_INVALID, /* TS32 не съществува на RA4M2 */
    [33] = BSP_IO_PORT_PIN_INVALID, /* TS33 не съществува на RA4M2 */
    [34] = BSP_IO_PORT_PIN_INVALID, /* TS34 не съществува на RA4M2 */
    [35] = BSP_IO_PORT_PIN_INVALID, /* TS35 не съществува на RA4M2 */
};

// Ако не е нито RA4W1, нито RA4M2 - грешка при компилация
#else
#error "Unsupported MCU - Неподдържан микроконтролер"
#endif

// Външни променливи от FSP - контрол на IO портовете
extern ioport_instance_ctrl_t g_ioport_ctrl;  // Контролна структура за IOPORT
extern const ioport_cfg_t g_bsp_pin_cfg;      // Конфигурация на пиновете

/* ============================================================
 * INTERNAL STATE - ВЪТРЕШНО СЪСТОЯНИЕ НА ДРАЙВЕРА
 * ============================================================ */

// Контролна структура на CTSU (управлява хардуера)
static ctsu_instance_ctrl_t g_ctsu_ctrl;
// Конфигурация на CTSU (параметри за работа)
static ctsu_cfg_t          g_ctsu_cfg;
// Масив с конфигурация на всеки активен канал (ssdiv, so, snum, sdpa)
static ctsu_element_cfg_t  g_elements[CTSU_NUM_CHANNELS];

// Флаг: сканирането е завършено (променя се от callback функцията)
static volatile bool        g_scan_done;
// Последно събитие от CTSU (SCAN_COMPLETE, OVERFLOW и др.)
static volatile ctsu_event_t g_last_event;

// Последна FSP грешка (fsp_err_t) наблюдавана при последния опит за четене
// Използва се за диагностика (напр. защо R_CTSU_DataGet() връща != FSP_SUCCESS)
static volatile fsp_err_t g_last_fsp_err = FSP_SUCCESS;

// Флаг: драйверът е инициализиран и готов за работа
static bool g_ready = false;

// Контекст за CTSU callback (подава се през ctsu_cfg_t.p_context)
// Цел: да НЕ разчитаме на глобални променливи вътре в callback-а, ако има подаден контекст.
typedef struct st_ctsu_cb_context
{
    volatile bool        * p_scan_done;
    volatile ctsu_event_t * p_last_event;
} ctsu_cb_context_t;

static ctsu_cb_context_t g_ctsu_cb_context =
{
    .p_scan_done  = &g_scan_done,
    .p_last_event = &g_last_event,
};

static void ctsu_callback(ctsu_callback_args_t *p_args);

/* ============================================================
 * CALLBACK - ФУНКЦИЯ ЗА ОБРАТНО ИЗВИКВАНЕ
 * ============================================================ */

// Тази функция се извиква от CTSU хардуера при завършване на сканиране
// p_args съдържа информация за събитието (успех, грешка и др.)
static void ctsu_callback(ctsu_callback_args_t *p_args)
{
    // Проверяваме дали указателят е валиден
    if (!p_args) {
        return;  // Ако е NULL, излизаме
    }

    // Ако е подаден context, пишем в него. Иначе ползваме глобалните променливи (backward compatible).
    ctsu_cb_context_t const * p_ctx = (ctsu_cb_context_t const *) p_args->p_context;
    if (p_ctx && p_ctx->p_last_event && p_ctx->p_scan_done) {
        *(p_ctx->p_last_event) = p_args->event;
        *(p_ctx->p_scan_done)  = true;
        return;
    }

    // Запазваме събитието (CTSU_EVENT_SCAN_COMPLETE или грешка)
    g_last_event = p_args->event;
    // Маркираме, че сканирането е завършено
    g_scan_done  = true;
}

/* ============================================================
 * VALIDATION - ПРОВЕРКА НА КОНФИГУРАЦИЯТА
 * ============================================================ */

// Проверява дали конфигурацията е валидна
static void ctsu_validate_config(void)
{
    /* TS channels must be strictly ascending */
    /* TS каналите ТРЯБВА да са в строго възходящ ред (0, 1, 3 е OK; 1, 0, 3 е грешка) */
    // Обхождаме всички канали (започваме от втория)
    for (uint32_t i = 1; i < CTSU_NUM_CHANNELS; i++) {
        // Проверяваме дали текущият канал е по-малък или равен на предишния
        if (g_ts_channels[i] <= g_ts_channels[i - 1]) {
            /* configuration error – stop hard */
            /* Грешка в конфигурацията - спираме програмата завинаги */
            while (1);  // Безкраен цикъл (система се блокира)
        }
    }
}

/* ============================================================
 * HARDWARE PREPARATION - ПОДГОТОВКА НА ХАРДУЕРА
 * ============================================================ */

// Подготвя GPIO пиновете за CTSU работа
static void ctsu_prepare_pins(void)
{
    /* Open IOPORT (ignore already open) */
    /* Отваряме IOPORT модула (игнорираме ако вече е отворен) */
    (void)R_IOPORT_Open(&g_ioport_ctrl, &g_bsp_pin_cfg);

    /* Discharge TSCAP */
    /* Разреждаме TSCAP кондензатора (важно за стабилни измервания) */
    // Конфигурираме TSCAP пина като изход с ниско ниво (0V)
    R_IOPORT_PinCfg(&g_ioport_ctrl,
        TSCAP_PIN,                          // TSCAP пин (виж TSCAP_PIN за избраната MCU група)
        IOPORT_CFG_PORT_DIRECTION_OUTPUT |  // Посока: изход
        IOPORT_CFG_PORT_OUTPUT_LOW);        // Ниво: LOW (0V)

    // Изчакваме 1 милисекунда за пълно разреждане на кондензатора
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);

    /* Restore TSCAP to CTSU peripheral */
    /* Връщаме TSCAP пина към CTSU периферията */
    // Конфигурираме TSCAP пина като периферен пин за CTSU
    R_IOPORT_PinCfg(&g_ioport_ctrl,
                    TSCAP_PIN,                      // TSCAP пин
                    IOPORT_CFG_PERIPHERAL_PIN |     // Режим: периферен пин
                    IOPORT_PERIPHERAL_CTSU);        // Периферия: CTSU

    /* Configure TS pins */
    /* Конфигурираме всички активни TS пинове */
    // Обхождаме всички активни канали
    for (uint32_t i = 0; i < CTSU_NUM_CHANNELS; i++) {
        // Вземаме GPIO пина за текущия TS канал от таблицата
        bsp_io_port_pin_t pin = ts_to_pin[g_ts_channels[i]];

        // Проверяваме дали пинът е валиден (не е 0xFFFF)
        if (pin == BSP_IO_PORT_PIN_INVALID) {
            continue;  // Прескачаме невалидни пинове
        }

        // Конфигурираме пина като периферен пин за CTSU
        R_IOPORT_PinCfg(&g_ioport_ctrl,
                        pin,                        // GPIO пин (напр. P204)
                        IOPORT_CFG_PERIPHERAL_PIN | // Режим: периферен пин
                        IOPORT_PERIPHERAL_CTSU);    // Периферия: CTSU
    }
}

/* ============================================================
 * INIT - ИНИЦИАЛИЗАЦИЯ НА CTSU ДРАЙВЕРА
 * ============================================================ */

// Опростена инициализация на CTSU (извиква се веднъж при старт)
int ra_ctsu_init_simple(void)
{
    // Проверяваме дали вече сме инициализирани
    if (g_ready) {
        return 0;  // Вече сме готови, връщаме успех
    }

    // Проверяваме конфигурацията (TS каналите трябва да са в ред)
    ctsu_validate_config();
    // Подготвяме GPIO пиновете (TSCAP и TS пинове)
    ctsu_prepare_pins();

    // Запълваме конфигурацията с нули (изчистваме паметта)
    memset(&g_ctsu_cfg, 0, sizeof(g_ctsu_cfg));
    // Запълваме масива с елементи с нули
    memset(g_elements, 0, sizeof(g_elements));

    /* Element configuration (safe defaults for FSP 4.4) */
    /* Конфигурация на елементите (безопасни стойности за FSP 4.4) */
    // Обхождаме всички активни канали
    for (uint32_t i = 0; i < CTSU_NUM_CHANNELS; i++) {
        g_elements[i].ssdiv = CTSU_SSDIV_4000;  // Делител на сензорната честота (4.00 MHz)
        g_elements[i].so    = 0x100;            // Offset стойност (256)
        g_elements[i].snum  = 7;                // Брой измервания (7+1=8)
        g_elements[i].sdpa  = RA_CTSU_SDPA_DEFAULT; // SDPA (делител на драйв импулса), per-TS
    }

    /* Channel mask */
    /* Маска на каналите (битова карта кои канали са активни) */
    // Масив от 5 байта (40 бита) за 36 канала (TS00-TS35)
    uint8_t chac[5] = {0};
    // Обхождаме всички активни канали
    for (uint32_t i = 0; i < CTSU_NUM_CHANNELS; i++) {
        uint8_t ts = g_ts_channels[i];  // Вземаме номера на TS канала (0, 1, 3...)
        // Изчисляваме в кой байт е битът (ts >> 3 = ts / 8)
        // Изчисляваме коя позиция в байта (ts & 0x7 = ts % 8)
        // Задаваме съответния бит на 1
        chac[ts >> 3] |= (uint8_t)(1U << (ts & 0x7));
    }

    /* CTSU configuration (CTSU1 за RA4W1) */
    /* CTSU конфигурация (CTSU версия 1 за RA4W1) */
    g_ctsu_cfg.cap      = CTSU_CAP_SOFTWARE;       // Софтуерно стартиране на сканиране
    g_ctsu_cfg.md       = CTSU_MODE_SELF_MULTI_SCAN; // Само-капацитивен мулти-скан режим
    g_ctsu_cfg.txvsel   = CTSU_TXVSEL_VCC;         // Захранване от VCC
    g_ctsu_cfg.atune1   = CTSU_ATUNE1_NORMAL;      // Нормален изходен ток (40uA)
    // Забележка: posel, atune12, tclk са само за CTSU2 - RA4W1 използва CTSU1

    // Копираме масивите chac в конфигурацията (битова карта на активните канали)
    g_ctsu_cfg.ctsuchac0 = chac[0];  // Канали TS00-TS07 (бит 0 = TS00, бит 1 = TS01...)
    g_ctsu_cfg.ctsuchac1 = chac[1];  // Канали TS08-TS15
    g_ctsu_cfg.ctsuchac2 = chac[2];  // Канали TS16-TS23
    g_ctsu_cfg.ctsuchac3 = chac[3];  // Канали TS24-TS31
    g_ctsu_cfg.ctsuchac4 = chac[4];  // Канали TS32-TS35 (само 4 бита се използват)

    // Указател към масива с конфигурация на елементите
    g_ctsu_cfg.p_elements = g_elements;
    // Брой приемащи канали (за self-capacitance = брой активни канали)
    g_ctsu_cfg.num_rx     = CTSU_NUM_CHANNELS;
    // Брой предаващи канали (за self-capacitance = 0, за mutual = >0)
    g_ctsu_cfg.num_tx     = 0;

    // IMPORTANT: num_moving_average MUST be > 0.
    // FSP R_CTSU_DataGet() returns FSP_ERR_CTSU_INCOMPLETE_TUNING while
    // p_instance_ctrl->average == 0; average is incremented up to num_moving_average
    // only when tuning is complete. If num_moving_average is left at 0 (memset default),
    // average can never become > 0 and DataGet() will keep returning 6002 forever.
    g_ctsu_cfg.num_moving_average = 1;

    // Номера на прекъсванията (от vector_data.h)
    g_ctsu_cfg.write_irq = VECTOR_NUMBER_CTSU_WRITE;  // Прекъсване за запис (CTSUWR)
    g_ctsu_cfg.read_irq  = VECTOR_NUMBER_CTSU_READ;   // Прекъсване за четене (CTSURD)
    g_ctsu_cfg.end_irq   = VECTOR_NUMBER_CTSU_END;    // Прекъсване за край (CTSUFN)

    // Указател към callback функцията (извиква се при завършване на сканиране)
    g_ctsu_cfg.p_callback = ctsu_callback;
    // Контекст за callback (ползва се за да запишем event + scan_done без да разчитаме на глобални променливи)
    g_ctsu_cfg.p_context  = &g_ctsu_cb_context;

    // Отваряме CTSU драйвера с конфигурацията
    if (R_CTSU_Open(&g_ctsu_ctrl, &g_ctsu_cfg) != FSP_SUCCESS) {
        return -1;  // Грешка при отваряне
    }

    // Изчакваме 1 милисекунда за стабилизиране на хардуера
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);

    // Маркираме, че драйверът е готов за работа
    g_last_fsp_err = FSP_SUCCESS;
    g_ready = true;

    // Initial offset tuning (recommended after R_CTSU_Open()).
    // Non-fatal: we try a bounded number of scans and store the last FSP code.
    // User can re-run offset tuning later if needed.
    ra_ctsu_offset_result_t ot_res;
    (void) ra_ctsu_offset_tune(32, &ot_res);
    return 0;  // Успех
}

/* ============================================================
 * READ - ЧЕТЕНЕ НА КАПАЦИТИВНА СТОЙНОСТ
 * ============================================================ */

// Опростено четене на капацитивна стойност по индекс (0, 1, 2...)
// index е позицията в масива g_ts_channels (НЕ номерът на TS канала!)
int32_t ra_ctsu_read_simple(uint8_t index)
{
    // Проверяваме дали драйверът е инициализиран и индексът е валиден
    if (!g_ready || index >= CTSU_NUM_CHANNELS) {
        return -1;  // Грешка: не е инициализиран или невалиден индекс
    }

    // Масив за съхранение на данните от всички канали
    uint16_t data[CTSU_NUM_CHANNELS];

    // Нулираме последната FSP грешка за този read опит
    g_last_fsp_err = FSP_SUCCESS;

    // Ако CTSU tuning не е завършен, DataGet() може да върне
    // FSP_ERR_CTSU_INCOMPLETE_TUNING (6002).
    // IMPORTANT:
    // - Само допълнителни сканове НЕ са достатъчни в този случай.
    // - Renesas препоръчва scan -> wait callback -> R_CTSU_OffsetTuning() loop.
    // Затова при първа поява на 6002 правим един bounded offset_tune опит,
    // след което повтаряме scan+DataGet.
    const uint32_t tuning_max_scans = 16;
    bool did_offset_tune = false;

    for (uint32_t attempt = 0; attempt < tuning_max_scans; attempt++) {
        // Нулираме флага за завършено сканиране
        g_scan_done  = false;
        // Задаваме невалидно събитие (0xFF)
        g_last_event = (ctsu_event_t)0xFF;

        // Стартираме CTSU сканиране
        fsp_err_t err = R_CTSU_ScanStart(&g_ctsu_ctrl);
        if (err != FSP_SUCCESS) {
            g_last_fsp_err = err;
            return -2;  // Грешка при стартиране на сканиране
        }

        /* 50 ms hard timeout */
        /* Твърд таймаут от 50 милисекунди */
        // 500 итерации × 100 микросекунди = 50 милисекунди
        uint32_t timeout = 500;

        // Чакаме callback-а (или таймаут)
        while (!g_scan_done && timeout--) {
            R_BSP_SoftwareDelay(100, BSP_DELAY_UNITS_MICROSECONDS);
        }

        // Ако callback не се е случил - таймаут
        if (!g_scan_done) {
            return -3;
        }

        // Винаги опитваме DataGet (event/state са диагностични; DataGet връща точния FSP код)
        err = R_CTSU_DataGet(&g_ctsu_ctrl, data);
        if (err == FSP_SUCCESS) {
            // Успех: излизаме от tuning retry цикъла
            g_last_fsp_err = FSP_SUCCESS;
            break;
        }

        // Ако tuning още не е готов, изпълняваме препоръчания offset tuning flow
        // (само веднъж на read опит), после пробваме отново.
        if ( err == FSP_ERR_CTSU_INCOMPLETE_TUNING) {
            g_last_fsp_err = err;

            if (!did_offset_tune) {
                did_offset_tune = true;

                ra_ctsu_offset_result_t ot_res;
                (void)ra_ctsu_offset_tune(32, &ot_res);

                // Ако offset tuning не успее, връщаме грешка и оставяме
                // точния FSP код в g_last_fsp_err за диагностика.
                if (ot_res.offset_err != FSP_SUCCESS) {
                    g_last_fsp_err = ot_res.offset_err;
                    return -4;
                }
            }

            continue;
        }

        // Други грешки са фатални за това четене
        g_last_fsp_err = err;
        return -4;  // Грешка при четене на данни
    }

    // Ако след retry цикъла още има INCOMPLETE_TUNING - връщаме -4 и оставяме кода в g_last_fsp_err
    if (g_last_fsp_err == FSP_ERR_CTSU_INCOMPLETE_TUNING) {
        return -4;
    }

    /* TS00 hardware workaround */
    /* Заобикаляне на хардуерен проблем с TS00 */
    // TS00 има хардуерен проблем - стойността му е нестабилна
    // Решение: усредняваме TS00 с TS01 (ако има поне 2 канала)
    //if (g_ts_channels[0] == 0 && CTSU_NUM_CHANNELS > 1) {
        // Изчисляваме средната стойност между TS00 и TS01
    //    data[0] = (uint16_t)((data[0] + data[1]) >> 1);  // >> 1 е деление на 2
    //}

    // Връщаме стойността на исканият канал
    return (int32_t)data[index];
}

/* ============================================================
 * PUBLIC API WRAPPERS - ПУБЛИЧНИ API ФУНКЦИИ (ОБВИВКИ)
 * ============================================================ */

// Wrapper за ra_ctsu_init_simple - публична функция за инициализация
// Извиква се от MicroPython при създаване на TouchPad обект
int ra_ctsu_init(void) {
    return ra_ctsu_init_simple();  // Просто извикваме вътрешната функция
}

// Заглушка за deinit - деинициализация на CTSU
// В опростената версия не се имплементира (не затваряме CTSU)
void ra_ctsu_deinit(void) {
    // Не се имплементира в опростената версия
    // В пълната версия би извикала R_CTSU_Close(&g_ctsu_ctrl)
}

// Заглушка за channel_config - конфигуриране на канал с праг
// ts_channel: номер на TS канала (0-35)
// threshold: праг за докосване (не се използва в опростената версия)
int ra_ctsu_channel_config(uint8_t ts_channel, uint16_t threshold) {
    // Опростената версия не поддържа динамична конфигурация
    // Каналите се конфигурират статично в g_ts_channels масива
    // Просто проверяваме дали каналът е в списъка
    for (uint32_t i = 0; i < CTSU_NUM_CHANNELS; i++) {
        if (g_ts_channels[i] == ts_channel) {
            return 0;  // Успех - каналът е конфигуриран
        }
    }
    return -1;  // Грешка - каналът не е конфигуриран
}

// Wrapper за ra_ctsu_read_simple - четене на капацитивна стойност
// ts_channel: номер на TS канала (0, 1, 3... от g_ts_channels)
// Връща: капацитивна стойност (0-65535) или отрицателно число при грешка
int32_t ra_ctsu_read(uint8_t ts_channel) {
    // Намираме индекса на канала в масива g_ts_channels
    for (uint32_t i = 0; i < CTSU_NUM_CHANNELS; i++) {
        if (g_ts_channels[i] == ts_channel) {
            // Намерихме канала - извикваме ra_ctsu_read_simple с индекса
            return ra_ctsu_read_simple((uint8_t)i);
        }
    }
    return -1;  // Грешка - каналът не е намерен
}

// Проверка дали е докоснат (опростена версия без праг)
// ts_channel: номер на TS канала
// Връща: 1 ако е докоснат, 0 ако не е, отрицателно при грешка
int ra_ctsu_is_touched(uint8_t ts_channel) {
    // Четем капацитивната стойност
    int32_t value = ra_ctsu_read(ts_channel);
    if (value < 0) {
        return (int)value;  // Грешка - връщаме грешката
    }
    // Опростена проверка: ако стойността е над 1000, считаме за докосване
    // В пълната версия би се сравнявала с конфигурирания праг
    return (value > 1000) ? 1 : 0;
}

// Брой канали - връща броя на активните CTSU канали
uint8_t ra_ctsu_get_channel_count(void) {
    return (uint8_t)CTSU_NUM_CHANNELS;  // Брой елементи в g_ts_channels
}

// Read back the currently active per-channel SO offsets from the FSP control block.
// This exposes what R_CTSU_OffsetTuning() has converged to (or the initial value if not tuned).
int ra_ctsu_get_offsets(uint8_t * ts_channels, uint16_t * so_values, uint32_t max_entries, uint32_t * out_count)
{
    if (ts_channels == NULL || so_values == NULL || out_count == NULL) {
        return RA_CTSU_ERR_INVALID_ARG;
    }

    if (!g_ready) {
        return RA_CTSU_ERR_NOT_INITIALIZED;
    }

    if (CTSU_NUM_CHANNELS > max_entries) {
        return RA_CTSU_ERR_TOO_MANY_CHANNELS;
    }

    if (g_ctsu_ctrl.p_ctsuwr == NULL) {
        return RA_CTSU_ERR_NOT_INITIALIZED;
    }

    // SO is in the low 10 bits (CTSUSO[9:0]) for both CTSU v1 (CTSUSO0) and CTSU v2 (CTSUSO).
    const uint16_t so_mask = 0x03FFu;

    *out_count = (uint32_t)CTSU_NUM_CHANNELS;
    for (uint32_t i = 0; i < CTSU_NUM_CHANNELS; i++) {
        ts_channels[i] = g_ts_channels[i];

#if (BSP_FEATURE_CTSU_VERSION == 2)
        // For multi-frequency scans, p_ctsuwr is laid out per element * CTSU_CFG_NUM_SUMULTI.
        uint32_t wr_index = i;
#if defined(CTSU_CFG_NUM_SUMULTI)
        wr_index = i * (uint32_t)CTSU_CFG_NUM_SUMULTI;
#endif
        so_values[i] = (uint16_t)(g_ctsu_ctrl.p_ctsuwr[wr_index].ctsuso & so_mask);
#else
        so_values[i] = (uint16_t)(g_ctsu_ctrl.p_ctsuwr[i].ctsuso0 & so_mask);
#endif
    }

    return 0;
}

// Manually set the per-channel SO offset (low 10 bits) for one configured TS channel.
// This is intended for experiments/debug. It does not run FSP offset tuning.
int ra_ctsu_set_offset(uint8_t ts_channel, uint16_t so_value)
{
    if (!g_ready) {
        return RA_CTSU_ERR_NOT_INITIALIZED;
    }

    // Validate TS range and SO range (10-bit).
    if (ts_channel >= RA_CTSU_TS_CHANNEL_COUNT) {
        return RA_CTSU_ERR_TS_OUT_OF_RANGE;
    }

    const uint16_t so_mask = 0x03FFu;
    if ((so_value & ~so_mask) != 0u) {
        return RA_CTSU_ERR_INVALID_ARG;
    }

    if (g_ctsu_ctrl.p_ctsuwr == NULL) {
        return RA_CTSU_ERR_NOT_INITIALIZED;
    }

    // Find configured element index for this TS channel.
    uint32_t elem_index = 0;
    bool found = false;
    for (uint32_t i = 0; i < CTSU_NUM_CHANNELS; i++) {
        if (g_ts_channels[i] == ts_channel) {
            elem_index = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return RA_CTSU_ERR_NOT_CONFIGURED;
    }

    // Keep g_elements consistent too (used during init/open).
    g_elements[elem_index].so = so_value;

#if (BSP_FEATURE_CTSU_VERSION == 2)
    // For multi-frequency scans, p_ctsuwr is laid out per element * CTSU_CFG_NUM_SUMULTI.
    uint32_t wr_index = elem_index;
#if defined(CTSU_CFG_NUM_SUMULTI)
    wr_index = elem_index * (uint32_t)CTSU_CFG_NUM_SUMULTI;
#endif

    uint16_t cur = (uint16_t)g_ctsu_ctrl.p_ctsuwr[wr_index].ctsuso;
    g_ctsu_ctrl.p_ctsuwr[wr_index].ctsuso = (uint16_t)((cur & (uint16_t)~so_mask) | (so_value & so_mask));
#else
    uint16_t cur = (uint16_t)g_ctsu_ctrl.p_ctsuwr[elem_index].ctsuso0;
    g_ctsu_ctrl.p_ctsuwr[elem_index].ctsuso0 = (uint16_t)((cur & (uint16_t)~so_mask) | (so_value & so_mask));
#endif

    return 0;
}


// Последна FSP грешка (fsp_err_t) от последния опит за четене.
// Връща записания код и го занулява обратно към FSP_SUCCESS.
fsp_err_t ra_ctsu_last_fsp_err(void) {
    // Връщаме и зануляваме (както е описано в ra_ctsu.h)
    fsp_err_t err = g_last_fsp_err;
    g_last_fsp_err = FSP_SUCCESS;
    return err;
}

// Последно събитие - връща последното CTSU събитие
// Връща: CTSU_EVENT_SCAN_COMPLETE при успех, или код на грешка
uint32_t ra_ctsu_last_event(void) {
    return (uint32_t)g_last_event;  // Връщаме глобалната променлива
}

int ra_ctsu_offset_tune(uint32_t max_scans, ra_ctsu_offset_result_t * p_result)
{
    if (!p_result) {
        return -2;
    }

    if (!g_ready) {
        return RA_CTSU_ERR_NOT_INITIALIZED;
    }

    p_result->offset_err = FSP_SUCCESS;
    p_result->last_event = 0;
    p_result->scans      = 0;

    if (max_scans == 0) {
        max_scans = 32;
    }

    // Buffer used only to "consume"/clear scan data when needed.
    // This prevents a follow-up R_CTSU_ScanStart() from failing with
    // FSP_ERR_CTSU_NOT_GET_DATA (6001) after an aborted scan.
    uint16_t data[CTSU_NUM_CHANNELS];

    for (uint32_t attempt = 0; attempt < max_scans; attempt++) {
        p_result->scans = attempt + 1;

        g_scan_done  = false;
        g_last_event = (ctsu_event_t)0xFF;

        fsp_err_t err = R_CTSU_ScanStart(&g_ctsu_ctrl);
        if (err != FSP_SUCCESS) {
            g_last_fsp_err        = err;
            p_result->offset_err  = err;
            p_result->last_event  = (uint32_t)g_last_event;
            return 0;
        }

        uint32_t timeout = 500;
        while (!g_scan_done && timeout--) {
            R_BSP_SoftwareDelay(100, BSP_DELAY_UNITS_MICROSECONDS);
        }

        if (!g_scan_done) {
            g_last_fsp_err        = FSP_ERR_TIMEOUT;
            p_result->offset_err  = FSP_ERR_TIMEOUT;
            p_result->last_event  = (uint32_t)g_last_event;
            return 0;
        }


        // "Measurement completed" in the FSP docs means a successful scan completion.
        // If the scan ended with OVERFLOW/ICOMP/etc., do not attempt offset tuning.
        // IMPORTANT: still call DataGet() once to consume/clear the scan result,
        // otherwise the next ScanStart may fail with 6001 (previous scan not processed).
        if (g_last_event != CTSU_EVENT_SCAN_COMPLETE) {
            uint32_t last_ev = (uint32_t)g_last_event;
            (void) R_CTSU_DataGet(&g_ctsu_ctrl, data);
            g_last_fsp_err        = FSP_ERR_ABORTED;
            p_result->offset_err  = FSP_ERR_ABORTED;
            p_result->last_event  = last_ev;
            return 0;
        }

        err = R_CTSU_OffsetTuning(&g_ctsu_ctrl);
        g_last_fsp_err       = err;
        p_result->offset_err = err;
        p_result->last_event = (uint32_t)g_last_event;

        if (err == FSP_ERR_CTSU_INCOMPLETE_TUNING) {
            continue;
        }

        return 0;
    }

	    // If we reached max_scans without completion, keep the *real* FSP status
	    // (typically FSP_ERR_CTSU_INCOMPLETE_TUNING=6002) for diagnostics.
    return 0;
}

int ra_ctsu_diagnose(uint32_t max_scans, ra_ctsu_diag_result_t * p_result)
{
    if (!p_result) {
        return -2;
    }

    if (!g_ready) {
        return RA_CTSU_ERR_NOT_INITIALIZED;
    }

    p_result->data_get_err  = FSP_SUCCESS;
    p_result->diagnosis_err = FSP_SUCCESS;
    p_result->last_event    = 0;
    p_result->scans         = 0;

    if (max_scans == 0) {
        max_scans = 32;
    }

    // Затваряме нормалната инстанция
    R_CTSU_Close(&g_ctsu_ctrl);

    // Конфигурираме за diagnosis режим (временно променяме g_ctsu_cfg)
    ctsu_md_t saved_mode = g_ctsu_cfg.md;
    g_ctsu_cfg.md = CTSU_MODE_DIAGNOSIS_SCAN;

    // Отваряме в diagnosis режим
    fsp_err_t err = R_CTSU_Open(&g_ctsu_ctrl, &g_ctsu_cfg);
    if (err != FSP_SUCCESS) {
        // Опит да възстановим нормалния режим
        g_ctsu_cfg.md = saved_mode;
        R_CTSU_Open(&g_ctsu_ctrl, &g_ctsu_cfg);
        return -3;
    }

    // Diagnosis цикъл
    for (uint32_t attempt = 0; attempt < max_scans; attempt++) {
        p_result->scans = attempt + 1;

        g_scan_done  = false;
        g_last_event = (ctsu_event_t)0xFF;

        err = R_CTSU_ScanStart(&g_ctsu_ctrl);
        if (err != FSP_SUCCESS) {
            p_result->data_get_err = err;
            p_result->last_event   = (uint32_t)g_last_event;
            goto restore_normal_mode;
        }

        uint32_t timeout = 500;
        while (!g_scan_done && timeout--) {
            R_BSP_SoftwareDelay(100, BSP_DELAY_UNITS_MICROSECONDS);
        }

        if (!g_scan_done) {
            p_result->data_get_err = FSP_ERR_TIMEOUT;
            p_result->last_event   = (uint32_t)g_last_event;
            goto restore_normal_mode;
        }

        uint16_t dummy = 0;
        err = R_CTSU_DataGet(&g_ctsu_ctrl, &dummy);
        p_result->data_get_err = err;
        p_result->last_event   = (uint32_t)g_last_event;

        if (err == FSP_ERR_CTSU_DIAG_NOT_YET) {
            continue;
        }

        if (err == FSP_ERR_CTSU_INCOMPLETE_TUNING) {
            continue;
        }

        if (err != FSP_SUCCESS) {
            goto restore_normal_mode;
        }

        p_result->diagnosis_err = R_CTSU_Diagnosis(&g_ctsu_ctrl);
        goto restore_normal_mode;
    }

    // Ако излязохме от цикъла без успех (все още DIAG_NOT_YET или INCOMPLETE_TUNING),
    // това означава че достигнахме max_scans без да завършим diagnosis - timeout
    if (p_result->data_get_err == FSP_ERR_CTSU_DIAG_NOT_YET ||
        p_result->data_get_err == FSP_ERR_CTSU_INCOMPLETE_TUNING) {
        p_result->data_get_err = FSP_ERR_TIMEOUT;
    }

restore_normal_mode:
    // Затваряме diagnosis режим
    R_CTSU_Close(&g_ctsu_ctrl);

    // Възстановяваме нормалния режим
    g_ctsu_cfg.md = saved_mode;
    err = R_CTSU_Open(&g_ctsu_ctrl, &g_ctsu_cfg);
    if (err != FSP_SUCCESS) {
        g_ready = false;
        return -4;
    }

    // Изчакваме 1ms за стабилизиране на хардуера след смяна на режима
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);

    return 0;
}

// Преобразуване пин -> канал
// pin: GPIO пин (напр. BSP_IO_PORT_02_PIN_04)
// Връща: номер на TS канала (0-35) или -1 ако пинът не е CTSU пин
int8_t ra_ctsu_pin_to_channel(uint16_t pin) {
    // Търсим в таблицата ts_to_pin (обхождаме всички 36 възможни канала)
    for (uint8_t ts = 0; ts < 36; ts++) {
        // Проверяваме дали текущият TS канал съответства на търсения пин
        if (ts_to_pin[ts] == pin) {
            return (int8_t)ts;  // Намерихме съответствието - връщаме TS номера
        }
    }
    return -1;  // Пинът не е CTSU пин (не е намерен в таблицата)
}

// Заглушка за channel_config_pin - конфигуриране на канал по pin_code
// pin_code: кодиран GPIO пин (Port<<4 | Pin), напр. P011 = 0x0B
// threshold: праг за докосване
// Връща: 0 при успех, -1 при грешка
int ra_ctsu_channel_config_pin(uint8_t pin_code, uint16_t threshold) {
    // Преобразуваме pin_code в bsp_io_port_pin_t формат
    // pin_code = 0x0B (Port 0, Pin 11) -> pin = 0x000B
    // pin_code = 0x24 (Port 2, Pin 4)  -> pin = 0x0204
    uint16_t pin = (uint16_t)((pin_code >> 4) << 8) | (pin_code & 0x0F);
    // Намираме TS канала за този пин
    int8_t ts = ra_ctsu_pin_to_channel(pin);
    if (ts < 0) {
        return -1;  // Грешка - пинът не е CTSU пин
    }
    // Конфигурираме канала с прага
    return ra_ctsu_channel_config((uint8_t)ts, threshold);
}
