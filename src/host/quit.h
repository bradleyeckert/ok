//===============================================================================
// quit.h
//===============================================================================

#ifndef __QUIT_H__
#define __QUIT_H__
#ifdef __cplusplus
extern "C" {
#endif

#include "../bci/bci.h"

#define CPUCORES         1
#define PAD_SIZE       128      /* cells of pad to place at the top of data space */

#define LineBufferSize 128      /* Size of line buffer                      */
#define MaxLineLength   80      /* Max TIB size                             */
#define MaxKeywords   5000      /* Number of headers                        */
#define MaxNameSize     32      /* Number of chars in a name (less 1)       */
#define MaxFiles        16      /* Max open files                           */
#define MaxFilePaths   250      /* Max unique files                         */
#define MaxWordlists  (20+CPUCORES)  /* Max number of wordlists             */
#define MaxOrder        10      /* Max length of search order list          */
#define DictionaryBytes (0x100000)

#define WRONG_NUM_OF_RESULTS    -88
#define WRONG_TEST_RESULTS      -89
#define INVALID_DIRECTORY      -190
#define KEY_LOOKUP_FAILURE     -191
#define MOLE_PAIRING_FAILURE   -192

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
    uint32_t where;                     // link into dictionary where tick record is
    uint16_t link;                      // enough for 64k headers
    uint32_t *aux;                      // pointer to aux C data
    uint16_t core;                      // which core compiled for
    uint8_t notail;                     // inhibit tail recursion
    uint8_t smudge;                     // hide current definition
    uint8_t isALU;                      // is an ALU word
    uint8_t srcFile;                    // source file ID
    uint16_t srcLine;                   // source line number
};

/*
Note: ListStruct.ctx should almost never be used because the VM should be
accessed through the BCI rather than directly. Do a "search all files" to see
where it used. There are a few places other than vm.c, bci.c and bciHW.c:

In quit.c, the BCIhandler function needs a vm_ctx.
In main.c, SimulateCPU uses vm_ctx to initialize the VMs.
Access to the const boilerplate is allowed - it never changes.
*/

struct ListStruct {                     // Each VM has one of these...
    vm_ctx   ctx;                       // simulator + BCI instance
    uint32_t forth;                     // not sure if this is used, see quit.c
    uint32_t asmb;
};

typedef void (*BCIhand)(vm_ctx *ctx, const uint8_t *src, uint16_t length);
typedef void (*BCIinit)(vm_ctx *ctx);

struct QuitStruct {                     // The app has one of these...
    VMinst_t code[CPUCORES][CODESIZE];  // host-side images for compiling
    VMcell_t text[CPUCORES][TEXTSIZE];
    uint8_t  boilerplate[CPUCORES][BOILERPLATE_SIZE];
    char     token[LineBufferSize];     // blank delimited token zstring
    char     *tib;                      // location of text input buffer
    uint16_t maxtib;                    // buffer size available for tib's zstring
    uint16_t core;                      // current VM core
    uint16_t verbose;                   // size of the hole in the breadcrumb bag
    int16_t  error;                     // detected error
    uint16_t toin;                      // offset into tib
    uint16_t dp[CPUCORES];              // data space pointer
    uint16_t cp[CPUCORES];              // code space pointer
    uint16_t tp[CPUCORES];              // text space pointer
    uint32_t hp;                        // header pointer
    uint8_t  reloaded[CPUCORES];        // 0 when target is out of sync
    uint8_t  fileID;                    // file history stack
    uint8_t  wordlists;                 // wordlists stack
    uint8_t  base;                      // numeric conversion radix
    uint8_t  state;                     // 0 = interpret, 1 = compile
    int8_t   dpl;                       // decimal place, -1 if not a double number
    int8_t   sp;                        // data stack pointer
    int8_t   orders;                    // length of search order
    uint8_t  filedepth;
    uint32_t  wordlist[MaxWordlists];   // wordlist indices into header array
    char *wordlistname[MaxWordlists];
    uint32_t ds[256];                   // data stack
    uint64_t cycles;                    // VM cycle count for FN_EXECUTE
    uint64_t elapsed_us;                // command line took this long
    uint64_t startup_us;                // startup time
    uint64_t globalSize;                // global capacity in bytes
    uint32_t *global;                   // global non-volatile data
    uint32_t me;                        // found header
    char*  WidName;                     // found WID name
    struct HeaderStruct Header[MaxKeywords];
    struct FileRec      FileStack[MaxFiles];
    struct FilePath     FilePaths[MaxFilePaths];
    struct ListStruct   VMlist[CPUCORES];
    int    context[MaxOrder];           // first is CONTEXT, rest is search order
    uint32_t current;
    uint32_t host;                      // common to all VMs
    uint32_t baudrate;
    uint16_t TxMsgLength;
    uint8_t TxMsg[MaxBCIresponseSize];
    uint8_t TxMsgSend;
    uint8_t port;
    uint8_t portisopen;
    uint8_t connected;
};

// export to main.c
int QuitLoop(char *line, int maxlength, struct QuitStruct *state);
void YieldThread(void);

// export to forth.c, etc.
void DataPush(uint32_t x);
uint32_t DataPop(void);
uint32_t FetchDictionaryN(int addr, int width);
void DataDot(uint32_t x);
void PrintDataStack(void);
int parseword(char delimiter);
void Tick (void);
int  AddHead (char* name, char* help);
void SetFns (uint32_t value, void (*exec)(), void (*comp)());
void AddKeyword (char* name, char* help, void (*xte)(), void (*xtc)());
void noCompile(void);
void noExecute(void);
char * Const(char *name);
char * TIBtoEnd(void);
char * GetToken(void);
void Color(const char *color);
void message(const char* color, const char *s);

#define NOTANEQU -3412
#define MAGIC_LATER 1000
#define MAGIC_OPCODE 1001

// THROW Codes

#define BAD_STACKOVER    -3 // Stack overflow
#define BAD_STACKUNDER   -4 // Stack underflow
#define BAD_RSTACKOVER   -5 // Return stack overflow
#define BAD_RSTACKUNDER  -6 // Return stack underflow
#define DICTIONARY_OVERFLOW  -8 // Dictionary underflow
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
#define VERBOSE_COLOR    1  // show the source file line
#define VERBOSE_SOURCE   2  // show the source file line
#define VERBOSE_TOKEN    4  // show the source token (blank-delimited string)
#define VERBOSE_SRC      8  // display the remaining source in the TIB
#define VERBOSE_BCI     16  // trace BCI input and output
#define VERBOSE_CYCLES  32  // display cycle count of last executed word

#define COLOR_NONE      "\033[0m"
#define COLOR_RED       "\033[0;91m"
#define COLOR_YELLOW    "\033[0;93m"
#define COLOR_GREEN     "\033[0;92m"
#define COLOR_CYAN      "\033[0;96m"
#define COLOR_BLUE      "\033[0;94m"
#define COLOR_MAGENTA   "\033[0;95m"

#ifdef __cplusplus
}
#endif

#endif // __QUIT_H__
