SRC = aesdsocket.c
#BIN = aesdsocket
#DESTDIR = /usr/bin

TARGET ?= aesdsocket
USE_AESD_CHAR_DEVICE ?= -DUSE_AESD_CHAR_DEVICE

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -pthread -lrt

CPPFLAGS ?= $(USE_AESD_CHAR_DEVICE)

all: $(TARGET)

$(TARGET): $(TARGET).o
	$(CC) -o $(TARGET) $(TARGET).o $(CFLAGS) $(LDFLAGS)

$(TARGET).o: $(TARGET).c
	$(CC) $(CPPFLAGS) -c $(TARGET).c -o $(TARGET).o $(CFLAGS)

install: $(BIN)
	install -m 755 $(BIN) $(DESTDIR)
.PHONY: clean
clean:
	rm -f $(TARGET) $(TARGET).o
