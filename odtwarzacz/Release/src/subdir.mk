################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/D03.c \
../src/cr_startup_lpc17.c 

C_DEPS += \
./src/D03.d \
./src/cr_startup_lpc17.d 

OBJS += \
./src/D03.o \
./src/cr_startup_lpc17.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -DNDEBUG -D__USE_CMSIS=CMSISv1p30_LPC17xx -D__CODE_RED -D__NEWLIB__ -I"/Users/piotrwlazlo/Desktop/studia/4semestr/SystemyWbudowane/lpc17xx_xpr_bb_140609/Lib_CMSISv1p30_LPC17xx/inc" -I"/Users/piotrwlazlo/Desktop/studia/4semestr/SystemyWbudowane/lpc17xx_xpr_bb_140609/Lib_EaBaseBoard/inc" -I"/Users/piotrwlazlo/Desktop/studia/4semestr/SystemyWbudowane/lpc17xx_xpr_bb_140609/Lib_FatFs_SD/inc" -I"/Users/piotrwlazlo/Desktop/studia/4semestr/SystemyWbudowane/lpc17xx_xpr_bb_140609/Lib_MCU/inc" -Os -Os -g3 -gdwarf-4 -Wall -c -fmessage-length=0 -fno-builtin -ffunction-sections -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m3 -mthumb -D__NEWLIB__ -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/D03.d ./src/D03.o ./src/cr_startup_lpc17.d ./src/cr_startup_lpc17.o

.PHONY: clean-src

