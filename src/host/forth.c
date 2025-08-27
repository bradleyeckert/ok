#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "quit.h"
#include "forth.h"
#include "comm.h"
#include "tools.h"
#include "../bci/bci.h"

static struct QuitStruct *q;

#define CORE      q->core
#define TP        q->tp[CORE]           // text space pointer
#define DP        q->dp[CORE]           // data space pointer
#define CP        q->cp[CORE]           // code space pointer
#define NP        q->np                 // NVM space pointer
#define SP        q->sp                 // data stack pointer
#define HP        q->hp
#define BASE      q->base
#define STATE     q->state
#define ERR       q->error              // detected error
#define HEADER    q->Header
#define ME        q->me
#define VERBOSE   q->verbose
#define TOKEN     q->token
#define INST_TAG  0x80000000            // executes as individual instruction
#define IS_UOP(x) (INST_TAG | VM_UOPS \
                  | (x << ((VM_INSTBITS - 2) % 5)))

static void dotESS (void) {             // ( ... -- ... )
    PrintDataStack();
    printf("<-Top\n");
}

static void dot(void) {                 // ( n -- )
    DataDot(DataPop());
}

static uint32_t my (void) {             // value in last-found header
    return HEADER[ME].w;
}

static uint8_t getBP(int index) {
    return q->VMlist[CORE].ctx.boilerplate[index];
}

int BitsPerCell(void) {return getBP(1);}
static uint32_t DataMemSize(void) {return (getBP(5) + 1) << 8;}
static uint32_t CodeMemSize(void) {return (getBP(6) + 1) << 8;}
static uint32_t TextMemSize(void) {return (getBP(7) + 1) << 10;}
static uint32_t TextMemOrigin(void) {return getBP(8) << 12;}

static void InstExecute(uint32_t xt) {
    if (!(xt & INST_TAG)) { // instructions do not need reload
        if (q->reloaded[CORE] == 0) {
            printf("reloading host --> target ");
            ShowLine();
            Reload();
        }
    }
    SendInit();
    SendChar(BCIFN_EXECUTE);
    SendCell((STATE << 8) + BASE);
    SendChar(SP);
    for (int i = 1; i <= SP; i++) SendCell(q->ds[i]);
    SendCell(xt); // xt or inst
    SendFinal();
    BCIwait("InstExecute", 1);
}

static void Prim_Exec(void) {
    if (VERBOSE & VERBOSE_TOKEN) {
        printf(" <Prim_Exec:%Xh>", my());
    }
    InstExecute(my());
}

//---------------------------------------------------------------------------
// Compiler

int slot = SLOT0_POSITION;
uint32_t instruction;

static void CodeComma(uint32_t inst) {
    q->code[CORE][CP++] = inst;
    q->reloaded[CORE] = 0;
    if (CP >= CodeMemSize()) {
        printf("Out of code memory, increase CODESIZE.\n");
        ERR = BCI_IOR_INVALID_ADDRESS;
        return;
    }
    instruction = 0;
    slot = SLOT0_POSITION;
}

static void NewInst(void) {
    if (slot != SLOT0_POSITION) CodeComma(VM_UOPS | instruction);
    instruction = 0;
}

static void InstCompile(uint32_t inst) {// compile instruction
    NewInst();                          // flush any uops
    CodeComma(inst);
}

static void CompCall(uint32_t addr) {
    NewInst();
    if (addr & ~VM_IMM_MASK) CodeComma(VMI_PFX + (addr >> VM_IMMBITS));
    CodeComma(VMI_CALL + (addr & VM_IMM_MASK));
}

static void CompUop  (uint32_t uop) {
    uop &= 0x1F; // slots = 9, 4, -1
    if (slot < -4) CodeComma(VM_UOPS | instruction);
    if (slot < 0) { // last slot
        slot = 0;
        if (uop >= (1 << LAST_SLOT_WIDTH)) {
            CodeComma(VM_UOPS | instruction);
        }
    }
    instruction |= uop << slot;
    slot -= 5;
}

static void uOp_Comp (void) { CompUop(my() >> LAST_SLOT_WIDTH); }
static void Op_Comp  (void) { InstCompile(my()); }

