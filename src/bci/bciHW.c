#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "bci.h"
#include "bciHW.h"

#ifdef HOST_ONLY
#ifdef GUItype // using simulated LCD
#include "../../windows/withLCD/TFTsim.h"
#endif
#else
#include "main.h" // STM32-specific includes
#endif

#define THIRD ctx->DataStack[ctx->sp]

/*
This file contains common API functions for the VM and BCI.
The BCI uses it to access non-volatile memory (simulated external SPI Flash).

All versions of this file should include the generic C simulated hardware as
well as MCU-specific equivalent functions. HOST_ONLY indicates that it is
compiled into the host VM, not on an MCU target.
*/

uint32_t BigRAMbuffer[RAMBUFSIZE];

VMcell_t API_BigRAMfetch(vm_ctx* ctx) {
    uint32_t r;
    uint32_t addr = ctx->t;
    if (addr >= RAMBUFSIZE) return 0;
    memcpy(&r, &BigRAMbuffer[addr], 4);
    return r;
}

VMcell_t API_BigRAMstore(vm_ctx* ctx) {
    uint32_t addr = ctx->t;
    if (addr >= RAMBUFSIZE) return 0;
    memcpy(&ctx->n, &BigRAMbuffer[addr], 4);
    return 0;
}

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

#ifdef HOST_ONLY
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
#else // STM32 CRC hardware
extern CRC_HandleTypeDef hcrc; // in main.c
uint32_t CRC32(uint8_t *addr, uint32_t len) {
    uint32_t crc = HAL_CRC_Calculate(&hcrc, (uint32_t *)addr, len);
    return ~crc;
}
#endif

// Interpreter for unpacking bitmap glyphs

#ifdef GUItype // using an LCD
#include "../LCD/gLCD.h"

VMcell_t API_LCDraw(vm_ctx* ctx) {
    return TFTLCDraw(ctx->n, ctx->t); // use the LCD simulator
}

VMcell_t API_LCDparm(vm_ctx* ctx) {
    return LCDgetParm(ctx->t);
}

VMcell_t API_LCDparmSet(vm_ctx* ctx) {
    LCDsetParm(ctx->t, ctx->n);
    return 0;
}

VMcell_t API_LCDchar(vm_ctx* ctx) {
    LCDchar(ctx->t);
    return 0;
}

VMcell_t API_LCDcharWidth(vm_ctx* ctx) {
    return LCDcharWidth(ctx->t);
}

VMcell_t API_LCDfill(vm_ctx* ctx) {
    LCDfill(ctx->n, ctx->t);
    return 0;
}
#else
VMcell_t API_LCDraw(vm_ctx* ctx) { return 0; }
VMcell_t API_LCDparm(vm_ctx* ctx) { return 0; }
VMcell_t API_LCDparmSet(vm_ctx* ctx) { return 0; }
VMcell_t API_LCDchar(vm_ctx* ctx) { return 0; }
VMcell_t API_LCDcharWidth(vm_ctx* ctx) { return 0; }
VMcell_t API_LCDfill(vm_ctx* ctx) { return 0; }
#endif


/***************************************************************************
Non-volatile memory

VM_FLASHSIZE        size of external Flash data in bytes
VM_FLASHFILENAME    initialization file
NVMbeginRead        Set the address for reading, return ior
NVMbeginWrite       Set the address for writing, return ior
NVMread             Read the next big-endian (up to 4-byte) value
NVMwrite            Write the next big-endian (up to 4-byte) value
NVMendRW            Deselect chip
NVMgetID            return 3-byte ID code

The write sequence auto-erases the sector before writing when the address is 
on a sector boundary. Byte writes insert new "page write" commands as needed.

For internal Flash, NVMgetID returns a pseudo-ID with the following fields:
  bits 0-7    number of sectors available for NVM
  bits 8-15   0x11 = log2 of sector size, 17 = 128KB
  bits 16-23  0x7 = device STM32H7
*/

uint32_t NVMgetID(void) {
#ifdef H7_HALF_FLASH
    return (8 - H7_SECTOR_NVM) + 0x071100;
#else
    return (16 - H7_SECTOR_NVM) + 0x071100;
#endif
}

#ifdef HOST_ONLY // simulate a W25Q32JVSSIQ
#include <stdio.h>

uint8_t NVMloaded = 0;
uint8_t NVMsimMem[VM_FLASHSIZE];
uint32_t NVMaddress;
int NVMmode = 0;

