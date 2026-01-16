"""Compatibility shim for `import bluetooth` on EK_RA4W1.

This port provides native BLE via the `renesas_ble` module (FSP BLE stack).
Some user code (and tools like Thonny) expect a `bluetooth` module to exist.

This module implements a small subset of MicroPython's `bluetooth` API by
wrapping `renesas_ble`. It is *not* a full ubluetooth/NimBLE implementation.
"""

try:
    from micropython import const
except ImportError:  # pragma: no cover (host tools)
    def const(x):
        return x

try:
    import renesas_ble as _rb
except ImportError as _e:  # pragma: no cover
    raise ImportError("BLE not supported on this build") from _e


# Common flag constants used by examples.
FLAG_READ = const(0x0002)
FLAG_WRITE = const(0x0008)
FLAG_NOTIFY = const(0x0010)
FLAG_INDICATE = const(0x0020)


# IRQ event constants (subset).
_IRQ_CENTRAL_CONNECT = const(1)
_IRQ_CENTRAL_DISCONNECT = const(2)
_IRQ_GATTS_WRITE = const(3)
_IRQ_GATTS_NOTIFY_COMPLETE = const(4)
_IRQ_GATTS_INDICATE_COMPLETE = const(5)


class UUID:
    """Minimal stand-in for bluetooth.UUID."""

    def __init__(self, value):
        self._value = value

    def __repr__(self):
        return "UUID(%r)" % (self._value,)


class BLE:
    """Partial compatibility wrapper around `renesas_ble`."""

    def __init__(self):
        self._active = False
        self._irq_handler = None
        self._gap_name = "mpy"

    def active(self, value=None):
        if value is None:
            return self._active
        self._active = bool(value)
        _rb.active(self._active)
        if self._active and self._irq_handler is not None:
            self._register_irqs()
        return None

    def irq(self, handler):
        self._irq_handler = handler
        if self._active and handler is not None:
            self._register_irqs()
        return None

    def config(self, **kwargs):
        # Support: ble.config(gap_name='MyName')
        if "gap_name" in kwargs:
            self._gap_name = str(kwargs["gap_name"])
        # Return dict-like behavior is not implemented.
        return None

    def gap_advertise(self, interval_us, adv_data=None, resp_data=None, connectable=True):
        # We can't accept raw ADV/RESP payloads with the current renesas_ble API.
        # Provide a best-effort implementation based on gap_name.
        if interval_us is None:
            return _rb.stop_advertise()
        interval_ms = int(interval_us // 1000)
        if interval_ms <= 0:
            interval_ms = 20
        return _rb.advertise(self._gap_name, interval_ms)

    def gatts_notify(self, conn_handle, value_handle, data):
        return _rb.notify(conn_handle, value_handle, data)

    def gatts_indicate(self, conn_handle, value_handle, data):
        return _rb.indicate(conn_handle, value_handle, data)

    def gap_disconnect(self, conn_handle):
        return _rb.disconnect(conn_handle)

    def _register_irqs(self):
        # Map native string events to ubluetooth-style IRQ calls.
        def _call(event, data_tuple):
            h = self._irq_handler
            if h is not None:
                h(event, data_tuple)

        _rb.on("connect", lambda conn, attr, data: _call(_IRQ_CENTRAL_CONNECT, (conn, None, None)))
        _rb.on("disconnect", lambda conn, attr, data: _call(_IRQ_CENTRAL_DISCONNECT, (conn, None, None)))
        _rb.on("write", lambda conn, attr, data: _call(_IRQ_GATTS_WRITE, (conn, attr)))
        _rb.on("notify_complete", lambda conn, attr, data: _call(_IRQ_GATTS_NOTIFY_COMPLETE, (conn, attr, 0)))
        _rb.on("indicate_complete", lambda conn, attr, data: _call(_IRQ_GATTS_INDICATE_COMPLETE, (conn, attr, 0)))
