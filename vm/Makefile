# Makefile for the F-PC native VM
CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -rdynamic
OBJS    = main.o vm.o seed.o dos.o screen.o image.o am.o jit.o float.o cp437.o sljitLir.o host_batch.o host_tty.o host_sdl.o

# SDL window host (fpc --sdl), when SDL2 and zlib are available
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 zlib 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 zlib 2>/dev/null)
ifneq ($(SDL_LIBS),)
CFLAGS  += -DHAVE_SDL
LIBS    += $(SDL_LIBS)
endif

fpc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lpthread -lm $(LIBS)

$(OBJS): vm.h prims.def seed.h amops.def

# the full F-PC system image, as EXTEND.BAT made F-PC.EXE
image: fpc
	cd ../SRC && ./../vm/fpc --batch - FLOAD F-PC.SEQ FSAVE ../F-PC BYE </dev/null >/dev/null

# load every TOOLS/*.SEQ add-on on the full system, each on its own
test-tools: fpc image
	../tools/loadtools.sh

# interactive UI scenarios in a pseudo-terminal (needs python pyte)
test-ui: fpc image
	../tools/runtests-ui.sh

sljitLir.o: sljit/sljitLir.c
	$(CC) $(CFLAGS) -w -DSLJIT_CONFIG_AUTO=1 -c -o $@ sljit/sljitLir.c

host_sdl.o: host_sdl.c vm.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ host_sdl.c

jit.o: jit.c vm.h amops.def
	$(CC) $(CFLAGS) -DSLJIT_CONFIG_AUTO=1 -c -o $@ jit.c

test: fpc
	../tools/runtests-native.sh
	cd ../SRC && ../vm/fpc --batch - SAVE-IMAGE ../vm/test.img BYE </dev/null >/dev/null
	FPC_IMAGE=$(CURDIR)/test.img ../tools/runtests-native.sh
	FPC_NOJIT=1 ../tools/runtests-native.sh
	rm -f test.img

# install under $(PREFIX): the tree goes to $(PREFIX)/lib/fpc (fpc finds
# its sources in the directory above its executable); bin/fpc is the VM
# (bare kernel by default), bin/f-pc starts the full system image
PREFIX ?= /usr/local
LIBDIR  = $(PREFIX)/lib/fpc
install: fpc image
	mkdir -p $(DESTDIR)$(LIBDIR)/bin $(DESTDIR)$(PREFIX)/bin
	cp fpc $(DESTDIR)$(LIBDIR)/bin/fpc
	cp ../F-PC.IMG $(DESTDIR)$(LIBDIR)/
	cp -r ../SRC ../HLP ../TOOLS $(DESTDIR)$(LIBDIR)/
	ln -sf ../lib/fpc/bin/fpc $(DESTDIR)$(PREFIX)/bin/fpc
	printf '#!/bin/sh\nexec "%s/bin/fpc" -i "%s/F-PC.IMG" "$$@"\n' $(LIBDIR) $(LIBDIR) > $(DESTDIR)$(PREFIX)/bin/f-pc
	chmod +x $(DESTDIR)$(PREFIX)/bin/f-pc

clean:
	rm -f fpc $(OBJS) test.img

.PHONY: test test-ui clean image
