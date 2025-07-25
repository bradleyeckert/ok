#ifndef  __GUI_H__
#define  __GUI_H__
// Header file for gui.c
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
    
// external function to handle touch events.
extern void GUItouchHandler(uint8_t offset, uint8_t length, uint32_t* p);

// Launch and run the gui window.
void GUIrun(void);

// Load a test bitmap from a file "lcdimage.bmp".
void GUILCDload(char* s);

// LED status input is by calling a function:
void GUIbye(void);

#ifdef __cplusplus
}
#endif
#endif
