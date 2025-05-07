################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/DiskImg.c \
../src/cr_startup_lpc17.c \
../src/memory.c \
../src/mscuser.c \
../src/usbcore.c \
../src/usbdesc.c \
../src/usbhw.c \
../src/usbuser.c 

C_DEPS += \
./src/DiskImg.d \
./src/cr_startup_lpc17.d \
./src/memory.d \
./src/mscuser.d \
./src/usbcore.d \
./src/usbdesc.d \
./src/usbhw.d \
./src/usbuser.d 

OBJS += \
./src/DiskImg.o \
./src/cr_startup_lpc17.o \
./src/memory.o \
./src/mscuser.o \
./src/usbcore.o \
./src/usbdesc.o \
./src/usbhw.o \
./src/usbuser.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -DDEBUG -D__USE_CMSIS=CMSISv1p30_LPC17xx -D__CODE_RED -D__NEWLIB__ -I"/Users/piotrwlazlo/Desktop/studia/4semestr/SystemyWbudowane/lpc17xx_xpr_bb_140609/Lib_CMSISv1p30_LPC17xx/inc" -I"/Users/piotrwlazlo/Desktop/studia/4semestr/SystemyWbudowane/lpc17xx_xpr_bb_140609/Lib_MCU/inc" -O0 -g3 -Wall -c -fmessage-length=0 -fno-builtin -ffunction-sections -fmerge-constants -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m3 -mthumb -D__NEWLIB__ -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/DiskImg.d ./src/DiskImg.o ./src/cr_startup_lpc17.d ./src/cr_startup_lpc17.o ./src/memory.d ./src/memory.o ./src/mscuser.d ./src/mscuser.o ./src/usbcore.d ./src/usbcore.o ./src/usbdesc.d ./src/usbdesc.o ./src/usbhw.d ./src/usbhw.o ./src/usbuser.d ./src/usbuser.o

.PHONY: clean-src

