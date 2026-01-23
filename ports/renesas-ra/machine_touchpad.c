/*
 * Този файл е част от проекта MicroPython, http://micropython.org/
 *
 * Лиценз MIT (MIT)
 *
 * Авторски права (c) 2017 Nick Moore
 * Авторски права (c) 2026 Vekatech
 *
 * Разрешава се безплатно на всяко лице, което получи копие
 * от този софтуер и свързаните документационни файлове ("Софтуера"), да работи
 * със Софтуера без ограничения, включително без ограничение правата
 * да използва, копира, модифицира, обединява, публикува, разпространява, сублицензира и/или продава
 * копия на Софтуера, и да позволява на лицата, на които Софтуерът е
 * предоставен, да правят това, при следните условия:
 *
 * Горното известие за авторски права и това известие за разрешение трябва да бъдат включени във
 * всички копия или значителни части от Софтуера.
 *
 * СОФТУЕРЪТ СЕ ПРЕДОСТАВЯ "КАКТО Е", БЕЗ ГАРАНЦИЯ ОТ КАКЪВТО И ДА Е ВИД, ИЗРИЧНА ИЛИ
 * ПОДРАЗБИРАЩА СЕ, ВКЛЮЧИТЕЛНО, НО НЕ САМО, ГАРАНЦИИ ЗА ПРОДАВАЕМОСТ,
 * ГОДНОСТ ЗА ОПРЕДЕЛЕНА ЦЕЛ И НЕНАРУШАВАНЕ. В НИКАКЪВ СЛУЧАЙ
 * АВТОРИТЕ ИЛИ ПРИТЕЖАТЕЛИТЕ НА АВТОРСКИ ПРАВА НЯМА ДА НОСЯТ ОТГОВОРНОСТ ЗА КАКВИТО И ДА Е ПРЕТЕНЦИИ, ЩЕТИ ИЛИ ДРУГА
 * ОТГОВОРНОСТ, НЕЗАВИСИМО ДАЛИ В ДОГОВОРНО ДЕЙСТВИЕ, ДЕЛИКТ ИЛИ ПО ДРУГ НАЧИН, ПРОИЗТИЧАЩИ ОТ,
 * ОТ ИЛИ ВЪВ ВРЪЗКА СЪС СОФТУЕРА ИЛИ ИЗПОЛЗВАНЕТО ИЛИ ДРУГИ СДЕЛКИ СЪС
 * СОФТУЕРА.
 */

// Включваме MicroPython runtime - основни функции на интерпретатора
#include "py/runtime.h"
// Включваме list helper API (mp_obj_list_append)
#include "py/objlist.h"
// Включваме MicroPython HAL (Hardware Abstraction Layer) - абстракция на хардуера
#include "py/mphal.h"
// Включваме MicroPython errno - кодове на грешки
#include "py/mperrno.h"
// Включваме модула machine - основен модул за хардуерен достъп
#include "modmachine.h"

// TouchPad за Renesas RA използва CTSU HAL обвивката.
#include "ra/ra_ctsu.h"

// -----------------------------------------------------------------------------
// Renesas RA TouchPad свързващ слой (binding)
// -----------------------------------------------------------------------------
// Този файл е MicroPython свързващ слой за капацитивно сензорно докосване.
//
// Граница на архитектурата (важно):
//   - Този модул НЕ ТРЯБВА да извиква FSP r_ctsu API директно (R_CTSU_*).
//   - Той извиква само тънката HAL обвивка (ra_ctsu_*), която притежава CTSU
//     жизнения цикъл и налага политиката "тънка обвивка".
//
// Предоставено API (минимално, предвидимо):
//   TouchPad(Pin)           -> създава TouchPad от пин с TS възможност
//   tp.config(threshold)    -> задава праг, използван от tp.value()
//   tp.read()               -> необработена CTSU стойност (int)
//   tp.value()              -> 0/1 използвайки конфигурирания праг (без междинен слой)
//   tp.last_error()         -> последен FSP код на грешка, уловен от ra_ctsu обвивката
//
// Употреба:
//   from machine import Pin, TouchPad
//   tp = TouchPad(Pin('P204'))
//   tp.config(500)
//   print(tp.read(), tp.value(), tp.last_error())

// Структура за TouchPad обект в MicroPython
typedef struct _machine_touchpad_obj_t {
    mp_obj_base_t base;       // Базова структура на MicroPython обект
    mp_hal_pin_obj_t pin;     // Пин обект (физически пин)
    uint8_t channel;          // CTSU канал (TS00-TS35)
    uint16_t threshold;       // Праг за докосване
} machine_touchpad_obj_t;

