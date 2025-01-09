# Variables
ifeq ($(CROSS_COMPILE),)
CC = gcc
else
CC = $(CROSS_COMPILE)gcc
endif

CFLAGS = -Wall -Wextra -O2
TARGET = writer
SRC = writer.c
OBJ = $(SRC:.c=.o)

# Default target
.PHONY: all
all: $(TARGET)

# Build target
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compile object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJ)

# Debugging rule for compiler settings
.PHONY: print-cc
print-cc:
	@echo "Using compiler: $(CC)"

