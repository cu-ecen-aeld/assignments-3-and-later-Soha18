ifeq ($(CROSS_COMPILE),)
CC = gcc
else
CC = $(CROSS_COMPILE)gcc
endif

CFLAGS = -Wall -Werror -g
TARGET = aesdsocket
SRC = aesdsocket.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

