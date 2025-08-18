#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include "quit.h"

#ifdef _MSC_VER
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "forth.h"
#include "see.h"
#include "tools.h"
#include "comm.h"
#include "../bci/bci.h"

struct QuitStruct quit_internal_state;
static struct QuitStruct *q;

#define TIB       q->tib            // location of text input buffer
#define MAXTIB    q->maxtib         // buffer size available for tib's zstring
#define VERBOSE   q->verbose
#define ERR       q->error          // did I err?
#define TOIN      q->toin           // offset into tib
#define HP        q->hp
#define SP        q->sp             // data stack pointer
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
#define CORE      q->VMlist[q->core] // Caution: only access static VM values

void DataPush(uint32_t x) {
    q->ds[++SP] = x;
}

uint32_t DataPop(void) {
    return q->ds[SP--];
}

void DataDot(uint32_t x) {
    printf("%s ", itos(x, BASE, 0, 1, BitsPerCell()));
}

void PrintDataStack(void) {
    for (int i = 1; i <= SP; i++) {
        DataDot(q->ds[i]);
    }
}

static void Verbosity (void) { VERBOSE = DataPop(); }

// Names are const data, which need "text" storage at runtime

static char dictspace[DictionaryBytes];
static unsigned int textsp = 0;

static void AppendDictionary(uint8_t c) {
    if (textsp >= (DictionaryBytes - 1)) ERR = DICTIONARY_OVER;
    dictspace[textsp++] = c;
}
static void AppendDictionaryN(uint32_t x, int width) {
    uint8_t *s = (uint8_t*)&x;
    while (width--) AppendDictionary(*s++);
}
uint32_t FetchDictionaryN(int addr, int width) {
    uint32_t x = 0;
    uint8_t *d = (uint8_t*)&x;
    char *s = &dictspace[addr];
    if (width < 5) {
        while (width--) *d++ = (uint8_t)*s++;
    }
    return x;
}


char * Const(char *name) {          // convert ephemeral string to constant
    int limit = 1024;
    char * r = &dictspace[textsp];
    while (limit--) {
        uint8_t c = *name++;
        AppendDictionary(c);
        if (ERR) goto no;
        if (c == 0) return r;
    }
no: return "bad";
}

void Color(const char * color) {
    if (VERBOSE & VERBOSE_COLOR) printf("%s", color);
}

void message(const char *color, const char *s) {
    Color(color);  printf("%s", s);
    Color(COLOR_NONE);
}

//############################################################################
//##  Dictionary
//##  The dictionary uses a static array of data structures loaded at startup.
//##  Links are int indices into this array of Headers.
//############################################################################


// The search order is a list of wordlists with CONTEXT searched first
// order:   wid1 wid2 wid3
// context-----^             Context is a list, not a stack.
// A wid points to a linked list of headers.
// The head pointer of the list is created by WORDLIST.

static void printWID(int wid) {
    if ((wid < 1) || (wid > WRDLISTS)) {
        message(COLOR_RED, "? ");
    } else {
        char* s = WLNAME[wid];
        if (*s) printf("%s ", s);
        else    DataDot(wid);
    }
}

static void Order(void) {
    printf(" Context : ");
    for (int i = 0; i < ORDERS; i++)  printWID(CONTEXT[i]);
    printf("\n Current : ");  printWID(CURRENT);
    printf("\n");
}

// find in wordlist wid, return "found" flag: -1 if found, 0 if not found
static int findinWL(char* key, int wid) {
    uint16_t i = q->wordlist[wid];
    uint32_t limit = MaxKeywords;
    if (strlen(key) < MaxNameSize) {
        while (limit--) {
            if (i == 0) return 0;
            if (strcmp(key, HEADER[i].name) == 0) {
                if (HEADER[i].smudge == 0) {
                    ME = i;
                    return -1;
                }
            }
            i = HEADER[i].link;
        }
    }
    return 0;
}

static int FindWord(char* key) {    // find in context, xt is ME
    for (int i = 0; i < ORDERS; i++) {
        int wid = CONTEXT[i];
        if (findinWL(key, wid)) {
            q->WidName = WLNAME[i];
            if (File.fp != stdin) {
                HEADER[ME].references++;
                int link = HEADER[ME].where;
                HEADER[ME].where = textsp;
                AppendDictionaryN(link, 3);
                AppendDictionaryN(File.FID, 2);
                AppendDictionaryN(File.LineNumber, 3);
            }
            return -1;
        }
    }
    return 0;
}

