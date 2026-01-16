# VK_RA4M2 (Renesas RA) - Data Flash persistent byte + LED blink
#
# Behavior on every reset:
# - reads 1 byte from Data Flash offset 0
# - if byte == 0x00: erase block 0 (byte becomes 0xFF) and blink 2 times/sec
# - else (incl 0xFF): write 0x00 and blink 10 times/sec
#
# Notes:
# - Writing 0x00 -> 0xFF requires an erase (flash can't set bits 0->1).
# - Output pin used for blink: P409 (machine.Pin).

import time

import dataflash
from machine import Pin

OFF = 0


def _toggle_delay_ms(blinks_per_sec: int) -> int:
    # "blink" = on+off cycle. We toggle twice per blink.
    return int(1000 // (2 * blinks_per_sec))


def _read_state() -> int:
    b = dataflash.read(OFF, 1)
    return b[0]


def _set_state_to_ff_via_erase():
    # offset 0 is in block 0
    dataflash.erase_block(0)


def _set_state_to_00():
    dataflash.write(OFF, bytes([0x00]))


def main():
    # User request: use P409 via machine.Pin
    led = Pin("P409", Pin.OUT, value=0)
    v = 0

    state = _read_state()

    if state == 0x00:
        # Next state should be 0xFF; must erase to go 0->1.
        _set_state_to_ff_via_erase()
        blinks = 2
    else:
        # Next state should be 0x00; 1->0 program is allowed.
        _set_state_to_00()
        blinks = 10

    d = _toggle_delay_ms(blinks)

    while True:
        v ^= 1
        led.value(v)
        time.sleep_ms(d)


main()

