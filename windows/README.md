# Compiling for Windows

`ok` is a Windows console application written mostly in C99.
`main.cpp` uses C11++ for threading.

There are at least three threads. They are:

- The main execution thread, which is blocked when waiting for stdin.
- A thread to process incoming RS232 data.
- A thread to execute code on the VM.

## predefined macros

You can define BCI_TRACE in the project to display traffic.

## Visual Studio

The Visual Studio 2022 `ok.sln` uses a C++ version of `main`.
`main.cpp` assumes the use of Windows libraries, including `<thread>`.

## makefile

You may be able to run the makefile, just like with Linux, under Windows. I could not.
Maybe the wrong version of `make` was in the path.

## Code::Blocks

Code::Blocks works under Windows after creating a project and adding files recursively.

`libwinpthread-1.dll` may be needed by the `ok.exe` compiled by Code::Blocks, regardless of instructions to compile static libraries.
I found several DLLs on my computer named libwinpthread-1.dll. I tried one from 2021, it did not work. The one from 2016 worked.
The working DLL is included in the repo in case Windows complains.
