# target

The first target board for `ok` chosen for development is the [Nucleo-F722ZE](https://www.st.com/en/evaluation-tools/nucleo-f722ze.html). It is a popular and capable part suitable for cryptographic applications.

[Embeetle](https://embeetle.com/) is a good toolchain, but I had to use STM's https://embeetle.com/ to load the .hex file onto the board over the ST-LINK. The board also enumerates as Mass Storage that you can drag and drop the executable into, so you don't really need a programmer, just a compiler.

STM's free SDK is much larger than Embeetle. Download and install the following:

- [STM32CubeMX](https://www.st.com/content/st_com/en/stm32cubemx.html), a configurator tool for generating startup code.
- [STM32CubeIDE](https://www.st.com/content/st_com/en/stm32cubeide.html), a bloated Eclipse® C IDE.
- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html), a very nice programming tool.

I ended up sticking with Embeetle. It was very easy to get started. They give you a demo project that blinks LEDs. It is easy to modify and rebuild. The project here is a skeleton, does not do anything useful. It just sends text out USART3 at 921600,N,8,1. And it's 1/4 GB of files.
