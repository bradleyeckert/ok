#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "threds.h"
#include "quit.h"
#include "tools.h"
#include "comm.h"
#include "../bci/bci.h"
#include "../bci/bciHW.h"
#include "../RS-232/rs232.h"
#include "../mole/src/mole.h"

static struct QuitStruct *q;

#define CORE      q->core
#define TP        (q->tp[CORE])         // text space pointer
#define DP        (q->dp[CORE])         // data space pointer
#define CP        (q->cp[CORE])         // code space pointer
#define NP        q->np                 // NVM space pointer
#define SP        q->sp                 // data stack pointer
#define BASE      q->base
#define STATE     q->state
#define VERBOSE   q->verbose

/*
Abstract: Interface to the VM selected by CORE, either internal or external.

 1. Host sends a thin-client command to the target with BCIsend.
 2. The BCI uses a callback to invoke BCIreceive.
 3. In C tradition, BCIwait hangs until BCIreceive has been called.

Cheat sheet for the sequence of functions hit by the BCI data flow:

 1. Prim_Exec is called to execute a word or instruction on the target
 2. InstExecute sends a thin-client query
 3. moleSend(HostPort) encrypts the query
 4. HostCharOutput sends ciphertext to the target a byte at a time
 5. molePutc(TargetPort) receives ciphertext from the target a byte at a time
 6. BCItransmit is invoked by molePutc to handle the plaintext query
 7. BCIhandler (the VM) interprets the query originally sent by InstExecute
 8. BCIsendToHost receives the plaintext response from BCIhandler
 9. moleSend(TargetPort) encrypts the response
10. TargetCharOutput sends ciphertext to the host a byte at a time
11. molePutc(HostPort) receives ciphertext from the host a byte at a time
12. BCIreceive is invoked by molePutc to handle the plaintext response
*/

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

port_ctx HostPort;
port_ctx TargetPort;

static void HostCharOutput(uint8_t c) {
    if (VERBOSE & VERBOSE_BCI) {
        printf(">%02X", c);
    }
    int r = molePutc(&TargetPort, c);
    if (r) printf("\n*** TargetPort returned %d, ", r);
}

void TargetCharOutput(uint8_t c) {
    if (VERBOSE & VERBOSE_BCI) {
        printf("<%02X", c);
    }
    int r = molePutc(&HostPort, c);
    if (r) printf("\n*** HostPort returned %d, ", r);
}

static uint8_t receivedBoilerplate[260];

static void BoilerHandlerB(const uint8_t *src) {
    printf("Target received %d-byte boilerplate {%s}\n", src[0], &src[1]);
    printf("This us unusual: Target requested the host's ID. ");
}

const uint8_t HostBoilerSrc[] =   {"\x13noyb<HostPortUUID>0"};
const uint8_t TargetBoilerSrc[] = {"\x13noyb<TargPortUUID>0"};

static void PairToTarget(void) {
    q->connected = 0;
    molePair(&HostPort);
    uint64_t end = GetMicroseconds() + (1000 * HANG_LIMIT_MS);
    while ((moleAvail(&HostPort) == 0)) {
        YieldThread();
        if (GetMicroseconds() > end) {
            if (q->connected == 0) {
                printf("BCI timeout while pairing\n");
                molePair(&TargetPort);
            }
            return;
        }
    }
    q->connected = (moleAvail(&HostPort) != 0);
}


// -----------------------------------------------------------------------------

static void BCItransmit(const uint8_t *src, int length) {
    uint16_t id;
    memcpy(&id, src, sizeof(uint16_t));
    src += sizeof(uint16_t);
    length -= sizeof(uint16_t);
    vm_ctx *ctx = &q->VMlist[id].ctx; // accessing the vm directly !!!
    BCIhandler(ctx, src, length);
}

static char TxMsg[MaxBCIresponseSize];
static uint16_t TxMsgLength = 0; // 3 functions for message formation
static int busy;

