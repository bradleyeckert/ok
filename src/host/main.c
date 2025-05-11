/*
Ok initialization

Start each simulated CPU core in its own thread
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "threds.h"
#include "quit.h"
#include "tools.h"
#include "../bci/bci.h"
#include "comm.h"
#include "../RS-232/rs232.h"

static struct QuitStruct quit_internal_state;

#ifdef STANDALONE
int quitloop(char *line, int maxlength, struct QuitStruct *state) {
    printf("\n'quitloop' function not found in project\n*state=%p", state);
    printf("\n%d VMs use %d kB of RAM", CPUCORES, (unsigned)(sizeof(quit_internal_state)/1024));
    printf("\ntext = [%s] of %d", line, maxlength);
    return 2;
}
static void printID(int id) {printf("\nStarting VM %d", id);}
#else
static void printID(int id) {}
#endif

static uint8_t  responseBuf[CPUCORES][MaxBCIresponseSize];
static uint16_t responseLen[CPUCORES];
// These functions are used by the BCI to return a response
static void mySendChar(int id, uint8_t c) {
    responseBuf[id][responseLen[id]++] = c;
}
static void mySendInit(int id) {
    responseLen[id] = 0;
    mySendChar(id, id & 0xFF);
    mySendChar(id, id >> 8);
}
static void mySendFinal(int id) {
    BCIsendToHost((const uint8_t*)&responseBuf[id], responseLen[id]);
}

// allocate "flash memory" for the VM(s)
VMcell_t TextMem[CPUCORES][TEXTSIZE];
VMinst_t CodeMem[CPUCORES][CODESIZE];

static int g_begun;

void* SimulateCPU(void* threadid) {
    int id = (size_t) threadid & 0xFFFF;
    vm_ctx *ctx = &quit_internal_state.VMlist[id].ctx;
    ctx->TextMem = &TextMem[id][0];     // flash sector for read-only data
    ctx->CodeMem = &CodeMem[id][0];     // flash sector for code
    memset(TextMem, BLANK_FLASH_BYTE, sizeof(TextMem));
    memset(CodeMem, BLANK_FLASH_BYTE, sizeof(CodeMem));
    ctx->InitFn = mySendInit;           // output initialization function
    ctx->putcFn = mySendChar;           // output putc function
    ctx->FinalFn = mySendFinal;         // output finalization function
    BCIinitial(ctx);
    ctx->id = id;
    printID(id);
    g_begun++;
    uint64_t t0 = GetMicroseconds() + 1000000;
    uint64_t c0 = ctx->cycles;
    while  (ctx->status != BCI_STATUS_SHUTDOWN) {
        if (ctx->status == BCI_STATUS_RUNNING) {
            // int ior =
            BCIstepVM(ctx, 0);
            uint64_t t = GetMicroseconds();
            if (t > t0) {
                t0 += 1000000;
                uint64_t c1 = ctx->cycles;
                float mips = (c1 - c0) / 1e6;
                printf ("\n%f mips", mips);
                c0 = c1;
            }
        }
        YieldThread();
    }
#ifdef _MSC_VER
    return 0;
#else
    pthread_exit(NULL);
    return (void*)0;
#endif
}

static int CommDone = 0;

void* PollCommRX(void* threadid) {
    uint8_t buffer[4];
    struct QuitStruct *q = &quit_internal_state;
    g_begun++;
    while (CommDone == 0) { // 'bye' sets done
        if (q->portisopen) {
            int bytes = RS232_PollComport(q->port, buffer, 1);
            if (bytes) {
                TargetCharOutput(buffer[0]);
            }
        }
        YieldThread();
    }
#ifdef _MSC_VER
    return 0;
#else
    pthread_exit(NULL);
    return (void*)0;
#endif
}

static char linebuf[LineBufferSize];

int main(int argc, char* argv[]) {
    g_begun = 0;
#ifdef _MSC_VER
    thrd_t tid[CPUCORES];
    thrd_t commtask;
    for (int i = 0; i < CPUCORES; i++) {
        if (thrd_create(&tid[i], SimulateCPU, (void *)(size_t)i) return 1;
    }
    if (thrd_create(&commtask, PollCommRX, NULL)) return 1;
#else
    pthread_t tid[CPUCORES];
    pthread_t commtask;
    for (int i = 0; i < CPUCORES; i++) {
        if (pthread_create(&tid[i], NULL, SimulateCPU, (void *)(size_t)i)) return 1;
    }
    if (pthread_create(&commtask, NULL, PollCommRX, (void *)(size_t)0)) return 1;
#endif
    YieldThread();
    linebuf[0] = 0;
    // concatenate all arguments to the line buffer
    for (int i = 1; i < argc; i++) {
        StrCat(linebuf, argv[i], sizeof(linebuf));
        if (i != (argc - 1))  StrCat(linebuf, " ", sizeof(linebuf));
    }
    while (g_begun != (CPUCORES + 1)) {  // wait for all tasks to start
        YieldThread();
    }
    int ior = quitloop(linebuf, sizeof(linebuf), &quit_internal_state);
    for (int i = 0; i < CPUCORES; i++) { // tell VM threads to quit
        quit_internal_state.VMlist[i].ctx.status = BCI_STATUS_SHUTDOWN;
    }
    CommDone = 1;                         // tell RS232 thread to quit
    for (int i = 0; i < CPUCORES; i++) { // wait for the threads to quit
        JoinThread(tid[i], NULL);
    }
    return ior;
}