static int Ctick(char* name) {
    if (FindWord(name) == 0) {
        ERR = UNRECOGNIZED;
        return 0;
    }
    return HEADER[ME].target;       // W field of found word
}

/* Wordlists are on the host. A copy of header space is made by MakeHeaders
for use by the target's interpreter. ANS Forth's WORDLIST is not useful
because it can't be used in Forth definitions. We don't get the luxury of
temporary wordlists but "wordlists--;" can be used by C to delete the last
wordlist.
*/

static int AddWordlist(char *name) {
    q->wordlist[++WRDLISTS] = 0;   // start with empty wordlist
    WLNAME[WRDLISTS] = name;
    if (WRDLISTS == (MaxWordlists - 1)) ERR = BAD_WID_OVER;
    return WRDLISTS;
}

static void OrderPush(uint8_t wid) {
    if ((wid < 1) || (wid > WRDLISTS)) {
        ERR = BAD_WID;
        return;
    }
    if (ORDERS >= MaxOrder) {
        ERR = BAD_ORDER_OVER;
        return;
    }
    int n = ORDERS;
    int *s = &CONTEXT[n++];
    int *d = &CONTEXT[n];
    ORDERS = n;
    while (n--) *d-- = *s--;
    CONTEXT[0] = wid;
}

static int OrderPop(void) {
    uint8_t r = 1;
    int n = ORDERS - 1;
    if (n < 0) {
        ERR = BAD_ORDER_UNDER;
    } else {
        r = CONTEXT[0];
        memcpy(&CONTEXT[0], &CONTEXT[1], n * sizeof(int));
        ORDERS = n;
    }
    return r;
}

static void Only       (void) { ORDERS = 0; OrderPush(q->host);
                                OrderPush(q->host); }
static void ForthLex   (void) { CONTEXT[0] = CORE.forth; }
static void HostLex    (void) { CONTEXT[0] = q->host; }
static void ForthWID   (void) { DataPush(CORE.forth); }
static void Definitions(void) { CURRENT = CONTEXT[0]; }
static void Also       (void) { OrderPush(CONTEXT[0]); }
static void Previous   (void) { OrderPop(); }
static void SetVocab   (void) { CONTEXT[0] = HEADER[ME].w; }

//############################################################################
//##  File I/O and parsing
//############################################################################

static const uint8_t BOMmarker[4] = {0xEF, 0xBB, 0xBF, 0x00};

static void SwallowBOM(FILE *fp) {  // swallow leading UTF8 BOM marker
    char BOM[4];                    // to support utf-8 files on Windows
    (void)(fgets(BOM, 4, fp) != NULL);
    if (strcmp(BOM, (char*)BOMmarker)) {
        rewind(fp);                 // keep beginning of file if no BOM
    }
}