static void BoilerHandlerA(const uint8_t *src) {
    memcpy(receivedBoilerplate, src, src[0] + 1);
    busy = 0;
}
void SendChar(uint8_t c) {
    TxMsg[TxMsgLength++] = c;
}
void SendInit(void) {
    TxMsgLength = 0;
    SendChar((q->core) & 0xFF);
    SendChar((q->core) >> 8);
}
void SendFinal(void) {
    if (VERBOSE & VERBOSE_BCI) {
        cdump((const uint8_t*)TxMsg, TxMsgLength);
        printf("Sending to BCI through mole\n");
    }
    busy = 1;
    moleSend(&HostPort, (const uint8_t*) TxMsg, TxMsgLength);
}
void SendCell(uint32_t x) {
    int n = 4;
    while (n--) {
        SendChar(x & 0xFF);
        x >>= 8;
    }
}
void BCIwait(void) {
    uint64_t end = GetMicroseconds() + (1000 * HANG_LIMIT_MS);
    while (busy) {
        YieldThread();
        if (GetMicroseconds() > end) {
            if (q->connected == 0) {
                printf("BCI timeout, re-pairing\n");
                molePair(&TargetPort);
            }
            return;
        }
    }
}

static void VMstrobe(int pin) {
    SendInit();
    SendChar(BCIFN_STROBE);
    SendCell(pin);
    SendFinal();
    BCIwait();
}

static void VMreset(void) {
    VMstrobe(VM_RESET_PIN);
}

static void VMshutdown(void) {
    VMstrobe(VM_SHUTDOWN_PIN);
}

static void VMsleep(void) {
    VMstrobe(VM_SLEEP_PIN);
}

void BCIsendToHost(const uint8_t *src, int length) {
    moleSend(&TargetPort, src, length);
}

// BCIreceive is a callback from any BCI.

static void extraChar(uint8_t c) {
    printf("%c", c);
}

static uint32_t ReceivedCRC[16];

#define remoteCode (ReceivedCRC[0])
#define remoteText (ReceivedCRC[2])
#define flashNeeds (FLASH_BLOCK_SIZE + 5)

static void ProgramFlash(uint8_t *addr, int blocks, int command) {
    if (VERBOSE & VERBOSE_BCI) {
        printf("\nProgramming Flash[%p], %d blocks, command %d ", addr, blocks, command);
    }
    size_t addr0 = (size_t)addr;
    while (blocks--) {
        SendInit();
        SendChar(command);
        SendCell((size_t)addr - addr0);
        for (int i = 0; i < FLASH_BLOCK_SIZE; i++) {
            SendChar(*addr++);
        }
        SendFinal();
        BCIwait();
    }
}

void Reload(void) {
    int avail = moleAvail(&HostPort);
    if (avail < flashNeeds) {
        printf("\nTarget port accepts only %d bytes, %d bytes needed ", avail, flashNeeds);
        return;
    }
    VMsleep();                          // pause the VM while programming
    SendInit();
    SendChar(BCIFN_CRC);                // get the remote CRCs
    SendFinal();
    BCIwait();
    uint32_t localCode = CRC32((uint8_t*)&q->code[CORE][0], ReceivedCRC[1]);
    uint32_t localText = CRC32((uint8_t*)&q->text[CORE][0], ReceivedCRC[3]);
    if (VERBOSE & VERBOSE_BCI) {
        printf("\nReceived CRC32 data: ");
        for (int i=0; i<4; i++) {printf("%X ", ReceivedCRC[i]);}
        printf("\nlocal CRCs: %X %X ", localCode, localText);
    }
    int blocks;
    uint8_t *addr;
    if (localCode != remoteCode) {
        blocks = (CP * sizeof(VMinst_t) + FLASH_BLOCK_SIZE - 1) / FLASH_BLOCK_SIZE;
        addr = (uint8_t*)&q->code[CORE][0];
        ProgramFlash(addr, blocks, BCIFN_WRCODE);
    }
    if (localText != remoteText) {
        blocks = (TP * sizeof(VMcell_t) + FLASH_BLOCK_SIZE - 1) / FLASH_BLOCK_SIZE;
        addr = (uint8_t*)&q->text[CORE][0];
        ProgramFlash(addr, blocks, BCIFN_WRTEXT);
    }
    q->reloaded[CORE] = 1;
    VMreset();                          // reset the target because code changed
}

