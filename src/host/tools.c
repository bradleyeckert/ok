/*
Compatibility tools
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "tools.h"
#include "../bci/bci.h"

#ifdef _MSC_VER
#include <Windows.h>
uint64_t GetMicroseconds(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long tt = ft.dwHighDateTime;
    tt <<= 32;
    tt |= ft.dwLowDateTime;
    tt /= 10;
    tt -= 11644473600000000ULL;
    return tt;
}
void uSleep(uint64_t usec)
{
    HANDLE timer;
    LARGE_INTEGER ft;

    ft.QuadPart = -(10*(signed)usec); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (timer == NULL) return;
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}
#else
#include <sys/time.h> // GCC library
uint64_t GetMicroseconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * (uint64_t)1000000 + tv.tv_usec;
}
#include <unistd.h>
void uSleep(uint64_t usec) {
    usleep(usec);
}
#endif

void cdump(const uint8_t *src, uint16_t len) {
    for (int i = 0; i < len; i++) {
        if ((i % 32) == 0) printf("\n___");
        printf("%02X ", src[i]);
    }
    printf("<- ");
}

// itoa replacement

static char buf[64];

char* itos(uint64_t x, uint8_t radix, int8_t digits, uint8_t issign, uint8_t cellbits) {
    int sign = 0;
    if (issign) sign = (x >> (cellbits - 1)) & 1;
    if (sign) {
        x = (~x) + 1;
        if (cellbits < 64) x &= ((1ull << cellbits) - 1);
    }
    int i = 64;  buf[--i] = 0;
    do {
        char c = x % radix;
        if (c > 9) c += 7;
        buf[--i] = c + '0';
        x /= radix;
        digits--;
    } while (((x != 0) && (i >= 0)) || (digits > 0));
    if (sign) buf[--i] = '-';
    return &buf[i];
}

// concatenate string (dest, src, maxdest)

void StrCat(char* dest, const char* src, int limit) {    // safe strcat
    int i = (int)strlen(dest);
    while (i < limit) {
        char c = *src++;
        dest[i++] = c;
        if (c == 0) return;             // up to and including the terminator
    }
    dest[--i] = 0;                      // max reached, add terminator
}


// A strncpy that complies with C safety checks.

void strmove(char* dest, char* src, unsigned int maxlen) {
    dest[0] = 0;
    StrCat(dest, src, maxlen);
}

// fopen without complaints

FILE* fopenx(char* filename, char* fmt) {
#ifdef MORESAFE
    FILE* fp;
    fopen_s(&fp, filename, fmt);
    return fp;
#else
    return fopen(filename, fmt);
#endif
}

// ANS Forth Standard Throw Codes

// This list includes everything but the kitchen sink and adds a couple more.

static char ErrorString[260];   // String to include in error message

char * ErrorMessage(int error, char* s) {
    switch (error) {
    case   -2: return s;                         /* ABORT" */
    case   -3: return "Stack overflow";
    case   -4: return "Stack underflow";
    case   -5: return "Return stack overflow";
    case   -6: return "Return stack underflow";
    case   -7: return "Do-loops nested too deeply during execution";
    case   -8: return "Dictionary overflow";
    case   -9: return "Invalid memory address";
    case  -10: return "Division by zero";
    case  -11: return "Result out of range";
    case  -12: return "Argument type mismatch";
    case  -13: memcpy(ErrorString, s, 256);
        StrCat(ErrorString, " ?", 256);
        return ErrorString;
    case  -14: return "Interpreting a compile-only word";
    case  -15: return "Invalid FORGET";
    case  -16: return "Attempt to use zero-length string as a name";
    case  -17: return "Pictured numeric output string overflow";
    case  -18: return "Parsed string overflow";
    case  -19: return "Definition name too long";
    case  -20: return "Write to a read-only location";
    case  -21: return "Unsupported operation";
    case  -22: return "Control structure mismatch";
    case  -23: return "Address alignment exception";
    case  -24: return "Invalid numeric argument";
    case  -25: return "Return stack imbalance";
    case  -26: return "Loop parameters unavailable";
    case  -27: return "Invalid recursion";
    case  -28: return "User interrupt";
    case  -29: return "Compiler nesting";
    case  -30: return "Obsolescent feature";
    case  -31: return ">BODY used on non-CREATEd definition";
    case  -32: return "Invalid name argument (e.g., TO xxx)";
    case  -33: return "Block read exception";
    case  -34: return "Block write exception";
    case  -35: return "Invalid block number";
    case  -36: return "Invalid file position";
    case  -37: return "File I/O exception";
    case  -38: return "File not found";
    case  -39: return "Unexpected end of file";
    case  -40: return "Invalid BASE for floating point conversion";
    case  -41: return "Loss of precision";
    case  -42: return "Floating-point divide by zero";
    case  -43: return "Floating-point result out of range";
    case  -44: return "Floating-point stack overflow";
    case  -45: return "Floating-point stack underflow";
    case  -46: return "Floating-point invalid argument";
    case  -47: return "Compilation wordlist deleted";
    case  -48: return "Invalid POSTPONE";
    case  -49: return "Search-order overflow";
    case  -50: return "Search-order underflow";
    case  -51: return "Compilation wordlist changed";
    case  -52: return "Control-flow stack overflow";
    case  -53: return "Exception stack overflow";
    case  -54: return "Floating-point underflow";
    case  -55: return "Floating-point unidentified fault";
    case  -56: return "QUIT";
    case  -57: return "Exception in sending or receiving a character";
    case  -58: return "[IF], [ELSE], or [THEN] exception";
    case  -59: return "Missing literal before opcode";
    case  -60: return "Attempt to write to non-blank flash memory";
    case  -61: return "Macro expansion failure";
    case  -62: return "Input buffer overflow, line too long";
    case  -63: return "Bad arguments to RESTORE-INPUT";
    case  -64: return "Write to non-existent data memory";
    case  -65: return "Read from non-existent data memory";
    case  -66: return "PC is in non-existent code memory";
    case  -67: return "Write to non-existent code memory";
    case  -68: return "Test failure";
    case  -69: return "Page fault writing flash memory";
    case  -70: return "Bad I/O address";
    case  -71: return "Writing to flash without issuing WREN first";
    case  -72: return "Invalid ALU opcode";
    case  -73: return "Bitfield is 0 or too wide for a cell";
    case  -74: return "Resolving a word that's not a DEFER";
    case  -75: return "Too many WORDLISTs used";
    case  -76: return "Invalid WID";
    case  -77: return "Invalid CREATE DOES> usage";
    case  -78: return "Nesting overflow during include";
    case  -79: return "Compiling an execute-only word";
    case  -80: return "Dictionary full";
    case  -81: return "Writing to invalid flash sector";
    case  -82: return "Flash string space overflow";
    case  -83: return "Invalid SPI flash address";
    case  -84: return "Invalid coprocessor field";
    case  -85: return "Can't postpone an applet word";
    case  -86: return "Bad message received from BCI";
    case  -87: return "PC is at address 0";
    case  -88: return "Assertion failure: Wrong number of results";
    case  -89: return "Assertion failure: Incorrect result";
    case -100: return "ALLOCATE failed";
    case -101: return "RESIZE failed";
    case -102: return "FREE failed";
    case -190: return "Can't change directory";
    case -191: return "Can't delete file";  /* DELETE-FILE */
    case -192: return "Can't rename file";  /* RENAME-FILE */
    case -193: return "Can't resize file";  /* RESIZE-FILE */
    case -194: return "Can't flush file";   /* FLUSH-FILE  */
    case -195: return "Can't read file";    /* READ-FILE,READ-LINE */
    case -196: return "Can't write file";   /* WRITE-FILE */
    case -197: return "Can't close file";   /* CLOSE-FILE */
    case -198: return "Can't create file";  /* CREATE-FILE */
    case -199: return "Can't open file";  /* OPEN-FILE,INCLUDE-FILE*/
    default: return itos(error, 10, 1, 1, 16);
    }
    return "";
}
