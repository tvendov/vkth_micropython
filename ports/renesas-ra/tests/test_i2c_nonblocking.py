"""Compile the actual cooperative RIIC C engine against virtual registers/GPIO.

No Python reimplementation of the state machine. This is software proof, not
electrical/timing proof. CC must be a native host compiler.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from test_i2c_stop_state import function

PORT = Path(__file__).resolve().parents[1]


def main():
    source = (PORT / 'ra/ra_i2c.c').read_text(encoding='utf-8')
    binding = (PORT / 'machine_i2c.c').read_text(encoding='utf-8')
    signatures = (
        'static bool ra_i2c_dtc_quiesce_one(',
        'static bool ra_i2c_dtc_quiesce(',
        'bool ra_i2c_recovery_begin(',
        'static bool ra_i2c_recovery_finish(',
        'bool ra_i2c_recovery_buffer_safe(',
        'int ra_i2c_recovery_result(',
        'bool ra_i2c_recovery_step(',
        'bool ra_i2c_action_is_busy(',
        'ra_i2c_async_status_t ra_i2c_action_poll_async(',
        'void ra_i2c_xaction_start(',
        'bool ra_i2c_action_start_async(',
        'void ra_i2c_action_cancel_async(',
        'void iic_master_rxi_isr(', 'void iic_master_txi_isr(',
        'void iic_master_tei_isr(', 'void iic_master_eri_isr(',
    )
    # Select definitions, not the forward declaration of quiesce_one.
    implementation = source[source.index('static bool ra_i2c_dtc_quiesce_one(',
                                        source.index('void ra_i2c_deinit(')):]
    extracted = '\n'.join(function(implementation, s) if 'dtc_quiesce' in s
                          else function(source, s) for s in signatures)
    for signature in signatures:
        code = function(implementation if 'dtc_quiesce' in signature else source, signature)
        code = re.sub(r'//[^\n]*|/\*.*?\*/', '', code, flags=re.S)
        assert not re.search(r'\b(for|while)\s*\(', code), signature
        assert not re.search(r'\b(mp_hal_delay|malloc|m_new)', code), signature
    assert 'R_DTC_Reset(' not in source and 'R_DTC_Reconfigure(' not in source
    assert 'while' not in function(source, 'static bool ra_i2c_init_idle(')
    for signature in ('static bool machine_i2c_async_update_impl(',
                      'static mp_obj_t machine_i2c_async_cancel_obj_fun('):
        # update has a forward declaration; find its definition after errno map.
        body = function(binding[binding.index('static int machine_i2c_async_error_to_errno('):], signature)
        assert 'ra_i2c_action_cancel_async(' not in body
        assert 'ra_i2c_recovery_step(' not in body
    program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
typedef struct {
    unsigned ICIER;
    struct { unsigned IICRST; } ICCR1_b;
    struct { unsigned WAIT, ACKWP, ACKBT; } ICMR3_b;
    struct { unsigned START; } ICSR2_b;
    struct { unsigned RS, BBSY, ST; } ICCR2_b;
} R_IIC0_Type;
#include "ra_i2c.h"
#include "ra_i2c_recovery.h"
typedef struct { bool open; struct {unsigned irq;} ctrl; } ra_i2c_master_dtc_t;
static struct { struct { unsigned DTCE, IELS; } IELSR_b[4]; } icu;
static struct { unsigned DTCSTS; } dtc_regs;
#define R_ICU (&icu)
#define R_DTC (&dtc_regs)
#define __DSB() ((void)0)
#define GPIO_MODE_OUTPUT_OD 2
#define GPIO_NOPULL 0
#define GPIO_LOW_POWER 0
static ra_i2c_master_dtc_t master_rx_dtc, master_tx_dtc;
static ra_i2c_recovery_t recovery;
static bool bus_fault[3], last_stop;
static uint8_t current_channel;
static xaction_t *current_xaction;
static xaction_unit_t *current_xaction_unit;
static uint32_t now, low_at, high_at;
static int output[2], configured[2], pulses, stops, release_after, stretch_at;
static bool scl_stuck, sda_stuck, init_fail;
static int irq_disabled, closes, restores, notifications;
static jmp_buf reset_target;
static int resets;
static void NVIC_SystemReset(void) { ++resets; longjmp(reset_target, 1); }
static uint32_t mp_hal_ticks_us(void) { return now; }
static uint32_t R_IIC0_Type_to_ch(R_IIC0_Type *inst) { (void)inst; return 1; }
bool ra_i2c_bus_faulted(R_IIC0_Type *inst) { (void)inst; return bus_fault[1]; }
void ra_i2c_irq_disable(R_IIC0_Type *inst) { (void)inst; irq_disabled = 1; }
void ra_i2c_irq_enable(R_IIC0_Type *inst) { (void)inst; irq_disabled = 0; }
void ra_i2c_clear_IR(R_IIC0_Type *inst) { (void)inst; }
static void ra_i2c_master_rx_dtc_close(void) { ++closes; master_rx_dtc.open = false; }
static void ra_i2c_master_tx_dtc_close(void) { ++closes; master_tx_dtc.open = false; }
void ra_i2c_deinit(R_IIC0_Type *inst) {
    (void)inst; assert(!dtc_regs.DTCSTS);
    ra_i2c_master_rx_dtc_close(); ra_i2c_master_tx_dtc_close();
}
static bool ra_i2c_init_idle(R_IIC0_Type *inst, uint32_t scl, uint32_t sda, uint32_t freq) {
    (void)inst; (void)scl; (void)sda; (void)freq; ++restores; return !init_fail;
}
static bool ra_i2c_master_rx_dtc_prepare(R_IIC0_Type *inst, xaction_t *action) {
    (void)inst; (void)action; return true;
}
static bool ra_i2c_master_tx_dtc_prepare(R_IIC0_Type *inst, xaction_t *action) {
    (void)inst; (void)action; return true;
}
static void ra_i2c_xaction_notify_terminal(xaction_t *action) {
    if (!action->m_completion_notified && action->m_status == RA_I2C_STATUS_Stopped) {
        action->m_completion_notified = true; ++notifications;
    }
}
typedef unsigned IRQn_Type;
static R_IIC0_Type inst;
static uint8_t irq_to_ch[] = {0, 1, 2};
volatile uint8_t iic_slave_mode[3];
static unsigned irq, irq_calls, slave_calls, irq_clears;
static IRQn_Type R_FSP_CurrentIrqGet(void) { return irq; }
static void R_BSP_IrqStatusClear(IRQn_Type n) { (void)n; ++irq_clears; }
static R_IIC0_Type *ch_to_R_IIC0_Type(unsigned ch) { (void)ch; return &inst; }
static void ra_i2c_icrxi_isr(R_IIC0_Type *i) { (void)i; ++irq_calls; }
static void ra_i2c_ictxi_isr(R_IIC0_Type *i) { (void)i; ++irq_calls; }
static void ra_i2c_ictei_isr(R_IIC0_Type *i) { (void)i; ++irq_calls; }
static void ra_i2c_iceri_isr(R_IIC0_Type *i) { (void)i; ++irq_calls; }
static void iic_slave_rxi_isr(void) { ++slave_calls; }
static void iic_slave_txi_isr(void) { ++slave_calls; }
static void iic_slave_tei_isr(void) { ++slave_calls; }
static void iic_slave_eri_isr(void) { ++slave_calls; }
static void ra_gpio_write(uint32_t pin, uint32_t value) {
    assert(pin < 2 && value <= 1);
    if (pin == 0 && configured[0] && output[0] != (int)value) {
        if (value) {
            assert(now - low_at >= 1000); ++pulses; high_at = now;
        } else {
            if (pulses) assert(now - high_at >= 1000);
            low_at = now;
        }
    }
    if (pin == 1 && configured[1] && !output[1] && value && output[0]) ++stops;
    output[pin] = value;
}
static void ra_gpio_config(uint32_t pin, uint32_t mode, uint32_t pull,
                          uint32_t power, uint32_t alt) {
    assert(output[pin] == 1 && mode == GPIO_MODE_OUTPUT_OD);
    assert(!pull && !power && !alt); configured[pin] = 1;
}
static int ra_gpio_read(uint32_t pin) {
    if (!output[pin]) return 0;
    if (!pin) return !scl_stuck && !(pulses == stretch_at && now - high_at < 2500);
    if (sda_stuck) return 0;
    // An early high data bit must not terminate an already started recovery.
    return pulses >= release_after || (pulses > 0 && pulses % 2);
}
FUNCTIONS
static R_IIC0_Type inst;
static xaction_t action;
static void setup(uint32_t start) {
    memset(&recovery, 0, sizeof(recovery)); memset(&action, 0, sizeof(action));
    memset(&inst, 0, sizeof(inst)); memset(bus_fault, 0, sizeof(bus_fault));
    action.m_status = RA_I2C_STATUS_Started; current_xaction = &action;
    current_xaction_unit = NULL; last_stop = false; now = start;
    output[0] = output[1] = 1; configured[0] = configured[1] = 0;
    scl_stuck = sda_stuck = init_fail = false;
    pulses = stops = restores = closes = irq_disabled = notifications = 0;
    release_after = 9; stretch_at = -1;
    master_rx_dtc.open = master_tx_dtc.open = true;
    master_rx_dtc.ctrl.irq = 1; master_tx_dtc.ctrl.irq = 2;
    icu.IELSR_b[1].IELS = 77; icu.IELSR_b[2].IELS = 88;
    icu.IELSR_b[1].DTCE = icu.IELSR_b[2].DTCE = 1; dtc_regs.DTCSTS = 0;
    assert(ra_i2c_recovery_begin(&inst, &action, 0, 1, 400000, now));
    assert(irq_disabled && !icu.IELSR_b[1].DTCE && !icu.IELSR_b[2].DTCE);
    assert(!ra_i2c_recovery_begin(&inst, &action, 0, 1, 400000, now));
}
static void run(uint32_t delta) {
    unsigned calls = 0;
    while (!ra_i2c_recovery_step(now)) {
        assert(++calls < 2000);
        ra_i2c_recovery_state_t phase = recovery.state;
        assert(!ra_i2c_recovery_begin(&inst, &action, 0, 1, 400000, now));
        assert(recovery.state == phase); // Cancel/recovery request in every phase.
        assert(ra_i2c_action_is_busy());
        assert(ra_i2c_action_poll_async(&action) == RA_I2C_ASYNC_PENDING);
        xaction_t foreign = {0};
        assert(!ra_i2c_action_start_async(&inst, &foreign, false));
        assert(current_xaction == &action);
        now += delta;
    }
    assert(action.m_status == RA_I2C_STATUS_Stopped);
    assert(ra_i2c_action_poll_async(&action) == RA_I2C_ASYNC_ERROR);
    if (recovery.buffer_safe) {
        assert(!ra_i2c_action_is_busy()); assert(notifications == 1);
        ra_i2c_action_poll_async(&action); assert(notifications == 1);
    } else {
        assert(ra_i2c_action_is_busy()); assert(!notifications);
    }
}
int main(void) {
    int cases = 0;
    for (int release = 0; release <= 9; ++release) {
        for (int cadence = 0; cadence < 3; ++cadence) {
            setup(cadence == 2 ? UINT32_MAX - 500 : 17);
            release_after = release;
            run(cadence == 0 ? 137 : (cadence == 1 ? 1000 : 1300));
            assert(recovery.result == 1 && !bus_fault[1] && restores == 1);
            assert(recovery.pulses == 9U);
            assert(pulses == 10 && stops == 1); // 9 clocks + STOP's SCL release.
            ++cases;
        }
    }
    setup(0); scl_stuck = true; run(1000); assert(recovery.result == -1); ++cases;
    setup(0); sda_stuck = true; run(1000); assert(recovery.result == -2); ++cases;
    setup(0); stretch_at = 3; run(1000); assert(recovery.result == 1); ++cases;
    setup(0); init_fail = true; run(1000); assert(recovery.result == -4); ++cases;
    setup(0); ra_i2c_recovery_step(now); now = 100000; run(1000);
    assert(recovery.result == -3 && bus_fault[1]); ++cases;
    setup(0); dtc_regs.DTCSTS = 0x8000 | 77; run(1000);
    assert(recovery.result == -5 && !ra_i2c_recovery_buffer_safe());
    assert(master_rx_dtc.open && master_tx_dtc.open && !closes); ++cases;
    setup(0); dtc_regs.DTCSTS = 0x8000 | 77;
    assert(!ra_i2c_recovery_step(now)); now += 1000; dtc_regs.DTCSTS = 0;
    run(1000); assert(recovery.result == 1); ++cases;
    setup(0); dtc_regs.DTCSTS = 0x8000 | 99; // Another peripheral is not ours.
    assert(ra_i2c_dtc_quiesce()); dtc_regs.DTCSTS = 0; run(1000); ++cases;
    current_xaction = NULL; recovery.state = REC_IDLE; bus_fault[1] = false;
    inst.ICCR2_b.BBSY = 1; last_stop = true;
    assert(ra_i2c_action_start_async(&inst, &action, false));
    assert(action.m_error == RA_I2C_ERROR_BUSY); ++cases;
    void (*handlers[])(void) = {iic_master_rxi_isr, iic_master_txi_isr,
        iic_master_tei_isr, iic_master_eri_isr};
    for (unsigned h = 0; h < 4; ++h) {
        current_channel = 1; current_xaction = &action; irq_calls = 0;
        recovery.state = REC_IDLE; irq = 0; handlers[h](); assert(!irq_calls);
        irq = 1; recovery.state = REC_LOW; handlers[h](); assert(!irq_calls);
        recovery.state = REC_DONE; handlers[h](); assert(!irq_calls);
        recovery.state = REC_IDLE; current_xaction = NULL; handlers[h](); assert(!irq_calls);
        current_xaction = &action; handlers[h](); assert(irq_calls == 1);
        cases += 5;
    }
    setup(0); ra_i2c_action_cancel_async(&inst, &action);
    assert(!current_xaction && !master_rx_dtc.open && !master_tx_dtc.open);
    assert(recovery.state == REC_IDLE && notifications == 1 && !irq_disabled); ++cases;
    setup(0); xaction_t foreign = {0}; ra_i2c_action_cancel_async(&inst, &foreign);
    assert(current_xaction == &action && !closes && !notifications); ++cases;
    setup(0); dtc_regs.DTCSTS = 0x8000 | 77; resets = 0;
    if (!setjmp(reset_target)) {
        ra_i2c_action_cancel_async(&inst, &action); assert(false);
    }
    assert(resets == 1 && current_xaction == &action && master_rx_dtc.open && !closes); ++cases;
    printf("PASS actual C: %d recovery/ownership/deadline/wrap/BBSY cases\n", cases);
}
'''.replace('FUNCTIONS', extracted)
    compiler = shlex.split(os.environ.get('CC', 'gcc'))
    with tempfile.TemporaryDirectory(prefix='riic-cooperative-test-') as directory:
        path = Path(directory)
        (path / 'test.c').write_text(program, encoding='utf-8')
        subprocess.run(compiler + ['-std=c99', '-Wall', '-Wextra', '-Werror',
                                  '-I', str(PORT / 'ra'), str(path / 'test.c'),
                                  '-o', str(path / 'test.exe')], check=True)
        subprocess.run([str(path / 'test.exe')], check=True)
    print('PASS source contract: no async hardware wait loop or FSP Reset')


if __name__ == '__main__':
    main()