static int OpenNewFile(char *name){ // Push a new file onto the file stack
    FILEDEPTH++;  FILEID++;
    File.fp = fopenx(name, "r");
    File.LineNumber = 0;
    File.Line[0] = 0;
    File.FID = FILEID;
    if (File.fp == NULL) {          // cannot open
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

int parseword(char delimiter) {
    while (TIB[TOIN] == delimiter) TOIN++;
    int length = 0;
    while (1) {
        char c = TIB[TOIN];
        if (c == 0) break;           // hit EOL
        TOIN++;
        if (c == delimiter) break;
        TOKEN[length++] = c;
    }
    TOKEN[length] = 0;              // tok is zero-delimited
    return length;
}

void ParseFilename(void) {
    while (TIB[TOIN] == ' ') TOIN++;
    if (TIB[TOIN] == '"') {
        parseword('"');             // allow filename in quotes
    }
    else {
        parseword(' ');             // or a filename with no spaces
    }
}

static void Include(void) {         // Nest into a source file
    ParseFilename();
    ERR = OpenNewFile(TOKEN);
}

static void trimCR(char* s) {       // clean up the buffer returned by fgets
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
        message(COLOR_GREEN, "ok>");
        lineno = 0;
    }
    if (fgets(TIB, MAXTIB, File.fp) == NULL) {
        result = 0;
        if (FILEDEPTH) {            // un-nest from last include
            fclose(File.fp);
            FILEDEPTH--;
            goto ask;
        }
    } else {
        trimCR(TIB);                // valid input line
    }
    strmove(File.Line, TIB, LineBufferSize);
    if (VERBOSE & VERBOSE_SOURCE)
        printf("%d: %s\n", lineno, TIB);
    return result;
}

char * GetToken (void) {
    parseword(' ');
    return TOKEN;
}

void Tick (void) {                  // get the w field of the word
    DataPush(Ctick(GetToken()));
}

static void BracketTick (void) {
    ForthLiteral(Ctick(GetToken()));
}

// [IF ] [ELSE] [THEN] (note: must be followed by newline if at end of file)

static void BrackElse(void) {
    int level = 1;
    while (level) {
        char *s = GetToken();
        int length = (int)strlen(s);
        if (length) {
            if (!strcmp(s, "[if]")) {
                level++;
            }
            if (!strcmp(s, "[then]")) {
                level--;
            }
            if (!strcmp(s, "[else]") && (level == 1)) {
                level--;
            }
        } else {                        // EOL
            if (!refill()) {
                ERR = BAD_EOF;
                return;
            }
        }
    }
}
static void BrackIf(void)        { if (!DataPop())  BrackElse(); }
static void BrackDefined(void)   { DataPush(FindWord(GetToken())); }
static void BrackUndefined(void) { BrackDefined();  DataPush(~DataPop()); }

// T{ 0 0 AND -> 0 }T

static uint8_t sp0;
static uint8_t actual_sp;
static uint32_t actual_results[256];

static void BeginTest(void) { // t{
    sp0 = SP;
}

static void DoTest(void) { // ->
    actual_sp = SP;
    memcpy(actual_results, &q->ds[1], SP*sizeof(uint32_t));
    SP = sp0;
}

static void EndTest(void) { // }t
    if (actual_sp != SP) {
        ERR = WRONG_NUM_OF_RESULTS;
        return;
    }
    if (memcmp(actual_results, &q->ds[1], SP*sizeof(uint32_t))) {
        ERR = WRONG_TEST_RESULTS;
        cdump((const uint8_t*)actual_results, SP*sizeof(uint32_t));
        printf("actual");
        cdump((const uint8_t*)&q->ds[1], SP*sizeof(uint32_t));
        printf("expected\n");
    }
    SP = sp0;
}

//############################################################################
//##  Text Interpreter
//##  Processes a line at a time from either stdin or a file.
//############################################################################

static void SkipToPar (void) { parseword(')'); }
static void EchoToPar (void) { SkipToPar();  printf("%s", TOKEN); }
static void SkipToEOL (void) { TOIN = (uint16_t)strlen(TIB); }
static void BaseStore (void) { int n = DataPop(); if (n > 1) BASE = n; }
static char* Source   (void) { char *src = &TIB[TOIN]; SkipToEOL();
                               return src; }
static void EchoToEOL (void) { printf("%s\n", Source()); }
char * TIBtoEnd       (void) { return Const(Source()); }

#ifdef _MSC_VER
static void Chdir(void) { ERR =
                 SetCurrentDirectoryA(Source()) ? INVALID_DIRECTORY : 0; }
#else
static void Chdir(void) { ERR = chdir(Source()) ? INVALID_DIRECTORY : 0; }
#endif

void ShowLine(void) {
    if (File.fp != stdin) {
        Color(COLOR_CYAN);
        printf("%s, Line %d: ",
               q->FilePaths[File.FID].filepath, File.LineNumber);
        Color(COLOR_NONE);
        printf("%s\n", File.Line);
    }
}

static uint32_t CellBitMask(void) {
    int bpc = BitsPerCell();
    if (bpc == 32) return 0xFFFFFFFF;
    return (1 << bpc) - 1;
}

static void Number(char* s) {
    if (BASE == 0) {
        ERR = DIV_BY_ZERO;
        return;
    }
    int i = 0;
    char c = 0;
    int64_t x = 0;
    int neg = 0;
    DPL = -1;
    int base = BASE;
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
bogus:          ERR = UNRECOGNIZED;
            x = x * BASE + c;
        }
    }
    BASE = base;                        // restore original base
    if (neg) x = -x;                    // sign number
    if (ERR == 0) {
        if (DPL < 0) {                  // single-length number
            x &= CellBitMask();
            if (STATE) {
                ForthLiteral((uint32_t)x);
            } else {
                DataPush((uint32_t)(x));
            }
        } else {                        // double-length number
            if (STATE) {
                ForthLiteral((x & CellBitMask()));
                ForthLiteral((x >> BitsPerCell()) & CellBitMask());
            } else {
                DataPush((x & CellBitMask()));
                DataPush((x >> BitsPerCell()) & CellBitMask());
            }
        }
    }
}

