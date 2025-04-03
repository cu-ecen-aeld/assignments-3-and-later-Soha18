SRC = aesdsocket.c
BIN = aesdsocket
DESTDIR = /usr/bin

TARGET ?= aesdsocket
USE_AESD_CHAR_DEVICE ?= 1

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -pthread -lrt

ifeq ($(USE_AESD_CHAR_DEVICE), 1)
    CFLAGS += -DUSE_AESD_CHAR_DEVICE
endif

all: $(TARGET)

$(TARGET): $(TARGET).o
	$(CC) -o $(TARGET) $(TARGET).o $(CFLAGS) $(LDFLAGS)

$(TARGET).o: $(TARGET).c
	$(CC) -c $(TARGET).c -o $(TARGET).o $(CFLAGS)

install: $(BIN)
	install -m 755 $(BIN) $(DESTDIR)
.PHONY: clean
clean:
	rm -f $(TARGET) $(TARGET).o
