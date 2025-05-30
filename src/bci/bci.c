#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "bci.h"
#include "bciHW.h"

#ifdef BCI_TRACE
#include <stdio.h>
#define PRINTF  printf
#else
#define PRINTF(...) do { } while (0)
#endif

//------------------------------------------------------------------------------
// Stream interface between BCI and VM

static const uint8_t *cmd;
static uint16_t len;
void get8debug(uint8_t c);

static uint8_t get8(void) {
    if (!len) return 0;
    len--; uint8_t c = *cmd++;
    get8debug(c);
    return c;
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
        if (VMstep(ctx, 0)) return; // bcisync instruction
    }
    VMreset(ctx); // hung
}

static int16_t simulate(vm_ctx *ctx, uint32_t xt){
    if (xt & 0x80000000) {
        PRINTF("\nExecuting single instruction %04x, ", xt & 0x7FFFFFFF);
        VMstep(ctx, xt);             // single instruction
    } else {
        PRINTF("\nCalling %04x, ", xt);
        int rdepth = ctx->rp;
        VMstep(ctx, xt | VMI_CALL);  // trigger call to xt
        while (rdepth != ctx->rp) {
            VMstep(ctx, 0);          // execute instructions
            if (ctx->ior) break;     // break on error
        }
        PRINTF("Done simulating ");
    }
    return ctx->ior;
}

#define EXEC_STACK_SIZE 16

void BCIhandler(vm_ctx *ctx, const uint8_t *src, uint16_t length) {
    BCIsendInit(ctx->id);               // empty the response buffer and send 2-byte node number
    cmd = src;  len = length;
    uint32_t ds[EXEC_STACK_SIZE];
    memset(ds, 0, EXEC_STACK_SIZE * sizeof(uint32_t));
    uint32_t addr;
    uint32_t x;
    int32_t temp;
    uint64_t ud;
    uint8_t *taddr;
    uint8_t n = get8();
    put8(ctx, BCI_BEGIN);               // indicate a BCI response message
    put8(ctx, n);                       // indicate what kind of response it is
    ctx->ior = 0;                       // So, that's a 4-byte preamble before the response
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
        waitUntilVMready(ctx);          // stop the VM if it is running
        ctx->cycles = 0;
        VMwriteCell(ctx, 0, get32());   // packed status at data[0]
        uint8_t sp0 = ctx->sp;
        n = get8();
        while (n--) {
            VMpushData(ctx, get32());
        }
        ctx->ior = simulate(ctx, get32()); // xt
        put8(ctx, BCI_BEGIN);           // indicate end of random chars, if any
        temp = ctx->sp - sp0;
        if (temp < 0) {
            ctx->ior = BCI_STACK_UNDERFLOW;
            temp = 0;
        }
        if (temp > EXEC_STACK_SIZE) {
            ctx->ior = BCI_STACK_OVERFLOW;
            temp = EXEC_STACK_SIZE;
        }
        for (int i = 0; i < temp; i++) {//
            ds[i] = VMpopData(ctx);
        }
        put8(ctx, temp);                // stack items returned
        while (temp--) {
            put32(ctx, ds[temp]);
        }
        put32(ctx, VMreadCell(ctx, 0)); // return packed status
        ctx->status = status0;          // run if it was previously running
    case BCIFN_GET_CYCLES:
        ud = ctx->cycles;
        put32(ctx, (uint32_t)ud);       // return cycle count
        put32(ctx, (uint32_t)(ud >> 32));
        break;
    case BCIFN_CRC:
        PRINTF("\nGetting CRCs of code and text spaces ");
        n = get8();
        uint32_t cp = get32();
        uint32_t tp = get32();
        temp = n;
        while (temp--) {
            if (temp & 0x80) break;
            get32(); // discard extra inputs
        }
        put8(ctx, n);
        put32(ctx, CRC32((uint8_t*)ctx->CodeMem, cp));
        put32(ctx, CRC32((uint8_t*)ctx->TextMem, tp));
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
            case BCI_RESET_PIN:    VMreset(ctx);                       break;
            default: break;
        }
        break;
    default:
        ctx->ior = BCI_BAD_COMMAND;
    }
    putN(ctx, ctx->ior, 2);
    BCIsendFinal(ctx->id);
}
