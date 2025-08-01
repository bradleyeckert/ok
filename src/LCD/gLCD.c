/*
Decompress font glyphs to the LCD module
*/

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "gLCD.h"

static void LCDbeginMem(void) {
    TFTLCDraw(0x2C, 7);
}

static void LCDendMem(void) {
    TFTLCDraw(0, 0 + TFTsimCSn);
}

static void LCDcmd2(uint8_t cmd, uint16_t x0, uint16_t x1) {
    TFTLCDraw(cmd, 7);
    TFTLCDraw(x0 >> 8, 7 + TFTsimDC);
    TFTLCDraw(x0 & 0xFF, 7 + TFTsimDC);
    TFTLCDraw(x1 >> 8, 7 + TFTsimDC);
    TFTLCDraw(x1 & 0xFF, 7 + TFTsimDC + TFTsimCSn);
}

static void LCDsetrows(uint16_t x0, uint16_t x1) {
    LCDcmd2(0x2B, x0, x1); // Row Address Set
}

static void LCDsetcols(uint16_t x0, uint16_t x1) {
    LCDcmd2(0x2A, x0, x1); // Column Address Set
}


// The foreground and background colors for the LCD
//                     R  G  B
uint8_t BGcolor[4] = { 0, 0, 0, 0 };
uint8_t FGcolor[4] = { 255, 255, 255, 0 };

// The character position on the LCD (top, left) and the kerning
int16_t charX;
int16_t charY;
int8_t kerning = 1;
uint8_t fontID;

uint32_t LCDgetParm(int index) {
	uint32_t result = 0;
    switch (index) {
    case 0: memcpy(&result, &FGcolor, 3);
        break;
    case 1: memcpy(&result, &BGcolor, 3);
        break;
    case 2: result = charX;
        break;
    case 3: result = charY;
        break;
    case 4: result = kerning;
        break;
    case 5: result = fontID;
        break;
    default:
        break;
    }
    return result;
}

void LCDsetParm(int index, uint32_t x) {
    switch (index) {
    case 0: memcpy(&FGcolor, &x, 3);
        return;
    case 1: memcpy(&BGcolor, &x, 3);
        return;
    case 2: charX = x;
        return;
    case 3: charY = x;
        return;
    case 4: kerning = x;
        return;
    case 5: fontID = x;
        return;
    default:
        return;
    }
}


// ST7789VW controller datasheet
// 8.8.41 Write data for 12-bit/pixel (RGB 4-4-4-bit input), 4K-Colors, 3Ah=”03h”
// MSB to LSB = RGB

static void sendShade(int shade) {
    uint8_t w0 = (~shade & 0x0F);
    uint8_t w1 = shade & 0x0F;
    w0 = w0 | (w0 << 4);
    w1 = w1 | (w1 << 4);
    uint16_t red =   (BGcolor[0] * w0 + FGcolor[0] * w1) >> 12;
    uint16_t green = (BGcolor[1] * w0 + FGcolor[1] * w1) >> 12;
    uint16_t blue =  (BGcolor[2] * w0 + FGcolor[2] * w1) >> 12;
    uint16_t color = (red << 8) + (green << 4) + blue;
    TFTLCDraw(color, 11 + TFTsimDC);
}

static void sendShades(int shade, int length) {
    for (int i = 0; i < length; i++) sendShade(shade);
}

static uint16_t monopel(uint16_t pattern) {
    sendShade(15 * (pattern & 1));
    return pattern >> 1;
}

static void monopels(int pattern, int length) {
    for (int i = 0; i < length; i++)
        pattern = monopel(pattern);
}

static void BGrun(int length) { sendShades(0, length); }
static void FGrun(int length) { sendShades(15, length); }

