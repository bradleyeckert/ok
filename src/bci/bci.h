#ifndef __BCI_H__
#define __BCI_H__

#include <stdint.h>

#define MaxBCIresponseSize      1030
#define MOLE_PROTOCOL              0
#define DEFAULT_BAUDRATE      921600
#define DEFAULT_HOSTPORT           2
#define DEFAULT_TARGETPORT         3

#define VM_CELLBITS               32    /* simulator bits per cell: 16 to 32 */
#define VM_INSTBITS               16    /* bits per instruction: 16, 17, 20, 21, 22, 25, 26, or 27 bits */
#define DATA_STACKSIZE           128    /* depth of stacks */
#define RETURN_STACKSIZE         128
#define CODESIZE              0x2000    /* cells in code space: 16KB */
#define DATASIZE              0x0800    /* cells in data space: 8KB */
#define TEXTORIGIN            0x1000    /* base address of internal Flash data in cells */
#define TEXTSIZE              0x4000    /* size of internal Flash data in cells */
#define BLANK_FLASH_BYTE        0xFF    /* byte values after flash erase */
#define FLASH_BLOCK_SIZE         256    /* bytes in a flash programming block */
#define BCI_DEBUG_ACCESS           3    /* debug access, should be 0 for production */
#define BCI_CYCLE_LIMIT     10000000    /* number of cycles to wait before resetting the VM */
#define BOILERPLATE_SIZE          16

#define VM_STACK_UNDERFLOW        -4
#define BCI_IOR_INVALID_ADDRESS   -9
#define BCI_BAD_COMMAND          -84
#define VM_SHUTDOWN_PIN        10000
#define VM_RESET_PIN           10011
#define VM_SLEEP_PIN           10022
#define BCI_BEGIN                252    /* beginning-of-message */

#if ((DATA_STACKSIZE-1) & DATA_STACKSIZE)
#error DATA_STACKSIZE must be an exact power of 2
#endif
#if ((RETURN_STACKSIZE-1) & DATA_STACKSIZE)
#error RETURN_STACKSIZE must be an exact power of 2
#endif

#define VM_SIGN     (1 << (VM_CELLBITS - 1)) // MSB
#if (VM_CELLBITS == 32)
#define VM_MASK           0xFFFFFFFF
#else
#define VM_MASK     ((1 << VM_CELLBITS) - 1)
#endif
#define VM_EMPTY_STACK (0x55555555 & VM_MASK)
#define VM_IMM_MASK  ((1 << (VM_INSTBITS - 3)) - 1)
#define VM_IMMS_MASK ((1 << (VM_INSTBITS - 7)) - 1)

#if (VM_CELLBITS > 16)
#define VMcell_t            uint32_t
#define VMdblcell_t         uint64_t
#define C_CELLBITS          32
#else
#define VMcell_t            uint16_t
#define VMdblcell_t         uint32_t
#define C_CELLBITS          16
#endif

#if (VM_INSTBITS > 16)
#define VMinst_t            uint32_t
#else
#define VMinst_t            uint16_t
#endif

#define VM_UOPS       (1 << (VM_INSTBITS - 1)) // uops bit location
#define VM_RET        (1 << (VM_INSTBITS - 2)) // return bit location
#define SLOT0_POSITION   (VM_INSTBITS - 7)
#define LAST_SLOT_WIDTH ((VM_INSTBITS - 2) % 5)
#define LAST_SLOT_MASK  ((1 << LAST_SLOT_WIDTH) - 1)

#define UOP_NAMES { \
    "nop",   "inv",   "over",  "a!",    "+",     "xor",   "and",   ">r", \
    "unext", "2*",    "dup",   "drop",  "@a",    "@a+",   "r@",    "r>", \
    "2/c",   "2/",    "0",     "b!",    "!a",    "!a+",   "!b",    "!b+", \
    "swap",  "?",     "u",     "u!",    "@b",    "@b+",   "a",     "cy"}

#define VM_STACKEFFECTS { /* 0=none, 1=dup, 2=drop */ \
    0x00,    0x00,    0x01,    0x02,    0x02,    0x02,    0x02,    0x02, \
    0x00,    0x00,    0x01,    0x02,    0x01,    0x01,    0x01,    0x01, \
    0x00,    0x00,    0x01,    0x02,    0x02,    0x02,    0x02,    0x02, \
    0x00,    0x00,    0x01,    0x02,    0x01,    0x01,    0x01,    0x01}

#define VMO_NOP                 0x00
#define VMO_INV                 0x01
#define VMO_OVER                0x02
#define VMO_ASTORE              0x03
#define VMO_PLUS                0x04
#define VMO_XOR                 0x05
#define VMO_AND                 0x06
#define VMO_PUSH                0x07
#define VMO_UNEXT               0x08
#define VMO_TWOSTAR             0x09
#define VMO_DUP                 0x0A
#define VMO_DROP                0x0B
#define VMO_FETCHA              0x0C
#define VMO_FETCHAPLUS          0x0D
#define VMO_R                   0x0E
#define VMO_POP                 0x0F
#define VMO_TWODIVC             0x10
#define VMO_TWODIV              0x11
#define VMO_ZERO                0x12
#define VMO_BSTORE              0x13
#define VMO_STOREA              0x14
#define VMO_STOREAPLUS          0x15
#define VMO_STOREB              0x16
#define VMO_STOREBPLUS          0x17
#define VMO_SWAP                0x18
//#define VMO_ZEROLESS          0x19 // unused
#define VMO_U                   0x1A
#define VMO_USTORE              0x1B
#define VMO_FETCHB              0x1C
#define VMO_FETCHBPLUS          0x1D
#define VMO_A                   0x1E
#define VMO_CY                  0x1F