void NVMendRW(void) {
    NVMmode = 0;
}

int NVMbeginRead (uint32_t faddr){
//	printf("NVMbeginRead[%d](%d)\n", faddr, NVMmode);
    if (NVMloaded == 0) {
        NVMloaded = 1;
        FILE *fp = fopen(VM_FLASHFILENAME, "rb");
        if (fp != NULL) { // ignore missing file
            fread(NVMsimMem, 1, VM_FLASHSIZE, fp);
            fclose(fp);
        }
    }
    NVMendRW();
    NVMaddress = 0;
    if (faddr >= VM_FLASHSIZE) return BCI_IOR_INVALID_ADDRESS;
    NVMaddress = faddr;
    NVMmode = 1;
    return 0;
}

int NVMbeginWrite (uint32_t faddr){
    NVMaddress = 0;
    NVMmode = 0;
    if (faddr >= VM_FLASHSIZE) return BCI_IOR_INVALID_ADDRESS;
    NVMaddress = faddr;
    NVMmode = 2;
    return 0;
}

uint32_t NVMread (int bytes){
	if (NVMaddress == 0) return 0;
//    printf("NVMread[%d]\n", bytes);
    if (NVMmode != 1) printf("NVM error %d: Not in READ mode\n", NVMmode);
    uint32_t r = 0;
    while(bytes--) {
        r = (r << 8) + NVMsimMem[NVMaddress++];
    }
    return r;
}

int16_t NVMwrite (uint32_t n, int bytes){
    if (NVMaddress == 0) return 0;
    if (NVMmode != 2) printf("NVM error: Not in WRITE mode\n");
    while(bytes--) {
        if ((NVMaddress & 0x1FFFF) == 0) {
            memset(&NVMsimMem[NVMaddress], 0xFF, 0x20000);
        }
        NVMsimMem[NVMaddress++] = n >> (bytes << 3);
    }
    return 0;
}

static void slurpNVM(uint8_t* dest, uint32_t bytes) {
    while (bytes--) {
        *dest++ = (uint8_t)NVMread(1);  // read one byte at a time
    }
}

// Initialize the hardware, usually by reading the NVM.
void BCIHWinit(vm_ctx* ctx) {
    NVMbeginRead(20);                   // blob 1 = VM initialization
    uint16_t sector = NVMread(2);       // read sector size
    uint32_t addr0 = sector << 16;
    if (sector == 0) addr0 = 0x1000;
    NVMbeginRead(addr0 + 8);            // -> csize, tsize, data...
    uint32_t codebytes = NVMread(4);
    uint32_t textbytes = NVMread(4);
    slurpNVM((uint8_t*)ctx->CodeMem, codebytes); // read code memory
    slurpNVM((uint8_t*)ctx->TextMem, textbytes); // read text memory
    NVMendRW();                         // deselect chip
#ifdef GUItype // using an LCD
    LCDinit();                          // initialize font rendering from NVM
#endif
}


#else
#include "stm32h7xx_hal.h"
/* NVM is implemented in the STM32H7 internal flash memory starting at sector
H7_SECTOR_NVM (typically sector 2, address 0x08040000). Writing is done in
32-byte chunks, with automatic sector pre-erase on 128K boundaries.

NVMaddress is the physical address for the next read or write.
It is set by NVMbeginRead or NVMbeginWrite, and cleared by NVMendRW.
If it is 0, no read or write is done.
*/
uint32_t NVMaddress;    // physical address for next read/write

uint32_t nwrbuffer[8];  // 32-byte write buffer
uint8_t nwridx;

/* The embedded flash memory implements the buffering of consecutive read
requests in the same bank.  Section 4.3.7 of the reference manual.
Reads are big-endian. Reads outside of the NVM area return 0.
*/
uint32_t NVMread(int bytes) {
	if (NVMaddress == 0) return 0;
#ifdef H7_HALF_FLASH
    if (NVMaddress & 0x080000) {
		NVMaddress = 0;
        return 0;
    }
#else
    if (NVMaddress & 0x200000) {
        NVMaddress = 0;
        return 0;
    }
#endif
    uint32_t r = 0;
    while (bytes--) {
        r = (r << 8) + *(__IO uint8_t*)NVMaddress++;
    }
    return r;
}
/* convert from NVM address to physical address.Returns 0 if invalid.
Return value: MSB = bank (1 or 2), LSBs = sector number
faddr is byte address within the NVM area, starting at 0.
STM32H7xGx has Bank 1 sectors 2-3 and Bank 2 sectors 0-3 available for NVM.
STM32H7xIx has Bank 1 sectors 2-7 and Bank 2 sectors 0-7 available for NVM.
*/
uint32_t nphyaddr(uint32_t faddr) {
#ifdef H7_HALF_FLASH
    if (faddr >= (6 << 17)) return 0;
    if (faddr >= (4 << 17)) faddr += 0x80000;     // skip to Bank 2
#else
    if (faddr >= (14 << 17)) return 0;
#endif
    return (H7_SECTOR_NVM << 17) + H7_SECTOR_BASE + faddr;
}

