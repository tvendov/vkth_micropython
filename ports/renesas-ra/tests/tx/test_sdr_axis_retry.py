"""Execute production axis-publish methods with fault-injected native/LVGL doubles.

No board access. A synthetic MemoryError tests recovery; it does not identify the
cause of the earlier hardware timeout.
"""
import ast
import types
import unittest

import test_sdr_tx_switch as switch


METHODS = {'_poll_axis_generation', '_clear_axis_pending'}


class AxisRetryTests(unittest.TestCase):
    def make_app(self):
        app, _ = switch.new_app()
        original = next(n for n in switch.TREE.body
                        if isinstance(n, ast.ClassDef) and n.name == 'SdrApp')
        methods = [n for n in original.body
                   if isinstance(n, ast.FunctionDef) and n.name in METHODS]
        self.assertEqual({n.name for n in methods}, METHODS)
        self.events = []
        self.state = 17
        self.committed = 0
        self.read_error = None
        self.render_error = None
        self.commit_on_render = True
        self.armed = 0
        self.reject = False

        def read():
            if self.read_error is not None:
                error, self.read_error = self.read_error, None
                raise error
            return self.state

        def publish(token=None):
            if token is None:
                return self.committed
            self.events.append(('publish', token))
            if token < 0:
                self.armed = 0
                return True
            if self.reject:
                return False
            self.armed = token
            return True

        def render(dd):
            if self.commit_on_render:
                self.committed = self.armed
            if self.render_error is not None:
                raise self.render_error

        env = {'lv': types.SimpleNamespace(refr_now=render)}
        tree = ast.Module(body=[ast.ClassDef(name='Axis', bases=[], keywords=[],
                          body=methods, decorator_list=[])], type_ignores=[])
        exec(compile(ast.fix_missing_locations(tree), str(switch.UI_SOURCE), 'exec'), env)
        for name in METHODS:
            setattr(app, name, types.MethodType(getattr(env['Axis'], name), app))
        app._spectrum_generation_api = True
        app._spectrum_center_fn = read
        app._spectrum_publish_fn = publish
        app._axis_pending_token = 17
        app._axis_pending_hz = 574500
        app._axis_pending_station_hz = 577500
        app._axis_pending_vfo = None
        app._axis_pending_mode = None
        app._dd = object()
        app._axis_publish_snapshot = lambda: ('old',)
        app._restore_axis_publish = lambda snap: self.events.append(('restore', snap))
        app._finish_axis_publish = lambda *args: self.events.append(('labels', args))
        app._request_spectrum_generation = lambda hz: self.events.append(('request', hz)) or 18
        return app

    def test_transient_read_failure_retries_without_releasing_tx_guard(self):
        app = self.make_app()
        self.read_error = MemoryError('native token boxing')
        self.assertFalse(app._poll_axis_generation())
        self.assertEqual(app._axis_pending_token, 17)
        self.assertIn('MemoryError', app.be.err)
        self.assertEqual(app._tx_guard_error(), 'wait for receiver tuning before TX')
        self.assertTrue(app._spectrum_generation_api, 'a read failure must not remove API capability')
        self.assertEqual(self.events, [])
        self.assertTrue(app._poll_axis_generation())
        self.assertEqual(app._axis_pending_token, 0)
        self.assertIsNone(app._tx_guard_error())
        self.assertEqual(self.committed, 17)

    def test_repeated_read_failures_do_not_publish_or_allow_tx(self):
        app = self.make_app()
        for _ in range(60):
            self.read_error = MemoryError('retry')
            self.assertFalse(app._poll_axis_generation())
            self.assertEqual(app._axis_pending_token, 17)
            self.assertIsNotNone(app._tx_guard_error())
        self.assertEqual(self.events, [])
        self.assertTrue(app._poll_axis_generation())

    def test_inflight_or_foreign_token_never_publishes(self):
        app = self.make_app()
        for self.state in (-17, -18, 18):
            self.assertFalse(app._poll_axis_generation())
            self.assertEqual(app._axis_pending_token, 17)
            self.assertIsNotNone(app._tx_guard_error())
        self.assertEqual(self.events, [])

    def test_cleared_native_request_rearms_same_physical_axis(self):
        app = self.make_app()
        self.state = 0
        self.assertFalse(app._poll_axis_generation())
        self.assertEqual(app._axis_pending_token, 18)
        self.assertEqual(self.events, [('request', 574500)])

    def test_rejected_publish_does_not_change_labels(self):
        app = self.make_app()
        self.reject = True
        self.assertFalse(app._poll_axis_generation())
        self.assertEqual(self.events, [('publish', 17)])
        self.assertEqual(app._axis_pending_token, 17)

    def test_render_failure_aborts_and_restores_before_retry(self):
        app = self.make_app()
        self.commit_on_render = False
        self.render_error = MemoryError('render')
        self.assertFalse(app._poll_axis_generation())
        self.assertEqual(self.events[-2:], [('publish', -17), ('restore', ('old',))])
        self.assertEqual(app._axis_pending_token, 17)
        self.render_error = None
        self.commit_on_render = True
        self.assertTrue(app._poll_axis_generation())

    def test_committed_render_survives_late_binding_error(self):
        app = self.make_app()
        self.render_error = MemoryError('after commit')
        self.assertTrue(app._poll_axis_generation())
        self.assertEqual(app._axis_pending_token, 0)
        self.assertFalse(any(event[0] == 'restore' for event in self.events))


if __name__ == '__main__':
    unittest.main()
