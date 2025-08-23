#ifndef __BCIHW_H__
#define __BCIHW_H__
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "bci.h"

VMcell_t BCIVMioRead (vm_ctx *ctx, VMcell_t addr);
void BCIVMioWrite (vm_ctx *ctx, VMcell_t addr, VMcell_t data);
uint32_t TFTLCDraw(uint32_t n, uint8_t mode);
void BCIHWinit(vm_ctx* ctx);

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
VMcell_t API_NVMbeginRead   (vm_ctx* ctx);
VMcell_t API_NVMbeginWrite  (vm_ctx* ctx);
VMcell_t API_NVMread        (vm_ctx* ctx);
VMcell_t API_NVMwrite       (vm_ctx* ctx);
VMcell_t API_NVMendRW       (vm_ctx* ctx);
VMcell_t API_NVMID          (vm_ctx* ctx);

VMcell_t API_Emit           (vm_ctx* ctx);
VMcell_t API_umstar         (vm_ctx* ctx);
VMcell_t API_mudivmod       (vm_ctx* ctx);

VMcell_t API_LCDraw         (vm_ctx* ctx);
VMcell_t API_LCDparm        (vm_ctx* ctx);
VMcell_t API_LCDparmSet     (vm_ctx* ctx);
VMcell_t API_LCDchar        (vm_ctx* ctx);
VMcell_t API_LCDcharWidth   (vm_ctx* ctx);
VMcell_t API_LCDfill        (vm_ctx* ctx);

VMcell_t API_Milliseconds   (vm_ctx* ctx);
VMcell_t API_Buttons        (vm_ctx* ctx);
VMcell_t API_CRC32          (vm_ctx* ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __BCIHW_H__ */
