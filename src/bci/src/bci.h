#ifndef __BCI_H__
#define __BCI_H__

#include <stdint.h>

#define BCI_IOR_INVALID_ADDRESS   -9

#define BCI_INPUT_OVERFLOW         1
#define BCI_ACK                  254
#define BCI_NACK                 253
#define BCI_INPUT_UNDERFLOW      252

#define VM_CELLBITS               32
#define VM_SIGN     (1 << (VM_CELLBITS - 1))
#if (VM_CELLBITS == 32)
#define VM_MASK           0xFFFFFFFF
#else
#define VM_MASK     ((1 << VM_CELLBITS) - 1)
#endif

#define VMO_NOP                 0x00
#define VMO_INV                 0x01
#define VMO_DUP                 0x02
#define VMO_ASTORE              0x03
#define VMO_PLUS                0x04
#define VMO_XOR                 0x05
#define VMO_AND                 0x06
#define VMO_DROP                0x07
#define VMO_SWAP                0x08
#define VMO_TWOSTAR             0x09
#define VMO_OVER                0x0A
#define VMO_CYSTORE             0x0B
#define VMO_FETCHA              0x0C
#define VMO_FETCHAPLUS          0x0D
#define VMO_FETCHB              0x0E
#define VMO_FETCHBPLUS          0x0F
#define VMO_TWODIVC             0x10
#define VMO_TWODIV              0x11
#define VMO_STOREA              0x14
#define VMO_STOREAPLUS          0x15
#define VMO_STOREB              0x16
#define VMO_STOREBPLUS          0x17
#define VMO_UNEXT               0x18
#define VMO_PUSH                0x1B
#define VMO_CY                  0x1C
#define VMO_A                   0x1D
#define VMO_R                   0x1E
#define VMO_POP                 0x1F

#define BCIFN_BOILER   0
#define BCIFN_READ     1
#define BCIFN_WRITE    2
#define BCIFN_EXECUTE  3
#define BCIFN_CRC      4
#define BCIFN_READREG  5
#define BCIFN_WRITEREG 6

#define DATASIZE                1024
#define CODESIZE                2048
#define STACKSIZE                 16
#define BCI_EMPTY_STACK   0xAAAAAAAA
#define BCI_STATUS_SINGLE          0
#define BCI_STATUS_STOPPED         1
#define BCI_STATUS_RUNNING         2
#define BCI_ACCESS_PERIPHERALS     1
#define BCI_ACCESS_CODESPACE       2
#define BCI_DEBUG_ACCESS           3
#define BCI_CYCLE_LIMIT     10000000

typedef void (*BCITXinitFn)(void);
typedef void (*BCITXputcFn)(uint8_t c);
typedef void (*BCITXfinalFn)(void);

typedef struct
{   uint32_t pc;                // program counter
    uint16_t ir;                // instruction register
    uint32_t r, n, t, a, b, x, y;
    uint16_t lex;
    int16_t ior;
    uint8_t sp, rp, status, cy;
    uint32_t DataStack[STACKSIZE];
    uint32_t ReturnStack[STACKSIZE];
    uint32_t DataMem[DATASIZE];
    uint32_t CodeMem[CODESIZE];
    BCITXinitFn InitFn;         // output initialization function
    BCITXputcFn putcFn;         // output putc function
    BCITXfinalFn FinalFn;       // output finalization function
    char* name;                 // node name (for debugging)
} vm_ctx;

/** Step the VM
 * @param ctx VM identifier
 * @return 0 if okay, otherwise VM_ERROR_?
 */
int BCIstepVM(vm_ctx *ctx, uint16_t inst);

void BCIhandler(vm_ctx *ctx, const uint8_t *src, uint16_t length);
void BCIinitial(vm_ctx *ctx);

#endif /* __BCI_H__ */
