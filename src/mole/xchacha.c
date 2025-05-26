/* https://github.com/bradleyeckert/xchacha
 *
 * A highly refactored version of https://github.com/spcnvdr/xchacha20
 */

#include <stdint.h>
#include <string.h>
#include "xchacha.h"

static const uint8_t ind[32] = {
    0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15,
    0, 5, 10, 15, 1, 6, 11, 12, 2, 7, 8, 13, 3, 4, 9, 14
};

static void doRounds(uint32_t *x) {
    for (int i = 0; i < 10; i++){
        for (int j = 0; j < 8; j++) {
            const uint8_t * p = &ind[j*4];  // eliminate QUARTERROUND macro
            x[p[0]] += x[p[1]];  x[p[3]] = ROTL32(x[p[3]] ^ x[p[0]], 16);
            x[p[2]] += x[p[3]];  x[p[1]] = ROTL32(x[p[1]] ^ x[p[2]], 12);
            x[p[0]] += x[p[1]];  x[p[3]] = ROTL32(x[p[3]] ^ x[p[0]], 8);
            x[p[2]] += x[p[3]];  x[p[1]] = ROTL32(x[p[1]] ^ x[p[2]], 7);
        }
    }
}

static uint32_t u8tou32(const uint8_t *p) {
    return                              // This little gem handles alignment
  (((uint32_t)(p[0])      ) |           // even if the CPU doesn't.
   ((uint32_t)(p[1]) <<  8) |
   ((uint32_t)(p[2]) << 16) |
   ((uint32_t)(p[3]) << 24));
}

static void u32tou8(uint8_t *p, uint32_t v) {
    memcpy(p, &v, 4);                   // 8-bit --> 32-bit little-endian
}

void xchacha_hchacha20(uint8_t *out, const uint8_t *in, const uint8_t *k){
    int i;
    uint32_t x[16];

    x[0] = 0x61707865;                  // XChaCha Constant
    x[1] = 0x3320646e;
    x[2] = 0x79622d32;
    x[3] = 0x6b206574;

    for (i = 0; i < 4; i++){
        x[i+ 4] = u8tou32(&k[i*4]);
        x[i+ 8] = u8tou32(&k[i*4+16]);
        x[i+12] = u8tou32(&in[i*4]);
    }
    doRounds(x);
    for (i = 0; i < 4; i++){
        u32tou8(out + i*4, x[i]);
        u32tou8(out + i*4 + 16, x[i+12]);
    }
}

void xchacha_init(xChaCha_ctx *ctx, const uint8_t *k, uint8_t *iv){
    /* The sub-key to use */
    uint8_t k2[32];
    int i;
    xchacha_hchacha20(k2, iv, k);
    ctx->input[0] = 0x61707865;
    ctx->input[1] = 0x3320646e;
    ctx->input[2] = 0x79622d32;
    ctx->input[3] = 0x6b206574;
    for (i = 0; i < 8; i++) {   // load the key
        ctx->input[i + 4] = u8tou32(&k2[i*4]);
    }
    ctx->input[12] = 0;         /* Internal counter */
    ctx->input[13] = 0;         /* Internal counter */
    ctx->input[14] = u8tou32(iv + 16);
    ctx->input[15] = u8tou32(iv + 20);
    ctx->chaptr = 64;
    ctx->blox = 0;
}

void xchacha_set_counter(xChaCha_ctx *ctx, uint8_t *counter){
    ctx->input[12] = u8tou32(&counter[0]);
    ctx->input[13] = u8tou32(&counter[4]);
}

uint8_t xchacha_next(xChaCha_ctx *ctx){
    if (ctx->chaptr > 63) {
        ctx->chaptr = 0;
        uint32_t x[16], j[16];
        memcpy(j, &ctx->input, 64);
        memcpy(x, j, 64);
        doRounds(x);
        for (int i = 0; i < 16; i++) {
            x[i] += j[i];
        }
        memcpy(ctx->chabuf, x, 64);
        j[12]++;
        if (!j[12]) j[13]++;
        ctx->input[12] = j[12];
        ctx->input[13] = j[13];
    }
    return ctx->chabuf[ctx->chaptr++];
}

void xchacha_encrypt_bytes(xChaCha_ctx *ctx, const uint8_t *m, uint8_t *c, uint32_t bytes){
    while (bytes--) {
        *c++ = *m++ ^ xchacha_next(ctx);
    }
}

void xchacha_decrypt_bytes(xChaCha_ctx *ctx, const uint8_t *c, uint8_t *m, uint32_t bytes){
    xchacha_encrypt_bytes(ctx,c,m,bytes);
}

/* ------------------------------------------------------------------------- */

// A more AES/SM4-like API abstraction

void xc_crypt_init(xChaCha_ctx *ctx, const uint8_t *key, const uint8_t *iv) {
    uint8_t nonce[24];
    memset(nonce, 0, 24);
    memcpy(nonce, iv, 16); // use 128 bits of the possible 192
    xchacha_init(ctx, key, nonce);
}
void xc_crypt_init_g(size_t *ctx, const uint8_t *key, const uint8_t *iv) {
    xc_crypt_init((void *)ctx, key, iv);
}

void xc_crypt_block(xChaCha_ctx *ctx, const uint8_t *in, uint8_t *out, int mode) {
    ctx->blox++;
    xchacha_encrypt_bytes(ctx, in, out, 16);
}
void xc_crypt_block_g(size_t *ctx, const uint8_t *in, uint8_t *out, int mode) {
    xc_crypt_block((void *)ctx, in, out, mode);
}
