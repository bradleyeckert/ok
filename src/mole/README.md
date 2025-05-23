# Mole

Git's submodule support is extremely frustrating. Rather than include submodules in the project,
the files are copied directly from the source repos.
This is no problem since the repos have the same author and license.

What you do to synchronize files is:

- Clone the [mole](https://github.com/bradleyeckert/mole) and [ok](https://github.com/bradleyeckert/ok) repositories.
- Run the `molesync` batch file if on Windows. If you need a Linux script, write one.
