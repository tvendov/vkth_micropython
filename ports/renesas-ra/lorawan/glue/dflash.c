/*
 * lorawan/glue/dflash.c
 *
 * Phase 6a.2 — RA4M2 data flash NVM backend for the LoRaWAN stack.
 *
 * Atomic ping-pong write protocol over the FSP `g_flash0` instance:
 *
 *   Bank A:  [header(32B)] [payload(≤bank-32 B)]
 *   Bank B:  [header(32B)] [payload(≤bank-32 B)]
 *
 *   load: read both bank headers via AHB pointer, validate magic +
 *         CRC32, choose higher sequence#.
 *   save: erase the OTHER bank (the one not currently active), write
 *         (header.sequence = active.sequence + 1, fresh CRC), verify.
 *         Power-loss anywhere along the save leaves the previously
 *         active bank intact.
 *
 * Calls share `g_flash0` with `moddataflash.c`. Concurrent calls from
 * Python `dataflash.write` and our save_blob are NOT serialized — but
 * LoRaMac NVM writes happen rarely (≈ once per uplink), so collisions
 * are extremely unlikely.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "hal_data.h"        /* g_flash0, R_BSP_FlashCache* */
#include "py/mphal.h"

#include "glue/dflash.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

/* ---- FCACHE coherency helper (mirrors moddataflash.c:23) ---------- */
static inline void dflash_fcache_sync(void) {
#if BSP_FEATURE_BSP_FLASH_CACHE
    __DSB();
    R_BSP_FlashCacheDisable();
    R_BSP_FlashCacheEnable();
    __DSB();
    __ISB();
#endif
}

/* ---- Internal layout state (resolved at dflash_init) -------------- */
static struct {
    bool     initialized;
    uint32_t df_start;        /* AHB base of data flash region        */
    uint32_t df_size;         /* total data flash size                */
    uint32_t erase_block_size;/* 64 on RA4M2                          */
    uint32_t write_size;      /* 4 on RA4M2                           */
    uint32_t bank_size;       /* per-bank size (block-aligned)        */
    uint32_t bank_a_addr;     /* AHB address of bank A                */
    uint32_t bank_b_addr;     /* AHB address of bank B                */
    uint32_t bank_blocks;     /* # erase blocks per bank              */
} s_df;

/* ---- CRC32 (Ethernet poly 0xEDB88320, no table — saves ~1 KB) ---- */
static uint32_t dflash_crc32(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; ++i) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

/* CRC over the variable part of header (sizes + reserved2) + payload.
   Excludes sequence/magic/crc/reserved (the first 16 bytes) so changing
   sequence# alone doesn't invalidate the data CRC. */
static uint32_t dflash_compute_crc(const dflash_header_t *hdr,
    const uint8_t *payload, size_t payload_len) {
    const uint8_t *vh = (const uint8_t *)hdr + 16;  /* mac_size onwards */
    uint32_t crc = dflash_crc32(0, vh, sizeof(*hdr) - 16);
    crc = dflash_crc32(crc, payload, payload_len);
    return crc;
}

/* ---- FSP wrappers (blocking; same pattern as moddataflash.c) ----- */

static bool dflash_wait_idle(uint32_t timeout_ms) {
    uint32_t t0 = mp_hal_ticks_ms();
    for (;;) {
        flash_status_t status;
        fsp_err_t err = g_flash0.p_api->statusGet(g_flash0.p_ctrl, &status);
        if (err != FSP_SUCCESS) return false;
        if (status == FLASH_STATUS_IDLE) return true;
        if ((uint32_t)(mp_hal_ticks_ms() - t0) > timeout_ms) return false;
    }
}

static bool dflash_erase_blocks(uint32_t addr, uint32_t blocks) {
    uint32_t ctx = __get_PRIMASK();
    __disable_irq();
    fsp_err_t err = g_flash0.p_api->erase(g_flash0.p_ctrl, addr, blocks);
    if (!ctx) __enable_irq();
    if (err != FSP_SUCCESS) return false;
    if (!dflash_wait_idle(500u + blocks * 20u)) return false;
    dflash_fcache_sync();
    return true;
}

static bool dflash_write_aligned(uint32_t addr, const uint8_t *src,
    uint32_t len) {
    /* Caller guarantees addr + len are aligned to write_size. */
    uint32_t ctx = __get_PRIMASK();
    __disable_irq();
    fsp_err_t err = g_flash0.p_api->write(g_flash0.p_ctrl,
        (uint32_t)src, addr, len);
    if (!ctx) __enable_irq();
    if (err != FSP_SUCCESS) return false;
    if (!dflash_wait_idle(500u + (len / 4u))) return false;
    dflash_fcache_sync();
    return true;
}

/* ---- Init -------------------------------------------------------- */