static void pelcommand(uint16_t cmd) {
    if (cmd & 0x8000) {                 // format: 1xxxxxxx_xxxxxxxx
        monopels(cmd, 15);              // 15 x 1bpp
        return;
    }
    if ((cmd & 0xE000) == 0x2000) {     // format: 001ccccx_xxxxxxxx
        monopels(cmd, 15 & (cmd >> 9));
        return;
    }
    if ((cmd & 0xF000) == 0x4000) {     // format: 0100aaaa_aabbbbbb
        BGrun((cmd >> 6) & 0x3F);
        FGrun(cmd & 0x3F);
        return;
    }
    if ((cmd & 0xF000) == 0x6000) {     // format: 0110gggg_cccccccc
        sendShade(cmd >> 8);
        BGrun(cmd & 0xFF);
        return;
    }
    if ((cmd & 0xF000) == 0x7000) {     // format: 0111gggg_cccccccc
        sendShade(cmd >> 8);
        FGrun(cmd & 0xFF);
        return;
    }
                                        // 000xxxxx_xxxxxxxx is reserved
}

uint8_t charwidth;
uint8_t charheight;
uint8_t chartop;
uint8_t charleft;

static void decompress(void) {
    if (charwidth == 0) return;         // invalid width
    int x0 = charX + charleft;
    int y0 = charY + chartop;
    LCDsetcols(x0, x0 + charwidth - 1);
    LCDsetrows(y0, y0 + charheight - 1);
    uint16_t n = NVMread(2);            // number of 16-bit words
	LCDbeginMem();				        // begin memory write
    while (n--) pelcommand(NVMread(2));
	LCDendMem();                        // end memory write
}

static int fontaddr(unsigned int xchar) {
    NVMbeginRead(0);                    // -> font blob
    uint8_t c = NVMread(1);
    if (fontID > c) return 0;           // font does not exist
    NVMread(3 + 6 * fontID);            // skip 3-byte rev # and font links
    uint32_t font = NVMread(3);
    uint32_t maxchar = NVMread(3);
    if (xchar > maxchar) return 0;      // overrange xchar
    int fine = (xchar >> 6) * 2 + font;
    NVMbeginRead(fine);                 // -> fine table
    font += NVMread(2);
    NVMbeginRead(font);                 // -> maxidx
    uint8_t maxidx = NVMread(1);
    font += 1;
    uint8_t idx = xchar & 0x3F;
    if (idx >= maxidx) return 0;        // beyond the end of the table
    NVMbeginRead(3 * idx + font);       // -> coarse table
	return NVMread(3);                  // address of glyph
}

static int linepitch(void) {            // line pitch in pixels
    if (fontID) return 44;
    return 24;
}

static int xspace(void) {               // pixels in a space
    int x = abs(kerning) + linepitch() / 4;
	if (kerning < 0) x = -x;            // negative kerning
}

static void cr(void) {
    if (kerning < 0) charX = WinWidth - 16;
    else charX = 0;                     // reset to start of line
    charY += linepitch();               // move to next line
    if (charY >= WinHeight) {           // end of screen?
        charY = 0;                      // reset to top of screen
    }
}

static int glyphSetup(int addr) {       // return right edge X
    NVMbeginRead(addr);
    charleft = NVMread(1);
    chartop = NVMread(1);
    charwidth = NVMread(1);
    charheight = NVMread(1);
    return charX + charleft + charwidth;
}

void LCDchar(unsigned int xchar) {
    uint32_t addr = fontaddr(xchar);
    if (xchar == '\n') {
        cr();
        return;
    }
    if (addr) {
        if (glyphSetup(addr) > WinWidth) cr();
        decompress();                   // draw the glyph
        int offset = charwidth + charleft;
        if (kerning < 0) offset = -offset; // negative kerning
		charX += offset + kerning;      // move to next character position
        if ((charX >= WinWidth) || (charX < 0)) cr();
    }
    else {
		charX += xspace(); 
    }
}

int LCDcharWidth(unsigned int xchar) {
    uint32_t addr = fontaddr(xchar);
    if (addr) {
        glyphSetup(addr);
        return abs(kerning) + charwidth + charleft;
    }
    else {
        return xspace();
    }
}
