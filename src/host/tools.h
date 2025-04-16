//===============================================================================
// tools.h
//===============================================================================

#ifndef __TOOLS_H__
#define __TOOLS_H__
#include <stdint.h>

uint64_t GetMicroseconds(void);
void strmove(char* dest, char* src, size_t maxlen);
char* itos(uint64_t x, uint8_t radix, int8_t digits, uint8_t unsign, uint8_t cellbits);
void strkitty(char* dest, char* src, size_t maxlen);
FILE* fopenx(char* filename, char* fmt);
void ErrorMessage(int error, char* s);

#endif // __TOOLS_H__
