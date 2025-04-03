/*
CPU simulator using one thread per CPU core

void RunSimThread(core) runs a loop that either simulates the CPU or sleeps.
The simulator gets one thread per simulated CPU core.

The main thread has a single host stack that is used in a way similar to how
SwiftX uses its XTL.

1. Pop data off the host stack and push it onto the simulator stack.
2. Copy the host's BASE to the equivalent simulator variable.
3. Simulate the code: Run, Single step, etc. Spin until it is finished.
4. Copy the simulator's BASE to the host's BASE.
5. Pop data off the simulator stack and push it onto the host stack.

Some functions that do this are:
void DataPush(x,core) pushes X to the data stack
cell DataPop(core) pops X from the data stack
cell DataPick(u, core) returns cell[u] of the data stack
int DataDepth(core) returns the depth of the data stack
*/

#ifdef _MSC_VER
#include <windows.h>
#include <processthreadsapi.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

#include "simcpu.h"
#include "simio.h"
#include "tools.h"

// The main thread has its own stacks:

static cell DataStack[256];
static cell ReturnStack[256];
static int8_t sp, rp;
uint8_t g_radix = 10;

void Cdot(cell x)  { printf("%s ", itos(x, g_radix, 0, 0)); }
void ClearStacks(void)   { sp = rp = 0; g_radix = 10; }
void DataPush(cell x)    { DataStack[++sp] = x; }
cell DataPop(void)       { return DataStack[sp--]; }
int8_t DataDepth(void)   { return sp; }
void ReturnPush(cell x)  { ReturnStack[++rp] = x; }
cell ReturnPop(void)     { return ReturnStack[rp--]; }
int8_t ReturnDepth(void) { return rp; }

void PrintDataStack(void) {
    switch (g_radix) {
    case 2:  printf("<bin> "); break;
    case 8:  printf("<oct> "); break;
    case 16: printf("<hex> "); break;
    case 10: printf("");       break;
    default: printf("<%d> ", g_radix);
    }
    for (int8_t i = 0; i < sp; i++) {
        Cdot(DataStack[i + 1]);
    }
}
void PrintReturnStack(void) {
    for (int8_t i = 0; i < rp; i++) {
        Cdot(ReturnStack[i + 1]);
    }
}

// sim_emit_fn (CPU -> host) usually sends characters to the terminal.
// Sometimes it sends characters to a target link receiver instead.

SV TermEmit(cell x) {
    printf("%c", (uint8_t)x);
}

extern CellFn sim_emit_fn = TermEmit;


// Note: The following are dangerous when linking system libraries
// so they are defined here rather in the header file.
// Their scope is from here until the end of this file.

#define CORENUM     core->corenum
#define simSTATE    core->simstate
#define REBOOT      core->reboot
#define INSN        core->insn
#define SIM_ERROR   core->error
#define CYCLES      core->cycles
#define PC          core->pc
#define T           core->t
#define U           core->u
#define R           core->r
#define N           core->n
#define A           core->a
#define W           core->w
#define C           core->c
#define LEX         core->lex
#define M           core->m
#define IO          core->io
#define DSP         core->dsp
#define RSP         core->rsp
#define DSPMAX      core->dspmax
#define RSPMAX      core->rspmax
#define BFB         core->bfb
#define REP         core->rep
#define DS (core->dstack[DSP & SPMASK])
#define RS (core->rstack[RSP & RPMASK])

/*
Main-thread interface to the current simulated core
*/

