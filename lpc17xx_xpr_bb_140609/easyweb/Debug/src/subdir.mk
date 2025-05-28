################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/cr_startup_lpc17.c \
../src/easyweb.c \
../src/ethmac.c \
../src/ew_systick.c \
../src/tcpip.c 

C_DEPS += \
./src/cr_startup_lpc17.d \
./src/easyweb.d \
./src/ethmac.d \
./src/ew_systick.d \
./src/tcpip.d 

OBJS += \
./src/cr_startup_lpc17.o \
./src/easyweb.o \
./src/ethmac.o \
./src/ew_systick.o \
./src/tcpip.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -DDEBUG -D__USE_CMSIS=CMSISv1p30_LPC17xx -D__CODE_RED -D__NEWLIB__ -I"/Users/piotrwlazlo/Desktop/studia/4semestr/SystemyWbudowane/lpc17xx_xpr_bb_140609/Lib_CMSISv1p30_LPC17xx/inc" -I"/Users/piotrwlazlo/Desktop/studia/4semestr/SystemyWbudowane/lpc17xx_xpr_bb_140609/Lib_EaBaseBoard/inc" -I"/Users/piotrwlazlo/Desktop/studia/4semestr/SystemyWbudowane/lpc17xx_xpr_bb_140609/Lib_MCU/inc" -O0 -g3 -gdwarf-4 -Wall -c -fmessage-length=0 -fno-builtin -ffunction-sections -fmerge-constants -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m3 -mthumb -D__NEWLIB__ -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/cr_startup_lpc17.d ./src/cr_startup_lpc17.o ./src/easyweb.d ./src/easyweb.o ./src/ethmac.d ./src/ethmac.o ./src/ew_systick.d ./src/ew_systick.o ./src/tcpip.d ./src/tcpip.o

.PHONY: clean-src

