#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "bci.h"
#include "bciHW.h"

#define TRACE 0

/*
BCIhandler takes input from a buffer and outputs to encrypted UART using these primitives:
void hermesSendInit(port_ctx *ctx, uint8_t tag);
void hermesSendChar(port_ctx *ctx, uint8_t c);
void hermesSendFinal(port_ctx *ctx);
which are late-bound in the port_ctx to decouple the BCI from its output stream.
*/

#if (TRACE)
#include <stdio.h>
void DUMP(const uint8_t *src, uint8_t len) {
    if (TRACE) {
        for (uint8_t i = 0; i < len; i++) {
            if ((i % 33) == 0) printf("\n___");
            printf("%02X ", src[i]);
        }
        printf("<- ");
    }
}
#define PRINTF  if (TRACE) printf
#else
void DUMP(const uint8_t *src, uint8_t len) {}
#define PRINTF(...) do { } while (0)
#endif

/*
Memory access is mediated by debugAccessFlags, which is 0 for production code.
Memory sections assume a 24-bit address space, where address units are cells.
String libraries in both C and Forth are overly simplistic, so if strings use
bytes the custom string functions would adapt as needed. Cell addressing was
always preferred by Chuck Moore.
*/

static uint32_t ReadCell(vm_ctx *ctx, uint32_t addr) {
    PRINTF("\nLoad [0x%x]", addr);
    if (addr < DATASIZE) return ctx->DataMem[addr];
    #if (BCI_DEBUG_ACCESS & BCI_ACCESS_CODESPACE)
        uint32_t a = addr - DATASIZE;
        if (a < CODESIZE) return ctx->CodeMem[a];
        if ((addr & ~0x3FFFF) == 0x040000) return BCIVMcodeRead(ctx, addr);
    #endif
    #if (BCI_DEBUG_ACCESS & BCI_ACCESS_PERIPHERALS)
        return BCIVMioRead(ctx, addr);
    #endif
    ctx->ior = BCI_IOR_INVALID_ADDRESS;
    return 0;
}

static void WriteCell(vm_ctx *ctx, uint32_t addr, uint32_t x) {
    PRINTF("\nStore [0x%x] = %d", addr, x);
    if (addr < DATASIZE) {
        ctx->DataMem[addr] = x;
        return;
    }
    #if (BCI_DEBUG_ACCESS & BCI_ACCESS_CODESPACE)
        uint32_t a = addr - DATASIZE;
        if (a < CODESIZE) {
            ctx->CodeMem[a] = x;
            return;
        }
    #endif
    ctx->ior = BCI_IOR_INVALID_ADDRESS;
}

// CRC32 based on ReadCell
uint32_t crcCells(vm_ctx *ctx, uint32_t addr, uint32_t len) {
    PRINTF("\nCRC [0x%x], %d cells", addr, len);
    uint32_t crc = 0xFFFFFFFF;
    while (len--) {
        uint32_t x = ReadCell(ctx, addr++);
        for (int i = 0; i < 4; i++) {
            uint32_t byte = (x >> (8*i)) & 0xFF;     // unpack octets
            crc = crc ^ byte;
            for (int j = 7; j >= 0; j--) {
                uint32_t mask = -(crc & 1);
                crc = (crc >> 1) ^ (0xEDB88320 & mask);
            }
        }
    }
    return ~crc;
}


static void dupData(vm_ctx *ctx) {
    ctx->DataStack[ctx->sp] = ctx->n;
    ctx->sp = (ctx->sp + 1) & (STACKSIZE - 1);
    ctx->n = ctx->t;
}

static void dropData(vm_ctx *ctx) {
    ctx->t = ctx->n;
    ctx->DataStack[ctx->sp] = BCI_EMPTY_STACK;
    ctx->sp = (ctx->sp - 1) & (STACKSIZE - 1);
    ctx->n = ctx->DataStack[ctx->sp];
}

static void pushReturn(vm_ctx *ctx, uint32_t x) {
    ctx->ReturnStack[ctx->rp] = ctx->r;
    ctx->rp = (ctx->rp + 1) & (STACKSIZE - 1);
    ctx->r = x;
}

static uint32_t popReturn(vm_ctx *ctx) {
    uint32_t r = ctx->r;
    ctx->ReturnStack[ctx->rp] = BCI_EMPTY_STACK;
    ctx->rp = (ctx->rp - 1) & (STACKSIZE - 1);
    ctx->r = ctx->ReturnStack[ctx->rp];
    return r;
}

