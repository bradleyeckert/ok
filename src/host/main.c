/*
Ok initialization

Start each simulated CPU core in its own thread
*/

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "quit.h"

struct QuitStruct vm_internal_state;

#ifdef STANDALONE
int quitloop(char *line, int maxlength, struct QuitStruct *state) {
    printf("\n'quitloop' function not found in project\n*state=%p", state);
    printf("\n%d VMs use %d kB of RAM", CPUCORES, (unsigned)(sizeof(vm_internal_state)/1024));
    printf("\ntext = [%s] of %d", line, maxlength);
    return 2;
}
static void printID(int id) {printf("\nStarting VM %d", id);}
#else
static void printID(int id) {}
#endif

static uint8_t  responseBuf[CPUCORES][MaxReceiveBytes];
static uint16_t responseLen[CPUCORES];
// These functions are used by the BCI to return a response
SV mySendChar(int id, uint8_t c) {
    responseBuf[id][responseLen[id]++] = c;
}
SV mySendInit(int id) {
    responseLen[id] = 0;
}
SV mySendFinal(int id) {
    BCIreceive(id, (const uint8_t*)&responseBuf[id], responseLen[id]);
}

static int g_begun;

void* SimulateCPU(void* threadid) {
    int id = (size_t) threadid & 0xFFFF;
    vm_ctx *ctx = &vm_internal_state.VMlist[id].ctx;
    ctx->InitFn = mySendInit;           // output initialization function
    ctx->putcFn = mySendChar;           // output putc function
    ctx->FinalFn = mySendFinal;         // output finalization function
    BCIinitial(ctx);
    ctx->id = id;
    printID(id);
    g_begun++;
    while  (ctx->status != BCI_STATUS_SHUTDOWN) {
        if (ctx->status == BCI_STATUS_RUNNING) {
            // int ior =
            BCIstepVM(ctx, 0);
        }
        sched_yield();
    }
    pthread_exit(NULL);
    return (void*)0;
}

static char linebuf[LineBufferSize];

void StrCat(char* dest, char* src) {    // safe strcat
    unsigned int i = strlen(dest);
    while (i < sizeof(linebuf)) {
        char c = *src++;
        dest[i++] = c;
        if (c == 0) return;             // up to and including the terminator
    }
    dest[--i] = 0;                      // max reached, add terminator
}

int main(int argc, char* argv[]) {
    pthread_t tid[CPUCORES];
    g_begun = 0;
    for (int i = 0; i < CPUCORES; i++) {
        if (pthread_create(&tid[i], NULL, SimulateCPU, (void *)(size_t)i)) {
            return 1;
        }
    }
    sched_yield();
    linebuf[0] = 0;
    // concatenate all arguments to the line buffer
    for (int i = 1; i < argc; i++) {
        StrCat(linebuf, argv[i]);
        if (i != (argc - 1))  StrCat(linebuf, " ");
    }
    while (g_begun != CPUCORES) {       // wait for all tasks to start
        sched_yield();
    }
    int ior = quitloop(linebuf, sizeof(linebuf), &vm_internal_state);
    for (int i = 0; i < CPUCORES; i++) {
        vm_internal_state.VMlist[i].ctx.status = BCI_STATUS_SHUTDOWN;
    }
    for (int i = 0; i < CPUCORES; i++) {
        pthread_join(tid[i], NULL);
    }
    return ior;
}
