#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "bci.h"
#include "bciHW.h"

uint64_t NVMaddress;

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

VMcell_t BCIVMioRead (vm_ctx *ctx, VMcell_t addr){
    return 0;
}
void BCIVMioWrite (vm_ctx *ctx, VMcell_t addr, VMcell_t data){
}
