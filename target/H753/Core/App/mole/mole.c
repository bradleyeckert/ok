/*
Original project: https://github.com/bradleyeckert/mole
AEAD-secured ports (for UARTs, etc.)
*/

#include <stdint.h>
#include <string.h>
#include "xchacha.h"
#include "blake2s.h"
#include "mole.h"
#include "moleconfig.h"

#define ALLOC_HEADROOM (MOLE_ALLOC_MEM_UINT32S - allocated_uint32s)

#ifndef MOLE_TRACE
#define MOLE_TRACE 0
#endif

#if (MOLE_TRACE)
#include <stdio.h>
static void DUMP(const uint8_t *src, uint8_t len) {
    if (MOLE_TRACE > 1) {
        for (uint8_t i = 0; i < len; i++) {
            if ((i % 32) == 0) printf("\n___");
            printf("%02X ", src[i]);
        }
        printf("<- ");
    }
}
#define PRINTF  if (MOLE_TRACE > 1) printf
#define PRINTf  printf
#else
static void DUMP(const uint8_t *src, uint8_t len) {}
#define PRINTF(...) do { } while (0)
#define PRINTf PRINTF
#endif

#define BLOCK_SHIFT 6
#define CTX (void *)&*ctx
#define BeginHash ctx->hInitFn
#define EndHash ctx->hFinalFn
#define Hash ctx->hputcFn
#define BeginCipher ctx->cInitFn
#define TX ctx->ciphrFn
#define BlockCipher ctx->cBlockFn

// -----------------------------------------------------------------------------
// Stack for contexts whose size is unknown until run time
// Allocate is used at startup.

static uint32_t context_memory[MOLE_ALLOC_MEM_UINT32S];
static int allocated_uint32s;

static void* Allocate(int bytes) {
	void* r = &context_memory[allocated_uint32s];
	allocated_uint32s += ((bytes + 3) >> 2);
	return r;
}

// Key management

static const uint8_t KDFhashKey[] = KDF_PASS;

static int testHMAC(port_ctx *ctx, const uint8_t *buf) {
    if (memcmp(ctx->hmac, buf, MOLE_HMAC_LENGTH)) return MOLE_ERROR_BAD_HMAC;
    return 0;
}

static int testKey(port_ctx *ctx, const uint8_t *key) {
    BeginHash(CTX->rhCtx, KDFhashKey, MOLE_HMAC_LENGTH, 0);
        DUMP(&key[0], MOLE_PASSCODE_HMAC);
        PRINTF("keyset data\n");
    for (int i=0; i < MOLE_PASSCODE_HMAC; i++) {
        Hash(CTX->rhCtx, key[i]);
    }
    EndHash(CTX->rhCtx, ctx->hmac);
        DUMP(ctx->hmac, MOLE_HMAC_LENGTH);
        PRINTF("expected key hmac");
        DUMP(&key[MOLE_PASSCODE_HMAC], MOLE_HMAC_LENGTH);
        PRINTF("actual key hmac\n");
    return testHMAC(ctx, &key[MOLE_PASSCODE_HMAC]);
}

// Send raw binary out to the stream. Certain bytes are replaced by escape
// sequences so MOLE_TAG_END is not streamed out by accident.

static void SendByteU(port_ctx *ctx, uint8_t c) {
    if ((c & 0xFE) == MOLE_TAG_END) {         // MOLE_TAG_END or MOLE_ESCAPE
        TX(MOLE_ESCAPE);
        TX(c & 1);
        ctx->counter++;
    } else {
        TX(c);
    }
    ctx->counter++;
}

static void SendByte(port_ctx *ctx, uint8_t c) {
    SendByteU(ctx, c);
    Hash(CTX->thCtx, c);                        // add to HMAC
}

static void SendN(port_ctx *ctx, const uint8_t *src, int length) {
    for (int i = 0; i < length; i++) {
        SendByte(ctx, src[i]);
    }
}

static void Send2(port_ctx *ctx, int x) {
    SendByte(ctx, x) ;                          // RX buffer size[2]
    SendByte(ctx, x >> 8) ;
}

static void SendBlock(port_ctx *ctx, const uint8_t *src) {
    SendN(ctx, src, MOLE_BLOCKSIZE);
}

static void SendEnd(port_ctx *ctx) {            // send END tag
    ctx->counter++;
    TX(MOLE_TAG_END);
}

