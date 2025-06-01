/*
 * Header file for the xChaCha encryption and SipHash keyed hash algorithms
 * Evolution:
 * Daniel J. Bernstein's ChaCha reference: http://cr.yp.to/chacha.html
 * xChaCha version: https://github.com/spcnvdr/xChaCha
 * This version: https://github.com/bradleyeckert/ychacha
 */
#include <stdint.h>

#ifndef _YCHACHA_H_
#define _YCHACHA_H_

/** Key and IV sizes that are supported by xChaCha.
 *  All sizes are in bits.
 */
#define NAME "xChaCha"
#define KEYSIZE 256                 /* 256-bits, 32 bytes */
#define BLOCKSIZE 512               /* 512-bits, 64 bytes */
#define IVSIZE 128                  /* 128-bits, 16 bytes */

/* The following macros are used to obtain exact-width results. */
#define U8V(v)  ((uint8_t)(v) & (0xFF))
#define U16V(v) ((uint16_t)(v) & (0xFFFF))
#define U32V(v) ((uint32_t)(v) & (0xFFFFFFFF))
#define U64V(v) ((uint64_t)(v) & (0xFFFFFFFFFFFFFFFF))

#define ROTL32(v, n) (U32V((v) << (n)) | ((v) >> (32 - (n))))

/** ChaCha_ctx is the structure containing the representation of the internal
 *  state of the xChaCha cipher. Typically 129 to 132 bytes.
 */

typedef struct
{   uint32_t input[16];     // state
    uint8_t chabuf[64];     // keystream buffer
    uint8_t chaptr;         // keystream pointer
    uint8_t blox;           // block counter
} xChaCha_ctx;

/* ------------------------------------------------------------------------- */

/** Encryption/decryption initialization
 * @param ctx   Encryption/Decryption context
 * @param key   Key, 32 bytes
 * @param iv    Initialization vector, 16 bytes
 * @param mode  1 for encryption, 0 for decryption
 */
void xc_crypt_init(xChaCha_ctx *ctx, const uint8_t *key, const uint8_t *iv, int mode);
void xc_crypt_init_g   (size_t *ctx, const uint8_t *key, const uint8_t *iv, int mode);

/** 16-byte block encryption/decryption
 * @param ctx   Encryption/Decryption context
 * @param in    16-byte buffer holding the input data
 * @param out   16-byte buffer holding the output data
 * @param mode  1 for encryption, 0 for decryption
 */
void xc_crypt_block(xChaCha_ctx *ctx, const uint8_t *in, uint8_t *out, int mode);
void xc_crypt_block_g   (size_t *ctx, const uint8_t *in, uint8_t *out, int mode);

// Classic functions for testing
void xchacha_hchacha20(uint8_t *out, const uint8_t *in, const uint8_t *k);
void xchacha_init(xChaCha_ctx *ctx, const uint8_t *k, uint8_t *iv);
void xchacha_set_counter(xChaCha_ctx *ctx, uint8_t *counter);
void xchacha_encrypt_bytes(xChaCha_ctx *ctx, const uint8_t *m, uint8_t *c, uint32_t bytes);
void xchacha_decrypt_bytes(xChaCha_ctx *ctx, const uint8_t *c, uint8_t *m, uint32_t bytes);

#endif // _YCHACHA_H_