static void GetBoiler(void) {
    busy = 1;
    moleBoilerReq(&HostPort);           // get plaintext boilerplate, pairing not needed
    BCIwait();
    printf("Host received %d-byte boilerplate {%s}\n", receivedBoilerplate[0], &receivedBoilerplate[1]);
}

static uint32_t BCIparam(const uint8_t **src, int *length, int bytes) {
    uint32_t x = 0;
    uint8_t *s = (uint8_t *)*src;
    for (int i = 0; i < bytes; i++) {
        x |= (*s++ << (i * 8));
    }
    *src += bytes;
    *length -= bytes;
    return x;
}

// evaluate the response from the target BCI
static void BCIreceive(const uint8_t *src, int length) {
    uint16_t id;
    memcpy(&id, src, sizeof(uint16_t));
    src += sizeof(uint16_t);
    length -= sizeof(uint16_t);

    if (VERBOSE & VERBOSE_BCI) {
        cdump((const uint8_t*)src, length);
        printf("Received from BCI node %d\n", id);
    }
    uint8_t c;
    uint32_t x = 0;
    while (length--) {
        c = *src++;
        if (c == BCI_BEGIN) {
            if (length--) {
                c = *src++;
                switch(c) {
                case BCIFN_EXECUTE:
                    while (length--) {
                        c = *src++;
                        if (c == BCI_BEGIN) {
                            c = *src++;     // cell count
                            SP = 0;
                            while ((length > 3) && (c--)) {
                                DataPush(BCIparam(&src, &length, 4));
                            }
                            if (length != 15) goto error;
                            x = BCIparam(&src, &length, 4);
                            BASE = x & 0xFF;
                            if (BASE < 2) BASE = 2;
                            STATE = x >> 8;
                            x = BCIparam(&src, &length, 4);
                            q->cycles = ((uint64_t)BCIparam(&src, &length, 4) << 32) | x;
                            goto getior;
                        } else {
                            extraChar(c);
                        }
                    }
                    break;
                case BCIFN_WRITE:
                    if (length != 2) goto error;
getior:             q->error = (src[1] << 8) | src[0];
                    busy = 0;
                    return;
                case BCIFN_READ:
                    c = *src++;     // cell count
                    if (length != (c * 4 + 3)) goto error;
                    while (c--) {
                        DataPush(BCIparam(&src, &length, 4));
                    }
                    goto getior;
                case BCIFN_CRC:
                    c = *src++; // # of CRC32 results (not needed)
                    if (length > (16 * sizeof(uint32_t))) length = 16 * sizeof(uint32_t);
                    memcpy(ReceivedCRC, src, length);
                    break;
                }
                busy = 0;
                return; // ignore other...
            }
error:      q->error = BAD_BCIMESSAGE;
            busy = 0;
            return;
        } else {
            extraChar(c);
        }
    }
}

static void uartCharOutput(uint8_t c) {
    if (VERBOSE & VERBOSE_BCI) {
        printf(">%02X", c);
    }
    int r = RS232_SendByte(q->port, c);
    if (r) printf("\n*** RS232_SendByte returned %d, ", r);
}

static char cmode[] = {'8','N','1',0};
static void ComBaud(void) { q->baudrate = DataPop(); }
static void ComPort(void) { q->port = DataPop(); }

