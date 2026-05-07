"""High-level OTA helper, sits on top of the C `_ota` module.

Usage from the active firmware:

    import ota
    ota.write_bin('/flash/firmware_v2.bin')   # programs staging
    ota.commit()                              # sets flag + reset

The bootloader (separate, lives at 0x00000000) sees the flag at next
reset and copies staging -> active in 1 MB.
"""
import _ota


_FLAG_RESERVE = 128         # first 128 B of staging belong to the swap flag
_CHUNK = 1024               # write chunk size, multiple of 128


def write_bin(path):
    """Erase staging, then stream `path` into it starting at offset 128.

    Raises OSError on flash failure or if the image does not fit.
    """
    if not _ota.erase_staging():
        raise OSError("erase staging failed")
    written = 0
    chunk = bytearray(_CHUNK)
    with open(path, "rb") as f:
        while True:
            n = f.readinto(chunk)
            if n == 0:
                break
            # Pad partial chunks to the FSP write granularity (128 B).
            if n % 128 != 0:
                pad = 128 - (n % 128)
                for i in range(pad):
                    chunk[n + i] = 0xFF
                n += pad
            offset = _FLAG_RESERVE + written
            if offset + n > _ota.STAGING_SIZE:
                raise OSError("image too large for staging slot")
            _ota.write(offset, bytes(chunk[:n]))
            written += n
    print("ota: wrote %d bytes to staging" % written)
    return written


def commit():
    """Set the swap flag and reset.  Does not return."""
    print("ota: committing — bootloader will swap on next reset")
    _ota.commit()              # NoReturn
