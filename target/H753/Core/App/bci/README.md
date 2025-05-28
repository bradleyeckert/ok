# BCI

The BCI is a binary command interpreter that executes functions on a virtual machine (VM) that emulates a CPU.
The interface to the BCI is:

```C
void BCIhandler(vm_ctx ctx, const uint8_t *src, int length);
```

It processes a binary command `*src`.
If necessary, it waits until the VM is ready.
Although the protocol may support multiple commands per input string, only one command is processed.

The BCI is like a binary version of QUIT that returns throw codes. It interprets commands for memory access and execution, building on the [3-instruction Forth](https://pages.cs.wisc.edu/~bolo/shipyard/3ins4th.html) proposed by Frank Sergeant in 1991\. Since that time, computers have grown fast enough to simulate typical CPUs used in embedded systems. Rather than have different execution environments on host and target systems, Hermes duplicates them for binary compatibility. The dictionary is kept in the host for use by QUIT, but words can be executed on either side because the code images are kept in sync. Data space on the host side may similarly be synced to the target side, making it a clone of the target VM suitable for testing.

BCI data is 32-bit regardless of the cell size of the VM.

CPUCORES is the number of separate VM instances and threads.

`main.c` instantiates the VMs and their threads. The main thread, which runs in `quit.c`, sends commands to the BCI of the selected VM. The command flows through an encrypted channel to be received by `BCIhandler` in `bci.c`. The `BCIhandler` executes the command and may step the VM to execute code or instructions. It sends a response back to the host with `BCIsendToHost` in `comm.c`, which encrypts the response with `moleSend`. After decryption, the plaintext response is sent to `BCIreceive` where it is interpreted to finish up the BCI operation. 

The point of this rather complex communication chain is to access the VM only through byte streams. The streams are intended to be redirected to RS232 to operate a VM running on a remote target device such as an MCU. `ok` then operates as a tethered Forth with an encrypted thin client on the MCU. The UART is effectively "locked down" per modern cybersecurity guidelines, but can be used for system access, debugging, and updates if the correct keys are obtained.