// Функция за създаване на нов TouchPad обект (конструктор)
static mp_obj_t ra_touchpad_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // Проверяваме броя аргументи: очакваме точно 1 аргумент (Pin)
    mp_arg_check_num(n_args, n_kw, 1, 1, true);

    // Създаваме нов TouchPad обект в паметта
    machine_touchpad_obj_t *self = m_new_obj(machine_touchpad_obj_t);
    // Задаваме типа на обекта
    self->base.type = type;
    // Извличаме пин обекта от първия аргумент
    self->pin = mp_hal_get_pin_obj(args[0]);

    // Прагът по подразбиране е 0 (value() ще връща false, докато потребителят не го зададе).
    self->threshold = 0;

    // Инициализираме CTSU HAL (идемпотентна операция - може да се извика многократно).
    if (ra_ctsu_init() != 0) {
        // Ако инициализацията се провали, хвърляме грешка
        mp_raise_OSError(MP_EIO);
    }

    // Преобразуваме физически пин -> CTSU TS канал.
    // ВАЖНО: machine_pin_obj_t->pin е 8-bit pin_code (port<<4 | bit), напр. P407 -> 0x47.
    // ra_ctsu_pin_to_channel() работи с FSP bsp_io_port_pin_t кодировка (port<<8 | bit), напр. P407 -> 0x0407.
    uint8_t pin_code = self->pin->pin;
    uint16_t bsp_pin = (uint16_t)(((pin_code >> 4) << 8) | (pin_code & 0x0F));
    int8_t ch = ra_ctsu_pin_to_channel(bsp_pin);
    if (ch < 0) {
        // Ако пинът не е CTSU сензорен пин, хвърляме грешка
        mp_raise_ValueError(MP_ERROR_TEXT("Pin is not a CTSU touch pin"));
    }
    // Запазваме номера на канала
    self->channel = (uint8_t)ch;

    // Конфигурираме прага на канала (съхранява се в ra_ctsu слоя).
    int  rc = ra_ctsu_channel_config(self->channel, self->threshold);
    // ra_ctsu_channel_config() е тънка обвивка; може да се провали ако конфигурацията
    // за целевата платка е наситена (твърде много канали) или ако CTSU init се провали.
    // Преобразуваме "твърде много канали" в ENOMEM за по-ясна MicroPython грешка.
    if (rc == -3) {
        // Грешка: няма памет (твърде много канали)
        mp_raise_OSError(MP_ENOMEM);
    }
    if (rc != 0) {
        // Друга грешка при конфигуриране
        mp_raise_OSError(MP_EIO);
    }

    // Връщаме създадения обект
    return MP_OBJ_FROM_PTR(self);
}

// Функция за конфигуриране на TouchPad (задаване на праг)
static mp_obj_t ra_touchpad_config(size_t n_args, const mp_obj_t *args) {
    // Извличаме self обекта от първия аргумент
    machine_touchpad_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    // Съвместимост с ESP32 TouchPad API: config(value) задава прага за докосване.
    // Това е умишлено строго (прагът е задължителен).
    mp_int_t value = mp_obj_get_int(args[1]);  // Вземаме втория аргумент (прага)
    // Проверяваме дали прагът е в допустимия диапазон (0-65535)
    if ((value < 0) || (value > 0xffff)) {
        mp_raise_ValueError(MP_ERROR_TEXT("threshold out of range"));
    }
    // Запазваме новия праг
    self->threshold = (uint16_t)value;

    // Актуализираме прага на CTSU канала.
    int rc = ra_ctsu_channel_config(self->channel, self->threshold);
    if (rc == -3) {
        // Грешка: няма памет (твърде много канали)
        mp_raise_OSError(MP_ENOMEM);
    }
    if (rc != 0) {
        // Друга грешка при конфигуриране
        mp_raise_OSError(MP_EIO);
    }

    // Връщаме None (функцията не връща стойност)
    return mp_const_none;
}
// Изискваме точно 2 аргумента: self + threshold.
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ra_touchpad_config_obj, 2, 2, ra_touchpad_config);

