### CROSS_COMPILE setup copied from BusyBox project: https://git.busybox.net/busybox/tree/Makefile
TARGET ?= aesdsocket
USE_AESD_CHAR_DEVICE ?= -DUSE_AESD_CHAR_DEVICE

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -lpthread -lrt

CPPFLAGS ?= $(USE_AESD_CHAR_DEVICE)

all: $(TARGET)

$(TARGET): $(TARGET).o
	$(CC) -o $(TARGET) $(TARGET).o $(CFLAGS) $(LDFLAGS)

$(TARGET).o: $(TARGET).c
	$(CC) $(CPPFLAGS) -c $(TARGET).c -o $(TARGET).o $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(TARGET) $(TARGET).o
