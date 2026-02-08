#ifndef ALCC_TEMPLATE_H
#define ALCC_TEMPLATE_H

#include "../plugin/alcc_plugin.h"
#include <stdio.h>

extern "C" {
#include "lua.h"
#include "lobject.h"
}

// Interface for Assembly Templates
class AlccTemplate {
public:
    virtual ~AlccTemplate() {}

    // Name of the template (e.g., "default", "template2")
    virtual const char* get_name() const = 0;

    // Disassemble a function (Proto) to standard output
    virtual void disassemble(Proto* p, AlccPlugin* plugin) = 0;

    // Assemble a function (Proto) from input context
    virtual Proto* assemble(lua_State* L, ParseCtx* ctx, AlccPlugin* plugin) = 0;
};

#endif
