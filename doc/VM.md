# VM ISA

Parameter passing is implicit in a stack computer. Stack code exploits “don’t care” states so that executable code has high semantic density whether graph reduction occurs at edit time or compile time. Phil Koopman explored this in “Stack Computers \- the new wave” ([1989](https://users.ece.cmu.edu/~koopman/stack_computers/index.html)). Other anecdotal evidence bears this out. For any given application, the Forth version compiles to a significantly smaller memory footprint than the C version. This is basically the result of information theory. Stack code has less entropy (classic H). Less noise means better compression.

Almost all work on ISAs is with the objective of providing a backend to Clang or GCC. With the advent of RISC-V, the canon seems practically closed. However, until any other language-ISA combination can offer lower H than Forth-Forth, the pioneers must ride forward with arrows in their backs.

The ISA would best be implmented in hardware, but it can be simulated on any CPU.
The value proposition of implementing the VM in an MCU-based system is:

- The application runs in a sandbox, so it can't tamper with C code.
- The Forth-coded application is much smaller than its C equivalent, allowing more features.
- Interpreter overhead is overcome by putting time-critical functions in a C API.

Forth is executed directly by the ISA, which features:

* 16- to 32-bit (typically 32-bit) data size   
* 16- to 32-bit (typically 16-bit) instruction width  
* A hardware data stack, typically 18-deep  
* A hardware return stack, typically 17-deep  
* A high-speed (3x or 4x clock) MISC execution core  
* Normal-speed (one per clock) support instructions

The proposed ISA features microinstructions that execute 5-bit µops at a rate multiple times the system clock. In practice, `+` would insert wait states into the FSM that implements the high-speed core to allow time for the ripple-carry adder to settle. The timing constraints (.sdc) file would give the adder multiple cycles to settle. So, no `nop` is needed before `+`.

The µops are:

| \\  | *0* | *1* | *2* | *3* | *4* | *5* | *6* | *7* |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | 
| *0* | nop   | inv | over | a!  | +   | xor | and | drop |  
| *1* | swap  | 2*  | dup  | cy! | @a  | @a+ | @b  | @b+  |  
| *2* | 2/c   | 2/  |      | u!  | !a  | !a+ | !b  | !b+  |  
| *3* | unext |     | >r   | u   | cy  | a   | r@  | r>   |

The instruction size is 16 to 31 bits, \#defined in the VM as VM\_INSTBITS. 16 to 20 is the most efficient value. The processor is programmed in Forth. There is no assembler, no analytical compiler. There is a direct correspondence between source tokens (blank-delimited strings) and object code (5-bit µops or 16-bit instructions).

**16-bit:**

| *Name* | *15:13* | *12:0* |
| :----- | ------- | ------ |
| call | 000  | 13-bit address, push PC to return stack   |
| jump | 001  | 13-bit address |
| lit  | 010  | 13-bit literal (push onto data stack) |
| imm  | 011  | 4-bit opcode, 9-bit immediate data |

| *Name* | *15* | *14* | *13:10* | *9:5* | *4:0* |
| :----- | ---- | ------ |------ |------ |------ |
| micro  | 1    | ;    | µop 0   | µop 1 | µop 2 |

**20-bit:**

| *Name* | *19:17* | *16:0* |
| :----- | ------- | ------ |
| call | 000  | 17-bit address, push PC to return stack   |
| jump | 001  | 17-bit address |
| lit  | 010  | 17-bit literal (push onto data stack) |
| imm  | 011  | 4-bit opcode, 13-bit immediate data |

| *Name* | *19* | *18* | *17:15* | *14:10* | *9:5* | *4:0* |
| :----- | ---- | ------ | ------ | ------ | ----- | ----- |
| micro  | 1    | ;    | µop 0   | µop 1   | µop 2 | µop 3 |

In a micro instruction, when the **`;`** bit is set, PC is popped off the return stack when µop 0 executes. With 16-bit or 20-bit instructions, µop 0 is truncated to either 4 or 3 bits so there is no possible conflicting use of the return stack. Control flow change proceeds at the system clock while the µops execute at a multiple of the system clock.

The data address comes from a single address register A. After a memory operation, A may be incremented, exchanged with B, exchanged with B and incremented, or left alone: @a+, @b, @b+, @a. Each fetch returns the value on the data bus after A is changed. Fetches may insert wait states to allow synchronous-read memory to settle after A is changed. This is an implementation detail that allows the programmer to assume that `@a` may be used immediately after `a!`. Wait states would be inserted to allow read and write of slower data memory rather than limiting the instruction rate to the memory speed.

The **u** register is for the upper data bus bits in systems where the bus size is less than the cell size. For example, AXI4 is 32-bit. A core with 20-bit cells would need a 12-bit **u**. There is no **u** register if cells are 32-bit.

## Imm instructions

Imm instructions have a 4-bit opcode and (VM\_INSTBITS \- 7\) bits of immediate data. They are used for branches, etc. Being in the “slow” clock domain, there is plenty of time for the adder to settle when producing the next code address. For a 16-bit instruction, that would be:

| *Name* | *12:9* | *8:0* |
| :---- | :--- | :--- |
| pfx | 0 | Prefix: lex \= (lex\<\<9) + u9 |
| zoo | 1 | Other instruction selected by u9 |
| ax | 2 | A \= X \+ u9 |
| ay | 3 | A \= Y \+ u9 |
| if | 4 | PC \= PC \+ s9 if T=0 |
| bran | 5 | PC \= PC \+ s9 |
| \-if | 6 | PC \= PC \+ s9 if T \>= 0 |
| next | 7 | PC \= PC \+ s9 if R \> 0 else drop R | 
| APIcall | 12 | Call API function in VM, no stack change |
| APIcall+ | 13 | Call API function in VM, dup before call |
| APIcall- | 14 | Call API function in VM, drop top of stack after call |
| APIcall– | 15 | Call API function in VM, 2drop after call |

The lex register supplies upper bits for literals. It is (cellsize-13) bits wide. The compiler will usually place an “inv” in slot 0 to support negative literals. The lex register may also supply upper address bits for call and jump.

Zoo instructions include:

| *Name* | *9:0* |
| :---- | ----- |
| x\! | 0 | X \= T, drop T |
| y\! | 1 | Y \= T, drop T |
| throw | 2 | VM returns ior \= T |

## Return stack instructions

The **`;`** bit is set to pop P from the return stack while µop 0 executes. When ending a definition, the compiler sets **`;`** when possible by ensuring that none of the µops are **`>r`**. If all else fails, it compiles **`;`** along with all nops.

`>r     ( x – )`    Push to return stack  
`r>     ( – x )`    Pop from return stack  
`r@     ( – x )`    Top of return stack  
`next   ( – )`      If R is non-zero, branch and decrement R. Otherwise pop R.  
`unext  ( – )`      If R is non-zero, go to µop 0 and decrement R. Otherwise pop R.

## Memory instructions

**`a    ( – x )`**  Get `a` register.  
**`a!   ( x – )`**  Set `a` register.  
`!a     ( x – )`    Store x to DataMem\[a\]  
`!a+    ( x – )`    Store x to DataMem\[a\] and increment `a` by “1 cells”  
`@a     ( – x )`    Fetch from DataMem\[a\]  
`@a+    ( – x )`    Fetch from DataMem\[a\] and increment `a` by “1 cells”

## Carry instructions

`cy     ( – carry )`    Fetch carry bit  
`cy!    ( carry – )`    Store carry bit  
`2*c    ( x – y )`      Rotate T left through carry  
`2/c    ( x – y )`      Rotate T right through carry

## ALU and stack instructions

`over   ( x y – x y x )` Fetch S (non-destructive)  
`dup    ( x – x x )`    Create a working copy of T  
`drop   ( x – )`        Discard T  
`swap   ( x y – y x )`  Swap S and T  
`inv    ( x – ~x )`     One's complement T  
`2*     ( x – y )`      Shift T left; shift 0 into bit 0; save sign in carry  
`2/     ( x – y )`      Shift T right; sign bit unchanged; save bit 0 in carry  
`+      ( x y – x+y )`  Add S to T; save the carry  
`xor    ( x y – x^y )`  Exclusive Or S with T  
`and    ( x y – x&y )`  And S with T  
`nop    ( – )`          No operation

The \+ instruction may take time to settle due to carry propagation. This is the reason there is no negate or 1+ instruction: carry into the adder must not be dependent on µop decoding.

## API calls

When the VM lives in a C-based system, there will be a lot of middleware, such as file systems, communications, encryption, etc. These libraries are accessed through APIs. API calls are made through an execution table, a table of function pointers.

## Memories

Address units are cells. Chars are also cells. While not as efficient as byte packing, wide characters are easier to work with than UTF-8.

Typical memory spaces are:

| *Name* | *width* | *cells* | *data<br>read* | *data<br>write* | *implementation* | *address<br>begin* | *host<br>copy* |
| :----- | :-----: | :-----: | :--------: | :--------: | :--------------- | :--------------- | :--------------: |
| Code   | 16-bit  | 8K      | no         | no         | Internal Flash   | 00000000h | yes |
| Data   | 32-bit  | 4K      | yes        | yes        | SRAM             | 00000000h | no |
| Text   | 32-bit  | 8K      | yes        | no         | Internal Flash   | TEXTORIGIN | yes |
| Peripherals | 32-bit | 1G  | yes        | yes        | Peripherals      | 01000000h | no |

Code and Text are writable via BCI (secure UART) commands only. The host has local copies of these, which are loaded onto the target by `reload`.

STM32-type MCUs have byte range 0x00000000 to 0x07FFFFFF free since that range is aliased elsewhere.
The CH32V303 looks like a nice part, but it boots from a SPI Flash die in the package.
That's too accessible for anyone with a chemistry lab and a probe jig.
A pin-compatible part, with on-chip Flash, is the STM32F722.
The STM32F722 can run at 216 MHz, drawing 180 mA (0.6W at 3.3V) at that speed.

Peripheral space is somewhat sparse. Peripherals need a 30-bit address. The `ax` or `ay` instruction can be exploited to address cells within a peripheral, since a 30-bit literal would need three 16-bit instructions.

Fetch and Store opcodes decode address ranges at runtime as follows:

- If the address is below 00001000h, it is SRAM, else
- If the address is below 01000000h, it is Internal Flash, else
- It is a peripheral.

The STM32F722 uses large flash sectors of 16KB, 64KB, or 128 KB. `reload` erases and re-programs an entire sector. Sector erase proceeds at about 20 µs/byte. With a UART baud rate of 921600, the programming rate including decryption is about 20 µs/byte. Supposing `reload` updates an entire 16KB sector, it will take about 0.65 seconds. Flash write endurance is rated at 10K cycles. Data retention is longer at 1K cycles. The CRC is checked before re-flashing to avoid unnecessary cycles.

Flashing is done FLASH_BLOCK_SIZE bytes at a time. A 512-byte block should program in 10 ms not including erase time. The 2 ms overhead of USB-serial can be ignored.

## SPI Flash

External SPI Flash would be accessed through the C API. The VM reads plaintext data using streaming primitives that match SPI operation.

Encrypted blocks have a 32-byte preamble, data, and a 16-byte HMAC. The length of the data is about 6% longer than the binary payload. A 4KB block can safely hold 800 cells of data. A block of 800 cells should encrypt or decrypt and authenticate in 1/2 ms. The C API for this runs in the background, transferring the block to or from a 4KB sector while stepping the VM when delaying after each page write or sector erase.
