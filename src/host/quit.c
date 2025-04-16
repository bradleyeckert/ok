#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "quit.h"
#include "forth.h"
#include "tools.h"
#include "../bci/bci.h"

struct QuitStruct *q;

#define TIB       q->tib                // location of text input buffer
#define MAXTIB    q->maxtib             // buffer size available for tib's zstring
#define VERBOSE   q->verbose
#define ERROR     q->error              // detected error
#define TOIN      q->toin               // offset into tib
#define HP        q->hp
#define SP        q->sp                 // data stack pointer
#define BASE      q->base
#define STATE     q->state
#define DPL       q->dpl
#define FILEID    q->fileID
#define FILEDEPTH q->filedepth
#define ORDERS    q->orders
#define CURRENT   q->current
#define CONTEXT   q->context
#define WRDLISTS  q->wordlists
#define WORDLIST  q->wordlist
#define WLNAME    q->wordlistname
#define File      q->FileStack[FILEDEPTH]
#define TOKEN     q->token
#define HEADER    q->Header
#define ME        q->me
#define CORE      q->VMlist[q->core]

void DataPush(cell x) {
    q->ds[++SP] = x;
}

cell DataPop(void) {
    return q->ds[SP--];
}

void DataDot(cell x) {
    printf("%s ", itos(x, BASE, 0, 0, CELLBITS));
}

void PrintDataStack(void) {
    for (int i = 1; i <= SP; i++) {
        DataDot(q->ds[i]);
    }
}

void cdump(const uint8_t *src, uint16_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if ((i % 33) == 0) printf("\n___");
        printf("%02X ", src[i]);
    }
    printf("<- ");
}
SV Verbosity (void) { VERBOSE = DataPop(); }


//##############################################################################
//##  Dictionary
//##  The dictionary uses a static array of data structures loaded at startup.
//##  Links are int indices into this array of Headers.
//##############################################################################


// The search order is a list of wordlists with CONTEXT searched first
// order:   wid1 wid2 wid3
// context-----^             Context is a list, not a stack.
// A wid points to a linked list of headers.
// The head pointer of the list is created by WORDLIST.

SV printWID(int wid) {
    if ((wid < 1) || (wid > WRDLISTS)) {
        printf("? ");
    } else {
        char* s = WLNAME[wid];
        if (*s) printf("%s ", s);
        else    DataDot(wid);
    }
}

SV Order(void) {
    printf(" Context : ");
    for (int i = 0; i < ORDERS; i++)  printWID(CONTEXT[i]);
    printf("\n Current : ");  printWID(CURRENT);
    printf("\n");
}

SI findinWL(char* key, int wid) {       // find in wordlist
    uint16_t i = q->wordlist[wid];
    uint32_t limit = MaxKeywords;
//    printf("findinWL(%s, %d) link=%d\n", key, wid, i);
    if (strlen(key) < MaxNameSize) {
        while (limit--) {
            if (i == 0) return -1;
            if (strcmp(key, HEADER[i].name) == 0) {
                if (HEADER[i].smudge == 0) {
                    ME = i;
                    return i;
                }
            }
//            printf("%d <-- %d\n", i, HEADER[i].link);
            i = HEADER[i].link;
        }
    }
    return -1;                          // return index of word, -1 if not found
}

SI FindWord(char* key) {                // find in context
    for (cell i = 0; i < ORDERS; i++) {
        int wid = CONTEXT[i];
        int id = findinWL(key, wid);
        if (id >= 0) {
            HEADER[ME].references += 1; // bump reference counter
            q->WidName = WLNAME[i];
            return id;
        }
    }
    return -1;
}

SI Ctick(char* name) {
    if (FindWord(name) < 0) {
        ERROR = UNRECOGNIZED;
        // printf("<%s> ", name);
        return 0;
    }
    return HEADER[ME].target;           // W field of found word
}

/* Wordlists are on the host. A copy of header space is made by MakeHeaders for
use by the target's interpreter. ANS Forth's WORDLIST is not useful because it
can't be used in Forth definitions. We don't get the luxury of temporary
wordlists but "wordlists--;" can be used by C to delete the last wordlist.
*/

