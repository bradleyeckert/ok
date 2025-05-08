# target

The proof-of-concept target is a VM with a serial-port-based thin client (see `bci.c`). It uses `mole.c` to encrypt the serial port using a default key. Key management is not included.

## pc

Code::Blocks compiles it to a 42KB executable, so there is hope it will port to embedded systems based on ARM M-series or RISC-V.

Once compiled, optional command line arguments change the port and baud rate from their defaults.
For example, `./targ 4 115200` changes to port 4, 115200 baud. In `main.c`, use `#define TRACE 0` to inhibit the use of `printf` if the environment directs `printf` to the UART.

Once it is running, you can use `ok` to connect to it. In ok's console,

`port!` word sets the port.  
`remote` tries to connect to `targ` using the current port.  
`local` connects to ok's internal VM and closes the serial port.