static void CompLit(uint32_t x) {       // unsigned cellbits-1 bits
    int depth = SP;
    DataPush(VMI_LIT | (x & VM_IMM_MASK));
    x = x >> (VM_INSTBITS - 3);
    while (x) {
        DataPush(VMI_PFX | (x & VM_IMMS_MASK));
        x = x >> (VM_INSTBITS - 7);
    }
    while (depth != SP) {
        InstCompile(DataPop());
    }
}

static void ForthCompile(uint32_t xt){
    CompCall(xt);
}

void ForthLiteral(uint32_t x){
    if (x & VM_SIGN) {
        CompLit(~x & VM_MASK);
        CompUop(VMO_INV);
    } else {
        CompLit(x);
    }
}


static int latest;
static int CtrlStack[256];              // control stack
static uint8_t ConSP = 0;
static int DefMarkID, DefMark;
static int notail; // inhibit tail calls

static void Def_Comp   (void) {
    notail = HEADER[ME].notail;
    ForthCompile(my());
}

static void cpFetch    (void) { DataPush(CP); }
static void cpStore    (void) { CP = DataPop(); }
static void dpFetch    (void) { DataPush(DP); }
static void dpStore    (void) { DP = DataPop(); }
static void toImmediate(void) { STATE = 0; }
static void toCompile  (void) { STATE = 1; }
static void tpFetch    (void) { DataPush(TP + TextMemOrigin()); }

static void tpStore    (void) {
    uint32_t p = DataPop();
    if (p < TextMemOrigin()) {
        printf("Text address is too low, minimum is 0x%X\n", TextMemOrigin());
        ERR = BCI_IOR_INVALID_ADDRESS;
        return;
    }
    uint32_t max = TextMemOrigin() + TextMemSize() - 1;
    if (p > max) {
        printf("Text address is too high, maximum is 0x%X\n", max);
        ERR = BCI_IOR_INVALID_ADDRESS;
        return;
    }
    TP = p;
}

/*
* Save memory contents as a blob file.
*/

FILE* blob;

static void ToBlob(int size, uint32_t x) {
    while (size--) {
        uint8_t c = x >> (size * 8);
        fwrite(&c, 1, 1, blob);
    }
}

static void saveblob(void) {                // ( revision -- )
    uint32_t codebytes = CP * sizeof(VMinst_t);
    uint32_t textbytes = TP * sizeof(VMcell_t);
    codebytes = (codebytes + 3) & ~3;
    ParseFilename();
    blob = fopenx(TOKEN, "wb");
    ToBlob(4, DataPop());                   // revision #
    ToBlob(4, codebytes + textbytes + 16);  // overall size
    ToBlob(4, codebytes);                   // code bytes
    ToBlob(4, textbytes);                   // text bytes
    fwrite(q->code[CORE], 1, codebytes, blob);
    fwrite(q->text[CORE], 1, textbytes, blob);
    fclose(blob);
}

static void NoName(void) {
    NewInst();
    DataPush(CP);
    DefMarkID = 0;                      // no length
    DefMark = CP;
    toCompile();
    latest = CP;
    ConSP = 0;
}

static void Definition(void) {
    if (AddHead(Const(GetToken()), "")) {
        NoName();
        uint32_t xt = DataPop();
        SetFns(xt, Prim_Exec, Def_Comp);
        HEADER[HP].target = xt;
        HEADER[HP].smudge = 1;
        HEADER[HP].core = CORE;
        DefMarkID = HP;                 // save for later reference
    }
}

static void EndDefinition(void) {       // resolve length of definition
    if (DefMarkID) {
        HEADER[DefMarkID].length = CP - DefMark;
        HEADER[DefMarkID].smudge = 0;
    }
    toImmediate();
}

const static uint8_t returnOps[] = {VMO_PUSH, VMO_R, VMO_POP};

static int UsesRetStack(uint32_t inst) {
    for (int i = SLOT0_POSITION; i >= -5; i -= 5) {
        uint8_t uop;
        if (i < 0) uop = inst & LAST_SLOT_MASK;
        else uop = (inst >> i) & 0x1F;
        for (int j = 0; j < sizeof(returnOps); j++) {
            if (returnOps[j] == uop) return 1;
        }
    }
    return 0;
}

static void Later(void) {
    Definition();  HEADER[HP].w2 = MAGIC_LATER;
    InstCompile(VMI_JUMP);
    EndDefinition();
}

