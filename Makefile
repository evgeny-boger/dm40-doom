# Doom for the Alientek DM40 (native image for the vendor FS-loader).
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size
BUILD = build
CPU = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS = $(CPU) -O2 -g -ffunction-sections -fdata-sections -fno-common \
	-std=gnu11 -Wall -Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-unused-function -Wno-misleading-indentation \
	-Iport -Idoom/include
LDFLAGS = $(CPU) -T link.ld -nostartfiles -Wl,--gc-sections \
	-Wl,-Map=$(BUILD)/dm40-doom.map --specs=nano.specs --specs=nosys.specs
SRC = $(wildcard doom/source/*.c) port/main.c port/hal.c port/startup.c \
	port/graphics.c port/font8x8.c port/printf.c port/stubs.c port/input.c
OBJ = $(patsubst %.c,$(BUILD)/%.o,$(SRC))

all: $(BUILD)/dm40-doom.elf $(BUILD)/dm40-doom.bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/dm40-doom.elf: $(OBJ) link.ld
	$(CC) $(LDFLAGS) $(OBJ) -o $@ -lm
	$(SIZE) $@

$(BUILD)/dm40-doom.bin: $(BUILD)/dm40-doom.elf
	$(OBJCOPY) -O binary --gap-fill=0xff $< $@

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)
