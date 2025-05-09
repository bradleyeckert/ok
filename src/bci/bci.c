#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "bci.h"
#include "bciHW.h"

#define TRACE 1

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

static const uint8_t boilerplate[BOILERPLATE_SIZE] = {
    1,                          // format 1
    VM_CELLBITS,                // bits per data cell
    VM_INSTBITS,                // bits per instruction icell
    DATA_STACKSIZE - 1,         // max stack depths
    RETURN_STACKSIZE - 1,
    (DATASIZE >> 8) - 1,        // 256-cell pages of data memory less 1
    (CODESIZE >> 8) - 1,        // 256-icell pages of code memory less 1
    (TEXTSIZE >> 10) - 1,       // 1K-cell pages of text memory less 1
    TEXTORIGIN >> 12,           // start address of text memory
};

typedef VMcell_t (*APIfn) (vm_ctx *ctx);

const APIfn APIfns[] = {
    NVMbeginRead, NVMbeginWrite, NVMread, NVMwrite, NVMendRW,
    API_Emit, API_umstar, API_mstar, API_mudivmod
};

void BCIinitial(vm_ctx *ctx) {
    memset(ctx, 0, sizeof(ctx->DataMem)); // data space initializes to 0
    memset(ctx, 0, 64);         // wipe the first 16 longs
    ctx->boilerplate = boilerplate;
    ctx->DataMem[0] = 10;
    ctx->status = BCI_STATUS_STOPPED;
    VMinst_t blank = (VMinst_t)((BLANK_FLASH_BYTE << 24) | (BLANK_FLASH_BYTE << 16)
                              | (BLANK_FLASH_BYTE << 8) | BLANK_FLASH_BYTE);
    if (ctx->CodeMem[0] != blank) // got code?
    ctx->status = BCI_STATUS_RUNNING;
}

/*
Memory access is mediated by debugAccessFlags, which is 0 for production code.
Memory sections assume a 24-bit address space, where address units are cells.
String libraries in both C and Forth are overly simplistic, so if strings use
bytes the custom string functions would adapt as needed.
*/

static uint32_t ReadCell(vm_ctx *ctx, uint32_t addr) {
    PRINTF("\nLoad [0x%x]", addr);
    if (addr < DATASIZE)
        return ctx->DataMem[addr];      // Data
    if (addr < TEXTORIGIN) {
        ctx->ior = BCI_IOR_INVALID_ADDRESS;
        return 0;
    }
    addr -= TEXTORIGIN;
    if (addr < TEXTSIZE) {
        PRINTF(" = TextMem[0x%x]", addr);
        return ctx->TextMem[addr];      // Text
    }
    return BCIVMioRead(ctx, addr);      // Peripherals
}

static void WriteCell(vm_ctx *ctx, uint32_t addr, uint32_t x) {
    PRINTF("\nStore [0x%x] = %d", addr, x);
    if (addr < DATASIZE) {
        ctx->DataMem[addr] = x;         // Data
        return;
    }
    if (addr < TEXTORIGIN) {
        ctx->ior = BCI_IOR_INVALID_ADDRESS;
        return;
    }
    addr -= TEXTORIGIN;
    if (addr < TEXTSIZE) {
        ctx->TextMem[addr] = x;         // Text
        return;
    }
    BCIVMioWrite(ctx, addr, x);         // Peripherals
}

static void dupData(vm_ctx *ctx) {
    ctx->DataStack[ctx->sp] = ctx->n;
    ctx->sp = (ctx->sp + 1) & (DATA_STACKSIZE - 1);
    ctx->n = ctx->t;
}

static void dropData(vm_ctx *ctx) {
    ctx->t = ctx->n;
    ctx->DataStack[ctx->sp] = (VMcell_t)BCI_EMPTY_STACK;
    ctx->sp = (ctx->sp - 1) & (DATA_STACKSIZE - 1);
    ctx->n = ctx->DataStack[ctx->sp];
}

static void pushReturn(vm_ctx *ctx, uint32_t x) {
    ctx->ReturnStack[ctx->rp] = ctx->r;
    ctx->rp = (ctx->rp + 1) & (RETURN_STACKSIZE - 1);
    ctx->r = x;
}

static uint32_t popReturn(vm_ctx *ctx) {
    uint32_t r = ctx->r;
    ctx->ReturnStack[ctx->rp] = (VMcell_t)BCI_EMPTY_STACK;
    ctx->rp = (ctx->rp - 1) & (RETURN_STACKSIZE - 1);
    ctx->r = ctx->ReturnStack[ctx->rp];
    return r;
}

static const uint8_t stackeffects[32] = {
    0x00, 0x00, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x00, 0x00, 0x01, 0x02, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x00, 0x00, 0x01, 0x02, 0x01, 0x01, 0x01, 0x01
};

// Single-step the VM and set ctx->status to 1 if the PC goes out of bounds.
// inst = instruction. If 0, fetch inst from code memory.
// ctx->ior should be 0 upon entering the function.