static void MuchLater(void) {
    Definition();  HEADER[HP].w2 = MAGIC_LATER;
    InstCompile(VMI_PFX);
    InstCompile(VMI_JUMP);
    EndDefinition();
}

static void Resolves(void) {            // ( xt <name> -- )
    Tick();
    int orig = DataPop();
    uint32_t addr = DataPop();
    if (HEADER[ME].w2 != MAGIC_LATER) ERR = BAD_IS;
    int first = q->code[CORE][orig];
    if (first == VMI_PFX) {
        q->code[CORE][orig++] = VMI_PFX | ((addr >> VM_IMMBITS)&VM_IMMS_MASK);
        addr &= VM_IMM_MASK;
    }
    q->code[CORE][orig] = VMI_JUMP | addr;
    if (addr & ~VM_IMM_MASK) {
        ERR = BAD_IS;
        printf("'later' must be the far type. Use 'muchlater' instead.\n");
    }
}

/*
several ways to exit:
if notail = 0, convert call to jump
if there are no uops in the instruction using the return stack, set the ; bit
otherwise, start a new instruction with just the ; bit set.
*/

static void CompExit(void) {
    if (slot != SLOT0_POSITION) {
        if (UsesRetStack(instruction)) NewInst();
        goto ex;
    }
    if ((latest != CP) && (notail == 0)) {
        int a = (CP - 1) & (CODESIZE - 1);
        int old = q->code[CORE][a];     // previous instruction
        if ((old & ~VM_IMM_MASK) == VMI_CALL) {
            q->code[CORE][a] = (old ^ VMI_CALL) | VMI_JUMP;
            return;
        }
    }
ex: instruction |= VM_UOPS | VM_RET;
    slot = 0;
    NewInst();
}

static void Macro_Fn(void (*func)(uint32_t)) {
    uint32_t macro = my();
    int active = 0;
    for (int i = 25; i >= 0; i -= 5) {
        int uop = (macro >> i) & 0x1F;
        if ((!active) && (uop)) active = 1;
        if (active) {
            func(uop);
        }
    }
}

static void ExecUop(uint32_t uop) { InstExecute(IS_UOP(uop)); }
static void Macro_Comp(void) { Macro_Fn(CompUop); }
static void Macro_Exec(void) { Macro_Fn(ExecUop); }

static void AddMacro(char* name, char* help, uint32_t value) {
    if (AddHead(name, help)) {
        SetFns(value, Macro_Exec, Macro_Comp);
    }
}

// Compile Control Structures

static void ControlSwap(void) {
    int x = CtrlStack[ConSP];
    CtrlStack[ConSP] = CtrlStack[ConSP - 1];
    CtrlStack[ConSP - 1] = x;
}

static void sane(void) {
    if (ConSP)  ERR = BAD_CONTROL;
    ConSP = 0;
}

static void ResolveFwd(void) {
    NewInst();
    int addr = CtrlStack[ConSP--];
    q->code[CORE][addr] |= ((CP - addr) & VM_IMMS_MASK);
    latest = CP;
}

static void ResolveRev(int inst) {
    NewInst();
    int addr = CtrlStack[ConSP--];
    InstCompile(inst | ((addr - CP) & VM_IMMS_MASK));
    latest = CP;
}

static void MarkFwd    (void) { NewInst(); CtrlStack[++ConSP] = CP; }
static void doBegin    (void) { MarkFwd(); }
static void doAgain    (void) { ResolveRev(VMI_BRAN); }
static void doUntil    (void) { ResolveRev(VMI_ZBRAN); }
static void doMuntil   (void) { ResolveRev(VMI_PBRAN); }
static void doIf       (void) { MarkFwd();  InstCompile(VMI_ZBRAN); }
static void doMif      (void) { MarkFwd();  InstCompile(VMI_PBRAN); }
static void doThen     (void) { ResolveFwd(); }
static void doElse     (void) { MarkFwd();  InstCompile(VMI_BRAN);
                                ControlSwap();  ResolveFwd(); }
