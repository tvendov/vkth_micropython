"""Actual I2CAsync worker + MicroPython softtimer, pairheap and C scheduler.

Host stubs replace hardware and Python calls, not timer/worker implementations.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from test_i2c_stop_state import function

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parents[1]


def without_includes(path):
    return re.sub(r'^#include[^\n]*', '', path.read_text(encoding='utf-8'), flags=re.M)


def definition(source, signature):
    index = source.index(signature)
    while ';' in source[index:source.index('{', index)]:
        index = source.index(signature, index + len(signature))
    return function(source[index:], signature)


def main():
    binding = (PORT / 'machine_i2c.c').read_text(encoding='utf-8')
    scheduler = (ROOT / 'py/scheduler.c').read_text(encoding='utf-8')
    names = ('root_set', 'callback_root_set', 'callback_root_get', 'from_node',
             'from_timer', 'schedule_completion', 'transfer_complete', 'record_us',
             'timeout_callback', 'timeout_stop', 'recovery_arm',
             'request_recovery', 'shutdown', 'error_to_errno', 'update_impl', 'update',
             'complete_node', 'cancel_obj_fun')
    worker = []
    for name in names:
        signature = re.search(r'static [\w *]+machine_i2c_async_' + name + r'\(', binding)
        worker.append(definition(binding, signature.group(0)))
    structs = '\n'.join(re.findall(
        r'typedef struct _machine_i2c(?:_async)?_obj_t \{.*?\} machine_i2c(?:_async)?_obj_t;',
        binding, re.S))
    program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t mp_obj_t;
typedef struct { void *type; } mp_obj_base_t;
typedef unsigned mp_uint_t;
typedef struct _mp_sched_node_t mp_sched_node_t;
typedef void (*mp_sched_callback_t)(mp_sched_node_t *);
struct _mp_sched_node_t { mp_sched_callback_t callback; mp_sched_node_t *next; };
typedef struct { mp_obj_t func, arg; } mp_sched_item_t;
static struct {
    int sched_state, sched_idx, sched_len;
    mp_sched_node_t *sched_head, *sched_tail;
    mp_sched_item_t sched_queue[8];
    mp_obj_t machine_i2c_async_buffer_roots[3], machine_i2c_async_callback_roots[3];
} vm;
#define MP_STATE_VM(x) (vm.x)
#define MP_STATE_PORT(x) (vm.x)
#define MP_STATE_THREAD(x) 0
#define MP_SCHED_IDLE 0
#define MP_SCHED_PENDING 1
#define MP_SCHED_LOCKED (-1)
#define MICROPY_SCHEDULER_STATIC_NODES 1
#define MICROPY_SCHED_HOOK_SCHEDULED ((void)0)
#define MICROPY_BEGIN_ATOMIC_SECTION() 0
#define MICROPY_END_ATOMIC_SECTION(x) ((void)(x))
#define MICROPY_PY_PENDSV_ENTER ((void)0)
#define MICROPY_PY_PENDSV_EXIT ((void)0)
#define MICROPY_SOFT_TIMER_TICKS_MS uwTick
#define __IO volatile
#define MP_OBJ_NULL 0
#define mp_const_none 1
#define MP_OBJ_FROM_PTR(x) ((mp_obj_t)(x))
#define MP_OBJ_TO_PTR(x) ((void *)(x))
#define MP_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define IDX_MASK(x) ((x) & 7)
#define MP_EBUSY 16
#define MP_ENODEV 19
#define MP_EIO 5
#define MP_ETIMEDOUT 110
#define MP_ECANCELED 125
static volatile uint32_t uwTick;
static int py_calls, begin_calls, steps, shutdown_calls;
static bool in_timer, hw_done, hw_success;
static unsigned inject_clock;
void soft_timer_handler(void);
static uint32_t mp_hal_ticks_ms(void) { return uwTick; }
static uint32_t mp_hal_ticks_us(void) {
    if (inject_clock && --inject_clock == 0) {
        ++uwTick; in_timer = true; soft_timer_handler(); in_timer = false;
    }
    return uwTick * 1000;
}
static unsigned mp_sched_num_pending(void) { return vm.sched_len; }
static bool mp_sched_empty(void) { return !vm.sched_len; }
static void mp_call_function_1_protected(mp_obj_t f, mp_obj_t a) {
    (void)f; (void)a; assert(!in_timer); ++py_calls;
}
static bool mp_irq_dispatch(mp_obj_t f, mp_obj_t a, bool hard) {
    (void)f; (void)a; (void)hard; assert(false); return true;
}
static void gc_collect_root(void **root, size_t len) { (void)root; (void)len; }
PAIRHEADER
PAIRCODE
TIMERHEADER
TIMERCODE
SCHEDULER
typedef struct { int unused; } R_IIC0_Type;
#include "ra_i2c.h"
typedef struct { uint32_t pin; } pin_t;
typedef pin_t *mp_hal_pin_obj_t;
STRUCTS
static machine_i2c_async_obj_t machine_i2c_async_obj[3];
static bool machine_i2c_async_update(machine_i2c_async_obj_t *self);
static void machine_i2c_async_complete_node(mp_sched_node_t *node);
bool ra_i2c_recovery_begin(R_IIC0_Type *i, xaction_t *a, uint32_t c,
    uint32_t d, uint32_t f, uint32_t n) {
    (void)i; (void)a; (void)c; (void)d; (void)f; (void)n;
    assert(!in_timer); ++begin_calls; return true;
}
bool ra_i2c_recovery_step(uint32_t now) {
    (void)now; assert(!in_timer); hw_done = ++steps == 3; return hw_done;
}
int ra_i2c_recovery_result(void) { return 1; }
bool ra_i2c_recovery_buffer_safe(void) { return true; }
ra_i2c_async_status_t ra_i2c_action_poll_async(xaction_t *a) {
    if (!hw_done) return RA_I2C_ASYNC_PENDING;
    if (hw_success) { a->m_error = RA_I2C_ERROR_OK; return RA_I2C_ASYNC_COMPLETE; }
    a->m_error = RA_I2C_ERROR_TMOF; return RA_I2C_ASYNC_ERROR;
}
void ra_i2c_action_cancel_async(R_IIC0_Type *i, xaction_t *a) {
    (void)i; (void)a; ++shutdown_calls;
}
WORKER
static pin_t scl, sda;
static R_IIC0_Type inst;
static machine_i2c_obj_t bus = {.i2c_inst=&inst, .i2c_id=1, .scl=&scl, .sda=&sda, .freq=400000};
static machine_i2c_async_obj_t *self = &machine_i2c_async_obj[1];
static void setup(bool callback) {
    assert(soft_timer_heap == NULL && vm.sched_head == NULL);
    memset(self, 0, sizeof(*self)); memset(&vm, 0, sizeof(vm));
    py_calls = steps = begin_calls = shutdown_calls = 0;
    hw_done = hw_success = false; uwTick = 0;
    self->bus = &bus; self->active = true; self->generation = 42;
    self->timeout_ms = 1; self->timer_generation = 42;
    machine_i2c_async_root_set(self, 100);
    machine_i2c_async_callback_root_set(self, callback ? 200 : MP_OBJ_NULL);
    soft_timer_static_init(&self->timeout_timer, SOFT_TIMER_MODE_ONE_SHOT, 0,
                           machine_i2c_async_timeout_callback);
    self->timeout_timer_active = true;
    soft_timer_insert(&self->timeout_timer, 1);
}
static void tick(uint32_t next) {
    uwTick = next; in_timer = true; soft_timer_handler(); in_timer = false;
}
static void dispatch(void) { mp_sched_run_pending(); }
static int other_calls;
static void other_timer(soft_timer_entry_t *entry) { (void)entry; ++other_calls; }
int main(void) {
    for (int callback = 0; callback < 2; ++callback) {
        setup(callback);
        tick(1); assert(steps == 0 && begin_calls == 0 && soft_timer_heap == NULL);
        dispatch(); assert(begin_calls == 1 && self->recovering && self->active);
        assert(soft_timer_heap == &self->timeout_timer && self->timeout_timer.expiry_ms == 2);
        tick(100); assert(steps == 0 && soft_timer_heap == NULL);
        dispatch(); assert(steps == 1 && self->timeout_timer.expiry_ms == 101);
        for (int n = 0; n < 1000; ++n) assert(!machine_i2c_async_update(self));
        assert(steps == 1 && vm.machine_i2c_async_buffer_roots[1] == 100);
        tick(101); dispatch(); tick(102); dispatch();
        assert(steps == 3 && !self->active && self->result == -110);
        assert(py_calls == callback && !vm.machine_i2c_async_buffer_roots[1]);
        dispatch(); assert(py_calls == callback && soft_timer_heap == NULL);
    }
    setup(true);
    machine_i2c_async_cancel_obj_fun((mp_obj_t)self);
    machine_i2c_async_cancel_obj_fun((mp_obj_t)self);
    assert(begin_calls == 1 && steps == 0 && self->active);
    tick(1); dispatch(); tick(2); dispatch(); tick(3); dispatch();
    assert(self->result == -125 && py_calls == 1);
    setup(true); tick(1); // Old node queued while VM is reset.
    machine_i2c_async_shutdown(self);
    assert(self->generation == 43 && self->completion_node.callback != NULL);
    dispatch(); assert(!py_calls && !steps && self->completion_node.callback == NULL);
    assert(soft_timer_heap == NULL && vm.sched_head == NULL);
    setup(false); machine_i2c_async_cancel_obj_fun((mp_obj_t)self);
    soft_timer_deinit(); // The shared implementation retains static timers.
    assert(soft_timer_heap == &self->timeout_timer);
    machine_i2c_async_shutdown(self); assert(soft_timer_heap == NULL);
    setup(false); self->timer_generation = 41; tick(1); dispatch();
    assert(!begin_calls && !steps && vm.sched_head == NULL); // Stale timer ignored.
    self->timeout_timer_active = false; machine_i2c_async_shutdown(self);
    setup(false); machine_i2c_async_transfer_complete(self);
    machine_i2c_async_transfer_complete(self); assert(vm.sched_head == vm.sched_tail);
    dispatch(); machine_i2c_async_shutdown(self);
    setup(false); self->action.m_error = RA_I2C_ERROR_TMOF;
    assert(!machine_i2c_async_update(self) && begin_calls == 1);
    tick(1); dispatch(); tick(2); dispatch(); tick(3); dispatch();
    assert(self->result == -110 && !self->active);
    setup(false); self->generation = UINT32_MAX;
    machine_i2c_async_schedule_completion(self); machine_i2c_async_shutdown(self);
    assert(self->generation == 0); dispatch(); assert(!py_calls && !steps);
    setup(false); machine_i2c_async_timeout_stop(self);
    uwTick = UINT32_MAX; self->start_ms = uwTick;
    machine_i2c_async_cancel_obj_fun((mp_obj_t)self);
    assert(self->timeout_timer.expiry_ms == 0);
    tick(0); dispatch(); machine_i2c_async_cancel_obj_fun((mp_obj_t)self);
    tick(1); dispatch(); machine_i2c_async_cancel_obj_fun((mp_obj_t)self);
    tick(2); dispatch(); assert(steps == 3 && begin_calls == 1 && self->result == -125);
    setup(false); soft_timer_entry_t other;
    soft_timer_static_init(&other, SOFT_TIMER_MODE_ONE_SHOT, 0, other_timer);
    soft_timer_insert(&other, 2); other_calls = 0;
    tick(1); dispatch(); tick(2); dispatch(); assert(other_calls == 1 && steps == 1);
    tick(3); dispatch(); tick(4); dispatch(); assert(!self->active && soft_timer_heap == NULL);
    setup(false); tick(1); dispatch(); tick(2);
    inject_clock = 4; // Fire the rearmed timer before current worker returns.
    dispatch(); assert(steps == 1 && self->recovery_due && vm.sched_head != NULL);
    assert(soft_timer_heap == NULL); // Expired, so only the queued node can progress.
    dispatch(); assert(steps == 2); tick(4); dispatch(); assert(!self->active);
    setup(true); self->transfer_len = 17; hw_done = hw_success = true;
    tick(1); machine_i2c_async_transfer_complete(self); dispatch();
    assert(self->result == 17 && py_calls == 1 && !begin_calls && !self->active);
    setup(true); tick(1); dispatch(); // Timeout selected before a later cancel.
    machine_i2c_async_cancel_obj_fun((mp_obj_t)self);
    tick(2); dispatch(); tick(3); dispatch(); tick(4); dispatch();
    assert(self->result == -110 && begin_calls == 1 && py_calls == 1);
    puts("PASS actual C worker + softtimer + pairheap + scheduler: 14 lifecycle scenarios");
}
'''
    for token, code in (
        ('PAIRHEADER', without_includes(ROOT / 'py/pairheap.h')),
        ('PAIRCODE', without_includes(ROOT / 'py/pairheap.c')),
        ('TIMERHEADER', without_includes(ROOT / 'shared/runtime/softtimer.h')),
        ('TIMERCODE', without_includes(ROOT / 'shared/runtime/softtimer.c')),
        ('SCHEDULER', '\n'.join(function(scheduler, s) for s in (
            'void mp_sched_unlock(', 'bool mp_sched_schedule_node(',
            'static inline void mp_sched_run_pending('))),
        ('STRUCTS', structs), ('WORKER', '\n'.join(worker)),
    ):
        program = program.replace('\n' + token + '\n', '\n' + code + '\n')
    with tempfile.TemporaryDirectory(prefix='riic-softtimer-test-') as directory:
        path = Path(directory)
        (path / 'test.c').write_text(program, encoding='utf-8')
        subprocess.run(shlex.split(os.environ.get('CC', 'gcc')) + [
            '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(PORT / 'ra'),
            str(path / 'test.c'), '-o', str(path / 'test.exe')], check=True)
        subprocess.run([str(path / 'test.exe')], check=True)


if __name__ == '__main__':
    main()
