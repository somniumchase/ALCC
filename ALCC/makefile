CXX=g++
CXXFLAGS=-O2 -Wall -I../lua_source -Isrc/core -Isrc/plugin -Isrc/templates -std=c++17 -DLUA_USE_LINUX -Wno-unused-function
LDFLAGS=-L../lua_source -llua -lm -ldl

ALL_TOOLS=alcc-c alcc-d alcc-a alcc-dec alcc
CORE_OBJ=src/core/alcc_utils.o src/backend/lua55.o
TEMPLATE_OBJ=src/templates/TemplateFactory.o src/templates/DefaultTemplate.o src/templates/Template2.o src/templates/DecompilerCore.o
PLUGIN_SRC=plugins/sample_plugin.cpp
PLUGIN_SO=plugins/sample_plugin.so

all: $(ALL_TOOLS) $(PLUGIN_SO)

src/core/alcc_utils.o: src/core/alcc_utils.cpp src/core/alcc_utils.h src/core/alcc_backend.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/backend/lua55.o: src/backend/lua55.cpp src/core/alcc_backend.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/templates/TemplateFactory.o: src/templates/TemplateFactory.cpp src/templates/TemplateFactory.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/templates/DefaultTemplate.o: src/templates/DefaultTemplate.cpp src/templates/DefaultTemplate.h src/templates/AlccTemplate.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/templates/Template2.o: src/templates/Template2.cpp src/templates/Template2.h src/templates/AlccTemplate.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/templates/DecompilerCore.o: src/templates/DecompilerCore.cpp src/templates/DecompilerCore.h src/core/alcc_utils.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

alcc-c: src/compiler.cpp $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc-d: src/disassembler.cpp $(CORE_OBJ) $(TEMPLATE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc-a: src/assembler.cpp $(CORE_OBJ) $(TEMPLATE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc-dec: src/decompiler.cpp $(CORE_OBJ) $(TEMPLATE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc: src/main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

$(PLUGIN_SO): $(PLUGIN_SRC)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $< $(LDFLAGS)

clean:
	rm -f $(ALL_TOOLS) src/core/*.o src/backend/*.o src/templates/*.o plugins/*.so
