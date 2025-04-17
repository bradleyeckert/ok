#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "../bci/bci.h"

/*
Test parser format:
String = in1, in2, in3, ..., inN ==> out1, out2, out3, ...
Parameters are in the C hex format 0x... where the number of bytes is determined
by the number of digits. A parameter begins with "0x" and ends with anything
that is not a digit.
*/

uint8_t command[3][260];                // command, expected, actual
vm_ctx me;
int testID;
int errors;

uint8_t mismatch(void) {                // compare expected to actual
    uint8_t len1 = command[1][0];
    uint8_t len2 = command[2][0];
    if (len1 != len2) return 1;
    for (int i = 1; i <= len1; i++) {
        if (command[1][i] != command[2][i]) return 1;
    }
    return 0;
}

void appendByte(int i, uint8_t c) {
    uint8_t len = command[i][0];
    command[i][len + 1] = c;
    command[i][0] = len + 1;
}

// Capture the BCI output stream in command[2]

void mySendInit(int id)     {command[2][0] = 0;}
void mySendChar(int id, uint8_t c) {appendByte(2, c);}

void mySendFinal(int id) {
    if (mismatch()) {
        errors++;
        printf("\nTest %d = ", testID);
        for (int i = 1; i <= command[0][0]; i++) printf("%02x", command[0][i]);
        printf("\n  Actual = ");
        for (int i = 1; i <= command[2][0]; i++) printf("%02x", command[2][i]);
        printf("\nExpected = ");
        for (int i = 1; i <= command[1][0]; i++) printf("%02x", command[1][i]);
    }
}

void myInitial(vm_ctx *ctx) {
    ctx->InitFn = mySendInit;           // output initialization function
    ctx->putcFn = mySendChar;           // output putc function
    ctx->FinalFn = mySendFinal;         // output finalization function
}

void t(int tid, const char *s) {        // test function
    uint8_t state = 0;
    uint8_t digits = 0;
    uint8_t expecting = 0;
    uint8_t done = 0;
    uint32_t x = 0;
    testID = tid;
    memset(command, 0, sizeof(command));
    do {
        uint8_t c = *s++;               // parse the hex numbers
        done = (c == 0);
        if (done) state = 3;
        switch (state) {
            case 0:
                x = 0;
                switch (c) {
                    case '0': state = 1;  break;    // 0x1234
                    case 'h': state = 2;  break;    // h1234
                    case '=': state = 4;  break;    // ==>
                    default: break;
                }
                break;
            case 1: if ('x' == c) state++;
                break;
            case 2: c = toupper(c);
                if (((c >= '0') && (c <= '9')) ||
                    ((c >= 'A') && (c <= 'F'))) {
                    c -= '0';
                    if (c > 9) c -= 7;
                    x = (x << 4) + c;
                    digits++;
                    break;
                }
            case 3:
                digits = (digits + 1) / 2;
                while (digits--) appendByte(expecting, x >> (8*digits));
                state = 0;
                break;
            case 4: if ('>' != c) break;
                expecting = 1;
            default: state = 0;
        }
    } while (!done);
    if (expecting) {                    // call the BCI (in bci.c)
        BCIhandler(&me, &command[0][1], command[0][0]);
    }
//  printf(" <-- Test %d", tid);
}

// Tests illustrate the use of the BCI functions
/*
00	nop	  inv	dup	  a!	+	  xor	and	  drop	    ..+-----
08	swap  2*	over  cy!	@a	  @a+	@b	  @b+		..+-++++
10	2/c	  2/		  u!	!a	  !a+	!b	  !b+		..+-----
18	unext		u	  >r	cy	  a	    r@	  r>		..+-++++
*/

