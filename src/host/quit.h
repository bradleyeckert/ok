//===============================================================================
// quit.h
//===============================================================================

#ifndef __QUIT_H__
#define __QUIT_H__

#include "../bci/bci.h"

#define CPUCORES         2

#define LineBufferSize 128      /* Size of line buffer                      */
#define MaxLineLength   80      /* Max TIB size                             */
#define MaxKeywords   2000      /* Number of headers                        */
#define MaxNameSize     32      /* Number of chars in a name (less 1)       */
#define MaxAnchorSize   40      /* Number of chars in an anchor string (-1) */
#define MaxFiles        16      /* Max open files                           */
#define MaxFilePaths    32      /* Max unique files                         */
#define MaxWordlists    20      /* Max number of wordlists                  */
#define MaxOrder         8      /* Max length of search order list          */
#define MaxReceiveBytes 256     /* Size of BCI response buffer              */

#define ALL_ONES  ((unsigned)(~0))
#define CELLBITS VM_CELLBITS
#if (CELLBITS == 32)
#define cell     uint32_t
#define CELLSIZE 5  /* log2(CELLBITS) */
#define CELL_ADDR(x) ((x) >> 2)
#define BYTE_ADDR(x) ((x) << 2)
#elif (CELLBITS > 16)
#define cell     uint32_t
#define CELLSIZE 5  /* # of bits needed to address bits in a cell */
#define CELL_ADDR(x) ((x) >> 1)
#define BYTE_ADDR(x) ((x) << 1)
#else
#define cell     uint16_t
#define CELLSIZE 4
#define CELL_ADDR(x) (x >> 1)
#define BYTE_ADDR(x) (x << 1)
#endif
#define CELLS    (BYTE_ADDR(1))

#if (CELLBITS < 32)
#define CELLMASK (~(ALL_ONES<<CELLBITS))
#define MSB      (1 << (CELLBITS-1))
#else
#define CELLMASK 0xFFFFFFFF
#define MSB      0x80000000
#endif

#define RPMASK   (StackSize-1)
#define RDEPTH   (rp & RPMASK)
#define SPMASK   (StackSize-1)
#define SDEPTH   (sp & SPMASK)
#define CELL_AMASK ((1 << CELLSIZE) - 1) /* 15 or 31 */
#define SV static void
#define SI static int
#define CELL static cell
#define HI_BYTE(x) ((x>>8) & 255)
#define LO_BYTE(x) (x & 255)

struct FileRec {
    char Line[LineBufferSize];          // the current input line
    FILE* fp;                           // input file pointer
    uint32_t LineNumber;                // line number
    int FID;                            // file ID for LOCATE
};

struct FilePath {
    char filepath[LineBufferSize];      // filename
};

typedef void (*VoidFn)();

struct HeaderStruct {                   // Each dictionary has many of these...
    char *name;                         // word name, local copy is needed
    char *help;                         // help anchor name
    VoidFn ExecFn;                      // C functions for compile/execute
    VoidFn CompFn;
    uint32_t color;                     // HTML color
    uint32_t length;                    // size of definition in code space
    uint32_t w;                         // optional data
    uint32_t w2;
    uint32_t target;                    // target address if used
    uint16_t references;                // how many times it has been referenced
    uint16_t link;                      // enough for 64k headers
    uint32_t *aux;                      // pointer to aux C data
    uint8_t notail;                     // inhibit tail recursion
    uint8_t smudge;                     // hide current definition
    uint8_t isALU;                      // is an ALU word
    uint8_t srcFile;                    // source file ID
    uint16_t srcLine;                   // source line number
};

struct ListStruct {                     // Each VM has one of these...
    vm_ctx   ctx;                       // simulator + BCI instance
    uint32_t forth;
    uint32_t asmb;
};

typedef void (*BCIhand)(vm_ctx *ctx, const uint8_t *src, uint16_t length);
typedef void (*BCIinit)(vm_ctx *ctx);

