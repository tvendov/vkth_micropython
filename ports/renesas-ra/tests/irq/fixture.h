#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>

typedef unsigned int uint;
typedef uintptr_t mp_uint_t;
typedef void *mp_obj_t;
typedef struct { mp_obj_t func, arg; } mp_sched_item_t;
typedef struct mp_sched_node { struct mp_sched_node *next; void (*callback)(struct mp_sched_node *); } mp_sched_node_t;
typedef void (*mp_sched_callback_t)(mp_sched_node_t *);
typedef struct { int pin, name; } machine_pin_obj_t;
typedef struct { int type, n_args; void *fun_data; } mp_obj_fun_asm_t;
typedef void (*ICU_CB)(void *);
typedef struct nlr_buf { jmp_buf env; void *ret_val; struct nlr_buf *prev; } nlr_buf_t;
static nlr_buf_t *nlr_top;
#define nlr_push(n) (((n)->prev=nlr_top), nlr_top=(n), setjmp((n)->env))
static void nlr_pop(void) { nlr_top=nlr_top->prev; }
static void throw_error(void) { nlr_top->ret_val=(void *)1; longjmp(nlr_top->env,1); }

#define MICROPY_SCHEDULER_DEPTH 8
#define MICROPY_SCHEDULER_STATIC_NODES 1
#define MICROPY_PY_THREAD 0
#define MICROPY_EMIT_INLINE_THUMB 1
#define MICROPY_WRAP_MP_SCHED_SCHEDULE(x) x
#define MICROPY_SCHED_HOOK_SCHEDULED ((void)0)
#define MICROPY_BEGIN_ATOMIC_SECTION() 0
#define MICROPY_END_ATOMIC_SECTION(x) ((void)(x))
#define MP_STATIC_ASSERT(x) _Static_assert(x, #x)
#define MP_SCHED_IDLE 0
#define MP_SCHED_PENDING 1
#define MP_SCHED_LOCKED -1
#define MP_STATE_VM(x) vm.x
#define MP_STATE_THREAD(x) vm.x
#define MP_STATE_PORT(x) vm.x
#define MP_OBJ_NULL NULL
#define mp_const_none NULL
#define EXTI_RTC_WAKEUP 16
#define PYB_EXTI_NUM_VECTORS 17
#define EXTI_NUM_VECTORS 17
#define IDX_MASK(i) ((i)&7)
#define mp_sched_num_pending() vm.sched_len
#define mp_sched_full() (mp_sched_num_pending()==8)
#define MP_OBJ_FROM_PTR(p) ((void *)(p))
#define MP_OBJ_TO_PTR(p) ((void *)(p))
#define MP_OBJ_NEW_SMALL_INT(x) ((void *)((uintptr_t)(x)*2+1))
#define mp_obj_is_small_int(x) (((uintptr_t)(x)&1)!=0)
#define mp_obj_is_int(x) mp_obj_is_small_int(x)
#define mp_type_fun_asm 1
#define mp_type_ValueError 2
#define mp_type_OSError 3
#define mp_obj_is_type(x,t) (((mp_obj_fun_asm_t *)(x))->type==*(t))
#define MICROPY_MAKE_POINTER_CALLABLE(x) (x)
#define MP_ERROR_TEXT(x) (x)
#define MP_HAL_PIN_PULL_NONE 0
#define MP_HAL_PIN_PULL_UP 1
#define MP_HAL_PIN_TRIGGER_LOWLEVEL 4
#define MP_HAL_PIN_TRIGGER_FALLING 2
#define MP_HAL_PIN_TRIGGER_RISING 1
#define IRQ_PRI_EXTINT 12
#undef mp_type_fun_asm
#undef mp_type_ValueError
#undef mp_type_OSError
static const int mp_type_fun_asm=1, mp_type_ValueError=2, mp_type_OSError=3;
static int mp_plat_print;
static struct {
    int sched_state; unsigned sched_len, sched_idx;
    mp_sched_item_t sched_queue[8]; mp_sched_node_t *sched_head, *sched_tail;
    mp_obj_t mp_pending_exception, pyb_extint_callback[17];
} vm;
static unsigned calls, fast_calls, printed, gc_depth, isr_calls, disabled, last_fast;
static bool in_isr, throw_callback, enabled[16];
static mp_obj_t last_function, last_arg;
static const machine_pin_obj_t pin_a={3,3}, pin_b={3,30};
static int handler_a, handler_b;
static void gc_lock(void) { ++gc_depth; }
static void gc_unlock(void) { assert(gc_depth); --gc_depth; }
static mp_obj_t mp_call_function_1(mp_obj_t f,mp_obj_t arg) {
    ++calls; last_function=f; last_arg=arg;
    if(in_isr) { ++isr_calls; assert(gc_depth==1 && vm.sched_state<0); }
    else { assert(gc_depth==0); }
    if(throw_callback) throw_error();
    return NULL;
}
static void mp_obj_print_exception(void *p,mp_obj_t obj) { (void)p;(void)obj;++printed; }
static void mp_call_function_1_protected(mp_obj_t f,mp_obj_t arg) {
    nlr_buf_t nlr;
    if(nlr_push(&nlr)==0) { mp_call_function_1(f,arg); nlr_pop(); }
    else { nlr_top=nlr.prev; ++printed; }
}
static void mp_raise_TypeError(const char *s) { (void)s; abort(); }
static void mp_raise_msg_varg(const int *t,const char *s,...) { (void)t;(void)s;abort(); }
static const machine_pin_obj_t *machine_pin_find(mp_obj_t o) { return o; }
bool ra_icu_find_irq_no(uint32_t pin,uint8_t *line);
static void ra_icu_disable_irq_no(uint n) { assert(n<16);enabled[n]=false;++disabled; }
static void ra_icu_enable_irq_no(uint n) { assert(n<16);enabled[n]=true; }
static void ra_icu_priority_irq_no(uint8_t n,uint32_t p) { (void)n;(void)p; }
static void ra_icu_set_callback(uint8_t n,ICU_CB cb,void *p) { (void)n;(void)cb;(void)p; }
static void ra_icu_set_pin(uint n,bool a,bool b) { (void)n;(void)a;(void)b; }
static void ra_icu_enable_pin(uint n) { enabled[n%16]=true; }
static void ra_icu_trigger_irq_no(uint8_t n,uint c) { (void)n;(void)c; }
static void ra_icu_init(void) {}
static void ra_icu_deinit(void) { memset(enabled,0,sizeof(enabled)); }
static mp_uint_t disable_irq(void) { return 0; }
static void enable_irq(mp_uint_t state) { (void)state; }
void extint_enable(uint line);
void extint_disable(uint line);
void extint_trigger_mode(uint line,uint32_t mode);
void extint_deinit(void);
