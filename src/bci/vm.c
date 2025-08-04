#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "vm.h"
#include "bciHW.h"

#ifdef BCI_TRACE
#include <stdio.h>
#define PRINTF  printf
#else
#define PRINTF(...) do { } while (0)
#endif

VMcell_t VMreadCell(vm_ctx *ctx, VMcell_t addr) {
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

void VMwriteCell(vm_ctx *ctx, VMcell_t addr, VMcell_t x) {
    if (addr < DATASIZE) ctx->DataMem[addr] = x;
    else ctx->ior = BCI_IOR_INVALID_ADDRESS;
}

static void VMdupData(vm_ctx *ctx) {
    ctx->sp = (ctx->sp + 1) & (VM_STACKSIZE - 1);
    ctx->DataStack[ctx->sp] = ctx->n;
    ctx->n = ctx->t;
}

void VMpushData(vm_ctx *ctx, VMcell_t x) {
    VMdupData(ctx);
    ctx->t = x;
}

VMcell_t VMpopData(vm_ctx *ctx) {
    VMcell_t r = ctx->t;
    ctx->t = ctx->n;
    ctx->n = ctx->DataStack[ctx->sp];
    ctx->sp = (ctx->sp - 1) & (VM_STACKSIZE - 1);
    return r;
}

static void VMpushReturn(vm_ctx *ctx, VMcell_t x) {
    ctx->rp = (ctx->rp + 1) & (VM_STACKSIZE - 1);
    ctx->ReturnStack[ctx->rp] = ctx->r;
    ctx->r = x;
}

static VMcell_t VMpopReturn(vm_ctx *ctx) {
    VMcell_t r = ctx->r;
    ctx->r = ctx->ReturnStack[ctx->rp];
    ctx->rp = (ctx->rp - 1) & (VM_STACKSIZE - 1);
    return r;
}

static const uint8_t stackeffects[32] = VM_STACKEFFECTS;

// Single-step the VM.
// inst = instruction; If 0, fetch inst from code memory.
// ctx->ior should be 0 upon entering the function.
// Returns 1 if running and ready for BCI execution

// cycles is incremented once per instruction whether the
// opcode is 5-bit or whole-instruction.

#define IMASK2 ((1 << (VM_INSTBITS - 2)) - 1)
static int ops0001(vm_ctx *ctx, int inst);

static VMcell_t VMapiCall(vm_ctx *ctx, int fn);
int VMstep(vm_ctx *ctx, VMinst_t inst);
int VMsteps(vm_ctx *ctx, uint32_t times) {
    while (times--) VMstep(ctx, 0);
    return ctx->ior;
}

int VMstep(vm_ctx *ctx, VMinst_t inst){
    int retval = 0;
    VMcell_t pc = ctx->pc;
    if (inst == 0) {
        inst = ctx->CodeMem[pc++];
    }
    if (inst & (1 << (VM_INSTBITS - 1))) { // MSB
        if (inst & (1 << (VM_INSTBITS - 2))) pc = VMpopReturn(ctx);
        inst &= IMASK2;
        int i = SLOT0_POSITION + 5;
        while (i > 0) {
            i -= 5;
            uint8_t uop;
            if (i < 0) uop = inst & LAST_SLOT_MASK;
            else uop = (inst >> i) & 0x1F;
            VMcell_t t = ctx->t;
            VMcell_t n = ctx->n;
            uint8_t se = stackeffects[uop];
            if (se & 1) {
                VMdupData(ctx);
            } else if (se & 2) {
                VMpopData(ctx);
            }
            switch (uop) {
                // basic stack operations
#if (VM_CELLBITS == 32)
                uint64_t ud;
#endif
                case VMO_NOP:        ctx->cycles--;
                case VMO_DUP:
                case VMO_DROP:                                          break;
                case VMO_INV:        ctx->t = ~t & VM_MASK;             break;
                case VMO_TWOSTAR:    ctx->t = (t << 1) & VM_MASK;
                                     ctx->cy = (t >> (VM_CELLBITS - 1))
                                             & 1;                       break;
                case VMO_TWODIV:     ctx->t = (t & VM_SIGN) | (t >> 1);
                                     ctx->cy = t & 1;                   break;
                case VMO_TWODIVC:    ctx->t = (ctx->cy << (VM_CELLBITS - 1))
                                            | (t >> 1);
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
                case VMO_B:          ctx->t = ctx->b;                   break;
                case VMO_OVER:       ctx->t = n;                        break;
                case VMO_PUSH:       VMpushReturn(ctx, t);              break;
                case VMO_R:          ctx->t = ctx->r;                   break;
                case VMO_POP:        ctx->t = VMpopReturn(ctx);         break;
                case VMO_UNEXT:      ctx->r--;
                    if (ctx->r == 0) VMpopReturn(ctx);
                    else i = SLOT0_POSITION + 5;
                    break;
                case VMO_PLUSSTAR:   if (ctx->a & 1) {
                                         ud = (uint64_t)t + (uint64_t)n;
                                     } else {
                                         ud = (uint64_t)t;
                                     }
                                     ud = (ud << (VM_CELLBITS - 1))
                                        | (ctx->a >> 1);
                                     ctx->t = (ud >> VM_CELLBITS) & VM_MASK;
                                     ctx->a = ud & VM_MASK;             break;
                // memory operations
                case VMO_BSTORE:     ctx->b = t;                        break;
                case VMO_A:          ctx->t = ctx->a;                   break;
                case VMO_ASTORE:     ctx->a = t;                        break;
                case VMO_FETCHA:     ctx->t = VMreadCell(ctx, ctx->a);  break;
                case VMO_FETCHAPLUS: ctx->t = VMreadCell(ctx, ctx->a++);break;
                case VMO_FETCHB:     ctx->t= BCIVMioRead(ctx, ctx->b);  break;
                case VMO_FETCHBPLUS: ctx->t= BCIVMioRead(ctx, ctx->b++);break;
                case VMO_STOREA:     VMwriteCell(ctx, ctx->a, t);       break;
                case VMO_STOREAPLUS: VMwriteCell(ctx, ctx->a++, t);     break;
                case VMO_STOREB:     BCIVMioWrite(ctx, ctx->b, t);      break;
                case VMO_STOREBPLUS: BCIVMioWrite(ctx, ctx->b++, t);    break;
                default: break;
            }
            ctx->cycles++;
        }
    } else {
        uint16_t _lex = 0;
        uint32_t imm;
        int32_t immex = (ctx->lex << (VM_INSTBITS - 3))
                    | (inst & ((1 << (VM_INSTBITS - 3)) - 1));
        switch ((inst >> (VM_INSTBITS - 3)) & 3) { // upper bits 000 to 011
            case VMO_CALL: VMpushReturn(ctx, pc);
            case VMO_JUMP: pc = immex;                                  break;
            case VMO_LIT: VMdupData(ctx);  ctx->t = immex;              break;
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
                case VMO_ZOO: retval = ops0001(ctx, imm);               break;
                case VMO_AX: ctx->a = ctx->x + imm;                     break;
                case VMO_BY: ctx->b = ctx->y + imm;                     break;
                case VMO_ZBRAN: flag = (ctx->t == 0);  VMpopData(ctx);
                    if (flag) { pc = ctx->pc + immex; }                 break;
                case VMO_BRAN: pc = ctx->pc + immex;                    break;
                case VMO_PBRAN: if ((ctx->t & VM_SIGN) == 0) {
                    pc = ctx->pc + immex;
                    } break;
                case VMO_NEXT:
                    ctx->r--;
                    if (ctx->r == 0) VMpopReturn(ctx);
                    else pc = ctx->pc + immex;
                    break;
                case VMO_PY: ctx->y = 0x10000000 + (imm << 8);          break;
                case VMO_DUPAPI: VMdupData(ctx);
                case VMO_API:
                case VMO_APIDROP:
                case VMO_API2DROP: ctx->t = VMapiCall(ctx, imm);        break;
                default: break;
            }
            switch (opcode) {
                case VMO_API2DROP: VMpopData(ctx);
                case VMO_APIDROP: VMpopData(ctx);
                default: break;
            }
        }
        ctx->lex = _lex;
        ctx->cycles++;
    }
    ctx->pc = pc;
    return retval;
}

