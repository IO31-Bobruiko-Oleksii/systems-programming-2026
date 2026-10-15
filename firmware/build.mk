LAYER_SOURCES := \
platform/src/plat_hal.c \
devices/src/servo.c

include Makefile

LAYER_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(LAYER_SOURCES:.c=.o)))

vpath %.c $(sort $(dir $(LAYER_SOURCES)))
C_INCLUDES += -Iplatform/inc -Idevices/inc
OBJECTS += $(LAYER_OBJECTS)
$(BUILD_DIR)/$(TARGET).elf: $(LAYER_OBJECTS)

$(LAYER_OBJECTS): CFLAGS += -Wextra -Werror
$(BUILD_DIR)/main.o: CFLAGS += -Wextra
