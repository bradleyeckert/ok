//===============================================================================
// forth.h
//===============================================================================

#ifndef __FORTH_H__
#define __FORTH_H__
#include <stdint.h>

void ForthLiteral(uint32_t x);
void ForthComple(uint32_t xt);

void AddForthKeywords(struct QuitStruct *state);

#endif // __FORTH_H__
