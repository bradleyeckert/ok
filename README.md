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

`test.f` demonstrates numeric output in 117 instructions.

Some examples, `ok` vs `SwiftForth`.

| word  | ok<br>bytes | SwiftForth<br>bytes |
| ----- | --- | --- |
| star  | 6   | 12  |
| stars | 8   | 37  |
| total | 14  | 49  |

The `ok` ISA is 30% the code size of x86 in this example. This is without taking into account that `ok` encodes the most often-used Forth words as 5-bit opcodes.

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
