from machine import UART
import time


UART_CHANNEL = 9
UART_BAUD = 115200
TX_PIN = "P602"
RX_PIN = "P601"
READ_TIMEOUT_MS = 400


def pass_line(label):
    print("PASS:", label)


def fail(label, detail):
    raise RuntimeError("{}: {}".format(label, detail))


def flush_rx(uart):
    while True:
        available = uart.any()
        if not available:
            return
        uart.read(available)
        time.sleep_ms(10)


def read_exact(uart, expected_len, timeout_ms):
    deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
    data = bytearray()

    while len(data) < expected_len:
        available = uart.any()
        if available:
            chunk = uart.read(available)
            if chunk:
                data.extend(chunk)
                continue
        if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
            break
        time.sleep_ms(10)

    return bytes(data)


def probe_roundtrip(uart, label, payload):
    flush_rx(uart)
    written = uart.write(payload)
    if written != len(payload):
        fail(label, "write returned {} for {} bytes".format(written, len(payload)))

    echoed = read_exact(uart, len(payload), READ_TIMEOUT_MS)
    if echoed != payload:
        fail(label, "expected {!r}, got {!r}".format(payload, echoed))


def run():
    print("UART firmware probe")
    print("Loopback required: connect {} (TX) to {} (RX).".format(TX_PIN, RX_PIN))
    print("Using UART({}) at {} baud.".format(UART_CHANNEL, UART_BAUD))

    uart = UART(UART_CHANNEL, UART_BAUD)

    try:
        print("1/3 text loopback")
        probe_roundtrip(uart, "text loopback", b"VK_RA4M2 UART OK\r\n")
        pass_line("text loopback")

        print("2/3 binary loopback")
        probe_roundtrip(
            uart,
            "binary loopback",
            bytes((1, 2, 3, 4, 0x55, 0xAA, 0x10, 0x20, 0x30, 0x40)),
        )
        pass_line("binary loopback")

        print("3/3 line-style request/response")
        probe_roundtrip(uart, "line loopback", b"AT\r\n")
        pass_line("line-style request/response")

        print("ALL PASS")
    finally:
        try:
            flush_rx(uart)
        except Exception:
            pass


run()
