//===============================================================================
// tools.h
//===============================================================================

#ifndef __TOOLS_H__
#define __TOOLS_H__
#include <stdint.h>
#include <stdio.h>

uint64_t GetMicroseconds(void);
void strmove(char* dest, char* src, unsigned int maxlen);
char* itos(uint64_t x, uint8_t radix, int8_t digits, uint8_t unsign, uint8_t cellbits);
void StrCat(char* dest, char* src, int limit);
FILE* fopenx(char* filename, char* fmt);
void ErrorMessage(int error, char* s);
void cdump(const uint8_t *src, uint16_t len);
uint32_t CRC32(uint8_t *addr, uint32_t len);

//#define MORESAFE // Linux does not like, but Visual Studio does. Code::Blocks does not care.

#endif // __TOOLS_H__

