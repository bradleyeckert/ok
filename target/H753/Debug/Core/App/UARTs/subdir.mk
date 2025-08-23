################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/App/UARTs/okuart.c 

OBJS += \
./Core/App/UARTs/okuart.o 

C_DEPS += \
./Core/App/UARTs/okuart.d 


# Each subdirectory must supply rules for building sources it contributes
Core/App/UARTs/%.o Core/App/UARTs/%.su Core/App/UARTs/%.cyclo: ../Core/App/UARTs/%.c Core/App/UARTs/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/BSP/STM32H7xx_Nucleo -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-App-2f-UARTs

clean-Core-2f-App-2f-UARTs:
	-$(RM) ./Core/App/UARTs/okuart.cyclo ./Core/App/UARTs/okuart.d ./Core/App/UARTs/okuart.o ./Core/App/UARTs/okuart.su

.PHONY: clean-Core-2f-App-2f-UARTs