static int ops0001(vm_ctx *ctx, int inst) { // instruction zoo 0110001...
    int r = 0;
    int imm = inst & 0x7F;
    if (inst & VMI_ZOODUP) VMdupData(ctx);
    switch(imm) {
        case VMO_XSTORE:  ctx->x = ctx->t;                              break;
        case VMO_YSTORE:  ctx->y = ctx->t;                              break;
        case VMO_BCISYNC: r = 1;                                        break;
        case VMO_THROW:   ctx->ior = ctx->t;                            break;
        case VMO_XFETCH:  ctx->t = ctx->x;                              break;
        case VMO_YFETCH:  ctx->t = ctx->y;                              break;
        default: break;
    }
    if (inst & VMI_ZOODROP) VMpopData(ctx);
    return r;
}

typedef VMcell_t (*APIfn) (vm_ctx *ctx);

static const APIfn APIfns[] = {
    API_NVMbeginRead, API_NVMbeginWrite, API_NVMread, API_NVMwrite, // 0
    API_NVMendRW, API_Emit, API_umstar, API_mudivmod,               // 4
    API_LCDraw, API_LCDparmSet, API_LCDparm, API_LCDchar,           // 8
    API_LCDcharWidth
};

#define APIfs (sizeof(APIfns)/sizeof(APIfns[0]))
static VMcell_t VMapiCall(vm_ctx *ctx, int fn) {
    fn &= 0x7F;
    if (fn < APIfs) return APIfns[fn](ctx);
    return -1;
}

