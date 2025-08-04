#ifndef __GLCD_H__
#define __GLCD_H__
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Screen dimensions in pixels
#define WinWidth  240
#define WinHeight 320

void LCDsetParm(int index, uint32_t x);
uint32_t LCDgetParm(int index);
void LCDchar(unsigned int xchar);
int LCDcharWidth(unsigned int xchar);
void LCDinit(void);

/*
* Underlying functions:
* TFTLCDraw(data, mode) is the interface to the LCD module.
* NVMbeginRead(address) sets the start address for reading bitmap data
* NVMread(bytes) reads the next big-endian value
*
* Top-level functions:
* LCDsetParm(index, data) sets a parameter for rendering to the LCD
* LCDgetParm(index) gets a parameter
* LCDchar(xchar) renders the bitmap glyph for xchar
*/

extern uint32_t TFTLCDraw(uint32_t n, uint8_t mode);
extern int NVMbeginRead (uint32_t faddr);
extern uint32_t NVMread (int bytes);
extern void NVMendRW (void);

// mode bits for TFTLCDraw, bits 0-4 are SPI beats (1 to 32)
// mode bit 5 is D/C (1=data, 0=command), set before SPI transfer
// mode bit 6 is CSn (0=active), set before SPI transfer
// mode bit 7 is RD (1=active), set before SPI transfer

#define TFTsimDC  0x20
#define TFTsimCSn 0x40
#define TFTsimRD  0x80

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GLCD_H__ */
