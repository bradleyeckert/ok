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

/*
This file contains common API functions for the VM and BCI.
The BCI uses it to access non-volatile memory (simulated external SPI Flash).

All versions of this file should include the generic C simulated hardware as
well as MCU-specific equivalent functions. HOST_ONLY indicates that it is
compiled into the host VM, not on an MCU target.
*/

#define HOST_ONLY

// Output to the mole output buffer with BCIsendChar.
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

/***************************************************************************
Non-volatile memory

VM_FLASHSIZE        size of external Flash data in bytes
VM_FLASHFILENAME    initialization file
NVMbeginRead        Set the address for reading, return ior
NVMbeginWrite       Set the address for writing, return ior
NVMread             Read the next big-endian (up to 4-byte) value
NVMwrite            Write the next big-endian (up to 4-byte) value
NVMendRW            Deselect chip

The write sequence auto-erases the 4K sector before writing when the address
is on a 4K boundary. Byte writes insert new "page write" commands as needed.
*/

#ifdef HOST_ONLY // simulate a W25Q32JVSSIQ

uint8_t NVMloaded = 0;
uint8_t NVMsimMem[VM_FLASHSIZE];
uint32_t NVMaddress;
int NVMmode = 0;

int NVMbeginRead (uint32_t faddr){
    if (NVMloaded == 0) {
        NVMloaded = 1;
        FILE *fp = fopen(VM_FLASHFILENAME, "rb");
        if (fp != NULL) { // ignore missing file
            fread(NVMsimMem, 1, VM_FLASHSIZE, fp);
            fclose(fp);
        }
    }
    NVMaddress = faddr;
    NVMmode = 0;
    if (faddr >= VM_FLASHSIZE) return BCI_IOR_INVALID_ADDRESS;
    NVMmode = 1;
    return 0;
}

int NVMbeginWrite (uint32_t faddr){
    NVMaddress = faddr;
    NVMmode = 0;
    if (faddr >= VM_FLASHSIZE) return BCI_IOR_INVALID_ADDRESS;
    NVMmode = 2;
    return 0;
}

uint32_t NVMread (int bytes){
    if (NVMmode != 1) printf("NVM error: Not in READ mode\n");
    uint32_t r = 0;
    while(bytes--) {
        r = (r << 8) + NVMsimMem[NVMaddress++];
    }
    return r;
}
void NVMwrite (uint32_t n, int bytes){
    if (NVMmode != 2) printf("NVM error: Not in WRITE mode\n");
    while(bytes--) {
        if ((NVMaddress & 0xFFF) == 0) {
            memset(&NVMsimMem[NVMaddress], 0xFF, 0x1000);
        }
        NVMsimMem[NVMaddress++] = n >> (bytes << 3);
    }
}
void NVMendRW (void){
    NVMmode = 0;
}

#else

int NVMbeginRead (uint32_t faddr){
    return 0;
}
int NVMbeginWrite (uint32_t faddr){
    return 0;
}
uint32_t NVMread (int bytes){
    return 0;
}
void NVMwrite (uint32_t n, int bytes){
}
void NVMendRW (void){
}

#endif

VMcell_t API_NVMbeginRead (vm_ctx *ctx){
    return NVMbeginRead(ctx->t);
}
VMcell_t API_NVMbeginWrite (vm_ctx *ctx){
    return NVMbeginWrite(ctx->t);
}
VMcell_t API_NVMread (vm_ctx *ctx){
    return NVMread(ctx->t);
}
VMcell_t API_NVMwrite (vm_ctx *ctx){
    NVMwrite(ctx->n, ctx->t);
    return 0;
}
VMcell_t API_NVMendRW (vm_ctx *ctx){
    NVMendRW();
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
    return TFTLCDraw(ctx->n, ctx->t); // use the LCD simulator
}

#else
VMcell_t API_LCDraw  (vm_ctx* ctx) { return -1; }
VMcell_t API_LCDFG   (vm_ctx* ctx) { return -1; }
VMcell_t API_LCDBG   (vm_ctx* ctx) { return -1; }
#endif
