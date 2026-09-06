"""Host-only contract tests for the SDR HOME RX/TX ownership switch.

This extracts only the state-machine methods from sdr_single.py, supplies mock
RX/DAC/IQTX objects, and never imports LVGL or touches target hardware.
"""

from __future__ import annotations

import ast
import gc
from pathlib import Path
import sys
import types


UI_SOURCE = (Path(__file__).resolve().parents[2] / "boards" / "VK_RA6M3" /
             "examples" / "sdr_single.py")
SOURCE = UI_SOURCE.read_text(encoding="utf-8")
TREE = ast.parse(SOURCE, filename=str(UI_SOURCE))

TRX_RX_OFF = "RX_OFF"
TRX_RX = "RX"
TRX_TO_TX = "TO_TX"
TRX_TX = "TX"
TRX_TO_RX = "TO_RX"
TRX_FAULT = "FAULT"
TRX_CLEANUP_RETRY_LIMIT = 3
TX_MODES = ("AM", "FM", "CW", "USB", "LSB")

METHOD_NAMES = {
    "_set_trx_fault",
    "_queue_rx_cleanup",
    "_retry_rx_cleanup",
    "_tx_guard_error",
    "_deinit_saved_rx_dacs",
    "_release_tx_checked",
    "_restore_rx_after_tx",
    "_service_to_tx",
    "_service_to_rx",
    "_service_trx_pending",
    "_poll_tx_status",
    "toggle_rx",
    "update_rx",
}


def build_harness_class():
    original = next(node for node in TREE.body
                    if isinstance(node, ast.ClassDef) and node.name == "SdrApp")
    methods = [node for node in original.body
               if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
               and node.name in METHOD_NAMES]
    found = {node.name for node in methods}
    assert found == METHOD_NAMES, ("missing state-machine methods", METHOD_NAMES - found)
    harness = ast.ClassDef(
        name="SdrAppHarness", bases=[], keywords=[], body=methods,
        decorator_list=[])
    module = ast.fix_missing_locations(ast.Module(body=[harness], type_ignores=[]))
    namespace = {
        "gc": gc,
        "TRX_RX_OFF": TRX_RX_OFF,
        "TRX_RX": TRX_RX,
        "TRX_TO_TX": TRX_TO_TX,
        "TRX_TX": TRX_TX,
        "TRX_TO_RX": TRX_TO_RX,
        "TRX_FAULT": TRX_FAULT,
        "TRX_CLEANUP_RETRY_LIMIT": TRX_CLEANUP_RETRY_LIMIT,
        "TX_MODES": TX_MODES,
        "CYAN_RX": 0x00A7B5,
        "PANEL2": 0x202020,
        "WHITE": 0xFFFFFF,
        "DARK_TXT": 0x101010,
        "GREEN": 0x66AA22,
        "GRAY2": 0x777777,
        "BORDER": 0x444444,
        "lv": types.SimpleNamespace(color_hex=lambda value: value),
    }
    exec(compile(module, str(UI_SOURCE), "exec"), namespace)
    return namespace["SdrAppHarness"]


Harness = build_harness_class()


class Styled:
    def __init__(self):
        self.bg = None
        self.border = None
        self.color = None

    def set_style_bg_color(self, value, selector):
        self.bg = value

    def set_style_border_color(self, value, selector):
        self.border = value

    def set_style_text_color(self, value, selector):
        self.color = value


class Label(Styled):
    def __init__(self):
        super().__init__()
        self.text = None

    def set_text(self, value):
        self.text = value


class Button(Styled):
    def __init__(self):
        super().__init__()
        self.label = Label()

    def get_child(self, index):
        assert index == 0
        return self.label


class Ui:
    def __init__(self):
        self.items = {"rx-dot": Styled(), "rx-button": Button()}

    def get(self, name):
        return self.items[name]


class Dac:
    def __init__(self, name, log):
        self.name = name
        self.log = log

    def deinit(self):
        self.log.append(self.name)


