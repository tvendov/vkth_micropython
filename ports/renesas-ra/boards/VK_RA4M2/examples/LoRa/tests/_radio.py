# Унифициран адаптер за двата SX1262 драйвера.
# Тестовите скриптове получават еднакъв API: init / send / recv / close.
#
# Избор на библиотека:
#   from _radio import Radio
#   r = Radio(lib="A")     # lora-sx126x (официална)
#   r = Radio(lib="B")     # ehong-tl/micropySX126X
#
# API:
#   r.init()                     -> None (или хвърля грешка)
#   r.send(bytes)                -> None (blocking)
#   r.recv(timeout_ms=2000)      -> (payload:bytes, rssi:float, snr:float) | None
#   r.close()                    -> None
#   r.lib                        -> "A" или "B" (за лог цели)
#   r.name                       -> име на драйвера

from machine import SPI, SoftSPI, Pin
import time
import _config as cfg


# RF_SW1 (P100) трябва да е HIGH за да enable-нат RF switch на Wio-SX1262.
# Държим reference към Pin обекта, иначе GC може да го освободи.
_rf_sw_pin = None


def _enable_rf_switch():
    global _rf_sw_pin
    _rf_sw_pin = Pin(cfg.PIN_RF_SW, Pin.OUT, value=1)


def _make_spi():
    # VK_RA4M2 firmware има bug в hardware SPI(1) → SoftSPI на същите пинове.
    return SoftSPI(
        baudrate=1_000_000,
        polarity=0, phase=0,
        sck=Pin(cfg.PIN_SCK,  mode=Pin.OUT),
        mosi=Pin(cfg.PIN_MOSI, mode=Pin.OUT),
        miso=Pin(cfg.PIN_MISO, mode=Pin.IN),
    )


class _RadioA:
    name = "lora-sx126x"

    def __init__(self):
        self._modem = None

    def init(self):
        _enable_rf_switch()
        from lora import SX1262
        spi = _make_spi()
        self._modem = SX1262(
            spi=spi,
            cs=Pin(cfg.PIN_CS, Pin.OUT, value=1),
            busy=Pin(cfg.PIN_BUSY, Pin.IN),
            dio1=Pin(cfg.PIN_DIO1, Pin.IN),
            reset=Pin(cfg.PIN_RST, Pin.OUT, value=1),
            dio2_rf_sw=cfg.DIO2_RF_SW,
            dio3_tcxo_millivolts=cfg.TCXO_MV,
            lora_cfg={
                "freq_khz":     cfg.FREQ_KHZ,
                "sf":           cfg.SF,
                "bw":           cfg.BW_STR,
                "coding_rate":  cfg.CR,
                "output_power": cfg.TX_POWER_DBM,
                "preamble_len": cfg.PREAMBLE_LEN,
                "crc_en":       cfg.CRC_EN,
                "syncword":     cfg.SYNC_WORD,
            },
        )

    def send(self, data):
        self._modem.send(data)

    def recv(self, timeout_ms=2000):
        # lora-sx126x: blocking recv с timeout
        rx = self._modem.recv(timeout_ms=timeout_ms)
        if rx is None:
            return None
        # RxPacket има: payload, rssi, snr, valid_crc, ticks_ms
        #
        # ВНИМАНИЕ — UPSTREAM BUG в lora-sx126x:
        #   `RxPacket.snr` връща raw signed int8 от регистър 0x051E (RX
        #   buffer status) без скалиране. Per SX1262 datasheet (§ 13.4.6),
        #   реалното SNR в dB е `raw / 4`.
        #   Установено в T4 parity тест: lib A репортва ~50, lib B (правилно
        #   скалиран) репортва ~12.5 за същия сигнал → 50/4 = 12.5 ✓.
        #   Корекция тук за паритет. TODO: PR към micropython-lib.
        return (bytes(rx), float(rx.rssi), float(rx.snr) / 4.0)

    def close(self):
        try:
            self._modem.sleep()
        except Exception:
            pass


class _RadioB:
    name = "micropySX126X"

    def __init__(self):
        self._modem = None

    def init(self):
        _enable_rf_switch()
        # micropySX126X е патчнат локално (sx126x.py) да използва SoftSPI.
        from sx1262 import SX1262
        self._modem = SX1262(
            spi_bus=cfg.SPI_BUS,
            clk=cfg.PIN_SCK, mosi=cfg.PIN_MOSI, miso=cfg.PIN_MISO,
            cs=cfg.PIN_CS, irq=cfg.PIN_DIO1, rst=cfg.PIN_RST, gpio=cfg.PIN_BUSY,
        )
        err = self._modem.begin(
            freq=cfg.FREQ_MHZ,
            bw=float(cfg.BW_KHZ),
            sf=cfg.SF,
            cr=cfg.CR,
            syncWord=cfg.SYNC_WORD,
            power=cfg.TX_POWER_DBM,
            currentLimit=60.0,
            preambleLength=cfg.PREAMBLE_LEN,
            implicit=False,
            crcOn=cfg.CRC_EN,
            txIq=False,
            rxIq=False,
            tcxoVoltage=cfg.TCXO_MV / 1000.0,   # 1.8 V вместо mV
            useRegulatorLDO=False,
            blocking=True,
        )
        if err != 0:
            raise RuntimeError("micropySX126X.begin err=%d" % err)

    def send(self, data):
        self._modem.send(data)

    def recv(self, timeout_ms=2000):
        msg, err = self._modem.recv(timeout_en=True, timeout_ms=timeout_ms)
        if err != 0 or not msg:
            return None
        rssi = float(self._modem.getRSSI())
        snr = float(self._modem.getSNR())
        return (bytes(msg), rssi, snr)

    def close(self):
        try:
            self._modem.standby()
        except Exception:
            pass


def Radio(lib="A"):
    lib = lib.upper()
    if lib == "A":
        return _RadioA()
    elif lib == "B":
        return _RadioB()
    else:
        raise ValueError("lib трябва да е 'A' или 'B', не %r" % lib)


def now_ms():
    return time.ticks_ms()


def elapsed_ms(t0):
    return time.ticks_diff(time.ticks_ms(), t0)
