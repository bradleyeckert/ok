#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "../bci.h"

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

void mySendInit(void)     {command[2][0] = 0;}
void mySendChar(uint8_t c) {appendByte(2, c);}

void mySendFinal(void) {
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
    t( 0, "0x00 ==> 0x0100FE");                                             // boilerplate
    t( 1, "0x01, 0x02, 0x00000123 ==> 0x02, 0x00000000, 0x00000000, 0xFE"); // read memory
    t( 2, "0x02, 0x02, 0x00000123, 0x00012345, 0x00056789 ==> 0xFE");       // write memory
    // run length --^  ^--address  ^--1st      ^--2nd         ^--ack
    t( 3, "0x01, 0x02, 0x00000123 ==> 0x02, 0x00012345, 0x00056789, 0xFE"); // read memory
    // run length --^  ^--address   len--^  ^--1st      ^--2nd      ^--ack
    t( 4, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00022222, 0x80000000 ==> 0x02, 0x00011111, 0x00022222, 0x0000000A, 0xFE"); // nop
    // base -----^   depth------^  ^--2nd      ^--top      ^--xt      depth--^  ^--2nd      ^--top      ^--base
    t( 5, "0x04, 0x00000123, 0x00000002 ==> 0x91e83315, 0xFE");             // read CRC
    t( 6, "0x04, 0x00000120, 0x00000002 ==> 0x6522df69, 0xFE");
    t( 7, "0x03, 0x0000000A, 0x01, 0x00055555,             0x80000001 ==> 0x01, 0xFFFAAAAA, 0x0000000A, 0xFE");             // inv
    t( 8, "0x03, 0x0000000A, 0x01, 0x00011111,             0x80000002 ==> 0x02, 0x00011111, 0x00011111, 0x0000000A, 0xFE"); // dup
    t( 9, "0x03, 0x0000000A, 0x01, 0x00000123,             0x80000003 ==> 0x00, 0x0000000A, 0xFE");                         // a!
    t(10, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00022222, 0x80000004 ==> 0x01, 0x00033333, 0x0000000A, 0xFE");             // +
    t(11, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00033333, 0x80000005 ==> 0x01, 0x00022222, 0x0000000A, 0xFE");             // xor
    t(12, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00033333, 0x80000007 ==> 0x01, 0x00011111, 0x0000000A, 0xFE");             // and
    t(13, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00033333, 0x80000007 ==> 0x01, 0x00011111, 0x0000000A, 0xFE");             // drop
    t(14, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00022222, 0x80000008 ==> 0x02, 0x00022222, 0x00011111, 0x0000000A, 0xFE"); // swap
    t(15, "0x03, 0x0000000A, 0x01, 0x00012345,             0x80000009 ==> 0x01, 0x0002468A, 0x0000000A, 0xFE");             // 2*
    t(16, "0x03, 0x0000000A, 0x02, 0x00011111, 0x00022222, 0x8000000A ==> 0x03, 0x00011111, 0x00022222, 0x00011111, 0x0000000A, 0xFE"); // over
    t(17, "0x03, 0x0000000A, 0x01, 0x00000123,             0x8000000B ==> 0x00, 0x0000000A, 0xFE");                         // cy!
    t(18, "0x03, 0x0000000A, 0x00,                         0x8000001D ==> 0x01, 0x00000123, 0x0000000A, 0xFE");             // a
    t(19, "0x03, 0x0000000A, 0x00,                         0x8000000D ==> 0x01, 0x00012345, 0x0000000A, 0xFE");             // @a+
    t(20, "0x03, 0x0000000A, 0x00,                         0x8000000C ==> 0x01, 0x00056789, 0x0000000A, 0xFE");             // @a
    t(21, "0x03, 0x0000000A, 0x00,                         0x8000000E ==> 0x01, 0x00056789, 0x0000000A, 0xFE");             // @b+
    t(22, "0x03, 0x0000000A, 0x00,                         0x8000001D ==> 0x01, 0x00000000, 0x0000000A, 0xFE");             // a
    t(23, "0x03, 0x0000000A, 0x01, 0x00012345,             0x80000010 ==> 0x01, 0x800091A2, 0x0000000A, 0xFE");             // 2/c
    t(24, "0x03, 0x0000000A, 0x01, 0x00012344,             0x80000011 ==> 0x01, 0x000091A2, 0x0000000A, 0xFE");             // 2/
    t(25, "0x03, 0x0000000A, 0x00,                         0x8000001C ==> 0x01, 0x00000000, 0x0000000A, 0xFE");             // cy
    t(26, "0x03, 0x0000000A, 0x01, 0x00000125,             0x80000003 ==> 0x00, 0x0000000A, 0xFE");                         // a!
    t(27, "0x03, 0x0000000A, 0x01, 0x00054321,             0x80000016 ==> 0x00, 0x0000000A, 0xFE");                         // !b
    t(28, "0x03, 0x0000000A, 0x00,                         0x8000001D ==> 0x01, 0x00000124, 0x0000000A, 0xFE");             // a
    t(29, "0x01, 0x02, 0x00000124 ==> 0x02, 0x00056789, 0x00054321, 0xFE"); // read memory
    return 0;
}
