# BBcall_APRS firmware 构建（arm-none-eabi-gcc + STM32CubeF1 HAL）
# 说明：本机未安装工具链时，请在装有 arm-none-eabi-gcc 与 CubeF1 的机器上执行。

# ---- 工具链 ----
CC       = arm-none-eabi-gcc
OBJCOPY  = arm-none-eabi-objcopy
SIZE     = arm-none-eabi-size

# ---- 目标芯片 ----
MCU      = -mcpu=cortex-m3 -mthumb
DEFS     = -DSTM32F103xB

# ---- HAL / CMSIS / 启动 / 链接（按你的路径修改） ----
HAL_DIR    ?= $(HOME)/STM32Cube/Repository/STM32Cube_FW_F1_V1.8.5
CMSIS_DIR  ?= $(HAL_DIR)/Drivers/CMSIS
STARTUP    ?= $(CMSIS_DIR)/Device/ST/STM32F1xx/Source/Templates/gcc/startup_stm32f103xb.s
LINKER     ?= $(CMSIS_DIR)/Device/ST/STM32F1xx/Source/Templates/gcc/linker/STM32F103C8Tx_FLASH.ld

# ---- 源文件 ----
APP_SRC = Src/main.c Src/bk4802.c Src/lcd_st7567.c Src/modem.c Src/ax25.c Src/aprs.c
HAL_SRC = $(wildcard $(HAL_DIR)/Drivers/STM32F1xx_HAL_Driver/Src/*.c)
CMSIS_SRC = $(CMSIS_DIR)/Device/ST/STM32F1xx/Source/Templates/system_stm32f1xx.c

SRCS = $(APP_SRC) $(HAL_SRC) $(CMSIS_SRC) $(STARTUP)

# ---- 头文件 ----
INCS  = -IInc \
        -I$(HAL_DIR)/Drivers/STM32F1xx_HAL_Driver/Inc \
        -I$(HAL_DIR)/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy \
        -I$(CMSIS_DIR)/Include \
        -I$(CMSIS_DIR)/Device/ST/STM32F1xx/Include

CFLAGS = $(MCU) $(DEFS) $(INCS) -Os -ffunction-sections -fdata-sections -Wall -Wextra
LFLAGS = $(MCU) -T$(LINKER) -Wl,--gc-sections -Wl,-Map=build/bbcall_aprs.map -lc -lm

OBJDIR = build
OBJS   = $(addprefix $(OBJDIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all clean flash

all: $(OBJDIR)/bbcall_aprs.elf $(OBJDIR)/bbcall_aprs.bin

$(OBJDIR)/bbcall_aprs.elf: $(OBJS)
	@mkdir -p $(OBJDIR)
	$(CC) $(OBJS) $(LFLAGS) -o $@
	$(SIZE) $@

$(OBJDIR)/bbcall_aprs.bin: $(OBJDIR)/bbcall_aprs.elf
	$(OBJCOPY) -O binary $< $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

define compile_c
$(OBJDIR)/$(notdir $(basename $(1))).o: $(1) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $$< -o $$@
endef
$(foreach s,$(filter %.c,$(SRCS)),$(eval $(call compile_c,$(s))))

define compile_s
$(OBJDIR)/$(notdir $(basename $(1))).o: $(1) | $(OBJDIR)
	$(CC) $(MCU) -c $$< -o $$@
endef
$(foreach s,$(filter %.s,$(SRCS)),$(eval $(call compile_s,$(s))))

flash: $(OBJDIR)/bbcall_aprs.bin
	@echo "用 ST-Link/OpenOCD 烧录："
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program $(OBJDIR)/bbcall_aprs.bin 0x08000000 verify reset exit"

clean:
	rm -rf $(OBJDIR)
