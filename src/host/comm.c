#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "quit.h"
#include "tools.h"
#include "comm.h"
#include "../bci/bci.h"
#include "../bci/bciHW.h"
#include "../RS-232/rs232.h"
#include "../mole/mole.h"
#include "../mole/moleconfig.h"
#include "../KMS/kms.h"

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
static const uint8_t default_keys[] = TESTPASS_1;
uint8_t TargetKey[sizeof(default_keys)];

/*
Write the key and return the address of the key (it may have changed)
Return NULL if key cannot be updated
*/

static uint8_t * UpdateKeySet(uint8_t* keyset) {
    memcpy(TargetKey, keyset, MOLE_PASSCODE_LENGTH);
    return TargetKey;
}
static uint8_t * UpdateNothing(uint8_t* keyset) {
    return NULL;
}

int moleTRNG(void) {
    return rand() & 0xFF;   // DO NOT USE in a real application
}                           // Use a TRNG instead

static port_ctx HostPort;
static port_ctx TargetPort;

static void HostCharOutput(uint8_t c) {
    if (VERBOSE & VERBOSE_BCI) {
        printf(">%02X", c);
    }
    int r = molePutc(&TargetPort, c);
    if ((r) && (r != 6)) printf("\n*** TargetPort returned %d, ", r);
}

void TargetCharOutput(uint8_t c) {
    if (VERBOSE & VERBOSE_BCI) {
        printf("<%02X", c);
    }
    int r = molePutc(&HostPort, c);
    if ((r) && (r != 6)) printf("\n*** HostPort returned %d, ", r);
}

static uint8_t receivedBoilerplate[260];

static void BoilerHandlerB(const uint8_t *src) {
    printf("Target received %d-byte boilerplate {%s}\n", src[0], &src[1]);
    printf("This us unusual: Target requested the host's ID. ");
}

const uint8_t HostBoilerSrc[] =   {"\x09mole0<ok>"};
const uint8_t TargetBoilerSrc[] = {"\x15mole0<TargetPortUUID>"};
static int busy;

int BCIwait(const char *s, int pairable) {
    uint64_t end = GetMicroseconds() + (1000 * HANG_LIMIT_MS);
    while (busy) {
        YieldThread();
        uint64_t t0 = GetMicroseconds();
        if (t0 > end) {
            if (pairable) {
                printf("Timeout in %s, re-pairing\n", s);
                molePair(&HostPort);
            }
            else {
                printf("Timeout in %s\n", s);
            }
            return 1;
        }
    }
    return 0;
}

// If you are using com0com null modem with nothing connected on the other end,
// molePair will hang because RS232_SendByte is hung.

uint8_t *HostKey;

static int PairToTarget(void) {
    q->connected = 0;
    busy = 1;
    moleBoilerReq(&HostPort);
    if (BCIwait("GetBoiler", 0)) goto bad;
    if (KMSlookup(receivedBoilerplate, &HostKey)) {
        printf("Keys not available\n\n");
        return KEY_LOOKUP_FAILURE;
    }
    moleNewKeys(&HostPort, HostKey);
    molePair(&HostPort);
    uint64_t end = GetMicroseconds() + (1000 * HANG_LIMIT_MS);
    while ((moleAvail(&HostPort) == 0)) {
        YieldThread();
        uint64_t t0 = GetMicroseconds();
        if (t0 > end) {
bad:        printf("Pairing failure\n\n");
            return MOLE_PAIRING_FAILURE;
        }
    }
    int ok = (moleAvail(&HostPort) != 0);
    q->connected = ok;
    return (!ok);
}

int EncryptAndSend(uint8_t* m, int length) {
    return moleSend(&HostPort, (const uint8_t*)m, length);
}


// -----------------------------------------------------------------------------

static void BCItransmit(const uint8_t *src, int length) { // message m from mole
    uint16_t id;
    memcpy(&id, src, sizeof(uint16_t));
    src += sizeof(uint16_t);
    length -= sizeof(uint16_t);
    vm_ctx *ctx = &q->VMlist[id].ctx; // accessing the vm directly !!!
    BCIhandler(ctx, src, length);
}

void get8debug(uint8_t c) {} // no debug output

static void BoilerHandlerA(const uint8_t *src) {
    memcpy(receivedBoilerplate, src, src[0] + 1);
    busy = 0;
}
void SendChar(uint8_t c) {
    q->TxMsg[q->TxMsgLength++] = c;
}
void SendInit(void) {
    q->TxMsgLength = 0;
    SendChar((q->core) & 0xFF);
    SendChar((q->core) >> 8);
}
void SendFinal(void) {
    if (VERBOSE & VERBOSE_BCI) {
        cdump((const uint8_t*)q->TxMsg, q->TxMsgLength);
        printf("Sending to BCI through mole\n");
    }
    busy = 1;
    q->TxMsgSend = 1;
}
void SendCell(uint32_t x) {
    int n = 4;
    while (n--) {
        SendChar(x & 0xFF);
        x >>= 8;
    }
}

static int VMstrobe(int pin) {
    SendInit();
    SendChar(BCIFN_STROBE);
    SendCell(pin);
    SendFinal();
    return BCIwait("VMstrobe", 1);
}

static void VMresetcmd(void) {
    VMstrobe(BCI_RESET_PIN);
}

static void VMshutdown(void) {
    VMstrobe(BCI_SHUTDOWN_PIN);
}

static void VMsleep(void) {
    VMstrobe(BCI_SLEEP_PIN);
}

void BCIsendToHost(const uint8_t *src, int length) {
    moleSend(&TargetPort, src, length);
}

// BCIreceive is a callback from any BCI.

static void extraChar(uint8_t c) {
    printf("%c", c);
}

static uint32_t ReceivedCRC[16];

