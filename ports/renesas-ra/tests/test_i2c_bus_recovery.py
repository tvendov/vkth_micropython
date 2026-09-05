"""Compile actual bus-clear C with an open-drain GPIO/clock-stretch model.

This verifies bounded control flow and pin operations, not physical timing.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
from test_i2c_stop_state import function


def main():
    port = Path(__file__).resolve().parents[1]
    source = (port / "ra/ra_i2c.c").read_text(encoding="utf-8")
    binding = (port / "machine_i2c.c").read_text(encoding="utf-8")
    abort = function(binding, "static void machine_i2c_async_abort_and_reinit(")
    assert abort.index("ra_i2c_action_cancel_async") < abort.index("ra_i2c_deinit")
    assert abort.index("ra_i2c_deinit") < abort.index("ra_i2c_recover_bus")
    assert abort.index("ra_i2c_recover_bus") < abort.index("ra_i2c_init")
    extracted = function(source, "static bool ra_i2c_recovery_scl_high(") + "\n"
    extracted += function(source, "void ra_i2c_recover_bus(")
    program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define GPIO_MODE_OUTPUT_OD 2
#define GPIO_NOPULL 0
#define GPIO_LOW_POWER 0
enum {SCL = 0, SDA = 1};
static int output[2], configured[2], pulses, stops, delay_us;
static int release_after, stretch_reads, stretch_left, stuck_at;
static void ra_gpio_write(uint32_t pin, uint32_t value) {
    assert(pin < 2 && value <= 1);
    if (pin == SCL && !output[SCL] && value) {
        ++pulses;
        stretch_left = stretch_reads;
    }
    if (pin == SDA && !output[SDA] && value && output[SCL]) {
        ++stops;
    }
    output[pin] = value;
}
static void ra_gpio_config(uint32_t pin, uint32_t mode, uint32_t pull,
    uint32_t drive, uint32_t alt) {
    assert(pin < 2 && output[pin] == 1);
    assert(mode == GPIO_MODE_OUTPUT_OD && !pull && !drive && !alt);
    configured[pin]++;
}
static uint32_t ra_gpio_read(uint32_t pin) {
    if (!output[pin]) { return 0; }
    if (pin == SCL) {
        if (stuck_at >= 0 && pulses >= stuck_at) { return 0; }
        if (stretch_left > 0) { --stretch_left; return 0; }
        return 1;
    }
    return pulses >= release_after;
}
static void mp_hal_delay_us(uint32_t value) { delay_us += value; }
FUNCTIONS
static void run(int release, int stretch, int stuck) {
    output[0] = output[1] = 1;
    configured[0] = configured[1] = pulses = stops = delay_us = 0;
    release_after = release;
    stretch_left = stretch_reads = stretch;
    stuck_at = stuck;
    ra_i2c_recover_bus(SCL, SDA);
    assert(output[0] == 1 && output[1] == 1);
    assert(configured[0] == 1 && configured[1] == 1);
    assert(pulses <= 10 && delay_us <= 5560);
}
int main(void) {
    run(0, 0, -1); assert(pulses == 0 && stops == 0);
    for (int release = 1; release <= 9; ++release) {
        run(release, 0, -1); assert(pulses == 10 && stops == 1);
        run(release, 99, -1); assert(pulses == 10 && stops == 1);
    }
    run(100, 0, -1); assert(pulses == 10 && stops == 1);
    run(100, 99, -1); assert(pulses == 10 && stops == 1);
    run(1, 0, 0); assert(pulses == 0 && delay_us == 500);
    run(5, 0, 3); assert(pulses == 3);
    run(3, 0, 10); assert(pulses == 10 && output[SDA] == 1);
    puts("PASS actual C: 24 bus-clear cases; open-drain, stretch/deadline and abort ordering");
    return 0;
}
'''.replace("FUNCTIONS", extracted)
    compiler = shlex.split(os.environ.get("CC", "gcc"))
    with tempfile.TemporaryDirectory(prefix="riic-recovery-test-") as directory:
        directory = Path(directory)
        test_source = directory / "test.c"
        executable = directory / ("test.exe" if os.name == "nt" else "test")
        test_source.write_text(program, encoding="utf-8")
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                   str(test_source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
