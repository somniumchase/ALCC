CC=gcc
CFLAGS=-O2 -Wall -I../lua_source -Isrc/core -Isrc/plugin -std=c99 -DLUA_USE_LINUX -Wno-unused-function
LDFLAGS=-L../lua_source -llua -lm -ldl

ALL_TOOLS=alcc-c alcc-d alcc-a alcc-dec
CORE_OBJ=src/core/alcc_utils.o src/backend/lua55.o
PLUGIN_SRC=plugins/sample_plugin.c
PLUGIN_SO=plugins/sample_plugin.so

all: $(ALL_TOOLS) $(PLUGIN_SO)

src/core/alcc_utils.o: src/core/alcc_utils.c src/core/alcc_utils.h src/core/alcc_backend.h
	$(CC) $(CFLAGS) -c -o $@ $<

src/backend/lua55.o: src/backend/lua55.c src/core/alcc_backend.h
	$(CC) $(CFLAGS) -c -o $@ $<

alcc-c: src/compiler.c $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

alcc-d: src/disassembler.c $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

alcc-a: src/assembler.c $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

alcc-dec: src/decompiler.c $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(PLUGIN_SO): $(PLUGIN_SRC)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< $(LDFLAGS)

clean:
	rm -f $(ALL_TOOLS) src/core/*.o src/backend/*.o plugins/*.so
