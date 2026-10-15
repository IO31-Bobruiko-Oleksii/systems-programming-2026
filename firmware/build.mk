LAYER_SOURCES := $(wildcard platform/src/*.c devices/src/*.c app/src/*.c)

include Makefile

LAYER_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(LAYER_SOURCES:.c=.o)))

vpath %.c $(sort $(dir $(LAYER_SOURCES)))
C_INCLUDES += -Iplatform/inc -Idevices/inc -Iapp/inc
OBJECTS += $(LAYER_OBJECTS)
$(BUILD_DIR)/$(TARGET).elf: $(LAYER_OBJECTS)

$(LAYER_OBJECTS): CFLAGS += -Wextra -Werror
$(BUILD_DIR)/main.o: CFLAGS += -Wextra
