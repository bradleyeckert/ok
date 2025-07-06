#ifndef __BCIHW_H__
#define __BCIHW_H__

#include "bci.h"

VMcell_t BCIVMioRead (vm_ctx *ctx, VMcell_t addr);
void BCIVMioWrite (vm_ctx *ctx, VMcell_t addr, VMcell_t data);

void FlashUnlock(uint8_t *addr);
void FlashLock(void);
void FlashErase(uint32_t sector);
void FlashWrite(uint8_t *dest, const uint8_t *src, uint16_t bytes);

VMcell_t NVMbeginRead  (vm_ctx *ctx);
VMcell_t NVMbeginWrite (vm_ctx *ctx);
VMcell_t NVMread       (vm_ctx *ctx);
VMcell_t NVMwrite      (vm_ctx *ctx);
VMcell_t NVMendRW      (vm_ctx *ctx);
VMcell_t API_Emit      (vm_ctx *ctx);
VMcell_t API_umstar    (vm_ctx *ctx);
VMcell_t API_mudivmod  (vm_ctx *ctx);
VMcell_t API_LCDout    (vm_ctx* ctx);

uint32_t CRC32(uint8_t *addr, uint32_t len);

#endif /* __BCIHW_H__ */
