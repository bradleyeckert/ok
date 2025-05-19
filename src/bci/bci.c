#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "bci.h"
#include "bciHW.h"

#define TRACE 0
#include <stdio.h>

/*
BCIhandler takes input from a buffer and outputs to encrypted UART using these primitives:
void hermesSendInit(port_ctx *ctx, uint8_t tag);
void hermesSendChar(port_ctx *ctx, uint8_t c);
void hermesSendFinal(port_ctx *ctx);
which are late-bound in the port_ctx to decouple the BCI from its output stream.
*/

#if (TRACE)
#include <stdio.h>
#define PRINTF  if (TRACE) printf
#else
#define PRINTF(...) do { } while (0)
#endif

//------------------------------------------------------------------------------
// Stream interface between BCI and VM

static const uint8_t *cmd;
static uint16_t len;

static uint8_t get8(void) {
    if (!len) return 0;
    len--; return *cmd++;
}

static uint32_t get32(void) {           // 32-bit stream data is little-endian
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) r |= (get8() << (i * 8));
    return r;
}

static void put8(vm_ctx *ctx, uint8_t c) {
    BCIsendChar(ctx->id, c);
}

static void putN(vm_ctx *ctx, uint32_t x, int n) {
    while (n--) {
        BCIsendChar(ctx->id, x & 0xFF);
        x >>= 8;
    }
}

static void put32(vm_ctx *ctx, uint32_t x) {
    putN(ctx, x, 4);
}

// VM wrappers

static void waitUntilVMready(vm_ctx *ctx){
    if (ctx->status == BCI_STATUS_STOPPED) return;
    uint32_t limit = BCI_CYCLE_LIMIT;
    StopVMthread(ctx);
    while (limit--) {
        if (VMstep(ctx, 0)) return;
    }
    VMreset(ctx); // hung
}

static int16_t simulate(vm_ctx *ctx, uint32_t xt){
    if (xt & 0x80000000) {
        VMstep(ctx, xt);             // single instruction
    } else {
        PRINTF("\nCalling %d, ", xt);
        int rdepth = ctx->rp;
        xt += VMI_CALL;
        VMstep(ctx, xt);             // trigger call to xt
        while (rdepth != ctx->rp) {
            VMstep(ctx, 0);          // execute instructions
            if (ctx->ior) break;     // break on error
        }
        PRINTF("Done simulating");
    }
    return ctx->ior;
}

/*
Since the VM has a context structure, these are late-bound in the context to allow stand-alone testing.
*/

void BCIhandler(vm_ctx *ctx, const uint8_t *src, uint16_t length) {
    BCIsendInit(ctx->id);               // empty the response buffer
    cmd = src;  len = length;
    uint32_t ds[16];
    memset(ds, 0, 16 * sizeof(uint32_t));
    uint32_t addr;
    uint32_t x;
    int32_t temp;
    uint8_t *taddr;
    uint8_t n = get8();
    put8(ctx, BCI_BEGIN);               // indicate a BCI response message
    put8(ctx, n);                       // indicate what kind of response it is
    ctx->ior = 0;
    uint8_t status0 = ctx->status;
    switch (n) {
    case BCIFN_READ:
        n = get8();
        addr = get32();
        put8(ctx, n);
        while (n--) put32(ctx, VMreadCell(ctx, addr++));
        break;
    case BCIFN_WRITE:
        n = get8();
        addr = get32();
        while (n--) {
            x = get32();
            VMwriteCell(ctx, addr++, x);
        }
        break;
    case BCIFN_EXECUTE:
        waitUntilVMready(ctx);
        ctx->cycles = 0;
        VMwriteCell(ctx, 0, get32());     // packed status at data[0]
        n = get8();
        // VM_EMPTY_STACK
        uint16_t sp0 = ctx->sp;
        while (n--) {
            VMdupData(ctx);
            ctx->t = get32();
        }
        ctx->ior = simulate(ctx, get32()); // xt
        put8(ctx, BCI_BEGIN);           // indicate end of random chars, if any
        temp = ctx->sp - sp0;           // bytes on stack
        if (temp < 0) ctx->ior = BCI_STACK_UNDERFLOW;
        else for (n = 0; n < temp; n++) {
            x = VMdropData(ctx);
            ds[n] = x;
        }
        put8(ctx, n);                   // stack depth
        while (n--) {
            put32(ctx, ds[n]);
        }
        put32(ctx, VMreadCell(ctx, 0));   // return packed status
        put32(ctx, (uint32_t)ctx->cycles);
        put32(ctx, (uint32_t)(ctx->cycles >> 32));  // return cycle count
        ctx->status = status0;
        break;
    case BCIFN_CRC:
        put8(ctx, 4);
        put32(ctx, CRC32((uint8_t*)ctx->CodeMem, CODESIZE * sizeof(VMinst_t)));
        put32(ctx, CODESIZE * sizeof(VMinst_t));
        put32(ctx, CRC32((uint8_t*)ctx->TextMem, TEXTSIZE * sizeof(VMcell_t)));
        put32(ctx, TEXTSIZE * sizeof(VMcell_t));
        break;
    case BCIFN_WRTEXT:
        x = TEXTSIZE * sizeof(VMcell_t);
        taddr = (uint8_t*)ctx->TextMem;
        goto write;
    case BCIFN_WRCODE:
        x = CODESIZE * sizeof(VMinst_t);
        taddr = (uint8_t*)ctx->CodeMem;
write:  addr = get32();
        temp = (x - addr);              // remaining
        if (temp < 0) temp = 0;         // nothing to program
        if (temp > FLASH_BLOCK_SIZE ) temp = FLASH_BLOCK_SIZE;
        if (len < temp)   temp = len;
        FlashUnlock(&taddr[addr]);
        PRINTF("\nWriting %d bytes to %p ", temp, &taddr[addr]);
        FlashWrite(&taddr[addr], (const uint8_t*) cmd, temp);
        FlashLock();
        break;
    case BCIFN_SECTOR_ERASE:
        x = get32();
        PRINTF("\nErasing sector %d of Flash ", x);
        FlashErase(x);
        break;
    case BCIFN_STROBE:
        x = get32();
        PRINTF("\nVM strobe=%d ", x);
        switch (x) {
            case BCI_SHUTDOWN_PIN: ctx->status = BCI_STATUS_SHUTDOWN;  break;
            case BCI_SLEEP_PIN:    ctx->status = BCI_STATUS_STOPPED;   break;
            case BCI_RESET_PIN:    VMreset(ctx);                    break;
            default: break;
        }
        break;
    default:
        ctx->ior = BCI_BAD_COMMAND;
    }
    putN(ctx, ctx->ior, 2);
    BCIsendFinal(ctx->id);
}
