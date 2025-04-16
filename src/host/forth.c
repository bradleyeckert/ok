#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include "quit.h"
#include "forth.h"
#include "tools.h"
#include "../bci/bci.h"

struct QuitStruct *q;

#define DP        q->dp                 // data space pointer
#define CP        q->cp                 // code space pointer
#define NP        q->np                 // NVM space pointer
#define SP        q->sp                 // data stack pointer
#define BASE      q->base
#define STATE     q->state
#define ERROR     q->error              // detected error
#define HEADER    q->Header
#define ME        q->me
#define VERBOSE   q->verbose

void ForthLiteral(uint32_t x){
    printf("compiling literal %d\n", x);
}

void ForthComple(uint32_t xt){
    printf("compiling call to %d\n", xt);
}


SV dotESS (void) {                      // ( ... -- ... )
    PrintDataStack();
    printf("<-Top\n");
}

SV dot(void) {                          // ( n -- )
    DataDot(DataPop());
}

//##############################################################################
//##  Interface to VM(s)
//##  Host sends a thin-client command to the target with BCIsend.
//##  The BCI uses a callback to invoke BCIreceive.
//##  In C tradition, BCIwait hangs until BCIreceive has been called.
//##############################################################################

SV BCIsendLocal(const uint8_t *src, uint16_t length) {
    vm_ctx *ctx = &q->VMlist[q->core].ctx;
    q->BCIfn(ctx, src, length);
}

void (*BCIsend)(const uint8_t *src, uint16_t length) = BCIsendLocal;

uint8_t boiler[256];
uint8_t boilerLen;

static char TxMsg[MaxReceiveBytes];
static uint16_t TxMsgLength = 0; // 3 functions for message formation
static int busy;
SV SendChar(uint8_t c) {
    TxMsg[TxMsgLength++] = c;
}
SV SendInit(void) {
    TxMsgLength = 0;
}
SV SendFinal(void) {
    if (VERBOSE & VERBOSE_BCI) {
        cdump((const uint8_t*)TxMsg, TxMsgLength);
        printf("Sent to BCI");
    }
    busy = 1;
    BCIsend((const uint8_t*)TxMsg, TxMsgLength);
}
SV SendCell(uint32_t x) {
    SendChar(x >> 24);
    SendChar(x >> 16);
    SendChar(x >> 8);
    SendChar(x);
}
SV BCIwait(void) {
    while (busy) sched_yield();
}

SV extraChar(uint8_t c) {
    printf("0x%02x ", c);
}

static uint32_t BCIparam(const uint8_t *src, int length) {
    uint32_t x = 0;
    for (int i = 0; i < length; i++) {
        x = (x << 8) | *src++;
    }
    return x;
}

// BCIreceive is an asynchronous callback from any BCI thread.

void BCIreceive(int id, const uint8_t *src, uint16_t length) {
    if (VERBOSE & VERBOSE_BCI) {
        cdump((const uint8_t*)src, length);
        printf("Received from BCI node %d\n", id);
    }
    uint8_t c;
    uint32_t x = 0;
    while (length--) {
        c = *src++;
        if (c == BCI_BEGIN) {
            if (length--) {
                c = *src++;
                switch(c) {
                case BCIFN_EXECUTE:
                    while (length--) {
                        c = *src++;
                        if (c == BCI_BEGIN) {
                            c = *src++;     // cell count
                            SP = 0;
                            while ((length > 3) && (c--)) {
                                DataPush(BCIparam(src, 4));
                                src += 4;
                                length -= 4;
                            }
                            if (length != 7) goto error;
                            x = BCIparam(src, 4);
                            BASE = x & 0xFF;
                            STATE = x >> 8;
                            goto getior;
                        } else {
                            extraChar(c);
                        }
                    }
                case BCIFN_WRITE:
                    if (length != 2) goto error;
getior:             ERROR = (src[0] << 8) | src[1];
                    busy = 0;
                    return;
                case BCIFN_READ:
                    c = *src++;     // cell count
                    if (length != (c * 4 + 3)) goto error;
                    while (c--) {
                        DataPush(BCIparam(src, 4));
                        src += 4;
                    }
                    goto getior;
                case BCIFN_BOILER:
                    c = *src++;
                    memcpy(boiler, src, c);
                    boilerLen = c;
                }
                busy = 0;
                return; // ignore other...
            }
error:      ERROR = BAD_BCIMESSAGE;
            busy = 0;
            return;
        } else {
            extraChar(c);
        }
    }
}

