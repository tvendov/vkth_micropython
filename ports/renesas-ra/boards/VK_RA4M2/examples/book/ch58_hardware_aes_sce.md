### Глава 58. Hardware AES — Secure Crypto Engine (SCE) `[Напреднал]`

> **💡 Аналогия:** Software AES е като да броиш пари ръчно — точно, но бавно. SCE е банкова сметна машина: пускаш купчината, получаваш резултата, без да си преминал през всеки банкнот.

---

#### 58.1. Кратък контекст

| MCU | SCE вариант | AES режими | Допълнителни |
|---|---|---|---|
| RA4M2 | SCE5 lite | AES-128 ECB/CBC/CTR, CMAC | TRNG |
| **RA6M5** | **SCE9** | AES-128/192/256 ECB/CBC/CTR/**GCM**/**CCM**/XTS, CMAC | SHA-1/256, RSA до 4096-bit, ECC P-192…P-521, TRNG, key wrapping |

SCE-то е **отделна периферия**: ядрото подава ключове, IV-та и данни през мемъри-мапнати регистри, SCE прави crypto в своя процесор и връща резултата. **CPU цикли — нула** по време на самата операция. На RA6M5 @ 200 MHz измерената печалба е 10-30× за AES bulk transfer и 50-100× за RSA modexp.

FSP API-то има две нива:
- **`HW_SCE_*` низко ниво** (в `lib/fsp/ra/fsp/src/r_sce/`) — директни AES/CMAC/SHA/TRNG примитиви
- **`rm_psa_crypto/` високо ниво** — реализира `mbedtls_*_alt.c` интерфейса; mbedTLS вижда нормално API, под капака извиква SCE

В нашия port-а в момента **нищо от това не е включено**. SCE-то стои неактивно. Тази глава описва как да се активира за три отделни вектора.

---

#### 58.2. Текуща ситуация в кода

```
ports/renesas-ra/Makefile:
   USE_FSP_SCE ?= 0       ← gate, по подразбиране изключено

ports/renesas-ra/mbedtls/mbedtls_config_port.h:
   #include "extmod/mbedtls/mbedtls_config_common.h"
   /* MBEDTLS_AES_C, _GCM_C, _SHA256_C — software реализации */

ports/renesas-ra/lorawan/soft_se/aes.c:
   /* чист C software AES, ползва се от LoRaMacCrypto */

ports/renesas-ra/modaes_cmac.c:
   /* axTLS AES, ползва се от Python-стека на LoRaWAN */

ports/renesas-ra/rng.c:
   /* TRNG: вече ползва R_SCE_RandomNumberGenerate? — провери */
```

---

#### 58.3. Вектор 1 — mbedTLS `*_ALT` за TLS/HTTPS на RA6M5

**Печалба:** TLS handshake 5-10× по-бърз, AES-128-GCM bulk encryption ~15-20 MB/s (от ~2 MB/s software). 100 Mbps Ethernet линията става bottleneck вместо CPU.

**Стъпки:**

1. **Включи SCE9 plainkey driver** (177 файла, ~150 KB ROM):
   ```makefile
   # boards/VK_RA6M5/mpconfigboard.mk
   USE_FSP_SCE = 1
   ```

2. **Добави SCE config** `boards/VK_RA6M5/ra_cfg/fsp_cfg/r_sce_cfg.h`:
   ```c
   #ifndef R_SCE_CFG_H_
   #define R_SCE_CFG_H_
   #define SCE_CFG_PARAM_CHECKING_ENABLE   (BSP_CFG_PARAM_CHECKING_ENABLE)
   #define SCE_CFG_AES_192BIT_ENABLE       (1)
   #define SCE_CFG_AES_256BIT_ENABLE       (1)
   #define SCE_USER_SHA_384_ENABLED        (0)
   #define SCE_USER_SHA_512_ENABLED        (0)
   #endif
   ```

3. **Включи R_SCE_Open в boot path** (`board_init.c`):
   ```c
   #include "r_sce.h"
   sce_instance_ctrl_t g_sce_ctrl;
   sce_cfg_t g_sce_cfg = { .lifecycle = SCE_SSD };
   void board_init(void) {
       /* ... съществуващ OSPI код ... */
       R_SCE_Open(&g_sce_ctrl, &g_sce_cfg);
   }
   ```

4. **Добави `*_ALT` дефиниции и rm_psa_crypto sources** в `mbedtls_config_port.h`:
   ```c
   #if defined(RA6M5)
   #define MBEDTLS_AES_ALT
   #define MBEDTLS_GCM_ALT
   #define MBEDTLS_CCM_ALT
   #define MBEDTLS_CMAC_ALT
   #define MBEDTLS_SHA256_ALT
   #define MBEDTLS_ECDH_ALT
   #define MBEDTLS_ECDSA_VERIFY_ALT
   #define MBEDTLS_ECDSA_SIGN_ALT
   #define MBEDTLS_ENTROPY_HARDWARE_ALT
   #endif
   ```

