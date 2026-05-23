/*
 * lorawan/system/flash/nvm_board.c
 *
 * NVM persistence for the LoRaMac stack.
 *
 * Modern API path (vs upstream's per-field nvm.c approach):
 *   * `LoRaMacMibGetRequestConfirm({Type=MIB_NVM_CTXS})` returns a
 *     `LoRaMacCtxs_t*` with 7 module context pointers + sizes (MAC,
 *     REGION, CRYPTO, SECURE_ELEMENT, COMMANDS, CLASS_B, CONFIRM_QUEUE).
 *   * On save, we serialize each context blob into our own buffer.
 *   * On restore, we copy back and set via MIB_NVM_CTXS.
 *   * `NvmContextChange` LoRaMac callback fires when contexts change,
 *     triggering an asynchronous save.
 *
 * Contexts are cached in RAM and persisted to RA4M2 data flash
 * (0x40100000-0x40101FFF, 8 KB) with an atomic ping-pong layout for
 * power-loss safety.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "nvm_board.h"
#include "dflash_lwnvm.h"
#include "lorawan_stats.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

#if defined(LORAWAN_BUILD_PHASE) && (LORAWAN_BUILD_PHASE >= 4)
#define NVM_PHASE6_AVAILABLE   (1)
#include "LoRaMac.h"
#else
#define NVM_PHASE6_AVAILABLE   (0)
#endif

/* RAM-backed slot per LoRaMac module context. Sizes calibrated for
   EU868 (measured on hardware via mac.nvm_diag()):
     mac=392, region=392, crypto=40, se=372, cmds=132, classb=0, cq=22.
   Buffers rounded up to next 64-byte boundary with ≥25% margin so
   future LoRaMac versions / additional MAC commands won't overflow. */
#define NVM_MAC_CTX_MAX        (512)
#define NVM_REGION_CTX_MAX     (512)
#define NVM_CRYPTO_CTX_MAX     (64)
#define NVM_SE_CTX_MAX         (512)
#define NVM_CMDS_CTX_MAX       (256)
#define NVM_CLASSB_CTX_MAX     (64)
#define NVM_CQ_CTX_MAX         (64)

typedef struct {
    uint8_t  mac_ctx[NVM_MAC_CTX_MAX];
    size_t   mac_size;
    uint8_t  region_ctx[NVM_REGION_CTX_MAX];
    size_t   region_size;
    uint8_t  crypto_ctx[NVM_CRYPTO_CTX_MAX];
    size_t   crypto_size;
    uint8_t  se_ctx[NVM_SE_CTX_MAX];
    size_t   se_size;
    uint8_t  cmds_ctx[NVM_CMDS_CTX_MAX];
    size_t   cmds_size;
    uint8_t  classb_ctx[NVM_CLASSB_CTX_MAX];
    size_t   classb_size;
    uint8_t  cq_ctx[NVM_CQ_CTX_MAX];
    size_t   cq_size;
    bool     valid;
} nvm_blob_t;

static nvm_blob_t s_nvm;

void nvm_board_init(void) {
    /* Open data flash via FSP and load any persisted blob
       from the active ping-pong bank into our RAM cache. On first boot
       (banks erased) `dflash_load_blob` returns false and we leave
       `s_nvm.valid` cleared so NvmDataMgmtRestore reports 0 bytes. */
    if (!dflash_init()) {
        return;
    }
    /* Defer flash bank scan out of constructor. dflash_init() above has
       already resolved geometry into s_df, so a later explicit restore can
       scan the banks lazily via nvm_load_from_dflash(). s_nvm.valid stays
       false until that succeeds, equivalent to the first-boot path. */
}

void nvm_board_deinit(void) {
    /* idempotent NOP — flash stays valid across boots; we don't need
       to release anything here. */
}

#if NVM_PHASE6_AVAILABLE

/* Deferred-load helper. Lives inside NVM_PHASE6_AVAILABLE because its sole
   caller (NvmDataMgmtRestore) is also gated by that macro. */
static bool nvm_load_from_dflash(void);

/* 028 — lazy-load helper. Holds the body that used to run at ctor in
   nvm_board_init(): pull the active dflash bank into a scratch buffer,
   validate header sizes, unpack into s_nvm slots. Called once from
   NvmDataMgmtRestore() if s_nvm.valid is still false (first call after
   Mac() construction). Returns true on a successful restore; false on
   missing bank, header size mismatch, or any dflash error. */
