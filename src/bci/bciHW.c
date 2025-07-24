#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "bci.h"
#include "bciHW.h"

#ifdef GUItype
#include "../../windows/withLCD/TFTsim.h"
#endif

#define THIRD ctx->DataStack[ctx->sp]

VMcell_t API_Emit (vm_ctx *ctx){
    uint32_t codepoint = ctx->t;
    if (codepoint < 0x80) {
        BCIsendChar(ctx->id, codepoint);
        return 0;
    }
    if (codepoint < 0x800) {
        BCIsendChar(ctx->id, (char)(0xC0 | (codepoint >> 6)));
        goto last;
    }
    if (codepoint < 0x10000) {
        BCIsendChar(ctx->id, (char)(0xE0 | (codepoint >> 12)));
        goto thrd;
    }
      BCIsendChar(ctx->id, (char)(0xF0 | (codepoint >> 18)));
      BCIsendChar(ctx->id, (char)(0x80 | ((codepoint >> 12) & 0x3F)));
thrd: BCIsendChar(ctx->id, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
last: BCIsendChar(ctx->id, (char)(0x80 | (codepoint & 0x3F)));
    return 0;
}

static VMcell_t API_umstar_x (vm_ctx *ctx, int sign) {
	VMdblcell_t a = (VMdblcell_t)(ctx->t & VM_MASK);
	VMdblcell_t b = (VMdblcell_t)(ctx->n & VM_MASK);
    int invert = 0;
    if (sign) {
        invert = (a ^ b) & VM_SIGN;
        if (a & VM_SIGN) a = (a ^ VM_MASK) + 1;
        if (b & VM_SIGN) b = (b ^ VM_MASK) + 1;
    }
    VMdblcell_t p = a * b;
    if (invert) {
        p = -(signed)p;
    }
    ctx->n = (VMcell_t)p & VM_MASK;
    return (VMcell_t)(p >> VM_CELLBITS);
}

VMcell_t API_umstar (vm_ctx *ctx) {
    return API_umstar_x(ctx, 0);
}

VMcell_t API_mudivmod (vm_ctx *ctx) {
/* MU/MOD ( dividendL dividendH divisor -- rem ql qh ) */
	VMdblcell_t dividend = ((VMdblcell_t)(ctx->n & VM_MASK) << VM_CELLBITS) | (THIRD & VM_MASK);
	VMdblcell_t divisor = (VMdblcell_t)(ctx->t & VM_MASK);
	VMdblcell_t q = dividend / divisor;
	THIRD = (VMcell_t)(dividend % divisor);
    ctx->n = (VMcell_t)(q & VM_MASK);
    return (VMcell_t)(q >> VM_CELLBITS) & VM_MASK;
}

// Some targets have hardware support for this
uint32_t CRC32(uint8_t *addr, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    while (len--) {
        uint32_t byte = *addr++;
        crc = crc ^ byte;
        for (int j = 7; j >= 0; j--) {
            uint32_t mask = ~(crc & 1) + 1;
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
}

VMcell_t NVMbeginRead (vm_ctx *ctx){
    return 0;
}
VMcell_t NVMbeginWrite (vm_ctx *ctx){
    return 0;
}
VMcell_t NVMread (vm_ctx *ctx){
    return 0;
}
VMcell_t NVMwrite (vm_ctx *ctx){
    return 0;
}
VMcell_t NVMendRW (vm_ctx *ctx){
    return 0;
}

// Absolute memory access - your PC's MMU will crash the app

VMcell_t BCIVMioRead (vm_ctx *ctx, VMcell_t addr){
    return *(VMcell_t*)(size_t)(addr << C_BYTESHIFT);
}

void BCIVMioWrite (vm_ctx *ctx, VMcell_t addr, VMcell_t data){
    *(VMcell_t*)(size_t)(addr << C_BYTESHIFT) = data;
}

// Write to flash region using byte address

void FlashUnlock(uint8_t *addr) { }
void FlashLock(void) { }
void FlashErase(uint32_t sector) { }

void FlashWrite(uint8_t *dest, const uint8_t *src, uint16_t bytes) {
    memcpy(dest, src, bytes);
}

// Interpreter for unpacking bitmap glyphs

#ifdef GUItype

uint32_t API_LCDraw(vm_ctx* ctx) {
    return TFTLCDraw(ctx->t); // use the LCD simulator
}

uint32_t LCD_FGcolor;   // packed foreground color
uint32_t LCD_BGcolor;   // packed background color

VMcell_t API_LCDFG(vm_ctx* ctx) { return LCD_FGcolor; }
VMcell_t API_LCDBG(vm_ctx* ctx) { return LCD_BGcolor; }
VMcell_t API_LCDFGset(vm_ctx* ctx) { LCD_FGcolor = ctx->t;  return 0; }
VMcell_t API_LCDBGset(vm_ctx* ctx) { LCD_BGcolor = ctx->t;  return 0; }

/*
API_LCDpacked interprets a 32-bit packed field to send foreground and
background colors to the LCD. The data is typically a monochrome bitmap.
*/

#else
VMcell_t API_LCDraw   (vm_ctx* ctx) { return -1; }
VMcell_t API_LCDpacked(vm_ctx* ctx) { return -1; }
VMcell_t API_LCDFG    (vm_ctx* ctx) { return -1; }
VMcell_t API_LCDBG    (vm_ctx* ctx) { return -1; }
VMcell_t API_LCDFGset (vm_ctx* ctx) { return -1; }
VMcell_t API_LCDBGset (vm_ctx* ctx) { return -1; }
#endif
