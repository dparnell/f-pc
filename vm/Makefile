# Makefile for the F-PC native VM
CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
OBJS    = main.o vm.o seed.o dos.o host_batch.o

fpc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJS): vm.h prims.def seed.h

test: fpc
	../tools/runtests-native.sh

clean:
	rm -f fpc $(OBJS)

.PHONY: test clean
