//===============================================================================
// tools.h
//===============================================================================

#ifndef __TOOLS_H__
#define __TOOLS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

uint64_t GetMicroseconds(void);
void strmove(char* dest, char* src, unsigned int maxlen);
char* itos(uint64_t x, uint8_t radix, int8_t digits, uint8_t unsign, uint8_t cellbits);
void StrCat(char* dest, const char* src, int limit);
FILE* fopenx(char* filename, char* fmt);
char * ErrorMessage(int error, char* s);
void cdump(const uint8_t *src, uint16_t len);
void uSleep(uint64_t usec);

#ifdef _MSC_VER
#define MORESAFE // Linux does not like, Visual Studio does. Code::Blocks does not care.
#endif

#ifdef __cplusplus
}
#endif

#endif // __TOOLS_H__