#define IMASK2 ((1 << (VM_INSTBITS - 2)) - 1)
#define APIfs (sizeof(APIfn)/sizeof(APIfns[0]))
static void ops0001(vm_ctx *ctx, int inst);


int BCIstepVM(vm_ctx *ctx, VMinst_t inst){
    uint32_t pc = ctx->pc;
#if (VM_CELLBITS == 32)
    uint64_t ud;
#endif
    if (inst == 0) {
        inst = ctx->CodeMem[pc++];
        PRINTF("\nPC=%04Xh inst=%04Xh", (pc-1), inst);
    } else {
        PRINTF("\nSim one inst, %Xh", inst);
    }
    if (inst & (1 << (VM_INSTBITS - 1))) { // MSB
        if (inst & (1 << (VM_INSTBITS - 2))) pc = popReturn(ctx);
        inst &= IMASK2;
        for (int i = UOP_SLOTS * 5; i >= 0; i -= 5) {
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
                case VMO_DROP:                                          break;
                case VMO_INV:        ctx->t = ~t;                       break;
                case VMO_TWOSTAR:    ctx->t = (t << 1) & VM_MASK;
                                     ctx->cy = (t >> (VM_CELLBITS - 1)) & 1;  break;
                case VMO_TWODIV:     ctx->t = (t & VM_SIGN) | (t >> 1);
                                     ctx->cy = t & 1;                   break;
                case VMO_TWODIVC:    ctx->t = (ctx->cy << (VM_CELLBITS - 1)) | (t >> 1);
                                     ctx->cy = t & 1;                   break;
                case VMO_PLUS:
#if (VM_CELLBITS == 32)
                                     ud = (uint64_t)t + (uint64_t)ctx->t;
                                     ctx->t = ud & VM_MASK;
                                     ctx->cy = (uint8_t)(ud >> 32);     break;
#else
                                     t += ctx->t;  ctx->t = t & VM_MASK;
                                     ctx->cy = (t >> VM_CELLBITS) & 1;  break;
#endif
                case VMO_XOR:        ctx->t = t ^ n;                    break;
                case VMO_AND:        ctx->t = t & n;                    break;
                case VMO_SWAP:       ctx->t = n;  ctx->n = t;           break;
                case VMO_CY:         ctx->t = ctx->cy;                  break;
                case VMO_ZERO:       ctx->t = 0;                        break;
                case VMO_OVER:       ctx->t = n;                        break;
                case VMO_PUSH:       pushReturn(ctx, t);                break;
                case VMO_R:          ctx->t = ctx->r;                   break;
                case VMO_POP:        ctx->t = popReturn(ctx);           break;
                case VMO_UNEXT: ctx->r--;
                    if (ctx->r == 0) {popReturn(ctx); break;}
                    else {i = 15; continue;}
                case VMO_U:          ctx->t = 0;                        break;
                // memory operations
                case VMO_ASTORE:     ctx->a = t;                        break;
                case VMO_A:          ctx->t = ctx->a;                   break;
                case VMO_FETCHB:     _b = ctx->a;  _a = ctx->b;
                case VMO_FETCHA:
fetch:                               ctx->t = ReadCell(ctx, ctx->a);
                                     ctx->a = _a;  ctx->b = _b;         break;
                case VMO_FETCHAPLUS: _a = ctx->a + 1;                goto fetch;
                case VMO_FETCHBPLUS: _b = ctx->a + 1;  _a = ctx->b;  goto fetch;
                case VMO_STOREB:     _b = ctx->a;  _a = ctx->b;
                case VMO_STOREA:
store:                               WriteCell(ctx, ctx->a, t);
                                     ctx->a = _a;  ctx->b = _b;         break;
                case VMO_STOREAPLUS: _a = ctx->a + 1;                goto store;
                case VMO_STOREBPLUS: _b = ctx->a + 1;  _a = ctx->b;  goto store;
                default: break;
            }
            PRINTF(" uop:%02xh (%x %x)", slot, ctx->n, ctx->t);
            ctx->cycles++;
        }
    } else {
        uint16_t _lex = 0;
        uint32_t imm;
        int flag;
        int32_t immex = (ctx->lex << (VM_INSTBITS - 3))
                    | (inst & ((1 << (VM_INSTBITS - 3)) - 1));
        switch ((inst >> (VM_INSTBITS - 3)) & 3) { // upper bits 000, 001, 010, 011
            case 0: pushReturn(ctx, pc);
            case 1: pc = immex;                                         break;
            case 2: dupData(ctx);  ctx->t = immex;                      break;
            default: // inst = 011 oooo imm...
            imm = inst & ((1 << (VM_INSTBITS - 7)) - 1);
            immex = imm;
            if (inst & (1 << (VM_INSTBITS - 8))) { // sign-extend
                immex |= ~((1 << (VM_INSTBITS - 7)) - 1);
            }
            int opcode = (inst >> (VM_INSTBITS - 7)) & 0x0F;
            switch (opcode) {
                case 0: _lex = (ctx->lex << (VM_INSTBITS - 7)) | imm;   break;
                case 1: ops0001(ctx, imm);                              break;
                case 2: ctx->a = ctx->x + imm;                          break;
                case 3: ctx->a = ctx->y + imm;                          break;
                case 4: flag = (ctx->t == 0);  dropData(ctx);
                    if (flag) { pc = ctx->pc + immex; }                 break;
                case 5: pc = ctx->pc + immex;                           break;
                case 6: if ((ctx->t & VM_SIGN) == 0) {
                    pc = ctx->pc + immex;
                    } break;
                case 7:
                    ctx->r--;
                    if (ctx->r == 0) popReturn(ctx);
                    else pc = ctx->pc + immex;
                    break;
                case 13: dupData(ctx);
                case 12:
                case 14:
                case 15:
                    if (imm < APIfs) ctx->t = -1;
                    else {ctx->t = APIfns[imm](ctx);}                   break;
                default: break;
            }
            switch (opcode) {
                case 15: dropData(ctx);
                case 14: dropData(ctx);
                default: break;
            }
        }
        ctx->lex = _lex;
        ctx->cycles += (UOP_SLOTS + 1);
    }
    ctx->pc = pc;
    return ctx->ior;
}

