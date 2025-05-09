# ok
Another C-based Forth

Status: Proof of concept works: Compiles and executes Forth. Execution is on a VM either within `ok` or on a remote [target](./target/PC) connected by serial ports.

This Forth is designed for embedded systems development using a host PC, serial port connection, and a target system based on an MCU or FPGA. To address modern cybersecurity requirements, the serial connection is encrypted.

`ok` contains a copy of the target VM(s) and a simulated serial port. The serial connection may be redirected to real target hardware.

Multiple VMs are supported via pthreads, so `ok` can simulate an array of Forth cores (one core per thread).
Some work would be required to simulate message passing between cores.


## compiling

`make all` under Linux

Code::Blocks works under Windows after manually adding files. Add files recursively, then remove all files with a `main` function, except main.c.

I could not make Visual Studio 2022 work with either pthreads or the Windows \<thread\> library. The magical project settings to avoid a cascade of errors in the library files could not be found. It possibly expects C++, not C.

## submodules
Note: Contains [submodules](https://www.geeksforgeeks.org/how-to-clone-git-repositories-including-submodules/). Use `git clone --recurse <url>` to clone.

I've found that this updates submodules:

1. `git submodule update --init --recursive` re-clones the files
2. `git submodule update --remote --recursive` points to the latest heads
3. `git status` shows if `--remote` made changes. If so,
4. `git add <filespec>` stages the change (use `git add --all` if living dangerously)
5. `git commit -m "Changed submodule head"` commits the change
5. `git push` pushes the commit

`mole` is a submodule containing crytographic primitives as submodules.
The rationale behind using submodules for these is that they can start as forks.
