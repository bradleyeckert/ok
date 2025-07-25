/*
 * app.c
 *
 *  Created on: May 27, 2025
 *      Author: User
 */

#include <stdint.h>
#include "UARTs/okuart.h"

UART_t uart3;
void USART3_IRQHandler(void) { UARTx_IRQHandler(&uart3); }
void UART_putc(uint8_t c)  { UARTx_putc(&uart3, c); }
void UART_puts(const uint8_t *src, int length) { UARTx_puts(&uart3, src, length); }
uint8_t UART_getc(void)  { return UARTx_getc(&uart3); }
uint8_t UART_received(void)  { return UARTx_received(&uart3); }

/*
static void Message(const char *s) {
	char c;
	while ((c = *s++)) UART_putc(c);
	UART_putc('\r');
	UART_putc('\n');
}
*/

#include <stdlib.h>
#include <string.h>
#include "mole/mole.h"
#include "mole/moleconfig.h"
#include "bci/bci.h"

const uint8_t TargetBoilerSrc[] = {"\x13mole0<Remote__UUID>"};

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

// should never happen
static void BoilerHandler(const uint8_t *src) {}

//------------------------------------------------------------------------------
// VM

void StopVMthread(vm_ctx *ctx){} // no thread to stop

VMcell_t TextMem[TEXTSIZE];
VMinst_t CodeMem[CODESIZE];

static uint8_t  responseBuf[MaxBCIresponseSize];
static uint16_t responseLen;
// These functions are used by the BCI to return a response
void BCIsendChar(int id, uint8_t c) {
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
    ctx->admin = TargetPort.adminOK;
    BCIhandler(ctx, &src[2], length);   // skip the CPUCORE id
}

static void uartCharOutput(uint8_t c) {
    UART_putc(c);
}

void get8debug(uint8_t c){
}

int TargetInit(void) {
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
        return 1;
    }
    moleNewKeys(&TargetPort, my_keys);
    VMreset(ctx);
    return 0;
}

void ApplicationStep(void) {
    if (ctx->status == BCI_STATUS_RUNNING) {
        VMsteps(ctx, 512); // ~100 usec, swamp the cache miss penalty
    }
    while (UART_received()) {
        uint8_t c = UART_getc();
        molePutc(&TargetPort, c);
    }
}

void ApplicationInit(void) {
	UARTx_init(&uart3, USART3);
	NVIC_EnableIRQ(USART3_IRQn); // since you didn't set it in the NVIC in MX
	TargetInit();
}

/*
 * DO NOT USE 'rand' in a real application. Use a TRNG instead.
 * Also, verify that the TRNG is working.
 */
int moleTRNG(uint8_t *dest, int length) {
	while (length--) *dest++ = rand() & 0xFF;
	return 0;
}

