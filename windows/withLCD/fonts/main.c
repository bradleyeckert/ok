/*
Decompress font glyphs to the console

The console mimics the mini-raster set up by SETROW and SETCOL commands in TFT controllers.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define TESTMODE

#ifdef TESTMODE
#include "myfont.h"

uint8_t FontBlob[] = { FONTBLOB };

int width;          // width of rendering box
int height;         // height of rendering box
int pelcount;       // position in the raster
int flashoffset;    // offset into the font structure in flash memory

void LCDsetcols (int x0, int x1) {
    width = x1 - x0;
}

void LCDsetrows (int y0, int y1) {
    height = y1 - y0;
}

void BeginFontRead(int offset) {
    flashoffset = offset;
}

uint8_t FontRead8(void) {
    if (flashoffset >= sizeof(FontBlob)) return 0;
    return FontBlob[flashoffset++];
}

uint16_t FontRead16(void) {
    uint16_t x = FontRead8() << 8;
    return x | FontRead8();
}

uint32_t FontRead24(void) {
    uint32_t x = FontRead8() << 16;
    return x | FontRead16();
}

const char shades[] = ".123456789ABCDE#";

// color is 0 to 15 (background to foreground gradient)
void sendPixel(int shade) {
    if ((pelcount % width) == 0) putchar('\n');
    pelcount++;
    putchar(shades[shade & 0x0F]);
}

#endif

//                     R  G  B
uint8_t BGcolor[4] = { 0, 0, 0, 0 };
uint8_t FGcolor[4] = { 255, 255, 255, 0 };

// ST7789VW controller datasheet
// 8.8.41 Write data for 12-bit/pixel (RGB 4-4-4-bit input), 4K-Colors, 3Ah=”03h”
// MSB to LSB = RGB

void sendShade(int shade) {
    uint8_t w0 = (~shade & 0x0F);
    uint8_t w1 = shade & 0x0F;
    w0 = w0 | (w0 << 4);
    w1 = w1 | (w1 << 4);
    uint16_t red =   (BGcolor[0] * w0 + FGcolor[0] * w1) >> 12;
    uint16_t green = (BGcolor[1] * w0 + FGcolor[1] * w1) >> 12;
    uint16_t blue =  (BGcolor[2] * w0 + FGcolor[2] * w1) >> 12;
    uint16_t color = (red << 8) + (green << 4) + blue;
    sendPixel(color);
}

void sendShades(int shade, int length) {
    for (int i = 0; i < length; i++) sendShade(shade);
}

int monopel(int pattern) {
    sendShade(15 * (pattern & 1));
    return pattern >> 1;
}

void monopels(int pattern, int length) {
    for (int i = 0; i < length; i++)
        pattern = monopel(pattern);
}

void BGrun(int length) { sendShades(0, length); }
void FGrun(int length) { sendShades(15, length); }

void pelcommand(uint16_t cmd) {
    if (cmd & 0x8000) {                 // format: 1xxxxxxx_xxxxxxxx
        monopels(cmd, 15);              // 15 x 1bpp
        return;
    }
    if ((cmd & 0xE000) == 0x2000) {     // format: 001ccccx_xxxxxxxx
        monopels(cmd, 15 & (cmd >> 9));
        return;
    }
    if ((cmd & 0xF000) == 0x4000) {     // format: 0100aaaa_aabbbbbb
        BGrun(cmd & 0x3F);
        FGrun((cmd >> 6) & 0x3F);
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
    putchar('?');                       // 000xxxxx_xxxxxxxx is reserved
}

void decompress(void) {
    uint16_t x = FontRead16();         // box dimensions
    LCDsetrows(0, x & 0xFF);
    LCDsetcols(0, (x >> 8) & 0xFF);
    printf("width=%d height=%d ", width, height);
    uint16_t length = FontRead16();    // number of 16-bit words
    for (int i = 0; i < length; i++) {
        x = FontRead16();
        pelcommand(x);
    }
}

int fontID;

int fontaddr(int xchar) {
    BeginFontRead(0);                  // -> font blob
    uint8_t c = FontRead8();
    if (fontID > c) {
        return 0;                       // font does not exist
    }
    for (int i = 0; i < (3 + 6 * fontID); i++)
        FontRead8();                   // skip 3-byte rev # and font links
    uint32_t font = FontRead24();
    uint32_t maxchar = FontRead24();
    if (xchar > maxchar) {
        return 0;                       // overrange xchar
    }
    int fine = (xchar >> 6) * 2 + font;
    BeginFontRead(fine);               // -> fine table
    uint16_t offset = FontRead16();
    fine += offset;
    BeginFontRead(fine);               // -> maxidx
    uint8_t maxidx = FontRead8();
    uint8_t idx = xchar & 0x3F;
    if (idx >= maxidx) {
        return 0;                       // beyond the end of the table
    }
    BeginFontRead(3 * idx + fine + 1); // -> coarse table
    return FontRead24();
}

void tchar(int xchar) {
    pelcount = 0;
    uint32_t addr = fontaddr(xchar);
    BeginFontRead(addr);
    printf("\nleft=%d ", FontRead8());
    printf("\ntop=%d ", FontRead8());
    decompress();
}

int main() {
    tchar('3');
    return 0;
}


