#ifndef __BCIHW_H__
#define __BCIHW_H__

#include "bci.h"

VMcell_t BCIVMioRead (vm_ctx *ctx, VMcell_t addr);
void BCIVMioWrite (vm_ctx *ctx, VMcell_t addr, VMcell_t data);

// Internal Flash
void FlashUnlock(uint8_t *addr);
void FlashLock(void);
void FlashErase(uint32_t sector);
void FlashWrite(uint8_t *dest, const uint8_t *src, uint16_t bytes);
uint32_t CRC32(uint8_t* addr, uint32_t len);

// External Flash
int NVMbeginRead (uint32_t faddr);
int NVMbeginWrite (uint32_t faddr);
uint32_t NVMread (int bytes);
void NVMwrite (uint32_t n, int bytes);
void NVMendRW (void);

// API functions in VM's execution table
VMcell_t API_NVMbeginRead   (vm_ctx *ctx);
VMcell_t API_NVMbeginWrite  (vm_ctx *ctx);
VMcell_t API_NVMread        (vm_ctx *ctx);
VMcell_t API_NVMwrite       (vm_ctx *ctx);
VMcell_t API_NVMendRW       (vm_ctx *ctx);
VMcell_t API_Emit           (vm_ctx *ctx);
VMcell_t API_umstar         (vm_ctx *ctx);
VMcell_t API_mudivmod       (vm_ctx *ctx);

VMcell_t API_LCDraw         (vm_ctx* ctx);
VMcell_t API_LCDpacked      (vm_ctx* ctx);
VMcell_t API_LCDFG	        (vm_ctx* ctx);
VMcell_t API_LCDBG          (vm_ctx* ctx);

#endif /* __BCIHW_H__ */