static void SendBoiler(port_ctx *ctx) {         // send boilerplate packet
    uint8_t len = ctx->boilerplate[0];
    SendByteU(ctx, MOLE_TAG_BOILERPLATE);
    for (int i = 0; i <= len; i++) SendByteU(ctx, ctx->boilerplate[i]);
    SendByteU(ctx, 0);                          // zero-terminate to stringify
    SendEnd(ctx);
}

// Encryption

static const uint8_t BISThmac[16] = {
    0xF2, 0x27, 0xE9, 0x62, 0x94, 0x7A, 0xAB, 0xE5,
    0xA7, 0x05, 0x88, 0x2A, 0xCF, 0xB3, 0x04, 0x82};

static const uint8_t BISTdecode[16] = {
    0xBC, 0xD0, 0x2A, 0x18, 0xBF, 0x3F, 0x01, 0xD1,
    0x92, 0x92, 0xDE, 0x30, 0xA7, 0xA8, 0xFD, 0xAC};

static int BIST(port_ctx *ctx, int protocol) {  // with keys and rxbuf = 0
    BeginHash  (CTX->rhCtx, ctx->hmackey, MOLE_HMAC_LENGTH, 0);
    BeginCipher(CTX->rcCtx, ctx->cryptokey, ctx->rxbuf, 0);
    BlockCipher(CTX->rcCtx, ctx->rxbuf, ctx->rxbuf, 0);
    if (memcmp(BISTdecode, ctx->rxbuf, MOLE_BLOCKSIZE)) return MOLE_ERROR_BAD_BIST;
    for (int i = 0; i < MOLE_BLOCKSIZE; i++) {
        Hash(CTX->rhCtx, ctx->rxbuf[i]);
    }
    EndHash(CTX->rhCtx, ctx->rxbuf);
    if (memcmp(BISThmac, ctx->rxbuf, MOLE_HMAC_LENGTH)) return MOLE_ERROR_BAD_BIST;
    return 0;
}

static void SendTxBuf(port_ctx *ctx) {
    BlockCipher(CTX->tcCtx, ctx->txbuf, ctx->txbuf, 0);
    SendBlock(ctx, ctx->txbuf);
}

static void SendHeader(port_ctx *ctx, int tag) {
    SendEnd(ctx);                               // reset state in case of oops
    BeginHash(CTX->thCtx, ctx->hmackey, MOLE_HMAC_LENGTH, ctx->hashCounterTX);
    SendByte(ctx, tag);                         // Header consists of a TAG byte,
}

#define ivADlength  2                           /* Associated data length */

static void SendAsHash(port_ctx *ctx, uint8_t *src) {
    for (int i = 0; i < MOLE_HMAC_LENGTH; i++) SendByteU(ctx, *src++);
    SendEnd(ctx);
}

static void SendTxHash(port_ctx *ctx, int pad){ // finish authenticated packet
    uint8_t hash[MOLE_HMAC_LENGTH];
        DUMP((uint8_t*)&ctx->hashCounterTX, 8);
        PRINTF("%s is sending HMAC with hashCounterTX, ", ctx->name);
    EndHash(CTX->thCtx, hash);
    ctx->hashCounterTX++;
    TX(MOLE_ESCAPE);                            // HMAC marker (in plaintext)
    TX(MOLE_HMAC_TRIGGER);
    ctx->counter += ivADlength;
    SendAsHash(ctx, hash);
    if (pad) {
        while (ctx->counter & (pad - 1)) {      // pad until next pad-byte boundary
            ctx->counter++;
            TX(0);
        }
    }
    SendEnd(ctx);
}