SI AddWordlist(char *name) {
    q->wordlist[++WRDLISTS] = 0;       // start with empty wordlist
    WLNAME[WRDLISTS] = name;
    if (WRDLISTS == (MaxWordlists - 1)) ERROR = BAD_WID_OVER;
    return WRDLISTS;
}

SV OrderPush(uint8_t wid) {
    if ((wid < 1) || (wid > WRDLISTS)) {
        ERROR = BAD_WID;
        return;
    }
    if (ORDERS >= MaxOrder) {
        ERROR = BAD_ORDER_OVER;
        return;
    }
    int n = ORDERS;
    int *s = &CONTEXT[n++];
    int *d = &CONTEXT[n];
    ORDERS = n;
    while (n--) *d-- = *s--;
    CONTEXT[0] = wid;
}

SI OrderPop(void) {
    uint8_t r = 1;
    int n = ORDERS - 1;
    if (n < 0) {
        ERROR = BAD_ORDER_UNDER;
    } else {
        r = CONTEXT[0];
        memcpy(&CONTEXT[0], &CONTEXT[1], n * sizeof(int));
        ORDERS = n;
    }
    return r;
}

SV Only       (void) { ORDERS = 0; OrderPush(q->root); OrderPush(q->root); }
SV ForthLex   (void) { CONTEXT[0] = CORE.forth; }
SV Definitions(void) { CURRENT = CONTEXT[0]; }
SV PlusOrder  (void) { OrderPush(DataPop()); }
SV MinusOrder (void) { DataPush(OrderPop()); }
SV SetCurrent (void) { CURRENT = DataPop(); }
SV GetCurrent (void) { DataPush(CURRENT); }

//##############################################################################
//##  File I/O and parsing
//##############################################################################

static char BOMmarker[4] = {0xEF, 0xBB, 0xBF, 0x00};

static void SwallowBOM(FILE *fp) {      // swallow leading UTF8 BOM marker
    char BOM[4];                        // to support utf-8 files on Windows
    (void)(fgets(BOM, 4, fp) != NULL);
    if (strcmp(BOM, BOMmarker)) {
        rewind(fp);                     // keep beginning of file if no BOM
    }
}

static int OpenNewFile(char *name) {    // Push a new file onto the file stack
    FILEDEPTH++;  FILEID++;
    File.fp = fopenx(name, "r");
    File.LineNumber = 0;
    File.Line[0] = 0;
    File.FID = FILEID;
    if (File.fp == NULL) {              // cannot open
        FILEDEPTH--;
        return BAD_OPENFILE;
    } else {
        if ((FILEDEPTH >= MaxFiles) || (FILEID >= MaxFilePaths))
            return BAD_INCLUDING;
        else {
            SwallowBOM(File.fp);
            strmove(q->FilePaths[FILEID].filepath, name, LineBufferSize);
        }
    }
    return 0;
}

// the quick brown fox jumped
// >in before -----^   ^--- after, TOKEN = fox\0

static int parseword(char delimiter) {
    while (TIB[TOIN] == delimiter) TOIN++;
    int length = 0;
    while (1) {
        char c = TIB[TOIN];
        if (c == 0) break;              // hit EOL
        TOIN++;
        if (c == delimiter) break;
        TOKEN[length++] = c;
    }
    TOKEN[length] = 0;                  // tok is zero-delimited
    return length;
}

static void ParseFilename(void) {
    while (TIB[TOIN] == ' ') TOIN++;
    if (TIB[TOIN] == '"') {
        parseword('"');                 // allow filename in quotes
    }
    else {
        parseword(' ');                 // or a filename with no spaces
    }
}

static void Include(void) {             // Nest into a source file
    ParseFilename();
    ERROR = OpenNewFile(TOKEN);
}

static void trimCR(char* s) {           // clean up the buffer returned by fgets
    char* p;
    if ((p = strchr(s, '\n')) != NULL) *p = '\0';
    size_t len = strlen(s);
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\t') s[i] = ' ';   // replace tabs with blanks
        if (s[i] == '\r') s[i] = '\0';  // trim CR if present
    }
}