static void ops0001(vm_ctx *ctx, int inst) { // alternate instructions 0110001
    switch(inst) {
        case 0: ctx->x = ctx->t;  dropData(ctx);  break;
        case 1: ctx->y = ctx->t;  dropData(ctx);  break;
        case 2: ctx->ior = ctx->t;  dropData(ctx);  break;
    }
}


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
    ctx->putcFn(ctx->id, c);
}

static void putN(vm_ctx *ctx, uint32_t x, int n) {
    while (n--) {
        ctx->putcFn(ctx->id, x & 0xFF);
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
    while (limit--) {
        if (BCIstepVM(ctx, 0)) return;
    }
    BCIinitial(ctx); // hung
}

static int16_t simulate(vm_ctx *ctx, uint32_t xt){
    int ior = 0;
    if (xt & 0x80000000) {
        ior = BCIstepVM(ctx, xt);       // single instruction
    } else {
        PRINTF("\nCalling %d ", xt);
        int rdepth = ctx->rp;
        xt += VMI_CALL;
        ior = BCIstepVM(ctx, xt);       // trigger call to xt
        while (rdepth != ctx->rp) {
            BCIstepVM(ctx, 0);          // execute instructions
            if (ctx->ior) break;        // break on error
        }
    }
    return ior;
}

/*
Since the VM has a context structure, these are late-bound in the context to allow stand-alone testing.
*/

void BCIhandler(vm_ctx *ctx, const uint8_t *src, uint16_t length) {
    ctx->InitFn(ctx->id);
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
    switch (n) {
    case BCIFN_READ:
        n = get8();
        addr = get32();
        put8(ctx, n);
        while (n--) put32(ctx, ReadCell(ctx, addr++));
        break;
    case BCIFN_WRITE:
        n = get8();
        addr = get32();
        while (n--) {
            x = get32();
            WriteCell(ctx, addr++, x);
        }
        break;
    case BCIFN_EXECUTE:
        waitUntilVMready(ctx);
        ctx->cycles = 0;
        WriteCell(ctx, 0, get32());     // packed status at data[0]
        n = get8();
        for (x = 0; x < DATA_STACKSIZE; x++) {
            dupData(ctx);               // fill the data stack with "empty stack"
            ctx->t = (VMcell_t)BCI_EMPTY_STACK;
        }
        while (n--) {
            dupData(ctx);
            ctx->t = get32();
        }
        ctx->ior = simulate(ctx, get32()); // xt
        put8(ctx, BCI_BEGIN);           // indicate end of random chars, if any
        for (n = 0; n < 16; n++) {
            x = ctx->t;
            dropData(ctx);
            ds[n] = x;
            if (x == (VMcell_t)BCI_EMPTY_STACK) break;
        }
        put8(ctx, n);                   // stack depth
        while (n--) {
            put32(ctx, ds[n]);
        }
        put32(ctx, ReadCell(ctx, 0));   // return packed status
        put32(ctx, (uint32_t)ctx->cycles);
        put32(ctx, (uint32_t)(ctx->cycles >> 32));  // return cycle count
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
    case BCIFN_SHUTDOWN:
        x = get32();
        PRINTF("\nVM shutdown triggered with pin=%d ", x);
        if (x == 123456789) ctx->status = BCI_STATUS_SHUTDOWN;
        break;
    default:
        ctx->ior = BCI_BAD_COMMAND;
    }
    putN(ctx, ctx->ior, 2);
    ctx->FinalFn(ctx->id);
}
