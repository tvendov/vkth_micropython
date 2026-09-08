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
    "hw_tune",
    "_set_trx_fault",
    "_queue_rx_cleanup",
    "_retry_rx_cleanup",
    "_tx_guard_error",
    "_deinit_saved_rx_dacs",
    "_release_tx_checked",
    "_restore_rx_after_tx",
    "_prepare_tx_file_frame",
    "_service_to_tx",
    "_start_tx_owner",
    "_request_tx_config",
    "_service_tx_reconfigure",
    "_service_to_rx",
    "_service_trx_pending",
    "_poll_tx_status",
    "_service_tx_file",
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
        "TARGETS": ast.literal_eval(next(n.value for n in TREE.body
            if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and
                t.id == 'TARGETS' for t in n.targets))),
        "MS_OUT_MIN": 500_000,
        "MS_OUT_MAX": 200_000_000,
        "F_MIN": 100_000,
        "F_MAX": 40_000_000,
        "TEAL": 0x33A68C,
        "CYAN_RX": 0x00A7B5,
        "PANEL2": 0x202020,
        "WHITE": 0xFFFFFF,
        "DARK_TXT": 0x101010,
        "GREEN": 0x66AA22,
        "GRAY2": 0x777777,
        "BORDER": 0x444444,
        "lv": types.SimpleNamespace(color_hex=lambda value: value, refr_now=lambda disp: None),
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
        self.items = {"rx-dot": Styled(), "rx-button": Button(), "brand-dot": Styled(),
                      "scope-view": Label()}

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
    app.p = {"m": mode, "f": 3_500_000, "s": 1000, "act": 0, "rt": [1, 1, 1]}
    app._lo_hz = app.p['f'] + {'AM': 3000, 'FM': 6000}.get(mode, 0)
    app._clock_failures = []

    class Synth:
        def probe(self):
            return True

        def set_freq(self, clk, hz):
            log.append(('clock', clk, hz))
            if app._clock_failures:
                return app._clock_failures.pop(0)
            return True

    app._synth = Synth()
    def refresh(disp):
        assert disp is None
        assert app._trx_state == TRX_TO_TX
        assert app.ui.get('rx-button').label.text == 'WAIT'
        assert app._tx is not None and not app._tx.running
        log.append('frame_before_start')
    Harness.hw_tune.__globals__['lv'].refr_now = refresh
    app.ui = Ui()
    app.paint_status = lambda status: None
    app._sync_tx_controls = lambda: None  # exercised by test_sdr_fm_controls.py
    app._refresh_home_context = lambda: None  # exercised by test_sdr_tx_home.py
    app.update_mode = lambda: None
    app.update_step = lambda: None
    app.update_freq = lambda: None
    app._paint_fm_status = lambda status: None
    app._refresh_tx_settings = lambda status=None: None
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
    app._inj_source = 0
    app._tx_file_on = False
    app._tx_file_label = None
    app._tx_source_settings = None
    app._tx_rx_snapshot = None
    app._tx_reconfigure = None
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
    assert "self._trx_state not in (TRX_RX, TRX_RX_OFF, TRX_TX)" in wire
    assert "self._trx_pending is not None" in wire
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
    app, log = new_app()
    app._inj_on = True
    app.update_rx()
    assert app.ui.get("rx-button").label.text == "TST"
    assert not log
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
    assert log == ["stop_rx", "Q", "I", ('clock', 1, 3_500_000), "construct",
                   "release", ('clock', 1, 14_012_000), "start_rx"]
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
                expected = {'mode': getattr(self, mode)}
                if mode == 'FM':
                    expected.update(deviation_hz=2500, mic_gain=100, amplitude=819)
                assert kwargs == expected
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
            assert log == ['stop_rx', 'Q', 'I', ('clock', 1, 3_500_000),
                           'construct_' + mode, 'tx_start', 'tx_stop', 'tx_deinit',
                           'release', ('clock', 1, app._lo_hz * 4), 'start_rx']
        finally:
            if previous is None:
                del sys.modules['machine']
            else:
                sys.modules['machine'] = previous


def check_ssb_stall_guard():
    for mode in ('USB', 'LSB', 'FM'):
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


