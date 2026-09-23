"""Execute the actual RTC driver with vendor register layouts and a clock model.

Requires native gcc and Python. This is not oscillator, timing, or board evidence.
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


def vendor_type(text, name):
    end = text.index("} " + name + ";") + len("} " + name + ";")
    start = text.rfind("typedef struct", 0, end)
    return text[start:end]


def main():
    vendor = (ROOT / "lib/fsp/ra/fsp/src/bsp/cmsis/Device/RENESAS/Include/R7FA4M2AD.h").read_text()
    types = "\n".join(vendor_type(vendor, n) for n in ["R_RTC_RTCCR_Type", "R_RTC_CP_Type", "R_RTC_Type", "R_SYSTEM_Type"])
    masks = "\n".join(l for l in vendor.splitlines() if re.match(r"\s*#define R_RTC_", l))
    driver = (PORT / "ra/ra_rtc.c").read_text()
    driver = re.sub(r'^#include "[^"]+"\s*$', '', driver, flags=re.M)
    header = (PORT / "ra/ra_rtc.h").read_text()
    bsp = (ROOT / 'lib/fsp/ra/fsp/src/bsp/mcu/all/bsp_clocks.c').read_text()
    early = bsp[bsp.index('#if BSP_FEATURE_CGC_HAS_SOSC\n'):]
    early = early[:early.index('#if BSP_FEATURE_CGC_HAS_HOCOWTCR')]
    startup = 'static void startup_clock(void) {\n' + early + '\n}\n' + function(bsp, 'void R_BSP_Init_RTC (')
    binding = (PORT / "machine_rtc.c").read_text()
    binding_functions = '\n'.join(function(binding, signature) for signature in [
        'void rtc_check_available(', 'void rtc_check_standby(',
        'static void machine_rtc_select_source(', 'mp_obj_t machine_rtc_wakeup('])
    prelude = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#define __IOM volatile
#define __IM volatile
#define __OM volatile
TYPES
MASKS
static R_RTC_Type mock_rtc;
static R_SYSTEM_Type mock_system;
#define R_RTC (&mock_rtc)
#define R_SYSTEM (&mock_system)
#define MICROPY_HW_RTC_OPTIONAL_SUBCLOCK 1
#define BSP_CLOCK_CFG_SUBCLOCK_DRIVE 0
#define BSP_FEATURE_CGC_SODRV_SHIFT 0
#define BSP_FEATURE_CGC_SODRV_MASK 1
#define BSP_DELAY_UNITS_MICROSECONDS 1
#define BSP_DELAY_UNITS_MILLISECONDS 1000
#define BSP_CLOCK_CFG_SUBCLOCK_RUNTIME 1
#define BSP_FEATURE_CGC_HAS_SOSC 1
#define BSP_PRV_SUBCLOCK_STOP_INTERVAL_US 1220
#define BSP_CLOCKS_SOURCE_CLOCK_SUBCLOCK 4
#ifndef BSP_CFG_CLOCK_SOURCE
#define BSP_CFG_CLOCK_SOURCE 1
#endif
#ifndef BSP_CFG_RTC_USED
#define BSP_CFG_RTC_USED 1
#endif
#ifndef BSP_PRV_HOCO_USE_FLL
#define BSP_PRV_HOCO_USE_FLL 0
#endif
#define VECTOR_NUMBER_RTC_PERIOD 1
#define VECTOR_NUMBER_RTC_ALARM 2
#define VECTOR_NUMBER_RTC_CARRY 3
#define RTC_PERIOD_IRQn 1
#define RTC_ALARM_IRQn 2
#define RA_PRI_RTC_WKUP 4
typedef int IRQn_Type;
static void R_BSP_IrqCfg(int a, unsigned b, void *c) {(void)a;(void)b;(void)c;}
static void R_BSP_IrqEnable(int a) {(void)a;}
static void R_BSP_IrqDisable(int a) {(void)a;}
static void R_BSP_IrqStatusClear(int a) {(void)a;}
static int R_FSP_CurrentIrqGet(void) {return 1;}
static unsigned elapsed, phase;
static bool crystal, loco, stuck_reset;
static void R_BSP_SoftwareDelay(unsigned count, unsigned unit) {
    unsigned us = count * unit;
    elapsed += us;
    bool clock = mock_rtc.RCR4_b.RCKSEL ? (loco && !mock_system.LOCOCR_b.LCSTP)
        : (crystal && !mock_system.SOSCCR_b.SOSTP);
    if (clock && !stuck_reset && mock_rtc.RCR2_b.RESET) {
        mock_rtc.RCR2_b.RESET = 0;
        mock_rtc.R64CNT = 0;
        phase = 0;
    }
    if (clock && mock_rtc.RCR2_b.START) {
        phase += us;
        while (phase >= 15625) {phase -= 15625; ++mock_rtc.R64CNT;}
    }
}
'''.replace('TYPES', types).replace('MASKS', masks)
    tests = r'''
static int cases;
static void reset_model(void) {
    memset(&mock_rtc,0,sizeof(mock_rtc)); memset(&mock_system,0,sizeof(mock_system));
    elapsed = phase = 0; crystal = false; loco = true; stuck_reset = false;
    rtc_fault = subclock_verified = subclock_shared = false;
    mock_system.SOSCCR_b.SOSTP = 1;
    mock_system.LOCOCR_b.LCSTP = 1;
    mock_system.PRCR = 0xA500;
}
static void set_calendar(void) {
    ra_rtc_t t = {2026,9,23,3,12,34,56};
    assert(ra_rtc_set_time(&t));
}
static void calendar_preserved(void) {
    ra_rtc_t t; assert(ra_rtc_get_time(&t));
    assert(t.year==2026 && t.month==9 && t.date==23 && t.hour==12 && t.minute==34 && t.second==56);
}
int main(void) {
    reset_model(); mock_system.SOSCCR_b.SOSTP=0;
    startup_clock(); R_BSP_Init_RTC();
    assert(mock_system.SOSCCR_b.SOSTP==1 && elapsed==1220); ++cases;
    reset_model(); crystal=true; assert(ra_rtc_init(1)); set_calendar();
    assert(ra_rtc_subclock_start()); assert(ra_rtc_init(0));
    startup_clock(); R_BSP_Init_RTC();
    assert(ra_rtc_source()==0 && mock_rtc.RCR2_b.START && !mock_system.SOSCCR_b.SOSTP);
    calendar_preserved(); ++cases;
    reset_model(); assert(ra_rtc_init(RA_RTC_SOURCE_AUTO));
    assert(ra_rtc_source()==1 && mock_system.SOSCCR_b.SOSTP==1 && !ra_rtc_subclock_ready());
    assert(elapsed<60000); ++cases;
    reset_model(); crystal=true; assert(ra_rtc_init(RA_RTC_SOURCE_AUTO));
    assert(mock_system.SOSCCR_b.SOSTP==1 && ra_rtc_source()==1); ++cases;
    set_calendar(); unsigned before=elapsed;
    assert(ra_rtc_init(RA_RTC_SOURCE_AUTO)); calendar_preserved(); assert(elapsed-before<=50000); ++cases;
    assert(ra_rtc_subclock_start()); assert(!ra_rtc_subclock_ready()); ++cases;
    assert(ra_rtc_init(0)); calendar_preserved(); assert(ra_rtc_subclock_ready() && ra_rtc_source()==0); ++cases;
    subclock_verified=false; subclock_shared=false; // MCU RAM reset, RTC retained.
    assert(ra_rtc_init(RA_RTC_SOURCE_AUTO)); calendar_preserved(); assert(ra_rtc_source()==0 && ra_rtc_subclock_ready()); ++cases;
    assert(ra_rtc_init(1)); calendar_preserved(); assert(ra_rtc_subclock_ready());
    ra_rtc_subclock_abort(); assert(mock_system.SOSCCR_b.SOSTP==0); ++cases;
    reset_model(); assert(ra_rtc_init(RA_RTC_SOURCE_AUTO)); set_calendar();
    assert(ra_rtc_subclock_start()); assert(!ra_rtc_init(0));
    assert(ra_rtc_source()==1 && !ra_rtc_has_error() && !ra_rtc_subclock_ready());
    assert(mock_system.SOSCCR_b.SOSTP==1); calendar_preserved(); ++cases;
    crystal=true; assert(ra_rtc_subclock_start()); assert(ra_rtc_init(0));
    assert(ra_rtc_source()==0); calendar_preserved(); ++cases;
    reset_model(); assert(ra_rtc_init(1)); set_calendar();
    assert(ra_rtc_subclock_start()); ra_rtc_subclock_abort();
    assert(mock_system.SOSCCR_b.SOSTP==1); calendar_preserved(); ++cases;
    reset_model(); stuck_reset=true; assert(!ra_rtc_init(1));
    assert(ra_rtc_has_error() && elapsed<150000); ++cases;
    stuck_reset=false; assert(ra_rtc_init(1)); assert(!ra_rtc_has_error()); ++cases;
    reset_model(); loco=false; assert(!ra_rtc_init(1)); assert(elapsed<150000 && ra_rtc_has_error()); ++cases;
    reset_model(); assert(ra_rtc_init(1)); set_calendar();
    mock_rtc.RCR4_b.RCKSEL=0; mock_system.SOSCCR_b.SOSTP=0;
    assert(ra_rtc_init(RA_RTC_SOURCE_AUTO)); assert(ra_rtc_source()==1); calendar_preserved(); ++cases;
    reset_model(); assert(!ra_rtc_init(3)); ++cases;
    reset_model(); assert(ra_rtc_init(1)); mock_system.PRCR=0xA502;
    assert(ra_rtc_subclock_start()); assert(mock_system.PRCR==0xA502);
    ra_rtc_subclock_abort(); assert(mock_system.PRCR==0xA502); ++cases;
    mock_rtc.RCR1=0; assert(!ra_rtc_irq_active()); mock_rtc.RCR1_b.PIE=1; assert(ra_rtc_irq_active()); ++cases;
    mock_rtc.RCR1=0; ra_rtc_set_period_time(14); assert(mock_rtc.RCR1_b.PIE && !ra_rtc_has_error());
    ra_rtc_period_off(); assert(!mock_rtc.RCR1_b.PIE); ++cases;
    ra_rtc_set_adjustment(3,0); assert(ra_rtc_get_adjustment()==0x43); ++cases;
    ra_rtc_set_adjustment(-2,0); assert(ra_rtc_get_adjustment()==0x82); ++cases;
    printf("PASS actual RTC C: %d register-model scenarios; no hardware claim\n",cases);
    binding_tests();
}
'''
    binding_stubs = r'''
#include <setjmp.h>
#define MP_EBUSY 16
#define MP_EIO 5
#define MP_ETIMEDOUT 110
#define MP_ERROR_TEXT(x) x
#define IRQ_STATE_ENABLED 1
#define BSP_CLOCK_CFG_SUBCLOCK_STABILIZATION_MS 1000
static int mp_type_OSError;
typedef struct _nlr_buf {jmp_buf jump; void *ret_val; struct _nlr_buf *prev;} nlr_buf_t;
static nlr_buf_t *top;
#define nlr_push(b) (((b)->prev=top), (top=(b)), setjmp((b)->jump))
static void nlr_pop(void) {top=top->prev;}
static void nlr_jump(void *value) {nlr_buf_t *b=top; top=b->prev; b->ret_val=value; longjmp(b->jump,1);}
static void mp_raise_OSError(int e) {nlr_jump((void *)(uintptr_t)e);}
static void mp_raise_msg(void *type,const char *message) {(void)type;(void)message;mp_raise_OSError(MP_EBUSY);}
static unsigned isr; static int irqs=1;
static unsigned __get_IPSR(void) {return isr;}
static int query_irq(void) {return irqs;}
static bool rtc_clock_busy, rtc_initialized;
static uint8_t rtc_clock_source;
typedef uintptr_t mp_obj_t;
typedef intptr_t mp_int_t;
#define mp_const_none 0
#define MP_OBJ_NEW_SMALL_INT(x) ((mp_obj_t)(x))
#define EXTI_RTC_WAKEUP 0
static mp_int_t mp_obj_get_int(mp_obj_t value) {return (mp_int_t)value;}
static mp_obj_t callbacks[1], pyb_extint_callback_arg[1];
#define MP_STATE_PORT(name) callbacks
static uint32_t rtc_wakeup_param;
static void extint_callback(void) {}
static int injected, nested_result, standby_result;
static void (*event_hook)(void);
static void mp_hal_delay_ms(unsigned ms) {
    if(event_hook) event_hook();
    if(injected) mp_raise_OSError(injected);
    R_BSP_SoftwareDelay(ms,BSP_DELAY_UNITS_MILLISECONDS);
}
static void rtc_calendar_config(void) {
    ra_rtc_t t={2019,1,1,2,0,0,0}; rtc_initialized=ra_rtc_set_time(&t);
}
BINDING
static int select_catching(unsigned source) {
    nlr_buf_t b;
    if(nlr_push(&b)==0) {machine_rtc_select_source(source);nlr_pop();return 0;}
    return (int)(uintptr_t)b.ret_val;
}
static void nested_event(void) {
    nested_result=select_catching(1);
    nlr_buf_t b;
    if(nlr_push(&b)==0) {rtc_check_standby();nlr_pop();standby_result=0;}
    else standby_result=(int)(uintptr_t)b.ret_val;
}
static void reset_model(void);
static void set_calendar(void);
static void calendar_preserved(void);
static void binding_tests(void) {
    int count=0;
    reset_model(); rtc_clock_busy=false; rtc_initialized=ra_rtc_init(1); set_calendar();
    injected=4; assert(select_catching(0)==4);
    assert(!rtc_clock_busy && mock_system.SOSCCR_b.SOSTP==1 && ra_rtc_source()==1);
    calendar_preserved(); ++count;
    injected=0; event_hook=nested_event; crystal=true;
    assert(select_catching(0)==0); assert(nested_result==MP_EBUSY && standby_result==MP_EBUSY);
    assert(!rtc_clock_busy && rtc_initialized && ra_rtc_source()==0); calendar_preserved(); ++count;
    event_hook=NULL;
    mock_rtc.RCR1_b.PIE=1; assert(select_catching(1)==MP_EBUSY);
    assert(ra_rtc_source()==0); ++count;
    mock_rtc.RCR1_b.PIE=0; assert(select_catching(1)==0); calendar_preserved(); ++count;
    isr=1; assert(select_catching(0)==MP_EBUSY); assert(!rtc_clock_busy); isr=0; ++count;
    irqs=0; assert(select_catching(0)==MP_EBUSY); assert(!rtc_clock_busy); irqs=1; ++count;
    reset_model(); rtc_initialized=ra_rtc_init(1); set_calendar();
    assert(select_catching(0)==MP_ETIMEDOUT); assert(rtc_initialized && !rtc_clock_busy);
    assert(ra_rtc_source()==1 && !ra_rtc_subclock_ready()); calendar_preserved(); ++count;
    crystal=true; assert(select_catching(0)==0); assert(ra_rtc_source()==0); ++count;
    reset_model(); rtc_initialized=false; assert(select_catching(1)==0);
    assert(rtc_initialized && mock_rtc.RMONCNT==1 && mock_rtc.RDAYCNT==1); ++count;
    mp_obj_t args[3]={0,1000,0xBAD};
    machine_rtc_wakeup(2,args); assert(callbacks[0]==mp_const_none && mock_rtc.RCR1_b.PIE); ++count;
    machine_rtc_wakeup(3,args); assert(callbacks[0]==0xBAD && mock_rtc.RCR1_b.PIE); ++count;
    args[1]=mp_const_none; machine_rtc_wakeup(2,args);
    assert(callbacks[0]==mp_const_none && !mock_rtc.RCR1_b.PIE); ++count;
    printf("PASS actual binding C: %d exception/reentry/source scenarios\n",count);
}
'''.replace('BINDING', binding_functions)
    with tempfile.TemporaryDirectory(prefix="ra-rtc-") as tmp:
        source = Path(tmp)/"rtc.c"
        binary = Path(tmp)/"rtc.exe"
        source.write_text(prelude+header+driver+startup+binding_stubs+tests)
        cc = shlex.split(os.environ.get("CC", "gcc"))
        subprocess.run(cc+["-std=c11","-Wall","-Wextra","-Werror",str(source),"-o",str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=10)
        for invalid in ['BSP_CFG_CLOCK_SOURCE=4', 'BSP_CFG_RTC_USED=0', 'BSP_PRV_HOCO_USE_FLL=1']:
            result = subprocess.run(cc+['-std=c11','-D'+invalid,str(source),'-o',str(binary)],capture_output=True,text=True)
            assert result.returncode and 'Optional subclock requires' in result.stderr, invalid
        print('PASS FSP compile guards: 3 invalid clock configurations rejected')
    # Integration contracts, separate from execution of the driver above.
    binding=(PORT/'machine_rtc.c').read_text()
    assert 'mp_hal_delay_ms(BSP_CLOCK_CFG_SUBCLOCK_STABILIZATION_MS)' in binding
    assert 'nlr_jump(nlr.ret_val)' in binding and 'rtc_clock_busy = false;' in binding
    assert 'if (n_args >= 3)' in binding
    assert 'MP_QSTR_source), MP_ROM_PTR(&machine_rtc_source_obj)' in binding
    for name in ['timer.c','ra/ra_timer.c']:
        assert 'ra_rtc_subclock_ready()' in (PORT/name).read_text()
    bsp=(ROOT/'lib/fsp/ra/fsp/src/bsp/mcu/all/bsp_clocks.c').read_text()
    assert bsp.count('defined(BSP_CLOCK_CFG_SUBCLOCK_RUNTIME)')==2
    print('PASS source contracts: cooperative wait, cleanup, source query, AGT and FSP guards')


if __name__ == '__main__':
    main()
