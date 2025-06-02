# USB UARTs

The UART typically has DTR and RTS.
If the USB interface uses a bus powered UART chip (like CH343) and a digital
isolator, TXD also delivers information: It is '0' when the USB cable is unplugged.

| Port state                     | RTS | DTR | TXD |
|--------------------------------|-----|-----|-----|
| USB cable is unplugged         | 0   | 0   | 0   |
| USB plugged in, not enumerated | 1   | 1   | 1   |
| COM port is closed             | 0   | 1   | 1   |
| COM port is open               | 0   | 0   | 1   |

While it is possible to make a system that lets you twiddle RTS and DTR to force 
the MCU into bootloader mode (how convenient), that is a cybersecurity no-no. 
You also don't know what quirks the host computer has that might accidentally
trigger a device reset. Some app could open the port and close it just to see if it's there.

A 2-in, 2-out digital isolator is cheap when the USB-chip side is bus-powered.
Isolating the USB cable eliminates ground loops and antenna effects.

## turnaround time

USB UARTs have propagation delays due to the limitations of USB and the non-real-time
nature of the OS (Windows, etc.). USBFS uses 1 ms frames, so fitting the CDC protocol
into these frames causes a delay. USBHS uses 8 usec frames, nice if you can afford it.

It's hard to find information online about the amount of delay to expect.
It depends on the system, the USB bridge chip, etc.
