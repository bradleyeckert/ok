/*
Target VM with BCI over RS232
Use with com0com null-modem emulator

Single-threaded, macroloop polls for incoming RS232 and steps the VM while waiting.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/mole/src/mole.h"
//#include "../../src/host/tools.h"
#include "../../src/bci/bci.h"
#include "../../src/RS-232/rs232.h"

const uint8_t TargetBoilerSrc[] = {"\x13noyb<TargPortUUID>1"};


// 32-byte BCI encryption key, 32-byte BCI MAC key, 16-byte admin password,
// 32-byte file encryption key, 32-byte file MAC key, 16-byte hash
static const uint8_t default_keys[] = {
  0x02,0xD7,0xE8,0x39,0x2C,0x53,0xCB,0xC9,0x12,0x1E,0x33,0x74,0x9E,0x0C,0xF4,0xD5,
  0xD4,0x9F,0xD4,0xA4,0x59,0x7E,0x35,0xCF,0x32,0x22,0xF4,0xCC,0xCF,0xD3,0x90,0x2D,
  0x48,0xD3,0x8F,0x75,0xE6,0xD9,0x1D,0x2A,0xE5,0xC0,0xF7,0x2B,0x78,0x81,0x87,0x44,
  0x0E,0x5F,0x50,0x00,0xD4,0x61,0x8D,0xBE,0x7B,0x05,0x15,0x07,0x3B,0x33,0x82,0x1F,
  0x18,0x70,0x92,0xDA,0x64,0x54,0xCE,0xB1,0x85,0x3E,0x69,0x15,0xF8,0x46,0x6A,0x04,
  0x96,0x73,0x0E,0xD9,0x16,0x2F,0x67,0x68,0xD4,0xF7,0x4A,0x4A,0xD0,0x57,0x68,0x76,
  0xFA,0x16,0xBB,0x11,0xAD,0xAE,0x24,0x88,0x79,0xFE,0x52,0xDB,0x25,0x43,0xE5,0x3C,
  0xF4,0x45,0xD3,0xD8,0x28,0xCE,0x0B,0xF5,0xC5,0x60,0x59,0x3D,0x97,0x27,0x8A,0x59,
  0x76,0x2D,0xD0,0xC2,0xC9,0xCD,0x68,0xD4,0x49,0x6A,0x79,0x25,0x08,0x61,0x40,0x14,
  0x62,0x43,0x5D,0x6A,0xFB,0x96,0x3C,0xDD,0xD4,0x58,0x3D,0x3B,0x34,0x76,0xBF,0xF4};

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
    printf("\nTarget received %d-byte boilerplate {%s}", src[0], &src[1]);
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
    printf("t%02X ", c);
    if (c == 0x0A) printf("\n");
    int r = RS232_SendByte(port, c);
    if (r) printf("\n*** RS232_SendByte returned %d, ", r);
}

static char cmode[] = {'8','N','1',0};
static void ComList(void) { // list available COM ports
    printf("Possible serial port numbers at %d,N,8,1 ", baudrate);
    for (int i = 0; i < 38; i++) {
        int ior = RS232_OpenComport(i, baudrate, cmode, 0);
        if (!ior) {
            RS232_CloseComport(i);
            printf("%d ", i);
        }
    }
}

static int ComOpen(void) {
    int ior = RS232_OpenComport(port, baudrate, cmode, 0);
    if (ior) {
        printf("\nError opening port %d\n", port);
        return 1;
    }
    printf("\nPort %d opened at %d,N,8,1\n", port, baudrate);
    return 0;
}



int main(int argc, char* argv[]) {
    printf("Remote target for 'ok'\n");
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
        printf("\nError %d, ", ior);
        printf("MOLE_ALLOC_MEM_UINT32S too small by %d ", -moleRAMunused()/4);
        printf("or the key has a bad HMAC");
        return 1;
    }
    ComList();
    ComOpen();
    BCIinitial(ctx);
    printf("Raw UART traffic is dumped to console: rXX=receive, tXX=transmit\n");
    while  (ctx->status != BCI_STATUS_SHUTDOWN) {
        if (ctx->status == BCI_STATUS_RUNNING) {
            // int ior =
            BCIstepVM(ctx, 0);
        }
        int bytes = RS232_PollComport(port, buffer, 1);
        if (bytes) {
            printf("r%02X ", buffer[0]);
            if (buffer[0] == 0x0A) printf("\n");
            molePutc(&TargetPort, buffer[0]);
        }
    }
    RS232_CloseComport(port);
    return 0;
}