5. **Добави rm_psa_crypto в Makefile** под USE_FSP_SCE:
   ```makefile
   ifeq ($(USE_FSP_SCE), 1)
   PSA_DIR = $(HAL_DIR)/ra/fsp/src/rm_psa_crypto
   HAL_SRC_C += $(wildcard $(PSA_DIR)/*.c)
   INC += -I$(TOP)/$(PSA_DIR)/inc
   endif
   ```

6. **Викай `mbedtls_psa_crypto_init()`** преди първото TLS използване (eg. в `mod_network_lwip_init`).

**Тест:**
```python
import urequests
r = urequests.get("https://api.github.com/zen")
print(r.text)
# Преди: ~5-10 sec; След: <1 sec за handshake + transfer
```

**Рискове:**
- ROM cost: +30-50 KB (но премахва ~25 KB software AES/GCM/SHA)
- PSA crypto wrapper изисква HEAP — на RA4M2 (~150 KB free) става тясно; на RA6M5 (~7.8 MB OSPI free) няма проблем
- Race condition: SCE е singleton; mbedTLS PSA layer-ът държи вътрешен mutex — без extra grijа

---

#### 58.4. Вектор 2 — Custom `urenesas_crypto` Python модул

**Печалба:** Application code в Python може да прави bulk AES без TLS overhead-а. Полезно за enrypt-on-write на JSON преди MQTT publish, file encryption и т.н.

**Скелет** (`ports/renesas-ra/modurenesas_crypto.c`):

```c
#include "py/runtime.h"
#include "py/objstr.h"
#if USE_FSP_SCE
#include "r_sce.h"

extern sce_instance_ctrl_t g_sce_ctrl;

/* AES-128 ECB encrypt: key (16 B), data (multiple of 16 B) → ciphertext */
static mp_obj_t urenesas_aes_ecb_encrypt(mp_obj_t key_in, mp_obj_t data_in) {
    mp_buffer_info_t key, data;
    mp_get_buffer_raise(key_in,  &key,  MP_BUFFER_READ);
    mp_get_buffer_raise(data_in, &data, MP_BUFFER_READ);
    if (key.len != 16 || data.len % 16 != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("key=16B, data=mult of 16B"));
    }
    sce_aes_key_index_t idx;
    if (HW_SCE_GenerateAes128PlainKeyIndex(key.buf, &idx) != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    vstr_t v;
    vstr_init_len(&v, data.len);
    sce_aes_handle_t h;
    R_SCE_AES128ECB_EncryptInit(&h, &idx);
    R_SCE_AES128ECB_EncryptUpdate(&h, data.buf, (uint8_t *)v.buf, data.len);
    R_SCE_AES128ECB_EncryptFinal(&h, (uint8_t *)v.buf + data.len, NULL);
    return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_2(urenesas_aes_ecb_encrypt_obj,
                                 urenesas_aes_ecb_encrypt);

/* TRNG bytes */
static mp_obj_t urenesas_urandom(mp_obj_t n_in) {
    size_t n = mp_obj_get_int(n_in);
    vstr_t v; vstr_init_len(&v, n);
    uint32_t buf[4];
    for (size_t i = 0; i < n; i += 16) {
        HW_SCE_GenerateRandomNumber(buf);
        size_t take = (n - i) < 16 ? (n - i) : 16;
        memcpy((uint8_t *)v.buf + i, buf, take);
    }
    return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_1(urenesas_urandom_obj, urenesas_urandom);

static const mp_rom_map_elem_t urenesas_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),         MP_ROM_QSTR(MP_QSTR_urenesas_crypto) },
    { MP_ROM_QSTR(MP_QSTR_aes_ecb_encrypt),  MP_ROM_PTR(&urenesas_aes_ecb_encrypt_obj) },
    { MP_ROM_QSTR(MP_QSTR_urandom),          MP_ROM_PTR(&urenesas_urandom_obj) },
};
static MP_DEFINE_CONST_DICT(urenesas_module_globals, urenesas_module_globals_table);

const mp_obj_module_t mp_module_urenesas_crypto = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&urenesas_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_urenesas_crypto, mp_module_urenesas_crypto);
#endif /* USE_FSP_SCE */
```

**Тест:**
```python
import urenesas_crypto as rc
key = b"YELLOW SUBMARINE"
ct  = rc.aes_ecb_encrypt(key, b"\x00" * 16)
print(ct.hex())          # очаквай 0f7f...
salt = rc.urandom(16)
print(salt.hex())
```

