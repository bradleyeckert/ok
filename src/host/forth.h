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

#define IOR_NOT_CELL_ADDRESS  -90
#define IOR_OFFSET_TOO_BIG    -91
#define IOR_BAD_BASEADDRESS   -92

#endif // __FORTH_H__