static void doWhile    (void) { doIf();  ControlSwap(); }
static void doMwhile   (void) { doMif();  ControlSwap(); }
static void doRepeat   (void) { doAgain();  doThen(); }
static void doFor      (void) { CompUop(VMO_PUSH);  MarkFwd(); }
static void doNext     (void) { ResolveRev(VMI_NEXT); }
static void douNext    (void) { ConSP--; CompUop(VMO_UNEXT);}
static void SemicoImm  (void) { EndDefinition();  sane(); }
static void SemiComp   (void) { CompExit();  SemicoImm();}
static void API_Comp   (void) { InstCompile(my()); }
static void Equ_Exec   (void) { DataPush(my()); }
static void Equ_Comp   (void) { ForthLiteral(my()); }
static void RegBY_Exec (void) { InstExecute(VMI_BY + my()); }
static void RegBY_Comp (void) { InstCompile(VMI_BY + my()); }
static void BaseY_Exec (void) { DataPush(my()); InstExecute(VMI_YSTORE); }
static void BaseY_Comp (void) { uint32_t ba = my();
    InstCompile(VMI_PY  | ((ba >> 8) & VM_IMMS_MASK)); }

void AddEquate(char* name, char* help, uint32_t value) {
    if (AddHead(name, help)) {
        SetFns(value, Equ_Exec, Equ_Comp);
    }
}
static void Constant(void) {
    char *name = Const(GetToken());
    AddEquate(name, "", DataPop());
}

static void AddRegBY(char* name, char* help, uint32_t value) {
    if (AddHead(name, help)) {
        SetFns(value, RegBY_Exec, RegBY_Comp);
    }
}
static void RegBY(void) {
    char *name = Const(GetToken());
    uint32_t byteoffset = DataPop();
    if (byteoffset & 3) ERR = IOR_NOT_CELL_ADDRESS;
    byteoffset >>= C_BYTESHIFT;
    if (byteoffset > VM_IMMS_MASK) ERR = IOR_OFFSET_TOO_BIG;
    AddRegBY(name, "", byteoffset);
}

static void AddBaseY(char* name, char* help, uint32_t value) {
    if (AddHead(name, help)) {
        SetFns(value, BaseY_Exec, BaseY_Comp);
    }
}
static void BaseY(void) {
    char *name = Const(GetToken());
    uint32_t ba = DataPop();
    ba = (ba >> C_BYTESHIFT) - 0x10000000;
    if (ba & 0x800000FF) ERR = IOR_BAD_BASEADDRESS;
    if (VM_INSTBITS < 31)
    if (ba >= (1 << (VM_INSTBITS + 1))) ERR = IOR_BAD_BASEADDRESS;
    AddBaseY(name, "", ba);
}

// char and [char] support utf-8:
// bytes  from       to          1st        2nd         3rd         4th
// 1	  U + 0000   U + 007F    0xxxxxxx
// 2	  U + 0080   U + 07FF    110xxxxx	10xxxxxx
// 3	  U + 0800   U + FFFF    1110xxxx	10xxxxxx	10xxxxxx
// 4	  U + 10000  U + 10FFFF  11110xxx	10xxxxxx	10xxxxxx	10xxxxxx

static int UFT8length;

static uint32_t getUTF8(char* p) {
    uint32_t c = *p++;             UFT8length = 1;
    if ((c & 0x80) == 0x00) return c;                       // 1-char UTF-8
    uint32_t d = *p++ & 0x3F;      UFT8length++;
    if ((c & 0xE0) == 0xC0) return ((c & 0x1F) << 6) | d;   // 2-char UTF-8
    d = (d << 6) | (*p++ & 0x3F);  UFT8length++;
    if ((c & 0xF0) == 0xE0) return ((c & 0x0F) << 12) | d;  // 3-char UTF-8
    d = (d << 6) | (*p++ & 0x3F);  UFT8length++;
    return ((c & 7) << 18) | d;                             // 4-char UTF-8
}

static void allot     (int n) {
    DP = n + DP;
    if (DP >= DataMemSize()) {
        printf("Out of data memory, increase DATASIZE.\n");
        ERR = BCI_IOR_INVALID_ADDRESS;
        return;
    }
}

static void Comma(void) {
    q->text[CORE][TP++] = DataPop();
    q->reloaded[CORE] = 0;
    if (TP >= TextMemSize()) {
        printf("Out of text memory, increase TEXTSIZE.\n");
        ERR = BCI_IOR_INVALID_ADDRESS;
        return;
    }
}

