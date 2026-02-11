CXX=g++
LUA_VER ?= 5.5

ifeq ($(LUA_VER), 5.3)
  CXXFLAGS=-O2 -Wall -I../lua53_source -Isrc/core -Isrc/plugin -Isrc/templates -std=c++17 -DLUA_USE_LINUX -Wno-unused-function -DLUA_53 -DLUA_COMPAT_5_2
  READLINE_LIBS=-lreadline
  ifdef NO_READLINE
    READLINE_LIBS=
  endif
  LDFLAGS=-L../lua53_source -llua -lm -ldl $(READLINE_LIBS)
  SUFFIX=-5.3
  BACKEND_OBJ=src/backend/lua53.o
  TOOL_SUFFIX=\"$(SUFFIX)\"
else ifeq ($(LUA_VER), 5.3.3)
  CXXFLAGS=-O2 -Wall -I../androlua533_source -Isrc/core -Isrc/plugin -Isrc/templates -std=c++17 -DLUA_USE_LINUX -Wno-unused-function -DLUA_53 -DLUA_COMPAT_5_2 -DANDROLUA
  READLINE_LIBS=-lreadline
  ifdef NO_READLINE
    READLINE_LIBS=
  endif
  LDFLAGS=-L../androlua533_source -llua -lm -ldl $(READLINE_LIBS)
  SUFFIX=-5.3.3
  BACKEND_OBJ=src/backend/androlua533.o
  TOOL_SUFFIX=\"$(SUFFIX)\"
else ifeq ($(LUA_VER), 5.2)
  CXXFLAGS=-O2 -Wall -I../lua52_source/src -Isrc/core -Isrc/plugin -Isrc/templates -std=c++17 -DLUA_USE_LINUX -Wno-unused-function -DLUA_52
  READLINE_LIBS=-lreadline
  ifdef NO_READLINE
    READLINE_LIBS=
  endif
  LDFLAGS=-L../lua52_source/src -llua -lm -ldl $(READLINE_LIBS)
  SUFFIX=-5.2
  BACKEND_OBJ=src/backend/lua52.o
  TOOL_SUFFIX=\"$(SUFFIX)\"
else ifeq ($(LUA_VER), 5.4)
  CXXFLAGS=-O2 -Wall -I../lua54_source/src -Isrc/core -Isrc/plugin -Isrc/templates -std=c++17 -DLUA_USE_LINUX -Wno-unused-function -DLUA_54
  READLINE_LIBS=-lreadline
  ifdef NO_READLINE
    READLINE_LIBS=
  endif
  LDFLAGS=-L../lua54_source/src -llua -lm -ldl $(READLINE_LIBS)
  SUFFIX=-5.4
  BACKEND_OBJ=src/backend/lua54.o
  TOOL_SUFFIX=\"$(SUFFIX)\"
else
  CXXFLAGS=-O2 -Wall -I../lua_source -Isrc/core -Isrc/plugin -Isrc/templates -std=c++17 -DLUA_USE_LINUX -Wno-unused-function
  LDFLAGS=-L../lua_source -llua -lm -ldl
  SUFFIX=
  BACKEND_OBJ=src/backend/lua55.o
  TOOL_SUFFIX=\"\"
endif

ALL_TOOLS=alcc-c$(SUFFIX) alcc-d$(SUFFIX) alcc-a$(SUFFIX) alcc-dec$(SUFFIX) alcc$(SUFFIX)
CORE_OBJ=src/core/alcc_utils.o $(BACKEND_OBJ)
AST_OBJ=src/ast/AST.o src/ast/ASTPrinter.o
TEMPLATE_OBJ=src/templates/TemplateFactory.o src/templates/DefaultTemplate.o src/templates/Template2.o src/templates/DecompilerCore.o $(AST_OBJ)
PLUGIN_SRC=plugins/sample_plugin.cpp
PLUGIN_SO=plugins/sample_plugin.so

all: $(ALL_TOOLS) $(PLUGIN_SO)

src/core/alcc_utils.o: src/core/alcc_utils.cpp src/core/alcc_utils.h src/core/alcc_backend.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/backend/lua55.o: src/backend/lua55.cpp src/core/alcc_backend.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/backend/lua53.o: src/backend/lua53.cpp src/core/alcc_backend.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/backend/lua54.o: src/backend/lua54.cpp src/core/alcc_backend.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/backend/androlua533.o: src/backend/lua53.cpp src/core/alcc_backend.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/backend/lua52.o: src/backend/lua52.cpp src/core/alcc_backend.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/templates/TemplateFactory.o: src/templates/TemplateFactory.cpp src/templates/TemplateFactory.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/templates/DefaultTemplate.o: src/templates/DefaultTemplate.cpp src/templates/DefaultTemplate.h src/templates/AlccTemplate.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/templates/Template2.o: src/templates/Template2.cpp src/templates/Template2.h src/templates/AlccTemplate.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/templates/DecompilerCore.o: src/templates/DecompilerCore.cpp src/templates/DecompilerCore.h src/core/alcc_utils.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/ast/AST.o: src/ast/AST.cpp src/ast/AST.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/ast/ASTPrinter.o: src/ast/ASTPrinter.cpp src/ast/ASTPrinter.h src/ast/AST.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

alcc-c$(SUFFIX): src/compiler.cpp $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc-d$(SUFFIX): src/disassembler.cpp $(CORE_OBJ) $(TEMPLATE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc-a$(SUFFIX): src/assembler.cpp $(CORE_OBJ) $(TEMPLATE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc-dec$(SUFFIX): src/decompiler.cpp $(CORE_OBJ) $(TEMPLATE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

alcc$(SUFFIX): src/main.cpp
	$(CXX) $(CXXFLAGS) -DTOOL_SUFFIX=$(TOOL_SUFFIX) -o $@ $<

$(PLUGIN_SO): $(PLUGIN_SRC)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $< $(LDFLAGS)

clean:
	rm -f $(ALL_TOOLS) src/core/*.o src/backend/*.o src/templates/*.o src/ast/*.o plugins/*.so alcc-* alcc
