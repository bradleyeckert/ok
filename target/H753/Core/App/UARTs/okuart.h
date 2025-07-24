/*
 * okuart.h
 *
 *  Created on: May 14, 2025
 *      Author: Bradley Eckert
 */

#ifndef INC_OKUART_H_
#define INC_OKUART_H_

#ifdef __cplusplus
 extern "C" {
#endif /* __cplusplus */

#ifdef STM32H753xx
#include "stm32h753xx.h"
#define READING_RDR_CLEARS_RXNE // this chip clears RXNE for you
#define WRITING_TDR_CLEARS_TXFNF
#else
#error "okuart.h needs to #include the right stm32*xx.h file"
#endif

typedef struct {
  USART_TypeDef *dev;
  uint8_t 		r_buffer[256],  t_buffer[256];
  uint8_t 		r_head, r_tail, t_head, t_tail, r_overflow;
} UART_t;

// example: UART_t myuart; UARTx_init(myuart, USART3);
void UARTx_init (UART_t *handle, USART_TypeDef *device);

// called by individual UART USRs
void UARTx_IRQHandler(UART_t *uart);

// output one byte
void UARTx_putc(UART_t *uart, uint8_t c);

// output an array of bytes
void UARTx_puts(UART_t *uart, const uint8_t *src, int length);

// headroom (free bytes remaining) in transmit buffer
int UARTx_headroom(UART_t *uart);

// number of bytes in the receive buffer
int UARTx_received(UART_t *uart);

// get next byte from the receive buffer, -1 if empty.
int UARTx_getc(UART_t *uart);

// look at the last received byte, -1 if empty.
int UARTx_peek(UART_t *uart);

// for backward compatibility with non-FIFO UARTs
#ifndef USART_ISR_RXNE_RXFNE
#define USART_ISR_RXNE_RXFNE USART_ISR_RXNE
#endif
#ifndef USART_ISR_TXE_TXFNF
#define USART_ISR_TXE_TXFNF USART_ISR_TXE
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* INC_OKUART_H_ */