static int refill(void) {
    int result = -1;
ask: TOIN = 0;
    int lineno = File.LineNumber++;
    if (File.fp == stdin) {
        printf("ok>");
        lineno = 0;
    }
    if (fgets(TIB, MAXTIB, File.fp) == NULL) {
        result = 0;
        if (FILEDEPTH) {                // un-nest from last include
            fclose(File.fp);
            FILEDEPTH--;
            goto ask;
        }
    } else {
        trimCR(TIB);                    // valid input line
    }
    strmove(File.Line, TIB, LineBufferSize);
    if (VERBOSE & VERBOSE_SOURCE)
        printf("%d: %s\n", lineno, TIB);
    return result;
}

SV Words(void) {
    parseword(' ');                     // tok is the search key (none=ALL)
    uint16_t i = q->wordlist[CONTEXT[0]];
    while (i) {
        size_t len = strlen(TOKEN);       // filter by substring
        char* s = strstr(HEADER[i].name, TOKEN);
        if ((s != NULL) || (len == 0))
            printf("%s ", HEADER[i].name);
        i = HEADER[i].link;         // traverse from oldest
    }
    printf("\n");
}


//##############################################################################
//##  Text Interpreter
//##  Processes a line at a time from either stdin or a file.
//##############################################################################

static void Tick (void) {               // get the w field of the word
    parseword(' ');
    DataPush(Ctick(TOKEN));
}

static void BracketTick (void) {
    parseword(' ');
    ForthLiteral(Ctick(TOKEN));
}

static void Number(char* s) {
    if (BASE == 0) {
        ERROR = DIV_BY_ZERO;
        return;
    }
    int i = 0;
    char c = 0;
    int64_t x = 0;
    int neg = 0;
    DPL = -1;
    switch (s[0]) {   // leading digit
    case '-': i++;  neg = -1;   break;
    case '+': i++;              break;
    case '$': i++;  BASE = 16;  break;
    case '#': i++;  BASE = 10;  break;
    case '&': i++;  BASE = 8;   break;
    case '%': i++;  BASE = 2;   break;
    case '\0': { goto bogus; }
    default: break;
    }
    while ((c = s[i++])) {              // convert string to number
        switch (c) {
        case '.':  DPL = i;     break;
        default: c = c - '0';
            if (c & 0x80) goto bogus;
            if (c > 9) {
                c -= 7;
                if (c < 10) goto bogus;
            }
            if (c > 41) c -= 32;        // lower to upper
            if (c >= BASE)
bogus:          ERROR = UNRECOGNIZED;
            x = x * BASE + c;
        }
    }
    if (neg) x = -x;                    // sign number
    if (ERROR == 0) {
        if (DPL < 0) {                  // single-length number
            x &= CELLMASK;
            if (STATE) {
                ForthLiteral((cell)x);
            } else {
                DataPush((cell)(x));
            }
        } else {                        // double-length number
            if (STATE) {
                ForthLiteral((x & CELLMASK));
                ForthLiteral((x >> CELLBITS) & CELLMASK);
            } else {
                DataPush((x & CELLMASK));
                DataPush((x >> CELLBITS)& CELLMASK);
            }
        }
    }
}

int AddHead (char* name, char* help) { // add a header to the list
    int r = 1;
    HP++;
    if (HP < MaxKeywords) {
        HEADER[HP].name = name;
        HEADER[HP].help = help;
        HEADER[HP].length = 0;          // set defaults to 0
        HEADER[HP].notail = 0;
        HEADER[HP].target = 0;
        HEADER[HP].notail = 0;
        HEADER[HP].smudge = 0;
        HEADER[HP].isALU = 0;
        HEADER[HP].srcFile = File.FID;
        HEADER[HP].srcLine = File.LineNumber;
        HEADER[HP].link = WORDLIST[CURRENT];
        HEADER[HP].references = 0;
        HEADER[HP].w2 = 0;
        WORDLIST[CURRENT] = HP;
    } else {
        printf("Please increase MaxKeywords and rebuild.\n");
        r = 0;  ERROR = BYE;
    }
    return r;
}

void SetFns (cell value, void (*exec)(), void (*comp)()) {
    HEADER[HP].w = value;
    HEADER[HP].ExecFn = exec;
    HEADER[HP].CompFn = comp;
}

void AddKeyword (char* name, char* help, void (*xte)(), void (*xtc)()) {
    if (AddHead(name, help)) {
        SetFns(NOTANEQU, xte, xtc);
    }
}

