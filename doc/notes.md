# Implementation notes

## RS232
FLASH_BLOCK_SIZE needed to be defined as 128 for RS232_SendByte to work. Otherwise, it would drop characters.
I do not know whether it was a problem with com0com.
The RS232 is read from and written to using the same thread, although opened and closed from a different thread.

The reference target, compiled with BCI_TRACE defined, shows this plainly. The red raw data from the PC is missing data.
Turning off "emulate baud rate" in com0com (there are setup utilities for this) fixed it.
With physical UARTs, you should be able to use a larger FLASH_BLOCK_SIZE to decrease Reload times.

### whither baud rates

Different USB UARTs support different baud rates. The CH343 supports arbitrary baud rates.
Implementation details are not available, but one can guess that the baud rate is derived from 48 MHz.
FTDI chips can have a 12M/n baud rate. 921600 fits okay here.

UART chips have a built-in propagation delay. The time to send 280 bytes at 921600 BPS is 3ms.
The USB delay shouldn't be too bad.
The intended FLASH_BLOCK_SIZE of 1024, if it works, would have a transmission time of 3.5 ms at 3MBPS.
Still not bad, and compatible with a CH343.

## Key management

`ok` should be able to use a proxy, or be a proxy, to manage keys. Each target has a firmware key and an access key.
Targets are intended to be single-chip MCUs that store their keys in internal Flash.
In the case of an MCU with only one Flash sector (STM32H750), a blank region of Flash would be reserved for
re-keying a limited number of times (until it fills up) since it cannot be erased.

The keys are unique per device. Three items need to be inserted into the firmware image before flashing:

- A unique serial number
- A 64-byte boot key, consisting of a 32-byte key, 16-byte admin passcode, and 16-byte HMAC
- A 64-byte access key for each serial port
- Public keys that go with the private keys

ECDH, such as secp256k1, uses a 256-bit (32-byte) private key and a 64-byte (33-byte compressed) public key.
Re-keying the device can be done securely using ECDH.
