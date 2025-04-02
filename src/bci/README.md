# BCI

The BCI is a binary command interpreter that executes functions on a virtual machine (VM) that emulates a CPU.
The interface to the BCI is:

```C
void BCIhandler(vm_ctx ctx, const uint8_t *src, int length);
```

It processes a binary command `*src`.
If necessary, it waits until the VM is ready.
Although the protocol may support multiple commands per input string, only one command is processed.

The BCI is like a binary version of QUIT that returns throw codes. It interprets commands for memory access and execution, building on the [3-instruction Forth](https://pages.cs.wisc.edu/~bolo/shipyard/3ins4th.html) proposed by Frank Sergeant in 1991\. Since that time, computers have grown fast enough to simulate typical CPUs used in embedded systems. Rather than have different execution environments on host and target systems, Hermes duplicates them for binary compatibility. The dictionary is kept in the host for use by QUIT, but words can be executed on either side because the code images are kept in sync. Data space on the host side may similarly be synced to the target side, making it a clone of the target VM suitable for testing.

BCI data is 32-bit regardless of the cell size of the VM.

## BCI-VM handoff

`BCIhandler` steps the VM (CPU simulator) function until it returns nonzero. The ISA includes `vmret` to send that trigger if the VM is executing instructions. The VM hands off control to the BCI when the data stack is empty. The ways the VM can return nonzero are:

- An error occurred
- `vmret` returned a value
- The CPU is stopped

## Functions

| *Fn* | *Description* | *Parameters to BCI* | *Parameters from BCI* |
| :--- | :------------ | :------------------ | :-------------------- |
| 0 | Boilerplate      | | *n(1), data(n), ack(1)* |
| 1 | Execute word (xt) | *base(1), state(1), n(1), stack(n\*4), xt(4)* | *mark(1), base(1), state(1), m(1), stack(m\*4), ack(1)* |
| 2 | Read from memory | *n(1), addr(4)* | *n(1), data(n\*4), ack(1)* |
| 3 | Get CRC of memory | *n(2), addr(4)* | *CRC32(4), ack(1)* |
| 4 | Store to memory  | *n(1), addr(4), data(n\*4)* | *ack(1)* |
| 5 | Read register    | *id(4)* | *data(4), ack(1)* |
| 6 | Write register   | *id(4), data(4)* | *ack(1)* |

If a function fails, it will send a *nack* and the host will ignore the remaining data in the buffer.

**Fn 0: Read Boilerplate**

Read static boilerplate  
f(1): format \= 0  
hw\_rev(1): Hardware revision  
vendor(2): 0 \= generic  
model(2): Target model ID  
sw\_rev(2): Software revision  
timescale(4): timer ticks per second  
timer(8): Real-time up-counter

**Fn 1: Execute**

Execution starts with an empty stack and ends with an empty stack. The BCI:
  
* Pushes the data stack from the incoming data
* Executes the xt  
* Pops the data stack to the return message  

When a word is executed, if the xt is positive, it is a code address. Execution (or simulation) starts there and continues until the return stack is empty. If the xt is negative, the lower five bits are a single five-bit instruction to execute.

If the VM does not have a stack pointer, the BCI first fills the stack with "empty" tokens such as 0x55555555. After execution, the "empty" token indicates that the stack is empty. 

`Fn1` sets up the return message with `hermesSendInit`. Words like `emit` may append to the output with `hermesSendChar`. `Fn1` uses *mark* to mark the end of text returned by the function, sends parameters, and ends the response with `hermesSendFinal`. The API does not offer direct access to the UART, so encryption cannot be bypassed.

```C
void hermesSendInit(port_ctx *ctx, 0);
void hermesSendChar(port_ctx *ctx, uint8_t c);
void hermesSendFinal(port_ctx *ctx);
```

As long as the host has a nicely-sized rxbuf, it can handle long messages produced by executing `dump`, etc. The messages flowing over the UART are encrypted and authenticated.

**Fn 2: Read from data space**

Read a run of data from memory. The address range splits the memory into different types such as RAM, internal Flash, external Flash, peripherals, etc.

**Fn 3: Get CRC32 of data**

Similar to Fn 2 but returns the CRC32.

**Fn 4: Store to memory**

Store a run of data to memory using the same addressing as Fn 6 and Fn 7\. When writing to Flash (internal or external), writing to the first 256-byte page of a sector will pre-erase the sector. The number of bytes to be written may be less than 256, but the run must not cross page boundaries

**Fn 5: Read register**

Read from a VM register if possible.

**Fn 6: Write register**

Write to a VM register if possible. If the VM supports it, special registers are:

253: pause execution  
254: resume execution  
255: single-step

## BCI artifacts

| *Byte* | *Meaning* | *Parameters from BCI* |
| :----- | :-------- | :-------------------- |
| FFh    | POR       |  |
| FEh    | Ack       |  |
| FDh    | Nack      |  |
| FCh    | Command underflow      |  |
| FBh    | Throw                  | *data(4)* |
| FAh    | Write data to log file | *length (1), data (length)* |

Anything that isn’t an artifact is sent to stdout. Artifacts are numbered F8 to FFh, which are not used by UTF-8.

In a single-threaded system, artifacts would appear after Execute, so there is no need to handle them asynchronously with a separate task. Throw codes are looked up on the host side and output as text messages to stderr. The terminal can treat stderr differently than stdout, such as with a split pane.

## Tethered Forth

Classically, tethered Forths share a UART with a terminal. The BCI cannot do this the same way due to its buffered interface. Words like `emit` and `type`, when directed to the console, can't just plop bytes into a UART. `emit` appends a byte to a buffer instead. `emit` is an API function that uses static copies of `*ret` and `maxret` in the C function:

```C
void BCIhandler(vm_ctx ctx, const uint8_t *src, uint8_t *ret, uint16_t maxret);
```

Data inserted into the return buffer, by the function called by Fn1 "Execute word", appears before whatever is returned by Fn1. So, this user output is delineated by *ack* (FEh).

# VM

The BCI implements a VM that calls an underlying API (of C functions) for hardware access, etc. The VM interprets a negative *xt* as an API call. An API function can be invoked by a negative address in `call` or `jump`, or with a negative address popped off the return stack by `;`.

| *Inst* | *Positive xt*      | *Negative xt*                  |
| :----- | :----------------- | :----------------------------- |
| *call* | Push PC, PC = *xt* | Run API fn *-xt*               |
| *jump* | PC = *xt*          | Run API fn *-xt*, PC = R (pop) |
| *;*    | PC = R (pop)       | Run API fn -R (pop)            |

`BCIHW.c` contains the execution table and the `int BCIAPIcall(vm_ctx ctx, int xt)` function. `ctx` exposes the VM internals to the C API. The execution table must contain only trusted C functions. `xt` is the index into the execution table.

Code and data memories are in `vm_ctx`. They are:

```C
uint32_t DataMem[DATASIZE];
uint32_t CodeMem[CODESIZE];
```

Memory access functions in `BCIHW.c` are called when the addresses are above `DATASIZE` or `CODESIZE` respectively.
These are generally for I/O. In a multi-core system, `ctx` will be needed.

```C
int BCIVMioRead (vm_ctx ctx, uint32_t addr, uint32_t *x);
int BCIVMioWrite(vm_ctx ctx, uint32_t addr, uint32_t x);
int BCIVMcodeRead (vm_ctx ctx, uint32_t addr, uint32_t *x);
```

In each case, the return value is 0 if okay, a standard Forth [throw code](https://forth-standard.org/standard/exception) if not.

## The VM thread

The VM function has multiple operating modes:

- Single step, called by C code when it is waiting for I/O.
- Run then stop, called by the BCI to execute a function.

To prevent these two from conflicting (is the VM running or stopped?), the VM function itself outputs a trigger. When the BCI sees the trigger, the PC is invalid, or the VM is stopped, it may manipulate the stacks and execute a function. Otherwise, BCI spins (steps the VM). The BCI should have a time-out. If the VM is hung, the BCI should be able to stop the VM.

## Block Storage

Data in SPI NOR flash is handled in blocks. Each encrypted block uses 32 bytes for nonce and 16 bytes for HMAC, so a 1K block is 928 bytes of data and 48 bytes of overhead. Firmware stored in external SPI Flash is safe as long as the keys are private. Each firmware update gives the blocks new nonces.

The C API implements block memory access.

A common use of NOR Flash is bitmap fonts. Each glyph is on the order of 32 bytes. Some blocks should not use encryption so that the entire block does not have to be read for a small part of the data to be used. The HMAC is still useful, as it digitally signs the plaintext data.

## Data Memory

Peripherals are not mapped to data space because that would not be a secure sandbox. Instead, they should be accessed through the C API. However, for development, `DEBUGMODE` bit 0 is set to enable access to peripherals to aid in deciphering the MCU reference manual. Bit 1 is set to enable access to code memory, which is normally unreadable. Production code would have `DEBUGMODE` set to 0.

A 22-bit address space is split into 16 regions, each with 18-bit offset.
The 4-bit index is into a table of 32-bit base addresses. Invalid table entries are 0.
The resulting re-mapping for a CH32V3x gives:

| *Addr* | *Base Address* | *For* | *DEBUGMODE bit* |
| :----- | :------------- | :---- | :-------------- |
| 000000 | DataMem  | Data Memory portion of SRAM  |  |
| 040000 | CodeMem  | Code memory (read if enabled)| 0 |
| 080000 | 08000000 | Code Flash absolute      | 1 |
| 0C0000 | 1FFF8000 | System Flash absolute    | 1 |
| 100000 | 20000000 | SRAM                     | 1 |
| 140000 | 40000000 | Peripherals              | 1 |
| 180000 | 50000000 | Obscure Peripherals      | 1 |
| 1C0000 | 60000000 | FSMC bank 1              | 1 |
| 200000 | 70000000 | FSMC bank 2              | 1 |
| 240000 | A0000000 | FSMC register            | 1 |
| 280000 | E0000000 | Core private peripherals | 1 |

## Code Memory

Code memory is in SRAM. It is accessed directly by `bci.c`.
A block of cache memory, loaded from the related NOR flash block, enables arbitrarily large code space.
Cache misses cause the SRAM cache to be overwritten from SPI flash.
Manual lookahead allows the software to spin or `pause` while the cache is filled by hardware.
8K cycles of 8 MHz SCLK takes 1 ms, a long time to wait for a cache miss.
Used properly, manual lookahead avoids blocking when the cache is missed.
The API provides the cache status.
`BCIVMcodeRead` in `bciHW.c` implements the cache as well as the API.
`DEBUGMODE` bit 2 enables a "cache miss" message in the simulator.

For each 1024-byte page of RAM, the last 48 bytes must be unused. They are reserved for AEAD overhead.
The compiler enforces this rule.

MCU internal flash is not used for code memory, with the rationale that a future SoC implementing the VM will not need internal flash, just a small amount of MTP for keys.

Software updates are outside the scope of the BCI. Part of the SPI Flash is used to stage software updates. Upon startup, the boot code checks revision numbers and applies the update if necessary. The BCI does, however, have access to the staging area for delivery of (encrypted) software updates.

# ISA

