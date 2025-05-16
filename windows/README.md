# Compiling for Windows

`ok` is a Windows console application written mostly in C99.
`main.c` may use some C11 for threading.

There are at least three threads. They are:

- The main execution thread, which is blocked when waiting for stdin.
- A thread to process incoming RS232 data.
- A thread to execute code on the VM.

## Code::Blocks

Code::Blocks works under Windows after manually adding files.
Add files recursively, then remove all files with a `main` function, except main.c.

`libwinpthread-1.dll` may be needed by the `ok.exe` compiled by Code::Blocks, regardless of instructions to compile static libraries. I found several DLLs on my computer named libwinpthread-1.dll. I tried one from 2021, it did not work. The one from 2016 worked. What good is a DLL if upgrades to the DLL break existing code? To prevent such headaches, the working DLL is included in the repo in case you get a complaint.

## Visual Studio

I could not make Visual Studio 2022 work with either pthreads or the Windows \<thread\> library. The magical project settings to avoid a cascade of errors in the Windows library files could not be found. It possibly expects C++, not C.

## makefile

You may be able to run the makefile, just like with Linux, under Windows. I could not.
Maybe the wrong version of `make` was in the path.
