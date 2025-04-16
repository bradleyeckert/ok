#ifndef __BCIHW_H__
#define __BCIHW_H__

#include "bci.h"

VMcell_t BCIVMioRead (vm_ctx *ctx, VMcell_t addr);
void BCIVMioWrite (vm_ctx *ctx, VMcell_t addr, VMcell_t data);

VMcell_t NVMbeginRead (vm_ctx *ctx);
VMcell_t NVMbeginWrite (vm_ctx *ctx);
VMcell_t NVMread  (vm_ctx *ctx);
VMcell_t NVMwrite (vm_ctx *ctx);
VMcell_t NVMendRW (vm_ctx *ctx);

#endif /* __BCIHW_H__ */
