#ifndef  __TFTSIM_H__
#define  __TFTSIM_H__
// Header file for TFTsim.c

// Target BMP = 24-bit W x H if even type, H x W if odd type
// format = bus width {8, 16, 9, 18, 8} and words/pixel: {2, 1, 2, 1, 3}
// Type: 0=portrait, 1=landscape, 2=portrait, 3=landscape
// width (W) must be a multiple of 4.
int TFTLCDsetup(uint8_t* BMP, int type, int width, int height);

// Send data to the controller.
//void TFTLCDdata(uint8_t format, uint32_t n);
//void TFTLCDcommand(uint8_t n);

uint32_t TFTLCDraw(uint32_t n, uint8_t mode);

#define PACKED16 0					// Pixel format over data bus
#define WHOLE16  1
#define PACKED18 2
#define WHOLE18  3
#define SERIAL8  4
#define WHOLE12  5

#define reverseRGB  /* define to reverse the RGB order for OpenGL */

// D/C: 1=data, 0=control
// CSn: 0 selects the controller, 1 = inactive
// ~RDn: 1=read

#define TFTsimDC  0x20
#define TFTsimCSn 0x40
#define TFTsimRD  0x80

#endif