static bool nvm_load_from_dflash(void) {
    static uint8_t scratch[1500];
    dflash_header_t hdr;
    if (!dflash_load_blob(scratch, sizeof(scratch), &hdr)) {
        return false;
    }
    size_t off = 0;
    if (hdr.mac_size > NVM_MAC_CTX_MAX || hdr.region_size > NVM_REGION_CTX_MAX
        || hdr.crypto_size > NVM_CRYPTO_CTX_MAX || hdr.se_size > NVM_SE_CTX_MAX
        || hdr.cmds_size > NVM_CMDS_CTX_MAX
        || hdr.classb_size > NVM_CLASSB_CTX_MAX
        || hdr.cq_size > NVM_CQ_CTX_MAX) {
        return false;
    }
    memcpy(s_nvm.mac_ctx,    scratch + off, hdr.mac_size);    off += hdr.mac_size;
    s_nvm.mac_size = hdr.mac_size;
    memcpy(s_nvm.region_ctx, scratch + off, hdr.region_size); off += hdr.region_size;
    s_nvm.region_size = hdr.region_size;
    memcpy(s_nvm.crypto_ctx, scratch + off, hdr.crypto_size); off += hdr.crypto_size;
    s_nvm.crypto_size = hdr.crypto_size;
    memcpy(s_nvm.se_ctx,     scratch + off, hdr.se_size);     off += hdr.se_size;
    s_nvm.se_size = hdr.se_size;
    memcpy(s_nvm.cmds_ctx,   scratch + off, hdr.cmds_size);   off += hdr.cmds_size;
    s_nvm.cmds_size = hdr.cmds_size;
    memcpy(s_nvm.classb_ctx, scratch + off, hdr.classb_size); off += hdr.classb_size;
    s_nvm.classb_size = hdr.classb_size;
    memcpy(s_nvm.cq_ctx,     scratch + off, hdr.cq_size);     off += hdr.cq_size;
    s_nvm.cq_size = hdr.cq_size;
    s_nvm.valid = true;
    return true;
}

/* Helper: copy `src_size` bytes from `src` into `dst_buf` (capped at
   `dst_max`). Updates `*dst_size`. Returns false on overflow. */
static bool nvm_copy_in(const void *src, size_t src_size,
    uint8_t *dst_buf, size_t dst_max, size_t *dst_size) {
    if (src == NULL || src_size == 0) {
        *dst_size = 0;
        return true;
    }
    if (src_size > dst_max) {
        return false;
    }
    memcpy(dst_buf, src, src_size);
    *dst_size = src_size;
    return true;
}

/* P3.4 — deferred-save pending flag. Set when NvmDataMgmtStore is called
   during an active RX window; cleared (and save replayed) by
   NvmDataMgmtFlushDeferred() called from the MAC confirm callbacks after
   they clear s_rx_window_active.

   Volatile because writer (Python pump / MAC callback context) and reader
   (same contexts, no ISR) coexist but no concurrent multi-threading.
   Single-byte aligned access on Armv8-M is atomic at the bus level. */
static volatile bool s_nvm_deferred_pending = false;