void BCIinitial(vm_ctx *ctx) {
    memset(ctx, 0, 64); // wipe the first 16 longs
    ctx->status = BCI_STATUS_STOPPED;
}

static const uint8_t stackeffects[32] = {
    0x00, 0x00, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x00, 0x00, 0x01, 0x02, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x00, 0x00, 0x01, 0x02, 0x01, 0x01, 0x01, 0x01
};

// Single-step the VM and set ctx->status to 1 if the PC goes out of bounds.
// inst = 20-bit instruction. If 0, fetch inst from code memory.
// ctx->ior should be 0 upon entering the function.

int stepVM(vm_ctx *ctx, uint16_t inst){
    PRINTF("\nstepVM inst=0x%x", inst);
    uint32_t pc = ctx->pc;
#if (VM_CELLBITS == 32)
    uint64_t ud;
#endif
    if (inst == 0) inst = ctx->CodeMem[pc++];
    if (inst & 0x8000) {
        if (inst & 0x4000) pc = popReturn(ctx);
        inst &= 0x1FFF;             // 3 + 5 + 5 = 13 slot bits
        for (int i = 10; i >= 0; i -= 5) {
            uint32_t t = ctx->t;
            uint32_t n = ctx->n;
            uint32_t _a = ctx->a;
            uint32_t _b = ctx->b;
            uint8_t slot = (inst >> i) & 0x1F;
            uint8_t se = stackeffects[slot];
            if (se & 1) {
                dupData(ctx);
            } else if (se & 2) {
                dropData(ctx);
            }
            switch(slot) {
                // basic stack operations
                case VMO_CYSTORE:    ctx->cy = t & 1;
                case VMO_NOP:
                case VMO_DUP:
                case VMO_DROP:                                            break;
                case VMO_INV:        ctx->t = ~t;                         break;
                case VMO_TWOSTAR:    ctx->t = (t << 1) & VM_MASK;
                                     ctx->cy = (t >> (VM_CELLBITS - 1)) & 1;  break;
                case VMO_TWODIV:     ctx->t = (t & VM_SIGN) | (t >> 1);
                                     ctx->cy = t & 1;                     break;
                case VMO_TWODIVC:    ctx->t = (ctx->cy << (VM_CELLBITS - 1)) | (t >> 1);
                                     ctx->cy = t & 1;                     break;
                case VMO_PLUS:
#if (VM_CELLBITS == 32)
                                     ud = (uint64_t)t + (uint64_t)ctx->t;
                                     ctx->t = ud & VM_MASK;
                                     ctx->cy = (ud >> 32);                break;
#else
                                     t += ctx->t;  ctx->t = t & VM_MASK;
                                     ctx->cy = (t >> VM_CELLBITS) & 1;    break;
#endif
                case VMO_XOR:        ctx->t = t ^ n;                      break;
                case VMO_AND:        ctx->t = t & n;                      break;
                case VMO_SWAP:       ctx->t = n;  ctx->n = t;             break;
                case VMO_CY:         ctx->t = ctx->cy;                    break;
                case VMO_OVER:       ctx->t = n;                          break;
                case VMO_PUSH:       pushReturn(ctx, t);                  break;
                case VMO_R:          ctx->t = ctx->r;                     break;
                case VMO_POP:        ctx->t = popReturn(ctx);             break;
                case VMO_UNEXT: ctx->r--;
                    if (ctx->r & VM_SIGN) {popReturn(ctx); break;}
                    else {i = 15; continue;}
                // memory operations
                case VMO_ASTORE:     ctx->a = t;                          break;
                case VMO_A:          ctx->t = ctx->a;                     break;
                case VMO_FETCHB:     _b = ctx->a;  _a = ctx->b;
                case VMO_FETCHA:
fetch:                               ctx->t = ReadCell(ctx, ctx->a);
                                     ctx->a = _a;  ctx->b = _b;           break;
                case VMO_FETCHAPLUS: _a = ctx->a + 1;                goto fetch;
                case VMO_FETCHBPLUS: _b = ctx->a + 1;  _a = ctx->b;  goto fetch;
                case VMO_STOREB:     _b = ctx->a;  _a = ctx->b;
                case VMO_STOREA:
store:                               WriteCell(ctx, ctx->a, t);
                                     ctx->a = _a;  ctx->b = _b;           break;
                case VMO_STOREAPLUS: _a = ctx->a + 1;                goto store;
                case VMO_STOREBPLUS: _b = ctx->a + 1;  _a = ctx->b;  goto store;
                default: break;
            }
            PRINTF(" op:%02xh (%x %x)", slot, ctx->n, ctx->t);
        }
    } else {
        uint16_t _lex = 0;
        uint16_t imm9;
        int32_t immex = (ctx->lex << 13) | (inst & 0x1FFF);
        switch ((inst >> 13) & 3) {
            case 0: pushReturn(ctx, pc);
            case 1: pc = immex;                                     break;
            case 2: dupData(ctx);  ctx->t = immex;                  break;
            default: imm9 = inst & 0x1FF;
            immex = imm9;
            if (inst & 0x100) immex |= 0xFFFFFE00;
            switch ((inst >> 9) & 15) {
                case 0: _lex = (ctx->lex << 9) | imm9;              break;
                case 1: ctx->ior = imm9;                            break;
                case 4: if (ctx->t == 0) pc += immex;               break;
                case 5: dropData(ctx);                              break;
                case 6: if ((ctx->t & VM_SIGN) == 0) pc += immex;   break;
                case 7: ctx->r--;
                    if (ctx->r & VM_SIGN) popReturn(ctx);
                    else pc += immex;
                    break;
                default: break;
            }
        }
        ctx->lex = _lex;
        ctx->pc = pc;
    }
    return ctx->ior;
}

