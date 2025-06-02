#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "quit.h"
#include "forth.h"
#include "see.h"
#include "tools.h"
#include "comm.h"
#include "../bci/bci.h"

struct QuitStruct *q;

#define CORE      q->core
#define DP        q->dp[CORE]           // data space pointer
#define CP        q->cp[CORE]           // code space pointer
#define NP        q->np                 // NVM space pointer
#define SP        q->sp                 // data stack pointer
#define ERR       q->error              // detected error
#define HEADER    q->Header
#define ME        q->me
#define VERBOSE   q->verbose
#define TOKEN     q->token

//------------------------------------------------------------------------------
// help

#ifdef _WIN32
static char *prefix_forth = "start https://forth-standard.org/standard/";
static char *prefix_local = "start file:///C:/Users/Brad/Documents/GitHub/ok/html/";
#else
#ifdef __linux__
static char *prefix_forth = "open https://forth-standard.org/standard/";
static char *prefix_local = "open file:///home/bradley/libs/ok/html/";
#else
static char *prefix_forth = "open https://forth-standard.org/standard/";
static char *prefix_local = "open file:///Users/User/libs/ok/html/";
#endif
#endif

static void PrefixSetForth(void) { prefix_forth = TIBtoEnd(); }
static void PrefixSetLocal(void) { prefix_local = TIBtoEnd(); }

static void Prefixes(void) {
    printf("help_forth: %s\n", prefix_forth);
    printf("help_local: %s\n", prefix_local);

}

static void help(void) {
    Tick();
    DataPop();
    if (ERR) return;
    char *str = HEADER[ME].help;
    char *pic = strchr(str, ' ');
    char *term;
    char command[LineBufferSize];
    memset(command, 0, sizeof(LineBufferSize));
    switch(*str++) {
        case 0: return;
        case '~': strmove(command, prefix_forth, LineBufferSize);  goto cat;
        case '-': strmove(command, prefix_local, LineBufferSize);
cat:        term = &command[strlen(command) + (pic - str)];
            StrCat(command, str, LineBufferSize);
            *term = 0; // nip the stack picture
            int ior = system(command);
            if (ior) printf("No external help\n");
            break;
        default: break; // no prefix means no help page
    }
    printf("( %s ) ", ++pic);
}

//------------------------------------------------------------------------------
// see

#define LABEL_COLUMN 18

static char* TargetName (uint32_t addr) {
    if (!addr) return NULL;
    int i = q->hp + 1;
    while (--i) {
        if ((HEADER[i].target == addr)
         && (HEADER[i].core == CORE))
            return HEADER[i].name;
    }
    return NULL;
}

static char DAbuf[256];                 // disassembling to a buffer

static void appendDA(const char* s) {   // append string to DA buffer
    int i = (int)strlen(DAbuf);
    int len = (int)strlen(s);
    strmove(&DAbuf[i], (char*)s, len + 1);
    i += len;
    DAbuf[i++] = ' ';                   // trailing space
    DAbuf[i++] = '\0';
}

static void DAtabTo(int column) {
    int i = (int)strlen(DAbuf);
    while (i < column) DAbuf[i++] = ' ';
    DAbuf[i++] = '\0';
}

static void ToLabel(void) {
    DAtabTo(LABEL_COLUMN);
    if (VERBOSE & VERBOSE_COLOR) appendDA(COLOR_NONE);
    else appendDA("\\");
}

static void HexToDA(uint32_t x) {       // append hex number to DA buffer
    appendDA(itos(x, 16, 1, 1, 32));
}

static const char *uopName[] = UOP_NAMES;
static const char *opName[] =  OP_NAMES;
static const char *immName[] = IMM_NAMES;
static const char *zooName[] = ZOO_NAMES;
static const char *aipName[] = API_NAMES;

