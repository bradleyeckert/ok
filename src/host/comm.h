//===============================================================================
// comm.h
//===============================================================================

#ifndef __COMM_H__
#define __COMM_H__
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void SendInit(void);                    // Begin building a message to send to the BCI
void SendChar(uint8_t c);               // Append a byte to the message
void SendCell(uint32_t x);              // Append a 32-bit word to the message
void SendFinal(void);                   // Send the message to the BCI
int BCIwait(const char *s, int pairable);// Wait for the BCI to return a message
void AddCommKeywords(struct QuitStruct *state);
void BCIsendToHost(const uint8_t *src, int length);
int EncryptAndSend(uint8_t* m, int length);
void TargetCharOutput(uint8_t c);       // target --> host chars (see main.c)
void Reload(void);                      // load host images onto target
void ComClose(void);

#ifdef __cplusplus
}
#endif
#endif // __COMM_H__
