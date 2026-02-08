#ifndef DECOMPILER_CORE_H
#define DECOMPILER_CORE_H

#include "../plugin/alcc_plugin.h"

extern "C" {
#include "lua.h"
#include "lobject.h"
}

class DecompilerCore {
public:
    static void decompile(Proto* p, int level, AlccPlugin* plugin);
};

#endif