// Send: Tag[1], mIV[], cIV[], RXbufsize[2], HMAC[]
static int SendIV(port_ctx *ctx, int tag) {     // send random IV with random IV
    uint8_t mIV[MOLE_IV_LENGTH];                // using these instead of txbuf
    uint8_t cIV[MOLE_IV_LENGTH];                // to allow for re-transmission
    int r = 0;
    int c;
    for (int i = 0; i < MOLE_IV_LENGTH ; i++) {
        c = moleTRNG();  r |= c;  mIV[i] = (uint8_t)c;
        c = moleTRNG();  r |= c;  cIV[i] = (uint8_t)c;
        if (r < 0) {
            return MOLE_ERROR_TRNG_FAILURE;
        }
    }
    memcpy(&ctx->hashCounterRX, cIV, 8);
        PRINTf("\n%s sending IV, tag=%d, ", ctx->name, tag);
    SendHeader(ctx, tag);                       // TAG (also resets HMAC)
#if (MOLE_IV_LENGTH == MOLE_BLOCKSIZE)
    SendBlock(ctx, mIV);
#else
    SendN(ctx, mIV, MOLE_IV_LENGTH);
#endif
        DUMP((uint8_t*)&ctx->hashCounterRX, 8); PRINTF("New %s.hashCounterRX",ctx->name);
        DUMP((uint8_t*)&ctx->hashCounterTX, 8); PRINTF("Current %s.hashCounterTX",ctx->name);
        DUMP((uint8_t*)mIV, MOLE_IV_LENGTH);    PRINTF("mIV used by %s to encrypt cIV",ctx->name);
        DUMP((uint8_t*)cIV, MOLE_IV_LENGTH);    PRINTF("IV (not output)");
    BeginCipher(CTX->tcCtx, ctx->cryptokey, mIV, 1); // begin encryption with plaintext IV
    BlockCipher(CTX->tcCtx, cIV, mIV, 1);       // replace mIV with encrypted cIV
        DUMP((uint8_t*)mIV, MOLE_IV_LENGTH);    PRINTF("cIV output as encrypted IV\n");
#if (MOLE_IV_LENGTH == MOLE_BLOCKSIZE)
    SendBlock(ctx, mIV);
#else
    SendN(ctx, mIV, MOLE_IV_LENGTH);
#endif
    Send2(ctx, ctx->rBlocks);                   // RX buffer size[2]
    SendTxHash(ctx, MOLE_END_UNPADDED);         // HMAC
    BeginCipher(CTX->tcCtx, ctx->cryptokey, cIV, 1);
    ctx->tReady = 1;
    return 0;
}

// Arbitrary length message streaming is similar to file output.
// Do not guarantee delivery, just send and forget. This scheme assumes a host PC
// with a large rxbuf, so it will get the data. Otherwise, the HMAC is dropped.

static int NewStream(port_ctx *ctx, uint32_t headspace) {
    ctx->counter = 0;
    ctx->prevblock = 0;
    ctx->rReady = 0;
    ctx->tReady = 0;
    ctx->hashCounterTX = 0;
    SendBoiler(ctx);                            // include ID information for keying
    while (ctx->counter < headspace) {          // reserve room for control block
        SendByteU(ctx, 0xFF);
    }
    int r = SendIV(ctx, MOLE_TAG_IV_A);         // and an encrypted IV
    return r;
}

int moleTxInit(port_ctx *ctx) {                 // use if not paired
    return NewStream(ctx, 0);
}

static void moleSendInit(port_ctx *ctx, uint8_t type) {
    SendHeader(ctx, MOLE_TAG_MESSAGE);
    ctx->txbuf[0] = type;
    ctx->txidx = 1;
}

static void moleSendChar(port_ctx *ctx, uint8_t c) {
    int i = ctx->txidx;
    ctx->txbuf[i] = c;
    i = (i + 1) & 0x0F;
    ctx->txidx = i;
    if (!i) SendTxBuf(ctx);
}

static void moleSendFinal(port_ctx *ctx) {
    ctx->txbuf[15] = ctx->txidx;
    SendTxBuf(ctx);
    SendTxHash(ctx, MOLE_END_UNPADDED);
}

static void moleSendMsg(port_ctx *ctx, const uint8_t *src, int len, int type) {
    moleSendInit(ctx, type);
    while (len--) moleSendChar(ctx, *src++);
    moleSendFinal(ctx);
}


// Encrypt and send a passcode
static int moleReKeyRequest(port_ctx *ctx, const uint8_t *key, int tag){
    if (moleAvail(ctx) < MOLE_PASSCODE_LENGTH) return MOLE_ERROR_MSG_NOT_SENT;
    moleSendMsg(ctx, key, MOLE_PASSCODE_LENGTH, tag);
    return 0;
}

// Derive various keys from the system passcode
// If KDFhashKey is not really secret, an attecker can get the keys from the passcode.
// However, the system passcode cannot be obtained from the keys. Hash is one-way.

static int KDF (port_ctx *ctx, uint8_t *dest, const uint8_t *src, int length,
                int iterations, int reverse) {
    uint8_t KDFbuffer[MOLE_ENCR_KEY_LENGTH];
    if (length > MOLE_ENCR_KEY_LENGTH) return MOLE_ERROR_KDFBUF_TOO_SMALL;
    for (int i = 0; i < length; i++) {
        if (reverse) KDFbuffer[i] = src[length + (~i)];
        else         KDFbuffer[i] = src[i];
    }
    while (iterations--) {                      // hash the KDFbuffer multiple times
        BeginHash(CTX->rhCtx, KDFhashKey, length, 0);
        for (int i = 0; i < length; i++) {
            Hash(CTX->rhCtx, KDFbuffer[i]);
        }
        EndHash(CTX->rhCtx, KDFbuffer);
    }
    memcpy(dest, KDFbuffer, length);
        DUMP(KDFbuffer, length); PRINTF("KDF output ");
    return 0;
}