static char * DisassembleInsn(uint32_t inst) {
    static uint32_t lex;
    uint32_t _lex = 0;
    memset(DAbuf, 0, sizeof(DAbuf));
    appendDA(itos(inst, 16, (VM_INSTBITS + 3) / 4, 0, VM_INSTBITS));
    appendDA("");
    if (inst & VM_UOPS) {
        int returning = inst & VM_RET;
        inst &= (VM_RET - 1);
        for (int i = SLOT0_POSITION; i > -5; i -= 5) {
            uint8_t slot;
            if (i < 0) slot = inst & LAST_SLOT_MASK;
            else slot = (inst >> i) & 0x1F;
            if (inst & ((1 << (i + 5)) - 1)) {
                appendDA(uopName[slot]);
            }
        }
        if (returning) appendDA(";");
    } else {
        int opcode =   (inst >> (VM_INSTBITS - 3)) & 3;
        int32_t immex = (lex << (VM_INSTBITS - 3))
               | (inst & ((1 << (VM_INSTBITS - 3)) - 1));
        if (opcode < 3) { // call, jump, imm
            HexToDA(immex);
            appendDA(opName[opcode]);
            char* name;
            switch (opcode) {
                case VMO_CALL:
                case VMO_JUMP:
                    name = TargetName(immex);
                    if (name) {
                        ToLabel();
                        appendDA(name);
                    } break;
                case VMO_LIT:
                    ToLabel();
                    appendDA(itos(immex, 10, 1, 1, 32));
                default: break;
            }
        } else {
            uint32_t imm = inst & ((1 << (VM_INSTBITS - 7)) - 1);
            immex = imm;
            if (inst & (1 << (VM_INSTBITS - 8))) { // sign-extend
                immex |= ~((1 << (VM_INSTBITS - 7)) - 1);
            }
            opcode = (inst >> (VM_INSTBITS - 7)) & 0x0F;
            if (opcode == 1) {
                imm &= 0x7F; // strip stack actions
                if (imm < sizeof(zooName)/sizeof(zooName[0])) {
                    appendDA(zooName[imm]);
                } else {
                    appendDA("zoo? ");
                }
            } else {
                if (opcode == VMO_LEX) _lex = imm;
                HexToDA(immex);
                appendDA(immName[opcode]);
                switch (opcode) {
                    case VMO_API:
                    case VMO_DUPAPI:
                    case VMO_APIDROP:
                    case VMO_API2DROP: ToLabel();
                        if (imm < sizeof(aipName) / sizeof(aipName[0])) {
                            appendDA(aipName[imm]);
                        }
                        else {
                            appendDA("api? ");
                        }
                        break;
                    case VMO_BY: ToLabel();
                        appendDA("b = y +");
                        appendDA(itos(imm, 16, 1, 1, 32));
                        break;
                    case VMO_PY: ToLabel();
                        appendDA("y =");
                        appendDA(itos((imm << 10) + 0x40000000, 16, 1, 1, 32));
                        appendDA("/ 4");
                        break;
                    default: break;
                }
            }
        }
    }
    if (_lex)
    lex = (lex << (VM_INSTBITS - 7)) | _lex;
    else lex = 0;
    return DAbuf;
}

static void Onesee(void) {printf("%s", DisassembleInsn(DataPop()));}

static void Dasm (void) { // ( addr len -- ) or ( -- )
    int length = CP;
    int addr = 0;
    if (SP > 1) {
        length = DataPop() & 0x0FFF;
        addr = DataPop();
    }
    char* name;
    for (int i=0; i<length; i++) {
        int a = addr++ & (CODESIZE-1);
        int x = q->code[CORE][a];
        name = TargetName(a);
        if (name != NULL) {
            Color(COLOR_RED);  printf("     %s\n", name);
            Color(COLOR_NONE);
        }
        printf("%04X ", a);
        message(COLOR_GREEN, DisassembleInsn(x));
        printf("\n");
    }
}

static void See (void) { // ( <name> -- )
    Tick();
    DataPush(HEADER[ME].length);
    Dasm();
}

//------------------------------------------------------------------------------
// other

static void ColorizeWord(char* str, char* key, int line) {
    size_t keylength = strlen(key);
    size_t strlength = strlen(str);
    char* leading = strstr(str, TOKEN);
    memmove (leading + 1, leading, strlength + 1);
    *leading = 0; // terminate the leading part
    printf("%4d: %s", line, str);
    message(COLOR_RED, key);
    printf("%s", leading + keylength + 1);
}

static void Locate(void) {
    Tick();
    if (ERR) return;
    DataPop();
    int length = 10;
    if (SP == 1) length = DataPop();
    int i = HEADER[ME].srcFile;
    char* filename = q->FilePaths[i].filepath;
    int line = HEADER[ME].srcLine;
    int offset = 0;
    int where = line;
    if (length < 0) {
        line += length;
        if (line < 0) line = 0;
        offset = length;
        length = 1 - offset;
    }
    FILE* fp = fopenx(filename, "r");
    if (fp != NULL) {
        printf("%s, Line# %d\n", filename, where);
        char b[LineBufferSize];
        for (int i = 1 - line; i < length; i++) {
            if (fgets(b, LineBufferSize, fp) == NULL) break;
            if (i >= 0) {
                if (i == -offset) {
                    ColorizeWord(b, TOKEN, line++);
                }
                else printf("%4d: %s", line++, b);
            }
        }
        fclose(fp);
    }
}

