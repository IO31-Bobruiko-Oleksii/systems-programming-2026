KERNEL := Middlewares/FreeRTOS-Kernel
RTOS_APP_SOURCES := $(wildcard rtos/src/*.c)
KERNEL_SOURCES := $(KERNEL)/tasks.c $(KERNEL)/queue.c $(KERNEL)/list.c \
                  $(KERNEL)/portable/GCC/ARM_CM0/port.c $(KERNEL)/portable/GCC/ARM_CM0/portasm.c \
                  $(KERNEL)/portable/MemMang/heap_1.c

include build.mk

RTOS_APP_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(RTOS_APP_SOURCES:.c=.o)))
KERNEL_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(KERNEL_SOURCES:.c=.o)))

vpath %.c $(sort $(dir $(RTOS_APP_SOURCES) $(KERNEL_SOURCES)))
C_DEFS += -DUSE_FREERTOS $(RTOS_DEFS)
C_INCLUDES += -Irtos/inc -I$(KERNEL)/include -I$(KERNEL)/portable/GCC/ARM_CM0
OBJECTS += $(RTOS_APP_OBJECTS) $(KERNEL_OBJECTS)
$(BUILD_DIR)/$(TARGET).elf: $(RTOS_APP_OBJECTS) $(KERNEL_OBJECTS)

$(RTOS_APP_OBJECTS): CFLAGS += -Wextra -Werror