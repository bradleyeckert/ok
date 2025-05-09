//===============================================================================
// forth.h
//===============================================================================

#ifndef __FORTH_H__
#define __FORTH_H__
#include <stdint.h>

void ForthLiteral(uint32_t x);
void ForthComple(uint32_t xt);

void AddEquate(char* name, char* help, uint32_t value);
void AddForthKeywords(struct QuitStruct *state);

int BitsPerCell(void);
void ShowLine(void);

#endif // __FORTH_H__
