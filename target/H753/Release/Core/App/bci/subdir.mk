################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/App/bci/bci.c \
../Core/App/bci/bciHW.c \
../Core/App/bci/vm.c 

OBJS += \
./Core/App/bci/bci.o \
./Core/App/bci/bciHW.o \
./Core/App/bci/vm.o 

C_DEPS += \
./Core/App/bci/bci.d \
./Core/App/bci/bciHW.d \
./Core/App/bci/vm.d 


# Each subdirectory must supply rules for building sources it contributes
Core/App/bci/%.o Core/App/bci/%.su Core/App/bci/%.cyclo: ../Core/App/bci/%.c Core/App/bci/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DUSE_PWR_LDO_SUPPLY -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32H753xx -DMOLE_ALLOC_MEM_UINT32S=512 -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/BSP/STM32H7xx_Nucleo -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-App-2f-bci

clean-Core-2f-App-2f-bci:
	-$(RM) ./Core/App/bci/bci.cyclo ./Core/App/bci/bci.d ./Core/App/bci/bci.o ./Core/App/bci/bci.su ./Core/App/bci/bciHW.cyclo ./Core/App/bci/bciHW.d ./Core/App/bci/bciHW.o ./Core/App/bci/bciHW.su ./Core/App/bci/vm.cyclo ./Core/App/bci/vm.d ./Core/App/bci/vm.o ./Core/App/bci/vm.su

.PHONY: clean-Core-2f-App-2f-bci

