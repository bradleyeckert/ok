/*
H1 simulator
*/

#include <stdio.h>
#include <stdlib.h>
#ifdef WIN32
#include <windows.h>
#include <processthreadsapi.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif  
#include "config.h"
#include "simcpu.h"
#include "tools.h"
#include "forth.h"

struct SimStateStruct* CPUinstance[CPUCORES];
struct SimStateStruct* g_core;

#ifdef WIN32
DWORD dwThreadIdArray[CPUCORES];
HANDLE  hThreadArray[CPUCORES];

static DWORD WINAPI StartThread(LPVOID lpParameter)
{
    RunSimThread(lpParameter);
    return 0;
}

static int LaunchThreads(void)
{
    for (int i = 0; i < CPUCORES; i++) {
        struct SimStateStruct* core = 
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(struct SimStateStruct));
        CPUinstance[i] = core;
        if (core == NULL) return 0;     // allocation failure
        core->corenum = i;
    }
    for (int i = 0; i < CPUCORES; i++) {
        hThreadArray[i] =
            CreateThread(NULL, 0, StartThread, CPUinstance[i], 0, &dwThreadIdArray[i]);
    }
    for (int i = 0; i < CPUCORES; i++) {
        while (CPUinstance[i]->simstate != simSTOPPED) { Sleep(1); }
    }
    g_core = CPUinstance[0];            // default to core 0
    return 1;
}

static void RetireThreads(void)
{
    for (int i = 0; i < CPUCORES; i++) {
        CPUinstance[i]->simstate = simDEAD;
        Sleep(2);
        CloseHandle(hThreadArray[i]);
        if (CPUinstance[i] != NULL)
            HeapFree(GetProcessHeap(), 0, CPUinstance[i]);
    }
}

#else
void* StartThread(void* vargp)
{
    RunSimThread(g_core);
    return 0;
}
#endif

static char linebuf[LineBufferSize];    // a line buffer for Forth

int main(int argc, char* argv[]) {
    int ior = 100;
    if (LaunchThreads()) {
        linebuf[0] = 0;
        // concatenate everything on the linebuf line to the line buffer
        for (int i = 1; i < argc; i++) {
            strkitty(linebuf, argv[i], LineBufferSize);
            if (i != (argc - 1))  strkitty(linebuf, " ", LineBufferSize);
        }
        ior = forth(linebuf, LineBufferSize);
    }
    RetireThreads();
    return ior;
}
