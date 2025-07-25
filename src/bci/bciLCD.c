/*
Decompress font glyphs to the LCD module
*/

#include <stdlib.h>
#include <stdint.h>
#include "bciHW.h"

/*
* TFTLCDraw is the interface to the LCD module. It takes a 32-bit value and
* a mode byte to set up control lnes and send data to the LCD controller.
* Font bitmaps are stored in the NVM and read by NVMbeginRead and NVMread.
*/

// The foreground and background colors for the LCD
//                     R  G  B
uint8_t BGcolor[4] = { 0, 0, 0, 0 };
uint8_t FGcolor[4] = { 255, 255, 255, 0 };

VMcell_t API_LCDFG(vm_ctx* ctx) { memcpy(&FGcolor, &ctx->t, 3);  return 0; }
VMcell_t API_LCDBG(vm_ctx* ctx) { memcpy(&BGcolor, &ctx->t, 3);  return 0; }


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
    TFTLCDraw(color, 12 + TFTsimDC);
}

static void LCDcmd2(uint8_t cmd, uint8_t x0, uint8_t x1) {
    TFTLCDraw(cmd, 8);
    TFTLCDraw(x0, 8);
    TFTLCDraw(x1, 8);
}

static void LCDsetrows(uint8_t x0, uint8_t x1) {
	LCDcmd2(0x2B, x0, x1); // Row Address Set
}

static void LCDsetcols(uint8_t x0, uint8_t x1) {
    LCDcmd2(0x2A, x0, x1); // Column Address Set
}

static void sendShades(int shade, int length) {
    for (int i = 0; i < length; i++) sendShade(shade);
}

static int monopel(int pattern) {
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
                                        // 000xxxxx_xxxxxxxx is reserved
}

static void decompress(void) {
    uint16_t x = NVMread(2);            // box dimensions
    LCDsetrows(0, x & 0xFF);
    LCDsetcols(0, (x >> 8) & 0xFF);
    uint16_t length = NVMread(2);       // number of 16-bit words
    for (int i = 0; i < length; i++) {
        x = NVMread(2);
        pelcommand(x);
    }
}

int fontID;

static int fontaddr(unsigned int xchar) {
    NVMbeginRead(0);                    // -> font blob
    uint8_t c = NVMread(1);
    if (fontID > c) {
        return 0;                       // font does not exist
    }
    NVMread(3 + 6 * fontID);            // skip 3-byte rev # and font links
    uint32_t font = NVMread(3);
    uint32_t maxchar = NVMread(3);
    if (xchar > maxchar) {
        return 0;                       // overrange xchar
    }
    int fine = (xchar >> 6) * 2 + font;
    NVMbeginRead(fine);                 // -> fine table
    uint16_t offset = NVMread(2);
    fine += offset;
    NVMbeginRead(fine);                 // -> maxidx
    uint8_t maxidx = NVMread(1);
    uint8_t idx = xchar & 0x3F;
    if (idx >= maxidx) {
        return 0;                       // beyond the end of the table
    }
    NVMbeginRead(3 * idx + fine + 1);   // -> coarse table
    return NVMread(3);
}

void tchar(unsigned int xchar) {
    uint32_t addr = fontaddr(xchar);
    NVMbeginRead(addr);
    uint8_t left = NVMread(1);
    uint8_t top = NVMread(1);
    decompress();
}