static void CommaStr(void) { /* ( string" -- a ) */
    tpFetch();
    uint32_t first = TP++;
    parseword('"');
    char* p = TOKEN;
    uint32_t c;
    int length = 0;
    while ((c = getUTF8(p))) {
        DataPush(c);
        Comma();
        length++;
        p += UFT8length;
        if (ERR) return;
    }
    q->text[CORE][first] = length;
}

static void buffer    (int n) { DataPush(DP);  Constant();  allot(n); }
static void Variable   (void) { buffer(1); }
static void Buffer     (void) { buffer(DataPop()); }
static void Char       (void) { char* p = GetToken(); DataPush(getUTF8(p)); }
static void BrackChar  (void) { char* p = GetToken(); CompLit(getUTF8(p)); }
static void Hex        (void) { BASE = 16; }
static void Decimal    (void) { BASE = 10; }
static void BitsCell   (void) { DataPush(BitsPerCell()); }
static void ComBitsCell(void) { CompLit (BitsPerCell()); }
static void MemTop     (void) { DataPush(DataMemSize()); }
static void ComTop     (void) { CompLit (DataMemSize()); }
static void Literal    (void) { CompLit (DataPop()); }
static void AddHelp    (void) { HEADER[HP].help = TIBtoEnd(); }
static void CompStr    (void) { CommaStr(); Literal(); }
static void byte2cell  (void) { DataPush(DataPop() >> C_BYTESHIFT); }
static void HostMinus  (void) { int32_t x = DataPop();  DataPush(DataPop() - x); }
static void HostDivide (void) { int32_t x = DataPop();  DataPush((int32_t)DataPop() / x); }
static void HostMults  (void) { int32_t x = DataPop();  DataPush((int32_t)DataPop() * x); }

static void AddAPIcall(char* name, char* help, uint32_t value) {
    if (AddHead(name, help)) {
        SetFns(value, Prim_Exec, API_Comp);
    }
}


static void AddUop(char* name, char* help, uint32_t value) {
    if (AddHead(name, help)) {
        SetFns(value, Prim_Exec, uOp_Comp);
    }
}

static void AddOp(char* name, char* help, uint32_t value) {
    if (AddHead(name, help)) {
        SetFns(value, Prim_Exec, Op_Comp);
    }
}