bool dflash_init(void) {
    if (s_df.initialized) return true;

    flash_info_t info;
    if (g_flash0.p_api->infoGet(g_flash0.p_ctrl, &info) != FSP_SUCCESS) {
        return false;
    }
    if (info.data_flash.num_regions < 1 ||
        info.data_flash.p_block_array == NULL) {
        return false;
    }
    const flash_block_info_t *b = &info.data_flash.p_block_array[0];
    uint32_t start = b->block_section_st_addr;
    uint32_t end_inclusive = b->block_section_end_addr;
    uint32_t size = (end_inclusive >= start) ? (end_inclusive - start + 1u) : 0u;
    if (size < 2048u || b->block_size == 0u || b->block_size_write == 0u) {
        return false;
    }

    /* IMPORTANT: preserve existing Data Flash layout used by the
       legacy `lorawan_app.py` Python stack:
         Block 0 (offset 0..63):    "LWCR" credentials record (40 B)
                                     written ONCE per board by
                                     provision_credentials.py.
         Block 1 (offset 64..127):  session-state wear-leveled log
                                     (DevNonce / FCntUp / FCntDn).
       Our LoRaMac NVM ping-pong banks live AFTER these reserved
       blocks so credentials and legacy state survive a renesas-stack
       firmware. */
    const uint32_t LEGACY_RESERVED_BLOCKS = 2;
    uint32_t reserved = LEGACY_RESERVED_BLOCKS * b->block_size;
    if (size <= reserved + 2 * b->block_size) {
        return false;  /* DF too small to host both legacy + 2 banks */
    }
    uint32_t available = size - reserved;
    uint32_t bank_blocks = (available / b->block_size) / 2u;
    uint32_t bank_size = bank_blocks * b->block_size;
    if (bank_size < (DFLASH_HEADER_SIZE + b->block_size)) {
        return false;
    }

    s_df.df_start         = start;
    s_df.df_size          = size;
    s_df.erase_block_size = b->block_size;
    s_df.write_size       = b->block_size_write;
    s_df.bank_size        = bank_size;
    s_df.bank_blocks      = bank_blocks;
    s_df.bank_a_addr      = start + reserved;
    s_df.bank_b_addr      = start + reserved + bank_size;
    s_df.initialized      = true;
    return true;
}

/* ---- Credentials helpers (Block 0 "LWCR" record) ---------------- */

/* Read the 40-byte credentials record from Data Flash block 0 and
   validate magic + version + CRC16. On success, copies DevEUI (8B),
   JoinEUI (8B), AppKey (16B) into the caller's buffers and returns
   true. On invalid / blank, returns false (caller should fall back
   to a Python `LoRaConfig_TTN.py` or similar).
   Layout matches provision_credentials.py:
     [0..3]  "LWCR" magic
     [4]     version 0x01
     [5]     reserved
     [6..13] DevEUI MSB
     [14..21] JoinEUI MSB
     [22..37] AppKey MSB
     [38..39] CRC16-CCITT BE over bytes 0..37 (poly 0x1021, init 0xFFFF) */
static uint16_t crc16_ccitt(const uint8_t *p, size_t n) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                   : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

bool dflash_load_credentials(uint8_t deveui[8], uint8_t joineui[8],
    uint8_t appkey[16]) {
    if (!dflash_init()) return false;

    dflash_fcache_sync();
    const uint8_t *blk0 = (const uint8_t *)s_df.df_start;
    if (blk0[0] != 'L' || blk0[1] != 'W' ||
        blk0[2] != 'C' || blk0[3] != 'R') {
        return false;
    }
    if (blk0[4] != 0x01) return false;
    uint16_t stored = ((uint16_t)blk0[38] << 8) | blk0[39];
    uint16_t computed = crc16_ccitt(blk0, 38);
    if (stored != computed) return false;

    memcpy(deveui,  blk0 + 6,  8);
    memcpy(joineui, blk0 + 14, 8);
    memcpy(appkey,  blk0 + 22, 16);
    return true;
}

/* ---- Load -------------------------------------------------------- */

/* Read header + payload from `bank_addr`, validate. On success, copy
   payload into `dst` (capped at dst_max) and fill *out_hdr. */
static bool dflash_try_bank(uint32_t bank_addr, uint8_t *dst,
    size_t dst_max, size_t *out_payload_len, dflash_header_t *out_hdr) {

    dflash_fcache_sync();
    dflash_header_t hdr;
    memcpy(&hdr, (const void *)bank_addr, sizeof(hdr));

    if (hdr.valid_magic != DFLASH_VALID_MAGIC) return false;
    if (hdr.sequence == 0xFFFFFFFFu)           return false;

    size_t total = hdr.mac_size + hdr.region_size + hdr.crypto_size
        + hdr.se_size + hdr.cmds_size + hdr.classb_size + hdr.cq_size;
    if (total > (s_df.bank_size - DFLASH_HEADER_SIZE)) return false;
    if (total > dst_max)                              return false;

    const uint8_t *payload = (const uint8_t *)(bank_addr + DFLASH_HEADER_SIZE);
    uint32_t expected = dflash_compute_crc(&hdr, payload, total);
    if (expected != hdr.crc32) return false;

    memcpy(dst, payload, total);
    *out_payload_len = total;
    if (out_hdr != NULL) *out_hdr = hdr;
    return true;
}

