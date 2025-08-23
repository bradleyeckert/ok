################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/App/mole/blake2s.c \
../Core/App/mole/mole.c \
../Core/App/mole/xchacha.c 

OBJS += \
./Core/App/mole/blake2s.o \
./Core/App/mole/mole.o \
./Core/App/mole/xchacha.o 

C_DEPS += \
./Core/App/mole/blake2s.d \
./Core/App/mole/mole.d \
./Core/App/mole/xchacha.d 


# Each subdirectory must supply rules for building sources it contributes
Core/App/mole/%.o Core/App/mole/%.su Core/App/mole/%.cyclo: ../Core/App/mole/%.c Core/App/mole/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/BSP/STM32H7xx_Nucleo -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-App-2f-mole

clean-Core-2f-App-2f-mole:
	-$(RM) ./Core/App/mole/blake2s.cyclo ./Core/App/mole/blake2s.d ./Core/App/mole/blake2s.o ./Core/App/mole/blake2s.su ./Core/App/mole/mole.cyclo ./Core/App/mole/mole.d ./Core/App/mole/mole.o ./Core/App/mole/mole.su ./Core/App/mole/xchacha.cyclo ./Core/App/mole/xchacha.d ./Core/App/mole/xchacha.o ./Core/App/mole/xchacha.su

.PHONY: clean-Core-2f-App-2f-mole