struct QuitStruct {                     // The app has one of these...
    uint16_t code[CODESIZE];            // local code image
    uint16_t nvm[NVMSIZE*CPUCORES];     // local NVM image
    char     *tib;                      // location of text input buffer
    uint16_t maxtib;                    // buffer size available for tib's zstring
    uint16_t core;                      // current VM core
    char token[LineBufferSize];         // blank delimited token zstring
    uint16_t verbose;                   // size of the hole in the breadcrumb bag
    int16_t  error;                     // detected error
    uint16_t toin;                      // offset into tib
    uint16_t dp;                        // data space pointer
    uint16_t cp;                        // code space pointer
    uint32_t np;                        // NVM space pointer
    uint8_t  base;                      // numeric conversion radix
    uint8_t  state;                     // 0 = interpret, 1 = compile
    int8_t   dpl;                       // decimal place, -1 if not a double number
    int8_t   sp;                        // data stack pointer
    uint8_t  fileID;
    uint8_t  filedepth;
    int8_t   orders;                    // length of search order
    uint8_t  wordlists;
    uint32_t  wordlist[MaxWordlists];   // wordlist indices into header array
    char *wordlistname[MaxWordlists];
    uint32_t ds[256];                   // data stack
    uint64_t elapsed_us;                // command line took this long
    uint64_t startup_us;                // startup time
    uint64_t globalSize;                // global capacity in bytes
    uint32_t *global;                   // global non-volatile data
    uint32_t hp;                        // header pointer
    uint32_t me;                        // found header
    char*  WidName;                     // found WID name
    struct HeaderStruct Header[MaxKeywords];
    struct FileRec      FileStack[MaxFiles];
    struct FilePath     FilePaths[MaxFilePaths];
    struct ListStruct   VMlist[CPUCORES];
    int    context[MaxOrder];           // first is CONTEXT, rest is search order
    uint32_t current;
    uint32_t root;                      // common to all VMs
    BCIhand BCIfn;                      // BCI command transmit function
    BCIinit BCIinitFn;                  // BCI reset function
};

// export to main.c
int quitloop(char *line, int maxlength, struct QuitStruct *state);

// export to forth.c
void BCIreceive(int id, const uint8_t *src, uint16_t length);

// export to forth.c
void DataPush(cell x);
cell DataPop(void);
void DataDot(cell x);
void PrintDataStack(void);
int  AddHead (char* name, char* help);
void SetFns (cell value, void (*exec)(), void (*comp)());
void AddKeyword (char* name, char* help, void (*xte)(), void (*xtc)());
void noCompile(void);
void noExecute(void);
void cdump(const uint8_t *src, uint16_t len);

#define NOTANEQU -3412
#define MAGIC_LATER 1000
#define MAGIC_OPCODE 1001


// THROW Codes

#define BAD_STACKOVER    -3 // Stack overflow
#define BAD_STACKUNDER   -4 // Stack underflow
#define BAD_RSTACKOVER   -5 // Return stack overflow
#define BAD_RSTACKUNDER  -6 // Return stack underflow
#define DIV_BY_ZERO     -10 // Division by 0
#define UNRECOGNIZED    -13 // Unrecognized word
#define BAD_NOEXECUTE   -14 // Interpreting a compile-only word
#define BAD_ROMWRITE    -20 // Write to a read-only location
#define BAD_UNSUPPORTED -21 // Unsupported operation
#define BAD_CONTROL     -22 // Control structure mismatch
#define BAD_ALIGNMENT   -23 // Address alignment exception
#define BAD_BODY        -31 // >BODY used on non-CREATEd definition
#define BAD_ORDER_OVER  -49 // Search-order overflow
#define BAD_ORDER_UNDER -50 // Search-order underflow
#define BAD_EOF         -58 // unexpected EOF in [IF]
#define BAD_INPUT_LINE  -62 // Input buffer overflow, line too long
#define BAD_DATA_WRITE  -64 // Write to non-existent data memory
#define BAD_DATA_READ   -65 // Read from non-existent data memory
#define BAD_PC          -66 // PC is in non-existent code memory
#define BAD_CODE_WRITE  -67 // Write to non-existent code memory
#define BAD_ASSERT      -68 // Test failure
#define BAD_ALU_OP      -72 // Invalid ALU code
#define BAD_BITFIELD    -73 // Bitfield is too wide for a cell
#define BAD_IS          -74 // Trying to IS a non-DEFER
#define BAD_WID_OVER    -75 // Too many WORDLISTs
#define BAD_WID         -76 // WID is invalid
#define BAD_DOES        -77 // Invalid CREATE DOES> usage
#define BAD_INCLUDING   -78 // Nesting overflow during include
#define BAD_NOCOMPILE   -79 // Compiling an execute-only word
#define BAD_FSOVERFLOW  -82 // Flash string overflow
#define BAD_COPROCESSOR -84 // Invalid coprocessor field
#define BAD_POSTPONE    -85 // Unsupported postpone
#define BAD_BCIMESSAGE  -86 // Bad message received from BCI
#define BAD_CREATEFILE -198
#define BAD_OPENFILE   -199 // Can't open file
#define BYE            -299

// verbose flags
#define VERBOSE_SOURCE  1   // show the source file line
#define VERBOSE_TOKEN   2   // show the source token (blank-delimited string)
#define VERBOSE_TRACE   4   // simulation trace in human readable form
#define VERBOSE_STKMAX  8   // track and show the maximum stack depth
#define VERBOSE_SRC     16  // display the remaining source in the TIB
#define VERBOSE_DASM    32  // disassemble in long format
#define VERBOSE_BCI     64  // trace BCI input and output

#endif // __QUIT_H__