static void ComOpen(void) {
    int open = q->portisopen;
    if (open) return;
    q->portisopen = 0;
    int ior = RS232_OpenComport(q->port, q->baudrate, cmode, 0);
    if (ior) {
        printf("Error opening port %d ", q->port);
        return;
    }
    q->portisopen = 1;
    HostPort.ciphrFn = uartCharOutput;  // connect to UART output
    printf("Port %d opened at %d,N,8,1 ", q->port, q->baudrate);
}

static void ComClose(void) {
    int open = q->portisopen;
    q->portisopen = 0;
    if (!open) return;
    RS232_CloseComport(q->port);
    HostPort.ciphrFn = HostCharOutput;  // connect to local VM with null-modem
    printf("Port %d closed ", q->port);
}

static void ComEmit(void) {
    uint8_t c = DataPop();
    int err = RS232_SendByte(q->port, c);
    if (err) printf("~");
}

static void ComList(void) { // list available COM ports
    printf("Possible serial port numbers at %d,N,8,1: ", q->baudrate);
    for (int i = 0; i < 38; i++) {
        int ior = RS232_OpenComport(i, q->baudrate, cmode, 0);
        if (!ior) {
            RS232_CloseComport(i);
            printf("%d ", i);
        }
    }
}

static void ConnectPair(void) {
    PairToTarget();
    printf("Max transmit length: HostPort=%d, TargetPort=%d ", moleAvail(&HostPort), moleAvail(&TargetPort));
}

static void ConnectLocal(void) {
    ComClose();
    ConnectPair();
}

static void ConnectRemote(void) {
    ComOpen();
    if (q->portisopen == 0) {
        printf("Error: Could not open serial port\n");
        return;
    }
    printf("pairing to target\n");
    q->reloaded[CORE] = 0;
    ConnectPair();
}


void AddCommKeywords(struct QuitStruct *state) {
    q = state;
    q->baudrate = DEFAULT_BAUDRATE;
    q->port =     DEFAULT_HOSTPORT;
    AddKeyword("shutdown",  "-comm.htm#shutdn --",      VMshutdown,   noCompile);
    AddKeyword("reset",     "-comm.htm#reset --",       VMreset,      noCompile);
    AddKeyword("com-list",  "-comm.htm#list --",        ComList,      noCompile);
    AddKeyword("com-open",  "-comm.htm#open --",        ComOpen,      noCompile);
    AddKeyword("com-close", "-comm.htm#close --",       ComClose,     noCompile);
    AddKeyword("com-emit",  "-comm.htm#emit c --",      ComEmit,      noCompile);
    AddKeyword("baud!",     "-comm.htm#baud --",        ComBaud,      noCompile);
    AddKeyword("port!",     "-comm.htm#port --",        ComPort,      noCompile);
    AddKeyword("local",     "-comm.htm#local --",       ConnectLocal, noCompile);
    AddKeyword("remote",    "-comm.htm#remote port --", ConnectRemote,noCompile);
    AddKeyword("reload",    "-comm.htm#reload --",      Reload,       noCompile);
    AddKeyword("boiler",    "-comm.htm#boiler --",      GetBoiler,    noCompile);

    // set up the mole ports
    memcpy(my_keys, default_keys, sizeof(my_keys));
    moleNoPorts();
    int ior = moleAddPort(&HostPort, HostBoilerSrc, MOLE_PROTOCOL, "HOST", 100, getc_RNG,
                  BoilerHandlerA, BCIreceive, HostCharOutput, my_keys, UpdateKeySet);
    if (!ior) ior = moleAddPort(&TargetPort, TargetBoilerSrc, MOLE_PROTOCOL, "TARGET", 17, getc_RNG,
                  BoilerHandlerB, BCItransmit, TargetCharOutput, my_keys, UpdateKeySet);
    if (ior) {
        printf("\nError %d, ", ior);
        printf("MOLE_ALLOC_MEM_UINT32S too small by %d ", -moleRAMunused()/4);
        printf("or the key has a bad HMAC");
        return;
    }
    PairToTarget();
}
