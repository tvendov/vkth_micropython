# Минимален JSON-line логер за LoRa тестовете.
# Всеки запис е една линия: {"id":..., "lib":..., "result":..., "data":{...}}

import sys
import time


def _to_json(obj):
    # MicroPython няма пълен json модул в minimal builds; реализираме примитив.
    try:
        import json
        return json.dumps(obj)
    except Exception:
        return str(obj)


class TestLog:
    def __init__(self, test_id, lib):
        self.test_id = test_id
        self.lib = lib
        self.t0 = time.ticks_ms()
        self.fails = 0
        print("TEST_ID:", test_id, "LIB:", lib)

    def event(self, kind, **data):
        rec = {
            "id":   self.test_id,
            "lib":  self.lib,
            "ts":   time.ticks_diff(time.ticks_ms(), self.t0),
            "kind": kind,
        }
        if data:
            rec["data"] = data
        print("LOG", _to_json(rec))

    def check(self, name, condition, **data):
        ok = bool(condition)
        if not ok:
            self.fails += 1
        self.event("check", name=name, ok=ok, **data)
        return ok

    def fail(self, msg, **data):
        self.fails += 1
        self.event("fail", msg=msg, **data)

    def info(self, msg, **data):
        self.event("info", msg=msg, **data)

    def done(self):
        ok = self.fails == 0
        self.event("done", pass_=ok, fails=self.fails)
        print("RESULT:", "PASS" if ok else "FAIL")
        print("EXIT_CODE:", 0 if ok else 1)
        return 0 if ok else 1


def select_lib():
    """Питаме потребителя кой драйвер да ползваме (REPL вход)."""
    sys.stdout.write("Drvier (A=lora-sx126x, B=micropySX126X) [A]: ")
    try:
        line = sys.stdin.readline().strip().upper()
    except Exception:
        line = ""
    if line not in ("A", "B"):
        line = "A"
    print("Selected lib:", line)
    return line
