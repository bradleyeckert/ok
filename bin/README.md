# Binary executables

Windows console apps:

- `ok.exe` CLI dev tool for the Forth application
- `freeglut.dll` is used by `ok`
- `blobcmp.exe` CLI-based blob amalgamator utility

Windows batch files:

- `makeflash.bat` Creates `spiflash.bin` from blobs

## Blobs

`ok` initializes its simulated SPI Flash from `spiflash.bin`.

| file         | created by        |
|--------------|-------------------|
| fontblob.bin | utilities/fontgen |
| vmblob.bin   | test.f `saveblob` |

## Firmware updates

`spiflash.bin` is also used as the basis for update files.
Utilities can use it in the following ways:

- Convert `spiflash.bin` to an encrypted and signed update file.
- Add HMACs to `spiflash.bin` and program the SPI Flash directly.

Encryption enlarges the blobs by about 3%, so the blob should be padded
if such expansion would cross into the next 64KB sector of flash.
