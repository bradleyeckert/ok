/*
Target VM with BCI over RS232
Use with com0com null-modem emulator or physical COM port

Single-thread C99, macroloop polls for incoming RS232 and steps the VM while waiting.
*/

#include <stdlib.h>
#include <string.h>
#include "../../src/mole/src/mole.h"
#include "../../src/mole/src/moleconfig.h"
#include "../../src/bci/bci.h"
#include "../../src/RS-232/rs232.h"

const uint8_t TargetBoilerSrc[] = {"\x13mole0<Remote__UUID>"};

#if (BCI_TRACE) // #define BCI_TRACE to display traffic
#include <stdio.h>
#define PRINTF  printf
#else
#define PRINTF(...) do { } while (0)
#endif

// 32-byte BCI encryption key, 32-byte BCI MAC key, 16-byte admin password,
// 32-byte file encryption key, 32-byte file MAC key, 16-byte hash
static const uint8_t default_keys[] = TESTPASS_1;

uint8_t my_keys[sizeof(default_keys)];

static vm_ctx vm_internal_state;
static vm_ctx *ctx = &vm_internal_state;
static port_ctx TargetPort;

/*
Write the key and return the address of the key (it may have changed)
Return NULL if key cannot be updated
*/

static uint8_t * UpdateKeySet(uint8_t* keyset) {
    memcpy(my_keys, keyset, MOLE_PASSCODE_LENGTH);
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

void StopVMthread(vm_ctx *ctx){} // no thread to stop

VMcell_t TextMem[TEXTSIZE];
VMinst_t CodeMem[CODESIZE];
#define RX_BUF_SIZE 256
static uint8_t rxBuffer[RX_BUF_SIZE];

static uint8_t  responseBuf[MaxBCIresponseSize];
static uint16_t responseLen;
// These functions are used by the BCI to return a response
void BCIsendChar(int id, uint8_t c) {
    PRINTF("\033[94m%02X\033[0m ", c);
    responseBuf[responseLen++] = c;
}
void BCIsendInit(int id) {
    responseLen = 0;
    BCIsendChar(id, id & 0xFF);
    BCIsendChar(id, id >> 8);
}
void BCIsendFinal(int id) {
    moleSend(&TargetPort, (const uint8_t*)&responseBuf, responseLen);
}

static void BCItransmit(const uint8_t *src, int length) {
    BCIhandler(ctx, &src[2], length);   // skip the CPUCORE id
}

static int port = DEFAULT_TARGETPORT;
static int baudrate = DEFAULT_BAUDRATE;

static void uartCharOutput(uint8_t c) {
    PRINTF("\033[92m%02X\033[0m ", c);
    if (c == 0x0A) PRINTF("\n");
    int r = RS232_SendByte(port, c);
    if (r) PRINTF("\n*** RS232_SendByte returned %d, ", r);
}

void get8debug(uint8_t c){
    PRINTF("\033[93m%02X\033[0m ", c);
}

static char cmode[] = {'8','N','1',0};
static void ComList(void) { // list available COM ports
    PRINTF("Possible serial port numbers at %d,N,8,1: ", baudrate);
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
    ComList();
    if ((port == 0) || (baudrate == 0) || ComOpen()) {
        PRINTF("\nCommand line parameters: <port#> <baudrate>\n");
        return 9;
    }
    ctx->TextMem = TextMem;         // flash sector for read-only data
    ctx->CodeMem = CodeMem;         // flash sector for code
    memset(TextMem, BLANK_FLASH_BYTE, sizeof(TextMem));
    memset(CodeMem, BLANK_FLASH_BYTE, sizeof(CodeMem));
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
    VMreset(ctx);
    PRINTF("Raw UART traffic is dumped to console: \033[95mXX\033[0m=receive, ");
    PRINTF("\033[92mXX\033[0m=transmit, \033[93mXX\033[0m=BCIrx, \033[94mXX\033[0m=BCItx\n");
    while  (ctx->status != BCI_STATUS_SHUTDOWN) {
        if (ctx->status == BCI_STATUS_RUNNING) {
            VMsteps(ctx, 4096); // compensates for the overhead of RS232_PollComport
        }
        uint8_t busy;
        do {
            uint8_t bytes = RS232_PollComport(port, rxBuffer, RX_BUF_SIZE);
            busy = bytes;
            uint8_t *s = rxBuffer;
            while (bytes--) {
                uint8_t c = *s++;
                PRINTF("\033[95m%02X\033[0m ", c);
                if (c == 0x0A) PRINTF("\n");
                int ior = molePutc(&TargetPort, c);
                if (ior)  PRINTF("\nior=%d (see mole.h)\n", ior);
            }
        } while (busy);
    }
    RS232_CloseComport(port);
    return 0;
}
