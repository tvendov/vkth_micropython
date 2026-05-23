/**************************************************************************
Copyright (C) 2009 Lander Casado, Philippas Tsigas

All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files 
(the "Software"), to deal with the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, 
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions: 

Redistributions of source code must retain the above copyright notice, 
this list of conditions and the following disclaimers. Redistributions in
binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimers in the documentation and/or 
other materials provided with the distribution.

In no event shall the authors or copyright holders be liable for any special,
incidental, indirect or consequential damages of any kind, or any damages 
whatsoever resulting from loss of use, data or profits, whether or not 
advised of the possibility of damage, and on any theory of liability, 
arising out of or in connection with the use or performance of this software.
 
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
DEALINGS WITH THE SOFTWARE

*****************************************************************************/
/*
    (C) 2017 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
//#include <sys/param.h>
//#include <sys/systm.h> 
#include <stdint.h>
#include "aes.h"
#include "cmac.h"
#include "utilities.h"

#ifdef RM_TINYCRYPT
#include <tinycrypt/aes.h>
#include "board.h"
#endif

#define LSHIFT(v, r) do {                           \
    int32_t i;                                      \
    for (i = 0; i < 15; i++)                        \
        (r)[i] = (v)[i] << 1 | (v)[i + 1] >> 7;     \
    (r)[15] = (v)[15] << 1;                         \
} while (0)
    
#define XOR(v, r) do {                              \
    int32_t i;                                      \
    for (i = 0; i < 16; i++)                        \
        (r)[i] = (r)[i] ^ (v)[i];                   \
} while (0)


void AES_CMAC_Init(AES_CMAC_CTX *ctx)
{
    memset1(ctx->X, 0, sizeof(ctx->X));
    ctx->M_n = 0;
    memset1(ctx->rijndael.ksch, '\0', sizeof(ctx->rijndael.ksch));
}
    
void AES_CMAC_SetKey(AES_CMAC_CTX *ctx, const uint8_t key[AES_CMAC_KEY_LENGTH])
{
    //rijndael_set_key_enc_only(&ctx->rijndael, key, 128);
#ifdef RM_TINYCRYPT
    tc_aes128_set_encrypt_key( (TCAesKeySched_t)&ctx->rijndael.ksch, key );
#else
    aes_set_key( key, AES_CMAC_KEY_LENGTH, &ctx->rijndael);
#endif
}
    
void AES_CMAC_Update(AES_CMAC_CTX *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t mlen;
    uint8_t in[16];
    
    if (ctx->M_n > 0) {
        mlen = MIN(16 - ctx->M_n, len);
        memcpy1(ctx->M_last + ctx->M_n, data, mlen);
        ctx->M_n += mlen;
        if (ctx->M_n < 16 || len == mlen)
            return;
        XOR(ctx->M_last, ctx->X);
        //rijndael_encrypt(&ctx->rijndael, ctx->X, ctx->X);
#ifdef RM_TINYCRYPT
        HW_SCE_PowerOn();
        tc_aes_encrypt( ctx->X, ctx->X, (TCAesKeySched_t)&ctx->rijndael.ksch );
        HW_SCE_PowerOff();
#else
        aes_encrypt( ctx->X, ctx->X, &ctx->rijndael);
#endif
        data += mlen;
        len -= mlen;
    }
    while (len > 16) {      /* not last block */

        XOR(data, ctx->X);
        //rijndael_encrypt(&ctx->rijndael, ctx->X, ctx->X);

        memcpy1(in, &ctx->X[0], 16); //Bestela ez du ondo iten
#ifdef RM_TINYCRYPT
        HW_SCE_PowerOn();
        tc_aes_encrypt( in, in, (TCAesKeySched_t)&ctx->rijndael.ksch );
        HW_SCE_PowerOff();
#else
        aes_encrypt( in, in, &ctx->rijndael);
#endif
        memcpy1(&ctx->X[0], in, 16);

        data += 16;
        len -= 16;
    }
    /* potential last block, save it */
    memcpy1(ctx->M_last, data, len);
    ctx->M_n = len;
}
   
void AES_CMAC_Final(uint8_t digest[AES_CMAC_DIGEST_LENGTH], AES_CMAC_CTX *ctx)
{
    uint8_t K[16];
    uint8_t in[16];
    /* generate subkey K1 */
    memset1(K, '\0', 16);

    //rijndael_encrypt(&ctx->rijndael, K, K);
#ifdef RM_TINYCRYPT
    HW_SCE_PowerOn();
    tc_aes_encrypt( K, K, (TCAesKeySched_t)&ctx->rijndael.ksch );
    HW_SCE_PowerOff();
#else
    aes_encrypt( K, K, &ctx->rijndael);
#endif

    if (K[0] & 0x80) {
        LSHIFT(K, K);
        K[15] ^= 0x87;
    } else
        LSHIFT(K, K);


    if (ctx->M_n == 16) {
        /* last block was a complete block */
        XOR(K, ctx->M_last);

    } else {
        /* generate subkey K2 */
        if (K[0] & 0x80) {
            LSHIFT(K, K);
            K[15] ^= 0x87;
        } else
            LSHIFT(K, K);

        /* padding(M_last) */
        ctx->M_last[ctx->M_n] = 0x80;
        while (++ctx->M_n < 16)
            ctx->M_last[ctx->M_n] = 0;

        XOR(K, ctx->M_last);

    }
    XOR(ctx->M_last, ctx->X);

    //rijndael_encrypt(&ctx->rijndael, ctx->X, digest);

    memcpy1(in, &ctx->X[0], 16); //Bestela ez du ondo iten

#ifdef RM_TINYCRYPT
    HW_SCE_PowerOn();
    tc_aes_encrypt( digest, in, (TCAesKeySched_t)&ctx->rijndael.ksch );
    HW_SCE_PowerOff();
#else
    aes_encrypt(in, digest, &ctx->rijndael);
#endif
    memset1(K, 0, sizeof K);

}

