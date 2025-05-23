/* 4/27/25 BNE
   BLAKE2 reference source code package - reference C implementations

   Copyright 2012, Samuel Neves <sneves@dei.uc.pt>.  You may use this under the
   terms of the CC0, the OpenSSL Licence, or the Apache Public License 2.0, at
   your option.  The terms of these licenses can be found at:

   - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
   - OpenSSL license   : https://www.openssl.org/source/license.html
   - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0

   More information about the BLAKE2 hash function can be found at
   https://blake2.net.
*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "blake2s.h"

static const uint32_t blake2s_IV[8] =
{
  0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
  0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL
};

static const uint8_t blake2s_sigma[10][16] =
{
  {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 } ,
  { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 } ,
  { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 } ,
  {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 } ,
  {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 } ,
  {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 } ,
  { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 } ,
  { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 } ,
  {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 } ,
  { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13 , 0 } ,
};

static void blake2s_set_lastnode( blake2s_state *S )
{
  S->f[1] = (uint32_t)-1;
}

/* Some helper functions, not necessarily useful */
static int blake2s_is_lastblock( const blake2s_state *S )
{
  return S->f[0] != 0;
}

static void blake2s_set_lastblock( blake2s_state *S )
{
  if( S->last_node ) blake2s_set_lastnode( S );

  S->f[0] = (uint32_t)-1;
}

static void blake2s_increment_counter( blake2s_state *S, const uint32_t inc )
{
  S->t[0] += inc;
  S->t[1] += ( S->t[0] < inc );
}

static void blake2s_init0( blake2s_state *S )
{
  int i;
  memset( S, 0, sizeof( blake2s_state ) );

  for( i = 0; i < 8; ++i ) S->h[i] = blake2s_IV[i];
}

/* init2 xors IV with input parameter block */
static int blake2s_init_param( blake2s_state *S, const blake2s_param *P )
{
  const unsigned char *p = ( const unsigned char * )( P );
  int i;

  blake2s_init0( S );

  /* IV XOR ParamBlock */
  for( i = 0; i < 8; ++i )
    S->h[i] ^= load32( &p[i * 4] );

  S->outlen = P->digest_length;
  return 0;
}


static uint32_t m[16];
static uint32_t v[16];

static const uint8_t idxb[] = {4, 5, 6, 7, 5, 6, 7, 4};
static const uint8_t idxc[] = {8, 9, 10, 11, 10, 11, 8, 9};
static const uint8_t idxd[] = {12, 13, 14, 15, 15, 12, 13, 14};

// The hot spot, called 10 times per 64-byte block
static void hotround( int r )
{
  for( int i = 0; i < 8; ++i ) {
    uint8_t a = i & 3;
    uint8_t b = idxb[i];
    uint8_t c = idxc[i];
    uint8_t d = idxd[i];
    v[a] += v[b] + m[blake2s_sigma[r][2*i+0]];
    v[d] = rotr32(v[d] ^ v[a], 16);
    v[c] += v[d];
    v[b] = rotr32(v[b] ^ v[c], 12);
    v[a] += v[b] + m[blake2s_sigma[r][2*i+1]];
    v[d] = rotr32(v[d] ^ v[a], 8);
    v[c] += v[d];
    v[b] = rotr32(v[b] ^ v[c], 7);
  }
}

static void blake2s_compress( blake2s_state *S, const uint8_t in[BLAKE2S_BLOCKBYTES] )
{
  int i;

  for( i = 0; i < 16; ++i ) {
    m[i] = load32( in + i * sizeof( m[i] ) );
  }

  for( i = 0; i < 8; ++i ) {
    v[i] = S->h[i];
  }

  v[ 8] = blake2s_IV[0];
  v[ 9] = blake2s_IV[1];
  v[10] = blake2s_IV[2];
  v[11] = blake2s_IV[3];
  v[12] = S->t[0] ^ blake2s_IV[4];
  v[13] = S->t[1] ^ blake2s_IV[5];
  v[14] = S->f[0] ^ blake2s_IV[6];
  v[15] = S->f[1] ^ blake2s_IV[7];

  for (int r = 0; r < 10; r++) {
    hotround(r);
  }

  for( i = 0; i < 8; ++i ) {
    S->h[i] = S->h[i] ^ v[i] ^ v[i + 8];
  }
}

void b2s_hmac_putc(blake2s_state *S, uint8_t c) {
  if (S->buflen == BLAKE2S_BLOCKBYTES) {
    blake2s_increment_counter(S, BLAKE2S_BLOCKBYTES);
    blake2s_compress( S, S->buf );
    S->buflen = 0;
  }
  S->buf[S->buflen++] = c;
}
void b2s_hmac_putc_g(size_t *S, uint8_t c) {
  b2s_hmac_putc((void *)S, c);
}

int b2s_hmac_final( blake2s_state *S, uint8_t *out )
{
  uint8_t buffer[BLAKE2S_OUTBYTES] = {0};

  if( blake2s_is_lastblock( S ) ) return -1;

  blake2s_increment_counter( S, ( uint32_t )S->buflen );
  blake2s_set_lastblock( S );
  memset( S->buf + S->buflen, 0, BLAKE2S_BLOCKBYTES - S->buflen ); /* Padding */
  blake2s_compress( S, S->buf );

  for( int i = 0; i < 8; ++i ) /* Output full hash to temp buffer */
    store32( buffer + sizeof( S->h[i] ) * i, S->h[i] );

  memcpy( out, buffer, S->outlen );
  secure_zero_memory(buffer, sizeof(buffer));
  return 0;
}
int b2s_hmac_final_g(size_t *S, uint8_t *out) {
  return b2s_hmac_final((void *)S, out);
}


int b2s_hmac_puts( blake2s_state *S, const uint8_t *in, int inlen )
{
  while (inlen--) {
    b2s_hmac_putc( S, *in++ );
  }
  return 0;
}

int b2s_hmac_init(blake2s_state *S, const uint8_t *key, int hsize, uint64_t ctr)
{
  blake2s_param P[1];

  if ( ( !hsize ) || ( hsize > BLAKE2S_OUTBYTES ) ) return 0;

  P->digest_length = (uint8_t)hsize;
  P->key_length    = BLAKE2S_KEYBYTES;
  P->fanout        = 1;
  P->depth         = 1;
  store32( &P->leaf_length, 0 );
  store32( &P->node_offset, 0 );
  store16( &P->xof_length, 0 );
  P->node_depth    = 0;
  P->inner_length  = 0;
  /* memset(P->reserved, 0, sizeof(P->reserved) ); */
  memset( P->salt,     0, sizeof( P->salt ) );
  memset( P->personal, 0, sizeof( P->personal ) );

  if( blake2s_init_param( S, P ) < 0 ) return 0;
  S->t[0] ^= (uint32_t) ctr;
  S->t[1] ^= ctr >> 32;

  {
    uint8_t block[BLAKE2S_BLOCKBYTES];
    memset( block, 0, BLAKE2S_BLOCKBYTES );
    memcpy( block, key, BLAKE2S_KEYBYTES );
    b2s_hmac_puts( S, block, BLAKE2S_BLOCKBYTES );
    secure_zero_memory( block, BLAKE2S_BLOCKBYTES ); /* Burn the key from stack */
  }
  return S->outlen;
}
int b2s_hmac_init_g(size_t *S, const uint8_t *key, int hsize, uint64_t ctr) {
  return b2s_hmac_init((void *)S, key, hsize, ctr);
}
