// Lang: C11 is needed for fopen_s
// This program amalgamates blob files into a binary image.
// A blob is a binary file that contains a single image or data set.

// The storage medium is normally SPI NOR Flash with 4KB sectors.
// Physically, SPI commands on newer parts have a 32-bit address which
// imposes a limit of 32 Gb. As of 2026, 25Q128 is 16MB for under $2.
// Allowing for the addition of 1 address bit every 2 to 3 years,
// low-cost parts will hit 32 Gb (4 GB) between 2042 and 2050.
// Allowing for 32-bit sector IDs (44-bit byte address) pushes that out
// another 24 to 36 years, assuming anyone wants a 2TB SPI NOR Flash.

// The physical layout of the NVM image (output file) is:
// 4096-byte index table
// Blob 0
// Blob 1, 4KB-aligned
// ...
// Blob N, 4KB-aligned

// When used in an application, an NVMinit function reads the index table
// at startup and checks the HMAC to prevent tampering. NVM access words
// map logical addresses (starting from 0) onto physical addresses, with
// each blob being a block of 128 KB. This abstraction allows porting to
// internal STM32H7 flash, which uses 128 KB sectors.

#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define NVMSIZE 0x1000000 // 25Q128 or smaller

/*
The first 4KB sector of SPI Flash contains a list of blobs.
Each 24-byte table entry consists of:

- 4-byte first 4KB sector
- 4-byte last 4KB sector
- 16-byte HMAC

The table is a simple linear list.
A "first sector" value of FFFFFFFFh terminates the list.

The output file does not contain the HMAC because it is
only used in a development environment.
*/

uint8_t NVM[NVMSIZE];

static void store32be(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
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
    memset(NVM, 255, NVMSIZE);
    size_t here = 4096;
    FILE* f;
    size_t addr0 = 0;                 // support addresses over 32-bit
    size_t addr1 = 0;
    for (int i = 2; i < argc; i++) {
        addr0 = here;
        uint32_t firstsector = (uint32_t)(here >> 12);
        printf("Blob %d, 4KB sectors %d to ", i - 2, firstsector);
        store32be(NVM + (i - 2) * 24, firstsector);
        errno_t err = fopen_s(&f, argv[i], "rb");
        if (err != 0) {
            printf("Cannot open file %s\n", argv[i]);
            return 2;
        }
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (here + size > NVMSIZE) {
            printf("Not enough space in NVM for file %s (%zu bytes)\n",
                argv[i], size);
            fclose(f);
            return 3;
        }
        fread(NVM + here, 1, size, f);
        fclose(f);
		here += size;
        addr1 = here - 1;
        uint32_t lastsector = (uint32_t)(addr1 >> 12);
        store32be(NVM + 4 + (i - 2) * 24, lastsector);
		here = ((size_t)(lastsector + 1) << 12);
        printf("%d, 0x%zx to 0x%zx = %s\n",
            lastsector, addr0, addr1, argv[i]);
    }
    errno_t err = fopen_s(&f, argv[1], "wb");
    if (err != 0) {
        printf("Cannot write file %s\n", argv[1]);
        return 2;
    }
	addr1 = (addr1 + 4096) & ~0xFFF;    // Round up to next 4KB sector
    if (addr1 > NVMSIZE) {
        printf("Not enough space in NVM for file %s (%zu bytes)\n",
            argv[1], addr1);
        fclose(f);
        return 3;
	}
    fwrite(NVM, 1, addr1, f);
    fclose(f);
    printf("%zu bytes written to %s\n", addr1, argv[1]);
}