def new_app(mode="AM", log=None):
    if log is None:
        log = []
    app = Harness.__new__(Harness)
    app.p = {"m": mode}
    app.ui = Ui()
    app.paint_status = lambda status: None
    app._trx_state = TRX_RX
    app._trx_pending = None
    app._trx_error = None
    app._trx_cleanup_failures = 0
    app._tx = None
    app._tx_class = None
    app._tx_mode = None
    app._tx_last_samples = None
    app._tx_keyed = False
    app._trx_dac = None
    app._trx_dac_q = None
    app._inj_on = False
    app._tester_arm_pending = False
    app._inj_prev_scope = None
    app._iq_file = types.SimpleNamespace(_attached=False, _feeding=False, _file=None)
    app._hw_pending = False
    app._hw_config_pending = False
    app._lo_pending_hz = None
    app._station_pending_hz = None
    app._axis_pending_token = 0
    be = types.SimpleNamespace(
        running=True,
        iq=object(),
        dac=Dac("I", log),
        dac_q=Dac("Q", log),
        err=None,
    )

    def stop_rx():
        log.append("stop_rx")
        be.running = False
        be.iq = None
        be.dac = None
        be.dac_q = None
        return True

    be.stop_rx = stop_rx
    app.be = be
    app.backend_on = lambda: True

    def start_rx():
        log.append("start_rx")
        be.running = True
        return True

    app.start_rx = start_rx
    return app, log


def check_source_contract():
    declared = next(n for n in TREE.body if isinstance(n, ast.Assign)
                    and any(isinstance(t, ast.Name) and t.id == 'TX_MODES' for t in n.targets))
    assert ast.literal_eval(declared.value) == TX_MODES
    assert 'add("rx-button", mk(lambda e: self.toggle_rx()))' in SOURCE
    start = next(node for node in TREE.body
                 if isinstance(node, ast.FunctionDef) and node.name == "start")
    start_text = ast.get_source_segment(SOURCE, start)
    assert "old_app._trx_state == TRX_RX_OFF" in start_text

    cls = next(node for node in TREE.body
               if isinstance(node, ast.ClassDef) and node.name == "SdrApp")
    method_text = {node.name: ast.get_source_segment(SOURCE, node)
                   for node in cls.body if isinstance(node, ast.FunctionDef)}
    assert "self._tx_class is not None" in method_text["start_rx"]
    assert "self._trx_cleanup_failures = 0" not in method_text["_release_tx_checked"]
    assert "self._trx_cleanup_failures = 0" in method_text["_restore_rx_after_tx"]
    for name in ("open_entry", "close_entry", "open_route_menu",
                 "open_step_menu", "open_filter_menu"):
        assert "TRX_RX_OFF" in method_text[name], name
    wire = method_text["_wire"]
    assert "self._trx_state not in (TRX_RX, TRX_RX_OFF)" in wire
    assert "if self._trx_state == TRX_TO_RX:" in wire
    assert "self._retry_rx_cleanup(error)" in wire


def check_button_and_labels():
    app, unused = new_app()
    app.toggle_rx()
    assert (app._trx_state, app._trx_pending) == (TRX_TO_TX, TRX_TX)
    assert app.ui.get("rx-button").label.text == "WAIT"

    app._trx_pending = None
    app._trx_state = TRX_TX
    app.toggle_rx()
    assert (app._trx_state, app._trx_pending) == (TRX_TO_RX, TRX_RX)

    for state, running, expected in (
            (TRX_RX_OFF, False, "RX"),
            (TRX_RX, True, "RX"),
            (TRX_TO_TX, False, "WAIT"),
            (TRX_TX, False, "TX"),
            (TRX_TO_RX, False, "WAIT"),
            (TRX_FAULT, False, "ERR")):
        app._trx_state = state
        app.be.running = running
        app._trx_error = None
        app.update_rx()
        assert app.ui.get("rx-button").label.text == expected, state


def check_guards_before_teardown():
    for mode, attr, value, expected in (
            ("DSB", None, None, "not implemented"),
            ("USB", "_inj_on", True, "TESTER/FILE"),
            ("LSB", "_hw_pending", True, "tuning"),
            ("AM", "_inj_on", True, "TESTER/FILE"),
            ("AM", "_hw_pending", True, "tuning"),
            ("FM", "_lo_pending_hz", 123, "tuning")):
        app, log = new_app(mode)
        if attr is not None:
            setattr(app, attr, value)
        result = app._service_to_tx()
        assert result is False
        assert "stop_rx" not in log, (mode, attr, log)
        assert expected in app._trx_error
        assert app._trx_state == TRX_RX

    app, log = new_app("CW")
    app._iq_file._attached = True
    assert app._service_to_tx() is False
    assert "stop_rx" not in log


def check_dac_order():
    app, log = new_app()
    app._trx_dac_q = Dac("Q", log)
    app._trx_dac = Dac("I", log)
    assert app._deinit_saved_rx_dacs() == (True, None)
    assert log == ["Q", "I"]