#define PREAMBLE_SIZE 2
#define MAX_RX_LENGTH ((ctx->rBlocks << BLOCK_SHIFT) - (MOLE_HMAC_LENGTH + PREAMBLE_SIZE))

// -----------------------------------------------------------------------------
// Public functions

int moleNewKeys(port_ctx *ctx, const uint8_t *key) {
    int r = testKey(ctx, key);
    if (r) return r;
    r |= KDF(ctx, ctx->hmackey,       key, MOLE_HMAC_KEY_LENGTH, 55, 0);
    r |= KDF(ctx, ctx->cryptokey,     key, MOLE_ENCR_KEY_LENGTH, 55, 1);
    r |= KDF(ctx, ctx->adminpasscode, &key[32], MOLE_ADMINPASS_LENGTH, 34, 0);
    return r;
}

// Call this before setting up any mole ports and when closing app.
void moleNoPorts(void) {
	memset(context_memory, 0, sizeof(context_memory));
	allocated_uint32s = 0;
}

// Add a secure port
int moleAddPort(port_ctx *ctx, const uint8_t *boilerplate, int protocol,
                const char* name, uint16_t rxBlocks, mole_boilrFn boiler,
                mole_plainFn plain, mole_ciphrFn ciphr, mole_WrKeyFn WrKeyFn) {
    memset(ctx, 0, sizeof(port_ctx));
    ctx->plainFn = plain;                       // plaintext output handler
    TX = ciphr;                                 // ciphertext output handler
    ctx->boilrFn = boiler;                      // boilerplate output handler
    ctx->boilerplate = boilerplate;             // counted string
    ctx->name = name;                           // Zstring name for debugging
    ctx->WrKeyFn = WrKeyFn;
    ctx->rBlocks = rxBlocks;                    // block size (1<<BLOCK_SHIFT) bytes
    ctx->rxbuf = Allocate(rxBlocks << BLOCK_SHIFT);
    if (rxBlocks < 2) return MOLE_ERROR_BUF_TOO_SMALL;
    switch (protocol) {
    default: // 0
        ctx->rcCtx = Allocate(sizeof(xChaCha_ctx));
        ctx->tcCtx = Allocate(sizeof(xChaCha_ctx));
        ctx->rhCtx = Allocate(sizeof(blake2s_state));
        ctx->thCtx = Allocate(sizeof(blake2s_state));
        if (allocated_uint32s >= MOLE_ALLOC_MEM_UINT32S) return MOLE_ERROR_OUT_OF_MEMORY;
        BeginHash   = b2s_hmac_init_g;
        Hash        = b2s_hmac_putc_g;
        EndHash     = b2s_hmac_final_g;
        BeginCipher = xc_crypt_init_g;
        BlockCipher = xc_crypt_block_g;
    }
    return BIST(ctx, protocol);
}

int moleRAMused (int ports) {
    return sizeof(uint32_t) * allocated_uint32s + ports * sizeof(port_ctx);
}

int moleRAMunused (void) {
    return sizeof(uint32_t) * ALLOC_HEADROOM;
}

// Encrypt and send a key set
int moleReKey(port_ctx *ctx, const uint8_t *key){
    return moleReKeyRequest(ctx, key, MOLE_MSG_NEW_KEY);
}

// molePair and moleBoilerReq assume that the FSMs are not seeing traffic

void molePair(port_ctx *ctx) {
    PRINTf("\n%s sending Pairing request, ", ctx->name);
    ctx->rReady = 0;
    ctx->tReady = 0;
    ctx->state = IDLE;                          // reset local FSM
    SendHeader(ctx, MOLE_TAG_RESET);
    SendEnd(ctx);
}

void moleBoilerReq(port_ctx *ctx) {
    PRINTf("\n%s sending Boilerplate request, ", ctx->name);
    ctx->state = IDLE;                          // reset local FSM
    SendHeader(ctx, MOLE_TAG_GET_BOILER);
    SendEnd(ctx);
}

// Send: Tag[1], password[16], HMAC[]
void moleAdmin(port_ctx *ctx) {
    uint8_t m[MOLE_ADMINPASS_LENGTH];
    PRINTf("\n%s sending Admin passcode, ", ctx->name);
    SendHeader(ctx, MOLE_TAG_ADMIN);
    BlockCipher(CTX->tcCtx, ctx->adminpasscode, m, 0);
    SendBlock(ctx, m);
    SendTxHash(ctx, MOLE_END_UNPADDED);
}

