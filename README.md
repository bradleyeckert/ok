# ok
This Forth is designed for embedded systems development using a host PC, serial port connection, and a target system based on an MCU or FPGA. To address modern cybersecurity requirements, the serial connection is encrypted.

Status: Proof of concept works: Compiles and executes Forth. Execution is on a VM either within `ok` or on a remote [target](./target/PC) connected by serial ports.

`ok` contains a copy of the target VM(s) and a simulated serial port. The serial connection may be redirected to real target hardware.

Multiple VMs are supported via pthreads, so `ok` can simulate an array of Forth cores (one core per thread).
Some work would be required to simulate message passing between cores.
## compiling
`make all` under Linux. For Windows, see [windows/README](./windows/README.md).
## benchmarks
`mips` sleeps the main thread for 1/2 second while the VM fetches and executes instructions in its own thread.
Some approximate benchmarks:

| Processor | VM MIPS |
| --------- | ------- |
| AMD Ryzen 1950X | 150 |
| Intel Alder Lake N97 | 60 |

I was a little worried the VM would be slow. As it is, it looks like 25 clock cycles per VM instruction.
Branches are somewhat predictable and it's cache-friendly.

MIPS truly are meaningless in this case. All of the heavy lifting in a real application would be done by C.
Find a hot spot? Add it to the C API.

VM binary code size borders on the ridiculous, thanks to the frequency of 5-bit Forth instructions and Forth's
intrinsic leveraging of "don't care" states which other languages cannot exploit.
A little memory goes a long way, which is very useful in MCUs.
