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
    API_Emit, API_umstar, API_mudivmod
};

void BCIinitial(vm_ctx *ctx) {
    memset(ctx, 0, sizeof(ctx->DataMem)); // data space initializes to 0
    memset(ctx, 0, 64);         // wipe the first 16 longs
    ctx->boilerplate = boilerplate;
    ctx->DataMem[0] = 10;
    ctx->status = BCI_STATUS_STOPPED;
    ctx->statusNew = BCI_STATUS_STOPPED;
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
        return ctx->TextMem[addr];      // Text
    }
    ctx->ior = BCI_IOR_INVALID_ADDRESS;
    return 0;
}

static void WriteCell(vm_ctx *ctx, uint32_t addr, uint32_t x) {
    PRINTF("\nStore [0x%x] = %d", addr, x);
    if (addr < DATASIZE) ctx->DataMem[addr] = x;
    else ctx->ior = BCI_IOR_INVALID_ADDRESS;
}

static void dupData(vm_ctx *ctx) {
    ctx->DataStack[ctx->sp] = ctx->n;
    ctx->sp = (ctx->sp + 1) & (DATA_STACKSIZE - 1);
    ctx->n = ctx->t;
}

static void dropData(vm_ctx *ctx) {
    ctx->t = ctx->n;
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
    ctx->rp = (ctx->rp - 1) & (RETURN_STACKSIZE - 1);
    ctx->r = ctx->ReturnStack[ctx->rp];
    return r;
}

static const uint8_t stackeffects[32] = VM_STACKEFFECTS;

// Single-step the VM and set ctx->status to 1 if the PC goes out of bounds.
// inst = instruction. If 0, fetch inst from code memory.
// ctx->ior should be 0 upon entering the function.
// Returns 1 if running and ready for BCI execution

#define IMASK2 ((1 << (VM_INSTBITS - 2)) - 1)
#define APIfs (sizeof(APIfn)/sizeof(APIfns[0]))
static int ops0001(vm_ctx *ctx, int inst);

