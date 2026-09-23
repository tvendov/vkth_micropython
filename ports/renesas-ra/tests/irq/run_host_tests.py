"""Compile real RA dispatch/registration and core scheduler bodies with host stubs.

This verifies software contracts, not NVIC timing, GPIO routing or heap on a board.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--baseline", action="store_true", help="Expect the pre-fix HEAD dispatcher to fail")
    args = parser.parse_args()
    port = Path(__file__).resolve().parents[2]
    root = port.parents[1]
    source = (port / "extint.c").read_text()
    if args.baseline:
        source = subprocess.check_output(["git", "show", "HEAD:ports/renesas-ra/extint.c"],
                                         cwd=root, text=True)
    scheduler = (root / "py/scheduler.c").read_text()
    chunks = [Path(__file__).with_name("fixture.h").read_text()]
    for sig in ("static inline bool mp_sched_empty(", "void mp_sched_lock(",
                "void mp_sched_unlock(", "static inline void mp_sched_run_pending(",
                "bool MICROPY_WRAP_MP_SCHED_SCHEDULE(mp_sched_schedule)("):
        chunks.append(function(scheduler, sig))
    start = source.index("static uint8_t pyb_extint_mode[")
    chunks.append(source[start:source.index("static void *extint_get_fast_asm_entry", start)])
    for sig in ("static void *extint_get_fast_asm_entry(", "static void extint_set_fast_state(",
                "void extint_callback(", "uint extint_register(", "void extint_register_pin(",
                "void extint_enable(", "void extint_disable(", "void extint_trigger_mode(",
                "void extint_init0("):
        chunks.append(function(source, sig))
    if "void extint_deinit(" in source:
        chunks.append(function(source, "void extint_deinit("))
        chunks.append("#define HAVE_EXTINT_DEINIT 1")
    chunks.append(Path(__file__).with_name("test_dispatch.c").read_text())
    env = dict(os.environ)
    env["PATH"] = str(Path(args.cc).parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="ra-irq-host-") as directory:
        src = Path(directory) / "test.c"
        binary = Path(directory) / "test.exe"
        src.write_text("\n".join(chunks))
        # Match the port's production optimisation as well as an unoptimised run.
        for optimize in ("-O0", "-Os"):
            subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            optimize, str(src), str(Path(__file__).with_name("icu_stub.c")),
                            "-o", str(binary)], check=True, env=env)
            subprocess.run([str(binary)], check=True, env=env)
    # Check integration points not represented by the host peripheral stubs.
    pin = (port / "machine_pin.c").read_text()
    assert "extint_deinit();" in function(pin, "void machine_pin_deinit(")
    assert "MP_ARG_BOOL, {.u_bool = false}" in pin
    main_c = (port / "main.c").read_text()
    cleanup = main_c[main_c.index("soft_reset_exit:"):]
    assert cleanup.index("machine_pin_deinit();") < cleanup.index("gc_sweep_all();")
    assert "MP_REGISTER_ROOT_POINTER(mp_obj_t pyb_extint_callback[" in source
    assert "MP_REGISTER_ROOT_POINTER(mp_sched_item_t sched_queue[" in scheduler
    runtime = function((root / "py/runtime.c").read_text(), "void mp_init(")
    assert "MP_STATE_VM(sched_len) = 0" in runtime
    assert "MP_STATE_VM(sched_idx) = 0" in runtime
    print("PASS integration: Pin default, rooted callbacks/queue, cleanup before GC, VM queue reset")
    print("NOT RUN: target GPIO/NVIC, real heap/GC, physical soft reset and timing")


if __name__ == "__main__":
    main()
