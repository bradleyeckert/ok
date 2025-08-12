// C11 is needed for fopen_s
// Compile with: gcc -o blobcmp blobcmp.c -I../../src/mole -lm
// This program amalgamates blob files for the host version of NVM.

#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define NVMSIZE 0x400000
#define MAXBLOBS 8

/*
The first 4KB sector of SPI Flash contains a list of blobs.
Each 20-byte table entry consists of:

- 2-byte first sector
- 2-byte last sector
- 16-byte HMAC

The table is a simple linear list.
A "first sector" value of FFFFh terminates the list.
Sector 0 address range is 00001000h to 0000FFFFh. Otherwise,
sector N address range is NNNN0000h to NNNNFFFFh.

The output file does not contain the HMAC because it is
only used in a development environment.

This format supports a 32-bit SPI address range, which is likely the highest
you will ever see on a NOR SPI Flash (32Gb).
*/

uint8_t NVM[NVMSIZE];
uint32_t here;

static void store16be(uint8_t* p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

int main(int argc, char* argv[]) {
    if (argc < 1) {     // argv[1] = output
        printf("No output file specified.\n\n");
    help:   
        printf("Usage: blobcmp outfile [blob0] [blob1] ...");
        return 1;
    }
    if (argc < 2) {     // argv[2,3,4...] = inputs
        printf("No blob files specified.\n\n");
        goto help;
    }
    if (argc >= MAXBLOBS) {
        printf("Too many blobs specified, max is %d.\n\n", MAXBLOBS);
        goto help;
    }
    memset(NVM, 255, NVMSIZE);
    here = 4096;
    FILE* f;
    uint32_t addr0 = 0;
    uint32_t addr1 = 0;
    for (int i = 2; i < argc; i++) {
        addr0 = here;
        uint16_t firstsector = (uint16_t)(here >> 16);
        printf("Blob %d, 64KB sectors %d to ", i - 2, firstsector);
        store16be(NVM + (i - 2) * 20, firstsector);
        errno_t err = fopen_s(&f, argv[i], "rb");
        if (err != 0) {
            printf("Cannot open file %s\n", argv[i]);
            return 2;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (here + size > NVMSIZE) {
            printf("Not enough space in NVM for file %s (%ld bytes)\n",
                argv[i], size);
            fclose(f);
            return 3;
        }
        fread(NVM + here, 1, size, f);
        fclose(f);
		here += size;
        addr1 = here - 1;
        uint16_t lastsector = (uint16_t)(addr1 >> 16);
        store16be(NVM + 2 + (i - 2) * 20, lastsector);
		here = (lastsector + 1) << 16;
        printf("%d, %06X to %06X = %s\n", lastsector, addr0, addr1, argv[i]);
    }
    errno_t err = fopen_s(&f, argv[1], "wb");
    if (err != 0) {
        printf("Cannot write file %s\n", argv[1]);
        return 2;
    }
	addr1 = (addr1 + 4096) & ~0xFFF; // Round up to next 4KB sector
    fwrite(NVM, 1, addr1, f);
    fclose(f);
    printf("%d bytes written to %s\n", addr1, argv[1]);
}
