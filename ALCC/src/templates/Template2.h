#ifndef TEMPLATE2_H
#define TEMPLATE2_H

#include "AlccTemplate.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lstring.h"
#include "lmem.h"
}

#include "../core/alcc_utils.h"
#include "../core/alcc_backend.h"

class Template2 : public AlccTemplate {
public:
    const char* get_name() const override { return "template2"; }

    void disassemble(Proto* p, AlccPlugin* plugin) override;
    Proto* assemble(lua_State* L, ParseCtx* ctx, AlccPlugin* plugin) override;
    void decompile(Proto* p, int level, AlccPlugin* plugin) override;

private:
    void print_proto(Proto* p, int level, AlccPlugin* plugin);
    void print_code(Proto* p, int level, AlccPlugin* plugin);

    char* get_line(ParseCtx* ctx, AlccPlugin* plugin);
    void parse_error(ParseCtx* ctx, const char* fmt, ...);
    char* find_line_starting_with(ParseCtx* ctx, AlccPlugin* plugin, const char* prefix);
};

#endif
