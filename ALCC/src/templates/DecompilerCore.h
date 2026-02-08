#ifndef DECOMPILER_CORE_H
#define DECOMPILER_CORE_H

#include "../plugin/alcc_plugin.h"
#include "../ast/AST.h"

extern "C" {
#include "lua.h"
#include "lobject.h"
}

class DecompilerCore {
public:
    static void decompile(Proto* p, int level, AlccPlugin* plugin, const char* name_override = NULL);
    static ASTNode* build_ast(Proto* p, AlccPlugin* plugin);
};

#endif
