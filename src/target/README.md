# target

The BCI implements the VM. The VM is a tight interpreter that uses 10 to 12 local variables that the compiler might assign to registers. The compiler will assign VM internal state (VM registers, stacks) to RAM. As such, the VM state uses a `struct` named `vm_ctx`.

It would be nice to have a cheap FPGA suitable for a cryptography module. Such an FPGA would have writable (but non-volatile) key storage, a random number generator, and an encrypted bitstream. Until something like that comes along, a cyber-secure SoC would need an MCU+FPGA combo or be implemented with an MCU. The latter will be the best option for a while. Performance hotspots can be handled by C API calls.

The BCI implements a thin-client debug interface. In an FPGA-based system, the BCI would be in hardware.
