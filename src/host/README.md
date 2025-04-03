# host
***preliminary***

`ok` is a Forth based on lessons learned from `chad`, which were:

- Obfuscation is not encryption
- Master keys are not your friends
- A hybrid MISC ISA is basically as good as Novix-style
- A thin client is fine for debugging
- Feature sprawl is hard to resist

The ISA is implemented as a VM coded in C. The VM would be much faster in hardware, but as a tool for running MCU apps it is fast enough. MCU usage is on a single thread. When C code is blocked by hardware, the VM is stepped in a tight loop.

On the host side, this is not an option since the single thread gets blocked when waiting for `stdin` (cooked input from the terminal). C11's thread support solves this problem. Each VM lives separate, asynchronous thread. The VM thread spins the VM while waiting for a command message from the main thread. When it gets the message, it executes it and resumes spinning.

The simulator contains a BCI (Binary Command Interface) which is a thin-client debugger. If the BCI sees ASCII text, it writes it into a TIB (Text Input Buffer) at the top of RAM. This scheme removes the need to intermix compilation roles. Either the host or the target is the compiler.

When the host is the compiler, text input is parsed and interpreted (with headers on the host) with compiled words being executed on the target using a thin-client protocol (the BCI). When the target is the compiler, it has its own copy of the headers and its own `quit` loop.

Search order, wordlists, and vocabularies not supported. The host dictionary is one linear list. Each header is a fixed-size structure. A string stack holds variable-length strings for use by headers. Each used header `struct` is initialized at startup.

There are several interpretation modes:

- Execute on the VM using backward header search (Forth-like)
- Execute on the VM using forward header search
- Send raw text to the VM

The `term` keyword switches to raw mode. `+++` exits raw mode.