uint16_t NvmDataMgmtStore(void) {
    /* Observation only. Entry stamp + RX-window collision
       check happen before any save work; done stamp + last/max happen after
       dflash_save_blob() at the bottom of the success path. Counters never
       alter return value or save semantics. _nvm_call_us is also consumed
       by the exit-delta block ~80 lines below, so it lives across the save
       — `#ifndef`-wrapped here and at the consumer to avoid an unused-
       variable warning under -Werror in the disable build. */
    #ifndef LORAWAN_OBSERVATION_DISABLE
    uint32_t _nvm_call_us = mp_hal_ticks_us();
    STATS_STORE(nvm_save_call_us, _nvm_call_us);
    #endif
    STATS_INC(nvm_save_count);
    if (s_rx_window_active) {
        STATS_INC(nvm_save_in_rx_window_count);
        /* P3.4 — defer the actual flash work until RX window closes.
           dflash_save_blob is ~5-6 ms blocking; running it inside the
           RX1 (1 s) or RX2 (2 s) window would push BUSY-low past the
           radio's RxDone latch and miss the downlink. RAM cache update
           also deferred — MAC layer keeps the in-RAM context dirty,
           and the next OnNvmDataChange callback (or our flush) will
           snapshot it then. Return 0 = "no bytes persisted this call". */
        s_nvm_deferred_pending = true;
        return 0;
    }

    /* Pull current contexts from LoRaMac and snapshot into our RAM
       blob. Returns total bytes stored.

       All early-return paths funnel through `early_return:` so
       nvm_save_done_us / nvm_save_last_ms get a consistent attempt-vs-
       success-shaped record: zero-ms entry == early-return path,
       non-zero ms == real flash work. nvm_save_max_ms is intentionally
       untouched on early returns (STATS_UPDATE_MAX with 0 is a no-op),
       and nvm_save_error_count is left for the dflash_save_blob failure
       branch only (an MIB/copy failure isn't a flash error). */
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_NVM_CTXS;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        goto early_return;
    }
    LoRaMacCtxs_t *ctx = mib.Param.Contexts;
    if (ctx == NULL) {
        goto early_return;
    }

    if (!nvm_copy_in(ctx->MacNvmCtx, ctx->MacNvmCtxSize,
            s_nvm.mac_ctx, NVM_MAC_CTX_MAX, &s_nvm.mac_size)) goto early_return;
    if (!nvm_copy_in(ctx->RegionNvmCtx, ctx->RegionNvmCtxSize,
            s_nvm.region_ctx, NVM_REGION_CTX_MAX, &s_nvm.region_size)) goto early_return;
    if (!nvm_copy_in(ctx->CryptoNvmCtx, ctx->CryptoNvmCtxSize,
            s_nvm.crypto_ctx, NVM_CRYPTO_CTX_MAX, &s_nvm.crypto_size)) goto early_return;
    if (!nvm_copy_in(ctx->SecureElementNvmCtx, ctx->SecureElementNvmCtxSize,
            s_nvm.se_ctx, NVM_SE_CTX_MAX, &s_nvm.se_size)) goto early_return;
    if (!nvm_copy_in(ctx->CommandsNvmCtx, ctx->CommandsNvmCtxSize,
            s_nvm.cmds_ctx, NVM_CMDS_CTX_MAX, &s_nvm.cmds_size)) goto early_return;
    if (!nvm_copy_in(ctx->ClassBNvmCtx, ctx->ClassBNvmCtxSize,
            s_nvm.classb_ctx, NVM_CLASSB_CTX_MAX, &s_nvm.classb_size)) goto early_return;
    if (!nvm_copy_in(ctx->ConfirmQueueNvmCtx, ctx->ConfirmQueueNvmCtxSize,
            s_nvm.cq_ctx, NVM_CQ_CTX_MAX, &s_nvm.cq_size)) goto early_return;

    s_nvm.valid = true;

    /* Also persist to data flash. Pack the 7 module
       blobs into a single contiguous buffer that dflash will write
       atomically to the inactive bank. */
    {
        static uint8_t packed[1500] __attribute__((aligned(4)));
        size_t off = 0;
        memcpy(packed + off, s_nvm.mac_ctx,    s_nvm.mac_size);    off += s_nvm.mac_size;
        memcpy(packed + off, s_nvm.region_ctx, s_nvm.region_size); off += s_nvm.region_size;
        memcpy(packed + off, s_nvm.crypto_ctx, s_nvm.crypto_size); off += s_nvm.crypto_size;
        memcpy(packed + off, s_nvm.se_ctx,     s_nvm.se_size);     off += s_nvm.se_size;
        memcpy(packed + off, s_nvm.cmds_ctx,   s_nvm.cmds_size);   off += s_nvm.cmds_size;
        memcpy(packed + off, s_nvm.classb_ctx, s_nvm.classb_size); off += s_nvm.classb_size;
        memcpy(packed + off, s_nvm.cq_ctx,     s_nvm.cq_size);     off += s_nvm.cq_size;

        dflash_header_t hdr = { 0 };
        hdr.mac_size    = (uint16_t)s_nvm.mac_size;
        hdr.region_size = (uint16_t)s_nvm.region_size;
        hdr.crypto_size = (uint16_t)s_nvm.crypto_size;
        hdr.se_size     = (uint16_t)s_nvm.se_size;
        hdr.cmds_size   = (uint16_t)s_nvm.cmds_size;
        hdr.classb_size = (uint16_t)s_nvm.classb_size;
        hdr.cq_size     = (uint16_t)s_nvm.cq_size;
        bool _save_ok = dflash_save_blob(&hdr, packed, off);
        /* Errors here are non-fatal: RAM cache is still valid for
           in-boot persistence even if flash write failed. */
        if (!_save_ok) {
            STATS_INC(nvm_save_error_count);
        }
    }

    /* Close the timing window. dflash_save_blob is
       blocking-synchronous (see phase1_step0_preflight_symbols.md §9), so
       the delta covers the actual flash erase+write cost. */
    #ifndef LORAWAN_OBSERVATION_DISABLE
    {
        uint32_t _nvm_done_us = mp_hal_ticks_us();
        STATS_STORE(nvm_save_done_us, _nvm_done_us);
        uint32_t _nvm_dt_ms = (uint32_t)((_nvm_done_us - _nvm_call_us) / 1000u);
        STATS_STORE(nvm_save_last_ms, _nvm_dt_ms);
        STATS_UPDATE_MAX(nvm_save_max_ms, _nvm_dt_ms);
    }
    #endif

    return (uint16_t)(s_nvm.mac_size + s_nvm.region_size + s_nvm.crypto_size
        + s_nvm.se_size + s_nvm.cmds_size + s_nvm.classb_size + s_nvm.cq_size);

