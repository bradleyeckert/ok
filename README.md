# ok
This Forth is designed for embedded systems development using a host PC, serial port connection,
and a target system based on an MCU or FPGA.

`ok` contains a copy of the target VM(s) and a simulated serial port. The serial connection may be redirected to real target hardware.

Status: Works on with **ok** connected to:

- An internal target VM
- An external [ref](./target/ref) target VM through a Virtual Null-Modem (com0com)
- A [NUCLEO-H753ZI](./target/ref/H753) target VM through a USB cable

Multiple VMs are supported via pthreads, so `ok` can simulate an array of Forth cores (one core per thread).
Some work would be required to simulate message passing between cores.
## compiling
`make all` under Linux. For Windows, see [windows/README](./windows/README.md).
## benchmarks
After the command line `ok include test.f` launches the `ok>` prompt, `mips` sleeps the main thread
for 1/2 second while the VM fetches and executes instructions in its own thread.
Some approximate benchmarks (a little unpredictable on a PC) when compiled with Code::Blocks 20.03:

| Processor | VM MIPS | CPU Usage |
| --------- | ------- | --------- |
| AMD Ryzen 1950X | 120 | 6% |
| Intel Alder Lake N97 | 50 | 40% |

MIPS truly are meaningless in this case. All of the heavy lifting in a real application would be done by C.
Find a hot spot? Add it to the C API.

VM binary code size borders on the ridiculous, thanks to the frequency of 5-bit Forth instructions and Forth's
intrinsic leveraging of "don't care" states which other languages cannot exploit.
A little memory goes a long way, which is very useful in MCUs.
## snooping
COM port snooping is supported by the [ref](./target/ref) target.
There are also commercial apps that snoop traffic on your PC's COM ports.
Either will show the encrypted traffic flowing across the serial connection.
## UART hardening
To address modern cybersecurity requirements, the serial connection is encrypted.
The thin client is inspired by that of Forth Inc's `SwiftX` but tuned to support a secure UART.
UART hardening prevents UART snooping and/or tampering. 
The security is only as good as key management.
Key management is outside the scope of `ok`.

Each thin client should have unique keys installed via JTAG using `STM_Programmer_CLI.exe`.
In practice, these keys would be held in escrow and/or kept on a secure server.
Key access should require access to multiple different servers, each serving part of the key.
As long as keys are not leaked, the UART does not function as a hardware backdoor.
The thin client stays in production firmware to provide the manufacturer emergency and maintenance access.