void noCompile(void) { ERROR = BAD_NOCOMPILE; }
void noExecute(void) { ERROR = BAD_NOEXECUTE; }
SV Bye(void) {ERROR = BYE;}
//SV POR(void) {q->BCIinitFn(vm_ctx *ctx);}

SV AddRootKeywords(void) {
    HP = 0; // start empty
    WRDLISTS = 0;
    // Forth definitions
    q->root = AddWordlist("root");
    int wid = AddWordlist("forth");
    for (int i = 0; i < CPUCORES; i++) {
        CORE.forth = wid;
    }
    Only();
    Definitions();
//                            v--- ~ = https://forth-standard.org/standard/
    AddKeyword("bye",        "~tools/BYE --",                       Bye,         noCompile);
    AddKeyword("only",       "~search/ONLY --",                     Only,        noCompile);
    AddKeyword("order",      "~search/ORDER --",                    Order,       noCompile);
    AddKeyword("set-current","~search/SET-CURRENT wid --",          SetCurrent,  noCompile);
    AddKeyword("get-current","~search/GET-CURRENT -- wid",          GetCurrent,  noCompile);
    AddKeyword("definitions","~search/DEFINITIONS --",              Definitions, noCompile);
    AddKeyword("+order",     " wid --",                             PlusOrder,   noCompile);
    AddKeyword("-order",     " -- wid",                             MinusOrder,  noCompile);
    AddKeyword("include",    "~file/INCLUDE i*x \"name\" -- j*x",   Include,     noCompile);
    AddKeyword("'",          "~core/Tick <spaces>\"name\" -- xt",   Tick,        noCompile);
    AddKeyword("[']",        "~core/BracketTick <spaces>\"name\" -- xt", noExecute, BracketTick);
    AddKeyword("forth",      "~search/FORTH --",                    ForthLex,    noCompile);
    AddKeyword("verbosity",  " flags --",                           Verbosity,   noCompile);
    AddKeyword("words",      "~tools/WORDS --",                     Words,       noCompile);
}

int quitloop(char * line, int maxlength, struct QuitStruct *state) {
    q = state;
    TIB = line;
    MAXTIB = maxlength;                 // assign a working buffer
    state->BCIfn = BCIhandler;          // BCI command transmit function
    state->BCIinitFn = BCIinitial;      // BCI reset function

//    VERBOSE = -1;
    VERBOSE = VERBOSE_BCI;

    AddRootKeywords();
    AddForthKeywords(q);
    FILEID = 0;
    BASE = 10;
    while (1) {
        FILEDEPTH = 0;
        File.fp = stdin;                // keyboard input
        ERROR = 0;                      // interpreter state
        STATE = 0;
        q->startup_us = GetMicroseconds();
        while (ERROR == 0) {
            TOIN = 0;
            int TIBlen = strlen(TIB);
            if (TIBlen > MAXTIB) ERROR = BAD_INPUT_LINE;
            uint64_t time0 = GetMicroseconds();
            while (parseword(' ')) {
                if (VERBOSE & VERBOSE_TOKEN) {
                    printf("  `%s`", TOKEN);
                }
                int h = FindWord(TOKEN);
                if (h != -1) {          // the token is a word
                    if (STATE) HEADER[h].CompFn();
                    else       HEADER[h].ExecFn();
                } else {                // or a number
                    Number(TOKEN);
                }
                if (VERBOSE & VERBOSE_TOKEN) {
                    printf(" ( ");
                    PrintDataStack();
                    printf(")\n");
                }
                if (VERBOSE & VERBOSE_SRC) {
                    printf(") (\"%s\"", &TIB[TOIN]);
                }
                if (SP < 0) ERROR = BAD_STACKUNDER;
                if (ERROR) {
                    switch (ERROR) {
                    case BYE: return 0;
                    default: ErrorMessage (ERROR, TOKEN);
                    }
                    while (FILEDEPTH) {
                        printf("%s, Line %d: ",
                            q->FilePaths[File.FID].filepath, File.LineNumber);
                        printf("%s\n", File.Line);
                        fclose(File.fp);
                        FILEDEPTH--;
                    }
                    SP = 0;
                    goto done;
                }
            }
done:       q->elapsed_us = GetMicroseconds() - time0;
            if (File.fp == stdin) {
                if (SP) {
                    printf("\\ ");
                    PrintDataStack();
                }
            }
            refill();
        }
    }
}
