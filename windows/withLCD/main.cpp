// 'ok' front end
// tested with Visual Studio 2022.
// not tested with Code::Blocks or MinGW, but is intended to work with them as well.

// GUItype is defined to enable the GUI.
// #define GUItype // define this globally in the project (see gui.c for usage)

#include <iostream>
#include <process.h> 
#include <thread>
#include <windows.h>
#include <string.h>

#define TRACE

using namespace std;

#include "../../src/host/quit.h"
#include "../../src/host/tools.h"
#include "../../src/bci/bci.h"
#include "../../src/host/comm.h"
#include "../../src/RS-232/rs232.h"

#ifdef GUItype
#include "gui.h"
#endif

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

/// Tasks

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

/*
GUIrun is blocking, so we run it in a separate thread.
*/
#ifdef GUItype
#ifdef _MSC_VER
static DWORD WINAPI LaunchGUI(LPVOID threadid) {
#else
static void* LaunchGUI(void* threadid) {
#endif
    GUIrun();
#ifdef _MSC_VER
    return 0;
#else
    pthread_exit(NULL);
    return (void*)0;
#endif
}
#endif

/// End of tasks

/// GUI callbacks

#ifdef GUItype
/*
The GUI thread calls this function to handle touch events.
The GT911 touch controller provides up to 5 touch points.
The points array contains the x and y coordinates of each touch point.
The first two elements are the x and y coordinates of the first touch point,
the next two elements are the x and y coordinates of the second touch point, and so on.
If there are no touch points, the array is empty (all zeros).

The VM uses 32-bit data, so we use one touch coordinate per cell.
The handler stores data to a mailbox in core 0, DataMem[1..6].
Cell 1: button and switch states, up to 32 buttons or switches.
Cell 2: x, y coordinates of the first touch point.
*/
void GUItouchHandler(uint8_t offset, uint8_t length, uint32_t *p) {
    vm_ctx* ctx = &quit_internal_state.VMlist[0].ctx;
    if (offset + length > 6) {
        printf("GUItouchHandler: offset %d + length %d exceeds 6\n", offset, length);
        return; // should never happen
	}
	memcpy(&ctx->DataMem[1], p, length * sizeof(uint32_t));
#ifdef TRACE
    if (offset == 0) {
        // handle button and switch states
        uint32_t buttons = *p++;
        for (int i = 0; i < 1; i++) {
            if (buttons & (1 << i)) {
                printf("Button %d pressed\n", i);
            } else {
                printf("Button %d released\n", i);
            }
        }
	}
    for (int i = 0; i < length; i++) {
        int x = *p++;
		if (x == 0) continue; // no touch point
		printf("Touch point %d: x=%d, y=%d\n", i, x & 0xFFFF, x >> 16);
	}
#endif
}
#endif

/// Main

static char linebuf[LineBufferSize];

int main(int argc, char* argv[]) {
    g_begun = 0;
#ifdef _MSC_VER
    HANDLE tid[CPUCORES] = { NULL };
    for (int i = 0; i < CPUCORES; i++) {
        LPDWORD index = (LPDWORD)(size_t)i;
        tid[i] = CreateThread(NULL, 0, SimulateCPU, NULL, 0, index);
        if (tid[i] == NULL) return 1;
    }
    HANDLE commtask = CreateThread(NULL, 0, PollCommRX, NULL, 0, NULL);
    if (commtask == NULL) return 2;
#ifdef GUItype
    HANDLE GUItask = CreateThread(NULL, 0, LaunchGUI, NULL, 0, NULL);
    if (GUItask == NULL) return 3;
#endif
#else
    pthread_t tid[CPUCORES];
    pthread_t commtask, GUItask;
    for (int i = 0; i < CPUCORES; i++) {
        if (pthread_create(&tid[i], NULL, SimulateCPU, (void*)(size_t)i)) return 1;
    }
    if (pthread_create(&commtask, NULL, PollCommRX, (void*)(size_t)0)) return 2;
#ifdef GUItype
    if (pthread_create(&GUItask, NULL, LaunchGUI, (void*)(size_t)0)) return 3;
#endif
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
#ifdef GUItype
    GUIbye();                            // tell GUI thread to quit
#endif
#ifdef _MSC_VER
    for (int i = 0; i < CPUCORES; i++) { // wait for the threads to quit
        WaitForSingleObject(tid[i], INFINITE);
    }
    WaitForSingleObject(commtask, INFINITE);
#ifdef GUItype
    WaitForSingleObject(GUItask, INFINITE);
#endif
#else
    for (int i = 0; i < CPUCORES; i++) { // wait for the threads to quit
        JoinThread(tid[i], NULL);
    }
    JoinThread(commtask, NULL);
#ifdef GUItype
    JoinThread(GUItask, NULL);
#endif
#endif
    return ior;
}