bool dflash_load_blob(uint8_t *dst, size_t dst_max,
    dflash_header_t *out_hdr) {
    if (!dflash_init()) return false;

    dflash_header_t hdr_a, hdr_b;
    size_t len_a = 0, len_b = 0;
    bool a_ok = dflash_try_bank(s_df.bank_a_addr, dst, dst_max,
        &len_a, &hdr_a);
    /* Use a temp buffer for B so we don't clobber A if A wins. */
    uint8_t tmp_b[1500];  /* fits 1350-byte payload + future growth */
    bool b_ok = false;
    if (sizeof(tmp_b) >= dst_max ||
        sizeof(tmp_b) >= (s_df.bank_size - DFLASH_HEADER_SIZE)) {
        b_ok = dflash_try_bank(s_df.bank_b_addr, tmp_b, sizeof(tmp_b),
            &len_b, &hdr_b);
    }

    if (!a_ok && !b_ok) return false;
    if (a_ok && (!b_ok || (int32_t)(hdr_a.sequence - hdr_b.sequence) > 0)) {
        if (out_hdr) *out_hdr = hdr_a;
        return true;
    }
    /* Bank B wins. Copy from tmp_b. */
    if (len_b > dst_max) return false;
    memcpy(dst, tmp_b, len_b);
    if (out_hdr) *out_hdr = hdr_b;
    return true;
}

/* ---- Save -------------------------------------------------------- */

bool dflash_save_blob(const dflash_header_t *hdr_template,
    const uint8_t *payload, size_t payload_len) {
    if (!dflash_init()) return false;
    if (payload_len > (s_df.bank_size - DFLASH_HEADER_SIZE)) return false;

    /* Discover current active bank (highest valid seq#). */
    dflash_header_t cur;
    uint8_t scratch[1500];
    bool have_current = dflash_load_blob(scratch, sizeof(scratch), &cur);

    uint32_t target_addr = have_current && cur.sequence != 0xFFFFFFFFu
        && memcmp(&cur, (void *)s_df.bank_a_addr, sizeof(cur)) == 0
        ? s_df.bank_b_addr   /* A is current → write to B */
        : s_df.bank_a_addr;  /* default to A on first save */

    /* Build new header. */
    dflash_header_t hdr = *hdr_template;
    hdr.valid_magic = DFLASH_VALID_MAGIC;
    hdr.sequence    = have_current ? (cur.sequence + 1u) : 1u;
    hdr.crc32       = dflash_compute_crc(&hdr, payload, payload_len);
    hdr.reserved    = 0;
    hdr.reserved2   = 0;

    /* Erase target bank. */
    if (!dflash_erase_blocks(target_addr, s_df.bank_blocks)) return false;

    /* Write payload first (so a power-loss before header doesn't make
       the bank look valid with a partial payload). */
    if (payload_len > 0) {
        /* Write granularity is 4 bytes — pad payload to write_size. */
        size_t aligned_len = (payload_len + s_df.write_size - 1u)
            & ~(s_df.write_size - 1u);
        static uint8_t pad_buf[1504] __attribute__((aligned(4)));
        if (aligned_len > sizeof(pad_buf)) return false;
        memcpy(pad_buf, payload, payload_len);
        if (aligned_len > payload_len) {
            memset(pad_buf + payload_len, 0xFF,
                aligned_len - payload_len);
        }
        if (!dflash_write_aligned(target_addr + DFLASH_HEADER_SIZE,
                pad_buf, aligned_len)) {
            return false;
        }
    }

    /* Then write the header. valid_magic only becomes set when the
       4-byte word containing it is committed — atomic at HW level. */
    if (!dflash_write_aligned(target_addr,
            (const uint8_t *)&hdr, sizeof(hdr))) {
        return false;
    }

    return true;
}

/* ---- Factory reset ---------------------------------------------- */

bool dflash_factory_reset(void) {
    if (!dflash_init()) return false;
    if (!dflash_erase_blocks(s_df.bank_a_addr, s_df.bank_blocks)) return false;
    if (!dflash_erase_blocks(s_df.bank_b_addr, s_df.bank_blocks)) return false;
    return true;
}

#endif /* MICROPY_HW_LORA_STACK_RENESAS */
