/*
 * lorawan/glue/nvm_board.c
 *
 * NVM persistence for the LoRaMac stack — Phase 6a.
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
 * Phase 6a.1 (this commit) — RAM-backed buffer. Survives within one
 * boot, allowing the stack to re-init + restore after `mac.deinit()`,
 * but does NOT survive power cycle. Validates the save/restore path.
 *
 * Phase 6a.2 (next iteration) — replace RAM buffer with R_FLASH_HP BGO
 * writes to RA4M2 data flash (0x40100000-0x40101FFF, 8 KB). Atomic
 * ping-pong layout for power-loss safety.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "py/runtime.h"
#include "glue/nvm_board.h"
#include "glue/dflash.h"

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
    /* Phase 6a.2 — open data flash via FSP and load any persisted blob
       from the active ping-pong bank into our RAM cache. On first boot
       (banks erased) `dflash_load_blob` returns false and we leave
       `s_nvm.valid` cleared so NvmDataMgmtRestore reports 0 bytes. */
    if (!dflash_init()) {
        return;
    }
    /* Pack flash payload into our s_nvm slots. We pack contigously
       in dflash_save_blob; reverse the process here. */
    static uint8_t scratch[1500];
    dflash_header_t hdr;
    if (!dflash_load_blob(scratch, sizeof(scratch), &hdr)) {
        return;
    }
    size_t off = 0;
    if (hdr.mac_size > NVM_MAC_CTX_MAX || hdr.region_size > NVM_REGION_CTX_MAX
        || hdr.crypto_size > NVM_CRYPTO_CTX_MAX || hdr.se_size > NVM_SE_CTX_MAX
        || hdr.cmds_size > NVM_CMDS_CTX_MAX
        || hdr.classb_size > NVM_CLASSB_CTX_MAX
        || hdr.cq_size > NVM_CQ_CTX_MAX) {
        return;  /* size mismatch — likely from a different LoRaMac build */
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
}

void nvm_board_deinit(void) {
    /* idempotent NOP — flash stays valid across boots; we don't need
       to release anything here. */
}

#if NVM_PHASE6_AVAILABLE

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

/* Last MIB-Get status, exposed via nvm_board_last_status() for debug
   so Python can distinguish "MIB request rejected" vs "rejected by our
   wrapper". */
static int s_last_mib_status = -1;
static const LoRaMacCtxs_t *s_last_ctx = NULL;

int nvm_board_last_status(void) {
    return s_last_mib_status;
}

size_t nvm_board_last_total_size(void) {
    if (s_last_ctx == NULL) {
        return 0;
    }
    return s_last_ctx->MacNvmCtxSize + s_last_ctx->RegionNvmCtxSize
        + s_last_ctx->CryptoNvmCtxSize + s_last_ctx->SecureElementNvmCtxSize
        + s_last_ctx->CommandsNvmCtxSize + s_last_ctx->ClassBNvmCtxSize
        + s_last_ctx->ConfirmQueueNvmCtxSize;
}

uint16_t NvmDataMgmtStore(void) {
    /* Pull current contexts from LoRaMac and snapshot into our RAM
       blob. Returns total bytes stored. */
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_NVM_CTXS;
    s_last_mib_status = (int)LoRaMacMibGetRequestConfirm(&mib);
    if (s_last_mib_status != (int)LORAMAC_STATUS_OK) {
        s_last_ctx = NULL;
        return 0;
    }
    LoRaMacCtxs_t *ctx = mib.Param.Contexts;
    s_last_ctx = ctx;
    if (ctx == NULL) {
        return 0;
    }

    if (!nvm_copy_in(ctx->MacNvmCtx, ctx->MacNvmCtxSize,
            s_nvm.mac_ctx, NVM_MAC_CTX_MAX, &s_nvm.mac_size)) return 0;
    if (!nvm_copy_in(ctx->RegionNvmCtx, ctx->RegionNvmCtxSize,
            s_nvm.region_ctx, NVM_REGION_CTX_MAX, &s_nvm.region_size)) return 0;
    if (!nvm_copy_in(ctx->CryptoNvmCtx, ctx->CryptoNvmCtxSize,
            s_nvm.crypto_ctx, NVM_CRYPTO_CTX_MAX, &s_nvm.crypto_size)) return 0;
    if (!nvm_copy_in(ctx->SecureElementNvmCtx, ctx->SecureElementNvmCtxSize,
            s_nvm.se_ctx, NVM_SE_CTX_MAX, &s_nvm.se_size)) return 0;
    if (!nvm_copy_in(ctx->CommandsNvmCtx, ctx->CommandsNvmCtxSize,
            s_nvm.cmds_ctx, NVM_CMDS_CTX_MAX, &s_nvm.cmds_size)) return 0;
    if (!nvm_copy_in(ctx->ClassBNvmCtx, ctx->ClassBNvmCtxSize,
            s_nvm.classb_ctx, NVM_CLASSB_CTX_MAX, &s_nvm.classb_size)) return 0;
    if (!nvm_copy_in(ctx->ConfirmQueueNvmCtx, ctx->ConfirmQueueNvmCtxSize,
            s_nvm.cq_ctx, NVM_CQ_CTX_MAX, &s_nvm.cq_size)) return 0;

    s_nvm.valid = true;

    /* Phase 6a.2 — also persist to data flash. Pack the 7 module
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
        (void)dflash_save_blob(&hdr, packed, off);
        /* Errors here are non-fatal: RAM cache is still valid for
           in-boot persistence even if flash write failed. */
    }

    return (uint16_t)(s_nvm.mac_size + s_nvm.region_size + s_nvm.crypto_size
        + s_nvm.se_size + s_nvm.cmds_size + s_nvm.classb_size + s_nvm.cq_size);
}

uint16_t NvmDataMgmtRestore(void) {
    /* Push our RAM blob back into LoRaMac via MIB_NVM_CTXS set. Must
       be called AFTER LoRaMacInitialization (which allocates the
       internal context buffers). LoRaMac copies into its own buffers,
       so we are not exposing transient pointers. */
    if (!s_nvm.valid) {
        /* nvm_board_init() ran on Mac() construction and would have
           populated s_nvm from the active dflash bank if one existed.
           If we still see !valid, the bank is missing or corrupt. */
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
}

bool NvmDataMgmtFactoryReset(void) {
    memset(&s_nvm, 0, sizeof(s_nvm));
    /* Phase 6a.2 — also wipe the flash banks. */
    (void)dflash_factory_reset();
    return true;
}

#else  /* !NVM_PHASE6_AVAILABLE */

uint16_t NvmDataMgmtStore(void) { return 0; }
uint16_t NvmDataMgmtRestore(void) { return 0; }
bool NvmDataMgmtFactoryReset(void) { return true; }

#endif

#endif /* MICROPY_HW_LORA_STACK_RENESAS */