int BCIstepVM(vm_ctx *ctx, VMinst_t inst){
    int r = 0;
    uint32_t pc = ctx->pc;
    if (inst == 0) {
        inst = ctx->CodeMem[pc++];
        PRINTF("\nPC=%04Xh inst=%04Xh", (pc-1), inst);
    } else {
        PRINTF("\nBCIstepVM execute instruction, %Xh", inst);
    }
    if (inst & (1 << (VM_INSTBITS - 1))) { // MSB
        if (inst & (1 << (VM_INSTBITS - 2))) pc = popReturn(ctx);
        inst &= IMASK2;
        for (int i = SLOT0_POSITION; i > -5; i -= 5) {
            uint8_t uop;
            if (i < 0) uop = inst & LAST_SLOT_MASK;
            else uop = (inst >> i) & 0x1F;
            uint32_t t = ctx->t;
            uint32_t n = ctx->n;
            uint8_t se = stackeffects[uop];
            if (se & 1) {
                dupData(ctx);
            } else if (se & 2) {
                dropData(ctx);
            }
            switch (uop) {
                // basic stack operations
#if (VM_CELLBITS == 32)
                uint64_t ud;
#endif
                case VMO_NOP:
                case VMO_DUP:
                case VMO_DROP:                                            break;
                case VMO_INV:        ctx->t = ~t & VM_MASK;               break;
                case VMO_TWOSTAR:    ctx->t = (t << 1) & VM_MASK;
                                     ctx->cy = (t >> (VM_CELLBITS - 1)) & 1; break;
                case VMO_TWODIV:     ctx->t = (t & VM_SIGN) | (t >> 1);
                                     ctx->cy = t & 1;                     break;
                case VMO_TWODIVC:    ctx->t = (ctx->cy << (VM_CELLBITS - 1)) | (t >> 1);
                                     ctx->cy = t & 1;                     break;
                case VMO_PLUS:
#if (VM_CELLBITS == 32)
                                     ud = (uint64_t)t + (uint64_t)ctx->t;
                                     ctx->t = ud & VM_MASK;
                                     ctx->cy = (uint8_t)(ud >> 32);       break;
#else
                                     t += ctx->t;  ctx->t = t & VM_MASK;
                                     ctx->cy = (t >> VM_CELLBITS) & 1;    break;
#endif
                case VMO_XOR:        ctx->t = t ^ n;                      break;
                case VMO_AND:        ctx->t = t & n;                      break;
                case VMO_SWAP:       ctx->t = n;  ctx->n = t;             break;
                case VMO_CY:         ctx->t = ctx->cy;                    break;
                case VMO_ZERO:       ctx->t = 0;                          break;
                case VMO_OVER:       ctx->t = n;                          break;
                case VMO_PUSH:       pushReturn(ctx, t);                  break;
                case VMO_R:          ctx->t = ctx->r;                     break;
                case VMO_POP:        ctx->t = popReturn(ctx);             break;
                case VMO_UNEXT: ctx->r--;
                    if (ctx->r == 0) {popReturn(ctx); break;}
                    else {i = 15; continue;}
                case VMO_U:          ctx->t = 0;                          break;
                // memory operations
                case VMO_BSTORE:     ctx->b = t;                          break;
                case VMO_A:          ctx->t = ctx->a;                     break;
                case VMO_ASTORE:     ctx->a = t;                          break;
                case VMO_FETCHA:     ctx->t = ReadCell(ctx, ctx->a);      break;
                case VMO_FETCHAPLUS: ctx->t = ReadCell(ctx, ctx->a++);    break;
                case VMO_FETCHB:     ctx->t = BCIVMioRead(ctx, ctx->b);   break;
                case VMO_FETCHBPLUS: ctx->t = BCIVMioRead(ctx, ctx->b++); break;
                case VMO_STOREA:     WriteCell(ctx, ctx->a, t);           break;
                case VMO_STOREAPLUS: WriteCell(ctx, ctx->a++, t);         break;
                case VMO_STOREB:     BCIVMioWrite(ctx, ctx->b, t);        break;
                case VMO_STOREBPLUS: BCIVMioWrite(ctx, ctx->b++, t);      break;
                default: break;
            }
            PRINTF(" uop:%02xh (%x %x)", uop, ctx->n, ctx->t);
            ctx->cycles++;
        }
    } else {
        uint16_t _lex = 0;
        uint32_t imm;
        int32_t immex = (ctx->lex << (VM_INSTBITS - 3))
                    | (inst & ((1 << (VM_INSTBITS - 3)) - 1));
        switch ((inst >> (VM_INSTBITS - 3)) & 3) { // upper bits 000, 001, 010, 011
            case VMO_CALL: pushReturn(ctx, pc);
            case VMO_JUMP: pc = immex;                                  break;
            case VMO_LIT: dupData(ctx);  ctx->t = immex;                break;
            default: // inst = 011 oooo imm...
            imm = inst & ((1 << (VM_INSTBITS - 7)) - 1);
            immex = imm;
            if (inst & (1 << (VM_INSTBITS - 8))) { // sign-extend
                immex |= ~((1 << (VM_INSTBITS - 7)) - 1);
            }
            int opcode = (inst >> (VM_INSTBITS - 7)) & 0x0F;
            int flag;
            switch (opcode) {
                case VMO_LEX: _lex = (ctx->lex << (VM_INSTBITS - 7)) | imm;
                    break;
                case VMO_ZOO: r = ops0001(ctx, imm);                    break;
                case VMO_AX: ctx->a = ctx->x + imm;                     break;
                case VMO_BY: ctx->b = ctx->y + imm;                     break;
                case VMO_ZBRAN: flag = (ctx->t == 0);  dropData(ctx);
                    if (flag) { pc = ctx->pc + immex; }                 break;
                case VMO_BRAN: pc = ctx->pc + immex;                    break;
                case VMO_PBRAN: if ((ctx->t & VM_SIGN) == 0) {
                    pc = ctx->pc + immex;
                    } break;
                case VMO_NEXT:
                    ctx->r--;
                    if (ctx->r == 0) popReturn(ctx);
                    else pc = ctx->pc + immex;
                    break;
                case VMO_DUPAPI: dupData(ctx);
                case VMO_API:
                case VMO_APIDROP:
                case VMO_API2DROP:
                    if (imm < APIfs) ctx->t = -1;
                    else {ctx->t = APIfns[imm](ctx);}                   break;
                default: break;
            }
            switch (opcode) {
                case VMO_API2DROP: dropData(ctx);
                case VMO_APIDROP: dropData(ctx);
                default: break;
            }
        }
        ctx->lex = _lex;
//        ctx->cycles += 3; // whole instructions take longer
    }
    ctx->pc = pc;
    return r;
}