---

#### 58.5. Вектор 3 — LoRaWAN soft_se → SCE

**Печалба:** На всеки uplink/downlink LoRaWAN прави ~8 AES блока (4 за CMAC MIC, 4 за payload encrypt). Software на RA4M2 @ 100 MHz: ~120 µs total. SCE: ~10 µs. **>10× ускорение**, по-малък battery drain.

**Стъпки:**

1. Активирай SCE5 plainkey driver:
   ```makefile
   # boards/VK_RA4M2/mpconfigboard.mk
   USE_FSP_SCE = 1
   ```
   Същото R_SCE_Open в `board_init.c`.

2. Замени `aes_encrypt` в `lorawan/soft_se/aes.c`:
   ```c
   #if USE_FSP_SCE
   #include "r_sce.h"
   void aes_encrypt(uint8_t *block_in, uint8_t *block_out, aes_context *ctx) {
       sce_aes_key_index_t idx;
       HW_SCE_GenerateAes128PlainKeyIndex(ctx->key, &idx);
       sce_aes_handle_t h;
       R_SCE_AES128ECB_EncryptInit(&h, &idx);
       R_SCE_AES128ECB_EncryptUpdate(&h, block_in, block_out, 16);
       R_SCE_AES128ECB_EncryptFinal(&h, NULL, NULL);
   }
   #else
   /* съществуваща C реализация */
   #endif
   ```

3. Същата замяна за `cmac.c::AES_CMAC_*` (по-сложно — CMAC има state, използвайте `R_SCE_AES128_CMAC_*` API директно).

> **Бележка**: `HW_SCE_GenerateAes128PlainKeyIndex` се вика на всеки блок — би било по-ефективно да се cache-не индексът за дълготрайни ключове (NwkSKey, AppSKey). За първи бенчмарк не е критично; в production си струва.

---

#### 58.6. Какво е в текущия repo за тази глава

| Артефакт | Статус |
|---|---|
| `Makefile` `USE_FSP_SCE` gate | ✅ добавен (default 0) |
| SCE9 plainkey 177 файла | в `lib/fsp/`, готови за компилация |
| SCE5 plainkey 80 файла | в `lib/fsp/`, готови за компилация |
| `r_sce_cfg.h` за VK_RA6M5/RA4M2 | ❌ трябва създаване (виж 58.3 step 2) |
| `R_SCE_Open` в board_init | ❌ трябва добавка |
| `mbedtls_*_ALT` дефиниции | ❌ трябва добавка |
| `urenesas_crypto.c` модул | ❌ трябва добавка (скелет в 58.4) |
| LoRaWAN soft_se SCE binding | ❌ трябва добавка (диф в 58.5) |

Всеки вектор е независим — може да активираш само 2 (Python модул) без 1 (mbedTLS) или само 3 (LoRaWAN) без 2.

---

#### 58.7. Капани и съвети

| Тема | Капан | Решение |
|---|---|---|
| SCE singleton | Едновременно викане от ISR + main → race | FSP-то има вътрешен mutex; не викай SCE от ISR |
| Cache coherency | DMA в OSPI RAM преди SCE → стари данни | `SCB_CleanDCache_by_Addr` преди SCE; `SCB_InvalidateDCache_by_Addr` след |
| Side-channel | SCE *не* е 100% constant-time | За IoT/home използване OK; за hostile environment търси TFM или dedicated SE chip |
| Key management | Plain key в RAM = leakable | За production: `HW_SCE_GenerateAes128KeyIndex` с pre-injected OEM key (Renesas Key Injection Tool) |
| ROM size | +30-50 KB при SCE9 + rm_psa_crypto | Няма безплатен обяд; на RA4M2 (512 KB flash) внимавай за общия размер |
| Initial latency | `HW_SCE_GenerateAesXXXPlainKeyIndex` ~5-10 µs | Cache-вай индексите за дълготрайни ключове |

---

#### 58.8. Препратки

- `lib/fsp/ra/fsp/src/r_sce/hw_sce_aes_private.h` — низ-нив AES API
- `lib/fsp/ra/fsp/src/r_sce/crypto_procedures/src/sce9/plainkey/public/inc/r_sce_if.h` — high-level SCE9 API
- `lib/fsp/ra/fsp/src/rm_psa_crypto/aes_alt.c` — mbedTLS *_alt реализация
- FSP User Manual `r01an5414eu0250-ra-fsp-userman.pdf`, секция „Secure Crypto Engine"
- Renesas Application Note R11AN0498 — „Using SCE7/SCE9 for AES Operations"
- mbedTLS upstream: `library/aes.c` за reference на ALT интерфейса
