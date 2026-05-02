/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// AES-128 CMAC (RFC 4493 / NIST SP 800-38B), backed by axTLS AES_encrypt.
//
// Phase 1 of the LoRaWAN crypto acceleration plan: replaces the pure-Python
// CMAC loop in lorawan_upstream/LoRaWAN/AES_CMAC.py. The bottleneck there is
// Python interpreter overhead per-byte (~250 µs/iter), not the AES math.
// Moving the loop to C drops a typical 28-byte LoRaWAN MIC computation from
// ~1-2 ms to ~50-80 µs (~20-40x).
//
// Phase 2 (SCE9 Compatibility Mode plain-key API,
// HW_SCE_Aes128CmacGenerate*Private) would push it further to ~5-10 µs but
// requires switching the FSP crypto stack and is parked as future work.

#include <string.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/binary.h"

#if MICROPY_SSL_AXTLS

// axTLS AES API — same one used by extmod/modcryptolib.c.
// Declared explicitly here to avoid dragging in <crypto/crypto.h> SSL deps.
typedef enum { AES_MODE_128, AES_MODE_256 } AES_MODE;
typedef struct aes_key_st {
    uint16_t rounds;
    uint16_t key_size;
    uint32_t ks[(14 + 1) * 8];
    uint8_t iv[16];
} AES_CTX;
extern void AES_set_key(AES_CTX *ctx, const uint8_t *key, const uint8_t *iv, AES_MODE mode);
extern void AES_encrypt(const AES_CTX *ctx, uint32_t *data);

#define BLOCK_SIZE  (16)

// Single-block AES-128 ECB encrypt. axTLS internally stores the key schedule
// in big-endian word order (AES_set_key uses ntohl); AES_encrypt operates on
// the data buffer as four big-endian uint32_t words. On a little-endian host
// (Cortex-M33) we therefore byte-swap on the way in and out, matching the
// pattern in extmod/modcryptolib.c (aes_process_ecb_impl).
static void aes_encrypt_block(const AES_CTX *ctx, const uint8_t *in, uint8_t *out) {
    uint32_t buf[4];
    memcpy(buf, in, BLOCK_SIZE);
    for (int i = 0; i < 4; ++i) {
        buf[i] = MP_HTOBE32(buf[i]);
    }
    AES_encrypt(ctx, buf);
    for (int i = 0; i < 4; ++i) {
        buf[i] = MP_BE32TOH(buf[i]);
    }
    memcpy(out, buf, BLOCK_SIZE);
}

// Left-shift a 16-byte big-endian value by 1 bit, conditionally XOR with Rb
// (0x...87). Implements the GF(2^128) doubling step from RFC 4493 §2.3.
static void left_shift_xor_rb(uint8_t *block) {
    uint8_t carry = 0;
    uint8_t msb = block[0] >> 7;
    for (int i = BLOCK_SIZE - 1; i >= 0; --i) {
        uint8_t next_carry = block[i] >> 7;
        block[i] = (uint8_t)((block[i] << 1) | carry);
        carry = next_carry;
    }
    if (msb) {
        block[BLOCK_SIZE - 1] ^= 0x87;
    }
}

// Generate K1 / K2 subkeys per RFC 4493 §2.3.
// L = AES-128(K, 0^128); K1 = L · x; K2 = K1 · x  (in GF(2^128)).
static void cmac_gen_subkeys(const AES_CTX *ctx, uint8_t *k1, uint8_t *k2) {
    uint8_t zero_block[BLOCK_SIZE] = {0};
    aes_encrypt_block(ctx, zero_block, k1);
    left_shift_xor_rb(k1);
    memcpy(k2, k1, BLOCK_SIZE);
    left_shift_xor_rb(k2);
}

static void xor_block(uint8_t *dst, const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        dst[i] = a[i] ^ b[i];
    }
}

// Compute AES-128 CMAC of msg (length msg_len). Output 16 bytes into mac.
// Implements RFC 4493 §2.4 (AES-CMAC algorithm).
static void aes_cmac_compute(const uint8_t *key, const uint8_t *msg,
    size_t msg_len, uint8_t *mac) {
    AES_CTX ctx;
    AES_set_key(&ctx, key, (const uint8_t *)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", AES_MODE_128);

    uint8_t k1[BLOCK_SIZE];
    uint8_t k2[BLOCK_SIZE];
    cmac_gen_subkeys(&ctx, k1, k2);

    // n = number of full or partial blocks. flag=true means msg_len is a
    // non-zero multiple of BLOCK_SIZE (last block is complete).
    size_t n;
    bool flag;
    if (msg_len == 0) {
        n = 1;
        flag = false;
    } else if ((msg_len % BLOCK_SIZE) == 0) {
        n = msg_len / BLOCK_SIZE;
        flag = true;
    } else {
        n = (msg_len / BLOCK_SIZE) + 1;
        flag = false;
    }

    // Build M_last from the trailing block (padded if incomplete) XOR K1 or K2.
    uint8_t m_last[BLOCK_SIZE];
    size_t tail_off = (n - 1) * BLOCK_SIZE;
    if (flag) {
        xor_block(m_last, msg + tail_off, k1);
    } else {
        uint8_t padded[BLOCK_SIZE] = {0};
        size_t tail_len = msg_len - tail_off;
        if (tail_len > 0) {
            memcpy(padded, msg + tail_off, tail_len);
        }
        padded[tail_len] = 0x80;
        xor_block(m_last, padded, k2);
    }

    // X = 0; for i = 0..n-2: X = AES(X XOR M_i)
    uint8_t x[BLOCK_SIZE] = {0};
    uint8_t y[BLOCK_SIZE];
    for (size_t i = 0; i + 1 < n; ++i) {
        xor_block(y, x, msg + i * BLOCK_SIZE);
        aes_encrypt_block(&ctx, y, x);
    }
    // T = AES(X XOR M_last)
    xor_block(y, x, m_last);
    aes_encrypt_block(&ctx, y, mac);
}

// aes_cmac.compute(key, msg) -> bytes(16)
//   key : 16-byte bytes-like
//   msg : bytes-like, any length (including zero)
static mp_obj_t mod_aes_cmac_compute(mp_obj_t key_obj, mp_obj_t msg_obj) {
    mp_buffer_info_t key_buf;
    mp_get_buffer_raise(key_obj, &key_buf, MP_BUFFER_READ);
    if (key_buf.len != BLOCK_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("key must be 16 bytes"));
    }
    mp_buffer_info_t msg_buf;
    mp_get_buffer_raise(msg_obj, &msg_buf, MP_BUFFER_READ);

    vstr_t vstr;
    vstr_init_len(&vstr, BLOCK_SIZE);
    aes_cmac_compute((const uint8_t *)key_buf.buf,
        (const uint8_t *)msg_buf.buf, msg_buf.len,
        (uint8_t *)vstr.buf);
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_aes_cmac_compute_obj, mod_aes_cmac_compute);

static const mp_rom_map_elem_t aes_cmac_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_aes_cmac) },
    { MP_ROM_QSTR(MP_QSTR_compute),  MP_ROM_PTR(&mod_aes_cmac_compute_obj) },
};
static MP_DEFINE_CONST_DICT(aes_cmac_module_globals, aes_cmac_module_globals_table);

const mp_obj_module_t mp_module_aes_cmac = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&aes_cmac_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_aes_cmac, mp_module_aes_cmac);

#endif // MICROPY_SSL_AXTLS