int AddHead (char* name, char* help) { // add a header to the list
    int r = 1;
    HP++;
    if (HP < MaxKeywords) {
        memset(&HEADER[HP], 0, sizeof(HEADER[0]));
        HEADER[HP].name = name;
        HEADER[HP].help = help;
        HEADER[HP].srcFile = File.FID;
        HEADER[HP].srcLine = File.LineNumber;
        HEADER[HP].link = WORDLIST[CURRENT];
        WORDLIST[CURRENT] = HP;
    } else {
        message(COLOR_RED, "Please increase MaxKeywords and rebuild.\n");
        r = 0;  ERR = BYE;
    }
    return r;
}

void SetFns (uint32_t value, void (*exec)(), void (*comp)()) {
    HEADER[HP].w = value;
    HEADER[HP].ExecFn = exec;
    HEADER[HP].CompFn = comp;
}

void AddKeyword (char* name, char* help, void (*xte)(), void (*xtc)()) {
    if (AddHead(name, help)) {
        SetFns(NOTANEQU, xte, xtc);
    }
}

void noCompile(void) { ERR = BAD_NOCOMPILE; }
void noExecute(void) { ERR = BAD_NOEXECUTE; }
static void Nothing(void) { }
static void Bye(void) {ERR = BYE;}
static void Halt(void) { CORE.ctx.status = BCI_STATUS_STOPPED; }
static void Run(void) { CORE.ctx.status = BCI_STATUS_RUNNING; }

static int hp0, wordlist0;
static void Empty(void) {
    memset(q->text, BLANK_FLASH_BYTE, sizeof(q->text));
    memset(q->code, BLANK_FLASH_BYTE, sizeof(q->code));
    for (int i = 0; i < CPUCORES; i++) {
        q->reloaded[i] = 0;
        q->dp[i] = 1;
        q->cp[i] = 0;
        q->tp[i] = 0;
    }
    HP = hp0;
    WRDLISTS = wordlist0;
    FILEID = 0;
    textsp = 0;
    Only(); ForthLex(); Definitions();
    Halt();
}

FILE* gfile = NULL;
static void Gild(void) {
    ParseFilename();
    gfile = fopenx(TOKEN, "wb");
    uint32_t x = sizeof(quit_internal_state);
    fwrite(&x, 1, sizeof(x), gfile);    // save q length and data
    fwrite(&quit_internal_state, 1, sizeof(quit_internal_state), gfile);
    x = textsp;
    fwrite(&x, 1, sizeof(x), gfile);    // save dictionary
    fwrite(dictspace, 1, textsp, gfile);
    fclose(gfile);
}

static void Vocabulary(void) {
    char *name = Const(GetToken());
    int wid = AddWordlist(name);
    if (AddHead(name, "")) {
        SetFns(wid, SetVocab, noCompile);
    }
}

static void AddRootKeywords(void) {
    HP = 0; // start empty
    WRDLISTS = 0;
    // Forth definitions
    q->host = AddWordlist("host");
    for (int i = 0; i < CPUCORES; i++) {
        CORE.forth = AddWordlist("forth");
        q->dp[i] = 1;                   // data[0] reserved for BCI
    }
    Only(); Definitions();
//                               v--- ~ = https://forth-standard.org/standard/
    AddKeyword("bye",           "~tools/BYE --",
               Bye,             noCompile);
    AddKeyword("empty",         "-quit.htm#empty --",
               Empty,           noCompile);
    AddKeyword("gild",          "-quit.htm#gild --",
               Gild,            noCompile);
    AddKeyword("halt",          "-quit.htm#halt --",
               Halt,            noCompile);
    AddKeyword("run",           "-quit.htm#run --",
               Run,             noCompile);
    AddKeyword("cd",            "-quit.htm#cdir ccc<EOL> --",
               Chdir,           noCompile);
    AddKeyword("base!",         "-quit.htm#basestore n --",
               BaseStore,       noCompile);
    AddKeyword("vocabulary",    "-quit.htm#vocab <name> --",
               Vocabulary,      noCompile);
    AddKeyword("only",          "~search/ONLY --",
               Only,            noCompile);
    AddKeyword("order",         "~search/ORDER --",
               Order,           noCompile);
    AddKeyword("definitions",   "~search/DEFINITIONS --",
               Definitions,     noCompile);
    AddKeyword("also",          "~search/ALSO <name> --",
               Also,            noCompile);
    AddKeyword("previous",      "~search/PREVIOUS -- ",
               Previous,        noCompile);
    AddKeyword("include",       "~file/INCLUDE i*x \"name\" -- j*x",
               Include,         noCompile);
    AddKeyword("'",             "~core/Tick <spaces>\"name\" -- xt",
               Tick,            noCompile);
    AddKeyword("[']",           "~core/BracketTick <spaces>\"name\" -- xt",
               noExecute,       BracketTick);
    AddKeyword("host",          "-quit.htm#host --",
               HostLex,         noCompile);
    AddKeyword("forth",         "~search/FORTH --",
               ForthLex,        noCompile);
    AddKeyword("*forth",        "-quit.htm#frth -- wid",
               ForthWID,        noCompile);
    AddKeyword("verbose!",      "-quit.htm#verbsto mask --",
               Verbosity,       noCompile);
    AddKeyword("(",             "~core/p ccc<paren> --",
               SkipToPar,       SkipToPar);
    AddKeyword("\\",            "~core/bs ccc<EOL> --",
               SkipToEOL,       SkipToEOL);
    AddKeyword("\\.",           "-quit.htm#bsdot ccc<EOL> --",
               EchoToEOL,       noCompile);
    AddKeyword(".(",            "~core/Dotp ccc<paren> --",
               EchoToPar,       noCompile);
    AddKeyword("[if]",          "~tools/BracketIF flag --",
               BrackIf,         noCompile);
    AddKeyword("[then]",        "~tools/BracketTHEN --",
               Nothing,         noCompile);
    AddKeyword("[else]",        "~tools/BracketELSE --",
               BrackElse,       noCompile);
    AddKeyword("[undefined]",   "~tools/BracketUNDEFINED \"name\" -- flag",
               BrackUndefined,  noCompile);
    AddKeyword("[defined]",     "~tools/BracketDEFINED \"name\" -- flag",
               BrackDefined,    noCompile);
    AddKeyword("}t",            "-quit.htm#tend --",
               EndTest,         noCompile);
    AddKeyword("->",            "-quit.htm#tmiddle ... --",
               DoTest,          noCompile);
    AddKeyword("t{",            "-quit.htm#tbegin ... --",
               BeginTest,       noCompile);
    AddKeyword("cells",         "~core/CELLS x1 -- x2",
               Nothing,         Nothing);
    AddKeyword("chars",         "~core/CHARS x1 -- x2",
               Nothing,         Nothing);
}