// Size of message available to accept
uint32_t moleAvail(port_ctx *ctx){
    if (!ctx->rReady) return 0;
    if (!ctx->tReady) return 0;
    return (ctx->avail << BLOCK_SHIFT) - (MOLE_HMAC_LENGTH + PREAMBLE_SIZE);
}

// -----------------------------------------------------------------------------
// Receive char or command from input stream
int molePutc(port_ctx *ctx, uint8_t c){
    int r = 0;
    int temp;
    uint8_t *k;
    // Pack escape sequence to binary ------------------------------------------
    int ended = (c == MOLE_TAG_END);            // distinguish '0A' from '0B 02'
    if (ctx->escaped) {
        ctx->escaped = 0;
        if (c > 1) switch(c) {
            case MOLE_HMAC_TRIGGER:
                EndHash(CTX->rhCtx, ctx->hmac);
                    DUMP((uint8_t*)&ctx->hashCounterRX, 8);
                    PRINTF("%s receiving HMAC with hashCounterRX, ", ctx->name);
                ctx->hashCounterRX++;
                ctx->MACed = 1;
                return 0;
            default:                            // embedded reset
                ctx->state = IDLE;
                molePair(ctx);
                return 0;
        } else {
        c += MOLE_TAG_END;
        }
    }
    else if (c == MOLE_ESCAPE) {
        ctx->escaped = 1;
        return 0;
    }
    // FSM ---------------------------------------------------------------------
    Hash(CTX->rhCtx, c);      // add to hash
    int i = ctx->ridx;
    switch (ctx->state) {
    case IDLE:
        if (c < MOLE_TAG_GET_BOILER) break;     // limit range of valid tags
        if (c > MOLE_TAG_ADMIN)      break;
        if (c == MOLE_TAG_IV_A) {
            ctx->hashCounterRX = 0;             // before initializing the hash
            ctx->rReady = 0;
            ctx->tReady = 0;
        }
        BeginHash(CTX->rhCtx, ctx->hmackey, MOLE_HMAC_LENGTH, ctx->hashCounterRX);
        Hash(CTX->rhCtx, c);
        ctx->tag = c;
        ctx->MACed = 0;
        ctx->state = DISPATCH;
        break;
    case DISPATCH: // message data begins here
        ctx->rxbuf[0] = c;
        ctx->ridx = 1;
        ctx->state = GET_PAYLOAD;
            PRINTF("\n%s incoming packet, tag=%d\n", ctx->name, ctx->tag);
        switch (ctx->tag) {
        case MOLE_TAG_GET_BOILER:
            SendBoiler(ctx);
            ctx->state = IDLE;
            break;
        case MOLE_TAG_RESET:
            ctx->hashCounterTX = 0;
            ctx->state = IDLE;
            r = SendIV(ctx, MOLE_TAG_IV_A);
            break;
        case MOLE_TAG_BOILERPLATE:
            ctx->state = GET_BOILER;
            break;
        case MOLE_TAG_IV_A:
        case MOLE_TAG_IV_B:
            ctx->state = GET_IV;
            break;
        }
        if (ended) ctx->state = IDLE;
        break;
    case HANG:                                  // wait for end token
noend:  if (ended) {                            // premature end not allowed
            ctx->state = IDLE;
            PRINTf("\nHANG state ");
            r = MOLE_ERROR_INVALID_LENGTH;
        }
        break;
    case GET_IV:
        ctx->rxbuf[ctx->ridx++] = c;
        if (ctx->ridx == MOLE_IV_LENGTH) {
            PRINTf("\nSet temporary IV for decrypting the secret IV ");
            BeginCipher(CTX->rcCtx, ctx->cryptokey, ctx->rxbuf, 0);
            ctx->state = GET_PAYLOAD;
        }
        goto noend;
    case GET_BOILER:
        if (i == MAX_RX_LENGTH) {
            r = MOLE_ERROR_LONG_BOILERPLT;
            ended = 1;
        }
        if (ended) {
            if ((i - 2) == ctx->rxbuf[0])
            ctx->boilrFn(ctx->rxbuf);
            ctx->state = IDLE;
        } else {
            ctx->rxbuf[ctx->ridx++] = c;
        }
        break;
    case GET_PAYLOAD:
        if (!ended) {                           // input terminated by end token
            if (i != (ctx->rBlocks << BLOCK_SHIFT)) {
                ctx->rxbuf[ctx->ridx++] = c;
                temp = ctx->ridx;
                if (!ctx->MACed && !(temp & (MOLE_BLOCKSIZE - 1))) {
                    temp -= MOLE_BLOCKSIZE;     // -> beginning of block
                   PRINTF("\n%s decrypting payload rxbuf[%d]; ", ctx->name, temp);
                    BlockCipher(CTX->rcCtx, &ctx->rxbuf[temp], &ctx->rxbuf[temp], 1);
                }
            } else {
                ctx->state = HANG;
                PRINTf("\nGET_PAYLOAD state ");
                r = MOLE_ERROR_INVALID_LENGTH;
            }
            break;
        }
        ctx->state = IDLE;
        temp = i - MOLE_HMAC_LENGTH;
        c = ctx->rxbuf[0];                      // repurpose c
        r = testHMAC(ctx, &ctx->rxbuf[temp]);   // 0 if okay, else bad HMAC
       PRINTF("\n%s received packet of length %d, tag %d, rxbuf[0]=0x%02X; ",
       ctx->name, temp, ctx->tag, c);
        if (r) {
            PRINTf("\n**** Bad HMAC ****");
        }
        switch (ctx->tag) {
        case MOLE_TAG_IV_A:
            ctx->tReady = 0;
            ctx->hashCounterTX = 0;
        case MOLE_TAG_IV_B:
            ctx->rReady = 0;
            if (r) break;
            if (temp != (2 * MOLE_IV_LENGTH + ivADlength)) {
                PRINTf("\nIV length was funny ");
                r = MOLE_ERROR_INVALID_LENGTH;
                break;
            }
            BeginCipher(CTX->rcCtx, ctx->cryptokey, &ctx->rxbuf[MOLE_IV_LENGTH], 0);
            memcpy(&ctx->hashCounterTX, &ctx->rxbuf[MOLE_IV_LENGTH], 8);
            memcpy(&ctx->avail, &ctx->rxbuf[2*MOLE_IV_LENGTH], 2);
            ctx->rReady = 1;
                PRINTf("\nReceived IV, tag=%d; ", ctx->tag);
                DUMP((uint8_t*)&ctx->hashCounterRX, 8);
                PRINTF("Received HMAC hashCounterRX, ");
                DUMP((uint8_t*)&ctx->rxbuf[MOLE_IV_LENGTH], MOLE_IV_LENGTH);
                PRINTF("Private cIV, ");
            if (ctx->tag == MOLE_TAG_IV_A) {
                r = SendIV(ctx, MOLE_TAG_IV_B);
            }
            break;
        case MOLE_TAG_ADMIN:
            ctx->adminOK = 0;
            if (r) break;
                DUMP(ctx->adminpasscode, MOLE_ADMINPASS_LENGTH);
                PRINTF("Expected Passcode");
                DUMP(ctx->rxbuf, MOLE_ADMINPASS_LENGTH);
                PRINTF("Actual Passcode");
            if (memcmp(ctx->rxbuf, ctx->adminpasscode, MOLE_ADMINPASS_LENGTH) == 0)
                ctx->adminOK = MOLE_ADMIN_ACTIVE;
            break;
        case MOLE_TAG_MESSAGE:
            if (r) {
                molePair(ctx);                  // assume synchronization is lost
            } else {
                switch(c) {
                case MOLE_MSG_MESSAGE:
                    i = ctx->rxbuf[temp - 1];   // remainder
                    temp = temp + i - 17;       // trim padding
                    ctx->plainFn(&ctx->rxbuf[1], temp);
                    memset(&ctx->rxbuf[1], 0, temp); // burn after reading
                    break;
                case MOLE_MSG_NEW_KEY:
                case MOLE_MSG_REKEYED:
                    PRINTf("\n%s is testing the new key", ctx->name);
                    temp = testKey(ctx, &ctx->rxbuf[1]);
                    if (temp) return temp;      // bad key
                    k = ctx->WrKeyFn(&ctx->rxbuf[1]);
                    if (k == NULL) return 0;    // no key
                    if (c == MOLE_MSG_NEW_KEY) {
                        moleReKeyRequest(ctx, k, MOLE_MSG_REKEYED);
                    }
                    moleNewKeys(ctx, k);        // re-key locally
                    PRINTf("\n%s has been re-keyed", ctx->name);
                    if (r) return r;
                    r = MOLE_ERROR_REKEYED;     // say "you've been re-keyed"
                    break;
                default:
                    r = 0;
                }
            }
            break;
        default: break;
        }
        break;
    default:
        ctx->state = IDLE;
        r = MOLE_ERROR_INVALID_STATE;
    }
    return r;
}

