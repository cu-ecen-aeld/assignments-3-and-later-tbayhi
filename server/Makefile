CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror
LDFLAGS ?= -pthread

aesdsocket: aesdsocket.o

aesdsocket.o: aesdsocket.c

.PHONY: clean, all, default

all: aesdsocket

default: all

clean:
	rm -f aesdsocket.o
	rm -f aesdsocket
