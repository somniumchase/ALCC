#ifndef ALCC_PLUGIN_H
#define ALCC_PLUGIN_H

#include "lua.h"
#include "lobject.h" // For Proto

typedef struct {
    FILE* f;
    int line_no;
    char buffer[4096];
} ParseCtx;

typedef struct {
    const char* name;
    // Called after bytecode is loaded but before processing
    void (*post_load)(lua_State* L, Proto* p);

    // Called for each instruction during disassembly
    int (*on_instruction)(Proto* p, int pc, char* out_buffer, size_t buffer_size);

    // Called before printing function header
    void (*on_disasm_header)(Proto* p);

    // Called when assembler reads a line
    void (*on_asm_line)(ParseCtx* ctx, char* line);

    // Called when decompiling an instruction. Return 1 if handled.
    int (*on_decompile_inst)(Proto* p, int pc, char* out_buffer, size_t buffer_size);
} AlccPlugin;

typedef AlccPlugin* (*alcc_plugin_init_fn)(void);

#endif
