"""Run manually on an identified RA board with an approved GPIO loopback.

No pins are selected automatically. Connect TX output to the IRQ-capable RX
input, disconnect other devices from these pins, then call run(rx_name, tx_name).
This script does not flash firmware, reset the board or access RTC.
"""
from array import array
import micropython
from machine import Pin
import os
import time
from uctypes import addressof


@micropython.asm_thumb
def ipsr():
    mrs(r0, IPSR)


def run(rx_name, tx_name):
    if rx_name == tx_name:
        raise ValueError("Use two different approved pins with a physical wire")
    print(os.uname())
    micropython.alloc_emergency_exception_buf(256)
    rx = Pin(rx_name, Pin.IN, Pin.PULL_DOWN)
    tx = Pin(tx_name, Pin.OUT, value=0)
    state = array("I", [0, 0, 0])

    def pulse():
        tx.value(1)
        time.sleep_ms(20)
        tx.value(0)
        time.sleep_ms(20)

    def soft(pin):
        assert pin is rx
        state[0] += 1
        state[1] = ipsr()
        state[2] = len(bytearray(64))  # Allocation must work outside the ISR.

    def hard(pin):
        state[0] += 1
        state[1] = ipsr()  # Only preallocated state and small integers here.

    # Match the port's existing fast examples: embed a rooted buffer address
    # into the generated ASM before enabling the interrupt.
    namespace = {"micropython": micropython}
    exec("""
@micropython.asm_thumb
def fast(r0):
    movwt(r1, %d)
    ldr(r2, [r1, 0])
    add(r2, r2, 1)
    str(r2, [r1, 0])
    mrs(r2, IPSR)
    str(r2, [r1, 4])
    str(r0, [r1, 8])
""" % addressof(state), namespace)
    try:
        rx.irq(handler=soft, trigger=Pin.IRQ_RISING)  # Default hard=False.
        pulse()
        assert state[0] == 1 and state[1] == 0 and state[2] == 64
        print("PASS soft: thread context, heap allocation, Pin argument")
        rx.irq(handler=hard, trigger=Pin.IRQ_RISING, hard=True)
        pulse()
        assert state[0] == 2 and state[1] != 0
        print("PASS hard: ISR context")
        rx.irq(handler=namespace["fast"], trigger=Pin.IRQ_RISING, fast=True)
        pulse()
        assert state[0] == 3 and state[1] != 0 and state[2] < 16
        print("PASS fast: ISR context, IRQ number", state[2])
        rx.irq(handler=None)
        pulse()
        assert state[0] == 3
        rx.irq(handler=soft, trigger=Pin.IRQ_RISING, hard=False)
        pulse()
        assert state[0] == 4 and state[1] == 0
        print("PASS unregister/re-register")
    finally:
        rx.irq(handler=None)
        tx.value(0)
        tx.init(Pin.IN)
    print("Soft-reset stress, saturation, exceptions and latency require separate HIL runs")
