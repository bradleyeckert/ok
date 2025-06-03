#ifndef __BCI_H__
#define __BCI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "vm.h"

#define MaxBCIresponseSize      1030
#define MOLE_PROTOCOL              0
#define DEFAULT_BAUDRATE      115200
#define DEFAULT_HOSTPORT           2
#define DEFAULT_TARGETPORT         3
#define HANG_LIMIT_MS           3000 /* milliseconds to wait for a response */

#define BLANK_FLASH_BYTE        0xFF /* byte values after flash erase */
#define FLASH_BLOCK_SIZE         128 /* bytes in a flash programming block */
#define BCI_CYCLE_LIMIT     10000000 /* number of cycles before VM is hung */

#define BCI_STACK_OVERFLOW        -3
#define BCI_STACK_UNDERFLOW       -4
#define BCI_IOR_INVALID_ADDRESS   -9
#define BCI_BAD_COMMAND          -84
#define BCI_SHUTDOWN_PIN       10000
#define BCI_RESET_PIN          10123
#define BCI_SLEEP_PIN          10321
#define BCI_BEGIN                252 /* beginning-of-message */
#define BCI_ADMIN_ACTIVE        0x55

#define BCIFN_READ                 1
#define BCIFN_WRITE                2
#define BCIFN_EXECUTE              3
#define BCIFN_GET_CYCLES           4
#define BCIFN_CRC                  5
#define BCIFN_WRCODE               6
#define BCIFN_WRTEXT               7
#define BCIFN_SECTOR_ERASE         8
#define BCIFN_STROBE               9
#define BCIFN_ACCESS_DENIED       10

#define BCI_STATUS_RUNNING         0
#define BCI_STATUS_STOPPED         1
#define BCI_STATUS_SHUTDOWN        2

/** Thin-client communication interface
 * @param ctx VM identifier, NULL if not simulated (no context available)
 * @param id VM identifier, used when ctx=NULL
 * @param src Command string address
 * @param length Command string length
 */
void BCIhandler(vm_ctx *ctx, const uint8_t *src, uint16_t length);

// Needs these externals

void StopVMthread(vm_ctx *ctx);
void BCIsendInit(int id);
void BCIsendChar(int id, uint8_t c);
void BCIsendFinal(int id);

#ifdef __cplusplus
}
#endif

#endif /* __BCI_H__ */
