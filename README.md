# ok
Another C-based Forth

Status: Proof of concept works: Compiles and executes Forth. Execution is on a VM either within `ok` or on a remote [target](./target/PC) connected by serial ports.

This Forth is designed for embedded systems development using a host PC, serial port connection, and a target system based on an MCU or FPGA. To address modern cybersecurity requirements, the serial connection is encrypted.

`ok` contains a copy of the target VM(s) and a simulated serial port. The serial connection may be redirected to real target hardware.

Multiple VMs are supported via pthreads, so `ok` can simulate an array of Forth cores (one core per thread).
Some work would be required to simulate message passing between cores.


## compiling

`make all` under Linux

Code::Blocks works under Windows after manually adding files. Add files recursively, then remove all files with a `main` function, except main.c.

I could not make Visual Studio 2022 work with either pthreads or the Windows \<thread\> library. The magical project settings to avoid a cascade of errors in the library files could not be found. It possibly expects C++, not C.

## submodules
Note: Contains [submodules](https://www.geeksforgeeks.org/how-to-clone-git-repositories-including-submodules/). Use `git clone --recurse <url>` to clone.

I've found that this updates submodules:

1. `git submodule update --init --recursive` re-clones the files
2. `git submodule update --remote --recursive` points to the latest heads
3. `git status` shows if `--remote` made changes. If so,
4. `git add <filespec>` stages the change (use `git add --all` if living dangerously)
5. `git commit -m "Changed submodule head"` commits the change
5. `git push` pushes the commit

`mole` is a submodule containing crytographic primitives as submodules.
The rationale behind using submodules for these is that they can start as forks.

## code size

Some examples, `ok` vs `SwiftForth`.

| word  | ok<br>bytes | SwiftForth<br>bytes |
| ----- | --- | --- |
| star  | 6   | 12  |
| stars | 8   | 37  |
| total | 14  | 49  |

The `ok` ISA is 30% the code size of x86 in this example. This is without taking into account that `ok` encodes the most often-used Forth words as 5-bit opcodes.

### Swiftforth:
```
: star 42 emit ;
: stars 0 do star loop ;

see star
48769F   -4 [EBP] EBP LEA               8D6DFC
4876A2   EBX 0 [EBP] MOV                895D00
4876A5   2A # EBX MOV                   BB2A000000
4876AA   41657F ( EMIT ) JMP            E9D0EEF8FF ok

see stars
4876CF   -80000000 # EBX ADD            81C300000080
4876D5   EBX PUSH                       53
4876D6   EBX NEG                        F7DB
4876D8   0 # EBX ADD                    83C300
4876DB   EBX PUSH                       53
4876DC   0 [EBP] EBX MOV                8B5D00
4876DF   4 [EBP] EBP LEA                8D6D04
4876E2   48769F ( star ) CALL           E8B8FFFFFF
4876E7   0 [ESP] INC                    FF0424
4876EA   4876E2 JNO                     0F81F2FFFFFF
4876F0   8 # ESP ADD                    83C408
4876F3   RET                            C3 ok
```
### ok
```
: star 42 emit ;
: stars for star next ;

: star
0013 402A  2A lit
0014 7C05  5 APIcall-
0015 C000  ; nop nop nop
: stars
0016 8340  nop >r nop
0017 0013  13 call \ star
0018 6FFF  -1 next
0019 C000  ; nop nop nop
```
`test.f` demonstrates numeric output in 117 instructions. `dasm` shows:
```
: -
0001 4001  1 lit
0002 A024  swap inv +
0003 D000  ; + nop nop
: or
0004 8501  inv swap inv
0005 D820  ; and inv nop
: rot
0006 8368  nop >r swap
0007 83E8  nop r> swap
0008 C000  ; nop nop nop
: dnegate
0009 8501  inv swap inv
000A 4001  1 lit
000B 911C  + swap cy
000C D000  ; + nop nop
: dabs
000D 6C02  2 -if
000E 0009  9 call \ dnegate
000F C000  ; nop nop nop
: 0<
0010 6C02  2 -if
0011 E8A1  ; dup xor inv
0012 E8A0  ; dup xor nop
: s>d
0013 A800  dup nop nop
0014 2010  10 jump \ 0<
: 0=
0015 6802  2 if
0016 C240  ; nop 0 nop
0017 C241  ; nop 0 inv
: @+
0018 8DBD  a! @a+ a
0019 E000  ; swap nop nop
: 1+
001A 4001  1 lit
001B D000  ; + nop nop
: 1-
001C 8241  nop 0 inv
001D D000  ; + nop nop
: goodN
001E 001C  1C call \ 1-
001F 6C03  3 -if
0020 9FE7  drop r> drop
0021 C000  ; nop nop nop
0022 201A  1A jump \ 1+
: goodAN
0023 001C  1C call \ 1-
0024 6C03  3 -if
0025 9CFF  drop drop r>
0026 DC00  ; drop nop nop
0027 201A  1A jump \ 1+
: space
0028 4020  20 lit
0029 7C05  5 APIcall-
002A C000  ; nop nop nop
: spaces
002B 001E  1E call \ goodN
002C 8360  nop >r nop
002D 0028  28 call \ space
002E 6FFF  -1 next
002F C000  ; nop nop nop
: type
0030 0023  23 call \ goodAN
0031 8360  nop >r nop
0032 0018  18 call \ @+
0033 7C05  5 APIcall-
0034 6FFE  -2 next
0035 DC00  ; drop nop nop
: digit
0036 4009  9 lit
0037 8480  inv + nop
0038 6C03  3 -if
0039 4006  6 lit
003A 8480  inv + nop
003B 4041  41 lit
003C D000  ; + nop nop
: <#
003D 4800  800 lit
003E 4001  1 lit
003F CE80  ; a! !a nop
: hold
0040 4001  1 lit
0041 8D92  a! @a 0
0042 848A  inv + dup
0043 8283  nop !a a!
0044 C280  ; nop !a nop
: #
0045 4000  0 lit
0046 8D80  a! @a nop
0047 7808  8 APIcall
0048 0006  6 call \ rot
0049 0036  36 call \ digit
004A 2040  40 jump \ hold
: #s
004B 0045  45 call \ #
004C 8840  over over nop
004D 0004  4 call \ or
004E 0015  15 call \ 0=
004F 69FC  -4 if
0050 C000  ; nop nop nop
: sign
0051 0010  10 call \ 0<
0052 6803  3 if
0053 402D  2D lit
0054 0040  40 call \ hold
0055 C000  ; nop nop nop
: #>
0056 9CE0  drop drop nop
0057 4001  1 lit
0058 8D80  a! @a nop
0059 4800  800 lit
005A 8800  over nop nop
005B 2001  1 jump \ -
: s.r
005C 8800  over nop nop
005D 0001  1 call \ -
005E 002B  2B call \ spaces
005F 2030  30 jump \ type
: d.r
0060 836A  nop >r dup
0061 8360  nop >r nop
0062 000D  D call \ dabs
0063 003D  3D call \ <#
0064 004B  4B call \ #s
0065 83E0  nop r> nop
0066 0051  51 call \ sign
0067 0056  56 call \ #>
0068 83E0  nop r> nop
0069 205C  5C jump \ s.r
: u.r
006A 8248  nop 0 swap
006B 2060  60 jump \ d.r
: .r
006C 8360  nop >r nop
006D 0013  13 call \ s>d
006E 83E0  nop r> nop
006F 2060  60 jump \ d.r
: d.
0070 8240  nop 0 nop
0071 0060  60 call \ d.r
0072 2028  28 jump \ space
: u.
0073 8240  nop 0 nop
0074 2070  70 jump \ d.
```