static void Where(void) {
    Tick();
    if (ERR) return;
    DataPop();
    int p = HEADER[ME].where;
    int limit = 10; // in case of broken links
    while (p) {
        if (!(limit--)) break;
        int fid = FetchDictionaryN(p + 3, 2);
        char* filename = q->FilePaths[fid].filepath;
        int line = FetchDictionaryN(p + 5, 3);
        p = FetchDictionaryN(p, 3);
        FILE* fp = fopenx(filename, "r");
        if (fp != NULL) {
            printf("%s, Line# %d\n", filename, line);
            char b[LineBufferSize];
            for (int i = 1 - line; i < 1; i++) {
                if (fgets(b, LineBufferSize, fp) == NULL) break;
                if (i >= 0) {
                    ColorizeWord(b, TOKEN, line++);
                }
            }
            fclose(fp);
        }
    }
}

static void words(int wid) {
    parseword(' ');                     // tok is the search key (none=ALL)
    uint16_t i = q->wordlist[wid];
    while (i) {
        size_t len = strlen(TOKEN);     // filter by substring
        char* s = strstr(HEADER[i].name, TOKEN);
        if ((s != NULL) || (len == 0)) {
            int refs = HEADER[i].references;
            if (refs) {
                printf("(%d)", refs);
                Color(COLOR_CYAN);
            }
            printf("%s ", HEADER[i].name);
            Color(COLOR_NONE);
        }
        i = HEADER[i].link;             // traverse from oldest
    }
    printf("\n");
}

static void Words(void) { words(q->context[0]); }
static void Wrds(void)  { int x = DataPop(); if (x < q->wordlists) words(x); }

static void DotBoiler(void) {
    const uint8_t *p = q->VMlist[CORE].ctx.boilerplate;
    uint8_t format = *p++;
    uint8_t cellbits = *p++;
    uint8_t instbits = *p++;
    uint8_t dsSize = *p++ + 1;
    uint8_t rsSize = *p++ + 1;
    uint32_t dSize = (*p++ + 1) << 8;
    uint32_t cSize = (*p++ + 1) << 8;
    uint32_t tSize = (*p++ + 1) << 10;
    uint32_t tOrigin = *p++ << 12;
    switch (format) {
        case 0:
            printf("Missing boilerplate "); break;
        case 1:
            printf("%d bits/cell, %d cells of data stack\n", cellbits, dsSize);
            printf("%d bits/inst, %d cells of return stack\n", instbits, rsSize);
            printf("Code space (cp) = 0 to %Xh, no access from @ and !\n", cSize - 1);
            printf("Data space (dp) = 0 to %Xh\n", dSize - 1);
            printf("Text space (tp) = %X to %Xh\n", tOrigin, tOrigin + tSize - 1);
            break;
        default:
        printf("Unknown boilerplate format ");
    }
}

static void CommGetCycles(void) {
    SendInit();
    SendChar(BCIFN_GET_CYCLES);
    SendFinal();
    BCIwait("CommGetCycles", 1);
}

static void ShowMIPS(void) {
    CommGetCycles();  uint64_t c0 = q->cycles;
    uSleep(500000);
    CommGetCycles();  uint64_t c1 = q->cycles;
    double mips = (float)(c1 - c0) / 5e5;
    printf("%.2f MIPS ", mips);
}

void AddSeeKeywords(struct QuitStruct *state) {
    q = state;
    AddKeyword("mips",   "-see.htm#mips --",                    ShowMIPS,   noCompile);
    AddKeyword(".ir",    "-see.htm#dotir inst --",              Onesee,     noCompile);
    AddKeyword("dasm",   "-see.htm#dasm [ a u ] --",            Dasm,       noCompile);
    AddKeyword("see",    "~tools/SEE <name> --",                See,        noCompile);
    AddKeyword("locate", "-see.htm#locate [lines] <name> --",   Locate,     noCompile);
    AddKeyword("where",  "-see.htm#where <name> --",            Where,      noCompile);
    AddKeyword("words",  "~tools/WORDS [substr] --",            Words,      noCompile);
    AddKeyword("Words",  "-see.htm#wrds [substr] wid --",       Wrds,       noCompile);
    AddKeyword("help",   "-see.htm#help <name> --",             help,       noCompile);
    AddKeyword("help_prefix", "-see.htm#helpprefix --",         Prefixes,   noCompile);
    AddKeyword("help_forth:", "-see.htm#helpforth \"ccc<eol>\" --", PrefixSetForth, noCompile);
    AddKeyword("help_local:", "-see.htm#helplocal \"ccc<eol>\" --", PrefixSetLocal, noCompile);
    AddKeyword(".boiler",  "-see.htm#dotboiler --",             DotBoiler,  noCompile);
}
