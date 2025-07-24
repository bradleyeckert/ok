/*
Ok initialization

Start each simulated CPU core in its own thread
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "quit.h"
#include "tools.h"
#include "../bci/bci.h"
#include "comm.h"
#include "../RS-232/rs232.h"

static struct QuitStruct quit_internal_state;
static uint8_t  responseBuf[CPUCORES][MaxBCIresponseSize];
static uint16_t responseLen[CPUCORES];

// These functions are used by the BCI to return a response

void BCIsendChar(int id, uint8_t c) {
    responseBuf[id][responseLen[id]++] = c;
}

void BCIsendInit(int id) {
    responseLen[id] = 0;
    BCIsendChar(id, id & 0xFF);
    BCIsendChar(id, id >> 8);
}

void BCIsendFinal(int id) {
    BCIsendToHost((const uint8_t*)&responseBuf[id], responseLen[id]);
    responseLen[id] = 0;
}

void YieldThread(void) {
    sched_yield();
}

/*
VM state accessed in main: CodeMem, TextMem, id, status, statusNew
*/

// allocate "flash memory" for the VM(s)

static VMcell_t TextMem[CPUCORES][TEXTSIZE];
static VMinst_t CodeMem[CPUCORES][CODESIZE];

static int g_begun;

void StopVMthread(vm_ctx *ctx) {
    ctx->status = BCI_STATUS_STOPPED;
    while (ctx->statusNew != BCI_STATUS_STOPPED) {
        YieldThread();
    }
}

#define CYCLES 4096

void* SimulateCPU(void* threadid) {
    int id = (int)(size_t)threadid;
    vm_ctx *ctx = &quit_internal_state.VMlist[id].ctx;
    ctx->TextMem = &TextMem[id][0];  // flash sector for read-only data
    ctx->CodeMem = &CodeMem[id][0];  // flash sector for code
    memset(TextMem, BLANK_FLASH_BYTE, sizeof(TextMem));
    memset(CodeMem, BLANK_FLASH_BYTE, sizeof(CodeMem));
    VMreset(ctx);
    ctx->id = id;
    g_begun++;
    while  (ctx->status != BCI_STATUS_SHUTDOWN) {
        if (ctx->status == BCI_STATUS_RUNNING) {
            int ior = VMsteps(ctx, CYCLES);
            if (ior) quit_internal_state.error = ior;
        }
        YieldThread();
        ctx->statusNew = ctx->status;
    }
    pthread_exit(NULL);
    return (void*)0;
}

static int CommDone = 0;

void* PollCommRX(void* threadid) {
    uint8_t buffer[256];
    struct QuitStruct *q = &quit_internal_state;
    g_begun++;
    while (CommDone == 0) { // 'bye' sets done
        if (q->portisopen) {
            uint16_t bytes = RS232_PollComport(q->port, buffer, 256);
            uint8_t *s = buffer;
            while (bytes--) {
                TargetCharOutput(*s++);
            }
            uSleep(100);    // not so rapid-fire polling
        }
        if (q->TxMsgSend) {
            q->TxMsgSend = 0;
            if (q->TxMsgLength == 0) { // should never happen
                printf("Unexpected empty message in PollCommRX\n");
            }
            else EncryptAndSend(q->TxMsg, q->TxMsgLength);
        }
        YieldThread();
    }
    pthread_exit(NULL);
    return (void*)0;
}

static char linebuf[LineBufferSize];

int main(int argc, char* argv[]) {
    g_begun = 0;
    pthread_t tid[CPUCORES];
    pthread_t commtask;
    for (int i = 0; i < CPUCORES; i++) {
        if (pthread_create(&tid[i], NULL, SimulateCPU, (void *)(size_t)i)) {
            return 1;
        }
    }
    if (pthread_create(&commtask, NULL, PollCommRX, (void *)(size_t)0)) {
        return 1;
    }
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
    int ior = QuitLoop(linebuf, sizeof(linebuf), &quit_internal_state);
    for (int i = 0; i < CPUCORES; i++) { // tell VM threads to quit
        quit_internal_state.VMlist[i].ctx.status = BCI_STATUS_SHUTDOWN;
    }
    CommDone = 1;                         // tell RS232 thread to quit
    for (int i = 0; i < CPUCORES; i++) { // wait for the threads to quit
        pthread_join(tid[i], NULL);
    }
    return ior;
}