def check_constructor_rollback():
    app, log = new_app("AM")

    class OomIQTX:
        AM = 1
        FM = 2
        CW = 0

        def __new__(cls, **kwargs):
            log.append("construct")
            raise MemoryError("mock compact LUT pressure")

        @staticmethod
        def release():
            log.append("release")

    previous = sys.modules.get("machine")
    sys.modules["machine"] = types.SimpleNamespace(IQTX=OomIQTX)
    try:
        assert app._service_to_tx() is True
    finally:
        if previous is None:
            del sys.modules["machine"]
        else:
            sys.modules["machine"] = previous
    assert log == ["stop_rx", "Q", "I", "construct", "release", "start_rx"]
    assert app._trx_state == TRX_RX
    assert app._tx is None and app._tx_class is None
    assert "MemoryError" in app._trx_error


def check_runtime_fault_and_bounded_cleanup():
    app, unused = new_app()

    class DeadTx:
        def status(self):
            return {"owned": True, "running": False, "error": 7,
                    "outputs_enabled": False, "keyed": False}

    app._tx = DeadTx()
    app._tx_class = object
    app._trx_state = TRX_TX
    assert app._poll_tx_status() is False
    assert (app._trx_state, app._trx_pending) == (TRX_TO_RX, TRX_RX)

    app, unused = new_app()

    class ReleaseFails:
        @staticmethod
        def release():
            raise OSError("mock teardown")

    app._tx_class = ReleaseFails
    app._tx = None
    app.be.running = False
    app.be.iq = None
    app._trx_state = TRX_TO_RX
    for attempt in range(1, TRX_CLEANUP_RETRY_LIMIT + 1):
        app._trx_pending = None
        assert app._service_to_rx() is False
        assert app._trx_cleanup_failures == attempt
        if attempt < TRX_CLEANUP_RETRY_LIMIT:
            assert (app._trx_state, app._trx_pending) == (TRX_TO_RX, TRX_RX)
        else:
            assert (app._trx_state, app._trx_pending) == (TRX_FAULT, None)


def check_all_modes_roundtrip():
    for mode in TX_MODES:
        app, log = new_app(mode)

        class FakeIQTX:
            CW, AM, FM, USB, LSB = range(5)

            def __init__(self, **kwargs):
                assert kwargs == {'mode': getattr(self, mode)}
                self.running = False
                self.samples = 0
                log.append('construct_' + mode)

            def status(self):
                if self.running:
                    self.samples += 128
                return {'owned': True, 'running': self.running, 'error': 0,
                        'quiesced': not self.running, 'outputs_enabled': True,
                        'keyed': False, 'i_code': 2048, 'q_code': 2048,
                        'dsp_samples': self.samples}

            def start(self):
                self.running = True
                log.append('tx_start')

            def stop(self):
                self.running = False
                log.append('tx_stop')

            def deinit(self):
                log.append('tx_deinit')

            @staticmethod
            def release():
                log.append('release')

        previous = sys.modules.get('machine')
        sys.modules['machine'] = types.SimpleNamespace(IQTX=FakeIQTX)
        try:
            app.toggle_rx()
            app._service_trx_pending()
            assert app._trx_state == TRX_TX
            assert app.ui.get('rx-button').label.text == 'TX'
            assert app._poll_tx_status()
            app.toggle_rx()
            app._service_trx_pending()
            assert app._trx_state == TRX_RX and app.be.running
            assert app._tx is None and app._tx_class is None
            assert log == ['stop_rx', 'Q', 'I', 'construct_' + mode, 'tx_start',
                           'tx_stop', 'tx_deinit', 'release', 'start_rx']
        finally:
            if previous is None:
                del sys.modules['machine']
            else:
                sys.modules['machine'] = previous


def check_ssb_stall_guard():
    for mode in ('USB', 'LSB'):
        app, log = new_app(mode)
        state = {'owned': True, 'running': True, 'error': 0,
                 'outputs_enabled': True, 'keyed': False, 'dsp_samples': 120}
        app._tx = types.SimpleNamespace(status=lambda: state)
        app._tx_mode = mode
        app._trx_state = TRX_TX
        assert app._poll_tx_status()
        assert not app._poll_tx_status()
        assert app._trx_pending == TRX_RX and 'samples stopped' in app._trx_error
        app._tx_last_samples = 0xffffffff
        state['dsp_samples'] = 0  # uint32 wrap is progress, not a startup stall
        assert app._poll_tx_status()


def main():
    check_source_contract()
    check_button_and_labels()
    check_guards_before_teardown()
    check_dac_order()
    check_constructor_rollback()
    check_runtime_fault_and_bounded_cleanup()
    check_all_modes_roundtrip()
    check_ssb_stall_guard()
    print("PASS SDR TX switch: source contract, guards, ownership, rollback, labels")
    print("Host-only mocks/AST; no LVGL runtime, target registers, analog output or RF verified.")


if __name__ == "__main__":
    main()