// -----------------------------------------------------------------------------
// File output: Init to start a packet, Out to append blocks, Final to finish.

static void moleFileInit (port_ctx *ctx) {
    SendHeader(ctx, MOLE_TAG_RAWTX);
    SendByte(ctx, MOLE_ANYLENGTH);
}

int moleFileNew(port_ctx *ctx) {                // start a new one-way message
    int r = NewStream(ctx, 0);
    BeginHash(CTX->rhCtx, ctx->hmackey, MOLE_HMAC_LENGTH, ctx->hashCounterRX);
        DUMP((uint8_t*)&ctx->hashCounterRX, 8);  PRINTF("Overall hash ctr");
    ctx->hashCounterTX = ctx->hashCounterRX + 1;
    moleFileInit(ctx);                          // get ready to write blocks
    return r;
}

void moleFileFinal (port_ctx *ctx) {            // end the one-way message
    SendTxHash(ctx, 0);                         // finish last chunk
    SendEnd(ctx);
    SendByteU(ctx, MOLE_TAG_EOF);
    EndHash(CTX->rhCtx, ctx->hmac);
    SendAsHash(ctx, ctx->hmac);                 // send overall hash
}

// Note: len must be a multiple of 16.
void moleFileOut (port_ctx *ctx, const uint8_t *src, int len) {
    while (len > 0) {
        memcpy(ctx->txbuf, src, MOLE_BLOCKSIZE);
        for (int i = 0; i < MOLE_BLOCKSIZE; i++) {
            Hash(CTX->rhCtx, src[i]);           // overall hash uses rx chan
        }
        SendTxBuf(ctx);
        src += MOLE_BLOCKSIZE;
        len -= MOLE_BLOCKSIZE;
        uint32_t p = ctx->counter + 2 * MOLE_HMAC_LENGTH + 3;
        uint8_t block = (uint8_t)(p >> MOLE_FILE_CHUNK_SIZE_LOG2);
        if (ctx->prevblock != block) {
            ctx->prevblock = block;
            SendTxHash(ctx, MOLE_END_PADDED);
            moleFileInit(ctx);                  // restart block if too long
        }
    }
}

