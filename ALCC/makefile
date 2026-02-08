CXX=g++
CXXFLAGS=-O2 -Wall -I../lua_source -Isrc/core -Isrc/plugin -std=c++17 -DLUA_USE_LINUX -Wno-unused-function
LDFLAGS=-L../lua_source -llua -lm -ldl

ALL_TOOLS=alcc-c alcc-d alcc-a alcc-dec
CORE_OBJ=src/core/alcc_utils.o src/backend/lua55.o
PLUGIN_SRC=plugins/sample_plugin.cpp
PLUGIN_SO=plugins/sample_plugin.so

all: $(ALL_TOOLS) $(PLUGIN_SO)

src/core/alcc_utils.o: src/core/alcc_utils.cpp src/core/alcc_utils.h src/core/alcc_backend.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/backend/lua55.o: src/backend/lua55.cpp src/core/alcc_backend.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

alcc-c: src/compiler.cpp $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc-d: src/disassembler.cpp $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc-a: src/assembler.cpp $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc-dec: src/decompiler.cpp $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(PLUGIN_SO): $(PLUGIN_SRC)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $< $(LDFLAGS)

clean:
	rm -f $(ALL_TOOLS) src/core/*.o src/backend/*.o plugins/*.so
