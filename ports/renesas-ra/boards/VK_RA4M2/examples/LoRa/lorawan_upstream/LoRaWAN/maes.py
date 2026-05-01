# maes.py — fast cryptolib-backed replacement for original pure-Python AES.
#
# Original GereZoltan/LoRaWAN ползва ~400-line pure Python AES (~10 ms/block).
# Този shim използва вградения `cryptolib` (axTLS backend, AES-128 ECB ~62 µs/block).
# 160× по-бързо. NIST AES-128 test vectors verified.
#
# Запазена е PEP-272 style API която LoRaWAN кодът очаква:
#   new(key, MODE_ECB) -> cipher
#   cipher.encrypt(plaintext_16) -> ciphertext_16
#   cipher.decrypt(ciphertext_16) -> plaintext_16

import cryptolib
from array import array            # AES_CMAC.py разчита на това чрез "from .maes import *"

# Mode constants (PEP-272)
MODE_ECB = 1
MODE_CBC = 2
MODE_CTR = 6

block_size = 16
key_size = None


class _Cipher:
    """Minimal PEP-272 wrapper.

    cryptolib не позволява една инстанция да encrypt-ва и decrypt-ва, затова
    държим две lazy-init-вани инстанции зад една обвивка.
    """

    def __init__(self, key, mode, iv=None):
        self._key = bytes(key)
        self._mode = mode
        self._iv = bytes(iv) if iv else None
        self._enc = None
        self._dec = None

    def _new_aes(self):
        if self._iv is not None:
            return cryptolib.aes(self._key, self._mode, self._iv)
        return cryptolib.aes(self._key, self._mode)

    def encrypt(self, data):
        if self._enc is None:
            self._enc = self._new_aes()
        return self._enc.encrypt(bytes(data))

    def decrypt(self, data):
        if self._dec is None:
            self._dec = self._new_aes()
        return self._dec.decrypt(bytes(data))


def new(key, mode, IV=None):
    """PEP-272 factory."""
    return _Cipher(key, mode, IV)


class AES:
    """Compat wrapper за случаи където кодът директно ползва AES."""

    block_size = 16

    def __init__(self, key):
        self._cipher = _Cipher(key, MODE_ECB)

    def encrypt_block(self, block):
        return self._cipher.encrypt(block)

    def decrypt_block(self, block):
        return self._cipher.decrypt(block)