int main() {
    myInitial(&me);
    BCIinitial(&me);
    printf("Starting tests for %d-bit cells\n", VM_CELLBITS);
    t( 0, "0x00 ==> 0xFC000700, 0x20100F, 0x03070F, 0x0000");                   // boilerplate
    t( 1, "0x01, 0x02, 0x00000123 ==> 0xFC0102, 0x00000000, 0x00000000, 0x0000"); // read memory
    t( 2, "0x02, 0x02, 0x00000123, 0x00012345, 0x00056789 ==> 0xFC020000");       // write memory
    // run length --^  ^--address  ^--1st      ^--2nd         ^--ack
    t( 3, "0x01, 0x02, 0x00000123 ==> 0xFC0102, 0x00012345, 0x00056789, 0x0000"); // read memory
    // run length --^  ^--address     len--^  ^--1st      ^--2nd      ^--ack
    t( 4, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00022222, 0x80008000 ==> 0xFC03FC02, 0x00011111, 0x00022222, 0x0000000A, 0x0000"); // nop
    // base -----^   depth------^  ^--2nd      ^--top      ^--xt       depth--^   ^--2nd      ^--top      ^--base
    t( 5, "0x04, 0x00000123, 0x00000002                               ==> 0xFC04, 0x91e83315, 0x0000");             // read CRC
    t( 6, "0x04, 0x00000120, 0x00000002                               ==> 0xFC04, 0x6522df69, 0x0000");
    t( 7, "0x03, 0x0000000A, 0x01, 0x00055555,             0x80008001 ==> 0xFC03FC01, 0xFFFAAAAA, 0x0000000A, 0x0000");             // inv
    t( 8, "0x03, 0x0000000A, 0x01, 0x00011111,             0x8000800A ==> 0xFC03FC02, 0x00011111, 0x00011111, 0x0000000A, 0x0000"); // dup
    t( 9, "0x03, 0x0000000A, 0x01, 0x00000123,             0x80008003 ==> 0xFC03FC00, 0x0000000A, 0x0000");                         // a!
    t(10, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00022222, 0x80008004 ==> 0xFC03FC01, 0x00033333, 0x0000000A, 0x0000");             // +
    t(11, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00033333, 0x80008005 ==> 0xFC03FC01, 0x00022222, 0x0000000A, 0x0000");             // xor
    t(12, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00033333, 0x80008007 ==> 0xFC03FC01, 0x00011111, 0x0000000A, 0x0000");             // and
    t(13, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00033333, 0x80008007 ==> 0xFC03FC01, 0x00011111, 0x0000000A, 0x0000");             // drop
    t(14, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00022222, 0x80008008 ==> 0xFC03FC02, 0x00022222, 0x00011111, 0x0000000A, 0x0000"); // swap
    t(15, "0x03, 0x0000000A, 0x01, 0x00012345,             0x80008009 ==> 0xFC03FC01, 0x0002468A, 0x0000000A, 0x0000");             // 2*
    t(16, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00022222, 0x80008002 ==> 0xFC03FC03, 0x00011111, 0x00022222, 0x00011111, 0x0000000A, 0x0000"); // over
    t(17, "0x03, 0x0000000A, 0x01, 0x00000123,             0x8000800B ==> 0xFC03FC00, 0x0000000A, 0x0000");                         // cy!
    t(18, "0x03, 0x0000000A, 0x00,                         0x8000801D ==> 0xFC03FC01, 0x00000123, 0x0000000A, 0x0000");             // a
    t(19, "0x03, 0x0000000A, 0x00,                         0x8000800D ==> 0xFC03FC01, 0x00012345, 0x0000000A, 0x0000");             // @a+
    t(20, "0x03, 0x0000000A, 0x00,                         0x8000800C ==> 0xFC03FC01, 0x00056789, 0x0000000A, 0x0000");             // @a
    t(21, "0x03, 0x0000000A, 0x00,                         0x8000800E ==> 0xFC03FC01, 0x00056789, 0x0000000A, 0x0000");             // @b+
    t(22, "0x03, 0x0000000A, 0x00,                         0x8000801D ==> 0xFC03FC01, 0x00000000, 0x0000000A, 0x0000");             // a
    t(23, "0x03, 0x0000000A, 0x01, 0x00012345,             0x80008010 ==> 0xFC03FC01, 0x800091A2, 0x0000000A, 0x0000");             // 2/c
    t(24, "0x03, 0x0000000A, 0x01, 0x00012344,             0x80008011 ==> 0xFC03FC01, 0x000091A2, 0x0000000A, 0x0000");             // 2/
    t(25, "0x03, 0x0000000A, 0x00,                         0x8000801C ==> 0xFC03FC01, 0x00000000, 0x0000000A, 0x0000");             // cy
    t(26, "0x03, 0x0000000A, 0x01, 0x00000125,             0x80008003 ==> 0xFC03FC00, 0x0000000A, 0x0000");                         // a!
    t(27, "0x03, 0x0000000A, 0x01, 0x00054321,             0x80008016 ==> 0xFC03FC00, 0x0000000A, 0x0000");                         // !b
    t(28, "0x03, 0x0000000A, 0x00,                         0x8000801D ==> 0xFC03FC01, 0x00000124, 0x0000000A, 0x0000");             // a
    t(29, "0x01, 0x02, 0x00000124                                     ==> 0xFC0102, 0x00056789, 0x00054321, 0x0000"); // read memory
    t(30, "0x55, 0x02, 0x00000124                                     ==> 0xFC55FFAC"); // bad command
    // test regular ops
    t(31, "0x03, 0x0000000A, 0x00,                         0x800061FC ==> 0xFC03FC00, 0x0000000A, 0x0000");                         // pfx
    t(32, "0x03, 0x0000000A, 0x00,                         0x80004123 ==> 0xFC03FC01, 0x003f8123, 0x0000000A, 0x0000");             // lit
    t(33, "0x03, 0x0000000A, 0x01, 0x00012398,             0x80006200 ==> 0xFC03FC00, 0x0000000A, 0x0000");                         // x!
    t(34, "0x03, 0x0000000A, 0x00,                         0x80006488 ==> 0xFC03FC00, 0x0000000A, 0x0000");                         // ax
    t(35, "0x03, 0x0000000A, 0x00,                         0x8000801D ==> 0xFC03FC01, 0x00012420, 0x0000000A, 0x0000");             // a
    return 0;
}