early_return:
    /* Close timing for the MIB / context-copy early-return paths
       so timing fields stay consistent with nvm_save_count (which tracks
       attempts, not successes). last_ms = 0 marks an early-return record;
       max_ms is untouched because STATS_UPDATE_MAX(field, 0) is a no-op. */
    #ifndef LORAWAN_OBSERVATION_DISABLE
    STATS_STORE(nvm_save_done_us, _nvm_call_us);
    #endif
    STATS_STORE(nvm_save_last_ms, 0u);
    return 0;
}

uint16_t NvmDataMgmtRestore(void) {
    /* Return first-boot semantics while lazy flash-bank restore remains
       disabled below. */
#if 1  /* set 0 to restore the lazy-load path */
    return 0;
#else
    /* Push our RAM blob back into LoRaMac via MIB_NVM_CTXS set. Must
       be called AFTER LoRaMacInitialization (which allocates the
       internal context buffers). LoRaMac copies into its own buffers,
       so we are not exposing transient pointers. */
    /* Lazy load. The ctor deferred the flash bank scan; first restore call
       tries the load after Mac construction. */
    if (!s_nvm.valid) {
        (void)nvm_load_from_dflash();
    }
    if (!s_nvm.valid) {
        /* Either banks erased (first-boot factory state) or the deferred
           load failed (corrupt header / size mismatch). MAC initialises
           from defaults. */
        return 0;
    }
    LoRaMacCtxs_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.MacNvmCtx              = s_nvm.mac_ctx;
    blob.MacNvmCtxSize          = s_nvm.mac_size;
    blob.RegionNvmCtx           = s_nvm.region_ctx;
    blob.RegionNvmCtxSize       = s_nvm.region_size;
    blob.CryptoNvmCtx           = s_nvm.crypto_ctx;
    blob.CryptoNvmCtxSize       = s_nvm.crypto_size;
    blob.SecureElementNvmCtx    = s_nvm.se_ctx;
    blob.SecureElementNvmCtxSize = s_nvm.se_size;
    blob.CommandsNvmCtx         = s_nvm.cmds_ctx;
    blob.CommandsNvmCtxSize     = s_nvm.cmds_size;
    blob.ClassBNvmCtx           = s_nvm.classb_ctx;
    blob.ClassBNvmCtxSize       = s_nvm.classb_size;
    blob.ConfirmQueueNvmCtx     = s_nvm.cq_ctx;
    blob.ConfirmQueueNvmCtxSize = s_nvm.cq_size;

    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_NVM_CTXS;
    mib.Param.Contexts = &blob;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        return 0;
    }
    return (uint16_t)(s_nvm.mac_size + s_nvm.region_size + s_nvm.crypto_size
        + s_nvm.se_size + s_nvm.cmds_size + s_nvm.classb_size + s_nvm.cq_size);
#endif  /* lazy restore disabled */
}

bool NvmDataMgmtFactoryReset(void) {
    memset(&s_nvm, 0, sizeof(s_nvm));
    /* Also wipe the flash banks. */
    (void)dflash_factory_reset();
    return true;
}

/* P3.4 — flush deferred save outside RX1/RX2 window.
   Called from mac_mcps_indication / mac_mcps_confirm / mac_mlme_confirm
   in mod_lorawan.c AFTER they set s_rx_window_active=0. Safe to call
   even when no save is pending (no-op). Pending flag cleared BEFORE
   the recursive NvmDataMgmtStore() so a re-entry from inside the
   flash callback chain won't spin. */
void NvmDataMgmtFlushDeferred(void) {
    if (!s_nvm_deferred_pending) {
        return;
    }
    s_nvm_deferred_pending = false;
    (void)NvmDataMgmtStore();
}

#else  /* !NVM_PHASE6_AVAILABLE */

uint16_t NvmDataMgmtStore(void) { return 0; }
uint16_t NvmDataMgmtRestore(void) { return 0; }
void     NvmDataMgmtFlushDeferred(void) {}
bool NvmDataMgmtFactoryReset(void) { return true; }

#endif

#endif /* MICROPY_HW_LORA_STACK_RENESAS */
