# AES-128 CMAC (RFC 4493 / NIST SP 800-38B)
#
# Phase 1 acceleration: the original pure-Python implementation looped over
# the AES block primitive in interpreter space, costing ~2 ms for a typical
# LoRaWAN MIC buffer. The native `aes_cmac` module computes the same MAC
# entirely in C using axTLS AES_encrypt — measured ~178 us on VK_RA4M2 (~13x
# faster) and bit-exact against the NIST SP 800-38B test vectors.
#
# The class-with-encode() shape is preserved so callers in PhyPayload and
# DataPayload do not change.

import aes_cmac as _aes_cmac


class AES_CMAC:
    def encode(self, K, M):
        return _aes_cmac.compute(bytes(K), bytes(M))
