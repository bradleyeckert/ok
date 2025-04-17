#ifndef __BCI_H__
#define __BCI_H__

#include <stdint.h>

#define VM_CELLBITS               32    /* simulator bits per cell */
#define VM_INSTBITS               16    /* bits per instruction */
#define BCI_DEBUG_ACCESS           3    /* debug access, should be 0 for production */
#define BCI_CYCLE_LIMIT     10000000    /* number of cycles to wait before resetting the VM */
#define DATASIZE                1024    /* cells in data space */
#define CODESIZE                2048    /* cells in code space */
#define STACKSIZE                 16    /* depth of stacks */
#define NVMSIZE                65536    /* size of private NVM storage in bytes */

#define BCI_IOR_INVALID_ADDRESS   -9
#define BCI_BAD_COMMAND          -84

#define BCI_BEGIN                252    /* beginning-of-message */

#define VM_SIGN     (1 << (VM_CELLBITS - 1))
#if (VM_CELLBITS == 32)
#define VM_MASK           0xFFFFFFFF
#else
#define VM_MASK     ((1 << VM_CELLBITS) - 1)
#endif

#if (VM_CELLBITS > 16)
#define VMcell_t            uint32_t
#else
#define VMcell_t            uint16_t
#endif

#if (VM_INSTBITS > 16)
#define VMinst_t            uint32_t
#else
#define VMinst_t            uint16_t
#endif

#define SLOT_OP_NAMES \
    "nop",  "inv",  "dup",  "a!",  "+", "xor", "and", "drop", \
    "swap",  "2*", "over", "cy!", "@a", "@a+",  "@b",  "@b+", \
    "2/c",   "2/",     "",  "u!", "!a", "!a+",  "!b",  "!b+", \
    "unext",   "",    "u",  ">r", "cy",   "a",   "r",   "r>"

// static const char *sopNames[] = {SLOT_OP_NAMES};

#define VMO_NOP                 0x00
#define VMO_INV                 0x01
#define VMO_OVER                0x02
#define VMO_ASTORE              0x03
#define VMO_PLUS                0x04
#define VMO_XOR                 0x05
#define VMO_AND                 0x06
#define VMO_DROP                0x07
#define VMO_SWAP                0x08
#define VMO_TWOSTAR             0x09
#define VMO_DUP                 0x0A
#define VMO_CYSTORE             0x0B
#define VMO_FETCHA              0x0C
#define VMO_FETCHAPLUS          0x0D
#define VMO_FETCHB              0x0E
#define VMO_FETCHBPLUS          0x0F
#define VMO_TWODIVC             0x10
#define VMO_TWODIV              0x11
#define VMO_USTORE              0x13
#define VMO_STOREA              0x14
#define VMO_STOREAPLUS          0x15
#define VMO_STOREB              0x16
#define VMO_STOREBPLUS          0x17
#define VMO_UNEXT               0x18
#define VMO_U                   0x1A
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

#define BCI_EMPTY_STACK   0xAAAAAAAA
#define BCI_STATUS_RUNNING         0
#define BCI_STATUS_STOPPED         1
#define BCI_STATUS_SHUTDOWN        2
#define BCI_ACCESS_PERIPHERALS     1
#define BCI_ACCESS_CODESPACE       2

typedef void (*BCITXinitFn)(int id);
typedef void (*BCITXputcFn)(int id, uint8_t c);
typedef void (*BCITXfinalFn)(int id);

typedef struct
{   uint32_t pc;                // program counter
    VMinst_t ir;                // instruction register
    VMcell_t r, n, t, a, b, x, y;
    uint16_t lex;
    int16_t  ior;
    uint8_t  sp, rp, status, cy;
    VMcell_t DataStack[STACKSIZE];
    VMcell_t ReturnStack[STACKSIZE];
    VMcell_t DataMem[DATASIZE];
    VMinst_t CodeMem[CODESIZE];
    BCITXinitFn InitFn;         // output initialization function
    BCITXputcFn putcFn;         // output putc function
    BCITXfinalFn FinalFn;       // output finalization function
    int16_t id;                 // node id
} vm_ctx;

/** Step the VM
 * @param ctx VM identifier
 * @param inst VM instruction
 * @return 0 if okay, otherwise VM_ERROR_?
 */
int BCIstepVM(vm_ctx *ctx, VMinst_t inst);

/** Thin-client communication interface
 * @param ctx VM identifier, NULL if not simulated (no context available)
 * @param id VM identifier, used when ctx=NULL
 * @param src Command string address
 * @param length Command string length
 */
void BCIhandler(vm_ctx *ctx, const uint8_t *src, uint16_t length);

/** Reset the VM
 * @param ctx VM identifier, NULL if not simulated (no context available)
 * @param id VM identifier, used when ctx=NULL
 */
void BCIinitial(vm_ctx *ctx);

#endif /* __BCI_H__ */
