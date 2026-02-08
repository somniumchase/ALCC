#ifndef ALCC_PLUGIN_H
#define ALCC_PLUGIN_H

#include "lua.h"
#include "lobject.h" // For Proto

typedef struct {
    const char* name;
    // Called after bytecode is loaded but before processing
    void (*post_load)(lua_State* L, Proto* p);

    // Called for each instruction during disassembly
    // If it returns 1, it assumes the instruction is printed to out_buffer
    // If it returns 0, default printing is used
    int (*on_instruction)(Proto* p, int pc, char* out_buffer, size_t buffer_size);
} AlccPlugin;

typedef AlccPlugin* (*alcc_plugin_init_fn)(void);

#endif