// Stream interface between BCI and VM

static const uint8_t *cmd;
static uint16_t len;

static uint8_t get8(void) {
    if (!len) return 0;
    len--; return *cmd++;
}

static uint32_t get32(void) {           // 32-bit stream data is big-endian
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) r = (r << 8) + get8();
    return r;
}

static void put8(vm_ctx *ctx, uint8_t c) {
    ctx->putcFn(c);
}

static void put32(vm_ctx *ctx, uint32_t x) {
    uint8_t n = 4;
    while (n--) ctx->putcFn(x >> (8*n));
}

// VM wrappers

static void waitUntilVMready(vm_ctx *ctx){
    if (ctx->status == BCI_STATUS_STOPPED) return;
    uint32_t limit = BCI_CYCLE_LIMIT;
    while (limit--) {
        if (stepVM(ctx, 0)) return;
    }
    BCIinitial(ctx);
}

static int16_t SimXT(vm_ctx *ctx, uint32_t xt){
    int ior;
    if (xt & (1<<31)) ior = stepVM(ctx, 0x8000 | (xt & 0x1F));
    else ior = stepVM(ctx, xt);
    return ior;
}

/*
Since the VM has a context structure, these are late-bound in the context to allow stand-alone testing.
*/

void BCIhandler(vm_ctx *ctx, const uint8_t *src, uint16_t length) {
    ctx->InitFn();
    cmd = src;  len = length;
    uint32_t addr;
    uint32_t x;
    uint8_t n;
    uint32_t ds[16];
    ctx->ior = 0;
    switch (get8()) {
    case BCIFN_BOILER:
        put8(ctx, 1);
        put8(ctx, 0);                   // minimum boilerplate, format 0
        put8(ctx, BCI_ACK);
        break;
    case BCIFN_READ:
        n = get8();
        addr = get32();
        put8(ctx, n);
        while (n--) put32(ctx, ReadCell(ctx, addr++));
ack:    if (ctx->ior) put8(ctx, BCI_NACK);
        else          put8(ctx, BCI_ACK);
        break;
    case BCIFN_WRITE:
        n = get8();
        addr = get32();
        while (n--) {
            x = get32();
            WriteCell(ctx, addr++, x);
        }
        goto ack;
    case BCIFN_EXECUTE:
        waitUntilVMready(ctx);
        WriteCell(ctx, 0, get32());     // packed status at data[0]
        n = get8();
        dupData(ctx);
        ctx->t = BCI_EMPTY_STACK;
        while (n--) {
            dupData(ctx);
            ctx->t = get32();
        }
        ctx->ior = SimXT(ctx, get32()); // xt
        for (n = 0; n < 16; n++) {
            x = ctx->t;
            dropData(ctx);
            ds[n] = x;
            if (x == BCI_EMPTY_STACK) break;
        }
        put8(ctx, n);
        while (n--) {
            put32(ctx, ds[n]);
        }
        put32(ctx, ReadCell(ctx, 0));
        goto ack;
    case BCIFN_CRC:
        addr = get32();
        x = get32();
        put32(ctx, crcCells(ctx, addr, x));
        goto ack;
    default:
        put8(ctx, BCI_NACK);
    }
    ctx->FinalFn();
}
