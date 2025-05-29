/*
Target VM with BCI over RS232
Use with com0com null-modem emulator or physical COM port

Single-thread C99, macroloop polls for incoming RS232 and steps the VM while waiting.
*/

#include <stdlib.h>
#include <string.h>
#include "../../src/mole/mole.h"
#include "../../src/mole/moleconfig.h"
#include "../../src/bci/bci.h"
#include "../../src/RS-232/rs232.h"

const uint8_t TargetBoilerSrc[] = {"\x13mole0<Remote__UUID>"};

#if (BCI_TRACE) // #define BCI_TRACE to display traffic
#include <stdio.h>
#define PRINTF  printf
#else
#define PRINTF(...) do { } while (0)
#endif

static int port = DEFAULT_TARGETPORT;
static int baudrate = DEFAULT_BAUDRATE;

//------------------------------------------------------------------------------
// Byte-oriented interface to RS232

static uint8_t rxBuffer[256];
static uint8_t head, tail;

static uint8_t UART_received(void) {
    uint8_t temp[256];
    if (head == tail) {
        uint8_t bytes = RS232_PollComport(port, temp, 256 - (head - tail));
        uint8_t *s = temp;
        while (bytes--) rxBuffer[head++] = *s++; // top up the buffer
    }
    return head - tail;
}

static uint8_t UART_getc(void) {
    if (UART_received()) return rxBuffer[tail++];
    return 0; // do not call on an empty buffer
}

static void UART_putc(uint8_t c) {
    int r = RS232_SendByte(port, c);
    if (r) PRINTF("\n*** RS232_SendByte returned %d, ", r);
}

//------------------------------------------------------------------------------
// mole support

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

int moleTRNG(void) {
	return rand() & 0xFF;	// DO NOT USE in a real application
}                           // Use a TRNG instead

// should never happen
static void BoilerHandler(const uint8_t *src) {
    PRINTF("\nTarget received %d-byte boilerplate {%s}", src[0], &src[1]);
}

//------------------------------------------------------------------------------
// VM

void StopVMthread(vm_ctx *ctx){} // no thread to stop

VMcell_t TextMem[TEXTSIZE];
VMinst_t CodeMem[CODESIZE];

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

static void uartCharOutput(uint8_t c) {
    PRINTF("\033[92m%02X\033[0m ", c);
    if (c == 0x0A) PRINTF("\n");
    UART_putc(c);
}

void get8debug(uint8_t c){
    PRINTF("\033[93m%02X\033[0m ", c);
}

int InitializeTarget(void) {
    ctx->TextMem = TextMem;         // flash sector for read-only data
    ctx->CodeMem = CodeMem;         // flash sector for code
    memset(TextMem, BLANK_FLASH_BYTE, sizeof(TextMem));
    memset(CodeMem, BLANK_FLASH_BYTE, sizeof(CodeMem));
    // set up the mole ports
    memcpy(my_keys, default_keys, sizeof(my_keys));
    moleNoPorts();
    int ior = moleAddPort(&TargetPort, TargetBoilerSrc, MOLE_PROTOCOL, "TARGET", 17,
                  BoilerHandler, BCItransmit, uartCharOutput, UpdateKeySet);
    if (ior) {
        PRINTF("\nError %d, ", ior);
        PRINTF("MOLE_ALLOC_MEM_UINT32S too small by %d ", -moleRAMunused()/4);
        PRINTF("or the key has a bad HMAC");
        return 1;
    }
//    PRINTF("MOLE_ALLOC_MEM_UINT32S too big by %d ", moleRAMunused()/4);
    moleNewKeys(&TargetPort, my_keys);
    VMreset(ctx);
    return 0;
}

void StepTarget(void) {
    if (ctx->status == BCI_STATUS_RUNNING) {
        VMsteps(ctx, 4096); // compensates for the overhead of RS232_PollComport
    }
    while (UART_received()) {
        uint8_t c = UART_getc();
        PRINTF("\033[95m%02X\033[0m ", c);
        if (c == 0x0A) PRINTF("\n");
        int ior = molePutc(&TargetPort, c);
        if (ior)  PRINTF("\nior=%d (see mole.h)\n", ior);
    }
}

//------------------------------------------------------------------------------
// main

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
    PRINTF("Raw UART traffic is dumped to console: \033[95mXX\033[0m=receive, ");
    PRINTF("\033[92mXX\033[0m=transmit, \033[93mXX\033[0m=BCIrx, \033[94mXX\033[0m=BCItx\n");
    if (InitializeTarget()) return 8;
    while (ctx->status != BCI_STATUS_SHUTDOWN) StepTarget();
    RS232_CloseComport(port);
    return 0;
}