static int ops0001(vm_ctx *ctx, int inst) { // alternate instructions 0110001...
    int r = 0;
    int imm = inst & 0x7F;
    if (inst & VMI_ZOODUP) dupData(ctx);
    switch(imm) {
        case VMO_XSTORE: ctx->x = ctx->t;                               break;
        case VMO_YSTORE: ctx->y = ctx->t;                               break;
        case VMO_SPSTORE: ctx->sp = ctx->t;                             break;
        case VMO_RPSTORE: ctx->rp = ctx->t;                             break;
        case VMO_SPFETCH: ctx->t = (ctx->sp - 1) & (DATA_STACKSIZE-1);  break;
        case VMO_RPFETCH: ctx->t = ctx->rp;                             break;
        case VMO_BCISYNC: r = 1;                                        break;
        case VMO_THROW: ctx->ior = ctx->t;                              break;
        default: break;
    }
    if (inst & VMI_ZOODROP) dropData(ctx);
    return r;
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
        if (BCIstepVM(ctx, 0)) return;
    }
    BCIinitial(ctx); // hung
}

static int16_t simulate(vm_ctx *ctx, uint32_t xt){
    if (xt & 0x80000000) {
        BCIstepVM(ctx, xt);             // single instruction
    } else {
        PRINTF("\nCalling %d, ", xt);
        int rdepth = ctx->rp;
        xt += VMI_CALL;
        BCIstepVM(ctx, xt);             // trigger call to xt
        while (rdepth != ctx->rp) {
            BCIstepVM(ctx, 0);          // execute instructions
            if (ctx->ior) break;        // break on error
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
        // VM_EMPTY_STACK
        uint16_t sp0 = ctx->sp;
        while (n--) {
            dupData(ctx);
            ctx->t = get32();
        }
        ctx->ior = simulate(ctx, get32()); // xt
        put8(ctx, BCI_BEGIN);           // indicate end of random chars, if any
        temp = ctx->sp - sp0;           // bytes on stack
        if (temp < 0) ctx->ior = VM_STACK_UNDERFLOW;
        else for (n = 0; n < temp; n++) {
            x = ctx->t;
            dropData(ctx);
            ds[n] = x;
        }
        put8(ctx, n);                   // stack depth
        while (n--) {
            put32(ctx, ds[n]);
        }
        put32(ctx, ReadCell(ctx, 0));   // return packed status
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
            case VM_SHUTDOWN_PIN: ctx->status = BCI_STATUS_SHUTDOWN;  break;
            case VM_SLEEP_PIN:    ctx->status = BCI_STATUS_STOPPED;   break;
            case VM_RESET_PIN:    BCIinitial(ctx);                    break;
            default: break;
        }
        break;
    default:
        ctx->ior = BCI_BAD_COMMAND;
    }
    putN(ctx, ctx->ior, 2);
    BCIsendFinal(ctx->id);
}