static const uint8_t BaseChar[] = {"??%.....&.#.....$"};

int QuitLoop(char * line, int maxlength, struct QuitStruct *state) {
    q = state;
    TIB = line;
    MAXTIB = maxlength;                 // assign a working buffer
    AddRootKeywords();
    AddForthKeywords(q);
    AddSeeKeywords(q);
    AddCommKeywords(q);
    hp0 = HP; wordlist0 = WRDLISTS;     // for Empty
    BASE = 10;
    VERBOSE = VERBOSE_COLOR;
    Empty();
    while (1) {
        FILEDEPTH = 0;
        File.fp = stdin;                // keyboard input
        ERR = 0;
        STATE = 0;                      // interpreter state
        q->startup_us = GetMicroseconds();
        while (ERR == 0) {
            TraceBufClear();
            TOIN = 0;
            int TIBlen = (int)strlen(TIB);
            if (TIBlen > MAXTIB) ERR = BAD_INPUT_LINE;
            uint64_t time0 = GetMicroseconds();
            if (VERBOSE & VERBOSE_SRC) {
                printf("TIB={%s}\n", TIB);
            }
            while (parseword(' ')) {
                if (VERBOSE & VERBOSE_TOKEN) {
                    printf("  `%s`", TOKEN);
                }
                if (FindWord(TOKEN)) {  // the token is a word
                    if (STATE) HEADER[ME].CompFn();
                    else       HEADER[ME].ExecFn();
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
                if (SP < 0) ERR = BAD_STACKUNDER;
                if (ERR) {
                    switch (ERR) {
                    case BYE: {
                        ComClose();
                        return 0;
                    }
                    default: Color(COLOR_RED);
                        printf("%s ", ErrorMessage (ERR, TOKEN));
                        Color(COLOR_NONE);
                        if (VERBOSE & VERBOSE_FATAL) return ERR;
                    }
                    while (FILEDEPTH) {
                        ShowLine();
                        fclose(File.fp);
                        FILEDEPTH--;
                    }
                    SP = 0;
                    goto done;
                }
            }
done:       q->elapsed_us = GetMicroseconds() - time0;
            if (File.fp == stdin) {
                if ((q->cycles) && (VERBOSE & VERBOSE_CYCLES)) {
                    printf("\\ %" PRIu64 " cycles ", q->cycles);
                }
                if (SP) {
                    printf("\\");
                    if (BASE < 17) {
                        printf("%c", BaseChar[BASE]);
                    }
                    printf(" ");
                    PrintDataStack();
                }
            }
            q->cycles = 0;
            refill();
        }
    }
}
