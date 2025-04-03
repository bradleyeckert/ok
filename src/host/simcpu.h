//===============================================================================
// simcpu.h
//===============================================================================
#ifndef __SIMCPU_H__
#define __SIMCPU_H__
#include "config.h"

void RunSimThread(struct SimStateStruct* core);

extern uint8_t g_radix;
extern int verbose;

void ClearCycles(void);
void PrintDataStack(void);
void PrintReturnStack(void);
void ClearStacks(void);
void DataPush(cell x);
cell DataPop(void);
int8_t DataDepth(void);
void ReturnPush(cell x);
cell ReturnPop(void);
int8_t ReturnDepth(void);
void Cdot(cell x);

int16_t SingleStep(void);
int16_t SimInstruction(uint16_t ins);
int16_t Simulate(cell xt);
void Cold(void);

extern struct SimStateStruct* g_core;

#define simBREAK 0xFFFF

// Assembler primitives for the ALU instruction
// Names are chosen to not conflict with Forth or C

// Instruction: [15:11] = STROBE, [10:6] = STACK, [5:1] = OPCODE

#define OPCODE(x) (((x) >> 1) & 0x1F)
#define TAND    (0x01 << 1)
#define INPUT   (0x02 << 1)
#define TMASK   (0x03 << 1)
#define NtoT    (0x04 << 1)
#define NshrT   (0x05 << 1)
#define AtoT    (0x06 << 1)
#define EOR     (0x07 << 1)
#define RRB     (0x08 << 1)
#define ADD     (0x09 << 1)
#define ZEQ     (0x0A << 1)
#define T_13    (0x0B << 1)
#define MEM     (0x0C << 1)
#define COM     (0x0D << 1)
#define DIV2    (0x0E << 1)
#define CARRY   (0x0F << 1)
#define SHL1    (0x10 << 1)
#define WtoT    (0x11 << 1)
#define NtoT1   (0x12 << 1)
#define RtoT    (0x13 << 1)
#define T_24    (0x14 << 1)
#define NshlT   (0x15 << 1)
#define SHLU    (0x18 << 1)
#define ROL     (0x19 << 1)
#define ROR     (0x1C << 1)
#define SHRC    (0x1E << 1)

// The default DS field is 1 (no stack change). XOR with the following to set.
#define DSTACK(x) (((x) >> 9) & 3)
#define Snone   (1 << 9)
#define Sup     ((0 << 9) ^ Snone)
#define Sdn     ((2 << 9) ^ Snone)
#define NU      ((3 << 9) ^ Snone)

// The default RS field is 6 (no stack change). XOR with the following to set.
#define RSTACK(x) (((x) >> 6) & 7)
#define Rnone   (3 << 6)
#define RET     ((4 << 6) ^ Rnone)
#define Rdn     ((5 << 6) ^ Rnone)
#define R_6     ((6 << 6) ^ Rnone)
#define Rup     ((7 << 6) ^ Rnone)
#define R_0     ((0 << 6) ^ Rnone)
#define R_1     ((1 << 6) ^ Rnone)
#define R_2     ((2 << 6) ^ Rnone)
#define R_3     ((3 << 6) ^ Rnone)

#define ISRET   ((inst & (7 << 6)) == (4 << 6))


#define STROBE(x) (((x) >> 11) & 0x1F)
#define NoStrobe  (0x04 << 11)

#define NS_0    ((0x00 << 11) ^ NoStrobe)
#define CLC     ((0x01 << 11) ^ NoStrobe)
#define NS_2    ((0x02 << 11) ^ NoStrobe)
#define Umshr   ((0x03 << 11) ^ NoStrobe)
#define NS_4    ((0x04 << 11) ^ NoStrobe)
// 0x05 unused
// 0x06 unused
#define NS_7    ((0x07 << 11) ^ NoStrobe)
#define IOR     ((0x08 << 11) ^ NoStrobe)
#define NS_11   ((0x09 << 11) ^ NoStrobe)
#define UtoN    ((0x0A << 11) ^ NoStrobe)
#define TtoN    ((0x0B << 11) ^ NoStrobe)
#define MEMW    ((0x0C << 11) ^ NoStrobe)
#define MEMWP   ((0x0D << 11) ^ NoStrobe)
#define IOW     ((0x0E << 11) ^ NoStrobe)
#define MEMWM   ((0x0F << 11) ^ NoStrobe)
#define TtoREP  ((0x10 << 11) ^ NoStrobe)
#define TtoA    ((0x11 << 11) ^ NoStrobe)
#define NS_22   ((0x12 << 11) ^ NoStrobe)
#define NS_23   ((0x13 << 11) ^ NoStrobe)
#define NS_24   ((0x14 << 11) ^ NoStrobe)
#define TtoR    ((0x15 << 11) ^ NoStrobe)
#define NS_26   ((0x16 << 11) ^ NoStrobe)
#define MASKtoN ((0x17 << 11) ^ NoStrobe)
#define NS_30   ((0x18 << 11) ^ NoStrobe)
#define MEMR    ((0x19 << 11) ^ NoStrobe)
// 0x1A unused
#define Ushr    ((0x1B << 11) ^ NoStrobe)
#define Ushlc   ((0x1C << 11) ^ NoStrobe)
#define MEMRP   ((0x1D << 11) ^ NoStrobe)

#define NOP     (NoStrobe | Snone | Rnone)

// Other instruction types

#define INST(x) ((x >> 1) & 3)
#define call     1
#define jump     3
#define literals 5
#define branches 7

#define ins_literal ((0 << 3) | literals)
#define ins_pfix    ((2 << 3) | literals)
#define ins_ror64   ((3 << 3) | literals)
#define ins_a       ((4 << 3) | literals)
#define ins_au      ((5 << 3) | literals)
#define ins_regr    ((6 << 3) | literals)
#define ins_regw    ((7 << 3) | literals)

#define bran_always ((0 << 3) | branches)
#define bran_pos    ((1 << 3) | branches)
#define bran_zero   ((2 << 3) | branches)
#define bran_0      ((3 << 3) | branches)
#define bran_next   ((6 << 3) | branches)
#define bran_nc     ((7 << 3) | branches)

#define reg_sp      ( 0 << 6 )
#define reg_rp      ( 1 << 6 )
#define reg_bfb     ( 2 << 6 )
#define reg_udm     ( 3 << 6 )
#define reg_dst     ( 4 << 6 )
#define reg_done    ( 5 << 6 )
#define reg_t       ( 6 << 6 )

static inline uint64_t ROR64(uint64_t x, int n) {
	return x >> n | x << (-n & 63);
}


#endif // __SIMCPU_H__