static int16_t SimWait(uint32_t ins) {
    cell temp;
    BASE = g_radix;
    struct SimStateStruct* core = g_core;
    DSP = RSP = 0;
    uint8_t next_state = ins >> 24;
    int8_t calling = 0;
    switch (next_state) {
    case simRUNNING:
        calling = -1;
        PC = ins & 0xFFFFFF;
    }
    for (int8_t i = 0; i < sp; i++) {   // move stacks from host to sim
        temp = DataStack[i + 1];
        DSP++;  DS = N;  N = T;  T = temp;
    }
    for (int8_t i = calling; i < rp; i++) {
        if (i < 0)
            temp = simBREAK;
        else
            temp = ReturnStack[i + 1];
        RSP++;  RS = R;  R = temp;
    }
    INSN = ins & 0xFFFF;
    simSTATE = next_state;
    while (g_core->simstate != simSTOPPED) {
        Sleep(0);
    }
    g_radix = (uint8_t)BASE;
    if (DSP < 0) DSP = 0;
    if (RSP < 0) RSP = 0;
    sp = DSP;
    rp = RSP;
    for (int8_t i = sp; i > 0; i--) {   // move stacks from sim to host
        DataStack[i] = T;
        T = N;  N = DS;  DSP--;
    }
    for (int8_t i = rp; i > 0; i--) {
        ReturnStack[i] = R;
        R = RS;  RSP--;
    }
    int16_t err = g_core->error;
    g_core->error = 0;
    return err;
}

int16_t SingleStep(void) {
    return SimWait(simONCE << 24);
}

int16_t SimInstruction(uint16_t ins) {
    return SimWait((simINSN << 24) | ins);
}

int16_t Simulate(cell xt) {
    return SimWait((simRUNNING << 24) | xt);
}

void Cold(void) {
    g_core->reboot = 1;
}


void ClearCycles(void) {
    struct SimStateStruct* core = g_core;
    CYCLES = DSP = RSP = DSPMAX = RSPMAX = 0;
}

SV ShowRegs(FILE* fp, uint16_t insn) { // this is a hack, remove later
    struct SimStateStruct* core = g_core;
    fprintf(fp, "PC=%04x,insn=%04x,DS=%06x,N=%06x,T=%06x,RS=%06x,R=%06x,sp=%02x,rp=%02x\n",
        PC, insn, DS, N, T, RS, R, DSP, RSP);
}

/*
The run states are:
  stopped
  run one instruction
  run until return stack underflow or error

  The global insn_trig triggers single instructions and notifies the
  main thread when it has been executed.
*/

