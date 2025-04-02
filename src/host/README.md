# host
`ok` is a Forth based on lessons learned from `chad`, which were:

- Obfuscation is not encryption
- Master keys are not your friends
- A hybrid MISC ISA is basically as good as Novix-style
- A thin client is fine for debugging.

The ISA is implemented as a VM coded in C. The VM would be much faster in hardware, but as a tool for running MCU apps it is fast enough.

There may be multiple instances of the VM, to support multiprocessing.

I am still waiting for a cheap FPGA suitable for a cryptography module. Such an FPGA would have writable (but non-volatile) key storage, a random number generator, and an encrypted bitstream. Until something like that comes along, a cyber-secure SoC would need an MCU+FPGA combo or be implemented with an MCU. The latter will be the best option for a while.