int moleSend(port_ctx *ctx, const uint8_t *src, int len) {
    moleSendMsg(ctx, src, len, MOLE_MSG_MESSAGE);
    return 0;
}

// -----------------------------------------------------------------------------
// File input: Decrypt and authenticate
// This is usually done in two passes. The first pass only authenticates.
// The second pass decrypts and authenticates.

static mole_inFn  inFn;
static int done;

#if (MOLE_TRACE)
static uint32_t position;
#endif

static uint8_t NextByte(void) {
    int c = inFn();
    if (c < 0) {
        done = 1;
        return MOLE_TAG_END;
    }
#if (MOLE_TRACE)
    position++;
#endif
    return (uint8_t)c;
}

static uint8_t SkipChars(uint8_t c) {           // skip run of chars
    uint8_t n = c;
    while ((n == c) && (!done)) {
        n = NextByte();
    }
    return n;
}

static void FindEndTag(void) {                  // skip to 1st byte after end tag
    uint8_t c;
    do {
        c = NextByte();                         // .. .. .. 0A .. ..
        if (done) return;                       //             ^-- position
    } while (c != MOLE_TAG_END);
}

static int SkipEndTags(int n) {                 // expect n sequential end tags
    int r = 0;
    while (n--) {
        uint8_t c = NextByte();
        if (c != MOLE_TAG_END) return 1;
    }
    return r;
}

#define HMAC_TAG 0x100
#define DONE_TAG 0x400

static int NextChar(port_ctx *ctx) {
    uint8_t c = NextByte();
    if (c == MOLE_ESCAPE) {
        c = NextByte();
        switch(c) {
        case 0:
        case 1: c += MOLE_TAG_END;
            break;
        case MOLE_HMAC_TRIGGER:
            EndHash(CTX->rhCtx, ctx->hmac);
            ctx->hashCounterRX++;
            return HMAC_TAG;
        default:
            done = 1;
        }
    }
    if (done) return DONE_TAG;
    Hash(CTX->rhCtx, c);
    return c;
}

#define RX NextChar(ctx)
#define mIV ctx->rxbuf
#define cIV &ctx->rxbuf[MOLE_IV_LENGTH]

static int NextBlock(port_ctx *ctx, uint8_t *dest) {
    for (int i = 0; i < MOLE_BLOCKSIZE; i++) {
        int c = RX;
        if (c & HMAC_TAG) return i;             // return bytes read before HMAC
        *dest++ = c;
    }
    return MOLE_BLOCKSIZE;
}

