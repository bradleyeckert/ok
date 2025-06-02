#include <iostream>
#include <process.h> 
#include <thread>
#include <windows.h>
#include <string.h>

using namespace std;

#include "../src/host/quit.h"
#include "../src/host/tools.h"
#include "../src/bci/bci.h"
#include "../src/host/comm.h"
#include "../src/RS-232/rs232.h"

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
}

void YieldThread(void) {
    SwitchToThread();
}
    
// allocate "flash memory" for the VM(s)

VMcell_t TextMem[CPUCORES][TEXTSIZE];
VMinst_t CodeMem[CPUCORES][CODESIZE];

static int g_begun;

void StopVMthread(vm_ctx* ctx) {
    ctx->status = BCI_STATUS_STOPPED;
    while (ctx->statusNew != BCI_STATUS_STOPPED) {
        YieldThread();
    }
}

/*
VM state accessed in main: CodeMem, TextMem, id, status, statusNew
*/
constexpr auto CYCLES = 5000;

#ifdef _MSC_VER
static DWORD WINAPI SimulateCPU(LPVOID threadid) {
    int id =  (int)(uintptr_t)threadid;
#else
static void* SimulateCPU(void* threadid) {
    int id = (size_t)threadid & 0xFFFF;
#endif
    vm_ctx* ctx = &quit_internal_state.VMlist[id].ctx;
    ctx->TextMem = &TextMem[id][0];     // flash sector for read-only data
    ctx->CodeMem = &CodeMem[id][0];     // flash sector for code
    memset(TextMem, BLANK_FLASH_BYTE, sizeof(TextMem));
    memset(CodeMem, BLANK_FLASH_BYTE, sizeof(CodeMem));
    VMreset(ctx);
    ctx->id = id;
    g_begun++;
    while (ctx->status != BCI_STATUS_SHUTDOWN) {
        if (ctx->status == BCI_STATUS_RUNNING) {
            int ior = VMsteps(ctx, CYCLES);
            if (ior) quit_internal_state.error = ior;
        }
        YieldThread();
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

#ifdef _MSC_VER
static DWORD WINAPI PollCommRX(LPVOID threadid) {
#else
static void* PollCommRX(void* threadid) {
#endif
    uint8_t buffer[64];
    struct QuitStruct* q = &quit_internal_state;
    g_begun++;
    while (CommDone == 0) { // 'bye' sets done
        if (q->portisopen) {
            uint8_t bytes = RS232_PollComport(q->port, buffer, 64);
            uint8_t *s = buffer;
            while (bytes--) {
                TargetCharOutput(*s++);
            }
            uSleep(100);    // not so rapid-fire polling
        }
        if (q->TxMsgSend) {
            q->TxMsgSend = 0;
            if (q->TxMsgLength == 0) printf("Unexpected empty message in PollCommRX\n");
            else EncryptAndSend(q->TxMsg, q->TxMsgLength);
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
    HANDLE tid[CPUCORES];
    memset(tid, 0, sizeof(tid));
    HANDLE commtask;
    for (int i = 0; i < CPUCORES; i++) {
        LPDWORD index = (LPDWORD)(size_t)i;
        tid[i] = CreateThread(NULL, 0, SimulateCPU, NULL, 0, index);
        if (tid[i] == NULL) return 1;
    }
    commtask = CreateThread(NULL, 0, PollCommRX, NULL, 0, NULL);
    if (commtask == NULL) return 1;
#else
    pthread_t tid[CPUCORES];
    pthread_t commtask;
    for (int i = 0; i < CPUCORES; i++) {
        if (pthread_create(&tid[i], NULL, SimulateCPU, (void*)(size_t)i)) return 1;
    }
    if (pthread_create(&commtask, NULL, PollCommRX, (void*)(size_t)0)) return 1;
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
    int ior = QuitLoop(linebuf, sizeof(linebuf), &quit_internal_state);
    for (int i = 0; i < CPUCORES; i++) { // tell VM threads to quit
        quit_internal_state.VMlist[i].ctx.status = BCI_STATUS_SHUTDOWN;
    }
    CommDone = 1;                        // tell RS232 thread to quit
#ifdef _MSC_VER
    for (int i = 0; i < CPUCORES; i++) { // wait for the threads to quit
        WaitForSingleObject(tid[i], INFINITE);
    }
    WaitForSingleObject(commtask, INFINITE);
#else
    for (int i = 0; i < CPUCORES; i++) { // wait for the threads to quit
        JoinThread(tid[i], NULL);
    }
    JoinThread(commtask, NULL);
#endif
    return ior;
}