void AddForthKeywords(struct QuitStruct *state) {
    q = state;
#ifdef GUItype
#include "../LCD/gLCD.h"
    AddEquate("LCDwidth",       "-forth.htm#LCDwidth -- n",
            WinWidth);
    AddEquate("LCDheight",      "-forth.htm#LCDheight -- n",
            WinHeight);
#endif
    AddKeyword("saveblob",      "-forth.htm#saveblob n1 n2 -- n3",
            saveblob, noCompile);
    // basic operators
    AddKeyword("-",             "~core/Minus n1 n2 -- n3",
            HostMinus, noCompile);
    AddKeyword("/",             "~core/Times n1 n2 -- n3",
            HostDivide, noCompile);
    AddKeyword("*",             "~core/Div n1 n2 -- n3",
            HostMults, noCompile);
    // constants
    AddEquate("MAX-N",          "~usage#table:env -- n",
              (VM_MASK >> 1));
    AddEquate("MAX-U",          "~usage#table:env -- u",
              VM_MASK);
    AddEquate("|nvmpad|",       "-forth.htm#nvmpad -- u",
              NVMPADSIZE);
    AddEquate("*host",          "-forth.htm#host -- wid",
              q->host);
    AddEquate("verbose_color",  "-forth.htm#vcolor -- mask",
              VERBOSE_COLOR);
    AddEquate("verbose_comm",   "-forth.htm#vcomm -- mask",
              VERBOSE_COMM);
    AddEquate("verbose_bci",    "-forth.htm#vbci -- mask",
              VERBOSE_BCI);
    AddEquate("verbose_token",  "-forth.htm#vtoken -- mask",
              VERBOSE_TOKEN);
    AddEquate("verbose_src",    "-forth.htm#vsrc -- mask",
              VERBOSE_SRC);
    AddEquate("verbose_source", "-forth.htm#vsource -- mask",
              VERBOSE_SOURCE);
    AddEquate("verbose_cycles", "-forth.htm#vcycles -- mask",
              VERBOSE_CYCLES);
    AddEquate("verbose_fatal",  "-forth.htm#vfatal -- mask",
              VERBOSE_FATAL);
    AddKeyword("memtop",        "-forth.htm#memtop -- a",
               MemTop,          ComTop);
    AddKeyword("\\h",           "-forth.htm#help --",
               AddHelp,         AddHelp);
    AddKeyword("bits/cell",     "-forth.htm#bpc -- n",
               BitsCell,        ComBitsCell);
    AddKeyword("bytes>cells",   "-forth.htm#b2c  ba -- ca",
               byte2cell,       noCompile);
    AddKeyword("equ",           "-forth.htm#equ x <name> --",
               Constant,        noCompile);
    AddKeyword("register",      "-forth.htm#reg x <name> --",
               RegBY,           noCompile);
    AddKeyword("peripheral",    "-forth.htm#per x <name> --",
               BaseY,           noCompile);
    AddKeyword(".s",            "~tools/DotS wid --",
               dotESS,          noCompile);
    AddKeyword(".",             "~core/d n --",
               dot,             noCompile);
    AddKeyword("cp",            "-forth.htm#cp -- ca",
               cpFetch,         noCompile);
    AddKeyword("cp!",           "-forth.htm#cpstore ca --",
               cpStore,         noCompile);
    AddKeyword("dp",            "-forth.htm#dp -- a",
               dpFetch,         noCompile);
    AddKeyword("dp!",           "-forth.htm#dpstore a --",
               dpStore,         noCompile);
    AddKeyword("here",          "~core/HERE -- a",
               tpFetch,         noCompile);
    AddKeyword("tp",            "-forth.htm#tp -- a",
               tpFetch,         noCompile);
    AddKeyword("tp!",           "-forth.htm#tpstore a --",
               tpStore,         noCompile);
    AddKeyword(",\"",           "-forth.htm#comstr string\" -- a",
               CommaStr,        CompStr);
    AddKeyword(",",             "~core/Comma x --",
               Comma,           noCompile);
    AddKeyword("[",             "~core/Bracket --",
               toImmediate,     toImmediate);
    AddKeyword("]",             "~right-bracket --",
               toCompile,       toCompile);
    AddKeyword(":",             "~core/Colon <name> --",
               Definition,      noCompile);
    AddKeyword(":noname",       "~core/ColonNONAME -- xt",
               NoName,          noCompile);
    AddKeyword(";",             "~core/Semi --",
               SemicoImm,       SemiComp);
    AddKeyword("exit",          "~core/EXIT --",
               noExecute,       CompExit);
    AddKeyword("variable",      "~core/VARIABLE <name> --",
               Variable,        noCompile);
    AddKeyword("buffer:",       "~core/BUFFERColon n <name> --",
               Buffer,          noCompile);
    AddKeyword("[char]",        "~core/BracketCHAR \"<spaces>name\" --",
               noExecute,       BrackChar);
    AddKeyword("char",          "~core/CHAR \"<spaces>name\" -- char",
               Char,            noCompile);
    AddKeyword("decimal",       "~core/DECIMAL --",
               Decimal,         noCompile);
    AddKeyword("hex",           "~core/HEX --",
               Hex,             noCompile);
    AddKeyword("literal",       "~core/LITERAL x --",
               noExecute,       Literal);
    AddUop("over",              "~core/OVER x1 x2 -- x1 x2 x1",
           IS_UOP(VMO_OVER));
    AddUop("xor",               "~core/XOR x1 x2 -- x3",
           IS_UOP(VMO_XOR));
    AddUop("and",               "~core/AND x1 x2 -- x3",
           IS_UOP(VMO_AND));
    AddUop("dup",               "~core/DUP x -- x x",
           IS_UOP(VMO_DUP));
    AddUop("drop",              "~core/DROP x x -- x",
           IS_UOP(VMO_DROP));
    AddUop("swap",              "~core/SWAP x1 x2 -- x2 x1",
           IS_UOP(VMO_SWAP));
    AddUop("invert",            "~core/INVERT x -- ~x",
           IS_UOP(VMO_INV));
    AddUop("inv",               "-forth.htm#inv x -- ~x",
           IS_UOP(VMO_INV));
    AddUop("nop",               "-forth.htm#nop --",
           IS_UOP(VMO_NOP));
    AddUop("a",                 "-forth.htm#a -- a",
           IS_UOP(VMO_A));
    AddUop("a!",                "-forth.htm#astore a --",
           IS_UOP(VMO_ASTORE));
    AddUop("b!",                "-forth.htm#bstore a --",
           IS_UOP(VMO_BSTORE));
    AddUop("cy",                "-forth.htm#cy -- carry",
           IS_UOP(VMO_CY));
    AddUop("b",                 "-forth.htm#b -- x",
           IS_UOP(VMO_B));
    AddUop("+*",                "-forth.htm#mult ud1 -- ud2",
           IS_UOP(VMO_PLUSSTAR));
    AddUop("+",                 "~core/Plus n1 n2 -- n3",
           IS_UOP(VMO_PLUS));
    AddUop("2*",                "~core/TwoTimes x1 -- x2",
           IS_UOP(VMO_TWOSTAR));
    AddUop("2/",                "~core/TwoDiv x1 -- x2",
           IS_UOP(VMO_TWODIV));
    AddUop("2/c",               "-forth.htm#twodivc x1 -- x2",
           IS_UOP(VMO_TWODIVC));
    AddUop(">r",                "~core/toR x --",
           IS_UOP(VMO_PUSH));
    AddUop("r@",                "~core/RFetch -- x",
           IS_UOP(VMO_R));
    AddUop("r>",                "~core/Rfrom -- x",
           IS_UOP(VMO_POP));
    AddUop("@a",                "-forth.htm#fetcha -- x",
           IS_UOP(VMO_FETCHA));
    AddUop("@a+",               "-forth.htm#fetchaplus -- x",
           IS_UOP(VMO_FETCHAPLUS));
    AddUop("@b",                "-forth.htm#fetchb -- x",
           IS_UOP(VMO_FETCHB));
    AddUop("@b+",               "-forth.htm#fetchbplus -- x",
           IS_UOP(VMO_FETCHBPLUS));
    AddUop("!a",                "-forth.htm#storea x --",
           IS_UOP(VMO_STOREA));
    AddUop("!a+",               "-forth.htm#storeaplus x --",
           IS_UOP(VMO_STOREAPLUS));
    AddUop("!b",                "-forth.htm#storeb x --",
           IS_UOP(VMO_STOREB));
    AddUop("!b+",               "-forth.htm#storebplus x --",
           IS_UOP(VMO_STOREBPLUS));

    AddOp("bcisync",            "-forth.htm#bcisync --",
          INST_TAG + VMI_BCISYNC);
    AddOp("err!",               "-forth.htm#throw x --",
          INST_TAG + VMI_THROW);
    AddOp("y!",                 "-forth.htm#ystore x --",
          INST_TAG + VMI_YSTORE);
    AddOp("x!",                 "-forth.htm#xstore x --",
          INST_TAG + VMI_XSTORE);
    AddOp("y@",                 "-forth.htm#yfetch x --",
          INST_TAG + VMI_YFETCH);
    AddOp("x@",                 "-forth.htm#xfetch x --",
          INST_TAG + VMI_XFETCH);

    AddAPIcall("nvm@[",         "-forth.htm#nrdbegin addr -- ior",
            INST_TAG + VMI_API      + 0);
    AddAPIcall("nvm![",         "-forth.htm#nwrbegin addr -- ior",
            INST_TAG + VMI_API      + 1);
    AddAPIcall("nvm@",          "-forth.htm#nrd bytes -- u",
            INST_TAG + VMI_API      + 2);
    AddAPIcall("nvm!",          "-forth.htm#nwr u bytes --",
            INST_TAG + VMI_API2DROP + 3);
    AddAPIcall("]nvm",          "-forth.htm#nend --",
            INST_TAG + VMI_API      + 4);
    AddAPIcall("semit",         "-forth.htm#semit c --",
            INST_TAG + VMI_APIDROP  + 5);
    AddAPIcall("um*",           "~core/UMTimes u1 u2 -- d1",
            INST_TAG + VMI_API      + 6);
    AddAPIcall("um/mod",        "~core/UMDivMOD ud u -- q r",
            INST_TAG + VMI_APIDROP  + 7);
    AddAPIcall("mu/mod",        "-forth.htm#mumod ud u -- dq r",
            INST_TAG + VMI_API      + 7);
    AddAPIcall("LCDraw",        "-forth.htm#LCDraw n bits -- u",
            INST_TAG + VMI_APIDROP  + 8);
    AddAPIcall("LCDparm!",      "-forth.htm#LCDpsto x index --",
            INST_TAG + VMI_API2DROP + 9);
    AddAPIcall("LCDparm",       "-forth.htm#LCDp index -- x",
            INST_TAG + VMI_API      + 10);
    AddAPIcall("LCDemit",       "-forth.htm#LCDemit xchar --",
            INST_TAG + VMI_APIDROP  + 11);
    AddAPIcall("charwidth",     "-forth.htm#charwidth xchar -- width",
            INST_TAG + VMI_API      + 12);
    AddAPIcall("LCDfill",       "-forth.htm#LCDfill width height --",
            INST_TAG + VMI_API2DROP + 13);
    AddAPIcall("counter",       "-forth.htm#counter -- ms",
            INST_TAG + VMI_DUPAPI   + 14);
    AddAPIcall("buttons",       "-forth.htm#buttons -- buttons",
            INST_TAG + VMI_DUPAPI   + 15);
    AddAPIcall("crc32",         "-forth.htm#crc32 addr u -- crc32",
            INST_TAG + VMI_APIDROP  + 16);
    AddAPIcall("nvmID",         "-forth.htm#nID -- ID24",
            INST_TAG + VMI_DUPAPI   + 17);

    // compile-only control words, can't be postponed
    AddKeyword("later",         "-forth.htm#later <name> --",
               Later,           noCompile);
    AddKeyword("muchlater",     "-forth.htm#muchlater <name> --",
               MuchLater,       noCompile);
    AddKeyword("resolves",      "-forth.htm#resolves xt <name> --",
               Resolves,        noCompile);
    AddKeyword("begin",         "~core/BEGIN C: -- dest",
               noExecute,       doBegin);
    AddKeyword("again",         "~core/AGAIN C: dest --",
               noExecute,       doAgain);
    AddKeyword("until",         "~core/UNTIL C: dest -- | f --",
               noExecute,       doUntil);
    AddKeyword("-until",        "-forth.htm#muntil C: dest -- | n -- n",
               noExecute,       doMuntil);
    AddKeyword("if",            "~core/IF C: -- orig | f --",
               noExecute,       doIf);
    AddKeyword("-if",           "-forth.htm#mif C: -- orig | f -- f",
               noExecute,       doMif);
    AddKeyword("else",          "~core/ELSE C: orig1 -- orig2",
               noExecute,       doElse);
    AddKeyword("then",          "~core/THEN C: orig",
               noExecute,       doThen);
    AddKeyword("while",         "~core/WHILE C: dest -- orig dest | f -- f",
               noExecute,       doWhile);
    AddKeyword("-while",        "-forth.htm#mwhile C: dest -- orig dest | f -- f",
               noExecute,       doMwhile);
    AddKeyword("repeat",        "~core/REPEAT C: orig dest --",
               noExecute,       doRepeat);
    AddKeyword("for",           "-forth.htm#for C: -- dest | n --",
               noExecute,       doFor);
    AddKeyword("next",          "-forth.htm#next C: dest --",
               noExecute,       doNext);
    AddKeyword("unext",         "-forth.htm#unext C: dest --",
               noExecute,       douNext);
    AddMacro("@",               "~core/Fetch a -- x",
             (VMO_ASTORE << 5) | VMO_FETCHA);
    AddMacro("!",               "~core/Store x a --",
             (VMO_ASTORE << 5) | VMO_STOREA);
    AddMacro("nip",             "~core/NIP x1 x2 -- x2",
             (VMO_SWAP << 5) | VMO_DROP);
    AddMacro("tuck",            "~core/TUCK x1 x2 -- x2 x1 x2",
             (VMO_SWAP << 5) | VMO_OVER);
    AddMacro("2dup",            "~core/TwoDUP x1 x2 -- x1 x2 x1 x2",
             (VMO_OVER << 5) | VMO_OVER);
    AddMacro("2drop",           "~core/NIP x1 x2 --",
             (VMO_DROP << 5) | VMO_DROP);
    AddMacro("3drop",           "-forth.htm#threedrop x1 x2 --",
             (VMO_DROP << 10) | (VMO_DROP << 5) | VMO_DROP);
}