// Функция за четене на необработена CTSU стойност
static mp_obj_t ra_touchpad_read(mp_obj_t self_in) {
    // Извличаме self обекта
    machine_touchpad_obj_t *self = MP_OBJ_TO_PTR(self_in);

    // Четем необработената CTSU стойност за този канал.
    int32_t count = ra_ctsu_read(self->channel);
    if (count < 0) {
        // Включваме последния FSP код на грешка, за да избегнем диагностика само чрез J-Link.
        int fsp_err = (int)ra_ctsu_last_fsp_err();  // Вземаме FSP грешката
        unsigned int ev = (unsigned int)ra_ctsu_last_event();  // Вземаме събитието
        // Хвърляме грешка с подробна информация
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("CTSU read error: %d (fsp=%d ev=0x%x)"), (int)count, fsp_err, ev);
    }

    // Връщаме стойността като MicroPython integer
    return mp_obj_new_int(count);
}
// Дефинираме функционален обект с 1 аргумент (self)
static MP_DEFINE_CONST_FUN_OBJ_1(ra_touchpad_read_obj, ra_touchpad_read);

static mp_obj_t ra_touchpad_value(mp_obj_t self_in) {
    machine_touchpad_obj_t *self = MP_OBJ_TO_PTR(self_in);

    int32_t count = ra_ctsu_read(self->channel);
    if (count < 0) {
        int fsp_err = (int)ra_ctsu_last_fsp_err();
        unsigned int ev = (unsigned int)ra_ctsu_last_event();
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("CTSU value error: %d (fsp=%d ev=0x%x)"),
            (int)count, fsp_err, ev);
    }

    return mp_obj_new_int(count > self->threshold);
}
 
// Дефинираме функционален обект с 1 аргумент (self)
static MP_DEFINE_CONST_FUN_OBJ_1(ra_touchpad_value_obj, ra_touchpad_value);

// read_value() - return (count, value, last_error) from a *single* measurement.
// This avoids doing two scans (read() + value()).
static mp_obj_t ra_touchpad_read_value(mp_obj_t self_in) {
    machine_touchpad_obj_t *self = MP_OBJ_TO_PTR(self_in);

    int32_t count = ra_ctsu_read(self->channel);
    if (count < 0) {
        int fsp_err = (int)ra_ctsu_last_fsp_err();
        unsigned int ev = (unsigned int)ra_ctsu_last_event();
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("CTSU read_value error: %d (fsp=%d ev=0x%x)"), (int)count, fsp_err, ev);
    }

    // Note: ra_ctsu_last_fsp_err() returns the last recorded FSP code and resets it.
    int fsp_err = (int)ra_ctsu_last_fsp_err();
    mp_obj_t items[3] = {
        mp_obj_new_int(count),
        mp_obj_new_int(count > self->threshold),
        mp_obj_new_int(fsp_err),
    };
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ra_touchpad_read_value_obj, ra_touchpad_read_value);

// Функция за получаване на последната FSP грешка
static mp_obj_t ra_touchpad_last_error(mp_obj_t self_in) {
    // Излагаме последната FSP грешка, записана от ra_ctsu обвивката.
    (void)self_in;
    return mp_obj_new_int((int)ra_ctsu_last_fsp_err());
}
// Дефинираме функционален обект с 1 аргумент (self)
static MP_DEFINE_CONST_FUN_OBJ_1(ra_touchpad_last_error_obj, ra_touchpad_last_error);

static mp_obj_t ra_touchpad_diagnose(size_t n_args, const mp_obj_t *args) {
    (void)args[0];

    mp_int_t max_scans = 32;
    if (n_args >= 2) {
        max_scans = mp_obj_get_int(args[1]);
        if (max_scans < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("max_scans must be >= 0"));
        }
    }

    ra_ctsu_diag_result_t res;
    int rc = ra_ctsu_diagnose((uint32_t)max_scans, &res);
    if (rc < 0) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("CTSU diagnose error: %d"), rc);
    }

    mp_obj_t items[4] = {
        mp_obj_new_int((int)res.data_get_err),
        mp_obj_new_int((int)res.diagnosis_err),
        mp_obj_new_int_from_uint((mp_uint_t)res.last_event),
        mp_obj_new_int_from_uint((mp_uint_t)res.scans),
    };
    return mp_obj_new_tuple(4, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ra_touchpad_diagnose_obj, 1, 2, ra_touchpad_diagnose);

static mp_obj_t ra_touchpad_offset_tune(size_t n_args, const mp_obj_t *args) {
    (void)args[0];

    mp_int_t max_scans = 32;
    if (n_args >= 2) {
        max_scans = mp_obj_get_int(args[1]);
        if (max_scans < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("max_scans must be >= 0"));
        }
    }

    ra_ctsu_offset_result_t res;
    int rc = ra_ctsu_offset_tune((uint32_t)max_scans, &res);
    if (rc < 0) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("CTSU offset_tune error: %d"), rc);
    }

    mp_obj_t items[3] = {
        mp_obj_new_int((int)res.offset_err),
        mp_obj_new_int_from_uint((mp_uint_t)res.last_event),
        mp_obj_new_int_from_uint((mp_uint_t)res.scans),
    };
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ra_touchpad_offset_tune_obj, 1, 2, ra_touchpad_offset_tune);

