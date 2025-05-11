/*
Target VM with BCI over RS232
Use with com0com null-modem emulator

Single-threaded, macroloop polls for incoming RS232 and steps the VM while waiting.
*/

// Use TRACE = 0 to avoid use of printf.
#define TRACE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/mole/src/mole.h"
#include "../../src/mole/src/testkeys.h"
#include "../../src/bci/bci.h"
#include "../../src/RS-232/rs232.h"

const uint8_t TargetBoilerSrc[] = {"\x13noyb<Remote__UUID>0"};

#if (TRACE)
#include <stdio.h>
#define PRINTF  if (TRACE) printf
#else
#define PRINTF(...) do { } while (0)
#endif

// 32-byte BCI encryption key, 32-byte BCI MAC key, 16-byte admin password,
// 32-byte file encryption key, 32-byte file MAC key, 16-byte hash
static const uint8_t default_keys[] = TESTKEY_1;

uint8_t my_keys[sizeof(default_keys)];

static vm_ctx vm_internal_state;
static vm_ctx *ctx = &vm_internal_state;
static port_ctx TargetPort;

/*
Write the key and return the address of the key (it may have changed)
Return NULL if key cannot be updated
*/

static uint8_t * UpdateKeySet(uint8_t* keyset) {
    memcpy(my_keys, keyset, MOLE_KEYSET_LENGTH);
	return my_keys;
}

static int getc_RNG(void) {
	return rand() & 0xFF;	// DO NOT USE in a real application
}                           // Use a TRNG instead

// should never happen
static void BoilerHandler(const uint8_t *src) {
    PRINTF("\nTarget received %d-byte boilerplate {%s}", src[0], &src[1]);
}


// VM

VMcell_t TextMem[TEXTSIZE];
VMinst_t CodeMem[CODESIZE];

static uint8_t  responseBuf[MaxBCIresponseSize];
static uint16_t responseLen;
// These functions are used by the BCI to return a response
static void mySendChar(int id, uint8_t c) {
    responseBuf[responseLen++] = c;
}
static void mySendInit(int id) {
    responseLen = 0;
    mySendChar(id, id & 0xFF);
    mySendChar(id, id >> 8);
}
static void mySendFinal(int id) {
    moleSend(&TargetPort, (const uint8_t*)&responseBuf, responseLen);
}

static void BCItransmit(const uint8_t *src, int length) {
    BCIhandler(ctx, &src[2], length);   // skip the CPUCORE id
}

static int port = DEFAULT_TARGETPORT;
static int baudrate = DEFAULT_BAUDRATE;
static uint8_t buffer[4];

static void uartCharOutput(uint8_t c) {
    PRINTF("t%02X ", c);
    if (c == 0x0A) PRINTF("\n");
    int r = RS232_SendByte(port, c);
    if (r) PRINTF("\n*** RS232_SendByte returned %d, ", r);
}

static char cmode[] = {'8','N','1',0};
static void ComList(void) { // list available COM ports
    PRINTF("Possible serial port numbers at %d,N,8,1 ", baudrate);
    for (int i = 0; i < 38; i++) {
        int ior = RS232_OpenComport(i, baudrate, cmode, 0);
        if (!ior) {
            RS232_CloseComport(i);
            PRINTF("%d ", i);
        }
    }
}

static int ComOpen(void) {
    int ior = RS232_OpenComport(port, baudrate, cmode, 0);
    if (ior) {
        PRINTF("\nError opening port %d\n", port);
        return 1;
    }
    PRINTF("\nPort %d opened at %d,N,8,1\n", port, baudrate);
    return 0;
}



int main(int argc, char* argv[]) {
    PRINTF("Remote target for 'ok'\n");
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    if (argc > 2) {
        baudrate = atoi(argv[2]);
    }
    ctx->TextMem = TextMem;         // flash sector for read-only data
    ctx->CodeMem = CodeMem;         // flash sector for code
    memset(TextMem, BLANK_FLASH_BYTE, sizeof(TextMem));
    memset(CodeMem, BLANK_FLASH_BYTE, sizeof(CodeMem));
    ctx->InitFn = mySendInit;           // output initialization function
    ctx->putcFn = mySendChar;           // output putc function
    ctx->FinalFn = mySendFinal;         // output finalization function
    // set up the mole ports
    memcpy(my_keys, default_keys, sizeof(my_keys));
    moleNoPorts();
    int ior = moleAddPort(&TargetPort, TargetBoilerSrc, MOLE_PROTOCOL, "TARGET", 17, getc_RNG,
                  BoilerHandler, BCItransmit, uartCharOutput, my_keys, UpdateKeySet);
    if (ior) {
        PRINTF("\nError %d, ", ior);
        PRINTF("MOLE_ALLOC_MEM_UINT32S too small by %d ", -moleRAMunused()/4);
        PRINTF("or the key has a bad HMAC");
        return 1;
    }
    ComList();
    ComOpen();
    BCIinitial(ctx);
    PRINTF("Raw UART traffic is dumped to console: rXX=receive, tXX=transmit\n");
    while  (ctx->status != BCI_STATUS_SHUTDOWN) {
        if (ctx->status == BCI_STATUS_RUNNING) {
            // int ior =
            BCIstepVM(ctx, 0);
        }
        int bytes = RS232_PollComport(port, buffer, 1);
        if (bytes) {
            PRINTF("r%02X ", buffer[0]);
            if (buffer[0] == 0x0A) PRINTF("\n");
            molePutc(&TargetPort, buffer[0]);
        }
    }
    RS232_CloseComport(port);
    return 0;
}