#define OP_NAMES  {"call", "jump", "lit"}

#define VM_IMMBITS              (VM_INSTBITS - 3)
#define VMO_CALL                0
#define VMO_JUMP                1
#define VMO_LIT                 2
#define VMO_PFX                 3
#define VMI_CALL                (VMO_CALL << VM_IMMBITS)
#define VMI_JUMP                (VMO_JUMP << VM_IMMBITS)
#define VMI_LIT                 (VMO_LIT << VM_IMMBITS)
#define VMI_PFX                 (VMO_PFX << VM_IMMBITS)
#define VMI_ZOODUP              (1 << (VM_INSTBITS - 8))
#define VMI_ZOODROP             (1 << (VM_INSTBITS - 9))
#define VMI_ZOO                 (VMI_PFX + (1 << (VM_INSTBITS - 7)))

#define ZOO_NAMES  {"x!", "y!", "sp!", "rp!", "sp@", "rp@", "bcisync", "err!"}

#define VMO_XSTORE              0
#define VMO_YSTORE              1
#define VMO_SPSTORE             2
#define VMO_RPSTORE             3
#define VMO_SPFETCH             4
#define VMO_RPFETCH             5
#define VMO_BCISYNC             6
#define VMO_THROW               7

#define VMI_XSTORE              (VMI_ZOO + VMO_XSTORE  + VMI_ZOODROP)
#define VMI_YSTORE              (VMI_ZOO + VMO_YSTORE  + VMI_ZOODROP)
#define VMI_SPSTORE             (VMI_ZOO + VMO_SPSTORE)
#define VMI_RPSTORE             (VMI_ZOO + VMO_RPSTORE + VMI_ZOODROP)
#define VMI_SPFETCH             (VMI_ZOO + VMO_SPFETCH + VMI_ZOODUP)
#define VMI_RPFETCH             (VMI_ZOO + VMO_RPFETCH + VMI_ZOODUP)
#define VMI_BCISYNC             (VMI_ZOO + VMO_BCISYNC)
#define VMI_THROW               (VMI_ZOO + VMO_THROW   + VMI_ZOODROP)

#define IMM_NAMES { \
    "pfx", "zoo", "ax", "by", "if", "bran", "-if", "next", \
    "?", "?", "?", "?", "APIcall", "APIcall+", "APIcall-", "APIcall–-"}

#define VMO_LEX                 0
#define VMO_ZOO                 1
#define VMO_AX                  2
#define VMO_BY                  3
#define VMO_ZBRAN               4
#define VMO_BRAN                5
#define VMO_PBRAN               6
#define VMO_NEXT                7
#define VMO_API                 12
#define VMO_DUPAPI              13
#define VMO_APIDROP             14
#define VMO_API2DROP            15

#define VMI_AX                  (VMI_PFX + (VMO_AX       << (VM_INSTBITS - 7)))
#define VMI_BY                  (VMI_PFX + (VMO_BY       << (VM_INSTBITS - 7)))
#define VMI_ZBRAN               (VMI_PFX + (VMO_ZBRAN    << (VM_INSTBITS - 7)))
#define VMI_BRAN                (VMI_PFX + (VMO_BRAN     << (VM_INSTBITS - 7)))
#define VMI_PBRAN               (VMI_PFX + (VMO_PBRAN    << (VM_INSTBITS - 7)))
#define VMI_NEXT                (VMI_PFX + (VMO_NEXT     << (VM_INSTBITS - 7)))
#define VMI_API                 (VMI_PFX + (VMO_API      << (VM_INSTBITS - 7)))
#define VMI_DUPAPI              (VMI_PFX + (VMO_DUPAPI   << (VM_INSTBITS - 7)))
#define VMI_APIDROP             (VMI_PFX + (VMO_APIDROP  << (VM_INSTBITS - 7)))
#define VMI_API2DROP            (VMI_PFX + (VMO_API2DROP << (VM_INSTBITS - 7)))

#define BCIFN_READ          1
#define BCIFN_WRITE         2
#define BCIFN_EXECUTE       3
#define BCIFN_CRC           4
#define BCIFN_WRCODE        5
#define BCIFN_WRTEXT        6
#define BCIFN_SECTOR_ERASE  7
#define BCIFN_STROBE        8
#define BCIFN_READREG      11
#define BCIFN_WRITEREG     12

#define BCI_STATUS_RUNNING         0
#define BCI_STATUS_STOPPED         1
#define BCI_STATUS_SHUTDOWN        2
#define BCI_ACCESS_PERIPHERALS     1
#define BCI_ACCESS_CODESPACE       2

typedef void (*BCITXinitFn)(int id);
typedef void (*BCITXputcFn)(int id, uint8_t c);
typedef void (*BCITXfinalFn)(int id);

typedef struct
{   VMcell_t pc;                // program counter
    VMinst_t ir;                // instruction register
    VMcell_t r, n, t, a, b, x, y;
    uint16_t lex;
    int16_t  ior;
    uint8_t  sp, rp, cy, status, statusNew;
    uint64_t cycles;
    VMcell_t DataStack[DATA_STACKSIZE];
    VMcell_t ReturnStack[RETURN_STACKSIZE];
    VMcell_t DataMem[DATASIZE]; // RAM can be anywhere
    VMcell_t *TextMem;          // Flash must have a specific address
    VMinst_t *CodeMem;
    int16_t id;                 // node id
    const uint8_t *boilerplate; // boilerplate info
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

// Needs these externals

void StopVMthread(vm_ctx *ctx);
void BCIsendInit(int id);
void BCIsendChar(int id, uint8_t c);
void BCIsendFinal(int id);

#endif /* __BCI_H__ */
