## caveats
FLASH_BLOCK_SIZE needed to be defined as 128 for RS232_SendByte to work. Otherwise, it would drop characters.
I do not know whether it was a problem with com0com.
The RS232 is read from and written to using the same thread, although opened and closed from a different thread.

The reference target, compiled with BCI_TRACE defined, shows this plainly. The red raw data from the PC is missing data.
Turning off "emulate baud rate" in com0com (there are setup utilities for this) fixed it.
With physical UARTs, you should be able to use a larger FLASH_BLOCK_SIZE to decrease Reload times.
