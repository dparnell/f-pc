# Makefile for the F-PC native VM
CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
OBJS    = main.o vm.o seed.o dos.o screen.o image.o am.o host_batch.o host_tty.o

fpc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lpthread

$(OBJS): vm.h prims.def seed.h amops.def

test: fpc
	../tools/runtests-native.sh
	cd ../SRC && ../vm/fpc --batch - SAVE-IMAGE ../vm/test.img BYE </dev/null >/dev/null
	FPC_IMAGE=$(CURDIR)/test.img ../tools/runtests-native.sh
	rm -f test.img

clean:
	rm -f fpc $(OBJS) test.img

.PHONY: test clean