def check_clock_switch_and_failures():
    # NCO-selected station and RX low IF must not leak into the TX carrier.
    for mode in TX_MODES:
        for route, clk, mult in ((5, 0, 4), (1, 1, 4), (7, 2, 4),
                                 (0, 0, 1), (6, 1, 1), (2, 2, 1)):
            app, log = new_app(mode)
            app.p['rt'][0] = route
            app._lo_hz += 1700  # selected RF may differ from the old RX window
            saved_lo = app._lo_hz
            assert app.hw_tune(app.p['f'], tx=True)
            assert app._lo_hz == saved_lo
            assert app.hw_tune(app._lo_hz)
            assert log == [('clock', clk, 3_500_000),
                           ('clock', clk, saved_lo * mult)]

    # Unsupported external-chip routes / too-low x1 clock: do not disturb RX.
    for route, hz in ((3, 3_500_000), (4, 3_500_000), (1, 499_999)):
        app, log = new_app()
        app.p['rt'][0], app.p['f'] = route, hz
        assert not app._service_to_tx()
        assert not log and app.be.running and app._trx_state == TRX_RX

    # Clock refusal before TX construction rolls RX back, with x4 FIRST.
    app, log = new_app()
    app._clock_failures = [False, True]
    assert app._service_to_tx()
    assert app._trx_state == TRX_RX and app.be.running
    assert 'TX Si5351' in app._trx_error
    assert log == ['stop_rx', 'Q', 'I', ('clock', 1, 3_500_000),
                   ('clock', 1, 14_012_000), 'start_rx']

    # If RX clock restore also fails, neither direction may start.
    app, log = new_app()
    app._clock_failures = [False, False]
    assert not app._service_to_tx()
    assert app._trx_state == TRX_FAULT and not app.be.running
    assert app._tx is None and 'start_rx' not in log
    assert 'RX Si5351' in app._trx_error

    app, log = new_app('USB')
    app.be.running = False
    app.be.iq = None
    app._clock_failures = [False]
    assert not app._service_to_rx()
    assert log == [('clock', 1, 14_000_000)]
    assert app._trx_state == TRX_FAULT and not app.be.running


def check_file_source_and_af():
    old_machine = sys.modules.get("machine")
    for profile, source_mode in (("R:AM", "AM"), ("R:USB", "USB"), ("R:LSB", "LSB")):
        app, log = new_app(source_mode)

        class Tx:
            AM, FM, CW, USB, LSB = TX_MODES
            running = False
            queued = 128

            def __init__(self, **kwargs):
                assert kwargs['file_mode'] == source_mode
                assert kwargs['file_tune'] == 1250
                log.append('construct_file_tx')

            def status(self):
                return dict(owned=True, running=self.running, quiesced=not self.running,
                            error=0, outputs_enabled=True, keyed=False, i_code=2048, q_code=2048)

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
                log.append('tx_release')

            def file_service(self):
                log.append('decode_file')
                return self.queued

        class File:
            _attached, _feeding, error, eof = True, True, None, False
            _file = object()
            path, sample_rate = '/flash/iqbank/source.sdriq', 48000
            requested = 1

            def stop(self):
                self._attached, self._feeding, self._file = False, False, None
                log.append('file_stop')

            def start(self, tx, point, rate, loop, path):
                assert isinstance(tx, Tx) and not tx.running
                assert (point, rate, loop, path) == (0, 48000, True, self.path)
                self._attached, self._feeding = True, True
                log.append('file_prefill')

            def _refill(self, index):
                log.append(('refill', index))

            def poll(self):
                return [1, self.requested] + [0] * 13

        sys.modules['machine'] = types.SimpleNamespace(IQTX=Tx)
        app._iq_file = File()
        app._inj_source = app._inj_on = 1
        app._inj_point = 0
        app._iq_file_profiles = ((profile, source_mode, '', ''),)
        app._iq_file_preset = 0
        app._iq_file_loop = True
        app.be.fine_hz = 1250
        app._tester_before_nco = lambda: True
        assert app._tx_guard_error() is None
        app._trx_state = TRX_TO_TX
        assert app._service_to_tx() and app._trx_state == TRX_TX
        assert log.index('file_stop') < log.index('stop_rx')
        assert log.index('file_prefill') < log.index('tx_start')
        assert log.index('file_prefill') < log.index('frame_before_start') < log.index('tx_start')
        assert app.ui.get('scope-view').text == 'AF ' + profile
        # Hidden/off visuals must not gate the source feeder.
        app._scope_view, app._spec_view, app._modal = 2, 2, True
        app._service_tx_file()
        assert log[-3:] == [('refill', 0), ('refill', 1), 'decode_file']
        app._iq_file.eof, app._iq_file.requested = True, 0
        app._service_tx_file()
        assert app._trx_state == TRX_TX  # queued AF must drain
        app._tx.queued = 0
        app._service_tx_file()
        assert app._trx_pending == TRX_RX and app._trx_state == TRX_TO_RX
        assert app._service_to_rx()
        assert not app._tx_file_on and app._iq_file._file is None
        assert log.index('tx_stop') < log.index('tx_deinit')
    if old_machine is None:
        sys.modules.pop('machine', None)
    else:
        sys.modules['machine'] = old_machine
    print('PASS R:AM/R:USB/R:LSB -> FILE TX, AF label, hidden/OFF feed, EOF drain, teardown')


def main():
    check_source_contract()
    check_button_and_labels()
    check_guards_before_teardown()
    check_dac_order()
    check_constructor_rollback()
    check_runtime_fault_and_bounded_cleanup()
    check_all_modes_roundtrip()
    check_ssb_stall_guard()
    check_clock_switch_and_failures()
    check_file_source_and_af()
    print("PASS SDR TX switch: source contract, guards, ownership, rollback, labels, Si5351 RX/TX clocks")
    print("Host-only mocks/AST; no LVGL runtime, target registers, analog output or RF verified.")


if __name__ == "__main__":
    main()
