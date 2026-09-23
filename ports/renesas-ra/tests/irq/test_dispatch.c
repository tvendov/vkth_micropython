static void edge(uint line) {
    assert(enabled[line]); in_isr=true; extint_callback(&line); in_isr=false;
    assert(gc_depth==0);
}
static void drain(void) {
    while(vm.sched_len) mp_sched_run_pending();
    assert(vm.sched_state==MP_SCHED_IDLE);
}
static mp_uint_t fast_handler(mp_uint_t line) {
    assert(in_isr && !gc_depth); ++fast_calls;last_fast=line;return 0;
}
static void reset(void) {
    memset(&vm,0,sizeof(vm));calls=fast_calls=printed=isr_calls=disabled=0;
    throw_callback=false;nlr_top=NULL;extint_init0();
}
int main(void) {
    reset();
    extint_register_pin(&pin_a,1,false,false,&handler_a);
    edge(3);assert(calls==0 && vm.sched_len==1);
    drain();assert(calls==1 && isr_calls==0 && last_arg==&pin_a);
    puts("PASS soft: no ISR Python call, original Pin argument, deferred execution");

    reset();extint_register_pin(&pin_a,1,true,false,&handler_a);
    edge(3);assert(calls==1 && isr_calls==1 && vm.sched_len==0);
    throw_callback=true;edge(3);assert(printed==1 && !enabled[3] && vm.pyb_extint_callback[3]==NULL);
    nlr_top=NULL;throw_callback=false;
    extint_register_pin(&pin_a,1,true,false,&handler_b);edge(3);
    assert(calls==3 && last_function==&handler_b);
    puts("PASS hard: direct call, locks balanced, exception disables, re-register works");

    reset();mp_obj_fun_asm_t fast={1,1,(void *)fast_handler};
    extint_register_pin(&pin_a,1,false,true,&fast);edge(3);
    assert(fast_calls==1 && last_fast==3 && calls==0 && vm.sched_len==0);
    extint_register_pin(&pin_a,1,false,false,&handler_a);edge(3);drain();
    assert(calls==1 && fast_calls==1);
    puts("PASS fast: takes precedence with hard=False, IRQ-number argument, cleared on re-register");

    reset();extint_register((mp_obj_t)&pin_a,1,0,&handler_a,false);edge(3);
    assert(isr_calls==1 && last_arg==MP_OBJ_NEW_SMALL_INT(3));
    puts("PASS legacy ExtInt remains hard with integer argument");

    reset();extint_register_pin(&pin_a,1,false,false,&handler_a);
    for(unsigned i=0;i<9;++i)edge(3);
    assert(calls==0 && vm.sched_len==8 && enabled[3]);drain();assert(calls==8);
    edge(3);drain();assert(calls==9 && isr_calls==0);
    puts("PASS actual core scheduler: queue full drops newest, no fallback, recovers");

    reset();extint_register_pin(&pin_a,1,false,false,&handler_a);edge(3);
    extint_register_pin(&pin_a,1,false,false,NULL);assert(!enabled[3]);
    extint_register_pin(&pin_b,1,false,false,&handler_b);
    drain();assert(calls==1 && last_function==&handler_a && last_arg==&pin_a);
    edge(3);drain();assert(calls==2 && last_function==&handler_b && last_arg==&pin_b);
    puts("PASS unregister/re-register: queued old callback retains old function/argument");

    reset();extint_register_pin(&pin_a,1,false,false,&handler_a);throw_callback=true;
    edge(3);drain();assert(printed==1 && enabled[3] && vm.pyb_extint_callback[3]==&handler_a);
    throw_callback=false;edge(3);drain();assert(calls==2);
    puts("PASS soft exception: protected execution, source remains enabled");

    reset();vm.pyb_extint_callback[16]=&handler_a;
    pyb_extint_callback_arg[16]=MP_OBJ_NEW_SMALL_INT(16);pyb_extint_hard_irq[16]=false;
    uint rtc=16;in_isr=true;extint_callback(&rtc);in_isr=false;
    assert(calls==1 && isr_calls==1 && !vm.sched_len);
    puts("PASS RTC slot: unchanged direct dispatch even with unset hard flag");

    #ifdef HAVE_EXTINT_DEINIT
    reset();extint_register_pin(&pin_a,1,false,true,&fast);
    vm.pyb_extint_callback[16]=&handler_a;extint_deinit();
    assert(!enabled[3] && !vm.pyb_extint_callback[3] && !pyb_extint_fast_entry[3]);
    assert(vm.pyb_extint_callback[16]==&handler_a);
    extint_init0();assert(!vm.pyb_extint_callback[16] && !pyb_extint_fast_irq[3]);
    puts("PASS cleanup: GPIO disabled/cleared, RTC unchanged by GPIO teardown, init resets slots");
    #else
    assert(!"GPIO teardown is missing");
    #endif
    puts("PASS all RA IRQ software-contract scenarios");
    return 0;
}
