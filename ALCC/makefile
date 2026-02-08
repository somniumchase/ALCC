CC=gcc
CFLAGS=-O2 -Wall -I../lua_source -std=c99 -DLUA_USE_LINUX -Wno-unused-function
LDFLAGS=-L../lua_source -llua -lm -ldl

ALL_TOOLS=alcc-c alcc-d alcc-a alcc-dec

all: $(ALL_TOOLS)

alcc-c: src/compiler.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

alcc-d: src/disassembler.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

alcc-a: src/assembler.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

alcc-dec: src/decompiler.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(ALL_TOOLS)
