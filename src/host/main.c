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

void YieldThread(void) {
    sched_yield();
}


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
}


// allocate "flash memory" for the VM(s)

VMcell_t TextMem[CPUCORES][TEXTSIZE];
VMinst_t CodeMem[CPUCORES][CODESIZE];

static int g_begun;

void StopVMthread(vm_ctx *ctx) {
    ctx->status = BCI_STATUS_STOPPED;
    while (ctx->statusNew != BCI_STATUS_STOPPED) {
#ifdef _MSC_VER
        SwitchToThread();
#else
        sched_yield();
#endif
    }
}

void* SimulateCPU(void* threadid) {
#ifdef _MSC_VER
    int* int_ptr = (int*)threadid;
    int id = *int_ptr;
#else
    int id = (size_t)threadid & 0xFFFF;
#endif
    printf("starting thread %d\n", id);
    vm_ctx *ctx = &quit_internal_state.VMlist[id].ctx;
    ctx->TextMem = &TextMem[id][0];     // flash sector for read-only data
    ctx->CodeMem = &CodeMem[id][0];     // flash sector for code
    memset(TextMem, BLANK_FLASH_BYTE, sizeof(TextMem));
    memset(CodeMem, BLANK_FLASH_BYTE, sizeof(CodeMem));
    BCIinitial(ctx);
    ctx->id = id;
    printID(id);
    g_begun++;
    while  (ctx->status != BCI_STATUS_SHUTDOWN) {
        if (ctx->status == BCI_STATUS_RUNNING) {
            // int ior =
            BCIstepVM(ctx, 0);
        }
#ifdef _MSC_VER
        SwitchToThread();
#else
        sched_yield();
#endif
        ctx->statusNew = ctx->status;
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
#ifdef _MSC_VER
        SwitchToThread();
#else
        sched_yield();
#endif
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
    HANDLE tid[CPUCORES];
    HANDLE commtask;
    for (int i = 0; i < CPUCORES; i++) {
        void* index = i;
        tid[i] = CreateThread(NULL, 0, SimulateCPU, NULL, 0, index);
    }
    commtask = CreateThread(NULL, 0, PollCommRX, NULL, 0, NULL);
#else
    pthread_t tid[CPUCORES];
    pthread_t commtask;
    for (int i = 0; i < CPUCORES; i++) {
        if (pthread_create(&tid[i], NULL, SimulateCPU, (void *)(size_t)i)) return 1;
    }
    if (pthread_create(&commtask, NULL, PollCommRX, (void *)(size_t)0)) return 1;
#endif
#ifdef _MSC_VER
        SwitchToThread();
#else
        sched_yield();
#endif
    linebuf[0] = 0;
    // concatenate all arguments to the line buffer
    for (int i = 1; i < argc; i++) {
        StrCat(linebuf, argv[i], sizeof(linebuf));
        if (i != (argc - 1))  StrCat(linebuf, " ", sizeof(linebuf));
    }
    while (g_begun != (CPUCORES + 1)) {  // wait for all tasks to start
#ifdef _MSC_VER
        SwitchToThread();
#else
        sched_yield();
#endif
    }
    int ior = quitloop(linebuf, sizeof(linebuf), &quit_internal_state);
    for (int i = 0; i < CPUCORES; i++) { // tell VM threads to quit
        quit_internal_state.VMlist[i].ctx.status = BCI_STATUS_SHUTDOWN;
    }
    CommDone = 1;                         // tell RS232 thread to quit
    for (int i = 0; i < CPUCORES; i++) { // wait for the threads to quit
        pthread_join(tid[i], NULL);
    }
    return ior;
}