SV Store(void) {
    SendInit();
    SendChar(BCIFN_WRITE);
    SendChar(1);  // 1 cell
    SendCell(DataPop());
    SendCell(DataPop());
    SendFinal();
    BCIwait();
}

SV Fetch(void) {
    SendInit();
    SendChar(BCIFN_READ);
    SendChar(1);  // 1 cell
    SendCell(DataPop());
    SendFinal();
    BCIwait();
}

SV dumpBoiler(void) {
    SendInit();
    SendChar(BCIFN_BOILER);
    SendFinal();
    BCIwait();
}

static uint32_t my (void) {
    return HEADER[ME].w;
}

SV Prim_Comp  (void) {
    printf("comp uop %x\n", my());
//    cmCode(my());
}
SV Prim_Exec  (void) {
    SendInit();
    SendChar(BCIFN_EXECUTE);
    SendCell((STATE << 8) + BASE);
    SendChar(SP);
    for (int i = 1; i <= SP; i++) SendCell(q->ds[i]);
    SendCell(my()); // xt
    SendFinal();
    BCIwait();
}


SV AddUop(char* name, char* help, cell value) {
    if (AddHead(name, help)) {
        SetFns(value, Prim_Exec, Prim_Comp);
    }
}

#define IS_UOP (VM_MASK & ~0xFF)

void AddForthKeywords(struct QuitStruct *state) {
    q = state;
    AddKeyword(".s", "~tools/DotS wid --",          dotESS,     noCompile);
    AddKeyword(".",  "~core/d n --",                dot,        noCompile);
    AddKeyword("@",  "~core/Fetch a -- x",          Fetch,      noCompile);
    AddKeyword("!",  "~core/Store x a --",          Store,      noCompile);
    AddKeyword("boiler",  " --",                    dumpBoiler, noCompile);
    AddUop("over",   "~core/OVER x1 x2 -- x1 x2 x1",IS_UOP + VMO_OVER);
    AddUop("xor",    "~core/XOR x1 x2 -- x3",       IS_UOP + VMO_XOR);
    AddUop("and",    "~core/AND x1 x2 -- x3",       IS_UOP + VMO_AND);
    AddUop("dup",    "~core/DUP x -- x x",          IS_UOP + VMO_DUP);
    AddUop("drop",   "~core/DROP x x -- x",         IS_UOP + VMO_DROP);
    AddUop("swap",   "~core/SWAP x1 x2 -- x2 x1",   IS_UOP + VMO_SWAP);
    AddUop("invert", "~core/INVERT x x -- x",       IS_UOP + VMO_INV);
    AddUop("nop",    " --",                         IS_UOP + VMO_NOP);
    AddUop("a!",     " a --",                       IS_UOP + VMO_ASTORE);
    AddUop("a",      " -- a",                       IS_UOP + VMO_A);
    AddUop("cy!",    " carry --",                   IS_UOP + VMO_CYSTORE);
    AddUop("cy",     " -- carry",                   IS_UOP + VMO_CY);
    AddUop("u!",     " u --",                       IS_UOP + VMO_USTORE);
    AddUop("u",      " -- u",                       IS_UOP + VMO_U);
    AddUop("+",      "~core/Plus n1 n2 -- n3",      IS_UOP + VMO_PLUS);
    AddUop("2*",     "~core/TwoTimes x1 -- x2",     IS_UOP + VMO_TWOSTAR);
    AddUop("2/",     "~core/TwoDiv x1 -- x2",       IS_UOP + VMO_TWODIV);
    AddUop("2/c",    " x1 -- x2",                   IS_UOP + VMO_TWODIVC);
    AddUop("unext",  " --",                         IS_UOP + VMO_UNEXT);
}
/*
#define VMO_FETCHA
#define VMO_FETCHAPLUS
#define VMO_FETCHB
#define VMO_FETCHBPLUS
#define VMO_STOREA
#define VMO_STOREAPLUS
#define VMO_STOREB
#define VMO_STOREBPLUS
#define VMO_PUSH
#define VMO_R
#define VMO_POP
*/