int moleFileIn (port_ctx *ctx, mole_inFn cFn, mole_outFn mFn) {
    done = 0;
#if (MOLE_TRACE)
    position = 0;
#endif
    inFn = cFn;
        PRINTf("\n%s decrypting input stream c, producing output stream m", ctx->name);
    if (mFn == NULL) PRINTf("\nAuthenticate Only");
    FindEndTag();                               // skip boilerplate
    int c = SkipChars(0xFF);                    // skip blanks, if there are any
        c = SkipChars(MOLE_TAG_END);            // skip end tags
    if (c != MOLE_TAG_IV_A) return MOLE_ERROR_MISSING_IV;
        PRINTf("\nIV tag is at position 0x%x ", position - 1);
    ctx->hashCounterRX = 0;
    BeginHash  (CTX->rhCtx, ctx->hmackey, MOLE_HMAC_LENGTH, 0);
    Hash(CTX->rhCtx, c);                        // hash includes the tag
    NextBlock  (ctx, mIV);
        DUMP(mIV, MOLE_HMAC_LENGTH); PRINTF("mIV read");
    BeginCipher(CTX->rcCtx, ctx->cryptokey, mIV, 0);
    NextBlock  (ctx, mIV);
        DUMP(mIV, MOLE_HMAC_LENGTH); PRINTF("cIV read");
    RX; RX;                                     // skip avail field
    BlockCipher(CTX->rcCtx, mIV, cIV, 0);
    memcpy(&ctx->hashCounterRX, cIV, 8);
        DUMP(cIV, MOLE_HMAC_LENGTH); PRINTF("IV calculated\n");
        DUMP((uint8_t*)&ctx->hashCounterRX, 8); PRINTF("hashCounterRX ");
    BeginHash  (CTX->thCtx, ctx->hmackey, MOLE_HMAC_LENGTH, ctx->hashCounterRX);
    BeginCipher(CTX->rcCtx, ctx->cryptokey, cIV, 0);
    if (NextBlock(ctx, mIV)) return MOLE_ERROR_MISSING_HMAC;
        PRINTf("\nIV HMAC is at position 0x%x ", position);
    NextBlock  (ctx, mIV);
    if (testHMAC(ctx, mIV)) {
badmac: DUMP(ctx->hmac, MOLE_HMAC_LENGTH); PRINTF("expected key hmac ");
        DUMP(mIV, MOLE_HMAC_LENGTH); PRINTF("actual key hmac\n");
        return MOLE_ERROR_BAD_HMAC;
    }
    if (SkipEndTags(3)) return MOLE_ERROR_BAD_END_RUN;
        PRINTf("\nRandom IV (nonce) has been set up and authenticated");
    ctx->chunks = 0;
    while(1) {
        BeginHash(CTX->rhCtx, ctx->hmackey, MOLE_HMAC_LENGTH, ctx->hashCounterRX);
        int n = RX;
        PRINTf("\nDecrypting the stream at position 0x%x, c=%d\n", position, n);
        if (n == MOLE_TAG_EOF)    break;
        if (n != MOLE_TAG_RAWTX)  return MOLE_ERROR_NO_RAWPACKET;
        if (RX != MOLE_ANYLENGTH) return MOLE_ERROR_NO_ANYLENGTH;
        while (1) {
            n = NextBlock(ctx, mIV);
            if (done)             return MOLE_ERROR_STREAM_ENDED;
            if (n < 16) break;                  // HMAC was captured
            BlockCipher(CTX->rcCtx, mIV, mIV, 0);
            for (int i=0; i<16; i++) {
                uint8_t c = mIV[i];
                Hash(CTX->thCtx, c);            // add plaintext to overall hash
                if (mFn != NULL) mFn(c);
            }
        }
        NextBlock(ctx, mIV);                    // get expected HMAC
        if (testHMAC(ctx, mIV)) goto badmac;
        if (SkipEndTags(1)) return MOLE_ERROR_BAD_END_RUN;
        SkipChars(0);                           // skip padding
        if (SkipEndTags(1)) return MOLE_ERROR_BAD_END_RUN;
        ctx->chunks++;
    }   PRINTf("\nEOF found at position 0x%x, %d chunks\n", position, ctx->chunks);
    EndHash(CTX->thCtx, ctx->hmac);
    NextBlock(ctx, mIV);
    if (testHMAC(ctx, mIV)) goto badmac;
    return 0;
}