uint8_t VMgetRP(vm_ctx *ctx) {return ctx->rp;}
uint8_t VMgetSP(vm_ctx *ctx) {return ctx->sp;}

static const uint8_t boilerplate[BOILERPLATE_SIZE] = {
    1,                          // format 1
    VM_CELLBITS,                // bits per data cell
    VM_INSTBITS,                // bits per instruction icell
    VM_STACKSIZE - 1,           // max stack depths
    VM_STACKSIZE - 1,
    (DATASIZE >> 8) - 1,        // 256-cell pages of data memory less 1
    (CODESIZE >> 8) - 1,        // 256-icell pages of code memory less 1
    (TEXTSIZE >> 10) - 1,       // 1K-cell pages of text memory less 1
    TEXTORIGIN >> 12,           // start address of text memory
};
#include <stdio.h>

void VMreset(vm_ctx *ctx) {
    memset(ctx, 0, sizeof(ctx->DataMem)); // data space initializes to 0
    memset(ctx, 0, 64);         // wipe all up to stacks
    ctx->boilerplate = boilerplate;
    ctx->DataMem[0] = 10;
    ctx->status = BCI_STATUS_STOPPED;
    ctx->statusNew = BCI_STATUS_STOPPED;
    VMinst_t blank = (VMinst_t)((BLANK_FLASH_BYTE << 24)
                              | (BLANK_FLASH_BYTE << 16)
                              | (BLANK_FLASH_BYTE << 8)
                              |  BLANK_FLASH_BYTE);
    if (ctx->CodeMem[0] != (VMinst_t)blank) { // got code?
        ctx->status = BCI_STATUS_RUNNING;
        PRINTF("\nReset, VM is running\n");
    } else {
        PRINTF("\nReset, VM is stopped\n");
    }
	BCIHWinit();                // initialize simulated hardware
}

