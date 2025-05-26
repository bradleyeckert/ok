# target reference implementation

The proof-of-concept target is a VM with a serial-port-based thin client (see `bci.c`). It uses `mole.c` to encrypt the serial port using a default key. Key management is not included.

## compiling

You must define BCI_TRACE in the project to get console output.
The gcc flag would be `-DBCI_TRACE`.

The rightmost of the bottom set of tabs in Code::Blocks compiler settings is "\#defines".

If you are using the makefile, `make clean` before you `make all` if you previously made `ok`.

## running

Once compiled, optional command line arguments change the port and baud rate from their defaults.
For example, `./targ 4 115200` changes to port 4, 115200 baud.
ports are numbered starting from 0, so they are off-by-1 in Windows.
`targ` lists available ports.

The [com0com](https://sourceforge.net/projects/com0com/) Null-modem emulator allows you to run `ok` and `targ`
in different terminals on the same PC. On Linux, use something like `socat PTY,link=/dev/ttyS14 PTY,link=/dev/ttyS15`.
com0com (the virtual null-modem cable) blocks RS232_SendByte if you send it data and nothing is on the other end.
It will wait for the other end to connect. Connect the target first to avoid a timeout error in `ok`.

Set (or drag) the terminal width to a multiple of 3 columns to get a cleaner display.

Once it is running, you can use `ok` to connect to it. In ok's console,

`com-list` lists the serial ports in the system  
`port!` word sets the port.  
`remote` tries to connect to `targ` using the current port.  
`local` connects to ok's internal VM and closes the serial port.
