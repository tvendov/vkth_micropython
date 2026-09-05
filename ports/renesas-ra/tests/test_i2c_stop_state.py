"""Compile the actual RIIC terminal functions with host-only hardware stubs.

This tests stop/error bookkeeping, not IRQ timing, DTC, or electrical I2C.
Run with Python 3 and a host C compiler (CC or gcc on PATH).
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile


RA = Path(__file__).resolve().parents[1] / "ra"


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor]


def main():
    source = (RA / "ra_i2c.c").read_text(encoding="utf-8")
    header = (RA / "ra_i2c.h").read_text(encoding="utf-8")
    enums = "\n".join(re.findall(r"typedef enum\s*\{[^}]+\}\s*\w+;", header))
    extracted = function(source, "void ra_i2c_xaction_stop(") + "\n" + function(
        source, "ra_i2c_async_status_t ra_i2c_action_poll_async(")
    program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
ENUMS
enum { REC_IDLE, REC_DONE };
static struct { int state; bool buffer_safe; } recovery;
static bool ra_i2c_dtc_quiesce(void) { return true; }
typedef struct {
    bool m_stop;
    xaction_error_t m_error;
    xaction_status_t m_status;
} xaction_t;
static bool last_stop;
static xaction_t *current_xaction;
static void *current_xaction_unit;
static int rx_closes, tx_closes, notifications;
static void ra_i2c_master_rx_dtc_close(void) { ++rx_closes; }
static void ra_i2c_master_tx_dtc_close(void) { ++tx_closes; }
static void ra_i2c_xaction_notify_terminal(xaction_t *action) {
    assert(action == current_xaction);
    ++notifications;
}
FUNCTIONS
int main(void) {
    xaction_error_t errors[] = {RA_I2C_ERROR_OK, RA_I2C_ERROR_NACK,
        RA_I2C_ERROR_TMOF, RA_I2C_ERROR_AL, RA_I2C_ERROR_BUSY};
    int cases = 0;
    for (int stop = 0; stop < 2; ++stop) {
        for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            xaction_t action = {stop, errors[i], RA_I2C_STATUS_Stopped};
            bool expected = stop || errors[i] != RA_I2C_ERROR_OK;
            current_xaction = &action;
            current_xaction_unit = &action;
            last_stop = !expected;
            ra_i2c_xaction_stop();
            assert(last_stop == expected);
            ++cases;
            last_stop = !expected;
            rx_closes = tx_closes = notifications = 0;
            int result = ra_i2c_action_poll_async(&action);
            assert(result == (errors[i] == RA_I2C_ERROR_OK ?
                RA_I2C_ASYNC_COMPLETE : RA_I2C_ASYNC_ERROR));
            assert(last_stop == expected);
            assert(current_xaction == NULL && current_xaction_unit == NULL);
            assert(rx_closes == 1 && tx_closes == 1 && notifications == 1);
            assert(action.m_stop == (bool)stop);
            ++cases;
        }
    }
    xaction_t active = {false, RA_I2C_ERROR_OK, RA_I2C_STATUS_Started};
    current_xaction = &active;
    current_xaction_unit = &active;
    last_stop = false;
    rx_closes = tx_closes = notifications = 0;
    assert(ra_i2c_action_poll_async(&active) == RA_I2C_ASYNC_PENDING);
    assert(current_xaction == &active && current_xaction_unit == &active);
    assert(!last_stop && !rx_closes && !tx_closes && !notifications);
    xaction_t foreign = {true, RA_I2C_ERROR_NACK, RA_I2C_STATUS_Stopped};
    assert(ra_i2c_action_poll_async(&foreign) == RA_I2C_ASYNC_ERROR);
    assert(ra_i2c_action_poll_async(NULL) == RA_I2C_ASYNC_ERROR);
    assert(current_xaction == &active && !last_stop);
    puts("PASS actual C: 20 stop/error cases; pending and foreign-owner guards");
    assert(cases == 20);
    return 0;
}
'''.replace("ENUMS", enums).replace("FUNCTIONS", extracted)
    compiler = shlex.split(os.environ.get("CC", "gcc"))
    with tempfile.TemporaryDirectory(prefix="riic-stop-test-") as directory:
        directory = Path(directory)
        test_source = directory / "test.c"
        executable = directory / ("test.exe" if os.name == "nt" else "test")
        test_source.write_text(program, encoding="utf-8")
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                   str(test_source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