#define flashNeeds (FLASH_BLOCK_SIZE + 5)

static int ProgramFlash(uint8_t *addr, int blocks, int command) {
    if (VERBOSE & VERBOSE_BCI) {
        printf("\nProgramming Flash[%p], %d blocks, command %d ", addr, blocks, command);
    }
    uint32_t addr0 = (uint32_t)(size_t)addr;
    while (blocks--) {
        SendInit();
        SendChar(command);
        SendCell((uint32_t)(size_t)addr - addr0);
        for (int i = 0; i < FLASH_BLOCK_SIZE; i++) {
            SendChar(*addr++);
        }
        SendFinal();
        if (BCIwait("ProgramFlash", 1)) return 1;
    }
    return 0;
}

void Reload(void) {
    q->reloaded[CORE] = 0;
    int avail = moleAvail(&HostPort);
    if (avail < flashNeeds) {
        return;
    }
    VMsleep();                          // pause the remote VM while programming
    SendInit();
    SendChar(BCIFN_CRC);                // get the remote CRCs
    SendChar(2);
    SendCell(CP * sizeof(VMinst_t));    // result will be in ReceivedCRC[0]
    SendCell(TP * sizeof(VMcell_t));    // result will be in ReceivedCRC[1]
    SendFinal();
    BCIwait("CRC", 1);
    uint32_t localCode = CRC32((uint8_t*)&q->code[CORE][0], CP * sizeof(VMinst_t));
    uint32_t localText = CRC32((uint8_t*)&q->text[CORE][0], TP * sizeof(VMcell_t));
    if (VERBOSE & VERBOSE_BCI) {
        printf("\nReceived CRC32 data: ");
        for (int i = 0; i < 2; i++) printf("%08X ", ReceivedCRC[i]);
        printf("\nlocal CRCs: %X %X ", localCode, localText);
    }
    int blocks;
    uint8_t *addr;
    int ior = 0;
    if (localCode != ReceivedCRC[0]) {
        blocks = (CP * sizeof(VMinst_t) + FLASH_BLOCK_SIZE - 1) / FLASH_BLOCK_SIZE;
        addr = (uint8_t*)&q->code[CORE][0];
        ior = ProgramFlash(addr, blocks, BCIFN_WRCODE);
    }
    if (localText != ReceivedCRC[1]) {
        blocks = (TP * sizeof(VMcell_t) + FLASH_BLOCK_SIZE - 1) / FLASH_BLOCK_SIZE;
        addr = (uint8_t*)&q->text[CORE][0];
        ior |= ProgramFlash(addr, blocks, BCIFN_WRTEXT);
    }
    if (ior) return;
    q->reloaded[CORE] = 1;
    VMresetcmd();                       // reset the target because code changed
}

static void GetBoiler(void) {           // mole port boilerplate
    busy = 1;
    moleBoilerReq(&HostPort);           // get plaintext boilerplate, pairing not needed
    BCIwait("GetBoiler", 0);
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
                case BCIFN_GET_CYCLES:
                            x = BCIparam(&src, &length, 4);
                            q->cycles = ((uint64_t)BCIparam(&src, &length, 4) << 32) | x;
                            goto getior;
                        } else {
                            extraChar(c);
                        }
                    }
                    goto getior;
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
                    busy=0;
                    break;
                default: break;
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
    HostPort.ciphrFn = uartCharOutput;  // connect to UART output
    printf("Port %d open ", q->port);
    printf("at %d,N,8,1 ", q->baudrate);// be obvious about blocked port
    q->portisopen = 1;
}

void ComClose(void) {
    int open = q->portisopen;
    q->portisopen = 0;                  // main.c will stop polling it
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

static int ConnectPair(void) {
    PairToTarget();
    int canTransmit = moleAvail(&HostPort);
    int canReceive = moleAvail(&TargetPort);
    if ((canTransmit < 1024) || (canReceive < 1024)) {
        printf("\nWarning: Target is not connected.\nHost can only transmit ");
        printf("%d, receive %d bytes\n", canTransmit, canReceive);
        return 1;
    }
    return 0;
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
    q->reloaded[CORE] = 0;
    if (ConnectPair()) {
        printf("Could not pair with remote, try again.\n");
        ComClose();
        return;
    }
    Reload();
}


void AddCommKeywords(struct QuitStruct *state) {
    q = state;
    q->baudrate = DEFAULT_BAUDRATE;
    q->port =     DEFAULT_HOSTPORT;
    atexit(ComClose); // in case of exit with ^C
    AddKeyword("shutdown",  "-comm.htm#shutdn --",      VMshutdown,   noCompile);
    AddKeyword("reset",     "-comm.htm#reset --",       VMresetcmd,   noCompile);
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
    AddKeyword("zzz",       "-comm.htm#zzz --",         VMsleep,      noCompile);

    // set up the mole ports
    memcpy(TargetKey, default_keys, sizeof(TargetKey));
    moleNoPorts();
    int ior = moleAddPort(&HostPort, HostBoilerSrc, MOLE_PROTOCOL, "HOST", 100,
                  BoilerHandlerA, BCIreceive, HostCharOutput, UpdateNothing);
    if (!ior) ior = moleAddPort(&TargetPort, TargetBoilerSrc, MOLE_PROTOCOL, "TARGET", 17,
                  BoilerHandlerB, BCItransmit, TargetCharOutput, UpdateKeySet);
    if (ior) {
        printf("\nError %d, ", ior);
        printf("MOLE_ALLOC_MEM_UINT32S too small by %d ", -moleRAMunused()/4);
        printf("or the key has a bad HMAC");
        return;
    }
    moleNewKeys(&TargetPort, TargetKey);
    ior = PairToTarget();
    if (ior) exit(ior);
}
