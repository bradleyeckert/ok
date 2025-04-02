#ifndef __BCIHW_H__
#define __BCIHW_H__

#include <stdint.h>

uint32_t BCIVMioRead  (vm_ctx *ctx, uint32_t addr);
void     BCIVMioWrite (vm_ctx *ctx, uint32_t addr, uint32_t x);
uint32_t BCIVMcodeRead(vm_ctx *ctx, uint32_t addr);

#endif /* __BCIHW_H__ */