void RunSimThread(struct SimStateStruct* core)
{
    simSTATE = simSTOPPED;
    uint8_t status;
    do {
        uint16_t instruction; // local variables are thread-safe
        int8_t ninc, rinc;
        uint8_t _c, _rep;
        cell temp, _pc, _lex, _t, _u, _r, _n, _a, _w;
        sum_t sum;
        status = simSTATE;
        switch (status) {
        case simSTOPPED:
            Sleep(1);
            break;
        case simONCE:
            goto fetch;
        case simINSN: _pc = 1; instruction = INSN;
            goto execute;
        case simRUNNING:
            if (REBOOT) {
                PC = BFB = DSP = RSP = REP = REBOOT = 0;
                T = U = R = N = A = W = C = 0;
            }
            else {
            fetch:
                instruction = core->code[PC];
                if (verbose & VERBOSE_TRACE)
                    ShowRegs(stdout, instruction);
                _pc = PC + 1;
                _rep = (REP) ? (uint8_t)(REP - 1) : 0;
            execute:
//              printf("exec %x:%x (%d,%d) DS=%x N=%x T=%x R=%x\n", PC, instruction, DSP, RSP, DS, N, T, R);
                _lex = ninc = rinc = 0;
                _t = T;  _n = N;  _r = R;  _w = W;
                if (instruction & 1) {
                    switch ((instruction >> 1) & 3) {
                    case INST(call):
                        rinc = 1;  _r = _pc;
                    case INST(jump):
                        _pc = (LEX << 13) | ((instruction & 070) << 7) | ((instruction >> 6) & 0x3FF); break;
                    case INST(literals):
                        switch (instruction & 0x3F) {
                        case ins_literal: ninc = 1;  _n = T;
                            _t = (LEX << 10) | (instruction >> 6) & 0x3FF;  break;
                        case ins_pfix:
                            _lex = (LEX << 10) | (instruction >> 6) & 0x3FF;  break;
                        case ins_regr:
                            switch ((instruction >> 6) & 7) {
                            case 0: _t = DSP;  break;
                            case 1: _t = RSP;  break;
                            case 2: _t = CORENUM;  break;
                            } break;
                        case ins_regw:
                            switch (((instruction >> 6) & 3)) {
                            case 2: BFB = T;  break;
                            } break;
                        }
                    case INST(branches):
                        switch (instruction & 0x3F) {
                        case bran_always:
                        branch: _pc = (PC & ~0x3FF) | ((instruction >> 6) & 0x3FF);  break;
                        case bran_pos: if (!(T & MSB)) { goto branch; } break;
                        case bran_0:  ninc = -1; _t = N; _n = DS;   if (T == 0) { goto branch; } break;
                        case bran_zero: if (T == 0) { ninc = -1; _t = N; _n = DS; goto branch; } break;
                        case bran_next: if ((R & 0xFF) != 0) { _r = (R - 1) & CELLMASK; goto branch; }
                                      else { rinc = -1;  _r = RS; } break;
                        case bran_nc: if (C == 0) { goto branch; } break;
                        }
                    }
                }
                else {
                    _c = C; _a = A; _u = U;
                    switch (OPCODE(instruction)) {
                    case OPCODE(NtoT1):
                    case OPCODE(NtoT):  _t = N;      break;
                    case OPCODE(AtoT):  _t = A;      break;
                    case OPCODE(RtoT):  _t = R;      break;
                    case OPCODE(COM):   _t = ~T;     break;
                    case OPCODE(EOR):   _t = T ^ N;  break;
                    case OPCODE(TAND):  _t = T & N;  break;
                    case OPCODE(ZEQ):   _t = (T) ? 0 : ALL_ONES;  break;
                  //  case OPCODE(INPUT): _t = IO;     break;
                    case OPCODE(SHL1):  _t = T << 1;
                        _c = (T >> (CELLBITS - 1)) & 1;  break;
                    case OPCODE(SHLU):  _t = (T << 1) + ((U >> (CELLBITS - 1)) & 1);
                        _c = (T >> (CELLBITS - 1)) & 1;  break;
                    case OPCODE(ROL):   _t = (T << 1) + ((T >> (CELLBITS - 1)) & 1);  break;
                    case OPCODE(DIV2):  _c = T & 1; temp = (T & MSB); _t = (T >> 1) | temp;  break;
                    case OPCODE(SHRC):  _c = T & 1;  _t = (T >> 1) | (C << (CELLBITS - 1));  break;
                    case OPCODE(ROR):   _c = T & 1;  _t = (T >> 1) | (_c << (CELLBITS - 1));  break;
                    case OPCODE(RRB):   _t = (T >> BYTEBITS) | ((T & BYTEMASK) << (CELLBITS - BYTEBITS));  break;
                    case OPCODE(ADD):
                        if (STROBE(instruction) == STROBE(NoStrobe ^ Umshr)) {
                            sum = (U & 1) ? (sum_t)T + (sum_t)N : (sum_t)T;
                            _t = (cell)(sum >> 1);
                        }
                        else {
                            sum = (sum_t)T + (sum_t)N;
                            _t = (cell)sum;
                            _c = (sum >> CELLBITS) & 1;
                        } break;
                    case OPCODE(MEM):   _t = M;  break;
                    case OPCODE(CARRY): _t = C;  break;
                    case OPCODE(WtoT):  _t = W;  break;
                    }
                    switch (DSTACK(instruction)) {
                    case DSTACK(Snone ^ Sdn): ninc = -1; _n = DS; break;
                    case DSTACK(Snone ^ NU):  _u = N;  break;
                    case DSTACK(Snone ^ Sup): ninc = 1; _n = T; break; // dup defaults to T->N
                    default:              break;
                    }
                    switch (STROBE(instruction)) {
                    case STROBE(NoStrobe ^ TtoN):   _n = T;  break;
                    case STROBE(NoStrobe ^ UtoN):   _n = U;  break;
                    case STROBE(NoStrobe ^ TtoA):   _a = T;  break;
                    case STROBE(NoStrobe ^ TtoR):   _r = T;  break;
                    case STROBE(NoStrobe ^ Umshr):  _c = U & 1;
                        sum = (U & 1) ? (sum_t)T + (sum_t)N : (sum_t)T;
                        _u = (U >> 1) | ((sum & 1) << (CELLBITS - 1));  break;
                    case STROBE(NoStrobe ^ Ushr):  _c = U & 1; // use with T2/ for "T:U / 2"
                        _u = (U >> 1) | ((T & 1) << (CELLBITS - 1));  break;
                    case STROBE(NoStrobe ^ Ushlc): _u = (U << 1) + C;  break; // use with T2*u for "T:U << 1 + C"
                    case STROBE(NoStrobe ^ MEMRP): _a = A + 1;
                    case STROBE(NoStrobe ^ MEMR):  M = core->data[A & (DataSize - 1)];  break;
                    case STROBE(NoStrobe ^ MEMWP): _a = A + 1;
                    case STROBE(NoStrobe ^ MEMW):  core->data[A & (DataSize - 1)] = T;  break;
                    case STROBE(NoStrobe ^ MEMWM): _a = A - 1;
                        core->data[A & (DataSize - 1)] = T;    break;
                    case STROBE(NoStrobe ^ IOW):   SimIOwrite(core, A, T);   break;
                    case STROBE(NoStrobe ^ IOR):   IO = SimIOread(core, A);  break;
                    case STROBE(NoStrobe ^ TtoREP): _rep = (uint8_t)(T - 1);  break;
                    case STROBE(NoStrobe ^ MASKtoN):
                        temp = (T >> CELLSIZE) & ~(ALL_ONES << CELLSIZE);
                        _n = ~(ALL_ONES << temp);
                        _a = (BFB << (2 * CELLSIZE + 2)) + ((T >> (2 * CELLSIZE)) << 2);  break;
                    case STROBE(NoStrobe ^ CLC):     _c = 0;  break;
                    default:              break;
                    }

                    C = _c;  U = _u & CELLMASK;  A = _a;
                    switch (RSTACK(instruction)) {
                    case RSTACK(Rnone ^ Rdn): rinc = -1; _r = RS;  break;
                    case RSTACK(Rnone ^ RET): rinc = -1; _r = RS; _pc = R;
                        if (_pc == simBREAK) {
                            simSTATE = simSTOP;
                        }
                        else if (_pc == 0) {
                            SIM_ERROR = BAD_ZEROPC;
                        }
                        break;
                    case RSTACK(Rnone ^ Rup): rinc = 1;  break;
                    default:              break;
                    }
                }
                RSP += rinc;  if (rinc == 1) RS = R;
                DSP += ninc;  if (ninc == 1) DS = N;
                T = _t & CELLMASK;  R = _r;  N = _n;  LEX = _lex;
#ifdef ErrorChecking
                if (DSP > DSPMAX) DSPMAX = DSP;
                if (RSP > RSPMAX) RSPMAX = RSP;
                if (RSP < 0) SIM_ERROR = BAD_RSTACKUNDER;
                if (DSP < 0) SIM_ERROR = BAD_STACKUNDER;
                if (RSP == RPMASK) SIM_ERROR = BAD_RSTACKOVER;
                if (DSP == SPMASK) SIM_ERROR = BAD_STACKOVER;
#endif
                if ((_pc & ~(CodeSize - 1)) && (simSTATE == simRUNNING)) {
                    SIM_ERROR = BAD_PC;
                }
                else if (!REP) {
                    PC = _pc;
                }
                REP = _rep;
                if (SIM_ERROR) {
                    simSTATE = simSTOP;
                }
            }
            // can get here by simRUNNING, fetch, or execute
            if (simSTATE != simRUNNING) {
                simSTATE = simSTOP;     // single step
            }
            CYCLES++;
            break; // simRUNNING
        case simSTOP:
        default: simSTATE = simSTOPPED; // bogus state
        }
        Sleep(0);
    } while (status != simDEAD);
}