int NVMbeginRead(uint32_t faddr) {
    NVMaddress = nphyaddr(faddr);
    if (NVMaddress == 0) return BCI_IOR_INVALID_ADDRESS;
    return 0;
}

int NVMbeginWrite(uint32_t faddr) {
    memset(nwrbuffer, 0xFF, sizeof(nwrbuffer));
    nwridx = faddr & 0x1F;
    return NVMbeginRead(faddr);
}

int16_t NVMwrite(uint32_t n, int bytes) {
    if (NVMaddress == 0) return BCI_IOR_INVALID_ADDRESS;
    while (bytes--) {
        uint8_t bitpos = (nwridx & 3) << 3;
        nwrbuffer[nwridx >> 2] &= (((n >> bitpos) & 0xFF) | ~(0xFF << bitpos));
        nwridx++;
        if (nwridx == sizeof(nwrbuffer)) {      // write the buffer
#ifdef H7_HALF_FLASH
			if (NVMaddress & 0x080000) {        // sectors 4 to 7 not available
                NVMaddress = BCI_IOR_INVALID_ADDRESS;
                return 0;
            }
#else
			if (NVMaddress & 0x200000) {        // sectors above 15 not available
                NVMaddress = BCI_IOR_INVALID_ADDRESS;
                return 0;
            }
#endif
            if ((NVMaddress & 0x1FFFF) == 0) {  // erase sector on 128K boundary
                int16_t sector = (NVMaddress - H7_SECTOR_BASE) >> 17;
                if (sector < 1) return BCI_IOR_INVALID_ADDRESS;
                uint16_t bank = sector >> 3;
                if (bank > 1) return BCI_IOR_INVALID_ADDRESS;
                sector = sector & 7;
                FLASH_EraseInitTypeDef EraseInitStruct;
                uint32_t SectorError = 0;
                HAL_FLASH_Unlock();
                EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
                EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
                EraseInitStruct.Sector = sector;
                EraseInitStruct.Banks = FLASH_BANK_1;
                if (bank) EraseInitStruct.Banks = FLASH_BANK_2;
                EraseInitStruct.NbSectors = 1;
                if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK) {
                    HAL_FLASH_Lock();
                    return BCI_IOR_INVALID_ADDRESS;         // erase error
                }
            }
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, NVMaddress, (uint32_t)nwrbuffer);
            NVMaddress += sizeof(nwrbuffer);
            HAL_FLASH_Lock();
            nwridx = 0;
            memset(nwrbuffer, 0xFF, sizeof(nwrbuffer));
        }
    }
    return 0;
}
void NVMendRW(void) {
    NVMaddress = 0;
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
VMcell_t API_NVMendRW(vm_ctx* ctx) {
    NVMendRW();
    return 0;
}
VMcell_t API_NVMID(vm_ctx* ctx) {
    return NVMgetID();
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

// Timer interface

#ifdef HOST_ONLY
#include "../host/tools.h"

VMcell_t API_Milliseconds(vm_ctx* ctx) {
    return (VMcell_t)GetMicroseconds() / 1000;
}

uint32_t g_VMbuttons;
VMcell_t API_Buttons(vm_ctx* ctx) {
    return g_VMbuttons; // return the button state
}

#else

extern uint32_t msec_counter;

VMcell_t API_Milliseconds(vm_ctx* ctx) {
    return msec_counter;
}

VMcell_t API_Buttons(vm_ctx* ctx) {
	int button = ~HAL_GPIO_ReadPin (BTN_GPIO_Port, BTN_Pin);
    return button;
}

#endif // HOST_ONLY

VMcell_t API_CRC32(vm_ctx* ctx) {
    ctx->n = CRC32((uint8_t*)&ctx->DataMem[ctx->n], ctx->t);
    return 0;
}
