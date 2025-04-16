# host
***preliminary***

`ok` is a Forth based on lessons learned from `chad`, which were:

- Obfuscation is not encryption
- Master keys are not your friends
- A MISCish ISA is at least as good as Novix-style
- A thin client is fine for debugging

`ok` supports up to 64K (or whatever the host computer will support) independent VMs, each of which has its own dictionary and memory spaces. The number of VMs can be changed by re-compiling `ok`. There is one `quit` loop shared among the VMs. 

The ISA is implemented as a VM coded in C. The VM would be much faster in hardware, but as a tool for running MCU apps it is fast enough. MCU usage is on a single thread. When C code is blocked by hardware, the VM is stepped in a tight loop.

On the host side, this is not an option since the single thread gets blocked when waiting for `stdin` (cooked input from the terminal). `pthread` support solves this problem. Each VM lives separate, asynchronous thread. The VM thread spins the VM while waiting for a command message from the main thread. When it gets the message, it executes it and resumes spinning.

The simulator contains a BCI (Binary Command Interface) which is a thin-client debugger. If the BCI sees ASCII text, it writes it into a TIB (Text Input Buffer) at the top of RAM. This scheme removes the need to intermix compilation roles. Either the host or the target is the compiler.

When the host is the compiler, text input is parsed and interpreted (with headers on the host) with compiled words being executed on the target using a thin-client protocol (the BCI). When the target is the compiler, it has its own copy of the headers and its own `quit` loop.

There are several interpretation modes triggered by a `keyword`:

- `forth` Evaluate using backward header search
- `htrof` Evaluate using forward header search
- `term` Send raw text to the VM

The `term` keyword switches to raw mode. `+++` exits raw mode.

## dictionary memory spaces

The VM has data spaces for code, data, and an associated header space on the host.
Their sizes are set by `#define` to maximize constant folding to avoid slowing down the VM.
All VMs have the same amount of code and data memory, which initialize from block memory.

They also have a fixed number of private blocks. Block memory may consist of:

- Internal flash memory (for booting core 0)
- External flash memory (such as SPI NOR Flash)
- SDRAM

Within global block space, private blocks form a regular array. Blocks after the end are public, available to any VM. There is only one global block space. Private block access maps to the global block space.

# console exit codes

0 = okay
1 = app could not launch threads