// offsets() - list of (ts_channel, so_offset) for all configured CTSU channels.
static mp_obj_t ra_touchpad_offsets(mp_obj_t self_in) {
    (void)self_in;

    uint8_t ts[RA_CTSU_MAX_CHANNELS];
    uint16_t so[RA_CTSU_MAX_CHANNELS];
    uint32_t count = 0;

    int rc = ra_ctsu_get_offsets(ts, so, RA_CTSU_MAX_CHANNELS, &count);
    if (rc < 0) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("CTSU offsets error: %d"), rc);
    }

    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < (size_t)count; i++) {
        mp_obj_t pair[2] = {
            mp_obj_new_int((int)ts[i]),
            mp_obj_new_int((int)so[i]),
        };
        mp_obj_list_append(list, mp_obj_new_tuple(2, pair));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ra_touchpad_offsets_obj, ra_touchpad_offsets);

// set_offset(so) - manually set SO offset (0..1023) for this TouchPad's TS channel.
static mp_obj_t ra_touchpad_set_offset(mp_obj_t self_in, mp_obj_t so_in) {
    machine_touchpad_obj_t *self = MP_OBJ_TO_PTR(self_in);

    mp_int_t so = mp_obj_get_int(so_in);
    if ((so < 0) || (so > 1023)) {
        mp_raise_ValueError(MP_ERROR_TEXT("so must be 0..1023"));
    }

    int rc = ra_ctsu_set_offset(self->channel, (uint16_t)so);
    if (rc < 0) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("CTSU set_offset error: %d"), rc);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(ra_touchpad_set_offset_obj, ra_touchpad_set_offset);

// Таблица с локални методи на TouchPad класа
static const mp_rom_map_elem_t ra_touchpad_locals_dict_table[] = {
    // config(threshold) - задава праг за докосване
    { MP_ROM_QSTR(MP_QSTR_config), MP_ROM_PTR(&ra_touchpad_config_obj) },
    // read() - чете необработена CTSU стойност
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&ra_touchpad_read_obj) },
    // read_value() - връща (count, value, last_error) от едно и също измерване
    { MP_ROM_QSTR(MP_QSTR_read_value), MP_ROM_PTR(&ra_touchpad_read_value_obj) },
    // value() - връща 0 или 1 (докоснат/не е докоснат)
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&ra_touchpad_value_obj) },
    // last_error() - връща последната FSP грешка
    { MP_ROM_QSTR(MP_QSTR_last_error), MP_ROM_PTR(&ra_touchpad_last_error_obj) },
    // diagnose([max_scans]) - стартира CTSU diagnosis scan loop и връща (data_get_err, diagnosis_err, last_event, scans)
    { MP_ROM_QSTR(MP_QSTR_diagnose), MP_ROM_PTR(&ra_touchpad_diagnose_obj) },
    // offset_tune([max_scans]) - стартира scan->OffsetTuning loop и връща (offset_err, last_event, scans)
    { MP_ROM_QSTR(MP_QSTR_offset_tune), MP_ROM_PTR(&ra_touchpad_offset_tune_obj) },
    // offsets() - връща списък от (ts_channel, so_offset) за всички активни канали
    { MP_ROM_QSTR(MP_QSTR_offsets), MP_ROM_PTR(&ra_touchpad_offsets_obj) },

    // set_offset(so) - ръчно задава SO offset (0..1023) за този канал
    { MP_ROM_QSTR(MP_QSTR_set_offset), MP_ROM_PTR(&ra_touchpad_set_offset_obj) },
};
// Дефинираме речника с локални методи
static MP_DEFINE_CONST_DICT(ra_touchpad_locals_dict, ra_touchpad_locals_dict_table);

// Дефинираме типа на TouchPad класа
MP_DEFINE_CONST_OBJ_TYPE(
    machine_touchpad_type,      // Име на типа в C
    MP_QSTR_TouchPad,           // Име на класа в Python
    MP_TYPE_FLAG_NONE,          // Флагове (няма специални)
    make_new, ra_touchpad_make_new,  // Конструктор
    locals_dict, &ra_touchpad_locals_dict  // Речник с методи
);
