# lorawan/python/lorawan_async.py
#
# Async wrapper for the renesas-ra `lorawan` C-module (Phase 6c).
#
# Usage:
#     import lorawan_async
#     mac = lorawan_async.AsyncMac()        # wraps lorawan.Mac()
#     await mac.lorawan_init()              # starts background process tick
#     mac.set_keys(deveui, joineui, appkey)
#     await mac.join(timeout_ms=20_000)     # MLME_JOIN; resolves on confirm
#     await mac.send(port=2, data=b"hi")    # MCPS UNCONFIRMED; resolves on confirm
#     msg = mac.recv()                       # non-blocking poll
#
# Implementation notes:
#   * The C module's `set_event_callback` delivers a (tag_qstr,
#     status_int) tuple via mp_sched_schedule whenever the LoRaMac
#     stack fires an MCPS / MLME confirm or indication.
#   * AsyncMac registers a single dispatcher there and routes events to
#     per-event asyncio.Event objects, so `await join()` blocks just
#     until the next mlme_confirm.
#   * `LoRaMacProcess()` must run continuously while the stack is
#     active. AsyncMac launches a background tick task at lorawan_init.
#     Period defaults to 50 ms; small enough that the AGT4-armed RX
#     windows never miss their dispatch boundary.

import asyncio
import lorawan


class LoraError(OSError):
    """LoRaMac-side failure. .status holds the LoRaMacEventInfoStatus_t code."""

    def __init__(self, msg, status):
        super().__init__(msg)
        self.status = status


class AsyncMac:
    """Asyncio wrapper around `lorawan.Mac`. One instance per radio."""

    def __init__(self, mac=None, **mac_kwargs):
        self._m = mac if mac is not None else lorawan.Mac(**mac_kwargs)
        # Per-event signalling. set_event_callback delivers (tag, status)
        # tuples; we re-dispatch into these futures + last-status caches.
        self._mlme_evt = asyncio.Event()
        self._mcps_evt = asyncio.Event()
        self._ind_evt = asyncio.Event()  # MCPS indication (downlink)
        self._last_mlme_status = None
        self._last_mcps_status = None
        self._last_mlme_request = None  # MLME_JOIN, MLME_LINK_CHECK, ...
        self._tick_task = None
        self._tick_period_ms = 50
        self._m.set_event_callback(self._on_event)

    # ---- Internal dispatch -------------------------------------------------

    def _on_event(self, evt):
        # Runs in scheduler context (mp_sched_schedule). Keep it short:
        # cache status + signal the matching asyncio.Event so the
        # awaiting coroutine wakes on the next event-loop tick.
        tag, status = evt
        if tag == "mlme_confirm":
            self._last_mlme_status = status
            self._mlme_evt.set()
        elif tag == "mcps_confirm":
            self._last_mcps_status = status
            self._mcps_evt.set()
        elif tag == "mcps_indication":
            self._ind_evt.set()
        # mlme_indication / mac_error fall through — user can hook
        # set_event_callback directly if they need them.

    async def _tick(self):
        # Background loop: pump LoRaMacProcess + radio IRQ deferred work.
        m = self._m
        period = self._tick_period_ms
        while True:
            try:
                m.process()
            except Exception:
                # Process must never throw to async runtime; log + continue.
                pass
            await asyncio.sleep_ms(period)

    # ---- Lifecycle ---------------------------------------------------------

    async def lorawan_init(self):
        """Init LoRaMac stack + start the background tick task."""
        self._m.lorawan_init()
        if self._tick_task is None:
            self._tick_task = asyncio.create_task(self._tick())

    def set_keys(self, deveui, joineui, appkey):
        self._m.set_keys(deveui, joineui, appkey)

    def load_credentials(self):
        """Read DevEUI/JoinEUI/AppKey from Data Flash Block 0 (the
        "LWCR" record written by `provision_credentials.py`).
        Returns (deveui, joineui, appkey) as bytes, or None if the
        record is blank / invalid. Mirrors the legacy
        lorawan_app.py:_load_credentials() fallback chain — caller
        should provide its own .py constants if this returns None."""
        return self._m.load_credentials()

    def set_keys_from_dataflash(self):
        """Convenience: load credentials from Data Flash and apply via
        set_keys(). Returns the (deveui, joineui, appkey) tuple if
        successful, None if Block 0 has no valid LWCR record."""
        creds = self.load_credentials()
        if creds is None:
            return None
        deveui, joineui, appkey = creds
        self._m.set_keys(deveui, joineui, appkey)
        return creds

    def set_rx2(self, freq, dr):
        return self._m.set_rx2(freq, dr)

    def set_class(self, cls):
        return self._m.set_class(cls)

    def get_class(self):
        return self._m.get_class()

    def set_adr(self, enable):
        self._m.set_adr(enable)

    def get_adr(self):
        return self._m.get_adr()

    @property
    def is_joined(self):
        return self._m.is_joined()

    async def deinit(self):
        if self._tick_task is not None:
            self._tick_task.cancel()
            try:
                await self._tick_task
            except asyncio.CancelledError:
                pass
            self._tick_task = None
        self._m.deinit()

    # ---- Join (OTAA) -------------------------------------------------------

    async def join(self, datarate=5, timeout_ms=20_000):
        """OTAA join. Blocks until mlme_confirm or timeout.

        Returns the LoRaMacEventInfoStatus_t code (0 = OK). Raises
        LoraError on stack-level rejection of the request itself.
        """
        # Drain any stale event so we wait only for THIS join's confirm.
        self._mlme_evt.clear()
        self._last_mlme_status = None
        st = self._m.join(datarate)
        if st != 0:
            raise LoraError("join request rejected", st)
        try:
            await asyncio.wait_for_ms(self._mlme_evt.wait(), timeout_ms)
        except asyncio.TimeoutError:
            raise LoraError("join timeout", -1)
        return self._last_mlme_status

    # ---- Uplink ------------------------------------------------------------

    async def send(self, port, data, confirmed=False, datarate=5,
                   timeout_ms=15_000):
        """Send an uplink frame. Blocks until mcps_confirm or timeout.

        Returns the LoRaMacEventInfoStatus_t code (0 = OK).
        """
        self._mcps_evt.clear()
        self._last_mcps_status = None
        st = self._m.send(port, data, confirmed, datarate)
        if st != 0:
            raise LoraError("send request rejected", st)
        try:
            await asyncio.wait_for_ms(self._mcps_evt.wait(), timeout_ms)
        except asyncio.TimeoutError:
            raise LoraError("send timeout", -1)
        return self._last_mcps_status

    # ---- Downlink ----------------------------------------------------------

    def recv(self):
        """Non-blocking pop of the most recent downlink, or None."""
        return self._m.recv()

    async def wait_recv(self, timeout_ms=None):
        """Wait until a downlink frame arrives, then return (port, data).

        If `timeout_ms` is None, waits indefinitely.
        """
        # Fast path: drain any frame already in the C ring.
        msg = self._m.recv()
        if msg is not None:
            return msg
        # Slow path: block until next mcps_indication.
        self._ind_evt.clear()
        if timeout_ms is None:
            await self._ind_evt.wait()
        else:
            try:
                await asyncio.wait_for_ms(self._ind_evt.wait(), timeout_ms)
            except asyncio.TimeoutError:
                return None
        return self._m.recv()

    # ---- NVM passthroughs --------------------------------------------------

    def nvm_store(self):
        return self._m.nvm_store()

    def nvm_restore(self):
        return self._m.nvm_restore()

    def nvm_factory_reset(self):
        return self._m.nvm_factory_reset()
